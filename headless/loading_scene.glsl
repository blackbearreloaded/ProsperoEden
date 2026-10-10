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
// The inks of the lettering; from kInkLime on they are lime, before it white.
const int kInkBrand = 0, kInkDone = 1, kInkNow = 2, kInkLater = 3, kInkWord = 4, kInkFigure = 5, kInkLime = 6,
          kInkLimeDim = 7;

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
    int at = y * kGlyphWidth + x;
    return float((glyph_word(glyph, at >> 2) >> uint((at & 3) * 8)) & 0xffu) / 255.0;
}

// The lettering is read in one place only, at the end of loading_scene: the console's shader
// compiler copes badly with the glyph table being read from many. So the text is laid out first,
// which only decides which glyph, if any, this pixel falls in (a Pick), and what ink it gets.
struct Pick
{
    int glyph;   // -1: no text here
    int ink;
    vec2 st;     // where in the glyph's field, in texels
    float texel; // pixels per texel
};

float pen_step(int glyph, float cap, float tracking)
{
    return (glyph < 0 ? 0.30 * kGlyphCap / 0.70 : kAdvance[glyph]) * cap / kGlyphCap + tracking * cap;
}

// Takes the pixel for this glyph when it lies between the glyph's pen and the next one.
// at is where the pen stands (left end of the baseline), cap the height of a capital in pixels.
void pick_glyph(inout Pick pick, int glyph, int ink, vec2 pixel, vec2 at, float cap, float tracking)
{
    float texel = cap / kGlyphCap;
    float step = pen_step(glyph, cap, tracking);
    if (pixel.x < at.x - 0.5 * tracking * cap || pixel.x >= at.x + step - 0.5 * tracking * cap)
        return;
    pick.glyph = glyph;
    pick.ink = ink;
    pick.st = vec2((pixel.x - at.x) / texel + kGlyphLeft,
                   float(kGlyphHeight) - ((pixel.y - at.y) / texel + kGlyphBase)) - 0.5;
    pick.texel = texel;
}

float line_width(int line, float cap, float tracking)
{
    float width = 0.0;
    for (int i = 0; i < kLine[line].y; ++i)
        width += pen_step(kText[kLine[line].x + i], cap, tracking);
    return width - tracking * cap;
}

// A line of text with the left end of its baseline at `at`.
void pick_line(inout Pick pick, int line, int ink, vec2 pixel, vec2 at, float cap, float tracking)
{
    if (pixel.y < at.y - 0.4 * cap || pixel.y > at.y + 1.4 * cap || pixel.x < at.x - cap)
        return;
    float pen = at.x;
    for (int i = 0; i < kLine[line].y; ++i)
    {
        int glyph = kText[kLine[line].x + i];
        if (glyph >= 0)
            pick_glyph(pick, glyph, ink, pixel, vec2(pen, at.y), cap, tracking);
        pen += pen_step(glyph, cap, tracking);
    }
}

float number_width(uint value, float cap, float tracking)
{
    float width = 0.0;
    for (int i = 0; i < 6; ++i)
    {
        width += pen_step(kDigit + int(value % 10u), cap, tracking);
        value /= 10u;
        if (value == 0u)
            break;
    }
    return width - tracking * cap;
}

// A number with the right end of its baseline at `at`.
void pick_number(inout Pick pick, uint value, int ink, vec2 pixel, vec2 at, float cap, float tracking)
{
    if (pixel.y < at.y - 0.4 * cap || pixel.y > at.y + 1.4 * cap)
        return;
    float pen = at.x + tracking * cap;
    for (int i = 0; i < 6; ++i)
    {
        int glyph = kDigit + int(value % 10u);
        pen -= pen_step(glyph, cap, tracking);
        pick_glyph(pick, glyph, ink, pixel, vec2(pen, at.y), cap, tracking);
        value /= 10u;
        if (value == 0u)
            break;
    }
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
    Pick pick = Pick(-1, 0, vec2(0.0), 1.0);

    // ---- the backdrop: near black, a little lighter towards the top, lit faintly from the bar ----
    vec3 color = mix(vec3(0.012, 0.015, 0.020), vec3(0.030, 0.038, 0.050), smoothstep(0.0, 1.0, uv.y));
    vec2 glow_from = (pixel - vec2(mix(left, right, progress), 168.0 * unit)) / (size.y * vec2(1.6, 0.9));
    color += kLime * 0.030 * exp(-dot(glow_from, glow_from) * 6.0);
    color *= 1.0 - 0.35 * smoothstep(0.45, 1.05, length((uv - 0.5) * vec2(1.0, 1.15)));

    // ---- the name, top left ----
    pick_line(pick, kLineBrand, kInkBrand, pixel, vec2(left, size.y - 132.0 * unit), 15.0 * unit, 0.34);

    // ---- the steps, one under the other ----
    for (int i = 1; i <= 4; ++i)
    {
        float y = (486.0 - float(i - 1) * 58.0) * unit;
        vec2 centre = vec2(left + 9.0 * unit, y + 9.0 * unit);
        float away = length(pixel - centre);
        float ring = clamp(1.6 * unit - abs(away - 8.0 * unit) + 0.5, 0.0, 1.0);
        float dot_cover = clamp(4.2 * unit - away + 0.5, 0.0, 1.0);
        pick_line(pick, i, i < step ? kInkDone : i == step ? kInkNow : kInkLater, pixel,
                  vec2(left + 44.0 * unit, y), 18.0 * unit, 0.16);
        if (i < step)
        {
            // Done: a filled mark.
            color = mix(color, kGreen, 0.85 * clamp(8.8 * unit - away + 0.5, 0.0, 1.0));
        }
        else if (i == step)
        {
            float breath = calm ? 1.0 : 0.82 + 0.18 * sin(seconds * 3.2);
            color = mix(color, kLime, ring);
            color = mix(color, kLime, dot_cover * breath);
        }
        else
        {
            color = mix(color, vec3(1.0), 0.16 * ring);
        }
    }

    // ---- above the bar: the word on the left, the figure on the right ----
    float baseline = 196.0 * unit;
    float small = 15.0 * unit;
    pick_line(pick, kLineLoading, kInkWord, pixel, vec2(left, baseline), small, 0.34);
    float percent_step = pen_step(kPercent, 34.0 * unit, 0.04);
    if (pixel.y > baseline - 14.0 * unit && pixel.y < baseline + 48.0 * unit)
        pick_glyph(pick, kPercent, kInkFigure, pixel, vec2(right - percent_step, baseline), 34.0 * unit, 0.04);
    pick_number(pick, uint(progress * 100.0 + 0.001), kInkFigure, pixel, vec2(right - percent_step, baseline),
                34.0 * unit, 0.04);
    // Beside the word: the shaders built of their total, or the seconds a slow step has lasted.
    float detail_at = left + line_width(kLineLoading, small, 0.34) + 26.0 * unit;
    if (step == 3 && state[3] > 0u)
    {
        float built_to = detail_at + number_width(state[2], small, 0.10);
        float slash_at = built_to + 7.0 * unit;
        float total_to = slash_at + pen_step(kSlash, small, 0.0) + 7.0 * unit + number_width(state[3], small, 0.10);
        pick_number(pick, state[2], kInkLime, pixel, vec2(built_to, baseline), small, 0.10);
        if (pixel.y > baseline - 0.4 * small && pixel.y < baseline + 1.4 * small)
            pick_glyph(pick, kSlash, kInkLimeDim, pixel, vec2(slash_at, baseline), small, 0.0);
        pick_number(pick, state[3], kInkLimeDim, pixel, vec2(total_to, baseline), small, 0.10);
    }
    else if (state[4] >= 20u)
    {
        float number_to = detail_at + number_width(state[4], small, 0.10);
        pick_number(pick, state[4], kInkLime, pixel, vec2(number_to, baseline), small, 0.10);
        pick_line(pick, kLineSeconds, kInkLime, pixel, vec2(number_to + 6.0 * unit, baseline), small, 0.0);
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

    // ---- the lettering: the one place the glyph table is read ----
    if (pick.glyph >= 0 && pick.st.x > -1.0 && pick.st.y > -1.0 && pick.st.x < float(kGlyphWidth) &&
        pick.st.y < float(kGlyphHeight))
    {
        ivec2 i = ivec2(floor(pick.st));
        vec2 f = pick.st - vec2(i);
        float value = mix(mix(glyph_texel(pick.glyph, i.x, i.y), glyph_texel(pick.glyph, i.x + 1, i.y), f.x),
                          mix(glyph_texel(pick.glyph, i.x, i.y + 1), glyph_texel(pick.glyph, i.x + 1, i.y + 1), f.x),
                          f.y);
        float cover = clamp((value - 128.0 / 255.0) * 2.0 * kGlyphSpread * pick.texel + 0.5, 0.0, 1.0);
        vec3 ink = pick.ink >= kInkLime ? kLime : vec3(1.0);
        float strength = pick.ink == kInkBrand ? 0.34 : pick.ink == kInkDone ? 0.42 : pick.ink == kInkNow ? 0.96 :
                         pick.ink == kInkLater ? 0.20 : pick.ink == kInkWord ? 0.52 : pick.ink == kInkFigure ? 0.96 :
                         pick.ink == kInkLime ? 0.90 : 0.54;
        color = mix(color, ink, strength * cover);
    }

    // Arrive out of the dark; a little noise keeps the gradients from banding.
    color *= smoothstep(0.0, 0.5, seconds);
    color += (hash21(pixel + fract(seconds) * 61.0) - 0.5) / 255.0 * 1.4;
    return clamp(color, 0.0, 1.0);
}
