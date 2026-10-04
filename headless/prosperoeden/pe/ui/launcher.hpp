// ProsperoEden - The launcher: home, library, settings and their dialogs.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "pe/audio/sounds.hpp"
#include "pe/ui/widgets.hpp"

#include <array>
#include <future>
#include <string>
#include <vector>

namespace pe::ui
{

// Every screen of the launcher as one state machine. The frontend feeds it
// pressed buttons and frame time, draws its list and plays its cues; it ends
// when a game is chosen (and the closing animation has run).
class Launcher
{
  public:
    // first_start: the app just opened (false after returning from a game).
    Launcher(Services &services, Textures &textures, const Fonts &fonts, bool first_start);
    ~Launcher();
    Launcher(const Launcher &) = delete;
    Launcher &operator=(const Launcher &) = delete;

    void press(Key key);
    void update(float dt);
    void draw(gfx::DrawList &list);

    // The sounds asked for since the last call.
    std::vector<audio::Cue> take_cues();
    // Launcher sound level (0-100) as set in Settings > Audio.
    int menu_volume() const
    {
        return prefs_.menu_volume;
    }
    // The size the picture is put out at, as set in Settings > Video (Preferences::output). The
    // frontend opens its display again when it changes, and then names its new font texture.
    int output() const
    {
        return prefs_.output;
    }
    void set_font_texture(std::uint32_t texture)
    {
        fonts_.texture = texture;
    }
    bool done() const
    {
        return done_;
    }
    // Empty until a game is chosen.
    const std::string &selected_game() const
    {
        return selected_game_;
    }

  private:
    enum class Screen : std::uint8_t
    {
        home,
        library,
        settings,
        files,
        language,
        about,
    };
    enum class Modal : std::uint8_t
    {
        none,
        video,
        performance,
        audio,
        controls,
        accessibility,
        diagnostics,
        game,
        mods,         // a game's mods, opened from its settings
        game_options, // one category of a game's own settings, opened from its settings
        mapping,      // the button mapping: Settings > Controls', or a game's own
        profiles,     // who is playing: Settings > Profiles
        update,       // a newer release of the app: install it or not (update.cpp)
    };
    // The update dialog's steps: the offer, installing, stopping it, closing for the helper to
    // finish, and a failure.
    enum class UpdateStage : std::uint8_t
    {
        offer,
        working,
        cancelling,
        closing,
        failed,
    };

    // ---- shell (launcher.cpp) ----
    void cue(audio::Cue value)
    {
        cues_.push_back(value);
    }
    void open(Screen screen, bool forward);
    void open_modal(Modal modal);
    void close_modal();
    void say(const std::string &text, bool warning = false);
    void launch(const std::string &file, const std::string &title, const std::string &cover);
    void draw_screen(Canvas &c, Screen screen);
    void draw_frame(Canvas &c, const char *title, const char *copy);
    void draw_footer(Canvas &c, const Hint *hints, int count);
    void draw_launch(Canvas &c);
    // The notification of a newer release the app cannot install itself, at the top right for ten
    // seconds.
    void draw_update_notice(Canvas &c);
    // quiet: a change that shows at once needs no "Saved" line.
    bool save_preferences(bool quiet = false);
    // The switches of a dialog as the preferences have them, in the order of its rows.
    std::array<bool, 7> switch_states(Modal modal) const;
    // Shows the launcher as the preferences' accessibility switches say.
    void apply_look();

    // ---- home (home.cpp) ----
    void press_home(Key key);
    void update_controllers(float dt);
    void draw_home(Canvas &c);
    void draw_controllers(Canvas &c);
    void open_library_at_last();

    // ---- library and game settings (library.cpp) ----
    // The game list is read beside the menu: reading every game takes a moment.
    void start_scan();
    void finish_scan(bool wait);
    void apply_games(std::vector<Game> games);
    void name_home_games();
    // The home screen's content, with its game's mods counted.
    void read_home();
    // A game's mods as its list has them: how many, and how many are switched on.
    void count_mods(Game &game, const std::vector<Mod> &mods);
    // Games whose file left the game files folder leave the home screen and the Library.
    void check_games_present();
    bool drop_missing_games();
    // Reads a game's mods for its dialogs, with the rows of the Mods list.
    void read_mods(Game &game);
    // What a game comes with, on one line: "Update 1.2.0, 2 DLC, 2 mods"; "None" without any.
    // brief: for Game::addons_short, where the line would not fit ("v1.2.0, 2 DLC, 1/2 mods").
    static std::string addons_line(const std::string &addons, int mods, int mods_on,
                                   bool brief = false);
    // A refresh rate setting as the player reads it: 0 is "60 Hz", 1 is "120 Hz".
    static std::string hertz(int refresh);
    void enter_library();
    void press_library(Key key);
    void draw_library(Canvas &c);
    void refresh_selected_game();
    void press_game(Key key);
    void draw_game(Canvas &c, float open);
    void press_mods(Key key);
    void draw_mods(Canvas &c, float open);

    // ---- a game's own settings by category, and the button mapping (game_options.cpp) ----
    // Categories, in the order of the game settings' rows: video, performance, audio, controls,
    // language.
    static constexpr int kGameOptionCategories = 5;
    void open_game_options(int category);
    void press_game_options(Key key);
    void draw_game_options(Canvas &c, float open);
    // How many of a category's settings the game has of its own.
    int game_overrides(int category) const;
    // ---- profiles (profiles.cpp) ----
    void read_profiles();
    int profile_row_count() const;
    void open_profiles();
    void press_profiles(Key key);
    void draw_profiles(Canvas &c, float open);
    void open_mapping(bool for_game);
    void press_mapping(Key key);
    void draw_mapping(Canvas &c, float open);

    // ---- settings and its dialogs (settings.cpp) ----
    void press_settings(Key key);
    void draw_settings(Canvas &c);
    void press_dialog(Key key);
    void draw_dialog(Canvas &c, Modal modal, float open);
    int dialog_rows(Modal modal) const;
    float dialog_row_top(Modal modal, int row) const;

    // ---- a newer release of the app (update.cpp) ----
    // Takes the update check's answer when it comes, and opens the dialog once the menu is free.
    void update_offer(float dt);
    // Follows the install: progress, its speed, the ready and failed steps.
    void update_install(float dt);
    void begin_update();
    void press_update(Key key);
    void draw_update(Canvas &c, float open);

    // ---- game files, language, about (browse.cpp) ----
    void enter_files();
    bool browse_to(const std::string &directory);
    void press_files(Key key);
    void draw_files(Canvas &c);
    void enter_language();
    void press_language(Key key);
    void draw_language(Canvas &c);
    void draw_about(Canvas &c);

    Services &services_;
    Textures &textures_;
    Fonts fonts_;
    Backdrop backdrop_;
    std::vector<audio::Cue> cues_;
    float time_ = 0.0f;
    float clock_wait_ = 0.0f;
    float presence_wait_ = 0.0f; // time since the shown games' files were last looked at
    std::string update_version_;       // the newer release being announced
    float update_notice_left_ = 0.0f;  // seconds its notification still shows
    tween::Spring update_notice_in_;   // 0 away .. 1 in place
    // The update dialog.
    UpdateOffer update_;
    bool update_waiting_ = false;      // an offer waits for the menu to be free
    UpdateStage update_stage_ = UpdateStage::offer;
    float update_stage_time_ = 0.0f;   // seconds in this step
    int update_choice_ = 0;            // 0: the first button, 1: the second
    tween::Spring update_choice_x_;    // the highlight between the two buttons
    tween::Spring update_height_;      // the panel's height: shorter while it works
    UpdateStatus update_status_;
    tween::Spring update_fraction_;    // the shown share done, 0..1
    float update_spin_ = 0.0f;         // the waiting arc's turn
    float update_rate_ = 0.0f;         // bytes per second, smoothed
    std::uint64_t update_rate_done_ = 0;
    float update_rate_wait_ = 0.0f;
    std::string clock_;
    std::string version_;

    // navigation
    Screen screen_ = Screen::home;
    Screen leaving_ = Screen::home;
    tween::Timer transition_;
    bool forward_ = true;
    Modal modal_ = Modal::none;
    Modal modal_shown_ = Modal::none; // still drawn while it closes
    tween::Spring modal_open_;
    tween::Spring dim_;   // darkness over the art behind full screens
    float press_ = 0.0f;  // 1 at a confirm, then decays: the focused item dips
    std::string message_; // the last result ("Saved..."), shown where the screen has room
    bool message_warning_ = false;
    float message_age_ = 0.0f;

    // launching a game
    std::string selected_game_;
    std::string launch_title_;
    std::string launch_cover_;
    tween::Timer launch_;
    bool done_ = false;

    // home: 0 continue, 1-3 header, 4 game details, 5-8 recent, 9 view all
    Home home_;
    // The controllers connected now (bit 0 is player 1), and how lit each one's icon is.
    unsigned controllers_ = 0;
    bool controllers_known_ = false;
    std::array<tween::Spring, 4> controller_lit_{};
    std::array<float, 4> controller_pop_{}; // 1 when a controller appears, then decays
    int home_focus_ = 0;
    std::array<tween::Spring, 10> home_springs_{};
    float intro_ = 0.0f;
    bool first_start_ = true;

    // library
    std::vector<Game> games_;
    std::future<std::vector<Game>> scan_; // the list being read
    bool games_loaded_ = false;
    ListView library_;
    bool selected_docked_ = true;
    tween::Spring mode_;   // 0 docked .. 1 handheld
    tween::Spring mods_switch_; // the selected game's Mods switch: 0 off .. 1 on
    tween::Spring detail_; // the details fade in after the selection moves

    // settings and dialogs
    Preferences prefs_;
    ListView settings_;
    tween::Spring section_;
    int option_ = 0;
    tween::Spring option_cursor_; // highlight position in pixels
    std::array<tween::Spring, 7> switches_{};
    ListView video_rows_; // the Video dialog's rows (more than it shows)
    ListView performance_rows_; // the Performance dialog's rows (more than it shows)
    GameSettings game_settings_;
    bool game_docked_ = true;
    SaveSource import_source_ = SaveSource::none; // what Save data could import for the game
    bool import_armed_ = false;                   // Cross was pressed once: the next one imports
    ListView game_rows_;                          // the game dialog's rows (more than it shows)
    std::vector<Mod> mods_;                       // the game's mods, read when its dialog opens
    // The Mods list's rows: each mod, then its cheats when it lists several.
    struct ModRow
    {
        int mod = 0;
        int cheat = -1; // -1: the mod itself
    };
    std::vector<ModRow> mod_list_;
    ListView mod_rows_;
    int game_options_ = 0;          // the category a game's own settings dialog shows
    ListView option_rows_;          // its rows
    bool mapping_for_game_ = false; // the mapping dialog edits the game's own mapping
    ListView mapping_rows_;
    std::vector<Profile> profiles_; // the people who play on this console
    std::string playing_;           // the one games start as
    ListView profile_rows_;
    int profile_remove_ = -1;       // the row Square was pressed on once (asked twice)

    // game files
    std::string browse_dir_;
    std::vector<std::string> browse_entries_; // ".." first unless at "/", then subfolders
    ListView files_;
    FolderInfo folder_info_;

    // language
    ListView language_;
};

} // namespace pe::ui
