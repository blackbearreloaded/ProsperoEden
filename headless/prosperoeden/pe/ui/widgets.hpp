// ProsperoEden - Launcher building blocks: backdrop, panels, plates, lists and hints.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "pe/core/strings.hpp"
#include "pe/core/tween.hpp"
#include "pe/ui/textures.hpp"
#include "pe/ui/theme.hpp"

#include <array>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>

namespace pe::ui
{

struct Fonts
{
    const gfx::Font *font = nullptr;
    std::uint32_t texture = 0;
};

// The animated background: the dusk art drifting slowly, a breathing glow at
// the sun and a few motes of light. Panels sample its blurred copy.
class Backdrop
{
  public:
    void update(float dt);
    // dim darkens the art (0 home, more behind full screens).
    void draw(gfx::DrawList &list, const Textures &textures, float dim) const;
    // Where the art lies in virtual pixels this frame.
    Rect art() const;
    // The part of the (blurred) art behind r, as a UV rectangle.
    Rect uv(const Rect &r) const;

  private:
    float time_ = 0.0f;
};

// What every draw function receives.
struct Canvas
{
    gfx::DrawList &list;
    const Fonts &fonts;
    Textures &textures;
    const Backdrop &backdrop;
    float time = 0.0f; // seconds since the launcher appeared
};

// ---- text ----
// Sizes are the design's: text is drawn at text_size(size), staying centred on its line.
float text(Canvas &c, std::string_view value, float x, float baseline, float size, Color color,
           Align align = Align::left, float tracking = 0.0f);
// How wide text() draws value.
float text_width(Canvas &c, std::string_view value, float size, float tracking = 0.0f);
// One line, ending in an ellipsis when it would pass max_width.
float text_fit(Canvas &c, std::string_view value, float x, float baseline, float size, Color color,
               float max_width, Align align = Align::left);
// How far the letters of translated text may shrink to fit their place.
constexpr float kShrink = 0.78f;
// One line of the launcher's own text: a translation longer than max_width gets smaller letters
// (down to `least` of size) before an ellipsis cuts it.
float text_shrink(Canvas &c, std::string_view value, float x, float baseline, float size,
                  Color color, float max_width, Align align = Align::left, float tracking = 0.0f,
                  float least = kShrink);
// Up to max_lines wrapped lines; the last one ends in an ellipsis when text remains. With `least`
// under 1 the letters shrink that far first (pass kShrink for the launcher's own text).
void text_block(Canvas &c, std::string_view value, float x, float first_baseline, float size,
                float line_height, Color color, float max_width, int max_lines, float least = 1.0f);
// Where the values of a group of labelled lines start: `column`, or further right when one of
// the labels (drawn from `left`) is longer than the English one.
float value_column(Canvas &c, std::initializer_list<std::string_view> labels, float left,
                   float column, float size, float tracking = 0.0f);

// A warning is marked, not only coloured: a "!" in a ring before the text.
// One line that may be a warning; fitted like text_shrink (the mark's room comes out of max_width).
float notice(Canvas &c, std::string_view value, float x, float baseline, float size, Color color,
             float max_width, bool warning, Align align = Align::left);
// The same for wrapped text (see text_block).
void notice_block(Canvas &c, std::string_view value, float x, float first_baseline, float size,
                  float line_height, Color color, float max_width, int max_lines, bool warning);

// For the PC preview: told of every line of the launcher's own text that had to shrink (scale
// under 1) or was cut short, so a translation that does not fit its place shows up.
using FitReport = void (*)(std::string_view text, float scale, bool cut);
void set_fit_report(FitReport report);

// ---- surfaces ----
// A panel of frosted glass: soft shadow, the blurred art behind it, a tint and a hairline edge.
void glass(Canvas &c, const Rect &r, float radius, Color tint, Color edge, float shadow = 1.0f);

// How a focusable surface looks at rest and under the highlight.
struct Plate
{
    float radius;
    Color fill;
    Color edge;
    Color focus_base;  // opaque-ish layer under the gradient (alpha 0: none)
    Color focus_left;  // the highlight gradient
    Color focus_right;
    Color focus_edge;
};
extern const Plate kRowPlate;     // settings and dialog rows
extern const Plate kListPlate;    // library, folder and language rows
extern const Plate kButtonPlate;  // home hero buttons
extern const Plate kTilePlate;    // recently played tiles
extern const Plate kNavPlate;     // header navigation
// The surface at rest, with `focus` (0..1) of the highlight blended over it.
void plate(Canvas &c, const Plate &style, const Rect &r, float focus);
// The surface at rest only (lists draw one gliding highlight over their rows).
void plate_rest(Canvas &c, const Plate &style, const Rect &r);
// The highlight alone.
void plate_focus(Canvas &c, const Plate &style, const Rect &r, float amount);

// A cover (or the app icon while it loads or when there is none), with rounded corners.
void cover(Canvas &c, const std::string &path, const Rect &r, float radius, float shadow = 0.0f);
// An arc from `start` (radians, 0 at the right, clockwise on screen) over `sweep`: a ring that
// fills, or a part of one that turns while something works.
void arc(gfx::DrawList &list, float cx, float cy, float radius, float width, float start, float sweep, Color color);

// A controller r.w wide (r.h is 25/36 of that): a faint outline at lit 0, white with its light
// bar glowing at lit 1.
void controller_icon(Canvas &c, const Rect &r, float lit);
// A switch: knob at `position` (0 off .. 1 on).
void toggle(Canvas &c, float right, float cy, float position);
// A level bar ending at `right`: `level` 0..1.
void level_bar(Canvas &c, float right, float cy, float width, float level, float focus);
// "< value >" for an option row; the chevrons appear with focus. Returns the width it took.
float chooser(Canvas &c, std::string_view value, float right, float baseline, float focus,
              Color color);

// ---- controller hints ----
enum class Pad : std::uint8_t
{
    none,
    cross,
    circle,
    square,
    triangle,
    dpad,
    updown,
    leftright,
    l1,
    r1,
};
struct Hint
{
    Pad button = Pad::none;
    const char *label = ""; // English, marked with TR(): draw_hints translates it
    Pad second = Pad::none; // a pair such as L1 R1
};
float pad_width(Pad button, float size);
void draw_pad(Canvas &c, Pad button, float x, float cy, float size, float alpha = 1.0f);
// Hints left to right from x with their icons centred on cy; returns the width used. They are
// drawn smaller when they would pass max_width (0: no limit).
float draw_hints(Canvas &c, const Hint *hints, int count, float x, float cy, Color color,
                 float max_width = 0.0f);

// "3 OF 12" under a list.
std::string list_position(int selected, int count);

// ---- lists ----
// A scrolling list of equal rows: the selection keeps inside the window, the
// scroll and the highlight follow it on springs.
struct ListView
{
    int count = 0;
    int selected = 0;
    int visible = 7;
    float pitch = 88.0f; // row to row
    bool wrap = true;

    void reset(int new_count, int new_selected);
    // Moves by one row (wrapping) or by a page (stopping at the ends); true when it moved.
    bool move(int delta);
    bool page(int delta);
    void update(float dt);
    // Scroll offset and highlight position in pixels from the first row.
    float scroll() const
    {
        return scroll_.value;
    }
    float cursor() const
    {
        return cursor_.value;
    }
    // The range of rows that can be seen this frame.
    int first_row() const;
    int last_row() const;
    // 0..1: how far a row is inside the window (rows fade at its edges).
    float row_alpha(int row, float row_height) const;
    // 0..1 position and size of the scroll thumb.
    float thumb() const;

  private:
    void follow();
    int top_ = 0;
    tween::Spring scroll_;
    tween::Spring cursor_;
};
// The scroll bar of a list at (x, y) with the window's height.
void scrollbar(Canvas &c, const ListView &view, float x, float y, float height);

} // namespace pe::ui
