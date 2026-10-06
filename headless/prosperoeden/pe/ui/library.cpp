// ProsperoEden - Launcher library: the game list, its details and per-game settings.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pe/ui/launcher.hpp"

#include "pe/core/log.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <string>

namespace pe::ui
{

using audio::Cue;

namespace
{

constexpr Rect kListPanel{108.0f, 188.0f, 820.0f, 720.0f};
constexpr Rect kDetailPanel{980.0f, 188.0f, 820.0f, 720.0f};
constexpr Rect kWindow{138.0f, 250.0f, 760.0f, 606.0f};
constexpr float kRowHeight = 78.0f;
// Under the game's details: its console mode, then the switch for its mods.
constexpr Rect kModeRow{1022.0f, 700.0f, 736.0f, 74.0f};
constexpr Rect kModsRow{1022.0f, 782.0f, 736.0f, 74.0f};
constexpr Rect kDialog{550.0f, 180.0f, 820.0f, 720.0f};

// The game settings dialog's rows, and the window that shows five of them (also the Mods list's).
// Video to Language open what the game does differently from Settings (game_options.cpp).
enum GameRow : int
{
    row_mode,
    row_video,
    row_performance,
    row_audio,
    row_controls,
    row_language,
    row_mods,
    row_save,   // in builds that move saves
    row_delete, // a game a download source has: deleted from the console
};
constexpr float kDialogRowsTop = 334.0f;
constexpr float kDialogRowPitch = 96.0f;
constexpr float kDialogRowHeight = 94.0f;
constexpr int kDialogRowsShown = 5;
constexpr Rect kDialogWindow{592.0f, kDialogRowsTop, 736.0f,
                             kDialogRowPitch * (kDialogRowsShown - 1) + kDialogRowHeight};
constexpr float kDialogHints = 848.0f;

// Console mode marks, drawn from lines: a screen on its stand, and a handheld.
void draw_docked(Canvas &c, float x, float cy, Color ink)
{
    c.list.bordered_rect({x, cy - 13.0f, 36.0f, 22.0f}, 3.0f, ink.with_alpha(0.0f), 2.0f, ink);
    c.list.line(x + 11.0f, cy + 14.0f, x + 25.0f, cy + 14.0f, 2.0f, ink);
}

void draw_handheld(Canvas &c, float x, float cy, Color ink)
{
    c.list.bordered_rect({x, cy - 10.0f, 40.0f, 20.0f}, 6.0f, ink.with_alpha(0.0f), 2.0f, ink);
    c.list.circle(x + 7.0f, cy, 2.2f, ink);
    c.list.circle(x + 33.0f, cy, 2.2f, ink);
}

} // namespace

void Launcher::start_scan()
{
    if (!scan_.valid() && home_.setup_ready)
        scan_ = std::async(std::launch::async, [this] { return services_.games(); });
}

void Launcher::finish_scan(bool wait)
{
    if (!scan_.valid())
        return;
    if (!wait && scan_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    try
    {
        apply_games(scan_.get());
    }
    catch (const std::exception &error)
    {
        sys::log("game list: %s", error.what());
        games_loaded_ = true; // the list stays as it was
    }
}

void Launcher::apply_games(std::vector<Game> games)
{
    // The same games in the same order (the usual case) keep the list where it is; otherwise
    // the selection follows its game.
    bool same = games_loaded_ && games.size() == games_.size();
    for (std::size_t i = 0; same && i < games.size(); ++i)
        same = games[i].file == games_[i].file;
    const std::string selected = library_.selected < static_cast<int>(games_.size()) ?
                                     games_[static_cast<std::size_t>(library_.selected)].file :
                                     std::string{};
    games_ = std::move(games);
    games_loaded_ = true;
    for (Game &game : games_)
        if (game.title_id != 0)
            count_mods(game, services_.mods(game.title_id));
    if (!same)
    {
        int index = 0;
        for (int i = 0; i < static_cast<int>(games_.size()); ++i)
            if (games_[static_cast<std::size_t>(i)].file == selected)
                index = i;
        library_.reset(static_cast<int>(games_.size()), index);
        refresh_selected_game();
        mode_.snap(selected_docked_ ? 0.0f : 1.0f);
    }
    name_home_games();
}

void Launcher::name_home_games()
{
    // Games carry their own names; until one has been read the home screen names it by its file.
    for (const Game &game : games_)
    {
        if (game.file == home_.last_file)
            home_.last_title = game.name;
        for (Recent &recent : home_.recents)
            if (recent.file == game.file)
                recent.title = game.name;
    }
}

void Launcher::read_home()
{
    home_ = services_.home();
    if (home_.last_title_id == 0)
        return;
    const std::vector<Mod> mods = services_.mods(home_.last_title_id);
    home_.last_mods = static_cast<int>(mods.size());
    // With the game's Mods switch off none of them is on.
    const bool enabled = mods.empty() || services_.mods_enabled(home_.last_title_id);
    home_.last_mods_on = !enabled ? 0 : static_cast<int>(
        std::count_if(mods.begin(), mods.end(), [](const Mod &mod) { return mod.enabled; }));
}

void Launcher::check_games_present()
{
    // The home screen: its game, or one of its recent ones, is gone. Its focus stays where it can.
    if (screen_ == Screen::home && modal_ == Modal::none)
    {
        bool gone = home_.last_exists && !services_.game_exists(home_.last_file);
        for (const Recent &recent : home_.recents)
            gone = gone || !services_.game_exists(recent.file);
        if (gone)
        {
            read_home();
            const int recents = static_cast<int>(home_.recents.size());
            if (home_focus_ >= 5 && home_focus_ < 9 && home_focus_ - 5 >= recents)
                home_focus_ = recents > 0 ? 4 + recents : 9;
            if (home_focus_ == 4 && !home_.last_exists)
                home_focus_ = 0;
        }
    }
    // The Library's list, whatever screen shows: a game's own dialogs stay with their game.
    if (modal_ == Modal::none)
        drop_missing_games();
}

bool Launcher::drop_missing_games()
{
    if (!games_loaded_)
        return false;
    const std::string selected =
        library_.selected < static_cast<int>(games_.size()) ? games_[static_cast<std::size_t>(library_.selected)].file :
                                                              std::string{};
    // A game on download sources has no file yet: it leaves when their lists do not have it.
    const auto gone = std::remove_if(games_.begin(), games_.end(), [this](const Game &game)
                                     { return !game.remote && !services_.game_exists(game.file); });
    if (gone == games_.end())
        return false;
    games_.erase(gone, games_.end());
    // The selection stays on its game, or on the one that took its place.
    int index = std::min(library_.selected, std::max(0, static_cast<int>(games_.size()) - 1));
    for (int i = 0; i < static_cast<int>(games_.size()); ++i)
        if (games_[static_cast<std::size_t>(i)].file == selected)
            index = i;
    library_.reset(static_cast<int>(games_.size()), index);
    refresh_selected_game();
    return true;
}

void Launcher::count_mods(Game &game, const std::vector<Mod> &mods)
{
    game.mods = static_cast<int>(mods.size());
    // The game's Mods switch (the Library's) comes first: off, none of its mods is used, whatever
    // their own switches say.
    game.mods_enabled = mods.empty() || services_.mods_enabled(game.title_id);
    game.mods_on = !game.mods_enabled ? 0 : static_cast<int>(
        std::count_if(mods.begin(), mods.end(), [](const Mod &mod) { return mod.enabled; }));
    // The home screen says the same of its game.
    if (game.file == home_.last_file && home_.last_title_id != 0)
    {
        home_.last_mods = game.mods;
        home_.last_mods_on = game.mods_on;
    }
}

void Launcher::read_mods(Game &game)
{
    mods_ = services_.mods(game.title_id);
    count_mods(game, mods_);
    mod_list_.clear();
    for (int mod = 0; mod < static_cast<int>(mods_.size()); ++mod)
    {
        mod_list_.push_back({mod, -1});
        for (int cheat = 0; cheat < static_cast<int>(mods_[static_cast<std::size_t>(mod)].cheats.size());
             ++cheat)
            mod_list_.push_back({mod, cheat});
    }
}

std::string Launcher::addons_line(const std::string &addons, int mods, int mods_on, bool brief)
{
    std::string line = addons;
    if (mods > 0)
    {
        // Mods that are switched off are still there: "1 of 2 mods on", or "1/2 mods" where there
        // is little room.
        const std::string count = std::to_string(mods);
        const char *counted = mods == 1 ? tr("{0} mod") : tr("{0} mods");
        line += (line.empty() ? "" : ", ") +
                (mods_on == mods ? fill(counted, {count}) :
                 brief ? fill(counted, {std::to_string(mods_on) + "/" + count}) :
                         fill(tr("{0} of {1} mods on"), {std::to_string(mods_on), count}));
    }
    return line.empty() ? std::string{tr("None")} : line;
}

std::string Launcher::hertz(int refresh)
{
    return fill(tr("{0} Hz"), {refresh == 1 ? "120" : "60"});
}

void Launcher::enter_library()
{
    if (games_loaded_)
    {
        // Games copied to the console since the list was read appear in a moment; games taken
        // away leave it at once.
        finish_scan(false);
        drop_missing_games();
        start_scan();
    }
    else
    {
        start_scan();
        finish_scan(true);
    }
    library_.visible = 7;
    library_.pitch = 88.0f;
    library_.reset(static_cast<int>(games_.size()), 0);
    refresh_selected_game();
    detail_.snap(1.0f);
    mode_.snap(selected_docked_ ? 0.0f : 1.0f);
}

void Launcher::refresh_selected_game()
{
    const std::uint64_t id =
        games_.empty() ? 0 : games_[static_cast<std::size_t>(library_.selected)].title_id;
    selected_docked_ = id == 0 || services_.docked(id);
    // Its Mods switch shows its state at once; it only animates when changed.
    mods_switch_.snap(!games_.empty() && games_[static_cast<std::size_t>(library_.selected)].mods_enabled ?
                          1.0f : 0.0f);
    detail_.value = 0.0f;
    detail_.velocity = 0.0f;
}

void Launcher::press_library(Key key)
{
    const int count = static_cast<int>(games_.size());
    const Game *game = count > 0 ? &games_[static_cast<std::size_t>(library_.selected)] : nullptr;
    switch (key)
    {
    case Key::circle:
        open(Screen::home, false);
        return;
    case Key::up:
    case Key::down:
        if (library_.move(key == Key::down ? 1 : -1))
        {
            message_.clear();
            refresh_selected_game();
            mode_.snap(selected_docked_ ? 0.0f : 1.0f);
            cue(Cue::focus);
        }
        return;
    case Key::l1:
    case Key::r1:
        if (library_.page(key == Key::r1 ? 1 : -1))
        {
            message_.clear();
            refresh_selected_game();
            mode_.snap(selected_docked_ ? 0.0f : 1.0f);
            cue(Cue::page);
        }
        return;
    case Key::left:
    case Key::right:
    {
        if (game == nullptr || game->title_id == 0)
        {
            if (game != nullptr)
                cue(Cue::error);
            return;
        }
        const bool saved = services_.set_docked(game->title_id, !selected_docked_);
        if (saved)
            selected_docked_ = !selected_docked_;
        say(saved ? tr("Saved for this game. Applies on next launch.") :
                    tr("Could not save console mode. Please try again."),
            !saved);
        cue(saved ? Cue::toggle : Cue::error);
        return;
    }
    case Key::square:
    {
        // A game on download sources: into the download queue, or out of it.
        if (game != nullptr && game->remote)
        {
            queue_remote(*game);
            return;
        }
        // The Mods switch: all of the game's mods on or off at once. Their own switches (Game
        // settings > Mods) keep their state behind it.
        if (game == nullptr || game->title_id == 0 || game->mods == 0)
        {
            if (game != nullptr)
                cue(Cue::error);
            return;
        }
        Game &chosen = games_[static_cast<std::size_t>(library_.selected)];
        const bool saved = services_.set_mods_enabled(chosen.title_id, !chosen.mods_enabled);
        if (saved)
            count_mods(chosen, services_.mods(chosen.title_id));
        say(saved ? tr("Saved for this game. Applies on next launch.") :
                    tr("Could not save. Please try again."),
            !saved);
        cue(saved ? Cue::toggle : Cue::error);
        return;
    }
    case Key::triangle:
        if (game == nullptr)
            return;
        if (game->remote)
        {
            say(tr("Download the game to change its settings."), true);
            cue(Cue::error);
            return;
        }
        if (game->title_id == 0)
        {
            say(tr("This game's settings cannot be saved (no title ID)."), true);
            cue(Cue::error);
            return;
        }
        game_settings_ = services_.game_settings(game->title_id);
        game_docked_ = selected_docked_;
        import_source_ = services_.save_transfer_available() ?
                             services_.save_import_source(game->title_id) : SaveSource::none;
        import_armed_ = false;
        delete_armed_ = false;
        read_mods(games_[static_cast<std::size_t>(library_.selected)]);
        open_modal(Modal::game);
        game_rows_.visible = kDialogRowsShown;
        game_rows_.pitch = kDialogRowPitch;
        game_rows_.reset(dialog_rows(Modal::game), 0);
        return;
    case Key::cross:
        if (game == nullptr || !home_.setup_ready)
        {
            cue(Cue::error);
            return;
        }
        if (game->remote)
        {
            play_remote(*game);
            return;
        }
        launch(game->file, game->name, game->cover);
        return;
    default:
        return;
    }
}

void Launcher::draw_library(Canvas &c)
{
    gfx::DrawList &list = c.list;
    const int count = static_cast<int>(games_.size());
    draw_frame(c, tr("Your games"), tr("Select a game to begin"));

    // ---- the list ----
    glass(c, kListPanel, 26.0f, theme::kPanel.with_alpha(0.80f), theme::kPanelEdge.with_alpha(0.55f));
    text(c, tr("LIBRARY"), 138.0f, baseline(208.0f, 28.0f, theme::kSmall), theme::kSmall,
         theme::kLimePale, Align::left, 3.0f);
    if (count == 0)
    {
        text(c, tr("No ROM files found."), kListPanel.x + kListPanel.w * 0.5f,
             baseline(488.0f, 38.0f, theme::kText24), theme::kText24, theme::kCopy, Align::center);
    }
    else
    {
        // The window is wider than its rows so the highlight's glow is not cut at the sides.
        list.push_clip({kWindow.x - 24.0f, kWindow.y - 6.0f, kWindow.w + 48.0f, kWindow.h + 12.0f});
        const auto row_rect = [&](int row) -> Rect
        {
            return {kWindow.x,
                    kWindow.y + static_cast<float>(row) * library_.pitch - library_.scroll(),
                    kWindow.w, kRowHeight};
        };
        for (int row = library_.first_row(); row <= library_.last_row(); ++row)
        {
            list.push_opacity(library_.row_alpha(row, kRowHeight));
            plate_rest(c, kListPlate, row_rect(row));
            list.pop_opacity();
        }
        plate_focus(c, kListPlate,
                    {kWindow.x, kWindow.y + library_.cursor() - library_.scroll(), kWindow.w,
                     kRowHeight},
                    1.0f);
        for (int row = library_.first_row(); row <= library_.last_row(); ++row)
        {
            const Game &game = games_[static_cast<std::size_t>(row)];
            const Rect r = row_rect(row);
            list.push_opacity(library_.row_alpha(row, kRowHeight));
            if (game.remote)
                remote_cover(c, game.cover, {r.x + 12.0f, r.y + 11.0f, 56.0f, 56.0f}, 8.0f);
            else
                cover(c, game.cover, {r.x + 12.0f, r.y + 11.0f, 56.0f, 56.0f}, 8.0f);
            if (game.remote)
            {
                // A game on download sources: where it is or what is happening to it instead of its
                // format, and its download's progress under its name.
                Color tone = theme::kMeta;
                const std::string state = remote_state(game, &tone);
                const float taken = text_shrink(c, state, r.x + 730.0f, baseline(r.y, kRowHeight, theme::kSmall),
                                                theme::kSmall, tone, 200.0f, Align::right);
                text_fit(c, game.name, r.x + 86.0f, baseline(r.y, kRowHeight, theme::kText24), theme::kText24,
                         Color::rgb(0xf3f5e9).with_alpha(0.78f), 644.0f - taken - 20.0f);
                const Download *download = download_of(game.key);
                if (download != nullptr && download->state == DownloadState::downloading && download->total > 0)
                {
                    const float done = std::clamp(static_cast<float>(static_cast<double>(download->done) /
                                                                     static_cast<double>(download->total)),
                                                  0.0f, 1.0f);
                    list.rounded_rect({r.x + 86.0f, r.y + kRowHeight - 14.0f, 560.0f, 4.0f}, 2.0f,
                                      theme::kPanelEdge.with_alpha(0.22f));
                    list.rounded_rect({r.x + 86.0f, r.y + kRowHeight - 14.0f, std::max(4.0f, 560.0f * done), 4.0f},
                                      2.0f, theme::kLime);
                }
            }
            else
            {
                text_fit(c, game.name, r.x + 86.0f, baseline(r.y, kRowHeight, theme::kText24),
                         theme::kText24, Color::rgb(0xf3f5e9), 560.0f);
                text(c, game.format, r.x + 730.0f, baseline(r.y, kRowHeight, theme::kSmall),
                     theme::kSmall, theme::kMeta, Align::right);
            }
            list.pop_opacity();
        }
        list.pop_clip();
        scrollbar(c, library_, 910.0f, kWindow.y, kWindow.h);
    }
    text(c, list_position(count > 0 ? library_.selected + 1 : 0, count), 898.0f, baseline(866.0f, 28.0f, theme::kSmall), theme::kSmall,
         theme::kLimePale, Align::right);

    // ---- the selected game ----
    glass(c, kDetailPanel, 26.0f, theme::kPanel.with_alpha(0.80f),
          theme::kPanelEdge.with_alpha(0.55f));
    text(c, tr("GAME DETAILS"), 1016.0f, baseline(210.0f, 28.0f, theme::kSmall), theme::kSmall,
         theme::kLimePale, Align::left, 3.0f);
    const Game *game = count > 0 ? &games_[static_cast<std::size_t>(library_.selected)] : nullptr;
    const float shown = tween::clamp01(detail_.value);
    list.push_opacity(shown);
    list.push_transform(1.0f, 0.0f, 0.0f, 0.0f, (1.0f - shown) * 10.0f * motion());
    text_block(c, game != nullptr ? game->name : tr("No ROM selected"), 1016.0f,
               baseline(250.0f, 42.0f, theme::kHeading), theme::kHeading, 42.0f, theme::kTitle,
               760.0f, 2);
    if (game != nullptr && game->remote)
        remote_cover(c, game->cover, {1016.0f, 372.0f, 288.0f, 288.0f}, 14.0f, 0.9f);
    else
        cover(c, game != nullptr ? game->cover : std::string{}, {1016.0f, 372.0f, 288.0f, 288.0f},
              14.0f, 0.9f);
    if (game == nullptr || game->cover.empty())
        text_shrink(c, game == nullptr ? tr("Select a game") : tr("No cover art"), 1160.0f,
                    baseline(676.0f, 30.0f, theme::kSmall), theme::kSmall, theme::kMeta, 288.0f,
                    Align::center);
    struct Field
    {
        const char *label;
        std::string value;
        Color color;
    };
    Field fields[] = {
        {tr("FORMAT"), game != nullptr ? game->format : "-", theme::kValue},
        {tr("SIZE"), game != nullptr ? game->size : "-", theme::kValue},
        {tr("ADD-ONS"),
         game != nullptr ? addons_line(game->addons, game->mods, game->mods_on) : "-",
         game != nullptr && (!game->addons.empty() || game->mods > 0) ? theme::kLimePale :
                                                                         theme::kValue},
        {tr("LANGUAGE"), game != nullptr ? game->language : "-",
         game != nullptr && !game->language_note.empty() ? theme::kWarning : theme::kValue},
    };
    // A game on download sources: what is happening to it and where it is, instead of what it comes
    // with and its language, which are known once it is on the console.
    const bool remote = game != nullptr && game->remote;
    if (remote)
    {
        Color tone = theme::kValue;
        const std::string state = remote_state(*game, &tone);
        const bool coming = download_of(game->key) != nullptr;
        fields[2] = {tr("STATE"), coming ? state : std::string{tr("Not on the console")}, coming ? tone : theme::kValue};
        std::string sources;
        for (const std::string &name : game->sources)
            sources += (sources.empty() ? "" : ", ") + name;
        fields[3] = {game->sources.size() == 1 ? tr("SOURCE") : tr("SOURCES"), sources, theme::kValue};
    }
    for (int i = 0; i < 4; ++i)
    {
        const float line = baseline(384.0f + 42.0f * static_cast<float>(i), 30.0f, theme::kSmall);
        // The name keeps its size up to 200 wide; the value has the rest of the line.
        const float label =
            text_shrink(c, fields[i].label, 1336.0f, line, theme::kSmall, theme::kLabel, 200.0f);
        const float room = 440.0f - label - 16.0f;
        // What a game comes with is said briefly where the whole line does not fit.
        if (i == 2 && game != nullptr && !remote && text_width(c, fields[i].value, theme::kSmall) > room)
            fields[i].value = addons_line(game->addons_short, game->mods, game->mods_on, true);
        text_shrink(c, fields[i].value, 1776.0f, line, theme::kSmall, fields[i].color, room,
                    Align::right);
    }
    if (game != nullptr && !game->language_note.empty())
        notice(c, game->language_note, 1776.0f, baseline(544.0f, 28.0f, 18.0f), 18.0f,
               theme::kWarning, 440.0f, true, Align::right);
    text(c, tr("FILE"), 1336.0f, baseline(574.0f, 30.0f, theme::kSmall), theme::kSmall, theme::kLabel);
    text_block(c, game != nullptr ? game->file : "-", 1336.0f,
               baseline(606.0f, 30.0f, theme::kSmall), theme::kSmall, 30.0f, theme::kValue, 440.0f,
               3);
    list.pop_transform();
    list.pop_opacity();

    // ---- console mode ----
    const bool can_configure = game != nullptr && game->title_id != 0;
    plate_rest(c, kRowPlate, kModeRow);
    text_shrink(c, tr("Console mode"), kModeRow.x + 26.0f,
                baseline(kModeRow.y, kModeRow.h, theme::kText24), theme::kText24,
                can_configure ? theme::kValue : theme::kMeta, 256.0f);
    if (can_configure)
    {
        const Rect track{1322.0f, kModeRow.y + 8.0f, 420.0f, kModeRow.h - 16.0f};
        const float half = track.w * 0.5f;
        list.rounded_rect(track, 14.0f, Color::rgb(0x0d1814, 0.75f));
        plate_focus(c, kNavPlate, {track.x + half * mode_.value + 3.0f, track.y + 3.0f, half - 6.0f,
                                   track.h - 6.0f},
                    1.0f);
        const float cy = track.y + track.h * 0.5f;
        const Color docked = gfx::mix(theme::kLimePale, theme::kValue.with_alpha(0.45f), mode_.value);
        const Color handheld =
            gfx::mix(theme::kValue.with_alpha(0.45f), theme::kLimePale, mode_.value);
        draw_docked(c, track.x + 26.0f, cy, docked);
        text_shrink(c, tr("Docked"), track.x + 78.0f, baseline(track.y, track.h, theme::kText24),
                    theme::kText24, docked, half - 78.0f - 10.0f, Align::left, 0.0f, 0.7f);
        draw_handheld(c, track.x + half + 18.0f, cy, handheld);
        text_shrink(c, tr("Handheld"), track.x + half + 72.0f,
                    baseline(track.y, track.h, theme::kText24), theme::kText24, handheld,
                    half - 72.0f - 10.0f, Align::left, 0.0f, 0.7f);
    }
    else
    {
        text_shrink(c, tr("Unavailable"), kModeRow.x + kModeRow.w - 26.0f,
                    baseline(kModeRow.y, kModeRow.h, theme::kText24), theme::kText24, theme::kMeta,
                    400.0f, Align::right);
    }

    // ---- mods: one switch for all of the game's mods (Square) ----
    const bool has_mods = can_configure && game->mods > 0;
    plate_rest(c, kRowPlate, kModsRow);
    text_shrink(c, tr("Mods"), kModsRow.x + 26.0f, baseline(kModsRow.y, kModsRow.h, theme::kText24),
                theme::kText24, has_mods ? theme::kValue : theme::kMeta, 256.0f);
    if (has_mods)
    {
        // The switch at the right, as wide as the console mode's control is from the edge; what
        // it means for this game beside it: how many of its mods are on, or that none is.
        const float right = kModsRow.x + kModsRow.w - 16.0f;
        const float on = tween::clamp01(mods_switch_.value);
        toggle(c, right, kModsRow.y + kModsRow.h * 0.5f, on);
        const std::string state =
            game->mods_enabled ? fill(tr("{0} of {1} on"),
                                      {std::to_string(game->mods_on), std::to_string(game->mods)}) :
                                 std::string{tr("Off")};
        text_shrink(c, state, right - 64.0f - 22.0f, baseline(kModsRow.y, kModsRow.h, theme::kSmall),
                    theme::kSmall, gfx::mix(theme::kMeta, theme::kLimePale, on), 330.0f, Align::right);
    }
    else
    {
        text_shrink(c, can_configure ? tr("No mods") : tr("Unavailable"),
                    kModsRow.x + kModsRow.w - 26.0f, baseline(kModsRow.y, kModsRow.h, theme::kText24),
                    theme::kText24, theme::kMeta, 400.0f, Align::right);
    }
    // The console mode is changed with left and right (not on a game that is still on its sources).
    if (game == nullptr || !game->remote)
        draw_pad(c, Pad::leftright, 1022.0f, 876.0f, 26.0f);
    // A message said while Game settings is open belongs to that dialog.
    const bool said = !message_.empty() && modal_shown_ != Modal::game && modal_shown_ != Modal::mods &&
                      modal_shown_ != Modal::game_options && modal_shown_ != Modal::mapping;
    const Download *download = remote ? download_of(game->key) : nullptr;
    const bool failed = !said && download != nullptr && download->state == DownloadState::failed;
    const std::string hint = said ? message_ :
                             failed ? download->error :
                             remote ? tr("Not on the console yet. Download it to play and to change its settings.") :
                             can_configure ? tr("Change mode. Saved per game.") :
                                             tr("Select a readable game to configure its mode.");
    notice(c, hint, 1060.0f, 883.0f, theme::kSmall,
           said ? (message_warning_ ? theme::kWarning : theme::kLimePale) : failed ? theme::kWarning : theme::kMeta,
           700.0f, (said && message_warning_) || failed);

    if (remote)
    {
        // A game on download sources: Square puts it in the queue, or takes it out.
        static constexpr Hint kRemote[] = {{Pad::cross, TR("Download and play")},
                                           {Pad::circle, TR("Back")},
                                           {Pad::updown, TR("Browse games")},
                                           {Pad::square, TR("Download")}};
        static constexpr Hint kQueued[] = {{Pad::cross, TR("Play when downloaded")},
                                           {Pad::circle, TR("Back")},
                                           {Pad::updown, TR("Browse games")},
                                           {Pad::square, TR("Cancel download")}};
        draw_footer(c, download != nullptr ? kQueued : kRemote, 4);
        return;
    }
    static constexpr Hint kHints[] = {{Pad::cross, TR("Select")},
                                      {Pad::circle, TR("Back")},
                                      {Pad::updown, TR("Browse games")},
                                      {Pad::leftright, TR("Console mode")},
                                      {Pad::square, TR("Mods")},
                                      {Pad::triangle, TR("Game settings")}};
    draw_footer(c, kHints, 6);
}

// ---------------------------------------------------------------- game settings dialog

int Launcher::game_row(int row) const
{
    if (row < row_save)
        return row;
    return services_.save_transfer_available() ? row : row + 1;
}

void Launcher::press_game(Key key)
{
    Game &game = games_[static_cast<std::size_t>(library_.selected)];
    const int kind = game_row(option_);
    switch (key)
    {
    case Key::circle:
        close_modal();
        selected_docked_ = game_docked_;
        return;
    case Key::up:
    case Key::down:
        if (game_rows_.move(key == Key::down ? 1 : -1))
        {
            option_ = game_rows_.selected;
            message_.clear();
            import_armed_ = false;
            delete_armed_ = false;
            cue(Cue::focus);
        }
        return;
    case Key::square:
        if (kind != row_save)
            return;
        break;
    case Key::left:
    case Key::right:
    case Key::cross:
        break;
    default:
        return;
    }
    if (kind >= row_video && kind <= row_language)
    {
        // A kind of setting has its own list.
        if (key == Key::cross)
            open_game_options(kind - row_video);
        return;
    }
    if (kind == row_mods)
    {
        // The game's mods have their own list. It is read again: mods may have been copied in
        // since this dialog opened.
        if (key != Key::cross)
            return;
        read_mods(game);
        mod_rows_.visible = kDialogRowsShown;
        mod_rows_.pitch = kDialogRowPitch;
        mod_rows_.reset(static_cast<int>(mod_list_.size()), 0);
        modal_ = modal_shown_ = Modal::mods;
        message_.clear();
        cue(Cue::open);
        return;
    }
    if (kind == row_delete)
    {
        // Deleting a game a download source has: its file and all of its updates and DLC go, its save
        // data and settings stay. Cross asks first, then deletes.
        if (key != Key::cross)
            return;
        if (!delete_armed_)
        {
            delete_armed_ = true;
            say(tr("Press again to delete the game and its updates and DLC from the console. Save data and "
                   "settings are kept."),
                true);
            cue(Cue::notify);
            return;
        }
        delete_armed_ = false;
        std::string result;
        if (!services_.delete_game(game, &result))
        {
            say(result.empty() ? std::string{tr("Could not delete the game.")} : result, true);
            cue(Cue::error);
            return;
        }
        // On its sources only now: so it shows until the list is read again (it keeps its place).
        game.remote = true;
        game.title_id = 0;
        game.addons.clear();
        game.addons_short.clear();
        game.mods = game.mods_on = 0;
        close_modal();
        read_home();
        refresh_selected_game();
        say(result);
        cue(Cue::saved);
        return;
    }
    if (kind == row_save)
    {
        // Save data: Square copies the game's save out, Cross (twice) copies one in.
        std::string result;
        if (key == Key::square)
        {
            import_armed_ = false;
            const bool exported = services_.save_export(game.title_id, &result);
            say(result, !exported);
            cue(exported ? Cue::saved : Cue::error);
        }
        else if (key != Key::cross)
        {
            cue(Cue::error);
        }
        else if (import_source_ != SaveSource::none && !import_armed_)
        {
            // Importing replaces the save in use, so it asks first.
            import_armed_ = true;
            say(tr("Press again to replace this game's save. The current one is backed up."), true);
            cue(Cue::notify);
        }
        else
        {
            // With nothing to import the answer says where a save has to be put.
            import_armed_ = false;
            const bool imported = services_.save_import(game.title_id, &result);
            say(result, !imported && import_source_ != SaveSource::none);
            cue(imported ? Cue::saved : import_source_ != SaveSource::none ? Cue::error : Cue::notify);
        }
        return;
    }
    // The console mode.
    const bool saved = services_.set_docked(game.title_id, !game_docked_);
    if (saved)
        game_docked_ = !game_docked_;
    say(saved ? tr("Saved for this game. Applies on next launch.") : tr("Could not save. Please try again."),
        !saved);
    cue(saved ? Cue::toggle : Cue::error);
}

void Launcher::draw_game(Canvas &c, float open)
{
    gfx::DrawList &list = c.list;
    list.push_opacity(open);
    list.push_transform(1.0f - 0.03f * (1.0f - open) * motion(), 960.0f, 540.0f, 0.0f,
                        (1.0f - open) * 26.0f * motion());
    glass(c, kDialog, 26.0f, theme::kPanel.with_alpha(0.97f), theme::kPanelEdge.with_alpha(0.66f),
          1.6f);
    text_shrink(c, tr("Game settings"), 592.0f, baseline(218.0f, 62.0f, theme::kDisplay),
                theme::kDisplay, theme::kTitle, 736.0f);
    const Game *game = games_.empty() ? nullptr : &games_[static_cast<std::size_t>(library_.selected)];
    text_fit(c, game != nullptr ? game->name : std::string{}, 592.0f,
             baseline(291.0f, 32.0f, theme::kSmall), theme::kSmall, Color::rgb(0xbecbb9), 736.0f);

    // A kind of setting: what the game does differently from Settings, if anything.
    const auto changed = [this](int category)
    {
        const int count = game_overrides(category);
        return count == 0 ? std::string{tr("Follows Settings")} :
                            fill(tr("{0} changed"), {std::to_string(count)});
    };
    const std::string values[] = {
        game_docked_ ? tr("Docked") : tr("Handheld"),
        changed(row_video - row_video),
        changed(row_performance - row_video),
        changed(row_audio - row_video),
        changed(row_controls - row_video),
        changed(row_language - row_video),
        // With the game's Mods switch off (the Library's), none of them is on.
        mods_.empty() ? std::string{tr("No mods")} :
        game != nullptr && !game->mods_enabled ? std::string{tr("Off")} :
            fill(tr("{0} of {1} on"),
                 {std::to_string(std::count_if(mods_.begin(), mods_.end(),
                                               [](const Mod &mod) { return mod.enabled; })),
                  std::to_string(mods_.size())}),
        import_source_ == SaveSource::ryujinx ? tr("Ryujinx save found") :
        import_source_ == SaveSource::folder ? tr("Save folder found") : tr("Nothing to import"),
        game != nullptr ? game->size : std::string{},
    };
    static constexpr const char *kLabels[] = {TR("Console mode"), TR("Video"),    TR("Performance"),
                                              TR("Audio"),        TR("Controls"), TR("Language"),
                                              TR("Mods"),         TR("Save data"), TR("Delete from console")};
    // Five rows show; the list scrolls to the others.
    list.push_clip({kDialogWindow.x - 24.0f, kDialogWindow.y - 6.0f, kDialogWindow.w + 48.0f,
                    kDialogWindow.h + 12.0f});
    const auto row_top = [&](int row)
    { return kDialogRowsTop + static_cast<float>(row) * kDialogRowPitch - game_rows_.scroll(); };
    for (int row = game_rows_.first_row(); row <= game_rows_.last_row(); ++row)
    {
        list.push_opacity(game_rows_.row_alpha(row, kDialogRowHeight));
        plate_rest(c, kRowPlate, {592.0f, row_top(row), 736.0f, kDialogRowHeight});
        list.pop_opacity();
    }
    plate_focus(c, kRowPlate,
                {592.0f, kDialogRowsTop + game_rows_.cursor() - game_rows_.scroll(), 736.0f,
                 kDialogRowHeight},
                1.0f);
    for (int row = game_rows_.first_row(); row <= game_rows_.last_row(); ++row)
    {
        const float top = row_top(row);
        const float focus = row == option_ ? 1.0f : 0.0f;
        const int kind = game_row(row);
        list.push_opacity(game_rows_.row_alpha(row, kDialogRowHeight));
        // The value first: the row's name takes what it leaves. The kinds of setting, Mods and
        // Save data say what is there; the console mode is a choice; deleting says the game's size.
        const bool there = kind == row_mods ? !mods_.empty() && (game == nullptr || game->mods_enabled) :
                           kind == row_save ? import_source_ != SaveSource::none :
                           kind == row_delete ? false :
                                              game_overrides(kind - row_video) > 0;
        const float taken =
            kind != row_mode ?
                text_shrink(c, values[kind], 1292.0f, baseline(top, kDialogRowHeight, theme::kSmall),
                            theme::kSmall, there ? theme::kLimePale : theme::kMeta, 320.0f,
                            Align::right) :
                chooser(c, values[kind], 1296.0f, baseline(top, kDialogRowHeight, theme::kText24),
                        focus, theme::kLimePale);
        text_shrink(c, tr(kLabels[kind]), 628.0f, baseline(top, kDialogRowHeight, theme::kText24),
                    theme::kText24, kind == row_delete ? theme::kWarning : theme::kValue, 664.0f - taken - 28.0f);
        list.pop_opacity();
    }
    list.pop_clip();
    scrollbar(c, game_rows_, 1340.0f, kDialogWindow.y, kDialogWindow.h);

    if (!message_.empty())
    {
        // Up to two lines: what Save data answers is longer than a "Saved".
        notice_block(c, message_, 592.0f, kDialogHints - 6.0f, theme::kSmall, 26.0f,
                     message_warning_ ? theme::kWarning : theme::kLimePale, 736.0f, 2,
                     message_warning_);
    }
    else if (game_row(option_) == row_delete)
    {
        static constexpr Hint kDelete[] = {{Pad::cross, TR("Delete")}, {Pad::circle, TR("Back")}};
        draw_hints(c, kDelete, 2, 592.0f, kDialogHints, theme::kCopy, 736.0f);
    }
    else if (game_row(option_) == row_save)
    {
        static constexpr Hint kTransfer[] = {{Pad::cross, TR("Import")},
                                             {Pad::square, TR("Export a copy")},
                                             {Pad::circle, TR("Back")}};
        draw_hints(c, kTransfer, 3, 592.0f, kDialogHints, theme::kCopy, 736.0f);
    }
    else if (option_ != row_mode)
    {
        static constexpr Hint kOpen[] = {{Pad::cross, TR("Open")}, {Pad::circle, TR("Back")}};
        draw_hints(c, kOpen, 2, 592.0f, kDialogHints, theme::kCopy, 736.0f);
    }
    else
    {
        static constexpr Hint kHints[] = {
            {Pad::updown, TR("Select")}, {Pad::leftright, TR("Change")}, {Pad::circle, TR("Back")}};
        draw_hints(c, kHints, 3, 592.0f, kDialogHints, theme::kCopy, 736.0f);
    }
    list.pop_transform();
    list.pop_opacity();
}

// ---------------------------------------------------------------- a game's mods

void Launcher::press_mods(Key key)
{
    Game &game = games_[static_cast<std::size_t>(library_.selected)];
    switch (key)
    {
    case Key::circle:
        // Back to the game's settings, on its Mods row.
        modal_ = modal_shown_ = Modal::game;
        message_.clear();
        cue(Cue::back);
        return;
    case Key::up:
    case Key::down:
        if (mod_rows_.move(key == Key::down ? 1 : -1))
        {
            message_.clear();
            cue(Cue::focus);
        }
        return;
    case Key::square:
    {
        // With no mods yet: the folder they go in, named after the game's ID, is made on request.
        if (!mods_.empty())
            return;
        const bool made = services_.make_mods_folder(game.title_id);
        say(made ? fill(tr("Created {0}. Copy each mod's folder into it."),
                        {services_.mods_folder(game.title_id)}) :
                   tr("Could not create the folder. Check that the game files folder can be written."),
            !made);
        cue(made ? Cue::saved : Cue::error);
        return;
    }
    case Key::cross:
    case Key::left:
    case Key::right:
    {
        if (mods_.empty())
            return;
        const ModRow at = mod_list_[static_cast<std::size_t>(mod_rows_.selected)];
        Mod &mod = mods_[static_cast<std::size_t>(at.mod)];
        // With the game's Mods switch off (the Library's), choosing a mod turns the switch on and
        // that mod with it: nobody switches a mod in a list that is switched off.
        const bool revive = !game.mods_enabled;
        if (at.cheat >= 0)
        {
            // One of the mod's cheats. Choosing it turns on what it runs behind: its mod, and the
            // game's Mods switch. One of a group (two frame rates) takes the other's place, so
            // the list is read again.
            const Cheat &cheat = mod.cheats[static_cast<std::size_t>(at.cheat)];
            const bool enabled = revive || !mod.enabled || !cheat.enabled;
            const bool saved =
                (!revive || services_.set_mods_enabled(game.title_id, true)) &&
                (mod.enabled || services_.set_mod_enabled(game.title_id, mod.name, true)) &&
                services_.set_cheat_enabled(game.title_id, mod.name, cheat.name, enabled);
            read_mods(game);
            if (mod_rows_.count != static_cast<int>(mod_list_.size()))
                mod_rows_.reset(static_cast<int>(mod_list_.size()), 0);
            say(saved ? tr("Saved for this game. Applies on next launch.") :
                        tr("Could not save. Please try again."),
                !saved);
            cue(saved ? Cue::toggle : Cue::error);
            return;
        }
        const bool enabled = revive || !mod.enabled;
        const bool saved = (!revive || services_.set_mods_enabled(game.title_id, true)) &&
                           services_.set_mod_enabled(game.title_id, mod.name, enabled);
        if (saved)
            mod.enabled = enabled;
        count_mods(game, mods_);
        say(saved ? tr("Saved for this game. Applies on next launch.") :
                    tr("Could not save. Please try again."),
            !saved);
        cue(saved ? Cue::toggle : Cue::error);
        return;
    }
    default:
        return;
    }
}

void Launcher::draw_mods(Canvas &c, float open)
{
    gfx::DrawList &list = c.list;
    list.push_opacity(open);
    list.push_transform(1.0f - 0.03f * (1.0f - open) * motion(), 960.0f, 540.0f, 0.0f,
                        (1.0f - open) * 26.0f * motion());
    glass(c, kDialog, 26.0f, theme::kPanel.with_alpha(0.97f), theme::kPanelEdge.with_alpha(0.66f),
          1.6f);
    text_shrink(c, tr("Mods"), 592.0f, baseline(218.0f, 62.0f, theme::kDisplay), theme::kDisplay,
                theme::kTitle, 736.0f);
    const Game *game = games_.empty() ? nullptr : &games_[static_cast<std::size_t>(library_.selected)];
    text_fit(c, game != nullptr ? game->name : std::string{}, 592.0f,
             baseline(291.0f, 32.0f, theme::kSmall), theme::kSmall, Color::rgb(0xbecbb9), 736.0f);

    if (mods_.empty())
    {
        text_block(c,
                   fill(tr("No mods for this game yet. Copy each mod's folder to {0}, next to roms/."),
                        {game != nullptr ? services_.mods_folder(game->title_id) : std::string{}}),
                   592.0f, baseline(364.0f, 40.0f, theme::kText24), theme::kText24, 40.0f,
                   theme::kBody, 736.0f, 5, kShrink);
    }
    else
    {
        list.push_clip({kDialogWindow.x - 24.0f, kDialogWindow.y - 6.0f, kDialogWindow.w + 48.0f,
                        kDialogWindow.h + 12.0f});
        const auto row_top = [&](int row)
        { return kDialogRowsTop + static_cast<float>(row) * kDialogRowPitch - mod_rows_.scroll(); };
        for (int row = mod_rows_.first_row(); row <= mod_rows_.last_row(); ++row)
        {
            list.push_opacity(mod_rows_.row_alpha(row, kDialogRowHeight));
            plate_rest(c, kRowPlate, {592.0f, row_top(row), 736.0f, kDialogRowHeight});
            list.pop_opacity();
        }
        plate_focus(c, kRowPlate,
                    {592.0f, kDialogRowsTop + mod_rows_.cursor() - mod_rows_.scroll(), 736.0f,
                     kDialogRowHeight},
                    1.0f);
        // With the game's Mods switch off (the Library's) no mod is used: their own switches keep
        // their state, drawn faint.
        const bool live = game == nullptr || game->mods_enabled;
        for (int row = mod_rows_.first_row(); row <= mod_rows_.last_row(); ++row)
        {
            const ModRow at = mod_list_[static_cast<std::size_t>(row)];
            const Mod &mod = mods_[static_cast<std::size_t>(at.mod)];
            const float top = row_top(row);
            list.push_opacity(mod_rows_.row_alpha(row, kDialogRowHeight));
            if (at.cheat >= 0)
            {
                // One of the mod's cheats, set in under it, with its own switch: faint while its
                // mod is off.
                const Cheat &cheat = mod.cheats[static_cast<std::size_t>(at.cheat)];
                const bool used = live && mod.enabled;
                text_fit(c, cheat.name, 668.0f,
                         baseline(top + (kDialogRowHeight - 38.0f) * 0.5f, 38.0f, theme::kText24),
                         theme::kText24, cheat.enabled && used ? theme::kValue : theme::kMeta, 520.0f);
                list.push_opacity(used ? 1.0f : 0.4f);
                toggle(c, 1292.0f, top + kDialogRowHeight * 0.5f, cheat.enabled ? 1.0f : 0.0f);
                list.pop_opacity();
                list.pop_opacity();
                continue;
            }
            // Its name as the player's folder has it, what it changes under it (with how many of
            // its cheats are chosen when it lists several), its switch.
            std::string kind = mod.kind;
            if (!mod.cheats.empty())
                kind += ", " + fill(tr("{0} of {1} on"),
                                    {std::to_string(std::count_if(
                                         mod.cheats.begin(), mod.cheats.end(),
                                         [](const Cheat &cheat) { return cheat.enabled; })),
                                     std::to_string(mod.cheats.size())});
            text_fit(c, mod.name, 628.0f, baseline(top + 14.0f, 38.0f, theme::kText24),
                     theme::kText24, mod.enabled && live ? theme::kValue : theme::kMeta, 560.0f);
            text_shrink(c, kind, 628.0f, baseline(top + 52.0f, 28.0f, theme::kSmall),
                        theme::kSmall, theme::kMeta, 560.0f);
            list.push_opacity(live ? 1.0f : 0.4f);
            toggle(c, 1292.0f, top + kDialogRowHeight * 0.5f, mod.enabled ? 1.0f : 0.0f);
            list.pop_opacity();
            list.pop_opacity();
        }
        list.pop_clip();
        scrollbar(c, mod_rows_, 1340.0f, kDialogWindow.y, kDialogWindow.h);
    }

    if (!message_.empty())
    {
        notice_block(c, message_, 592.0f, kDialogHints - 6.0f, theme::kSmall, 26.0f,
                     message_warning_ ? theme::kWarning : theme::kLimePale, 736.0f, 2,
                     message_warning_);
    }
    else if (mods_.empty())
    {
        static constexpr Hint kEmpty[] = {{Pad::square, TR("Create the folder")},
                                          {Pad::circle, TR("Back")}};
        draw_hints(c, kEmpty, 2, 592.0f, kDialogHints, theme::kCopy, 736.0f);
    }
    else
    {
        static constexpr Hint kHints[] = {{Pad::updown, TR("Select")},
                                          {Pad::cross, TR("Turn on or off")},
                                          {Pad::circle, TR("Back")}};
        draw_hints(c, kHints, 3, 592.0f, kDialogHints, theme::kCopy, 736.0f);
    }
    list.pop_transform();
    list.pop_opacity();
}

} // namespace pe::ui
