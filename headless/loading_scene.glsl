// SPDX-License-Identifier: GPL-3.0-or-later
// The screen shown while a game loads: a progress bar and the steps of the start, drawn entirely
// in this shader (no textures). Shared by both graphics backends, which add their own #version
// line, loading_text.glsl before this file, and a main().
//
//   vec3 loading_scene(vec2 pixel, vec2 size, float seconds, uint state[24])
//
// pixel has its origin at the bottom left; seconds counts from the start of loading. With 1000
// added to it nothing on the screen moves (Settings > Accessibility, reduced motion).
// state comes from hud.h (Loading::State): the step the start is at (1 to 4, 0 before the first),
// the progress in thousandths, the shaders built and their total, and the seconds the step has
// lasted.

const vec3 kLime = vec3(0.72, 0.95, 0.05);
const vec3 kGreen = vec3(0.16, 0.70, 0.30);
const int kLineBrand = 0; // then the four steps: kLine[step]
const int kLineLoading = 5;
const int kLineSeconds = 6;

float hash21(vec2 p)
{
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float glyph_texel(int glyph, int x, int y)
{
    x = clamp(x, 0, kGlyphWidth - 1);
    y = clamp(y, 0, kGlyphHeight - 1);
    int at = glyph * kGlyphWidth * kGlyphHeight + y * kGlyphWidth + x;
    return float((kGlyphs[at >> 2] >> uint((at & 3) * 8)) & 0xffu) / 255.0;
}

// How much of a pixel the glyph covers. at is where its pen stands (left end of the baseline),
// cap the height of a capital in pixels.
float glyph_cover(int glyph, vec2 pixel, vec2 at, float cap)
{
    float texel = cap / kGlyphCap;
    vec2 st = vec2((pixel.x - at.x) / texel + kGlyphLeft,
                   float(kGlyphHeight) - ((pixel.y - at.y) / texel + kGlyphBase)) - 0.5;
    if (st.x < -1.0 || st.y < -1.0 || st.x > float(kGlyphWidth) || st.y > float(kGlyphHeight))
        return 0.0;
    ivec2 i = ivec2(floor(st));
    vec2 f = st - vec2(i);
    float value = mix(mix(glyph_texel(glyph, i.x, i.y), glyph_texel(glyph, i.x + 1, i.y), f.x),
                      mix(glyph_texel(glyph, i.x, i.y + 1), glyph_texel(glyph, i.x + 1, i.y + 1), f.x), f.y);
    return clamp((value - 128.0 / 255.0) * 2.0 * kGlyphSpread * texel + 0.5, 0.0, 1.0);
}

float pen_step(int glyph, float cap, float tracking)
{
    return (glyph < 0 ? 0.30 * kGlyphCap / 0.70 : kAdvance[glyph]) * cap / kGlyphCap + tracking * cap;
}

float line_width(int line, float cap, float tracking)
{
    float width = 0.0;
    for (int i = 0; i < kLine[line].y; ++i)
        width += pen_step(kText[kLine[line].x + i], cap, tracking);
    return width - tracking * cap;
}

// A line of text with the left end of its baseline at `at`.
float line_cover(int line, vec2 pixel, vec2 at, float cap, float tracking)
{
    if (pixel.y < at.y - cap || pixel.y > at.y + 2.0 * cap || pixel.x < at.x - cap)
        return 0.0;
    float cover = 0.0;
    float pen = at.x;
    for (int i = 0; i < kLine[line].y; ++i)
    {
        int glyph = kText[kLine[line].x + i];
        if (glyph >= 0)
            cover = max(cover, glyph_cover(glyph, pixel, vec2(pen, at.y), cap));
        pen += pen_step(glyph, cap, tracking);
    }
    return cover;
}

// A number with the right end of its baseline at `at`; returns its cover, and its width in
// `width`. `sign` is a glyph drawn after the digits, or -1.
float number_cover(uint value, int sign, vec2 pixel, vec2 at, float cap, float tracking, out float width)
{
    float pen = at.x;
    float cover = 0.0;
    bool near = pixel.y > at.y - cap && pixel.y < at.y + 2.0 * cap;
    if (sign >= 0)
    {
        pen -= kAdvance[sign] * cap / kGlyphCap;
        if (near)
            cover = glyph_cover(sign, pixel, vec2(pen, at.y), cap);
        pen -= tracking * cap;
    }
    for (int i = 0; i < 6; ++i)
    {
        int glyph = kDigit + int(value % 10u);
        pen -= kAdvance[glyph] * cap / kGlyphCap;
        if (near)
            cover = max(cover, glyph_cover(glyph, pixel, vec2(pen, at.y), cap));
        value /= 10u;
        if (value == 0u)
            break;
        pen -= tracking * cap;
    }
    width = at.x - pen;
    return cover;
}

// A box with round ends: how much of a pixel it covers.
float pill(vec2 pixel, vec2 from, vec2 to, float radius)
{
    vec2 a = vec2(from.x + radius, 0.5 * (from.y + to.y));
    vec2 b = vec2(max(to.x - radius, a.x), a.y);
    float along = clamp(pixel.x, a.x, b.x);
    return clamp(radius - length(pixel - vec2(along, a.y)) + 0.5, 0.0, 1.0);
}

vec3 loading_scene(vec2 pixel, vec2 size, float seconds, uint state[24])
{
    bool calm = seconds >= 1000.0;
    if (calm)
        seconds -= 1000.0;
    int step = int(min(state[0], 4u));
    float progress = clamp(float(state[1]) / 1000.0, 0.0, 1.0);
    float unit = size.y / 1080.0; // one design pixel
    float left = 120.0 * unit;
    float right = size.x - 120.0 * unit;
    vec2 uv = pixel / size;

    // ---- the backdrop: near black, a little lighter towards the top, lit faintly from the bar ----
    vec3 color = mix(vec3(0.012, 0.015, 0.020), vec3(0.030, 0.038, 0.050), smoothstep(0.0, 1.0, uv.y));
    vec2 glow_from = (pixel - vec2(mix(left, right, progress), 168.0 * unit)) / (size.y * vec2(1.6, 0.9));
    color += kLime * 0.030 * exp(-dot(glow_from, glow_from) * 6.0);
    color *= 1.0 - 0.35 * smoothstep(0.45, 1.05, length((uv - 0.5) * vec2(1.0, 1.15)));

    // ---- the name, top left ----
    color = mix(color, vec3(1.0), 0.34 * line_cover(kLineBrand, pixel, vec2(left, size.y - 132.0 * unit),
                                                      15.0 * unit, 0.34));

    // ---- the steps, one under the other ----
    for (int i = 1; i <= 4; ++i)
    {
        float y = (486.0 - float(i - 1) * 58.0) * unit;
        vec2 centre = vec2(left + 9.0 * unit, y + 9.0 * unit);
        float away = length(pixel - centre);
        float ring = clamp(1.6 * unit - abs(away - 8.0 * unit) + 0.5, 0.0, 1.0);
        float dot_cover = clamp(4.2 * unit - away + 0.5, 0.0, 1.0);
        float text = line_cover(i, pixel, vec2(left + 44.0 * unit, y), 18.0 * unit, 0.16);
        if (i < step)
        {
            // Done: a filled mark, the text set back.
            color = mix(color, kGreen, 0.85 * clamp(8.8 * unit - away + 0.5, 0.0, 1.0));
            color = mix(color, vec3(1.0), 0.42 * text);
        }
        else if (i == step)
        {
            float breath = calm ? 1.0 : 0.82 + 0.18 * sin(seconds * 3.2);
            color = mix(color, kLime, ring);
            color = mix(color, kLime, dot_cover * breath);
            color = mix(color, vec3(1.0), 0.96 * text);
        }
        else
        {
            color = mix(color, vec3(1.0), 0.16 * ring);
            color = mix(color, vec3(1.0), 0.20 * text);
        }
    }

    // ---- above the bar: the word on the left, the figure on the right ----
    float baseline = 196.0 * unit;
    color = mix(color, vec3(1.0), 0.52 * line_cover(kLineLoading, pixel, vec2(left, baseline), 15.0 * unit, 0.34));
    float width;
    float figure = number_cover(uint(progress * 100.0 + 0.001), kPercent, pixel, vec2(right, baseline),
                                34.0 * unit, 0.04, width);
    color = mix(color, vec3(1.0), 0.96 * figure);
    // Beside the word: the shaders built of their total, or the seconds a slow step has lasted.
    float detail_at = left + line_width(kLineLoading, 15.0 * unit, 0.34) + 26.0 * unit;
    if (step == 3 && state[3] > 0u)
    {
        float total_width, built_width, unused;
        float total = number_cover(state[3], -1, pixel, vec2(0.0), 15.0 * unit, 0.10, total_width);
        float built = number_cover(state[2], -1, pixel, vec2(0.0), 15.0 * unit, 0.10, built_width);
        float slash = kAdvance[kSlash] * 15.0 * unit / kGlyphCap;
        float gap = 7.0 * unit;
        built = number_cover(state[2], -1, pixel, vec2(detail_at + built_width, baseline), 15.0 * unit, 0.10, unused);
        float bar_sign = glyph_cover(kSlash, pixel, vec2(detail_at + built_width + gap, baseline), 15.0 * unit);
        total = number_cover(state[3], -1, pixel,
                             vec2(detail_at + built_width + 2.0 * gap + slash + total_width, baseline),
                             15.0 * unit, 0.10, unused);
        color = mix(color, kLime, 0.90 * max(built, max(bar_sign * 0.6, total * 0.6)));
    }
    else if (state[4] >= 20u)
    {
        float number_width, unused;
        number_cover(state[4], -1, pixel, vec2(0.0), 15.0 * unit, 0.10, number_width);
        float number = number_cover(state[4], -1, pixel, vec2(detail_at + number_width, baseline),
                                    15.0 * unit, 0.10, unused);
        float sign = line_cover(kLineSeconds, pixel, vec2(detail_at + number_width + 6.0 * unit, baseline),
                                15.0 * unit, 0.0);
        color = mix(color, kLime, 0.90 * max(number, sign));
    }

    // ---- the bar ----
    vec2 bar_from = vec2(left, 150.0 * unit);
    vec2 bar_to = vec2(right, 158.0 * unit);
    float radius = 4.0 * unit;
    color = mix(color, vec3(1.0), 0.10 * pill(pixel, bar_from, bar_to, radius));
    float filled_to = mix(left + 2.0 * radius, right, progress);
    float fill = pill(pixel, bar_from, vec2(filled_to, bar_to.y), radius);
    float along = clamp((pixel.x - left) / max(filled_to - left, 1.0), 0.0, 1.0);
    vec3 fill_color = mix(kGreen, kLime, along);
    if (!calm)
    {
        // A light passing along the filled part.
        float sweep = fract(seconds * 0.45) * 1.4 - 0.2;
        fill_color += vec3(0.30) * exp(-pow((along - sweep) * 7.0, 2.0));
    }
    color = mix(color, fill_color, fill);
    // The glow of the filled part, strongest at its end.
    float under = exp(-abs(pixel.y - 154.0 * unit) / (9.0 * unit)) *
                  smoothstep(left - 20.0 * unit, left, pixel.x) * (1.0 - smoothstep(filled_to, filled_to + 26.0 * unit, pixel.x));
    color += kLime * 0.10 * under * (0.35 + 0.65 * along) * (1.0 - fill);

    // Arrive out of the dark; a little noise keeps the gradients from banding.
    color *= smoothstep(0.0, 0.5, seconds);
    color += (hash21(pixel + fract(seconds) * 61.0) - 0.5) / 255.0 * 1.4;
    return clamp(color, 0.0, 1.0);
}
