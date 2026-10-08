// ProsperoEden - The launcher's text: a baked SDF font, the console's fonts for other scripts.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "pe/gfx/font_format.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace pe::gfx
{

// One positioned glyph quad in output pixels, with its atlas UV rectangle.
struct GlyphQuad
{
    float x0, y0, x1, y1;
    float u0, v0, u1, v1;
};

enum class Align : std::uint8_t
{
    left,
    center,
    right,
};

// Text is measured and laid out a line at a time, from UTF-8.
//
// The baked font (Montserrat: Latin, Cyrillic) is the launcher's own. A character it lacks is
// taken from the console's fonts once use_system_fonts() has named them (system_fonts.hpp): its
// glyph is drawn into the lower part of the same atlas the first time it is needed. Such text is
// shaped (Arabic letters join, Thai marks stack) and ordered (Arabic runs right to left) before
// it is measured, so every function here gives the same answers for it as for Latin text.
//
// One thread uses a Font: the launcher's. Measuring and laying out change the glyph and line
// caches behind the const functions.
class Font
{
  public:
    Font();
    ~Font();
    Font(const Font &) = delete;
    Font &operator=(const Font &) = delete;

    // Parses a .pefont blob; returns false (and keeps an error) if invalid.
    bool load(std::string_view data);
    const std::string &error() const
    {
        return error_;
    }

    // The font files to take other scripts from, tried in order and read when first needed;
    // `language` is the launcher's (a tag such as "ja-JP"). An empty list: the baked font alone.
    void use_system_fonts(std::vector<std::string> files, std::string_view language);
    // Whether every character of text has a glyph, in the baked font or a system font.
    bool can_draw(std::string_view text) const;
    // text without the characters no font has a glyph for (they would be drawn as "?").
    std::string drawable(std::string_view text) const;
    // The system fonts read so far, for the log.
    std::string system_fonts_read() const;

    // Width in pixels of one line of UTF-8 text at the given pixel size.
    // tracking adds that many pixels after every glyph but the last.
    float measure(std::string_view text, float size, float tracking = 0.0f) const;
    float ascent(float size) const
    {
        return header_.ascent * size / header_.pixel_size;
    }
    float descent(float size) const
    {
        return -header_.descent * size / header_.pixel_size;
    }
    float line_height(float size) const
    {
        return (header_.ascent - header_.descent + header_.line_gap) * size / header_.pixel_size;
    }
    // Distance-field spread in output pixels at a size (for shader anti-aliasing).
    float sdf_range(float size) const
    {
        return header_.sdf_range * size / header_.pixel_size;
    }

    // Lays out one line with its baseline at y. x is the left edge, centre or
    // right edge depending on align. Appends to quads; returns the advance.
    float layout(std::string_view text, float x, float y, float size, Align align,
                 std::vector<GlyphQuad> &quads, float tracking = 0.0f) const;

    // Breaks text into lines no wider than max_width: at spaces, between the characters of
    // Japanese and Chinese text, and where a zero-width space (U+200B) allows it (Thai). A word
    // wider than the line (a file name) is split between characters.
    std::vector<std::string> wrap(std::string_view text, float size, float max_width) const;

    // The text itself when it fits max_width, else its start followed by an ellipsis.
    std::string fit(std::string_view text, float size, float max_width,
                    float tracking = 0.0f) const;

    // The atlas: the baked glyphs at the top, the system fonts' below. It keeps its size.
    std::uint16_t atlas_width() const
    {
        return header_.atlas_width;
    }
    std::uint16_t atlas_height() const
    {
        return atlas_height_;
    }
    const std::vector<std::uint8_t> &atlas() const
    {
        return atlas_;
    }
    // The rows [*first, *last) of the atlas that changed since the last call (glyphs drawn for
    // new text); false when none did. Call it once a frame, after the frame's text is laid out,
    // and upload those rows before drawing.
    bool take_changed_rows(int *first, int *last) const;

    bool has_glyph(std::uint32_t codepoint) const
    {
        return find(codepoint) != nullptr;
    }

  private:
    struct Dynamic;
    struct Line;

    const font_format::Glyph *find(std::uint32_t codepoint) const;
    float kern(std::uint32_t first, std::uint32_t second) const;
    // The shaped form of text that needs a system font; null for text the baked font covers.
    const Line *shaped(std::string_view text) const;

    font_format::Header header_{};
    std::vector<font_format::Glyph> glyphs_;
    std::vector<font_format::Kern> kerns_;
    mutable std::vector<std::uint8_t> atlas_;
    std::uint16_t atlas_height_ = 0;
    std::string error_;
    std::unique_ptr<Dynamic> dynamic_;
};

// Decodes the next UTF-8 codepoint from text at *index (advancing it);
// invalid bytes decode as U+FFFD.
std::uint32_t next_codepoint(std::string_view text, std::size_t *index);

// Whether two pieces of text follow each other without a space between them (the end of one or
// the start of the other is Japanese or Chinese).
bool joins_without_space(std::string_view first, std::string_view second);

} // namespace pe::gfx
