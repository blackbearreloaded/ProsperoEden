// ProsperoEden - The launcher's text: a baked SDF font, the console's fonts for other scripts.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pe/gfx/font.hpp"

#include "pe/gfx/bidi.hpp"
#include "pe/gfx/system_fonts.hpp"

#include <algorithm>
#include <climits>
#include <cstring>
#include <functional>
#include <unordered_map>
#include <utility>

namespace pe::gfx
{

namespace ff = font_format;

namespace
{

// System-font glyphs are drawn with the em at this many pixels; their distance fields keep the
// baked atlas's ratio of spread to em, so one shader setting serves both.
constexpr float kSystemPixelSize = 48.0f;
// The atlas is this tall with the system fonts' rows (about 1,300 Japanese glyphs fit).
constexpr int kAtlasHeight = 4096;
// Shaped lines kept; the launcher redraws the same text every frame.
constexpr std::size_t kLineLimit = 4096;

// Characters that take no room and draw nothing: joiners, direction marks, the zero-width
// space that allows a line break, variation selectors, the soft hyphen.
bool invisible(char32_t c)
{
    return (c >= 0x200b && c <= 0x200f) || (c >= 0x202a && c <= 0x202e) || (c >= 0x2060 && c <= 0x2069) ||
           (c >= 0xfe00 && c <= 0xfe0f) || c == 0xfeff || c == 0x00ad || c == 0x061c;
}

// Arabic letters join: no letter-spacing between them.
bool cursive(char32_t c)
{
    return (c >= 0x0600 && c <= 0x06ff) || (c >= 0x0750 && c <= 0x077f) || (c >= 0xfb50 && c <= 0xfdff) ||
           (c >= 0xfe70 && c <= 0xfeff);
}

// Japanese and Chinese text is written without spaces: a line may break between its characters.
// (Korean separates its words with spaces and is broken there.)
bool ideographic(char32_t c)
{
    return (c >= 0x2e80 && c <= 0x30ff) || (c >= 0x31f0 && c <= 0x31ff) || (c >= 0x3400 && c <= 0x4dbf) ||
           (c >= 0x4e00 && c <= 0x9fff) || (c >= 0xf900 && c <= 0xfaff) || (c >= 0xfe30 && c <= 0xfe4f) ||
           (c >= 0xff00 && c <= 0xff60) || (c >= 0xffe0 && c <= 0xffe6) || (c >= 0x20000 && c <= 0x2fa1f);
}

// What may not start a line: closing punctuation, the long vowel mark, iteration marks, small kana.
constexpr char32_t kNoStart[] = {
    ')',    ']',    '}',    ',',    '.',    '!',    '?',    ':',    ';',    '%',    0x00bb, 0x2019, 0x201d,
    0x2025, 0x2026, 0x3001, 0x3002, 0x3005, 0x3009, 0x300b, 0x300d, 0x300f, 0x3011, 0x3015, 0x3017, 0x3019,
    0x301f, 0x3041, 0x3043, 0x3045, 0x3047, 0x3049, 0x3063, 0x3083, 0x3085, 0x3087, 0x308e, 0x309d, 0x309e,
    0x30a1, 0x30a3, 0x30a5, 0x30a7, 0x30a9, 0x30c3, 0x30e3, 0x30e5, 0x30e7, 0x30ee, 0x30f5, 0x30f6, 0x30fb,
    0x30fc, 0x30fd, 0x30fe, 0xff01, 0xff05, 0xff09, 0xff0c, 0xff0e, 0xff1a, 0xff1b, 0xff1f, 0xff3d, 0xff5d,
};
// What may not end a line: opening brackets and quotes.
constexpr char32_t kNoEnd[] = {
    '(',    '[',    '{',    0x00ab, 0x2018, 0x201c, 0x3008, 0x300a, 0x300c, 0x300e,
    0x3010, 0x3014, 0x3016, 0x3018, 0x301d, 0xff08, 0xff3b, 0xff5b,
};

template <std::size_t N> bool among(const char32_t (&set)[N], char32_t c)
{
    return std::find(std::begin(set), std::end(set), c) != std::end(set);
}

bool breaks_between(char32_t before, char32_t after)
{
    if (!ideographic(before) && !ideographic(after))
        return false;
    return !among(kNoStart, after) && !among(kNoEnd, before) && !bidi::attaches(after) && after != ' ';
}

struct TextHash
{
    using is_transparent = void;
    std::size_t operator()(std::string_view text) const
    {
        return std::hash<std::string_view>{}(text);
    }
};

} // namespace

std::uint32_t next_codepoint(std::string_view text, std::size_t *index)
{
    const auto byte = [&](std::size_t at) { return static_cast<unsigned char>(text[at]); };
    const std::size_t i = *index;
    const unsigned char lead = byte(i);
    int length = 1;
    std::uint32_t value = lead;
    if (lead >= 0xf0 && lead < 0xf8)
    {
        length = 4;
        value = lead & 0x07u;
    }
    else if (lead >= 0xe0)
    {
        length = 3;
        value = lead & 0x0fu;
    }
    else if (lead >= 0xc0)
    {
        length = 2;
        value = lead & 0x1fu;
    }
    else if (lead >= 0x80)
    {
        *index = i + 1;
        return 0xfffd;
    }
    if (i + static_cast<std::size_t>(length) > text.size())
    {
        *index = text.size();
        return 0xfffd;
    }
    for (int k = 1; k < length; ++k)
    {
        const unsigned char continuation = byte(i + static_cast<std::size_t>(k));
        if ((continuation & 0xc0u) != 0x80u)
        {
            *index = i + 1;
            return 0xfffd;
        }
        value = (value << 6) | (continuation & 0x3fu);
    }
    *index = i + static_cast<std::size_t>(length);
    return value;
}

bool joins_without_space(std::string_view first, std::string_view second)
{
    if (first.empty() || second.empty())
        return true;
    std::size_t last = first.size() - 1;
    while (last > 0 && (static_cast<unsigned char>(first[last]) & 0xc0u) == 0x80u)
        --last;
    std::size_t index = last;
    const char32_t before = next_codepoint(first, &index);
    index = 0;
    const char32_t after = next_codepoint(second, &index);
    return ideographic(before) || ideographic(after);
}

// ---- system-font text ----

// A shaped line: its glyphs in drawing order, positions in em from the line's left edge.
struct Font::Line
{
    struct Glyph
    {
        std::int16_t face;  // -1: the baked font, id is a code point; else a system font, id is its glyph
        std::uint16_t gaps; // letter-spacing gaps to the left of this glyph
        std::uint32_t id;
        float x; // the pen
        float y; // below the baseline
    };
    std::vector<Glyph> glyphs;
    float advance = 0.0f;
    std::uint16_t gaps = 0;
};

struct Font::Dynamic
{
    // Where a system-font glyph sits in the atlas; w 0: it draws nothing.
    struct Placed
    {
        std::uint16_t x = 0;
        std::uint16_t y = 0;
        std::uint16_t w = 0;
        std::uint16_t h = 0;
        float offset_x = 0.0f;
        float offset_y = 0.0f;
    };

    SystemFonts fonts;
    std::unordered_map<std::uint64_t, Placed> placed;
    std::unordered_map<std::string, Line, TextHash, std::equal_to<>> lines;
    // Shelf packing in the rows under the baked atlas.
    int first_row = 0;
    int pen_x = 1;
    int pen_y = 0;
    int shelf = 0;
    int changed_first = INT_MAX;
    int changed_last = 0;
    bool full = false;      // a glyph of this frame's text found no room
    bool emptying = false;  // the next frame starts with empty rows
    std::vector<RunGlyph> run;

    void changed(int first, int last)
    {
        changed_first = std::min(changed_first, first);
        changed_last = std::max(changed_last, last);
    }
};

Font::Font() = default;
Font::~Font() = default;

bool Font::load(std::string_view data)
{
    error_.clear();
    if (data.size() < sizeof(ff::Header))
    {
        error_ = "font too small";
        return false;
    }
    std::memcpy(&header_, data.data(), sizeof(header_));
    if (header_.magic != ff::kMagic || header_.version != ff::kVersion ||
        header_.pixel_size <= 0.0f)
    {
        error_ = "not a baked font, version 1";
        return false;
    }
    const std::size_t glyph_bytes =
        static_cast<std::size_t>(header_.glyph_count) * sizeof(ff::Glyph);
    const std::size_t kern_bytes = static_cast<std::size_t>(header_.kern_count) * sizeof(ff::Kern);
    const std::size_t atlas_bytes = static_cast<std::size_t>(header_.atlas_width) *
                                    static_cast<std::size_t>(header_.atlas_height);
    if (data.size() != sizeof(ff::Header) + glyph_bytes + kern_bytes + atlas_bytes)
    {
        error_ = "font size mismatch";
        return false;
    }
    const char *cursor = data.data() + sizeof(ff::Header);
    glyphs_.resize(header_.glyph_count);
    std::memcpy(glyphs_.data(), cursor, glyph_bytes);
    cursor += glyph_bytes;
    kerns_.resize(header_.kern_count);
    std::memcpy(kerns_.data(), cursor, kern_bytes);
    cursor += kern_bytes;
    atlas_.assign(reinterpret_cast<const std::uint8_t *>(cursor),
                  reinterpret_cast<const std::uint8_t *>(cursor) + atlas_bytes);
    for (const ff::Glyph &glyph : glyphs_)
    {
        if (glyph.x + glyph.w > header_.atlas_width || glyph.y + glyph.h > header_.atlas_height)
        {
            error_ = "glyph outside atlas";
            return false;
        }
    }
    // Room under the baked glyphs for the system fonts' (when the baked atlas leaves any).
    atlas_height_ = static_cast<std::uint16_t>(std::max<int>(header_.atlas_height, kAtlasHeight));
    atlas_.resize(static_cast<std::size_t>(header_.atlas_width) * atlas_height_, 0);
    dynamic_ = std::make_unique<Dynamic>();
    dynamic_->first_row = header_.atlas_height;
    dynamic_->pen_y = header_.atlas_height + 1;
    return true;
}

void Font::use_system_fonts(std::vector<std::string> files, std::string_view language)
{
    if (!dynamic_)
        return;
    dynamic_->fonts.set(std::move(files), language);
    dynamic_->lines.clear();
    dynamic_->placed.clear();
    dynamic_->emptying = true;
}

std::string Font::system_fonts_read() const
{
    return dynamic_ ? dynamic_->fonts.loaded() : std::string{"none"};
}

bool Font::can_draw(std::string_view text) const
{
    for (std::size_t index = 0; index < text.size();)
    {
        const char32_t c = next_codepoint(text, &index);
        if (c == '\n' || invisible(c) || find(c) != nullptr)
            continue;
        if (!dynamic_ || dynamic_->fonts.face_for(c) < 0)
            return false;
    }
    return true;
}

std::string Font::drawable(std::string_view text) const
{
    std::string kept;
    for (std::size_t index = 0; index < text.size();)
    {
        const std::size_t start = index;
        const char32_t c = next_codepoint(text, &index);
        if (c == '
' || invisible(c) || find(c) != nullptr || (dynamic_ && dynamic_->fonts.face_for(c) >= 0))
            kept.append(text.substr(start, index - start));
    }
    return kept;
}

const ff::Glyph *Font::find(std::uint32_t codepoint) const
{
    const auto it =
        std::lower_bound(glyphs_.begin(), glyphs_.end(), codepoint,
                         [](const ff::Glyph &g, std::uint32_t c) { return g.codepoint < c; });
    return it != glyphs_.end() && it->codepoint == codepoint ? &*it : nullptr;
}

float Font::kern(std::uint32_t first, std::uint32_t second) const
{
    const auto it = std::lower_bound(
        kerns_.begin(), kerns_.end(), std::make_pair(first, second),
        [](const ff::Kern &k, const std::pair<std::uint32_t, std::uint32_t> &key)
        { return k.first != key.first ? k.first < key.first : k.second < key.second; });
    return it != kerns_.end() && it->first == first && it->second == second ? it->amount : 0.0f;
}

const Font::Line *Font::shaped(std::string_view text) const
{
    if (!dynamic_ || dynamic_->fonts.empty())
        return nullptr;
    // Text the baked font covers keeps the plain path below.
    bool plain = true;
    for (std::size_t index = 0; plain && index < text.size();)
        plain = find(next_codepoint(text, &index)) != nullptr;
    if (plain)
        return nullptr;
    Dynamic &d = *dynamic_;
    if (const auto found = d.lines.find(text); found != d.lines.end())
        return &found->second;
    if (d.lines.size() >= kLineLimit)
        d.lines.clear();

    constexpr std::int16_t kBaked = -1;
    constexpr std::int16_t kNone = -2;
    std::vector<char32_t> characters;
    for (std::size_t index = 0; index < text.size();)
        characters.push_back(next_codepoint(text, &index));
    const std::size_t count = characters.size();
    // Which font draws each character: the baked one when it can, else the first system font
    // that has it; a mark stays in the font of the letter it sits on.
    std::vector<std::int16_t> face(count, kNone);
    for (std::size_t i = 0; i < count; ++i)
    {
        const char32_t c = characters[i];
        const std::int16_t before = i > 0 ? face[i - 1] : kNone;
        if (invisible(c))
        {
            face[i] = before >= 0 ? before : kNone;
            continue;
        }
        if (find(c) != nullptr)
        {
            face[i] = kBaked;
            continue;
        }
        int chosen = d.fonts.face_for(c);
        if (chosen >= 0 && before >= 0 && before != chosen && bidi::attaches(c) && d.fonts.face_has(before, c))
            chosen = before;
        if (chosen < 0)
        {
            characters[i] = '?';
            face[i] = find('?') != nullptr ? kBaked : kNone;
        }
        else
        {
            face[i] = static_cast<std::int16_t>(chosen);
        }
    }
    std::vector<std::uint8_t> levels;
    bidi::resolve(characters, &levels);
    const std::vector<int> order = bidi::visual_order(levels);

    // Runs: neighbours drawn by one font in one direction. They are laid out in drawing order.
    struct Run
    {
        std::size_t first;
        std::size_t last; // one past
        std::int16_t face;
        std::uint8_t level;
        int rank; // where the run starts on the line
    };
    std::vector<int> rank(count);
    for (std::size_t position = 0; position < count; ++position)
        rank[static_cast<std::size_t>(order[position])] = static_cast<int>(position);
    std::vector<Run> runs;
    for (std::size_t i = 0; i < count; ++i)
    {
        if (face[i] == kNone)
            continue;
        if (!runs.empty() && runs.back().face == face[i] && runs.back().level == levels[i])
        {
            // Only invisible characters lie between the run's end and i.
            bool adjacent = true;
            for (std::size_t k = runs.back().last; k < i; ++k)
                adjacent = adjacent && face[k] == kNone;
            if (adjacent)
            {
                runs.back().last = i + 1;
                runs.back().rank = std::min(runs.back().rank, rank[i]);
                continue;
            }
        }
        runs.push_back({i, i + 1, face[i], levels[i], rank[i]});
    }
    std::sort(runs.begin(), runs.end(), [](const Run &a, const Run &b) { return a.rank < b.rank; });

    Line line;
    float pen = 0.0f;
    bool any = false;        // a glyph with a width has been placed
    bool joined = false;     // the last such glyph was part of joined (Arabic) writing
    std::uint16_t gaps = 0;
    const float baked_unit = 1.0f / header_.pixel_size;
    std::vector<char32_t> piece;
    for (const Run &run : runs)
    {
        const bool right_to_left = (run.level & 1) != 0;
        if (run.face == kBaked)
        {
            std::uint32_t previous = 0;
            for (std::size_t k = 0; k < run.last - run.first; ++k)
            {
                const std::size_t i = right_to_left ? run.last - 1 - k : run.first + k;
                if (face[i] == kNone)
                    continue;
                std::uint32_t c = characters[i];
                if (right_to_left && find(bidi::mirror(c)) != nullptr)
                    c = bidi::mirror(c);
                const ff::Glyph *glyph = find(c);
                if (glyph == nullptr)
                    continue;
                if (previous != 0)
                    pen += kern(previous, c) * baked_unit;
                if (any)
                    ++gaps;
                line.glyphs.push_back({kBaked, gaps, c, pen, 0.0f});
                pen += glyph->advance * baked_unit;
                previous = c;
                any = true;
                joined = false;
            }
            continue;
        }
        piece.clear();
        bool joins = false;
        for (std::size_t i = run.first; i < run.last; ++i)
        {
            if (face[i] == kNone)
                continue;
            piece.push_back(characters[i]);
            joins = joins || cursive(characters[i]);
        }
        d.fonts.shape(run.face, piece.data(), static_cast<int>(piece.size()), right_to_left, &d.run);
        bool first_of_run = true;
        for (const RunGlyph &glyph : d.run)
        {
            // Letter-spacing goes between glyphs that take room, but never inside joined writing.
            if (glyph.advance > 0.0f)
            {
                if (any && !(joins && joined && !first_of_run))
                    ++gaps;
                any = true;
                joined = joins;
                first_of_run = false;
            }
            line.glyphs.push_back({run.face, gaps, glyph.id, pen + glyph.x_offset, -glyph.y_offset});
            pen += glyph.advance;
        }
    }
    line.advance = pen;
    line.gaps = gaps;
    return &d.lines.emplace(std::string(text), std::move(line)).first->second;
}

float Font::measure(std::string_view text, float size, float tracking) const
{
    if (const Line *line = shaped(text))
        return line->advance * size + static_cast<float>(line->gaps) * tracking;
    const float scale = size / header_.pixel_size;
    float width = 0.0f;
    std::uint32_t previous = 0;
    for (std::size_t index = 0; index < text.size();)
    {
        std::uint32_t codepoint = next_codepoint(text, &index);
        const ff::Glyph *glyph = find(codepoint);
        if (glyph == nullptr)
        {
            codepoint = '?';
            glyph = find(codepoint);
            if (glyph == nullptr)
                continue;
        }
        if (previous != 0)
            width += kern(previous, codepoint) * scale + tracking;
        width += glyph->advance * scale;
        previous = codepoint;
    }
    return width;
}

float Font::layout(std::string_view text, float x, float y, float size, Align align,
                   std::vector<GlyphQuad> &quads, float tracking) const
{
    const float scale = size / header_.pixel_size;
    const float width = measure(text, size, tracking);
    float pen = x;
    if (align == Align::center)
        pen -= width * 0.5f;
    else if (align == Align::right)
        pen -= width;
    const float inverse_w = 1.0f / static_cast<float>(header_.atlas_width);
    const float inverse_h = 1.0f / static_cast<float>(atlas_height_);
    const auto baked_quad = [&](const ff::Glyph &glyph, float at_x, float at_y)
    {
        if (glyph.w == 0 || glyph.h == 0)
            return;
        GlyphQuad quad;
        quad.x0 = at_x + glyph.offset_x * scale;
        quad.y0 = at_y + glyph.offset_y * scale;
        quad.x1 = quad.x0 + static_cast<float>(glyph.w) * scale;
        quad.y1 = quad.y0 + static_cast<float>(glyph.h) * scale;
        quad.u0 = static_cast<float>(glyph.x) * inverse_w;
        quad.v0 = static_cast<float>(glyph.y) * inverse_h;
        quad.u1 = static_cast<float>(glyph.x + glyph.w) * inverse_w;
        quad.v1 = static_cast<float>(glyph.y + glyph.h) * inverse_h;
        quads.push_back(quad);
    };

    if (const Line *line = shaped(text))
    {
        Dynamic &d = *dynamic_;
        // A full atlas was noticed last frame: start this one with empty rows.
        if (d.emptying)
        {
            d.emptying = false;
            d.full = false;
            d.placed.clear();
            std::fill(atlas_.begin() + static_cast<std::ptrdiff_t>(header_.atlas_width) * d.first_row, atlas_.end(),
                      std::uint8_t{0});
            d.pen_x = 1;
            d.pen_y = d.first_row + 1;
            d.shelf = 0;
            d.changed(d.first_row, atlas_height_);
        }
        const float system_scale = size / kSystemPixelSize;
        const float range = header_.sdf_range * kSystemPixelSize / header_.pixel_size;
        for (const Line::Glyph &glyph : line->glyphs)
        {
            const float at_x = pen + glyph.x * size + static_cast<float>(glyph.gaps) * tracking;
            const float at_y = y + glyph.y * size;
            if (glyph.face < 0)
            {
                if (const ff::Glyph *baked = find(glyph.id))
                    baked_quad(*baked, at_x, at_y);
                continue;
            }
            const std::uint64_t key = (static_cast<std::uint64_t>(glyph.face) << 32) | glyph.id;
            auto found = d.placed.find(key);
            if (found == d.placed.end())
            {
                GlyphField field;
                Dynamic::Placed placed;
                if (d.fonts.field(glyph.face, glyph.id, kSystemPixelSize, range, &field) && field.w > 0)
                {
                    if (d.pen_x + field.w + 1 > header_.atlas_width)
                    {
                        d.pen_x = 1;
                        d.pen_y += d.shelf + 1;
                        d.shelf = 0;
                    }
                    if (field.w + 2 > header_.atlas_width || d.pen_y + field.h + 1 > atlas_height_)
                    {
                        d.full = true; // not remembered: it is drawn once the rows are emptied
                        continue;
                    }
                    for (int row = 0; row < field.h; ++row)
                        std::memcpy(&atlas_[static_cast<std::size_t>(d.pen_y + row) * header_.atlas_width +
                                            static_cast<std::size_t>(d.pen_x)],
                                    &field.pixels[static_cast<std::size_t>(row) * field.w],
                                    static_cast<std::size_t>(field.w));
                    placed.x = static_cast<std::uint16_t>(d.pen_x);
                    placed.y = static_cast<std::uint16_t>(d.pen_y);
                    placed.w = static_cast<std::uint16_t>(field.w);
                    placed.h = static_cast<std::uint16_t>(field.h);
                    placed.offset_x = field.offset_x;
                    placed.offset_y = field.offset_y;
                    d.changed(d.pen_y, d.pen_y + field.h);
                    d.pen_x += field.w + 1;
                    d.shelf = std::max(d.shelf, field.h);
                }
                found = d.placed.emplace(key, placed).first;
            }
            const Dynamic::Placed &placed = found->second;
            if (placed.w == 0)
                continue;
            GlyphQuad quad;
            quad.x0 = at_x + placed.offset_x * system_scale;
            quad.y0 = at_y + placed.offset_y * system_scale;
            quad.x1 = quad.x0 + static_cast<float>(placed.w) * system_scale;
            quad.y1 = quad.y0 + static_cast<float>(placed.h) * system_scale;
            quad.u0 = static_cast<float>(placed.x) * inverse_w;
            quad.v0 = static_cast<float>(placed.y) * inverse_h;
            quad.u1 = static_cast<float>(placed.x + placed.w) * inverse_w;
            quad.v1 = static_cast<float>(placed.y + placed.h) * inverse_h;
            quads.push_back(quad);
        }
        return width;
    }

    std::uint32_t previous = 0;
    for (std::size_t index = 0; index < text.size();)
    {
        std::uint32_t codepoint = next_codepoint(text, &index);
        const ff::Glyph *glyph = find(codepoint);
        if (glyph == nullptr)
        {
            codepoint = '?';
            glyph = find(codepoint);
            if (glyph == nullptr)
                continue;
        }
        if (previous != 0)
            pen += kern(previous, codepoint) * scale + tracking;
        baked_quad(*glyph, pen, y);
        pen += glyph->advance * scale;
        previous = codepoint;
    }
    return width;
}

bool Font::take_changed_rows(int *first, int *last) const
{
    if (!dynamic_)
        return false;
    Dynamic &d = *dynamic_;
    // Text that found no room this frame is drawn after the rows are emptied, next frame.
    if (d.full)
        d.emptying = true;
    if (d.changed_first >= d.changed_last)
        return false;
    *first = d.changed_first;
    *last = d.changed_last;
    d.changed_first = INT_MAX;
    d.changed_last = 0;
    return true;
}

std::vector<std::string> Font::wrap(std::string_view text, float size, float max_width) const
{
    std::vector<std::string> lines;
    std::string line;
    // Appends a word wider than a whole line, split between characters (never before a mark
    // that sits on the character before it).
    const auto split_word = [&](std::string_view word)
    {
        std::string part;
        for (std::size_t index = 0; index < word.size();)
        {
            const std::size_t start = index;
            next_codepoint(word, &index);
            for (std::size_t next = index; next < word.size();)
            {
                if (!bidi::attaches(next_codepoint(word, &next)))
                    break;
                index = next;
            }
            const std::string_view character = word.substr(start, index - start);
            if (!part.empty() && measure(part + std::string(character), size) > max_width)
            {
                lines.push_back(part);
                part.clear();
            }
            part.append(character);
        }
        line = part;
    };
    // The parts of a word a line may break between: one, unless it holds Japanese or Chinese
    // text or a zero-width space.
    std::vector<std::string_view> pieces;
    const auto split_pieces = [&pieces](std::string_view word)
    {
        pieces.clear();
        std::size_t start = 0;
        char32_t before = 0;
        for (std::size_t index = 0; index < word.size();)
        {
            const std::size_t at = index;
            const char32_t c = next_codepoint(word, &index);
            if (c == 0x200b)
            {
                if (at > start)
                    pieces.push_back(word.substr(start, at - start));
                start = index;
                before = 0;
                continue;
            }
            if (before != 0 && at > start && breaks_between(before, c))
            {
                pieces.push_back(word.substr(start, at - start));
                start = at;
            }
            before = c;
        }
        if (start < word.size() || pieces.empty())
            pieces.push_back(word.substr(start));
    };
    std::size_t index = 0;
    while (index <= text.size())
    {
        const std::size_t newline = text.find('\n', index);
        const std::string_view paragraph = text.substr(
            index, newline == std::string_view::npos ? std::string_view::npos : newline - index);
        std::size_t word_start = 0;
        line.clear();
        const std::size_t first_line = lines.size();
        while (word_start <= paragraph.size())
        {
            std::size_t word_end = paragraph.find(' ', word_start);
            if (word_end == std::string_view::npos)
                word_end = paragraph.size();
            split_pieces(paragraph.substr(word_start, word_end - word_start));
            for (std::size_t p = 0; p < pieces.size(); ++p)
            {
                const std::string_view piece = pieces[p];
                // A space before the word's first piece; the others follow without one.
                const std::string candidate =
                    line.empty() ? std::string(piece) : line + (p == 0 ? " " : "") + std::string(piece);
                if (measure(candidate, size) <= max_width)
                {
                    line = candidate;
                }
                else
                {
                    if (!line.empty())
                        lines.push_back(line);
                    if (measure(piece, size) > max_width)
                        split_word(piece);
                    else
                        line.assign(piece);
                }
            }
            word_start = word_end + 1;
        }
        lines.push_back(line);
        // Every line of a right-to-left paragraph runs right to left, also one that happens to
        // start with a Latin word: a right-to-left mark (U+200F, invisible) leads each of them.
        if (lines.size() - first_line > 1 && shaped(paragraph) != nullptr)
        {
            std::vector<char32_t> characters;
            for (std::size_t at = 0; at < paragraph.size();)
                characters.push_back(next_codepoint(paragraph, &at));
            if (bidi::paragraph_level(characters) == 1)
                for (std::size_t k = first_line; k < lines.size(); ++k)
                    lines[k].insert(0, "\xE2\x80\x8F");
        }
        if (newline == std::string_view::npos)
            break;
        index = newline + 1;
    }
    return lines;
}

std::string Font::fit(std::string_view text, float size, float max_width, float tracking) const
{
    if (measure(text, size, tracking) <= max_width)
        return std::string(text);
    static constexpr std::string_view kEllipsis = "\xE2\x80\xA6";
    const float room = max_width - measure(kEllipsis, size, tracking) - tracking;
    std::string result;
    for (std::size_t index = 0; index < text.size();)
    {
        const std::size_t start = index;
        next_codepoint(text, &index);
        // A mark stays with the character it sits on.
        for (std::size_t next = index; next < text.size();)
        {
            if (!bidi::attaches(next_codepoint(text, &next)))
                break;
            index = next;
        }
        const std::string candidate = result + std::string(text.substr(start, index - start));
        if (measure(candidate, size, tracking) > room)
            break;
        result = candidate;
    }
    while (!result.empty() && result.back() == ' ')
        result.pop_back();
    return result + std::string(kEllipsis);
}

} // namespace pe::gfx
