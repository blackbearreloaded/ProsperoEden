// ProsperoEden - Launcher: a newer release of the app. The offer (Update now / Skip) each time the
// app opens while one is listed; then a ring fills while it downloads and unpacks, and ProsperoEden
// closes for the update helper to replace its files.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pe/ui/launcher.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace pe::ui
{

using audio::Cue;

namespace
{

// The panel with buttons; while the update works it is shorter, and stays centred.
constexpr Rect kPanel{560.0f, 196.0f, 800.0f, 688.0f};
constexpr float kWorkingHeight = 604.0f;
constexpr float kCenterX = 960.0f;
// The badge and the ring, under the top of the panel.
constexpr float kRingY = 384.0f;
constexpr float kRingRadius = 96.0f;
constexpr float kRingWidth = 10.0f;
// The two buttons, side by side.
constexpr float kButtonsTop = 704.0f;
constexpr float kButtonWidth = 340.0f;
constexpr float kButtonHeight = 76.0f;
constexpr float kButtonLeft = 612.0f;
constexpr float kButtonGap = 356.0f;
// How long "closes now" shows before the app closes.
constexpr float kClosingSeconds = 3.0f;
constexpr float kPi = 3.14159265f;

std::string fill_text(std::string_view pattern, const std::string &value)
{
    return fill(pattern, {value});
}

std::string megabytes(std::uint64_t bytes)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return text;
}

// An arc from `start` (radians, 0 at the right, clockwise on screen) over `sweep`, as short round
// strokes.
void arc(gfx::DrawList &list, float cx, float cy, float radius, float width, float start, float sweep, Color color)
{
    if (sweep <= 0.001f)
        return;
    const int steps = std::max(2, static_cast<int>(sweep / (kPi / 60.0f)));
    float x = cx + radius * std::cos(start);
    float y = cy + radius * std::sin(start);
    for (int i = 1; i <= steps; ++i)
    {
        const float a = start + sweep * static_cast<float>(i) / static_cast<float>(steps);
        const float nx = cx + radius * std::cos(a);
        const float ny = cy + radius * std::sin(a);
        list.line(x, y, nx, ny, width, color);
        x = nx;
        y = ny;
    }
}

} // namespace

void Launcher::begin_update()
{
    update_status_ = {};
    update_fraction_.snap(0.0f);
    update_rate_ = 0.0f;
    update_rate_done_ = 0;
    update_rate_wait_ = 0.0f;
    update_stage_time_ = 0.0f;
    if (services_.start_update())
    {
        update_stage_ = UpdateStage::working;
        cue(Cue::select);
        return;
    }
    update_stage_ = UpdateStage::failed;
    update_status_.error = "The update helper could not start";
    update_choice_ = 0;
    update_choice_x_.snap(0.0f);
    cue(Cue::error);
}

void Launcher::update_install(float dt)
{
    update_choice_x_.target = static_cast<float>(update_choice_);
    update_choice_x_.update(dt, theme::kCursorSpring);
    if (modal_shown_ != Modal::update)
        return;
    const bool buttons = update_stage_ == UpdateStage::offer || update_stage_ == UpdateStage::failed;
    update_height_.target = buttons ? kPanel.h : kWorkingHeight;
    update_height_.update(dt, 13.0f);
    update_stage_time_ += dt;
    update_spin_ += dt;

    if (update_stage_ == UpdateStage::working || update_stage_ == UpdateStage::cancelling)
    {
        update_status_ = services_.update_status();
        const UpdatePhase phase = update_status_.phase;
        if (phase == UpdatePhase::cancelled)
        {
            services_.finish_update();
            modal_ = Modal::none;
            cue(Cue::modal_close);
            return;
        }
        if (phase == UpdatePhase::failed)
        {
            services_.finish_update();
            update_stage_ = UpdateStage::failed;
            update_stage_time_ = 0.0f;
            update_choice_ = 0;
            update_choice_x_.snap(0.0f);
            cue(Cue::error);
            return;
        }
        if (phase == UpdatePhase::ready && update_stage_ == UpdateStage::working)
        {
            if (services_.apply_update())
            {
                update_stage_ = UpdateStage::closing;
                update_stage_time_ = 0.0f;
                update_fraction_.target = 1.0f;
                cue(Cue::saved);
            }
            else
            {
                services_.finish_update();
                update_stage_ = UpdateStage::failed;
                update_stage_time_ = 0.0f;
                update_status_.error = "The update helper did not answer";
                update_choice_ = 0;
                update_choice_x_.snap(0.0f);
                cue(Cue::error);
            }
            return;
        }
        // The share done, for the ring and the bar: the download, then unpacking from where it
        // stands (it has no share of its own when the helper does not say one).
        if (update_status_.total > 0)
            update_fraction_.target =
                static_cast<float>(static_cast<double>(update_status_.done) / static_cast<double>(update_status_.total));
        else if (phase == UpdatePhase::unpacking)
            update_fraction_.target = 1.0f;
        // The speed, for the time left: sampled twice a second, smoothed.
        update_rate_wait_ += dt;
        if (phase == UpdatePhase::downloading && update_rate_wait_ >= 0.5f)
        {
            const std::uint64_t done = update_status_.done;
            const float now = done >= update_rate_done_ ? static_cast<float>(done - update_rate_done_) / update_rate_wait_
                                                        : 0.0f;
            update_rate_ = update_rate_ <= 0.0f ? now : update_rate_ * 0.75f + now * 0.25f;
            update_rate_done_ = done;
            update_rate_wait_ = 0.0f;
        }
    }
    update_fraction_.update(dt, 9.0f);

    // Closing: once the message has been read, the app ends; the helper finishes the update.
    if (update_stage_ == UpdateStage::closing && update_stage_time_ >= kClosingSeconds)
        done_ = true;
}

void Launcher::press_update(Key key)
{
    switch (update_stage_)
    {
    case UpdateStage::offer:
    case UpdateStage::failed:
        if (key == Key::left || key == Key::right)
        {
            const int choice = key == Key::right ? 1 : 0;
            if (choice != update_choice_)
            {
                update_choice_ = choice;
                cue(Cue::focus);
            }
            return;
        }
        if (key == Key::cross && update_choice_ == 0)
            return begin_update();
        if (key == Key::cross || key == Key::circle)
        {
            // Skipped: asked again the next time the app opens.
            modal_ = Modal::none;
            cue(Cue::modal_close);
        }
        return;
    case UpdateStage::working:
        if (key == Key::circle)
        {
            services_.cancel_update();
            update_stage_ = UpdateStage::cancelling;
            update_stage_time_ = 0.0f;
            cue(Cue::back);
        }
        return;
    case UpdateStage::cancelling:
    case UpdateStage::closing:
        return;
    }
}

void Launcher::draw_update(Canvas &c, float open)
{
    gfx::DrawList &list = c.list;
    list.push_opacity(open);
    list.push_transform(1.0f - 0.04f * (1.0f - open) * motion(), kCenterX, 540.0f, 0.0f,
                        (1.0f - open) * 30.0f * motion());
    // Shorter while it works: its content moves down by half of what the panel lost.
    const float height = std::clamp(update_height_.value, kWorkingHeight - 20.0f, kPanel.h + 20.0f);
    const float lowered = (kPanel.h - height) * 0.5f;
    list.push_transform(1.0f, 0.0f, 0.0f, 0.0f, lowered);
    glass(c, {kPanel.x, kPanel.y, kPanel.w, height}, 28.0f, theme::kPanel.with_alpha(0.97f),
          theme::kLime.with_alpha(0.38f), 1.8f);
    const float hints = kPanel.y + height - 54.0f;

    const float t = update_stage_time_;
    const float breathe = 0.5f + 0.5f * std::sin(c.time * 2.4f);
    const bool failed = update_stage_ == UpdateStage::failed;
    const Color accent = failed ? theme::kWarning : theme::kLime;

    // A soft glow behind the ring, breathing while it waits.
    list.shadow({kCenterX - kRingRadius, kRingY - kRingRadius, kRingRadius * 2.0f, kRingRadius * 2.0f},
                kRingRadius, 60.0f, accent.with_alpha(0.10f + 0.08f * breathe));
    list.circle(kCenterX, kRingY, kRingRadius - kRingWidth, theme::kBase.with_alpha(0.55f));
    list.ring(kCenterX, kRingY, kRingRadius, kRingWidth, theme::kPanelEdge.with_alpha(0.22f));

    const auto centred = [&](std::string_view value, float top, float line, float size, Color color)
    { text_shrink(c, value, kCenterX, baseline(top, line, size), size, color, kPanel.w - 96.0f, Align::center); };
    const auto buttons = [&](const char *first, const char *second)
    {
        for (int i = 0; i < 2; ++i)
        {
            const Rect r{kButtonLeft + kButtonGap * static_cast<float>(i), kButtonsTop, kButtonWidth, kButtonHeight};
            plate_rest(c, kRowPlate, r);
        }
        const float x = kButtonLeft + kButtonGap * update_choice_x_.value;
        plate_focus(c, kRowPlate, {x, kButtonsTop, kButtonWidth, kButtonHeight}, 1.0f - 0.25f * press_);
        for (int i = 0; i < 2; ++i)
        {
            const float left = kButtonLeft + kButtonGap * static_cast<float>(i);
            text_shrink(c, tr(i == 0 ? first : second), left + kButtonWidth * 0.5f,
                        baseline(kButtonsTop, kButtonHeight, theme::kText24), theme::kText24,
                        i == update_choice_ ? theme::kTitle : theme::kValue, kButtonWidth - 40.0f, Align::center);
        }
    };

    switch (update_stage_)
    {
    case UpdateStage::offer:
    {
        // The ring fills once as the dialog rises, around an arrow that settles into its tray.
        const float fill = tween::cubic_out(t / 0.9f);
        arc(list, kCenterX, kRingY, kRingRadius, kRingWidth, -kPi * 0.5f, 2.0f * kPi * fill, accent);
        const float drop = (1.0f - tween::back_out(t / 0.7f)) * -26.0f * motion();
        const float bob = std::sin(c.time * 2.2f) * 3.0f * motion();
        const float ay = kRingY - 4.0f + drop + bob;
        list.line(kCenterX, ay - 30.0f, kCenterX, ay + 16.0f, 6.0f, accent);
        list.line(kCenterX - 18.0f, ay - 2.0f, kCenterX, ay + 16.0f, 6.0f, accent);
        list.line(kCenterX + 18.0f, ay - 2.0f, kCenterX, ay + 16.0f, 6.0f, accent);
        list.line(kCenterX - 30.0f, kRingY + 38.0f, kCenterX + 30.0f, kRingY + 38.0f, 6.0f, accent.with_alpha(0.85f));

        centred(tr("Update available"), 506.0f, 60.0f, theme::kDisplay, theme::kTitle);
        centred(fill_text(tr("Version {0} is ready to install."), update_version_), 570.0f, 34.0f, theme::kText24,
                theme::kValue);
        if (update_.size > 0)
            centred(fill_text(tr("Download size: {0}"), megabytes(update_.size)), 606.0f, 30.0f, theme::kSmall,
                    theme::kLimePale);
        centred(tr("Your games, saves and settings are kept."), 640.0f, 28.0f, theme::kSmall, theme::kCopy);
        centred(tr("ProsperoEden closes to finish the update."), 668.0f, 28.0f, theme::kSmall, theme::kCopy);
        buttons(TR("Update now"), TR("Skip"));
        static constexpr Hint kOfferHints[] = {
            {Pad::leftright, TR("Navigate")}, {Pad::cross, TR("Select")}, {Pad::circle, TR("Skip")}};
        draw_hints(c, kOfferHints, 3, kPanel.x + 52.0f, hints, theme::kCopy, kPanel.w - 104.0f);
        break;
    }
    case UpdateStage::working:
    case UpdateStage::cancelling:
    {
        const UpdatePhase phase = update_status_.phase;
        const bool measured = phase == UpdatePhase::downloading && update_status_.total > 0 &&
                              update_stage_ == UpdateStage::working;
        const float share = tween::clamp01(update_fraction_.value);
        if (measured)
        {
            arc(list, kCenterX, kRingY, kRingRadius, kRingWidth, -kPi * 0.5f, 2.0f * kPi * share, accent);
            // A bright head at the arc's end.
            const float a = -kPi * 0.5f + 2.0f * kPi * share;
            list.circle(kCenterX + kRingRadius * std::cos(a), kRingY + kRingRadius * std::sin(a), kRingWidth * 0.9f,
                        theme::kLimePale.with_alpha(0.9f));
            const int percent = static_cast<int>(share * 100.0f + 0.5f);
            text(c, fill_text(tr("{0}%"), std::to_string(std::min(percent, 100))), kCenterX,
                 baseline(kRingY - 30.0f, 60.0f, theme::kDisplay), theme::kDisplay, theme::kTitle, Align::center);
        }
        else
        {
            // Waiting without a share: an arc that turns and breathes, and three dots.
            const float turn = update_spin_ * 4.2f;
            const float sweep = kPi * (0.55f + 0.45f * std::sin(update_spin_ * 2.1f));
            arc(list, kCenterX, kRingY, kRingRadius, kRingWidth, turn, sweep,
                update_stage_ == UpdateStage::cancelling ? theme::kWarning : accent);
            for (int i = 0; i < 3; ++i)
            {
                const float wave = 0.5f + 0.5f * std::sin(update_spin_ * 6.0f - static_cast<float>(i) * 0.9f);
                list.circle(kCenterX - 26.0f + 26.0f * static_cast<float>(i), kRingY - wave * 8.0f * motion(), 7.0f,
                            theme::kLimePale.with_alpha(0.35f + 0.6f * wave));
            }
        }

        const char *headline = update_stage_ == UpdateStage::cancelling ? TR("Cancelling")
                               : phase == UpdatePhase::downloading      ? TR("Downloading")
                               : phase == UpdatePhase::unpacking        ? TR("Unpacking")
                                                                        : TR("Preparing");
        centred(tr(headline), 512.0f, 46.0f, theme::kHeading, theme::kTitle);
        centred(fill_text(tr("Version {0}"), update_version_), 560.0f, 30.0f, theme::kSmall, theme::kCopy);

        // The bar: the share, or a light running along it while there is none.
        const Rect bar{kPanel.x + 96.0f, 618.0f, kPanel.w - 192.0f, 8.0f};
        list.rounded_rect(bar, 4.0f, theme::kPanelEdge.with_alpha(0.22f));
        if (measured || phase == UpdatePhase::unpacking)
        {
            list.rounded_rect({bar.x, bar.y, std::max(bar.h, bar.w * share), bar.h}, 4.0f, accent);
            // A sheen sliding over the filled part.
            const float sheen = std::fmod(update_spin_ * 0.6f, 1.0f);
            const float sx = bar.x + bar.w * share * sheen;
            list.push_clip({bar.x, bar.y, bar.w * share, bar.h});
            list.rounded_rect({sx - 40.0f, bar.y, 80.0f, bar.h}, 4.0f, theme::kLimePale.with_alpha(0.45f));
            list.pop_clip();
        }
        else
        {
            const float run = std::fmod(update_spin_ * 0.8f, 1.4f) - 0.2f;
            list.push_clip(bar);
            list.rounded_rect({bar.x + bar.w * run - 90.0f, bar.y, 180.0f, bar.h}, 4.0f, accent.with_alpha(0.8f));
            list.pop_clip();
        }

        // Bytes, and the time left once the speed is known.
        if (measured)
        {
            std::string line = megabytes(update_status_.done) + "  /  " + megabytes(update_status_.total);
            if (update_rate_ > 1.0f && update_stage_time_ > 1.5f && update_status_.total > update_status_.done)
            {
                const float seconds =
                    static_cast<float>(update_status_.total - update_status_.done) / update_rate_;
                const int whole = std::max(1, static_cast<int>(std::ceil(seconds)));
                line += "  ·  ";
                line += whole < 90 ? fill_text(tr("About {0} s left"), std::to_string(whole))
                                   : fill_text(tr("About {0} min left"), std::to_string((whole + 59) / 60));
            }
            centred(line, 646.0f, 32.0f, theme::kSmall, theme::kLimePale);
        }
        if (update_stage_ == UpdateStage::working)
        {
            static constexpr Hint kWorkingHints[] = {{Pad::circle, TR("Cancel")}};
            draw_hints(c, kWorkingHints, 1, kPanel.x + 52.0f, hints, theme::kCopy, kPanel.w - 104.0f);
        }
        break;
    }
    case UpdateStage::closing:
    {
        // The ring closes, then a tick draws itself and the badge pops.
        arc(list, kCenterX, kRingY, kRingRadius, kRingWidth, -kPi * 0.5f, 2.0f * kPi, accent);
        const float pop = tween::back_out(t / 0.5f);
        list.push_transform(0.6f + 0.4f * pop, kCenterX, kRingY, 0.0f, 0.0f);
        list.circle(kCenterX, kRingY, kRingRadius - kRingWidth - 10.0f, accent.with_alpha(0.16f));
        const float stroke = tween::cubic_out((t - 0.15f) / 0.45f);
        const float x0 = kCenterX - 34.0f, y0 = kRingY + 2.0f;
        const float x1 = kCenterX - 10.0f, y1 = kRingY + 26.0f;
        const float x2 = kCenterX + 38.0f, y2 = kRingY - 26.0f;
        const float first = tween::clamp01(stroke / 0.4f);
        const float second = tween::clamp01((stroke - 0.4f) / 0.6f);
        if (first > 0.0f)
            list.line(x0, y0, x0 + (x1 - x0) * first, y0 + (y1 - y0) * first, 9.0f, accent);
        if (second > 0.0f)
            list.line(x1, y1, x1 + (x2 - x1) * second, y1 + (y2 - y1) * second, 9.0f, accent);
        list.pop_transform();

        centred(tr("Update ready"), 506.0f, 60.0f, theme::kDisplay, theme::kTitle);
        centred(tr("ProsperoEden closes now."), 568.0f, 34.0f, theme::kText24, theme::kValue);
        centred(fill_text(tr("Open it again to use version {0}."), update_version_), 602.0f, 30.0f, theme::kSmall,
                theme::kCopy);
        // The time until it closes.
        const float left = 1.0f - tween::clamp01(t / kClosingSeconds);
        list.rounded_rect({kPanel.x + 96.0f, 640.0f, (kPanel.w - 192.0f) * left, 4.0f}, 2.0f, accent.with_alpha(0.7f));
        break;
    }
    case UpdateStage::failed:
    {
        arc(list, kCenterX, kRingY, kRingRadius, kRingWidth, -kPi * 0.5f, 2.0f * kPi * tween::cubic_out(t / 0.6f),
            accent);
        // A gentle shake as it arrives, and an exclamation mark.
        const float shake = std::sin(t * 38.0f) * 10.0f * (1.0f - tween::clamp01(t / 0.45f)) * motion();
        list.line(kCenterX + shake, kRingY - 40.0f, kCenterX + shake, kRingY + 12.0f, 10.0f, accent);
        list.circle(kCenterX + shake, kRingY + 38.0f, 7.0f, accent);

        centred(tr("The update could not finish"), 506.0f, 60.0f, theme::kHeading, theme::kTitle);
        centred(tr("ProsperoEden was not changed."), 570.0f, 34.0f, theme::kText24, theme::kValue);
        if (!update_status_.error.empty())
            centred(update_status_.error, 612.0f, 30.0f, theme::kSmall, theme::kCopy.with_alpha(0.8f));
        buttons(TR("Try again"), TR("Close"));
        static constexpr Hint kFailedHints[] = {
            {Pad::leftright, TR("Navigate")}, {Pad::cross, TR("Select")}, {Pad::circle, TR("Close")}};
        draw_hints(c, kFailedHints, 3, kPanel.x + 52.0f, hints, theme::kCopy, kPanel.w - 104.0f);
        break;
    }
    }
    list.pop_transform();
    list.pop_transform();
    list.pop_opacity();
}

} // namespace pe::ui
