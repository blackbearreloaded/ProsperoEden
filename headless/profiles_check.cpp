// SPDX-License-Identifier: GPL-3.0-or-later
// Host check of the player profiles (profiles.h): Eden's profile file read and written in its own
// layout, the choice kept in the settings file, a console that had one user before profiles could
// be chosen, and each profile's own recently played games.
#include "profiles.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>

int main() {
    using namespace Eden::Profiles;
    char folder[] = "/tmp/eden-profiles-XXXXXX";
    assert(mkdtemp(folder));
    const std::string file = std::string(folder) + "/nand/avators/profiles.dat";
    const std::string settings = std::string(folder) + "/prosperoeden.json";

    // A console that has run games already: the file Eden wrote, with one user called "Eden", its
    // icon data, and a hole before a second entry (Eden skips empty entries).
    std::array<char, kFileBytes> raw{};
    const auto entry = [&](std::size_t index) { return raw.data() + kHeaderBytes + index * kEntryBytes; };
    for (int i = 0; i < 16; ++i) entry(0)[i] = entry(0)[16 + i] = static_cast<char>(0x10 + i);
    std::memcpy(entry(0) + 32, "\x11\x22\x33\x44\x55\x66\x77\x88", 8);
    std::memcpy(entry(0) + 40, "Eden", 4);
    entry(0)[40 + kNameBytes + 4] = 0x5A; // somewhere in the user's other data
    for (int i = 0; i < 16; ++i) entry(2)[i] = entry(2)[16 + i] = static_cast<char>(0xA0 + i);
    std::memcpy(entry(2) + 40, "Second", 6);
    std::filesystem::create_directories(std::filesystem::path(file).parent_path());
    { std::ofstream(file, std::ios::binary).write(raw.data(), raw.size()); }

    std::vector<Profile> profiles = Read(file);
    assert(profiles.size() == 2 && profiles[0].name == "Eden" && profiles[1].name == "Second");
    // The ID as the save folders spell it: the upper half first.
    assert(profiles[0].Key() == "1F1E1D1C1B1A19181716151413121110");

    // Written back, the first entry is byte for byte what Eden wrote; the list is closed up.
    assert(Write(profiles, file));
    std::array<char, kFileBytes> again{};
    { std::ifstream(file, std::ios::binary).read(again.data(), again.size()); }
    assert(std::memcmp(again.data(), raw.data(), kHeaderBytes + kEntryBytes) == 0);
    assert(std::memcmp(again.data() + kHeaderBytes + kEntryBytes, entry(2), kEntryBytes) == 0);
    assert(Read(file).size() == 2);

    // Someone who used the version before profiles: a settings file as that version wrote it,
    // with everything it could hold (Settings, a game's own values, its mods and cheats, the
    // games played, the game files folder). The first start with profiles changes none of it:
    // the user that was there becomes the first profile, and every value reads as before.
    const char* const before = R"({
      "version": 1,
      "game_files": "/mnt/usb0/eden",
      "video": {"renderer": "opengl", "resolution": "2x", "upscaling_filter": "fsr", "fps_overlay": false},
      "audio": {"volume": 55, "mute": true, "menu_volume": 30},
      "controls": {"vibration": false, "mapping": {"a": "cross", "b": "circle"}},
      "performance": {"fast_gpu": true},
      "diagnostics": {"detailed_logging": true},
      "games": {"0100000000001000": {"console_mode": "handheld", "volume": 80, "mods_off": ["Big Head"],
                                     "cheats_on": ["Trainer#Infinite"], "mods": false}},
      "library": {"last_game": "First.nsp", "recent": ["First.nsp", "Second.nsp"]}
    })";
    { std::ofstream(settings) << before; }
    const uint64_t title = 0x0100000000001000ull;
    const Eden::Preferences old_preferences = Eden::LoadPreferences(settings);
    const Eden::GameSettings old_game = Eden::LoadGameSettings(title, settings);
    assert(old_preferences.volume == 55 && old_preferences.mute && !Eden::LoadGameDocked(title, settings));

    State state = Resolve(-1, file, settings);
    assert(state.profiles.size() == 2 && state.current == 0);
    assert(CurrentIndex(file, settings) == 0);
    // The file gained the profiles' own entry and nothing else moved or changed.
    Eden::Settings::Json after = Eden::Settings::LoadWhole(settings);
    assert(after["profiles"]["current"] == profiles[0].Key() && after["profiles"]["first"] == profiles[0].Key());
    after.erase("profiles");
    assert(after == Eden::Settings::Json::parse(before));
    // And every reader gives what it gave before.
    const Eden::Preferences new_preferences = Eden::LoadPreferences(settings);
    assert(new_preferences.volume == 55 && new_preferences.mute && new_preferences.menu_volume == old_preferences.menu_volume);
    assert(new_preferences.backend == Eden::GraphicsBackend::OpenGL && !new_preferences.vibration);
    assert(new_preferences.resolution == old_preferences.resolution &&
           new_preferences.upscaling_filter == old_preferences.upscaling_filter && !new_preferences.hud);
    assert(new_preferences.mapping == old_preferences.mapping && new_preferences.mapping != Eden::kDefaultMapping);
    assert(new_preferences.detailed_logging && new_preferences.detailed_logging == old_preferences.detailed_logging);
    assert(!Eden::LoadGameDocked(title, settings) && Eden::LoadGameSettings(title, settings).volume == old_game.volume);
    assert(Eden::LoadDisabledMods(title, settings) == std::vector<std::string>{"Big Head"});
    assert(Eden::LoadChosenCheats(title, settings) == std::vector<std::string>{"Trainer#Infinite"});
    assert(!Eden::LoadModsEnabled(title, settings) && Eden::LoadPerformance(title, settings).fast_gpu);
    assert(Eden::LoadLastGame(settings) == "First.nsp");
    assert((Eden::LoadRecentGames(settings) == std::vector<std::string>{"First.nsp", "Second.nsp"}));
    assert(Eden::LoadSavedAssetsDir(settings) == "/mnt/usb0/eden");
    // Its save data is where it was too: the first profile's ID is the user's that Eden had.
    assert(state.profiles[0].Key() == "1F1E1D1C1B1A19181716151413121110" && state.profiles[0].name == "Eden");
    // A second start changes nothing more.
    const std::string settled = Eden::Settings::LoadWhole(settings).dump();
    (void)Resolve(-1, file, settings);
    assert(Eden::Settings::LoadWhole(settings).dump() == settled);
    // The rest of this check plays with one game on the list.
    assert(Eden::SaveRecentGame("First.nsp", settings));
    {
        Eden::Settings::Json trimmed = Eden::Settings::LoadWhole(settings);
        trimmed["library"]["recent"] = {"First.nsp"};
        assert(Eden::Settings::WriteWhole(trimmed, settings));
    }

    // A new profile: its own ID, its own recently played games, and nothing of the first one's.
    profiles.push_back(Make(FreeName(profiles)));
    assert(profiles[2].name == "Player 1" && profiles[2].Valid() && profiles[2].Key() != profiles[0].Key());
    assert(Write(profiles, file));
    assert(Choose(profiles[2], 7, settings));
    assert(CurrentIndex(file, settings) == 2);
    assert(Eden::LoadLastGame(settings).empty() && Eden::LoadRecentGames(settings).empty());
    assert(Eden::SaveLastGame("Other.nsp", settings) && Eden::SaveRecentGame("Other.nsp", settings));
    assert(Eden::LoadLastGame(settings) == "Other.nsp");
    // Back to the first: what it played is as it was.
    assert(Choose(profiles[0], 3, settings));
    assert(Eden::LoadLastGame(settings) == "First.nsp");
    assert(Eden::LoadRecentGames(settings) == std::vector<std::string>{"First.nsp"});

    // Settings are each profile's own: the ones under Settings and each game's. A new profile
    // starts with those of the one that made it, and what one changes the other never sees.
    Eden::Preferences picture = Eden::LoadPreferences(settings);
    picture.volume = 40;
    assert(Eden::SavePreferences(picture, settings));               // the first profile's
    assert(Eden::SaveGameDocked(0x0100000000001000ull, false, settings));
    assert(Seed(profiles[2], settings) && Choose(profiles[2], 7, settings));  // made while the first plays
    assert(Eden::LoadPreferences(settings).volume == 40);           // started from the first one's
    assert(!Eden::LoadGameDocked(0x0100000000001000ull, settings));
    assert(Eden::LoadLastGame(settings).empty());                   // but not its games played
    picture.volume = 85;
    assert(Eden::SavePreferences(picture, settings));
    assert(Eden::SaveGameDocked(0x0100000000001000ull, true, settings));
    assert(Eden::SaveLastGame("Other.nsp", settings));
    assert(Eden::SaveAssetsDir("/mnt/usb0/games", settings));       // the console's, for everyone
    assert(Choose(profiles[0], 3, settings));
    assert(Eden::LoadPreferences(settings).volume == 40);
    assert(!Eden::LoadGameDocked(0x0100000000001000ull, settings));
    assert(Eden::LoadLastGame(settings) == "First.nsp");
    assert(Eden::LoadSavedAssetsDir(settings) == "/mnt/usb0/games");
    // The first profile's writes leave the other one's part alone.
    assert(Eden::SavePreferences(Eden::LoadPreferences(settings), settings));
    assert(Choose(profiles[2], 7, settings));
    assert(Eden::LoadPreferences(settings).volume == 85 && Eden::LoadGameDocked(0x0100000000001000ull, settings));
    assert(Choose(profiles[0], 3, settings));

    // The menu opens with the profile the PS5 user in front chose last, whoever played last.
    assert(Resolve(7, file, settings).current == 2);
    assert(Eden::LoadLastGame(settings) == "Other.nsp");
    assert(Resolve(3, file, settings).current == 0);
    assert(Resolve(99, file, settings).current == 0); // a user who never chose: the one chosen last

    // A profile taken off the list takes its settings with it.
    assert(Forget(profiles[2], settings));
    assert(!Eden::Settings::LoadWhole(settings)["profile_settings"].contains(profiles[2].Key()));
    // A profile taken off the list: the choice falls back to the first, the rest keep their place.
    profiles.erase(profiles.begin());
    assert(Write(profiles, file));
    state = Resolve(3, file, settings);
    assert(state.profiles.size() == 2 && state.current == 0 && state.profiles[0].name == "Second");

    // Names: cut at a whole character, never longer than the file's 31 bytes.
    assert(FitName("Marina").size() == 6);
    const std::string cut = FitName(std::string(29, 'a') + "\xC3\xA9\xC3\xA9");
    assert(cut.size() == 31 && cut.back() == '\xA9');
    assert(FitName(std::string(30, 'a') + "\xC3\xA9").size() == 30);
    // Nine profiles, an empty name and an empty list are refused, and the file stays.
    std::vector<Profile> many(9, Make("x"));
    assert(!Write(many, file) && !Write({}, file) && Read(file).size() == 2);

    // No file at all (no game has run yet): one profile is made, and it is the first.
    const std::string fresh = std::string(folder) + "/fresh/profiles.dat";
    const std::string fresh_settings = std::string(folder) + "/fresh.json";
    state = Resolve(-1, fresh, fresh_settings);
    assert(state.profiles.size() == 1 && state.profiles[0].name == "Player 1" && Read(fresh).size() == 1);

    std::filesystem::remove_all(folder);
    std::puts("Profiles: Eden's file read and written as it is, the first profile keeps what was there, "
              "each profile its own settings and recent games, the choice per PS5 user, removal and names PASS");
    return 0;
}
