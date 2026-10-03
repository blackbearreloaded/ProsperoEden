// SPDX-License-Identifier: GPL-3.0-or-later
#include "eden_services.h"

#include "assets_dir.h"
#include "crash_report.h"
#include "diagnostics.h"
#include "metadata_bridge.h"
#include "mods.h"
#include "profiles.h"
#include "update_notice.h"
#include "native_directory.h"
#include "pe/core/strings.hpp"
#include "radio_input.h"
#include "version.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <exception>
#include <fcntl.h>
#include <filesystem>
#include <initializer_list>
#include <sys/stat.h>
#include <unistd.h>

namespace {

using pe::fill;
using pe::tr;

bool IsFile(const std::string& path) {
    struct stat info {};
    return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

// A game's update and DLC: "Update 1.2.0, 2 DLC"; brief leaves the word out ("v1.2.0, 2 DLC") for
// places with little room.
std::string AddOnSummary(uint64_t title_id, bool brief = false) {
    char update[64]{};
    unsigned dlc = 0;
    eden_game_addons(title_id, update, sizeof(update), &dlc);
    std::string text;
    if (update[0]) text = brief ? (update[0] == 'v' ? std::string{update} : "v" + std::string{update}) :
                                  fill(tr("Update {0}"), {update});
    if (dlc) text += (text.empty() ? "" : ", ") + fill(tr("{0} DLC"), {std::to_string(dlc)});
    return text;
}

// The language a game will use for the chosen one (Settings > Language), and a note when the game
// does not offer the choice and falls back to another language.
struct GameLanguage {
    std::string label;
    std::string note;
};
GameLanguage LanguageFor(const std::string& path, uint64_t title_id, int choice) {
    const int chosen = Eden::kLanguageSettings[choice];
    const int used = eden_game_language(path.c_str(), Eden::AssetsPath("keys").c_str(), title_id, chosen);
    GameLanguage result{tr(Eden::kLanguageLabels[choice]), {}};
    if (used == chosen) return result;
    result.label = tr("Another language");
    for (std::size_t i = 0; i < std::size(Eden::kLanguageSettings); ++i)
        if (Eden::kLanguageSettings[i] == used) result.label = tr(Eden::kLanguageLabels[i]);
    result.note = fill(tr("{0} not available"), {tr(Eden::kLanguageLabels[choice])});
    return result;
}

// Why a game did not start, as headless/main.cpp reports it. The reasons that are whole sentences
// are shown in the player's language; the others carry codes and file names and stay as they are.
// (tools/launcher/strings.py checks that main.cpp still says these.)
constexpr const char* kLaunchErrors[] = {
    TR("Selected ROM is no longer available"),
    TR("PS5 controller initialization failed"),
    TR("Graphics backend initialization failed. Try another backend in Settings; see stderr.log and eden_log.txt for "
       "driver details."),
    TR("The game ran out of graphics memory. Lower the resolution in Settings, Video (or in the game's own settings) "
       "and start it again."),
};
std::string LaunchError(const std::string& reason) {
    std::string text = reason;
    for (const char* known : kLaunchErrors)
        if (reason == known) text = tr(known);
    // "Details:" follows it: a reason without its own full stop gets one.
    if (!text.empty() && text.back() != '.' && text.back() != '!' && text.back() != '?') text += '.';
    return text;
}

// Names of the subfolders (folders = true) or regular files in path, sorted without regard
// to case. Unlike ReadNativeDirectory, an odd entry is skipped rather than failing the
// folder: the Game files browser walks the whole console filesystem.
std::vector<std::string> ListEntries(const std::string& path, bool folders, bool& ok) {
    ok = false;
    std::vector<std::string> names;
    const int fd = open(path.c_str(), O_RDONLY | O_DIRECTORY);
    if (fd < 0) return names;
    std::vector<char> buffer(65536);
    for (;;) {
        const int count = sceKernelGetdents(fd, buffer.data(), static_cast<int>(buffer.size()));
        if (count == 0) { ok = true; break; }
        if (count < 0 || count > static_cast<int>(buffer.size())) break;
        for (std::size_t offset = 0; offset + offsetof(dirent, d_name) < static_cast<std::size_t>(count);) {
            uint16_t length;
            uint8_t type;
            std::memcpy(&length, buffer.data() + offset + offsetof(dirent, d_reclen), sizeof(length));
            std::memcpy(&type, buffer.data() + offset + offsetof(dirent, d_type), sizeof(type));
            if (length <= offsetof(dirent, d_name) || offset + length > static_cast<std::size_t>(count)) break;
            const char* name = buffer.data() + offset + offsetof(dirent, d_name);
            const std::string entry(name, strnlen(name, length - offsetof(dirent, d_name)));
            offset += length;
            if (entry.empty() || entry == "." || entry == "..") continue;
            bool is_folder = type == DT_DIR, is_file = type == DT_REG;
            if (type == DT_UNKNOWN || type == DT_LNK) {
                struct stat info {};
                const std::string full = path == "/" ? "/" + entry : path + "/" + entry;
                if (stat(full.c_str(), &info) != 0) continue;
                is_folder = S_ISDIR(info.st_mode);
                is_file = S_ISREG(info.st_mode);
            }
            if (folders ? is_folder : is_file) names.push_back(entry);
        }
    }
    close(fd);
    const auto lower = [](std::string text) {
        for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return text;
    };
    std::sort(names.begin(), names.end(), [&](const std::string& a, const std::string& b) {
        return lower(a) < lower(b);
    });
    return names;
}

std::string JoinPath(const std::string& directory, const std::string& name) {
    return directory == "/" ? "/" + name : directory + "/" + name;
}

// Files in directory with one of the (lower-case) extensions; -1 when it cannot be read.
int CountFiles(const std::string& directory, std::initializer_list<const char*> extensions) {
    bool ok = false;
    int count = 0;
    for (const auto& name : ListEntries(directory, false, ok)) {
        std::string lower = name;
        for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (const char* extension : extensions)
            if (lower.size() > std::strlen(extension) && lower.ends_with(extension)) { ++count; break; }
    }
    return ok ? count : -1;
}

unsigned int HashPath(const std::string& path) {
    unsigned int hash = 2166136261u;
    for (const unsigned char byte : path) hash = (hash ^ byte) * 16777619u;
    return hash;
}

// A title from the file's name: without its extension and the tags dumps carry
// ("Name [0100...][v0]", "Name (USA)").
std::string CleanTitle(const std::string& filename) {
    std::string title = std::filesystem::path(filename).stem().string();
    if (title.rfind("[Game] ", 0) == 0) title.erase(0, 7);
    if (const auto tag = title.find_first_of("[("); tag != std::string::npos && tag > 0) title.erase(tag);
    while (!title.empty() && (title.back() == ' ' || title.back() == '_' || title.back() == '-')) title.pop_back();
    return title.empty() ? std::filesystem::path(filename).stem().string() : title;
}

// Covers are keyed by the ROM's file name, not its full path, so they survive a new game
// files folder.
std::string CoverPath(const std::string& filename) {
    char name[16]{};
    std::snprintf(name, sizeof(name), "/%08x.tga", HashPath(filename));
    return Eden::CoversDir() + name;
}

// The game's own name is kept beside its cover once the ROM has been read, so the home screen
// can name its games without opening them.
std::string NamePath(const std::string& filename) {
    char name[16]{};
    std::snprintf(name, sizeof(name), "/%08x.name", HashPath(filename));
    return Eden::CoversDir() + name;
}

std::string SavedTitle(const std::string& filename) {
    char text[513]{};
    if (std::FILE* file = std::fopen(NamePath(filename).c_str(), "rb")) {
        const std::size_t size = std::fread(text, 1, sizeof(text) - 1, file);
        std::fclose(file);
        text[size] = '\0';
    }
    return text;
}

void SaveTitle(const std::string& filename, const std::string& title) {
    if (title.empty() || SavedTitle(filename) == title) return;
    const std::string path = NamePath(filename);
    const std::string staged = path + ".new";
    std::FILE* file = std::fopen(staged.c_str(), "wb");
    if (!file) return;
    const bool written = std::fwrite(title.data(), 1, title.size(), file) == title.size();
    if (std::fclose(file) != 0 || !written || std::rename(staged.c_str(), path.c_str()) != 0)
        (void)std::remove(staged.c_str());
}

std::string GameTitle(const std::string& filename) {
    const std::string saved = SavedTitle(filename);
    return saved.empty() ? CleanTitle(filename) : saved;
}

// The cached cover of a ROM, extracted from it when missing; empty when it has none.
std::string EnsureCover(const std::string& filename, std::string* title = nullptr) {
    const std::string cover = CoverPath(filename);
    if (Eden::FileExists(cover) && !title) return cover;
    const std::string rom = Eden::AssetsPath("roms/" + filename);
    if (!Eden::FileExists(rom)) return {};
    (void)mkdir(Eden::CoversDir().c_str(), 0777);
    char extracted[513]{};
    const int metadata = eden_extract_game_metadata(rom.c_str(), Eden::AssetsPath("keys").c_str(), cover.c_str(),
                                                    extracted, sizeof(extracted));
    if (metadata & EDEN_METADATA_TITLE) {
        SaveTitle(filename, extracted);
        if (title) *title = extracted;
    }
    if (metadata & EDEN_METADATA_COVER) return cover;
    Eden::Report("cover", ("No cover extracted from " + filename).c_str());
    return Eden::FileExists(cover) ? cover : std::string{};
}

int CountInstalledGames() {
    std::error_code error;
    const auto entries = Eden::ReadNativeDirectory(Eden::AssetsPath("roms"), error);
    if (error) return 0;
    int count = 0;
    for (const auto& entry : entries) {
        const std::string filename = entry.path().filename().string();
        if (Eden::ValidRomFilename(filename) && IsFile(Eden::AssetsPath("roms/" + filename))) ++count;
    }
    return count;
}

// The labels of a setting, in the player's language (tools/launcher/strings.py lists them).
template <std::size_t N>
std::vector<std::string> Labels(const char* const (&values)[N]) {
    std::vector<std::string> labels;
    for (const char* value : values) labels.emplace_back(tr(value));
    return labels;
}

// What is missing from the setup, as metadata_bridge.cpp words it, in the player's language. Each
// message is one of these texts, with a folder or a code where it says {0}.
std::string SetupMessage(const std::string& english) {
    static constexpr const char* kMessages[] = {
        TR("Missing or empty keys/prod.keys in {0}."),
        TR("prod.keys could not supply an NCA header key. Replace it with a valid key dump."),
        TR("Cannot read firmware/ in {0}. Install extracted firmware NCAs."),
        TR("A firmware NCA cannot be read. Reinstall the firmware dump."),
        TR("Firmware NCA validation failed (code {0}). Check that firmware and prod.keys are compatible."),
        TR("No firmware NCAs found in {0}."),
        TR("Firmware SystemVersion data is missing or unreadable. Install a complete firmware dump."),
        TR("Setup validation failed. Check that firmware and key files are readable and valid."),
    };
    for (const std::string_view pattern : kMessages) {
        const std::size_t hole = pattern.find("{0}");
        if (hole == std::string_view::npos) {
            if (english == pattern) return tr(english);
            continue;
        }
        const std::string_view before = pattern.substr(0, hole);
        const std::string_view after = pattern.substr(hole + 3);
        if (english.size() >= before.size() + after.size() && english.starts_with(before) &&
            english.ends_with(after))
            return fill(tr(std::string(pattern)),
                        {std::string_view{english}.substr(before.size(),
                                                           english.size() - before.size() - after.size())});
    }
    return english;
}

} // namespace

namespace {
// The PS5's users: who is in front, who is signed in, and their names.
struct LoginUsers {
    int id[4];
};
extern "C" int sceUserServiceInitialize(const void* parameters);
extern "C" int sceUserServiceGetForegroundUser(int* user);
extern "C" int sceUserServiceGetLoginUserIdList(LoginUsers* list);
extern "C" int sceUserServiceGetUserName(int user, char* name, std::size_t size);

// The names a profile can take: the signed-in PS5 users', then "Player 1" to "Player 8".
std::vector<std::string> ProfileNames() {
    std::vector<std::string> names;
    LoginUsers users{{-1, -1, -1, -1}};
    if (sceUserServiceGetLoginUserIdList(&users) >= 0) {
        for (const int user : users.id) {
            char name[17]{};
            if (user < 0 || sceUserServiceGetUserName(user, name, sizeof(name)) < 0 || name[0] == '\0') continue;
            if (std::find(names.begin(), names.end(), name) == names.end()) names.emplace_back(name);
        }
    }
    for (int number = 1; number <= static_cast<int>(Eden::Profiles::kMax); ++number) {
        const std::string name = "Player " + std::to_string(number);
        if (std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
    }
    return names;
}
} // namespace

EdenServices::EdenServices(std::string launch_error) : launch_error_(std::move(launch_error)) {
    (void)mkdir(Eden::ConfigDir().c_str(), 0777);
    // Who is playing (profiles.h): the profile this PS5 user chose last, else the one chosen last.
    (void)sceUserServiceInitialize(nullptr); // already done by the controller code: refused, harmless
    if (sceUserServiceGetForegroundUser(&user_) < 0) user_ = -1;
    const auto who = Eden::Profiles::Resolve(user_);
    if (!who.profiles.empty())
        Eden::Report("profile", (who.profiles[static_cast<std::size_t>(who.current)].name + " (" +
                                 std::to_string(who.current + 1) + " of " + std::to_string(who.profiles.size()) +
                                 ")").c_str());
    setup_ = eden_startup_error();
    Eden::Report("setup", setup_.empty() ? "Keys and firmware startup checks passed" : setup_.c_str());
}

pe::ui::Home EdenServices::home() {
    const std::lock_guard lock(bridge_);
    pe::ui::Home home;
    home.setup_ready = setup_.empty();
    if (!home.setup_ready) {
        home.status = fill(tr("Setup required: {0} Open Settings, Game files to choose the folder that holds your "
                              "keys, firmware and roms folders (or add the files to {1}), then reopen ProsperoEden."),
                           {SetupMessage(setup_), Eden::AssetsDir()});
    } else if (launch_error_.starts_with(Eden::Crash::kNotice)) {
        // The previous run ended with a crash report (headless/crash_report.h).
        home.status = fill(tr("ProsperoEden stopped because of an error. A report was saved to {0}."),
                           {launch_error_.substr(Eden::Crash::kNotice.size())});
        home.launch_failed = true;
    } else if (!launch_error_.empty()) {
        home.status = fill(tr("Game could not start: {0} Details: {1}"),
                           {LaunchError(launch_error_), Eden::LogFile("stderr.log")});
        home.launch_failed = true;
    }

    home.last_file = Eden::LoadLastGame();
    // A game no longer in the game files folder is not offered: the most recent one that is
    // takes its place (or none).
    if (!home.last_file.empty() && !IsFile(Eden::AssetsPath("roms/" + home.last_file))) {
        home.last_file.clear();
        for (const auto& name : Eden::LoadRecentGames()) {
            if (IsFile(Eden::AssetsPath("roms/" + name))) {
                home.last_file = name;
                break;
            }
        }
    }
    const std::string last_path = Eden::AssetsPath("roms/" + home.last_file);
    home.last_exists = !home.last_file.empty() && IsFile(last_path);
    if (!home.last_file.empty()) {
        std::string title = GameTitle(home.last_file);
        std::string cover = CoverPath(home.last_file);
        bool has_cover = Eden::FileExists(cover);
        if (home.last_exists && home.setup_ready && !has_cover) {
            cover = EnsureCover(home.last_file, &title);
            has_cover = !cover.empty();
        }
        home.last_title = title;
        home.last_caption = home.last_exists ? tr("Last game opened") :
                                               tr("ROM missing from the game files folder");
        home.last_caption_warning = !home.last_exists;
        if (has_cover) home.last_cover = cover;
    }
    // The last game's update and DLC and the language it will use; when it does not offer the
    // chosen one, the caption says so.
    if (home.setup_ready && home.last_exists) {
        eden_scan_addons(Eden::AssetsPath("updates").c_str(), Eden::AssetsPath("keys").c_str());
        const uint64_t title_id = eden_game_title_id(last_path.c_str());
        const GameLanguage language = LanguageFor(last_path, title_id, Eden::PreferencesFor(title_id).language);
        home.last_title_id = title_id;
        home.last_addons = AddOnSummary(title_id);
        home.last_language = language.label;
        if (!language.note.empty()) {
            home.last_caption = language.note;
            home.last_caption_warning = true;
        }
    }

    auto history = Eden::LoadRecentGames();
    if (history.empty() && home.last_exists) {
        if (!Eden::SaveRecentGame(home.last_file))
            Eden::Report("history", "Could not seed recent games from last played game");
        history.push_back(home.last_file);
    }
    for (const auto& name : history) {
        if (!IsFile(Eden::AssetsPath("roms/" + name))) continue;
        home.recents.push_back({name, GameTitle(name), EnsureCover(name)});
    }
    const int installed = CountInstalledGames();
    home.system_status = fill(installed == 1 ? tr("{0} game installed") : tr("{0} games installed"),
                              {std::to_string(installed)}) +
        "  /  " + (home.setup_ready ? tr("Firmware ready") : tr("Setup required"));
    return home;
}

std::string EdenServices::clock() {
    const std::time_t now = std::time(nullptr);
    char label[32]{};
    if (const std::tm* local = std::localtime(&now))
        (void)std::strftime(label, sizeof(label), "%H:%M", local);
    return label;
}

unsigned EdenServices::controllers() { return radio_input_controllers(); }

std::string EdenServices::version() {
    // "01.000.040" reads as v1.000.040.
    const char* text = Eden::kAppVersion;
    while (text[0] == '0' && text[1] != '.' && text[1] != '\0') ++text;
    return std::string("v") + text;
}

std::vector<pe::ui::Game> EdenServices::games() {
    // The launcher reads the list beside its menu (pe/ui/library.cpp), so the metadata reader
    // is used by one thread at a time.
    const std::lock_guard lock(bridge_);
    std::vector<pe::ui::Game> games;
    (void)mkdir(Eden::ConfigDir().c_str(), 0777);
    (void)mkdir(Eden::CoversDir().c_str(), 0777);
    std::error_code directory_error;
    const auto entries = Eden::ReadNativeDirectory(Eden::AssetsPath("roms"), directory_error);
    if (directory_error) return games;
    eden_scan_addons(Eden::AssetsPath("updates").c_str(), Eden::AssetsPath("keys").c_str());
    for (const auto& entry : entries) {
        const std::string file = entry.path().filename().string();
        const std::size_t dot = file.find_last_of('.');
        if (file == "." || file == ".." || dot == std::string::npos) continue;
        std::string format = file.substr(dot + 1);
        std::transform(format.begin(), format.end(), format.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        if (format != "NSP" && format != "XCI") continue;
        const std::string path = Eden::AssetsPath("roms/" + file);
        struct stat info {};
        if (stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) continue;
        char size[32];
        const double bytes = static_cast<double>(info.st_size);
        if (bytes >= 1073741824.0) std::snprintf(size, sizeof(size), "%.1f GB", bytes / 1073741824.0);
        else std::snprintf(size, sizeof(size), "%.1f MB", bytes / 1048576.0);
        pe::ui::Game game;
        game.name = CleanTitle(file);
        game.format = format;
        game.size = size;
        game.file = file;
        // A game whose data cannot be read is still listed, by its file name.
        try {
            char title[513]{};
            // The cover is replaced in one step: the menu may be loading the old one right now.
            const std::string cover = CoverPath(file);
            const std::string staged = cover + ".new";
            const int metadata = eden_extract_game_metadata(path.c_str(), Eden::AssetsPath("keys").c_str(),
                                                            staged.c_str(), title, sizeof(title));
            if (metadata & EDEN_METADATA_TITLE) {
                game.name = title;
                SaveTitle(file, game.name);
            }
            if ((metadata & EDEN_METADATA_COVER) && std::rename(staged.c_str(), cover.c_str()) == 0)
                game.cover = cover;
            else
                (void)std::remove(staged.c_str());
            game.title_id = eden_game_title_id(path.c_str());
            // The game's own language (Library > Game settings > Language) or Settings > Language.
            const GameLanguage language = LanguageFor(path, game.title_id, Eden::PreferencesFor(game.title_id).language);
            game.addons = AddOnSummary(game.title_id);
            game.addons_short = AddOnSummary(game.title_id, true);
            game.language = language.label;
            game.language_note = language.note;
        } catch (const std::exception& error) {
            Eden::Report("library", (file + ": " + error.what()).c_str());
        }
        games.push_back(std::move(game));
    }
    std::sort(games.begin(), games.end(),
              [](const pe::ui::Game& a, const pe::ui::Game& b) { return a.name < b.name; });
    return games;
}

std::string EdenServices::game_path(const std::string& file) { return Eden::AssetsPath("roms/" + file); }

bool EdenServices::take_update(std::string* version) { return Eden::UpdateNotice::Take(version); }

std::vector<pe::ui::Profile> EdenServices::profiles() {
    const auto who = Eden::Profiles::Resolve(user_);
    std::vector<pe::ui::Profile> list;
    for (std::size_t index = 0; index < who.profiles.size(); ++index)
        list.push_back({who.profiles[index].name, static_cast<int>(index) == who.current});
    return list;
}

bool EdenServices::choose_profile(int index) {
    const auto who = Eden::Profiles::Resolve(user_);
    if (index < 0 || index >= static_cast<int>(who.profiles.size())) return false;
    const auto& profile = who.profiles[static_cast<std::size_t>(index)];
    if (!Eden::Profiles::Choose(profile, user_)) return false;
    Eden::Report("profile", ("Now playing: " + profile.name).c_str());
    return true;
}

int EdenServices::add_profile() {
    auto list = Eden::Profiles::Read();
    if (list.empty() || list.size() >= Eden::Profiles::kMax) return -1;
    // A signed-in PS5 user's name when no profile has it yet, else the next "Player" name.
    std::string name = Eden::Profiles::FreeName(list);
    for (const std::string& candidate : ProfileNames()) {
        if (std::none_of(list.begin(), list.end(), [&](const auto& p) { return p.name == candidate; })) {
            name = candidate;
            break;
        }
    }
    list.push_back(Eden::Profiles::Make(name));
    if (!Eden::Profiles::Write(list)) return -1;
    // It starts with the settings of whoever made it; from then on they are its own.
    (void)Eden::Profiles::Seed(list.back());
    Eden::Report("profile", ("Added: " + name).c_str());
    return static_cast<int>(list.size()) - 1;
}

bool EdenServices::rename_profile(int index, int step) {
    auto list = Eden::Profiles::Read();
    if (index < 0 || index >= static_cast<int>(list.size())) return false;
    auto& profile = list[static_cast<std::size_t>(index)];
    // The names nobody else has; this profile's own name keeps its place among them.
    std::vector<std::string> names;
    for (const std::string& name : ProfileNames()) {
        const bool taken = std::any_of(list.begin(), list.end(), [&](const auto& other) {
            return &other != &profile && other.name == name;
        });
        if (!taken) names.push_back(name);
    }
    if (names.empty()) return false;
    const auto at = std::find(names.begin(), names.end(), profile.name);
    const int count = static_cast<int>(names.size());
    const int from = at == names.end() ? (step > 0 ? -1 : 0) : static_cast<int>(at - names.begin());
    profile.name = names[static_cast<std::size_t>(((from + step) % count + count) % count)];
    return Eden::Profiles::Write(list);
}

bool EdenServices::remove_profile(int index) {
    const auto who = Eden::Profiles::Resolve(user_);
    auto list = who.profiles;
    if (list.size() < 2 || index < 0 || index >= static_cast<int>(list.size()) || index == who.current) return false;
    const std::string name = list[static_cast<std::size_t>(index)].name;
    const auto gone = list[static_cast<std::size_t>(index)];
    list.erase(list.begin() + index);
    if (!Eden::Profiles::Write(list)) return false;
    (void)Eden::Profiles::Forget(gone);
    // Its save data stays where it is (nand/user/save/.../<ID>): removing a name destroys nothing.
    Eden::Report("profile", ("Removed from the list: " + name + " (its save data stays on the console)").c_str());
    return true;
}

bool EdenServices::game_exists(const std::string& file) {
    return Eden::ValidRomFilename(file) && IsFile(Eden::AssetsPath("roms/" + file));
}

bool EdenServices::docked(std::uint64_t title_id) { return Eden::LoadGameDocked(title_id); }

bool EdenServices::set_docked(std::uint64_t title_id, bool docked) {
    return Eden::SaveGameDocked(title_id, docked);
}

static_assert(pe::ui::kGameButtons == Eden::kGameButtons && pe::ui::kPadButtons == Eden::kPadButtons &&
              pe::ui::kDefaultMapping == Eden::kDefaultMapping, "the launcher's button mapping differs");
static_assert(std::tuple_size_v<decltype(pe::ui::GameSettings::performance)> == Eden::kPerformanceSwitches);

pe::ui::GameSettings EdenServices::game_settings(std::uint64_t title_id) {
    const Eden::GameSettings saved = Eden::LoadGameSettings(title_id);
    pe::ui::GameSettings result;
    result.renderer = saved.renderer;
    result.resolution = saved.resolution;
    result.filter = saved.upscaling_filter;
    result.refresh = saved.refresh;
    result.hud = saved.hud;
    result.volume = saved.volume;
    result.mute = saved.mute;
    result.vibration = saved.vibration;
    result.language = saved.language;
    result.own_mapping = saved.own_mapping;
    result.mapping = saved.mapping;
    result.performance = saved.performance;
    return result;
}

bool EdenServices::set_game_settings(std::uint64_t title_id, const pe::ui::GameSettings& settings) {
    Eden::GameSettings value;
    value.renderer = settings.renderer;
    value.resolution = settings.resolution;
    value.upscaling_filter = settings.filter;
    value.refresh = settings.refresh;
    value.hud = settings.hud;
    value.volume = settings.volume;
    value.mute = settings.mute;
    value.vibration = settings.vibration;
    value.language = settings.language;
    value.own_mapping = settings.own_mapping;
    value.mapping = settings.mapping;
    value.performance = settings.performance;
    const bool saved = Eden::SaveGameSettings(title_id, value);
    if (!saved) Eden::Report("settings", "Could not write game settings");
    return saved;
}

pe::ui::Preferences EdenServices::preferences() {
    const Eden::Preferences saved = Eden::LoadPreferences();
    pe::ui::Preferences result;
    result.hud = saved.hud;
    result.volume = saved.volume;
    result.mute = saved.mute;
    result.detailed_logging = saved.detailed_logging;
    result.renderer = saved.backend == Eden::GraphicsBackend::OpenGL ? 0 : 1;
    result.resolution = saved.resolution;
    result.filter = saved.upscaling_filter;
    result.refresh = saved.refresh;
    result.output = saved.output;
    result.vibration = saved.vibration;
    result.language = saved.language;
    result.menu_volume = saved.menu_volume;
    result.large_text = saved.large_text;
    result.high_contrast = saved.high_contrast;
    result.reduce_motion = saved.reduce_motion;
    // Settings > Performance: the general values (settings_store.h).
    const Eden::PerformanceSettings speed = Eden::LoadPerformance(0);
    result.block_list = speed.block_list;
    result.async_shaders = speed.async_shaders;
    result.fast_gpu = speed.fast_gpu;
    result.unsafe_cpu = speed.unsafe_cpu;
    result.unsafe_dma = speed.unsafe_dma;
    result.reactive_flushing = speed.reactive_flushing;
    result.skip_invalidation = speed.skip_invalidation;
    result.mapping = saved.mapping;
    return result;
}

bool EdenServices::set_preferences(const pe::ui::Preferences& preferences) {
    Eden::Preferences value;
    value.hud = preferences.hud;
    value.volume = preferences.volume;
    value.mute = preferences.mute;
    value.detailed_logging = preferences.detailed_logging;
    value.backend = preferences.renderer == 0 ? Eden::GraphicsBackend::OpenGL : Eden::GraphicsBackend::Vulkan;
    value.resolution = preferences.resolution;
    value.upscaling_filter = preferences.filter;
    value.refresh = preferences.refresh;
    value.output = preferences.output;
    value.vibration = preferences.vibration;
    value.language = preferences.language;
    value.menu_volume = preferences.menu_volume;
    value.large_text = preferences.large_text;
    value.high_contrast = preferences.high_contrast;
    value.reduce_motion = preferences.reduce_motion;
    value.mapping = preferences.mapping;
    Eden::PerformanceSettings speed = Eden::LoadPerformance(0);
    speed.block_list = preferences.block_list;
    speed.async_shaders = preferences.async_shaders;
    speed.fast_gpu = preferences.fast_gpu;
    speed.unsafe_cpu = preferences.unsafe_cpu;
    speed.unsafe_dma = preferences.unsafe_dma;
    speed.reactive_flushing = preferences.reactive_flushing;
    speed.skip_invalidation = preferences.skip_invalidation;
    const bool saved = Eden::SavePreferences(value) && Eden::SavePerformance(0, speed);
    if (!saved) Eden::Report("settings", "Could not write preferences");
    return saved;
}

const std::vector<std::string>& EdenServices::resolution_labels() {
    static const std::vector<std::string> labels = Labels(Eden::kResolutionLabels);
    return labels;
}

const std::vector<std::string>& EdenServices::resolution_keys() {
    static const std::vector<std::string> labels = Labels(Eden::kResolutionKeys);
    return labels;
}

const std::vector<std::string>& EdenServices::filter_labels() {
    static const std::vector<std::string> labels = Labels(Eden::kUpscalingFilterLabels);
    return labels;
}

const std::vector<std::string>& EdenServices::language_labels() {
    static const std::vector<std::string> labels = Labels(Eden::kLanguageLabels);
    return labels;
}

std::string EdenServices::language_region(int language) {
    static constexpr const char* kRegions[] = {TR("Japan"), TR("USA"), TR("Europe"), TR("Australia"), TR("China"),
                                               TR("Korea"), TR("Taiwan")};
    if (language < 0 || language >= int(std::size(Eden::kLanguageRegions))) return {};
    return tr(kRegions[Eden::kLanguageRegions[language]]);
}

std::string EdenServices::setup_details() {
    return setup_.empty() ?
        tr("Keys and firmware: startup checks passed. Game-specific compatibility is checked at launch.") :
        SetupMessage(setup_);
}

bool EdenServices::folders(const std::string& directory, std::vector<std::string>* names) {
    bool ok = false;
    *names = ListEntries(directory, true, ok);
    return ok;
}

pe::ui::FolderInfo EdenServices::folder_info(const std::string& directory) {
    pe::ui::FolderInfo info;
    info.keys = Eden::FileExists(JoinPath(directory, "keys/prod.keys"));
    info.firmware = CountFiles(JoinPath(directory, "firmware"), {".nca"});
    info.games = CountFiles(JoinPath(directory, "roms"), {".nsp", ".xci"});
    return info;
}

std::string EdenServices::files_folder() { return Eden::AssetsDir(); }
std::string EdenServices::saved_files_folder() { return Eden::LoadSavedAssetsDir(); }
std::string EdenServices::default_files_folder() { return Eden::kDefaultAssetsDir; }

bool EdenServices::set_files_folder(const std::string& directory) {
    const bool saved = Eden::SaveAssetsDir(directory);
    if (!saved) Eden::Report("settings", "Could not write the game files folder");
    return saved;
}

int EdenServices::filesystem_access() { return Eden::FilesystemAccessStatus(); }

#ifdef EDEN_SAVE_IMPORT
// Save transfer (ryujinx_saves.h): a save comes in from save-import/<title ID>/ or a Ryujinx data
// folder in ryujinx/, and goes out to save-export/, all next to roms/.
bool EdenServices::save_transfer_available() { return true; }

pe::ui::SaveSource EdenServices::save_import_source(std::uint64_t title_id) {
    switch (eden_save_import_source(title_id)) {
    case EDEN_SAVE_FOLDER: return pe::ui::SaveSource::folder;
    case EDEN_SAVE_RYUJINX: return pe::ui::SaveSource::ryujinx;
    default: return pe::ui::SaveSource::none;
    }
}

bool EdenServices::save_import(std::uint64_t title_id, std::string* message) {
    char backup[256]{};
    switch (eden_save_import(title_id, backup, sizeof(backup))) {
    case EDEN_SAVE_DONE:
        *message = backup[0] ? tr("Imported. The save it replaced was backed up.") : tr("Imported.");
        return true;
    case EDEN_SAVE_NOTHING: {
        char title[17]{};
        std::snprintf(title, sizeof(title), "%016llX", static_cast<unsigned long long>(title_id));
        *message = fill(tr("To import, copy a Ryujinx folder to ryujinx/ or a save to save-import/{0}/, next to roms/."),
                        {title});
        return false;
    }
    case EDEN_SAVE_NO_USER:
        *message = tr("Start any game once before importing a save.");
        return false;
    default:
        *message = tr("Import failed. The current save is unchanged.");
        return false;
    }
}

bool EdenServices::save_export(std::uint64_t title_id, std::string* message) {
    char folder[256]{};
    switch (eden_save_export(title_id, folder, sizeof(folder))) {
    case EDEN_SAVE_DONE: {
        // The end of the path tells it apart: save-export/<title ID>-<date>-<time>.
        const std::string path = folder;
        const std::size_t name = path.rfind("save-export/");
        *message = fill(tr("Exported to {0}."), {name == std::string::npos ? path : path.substr(name)});
        return true;
    }
    case EDEN_SAVE_NOTHING:
        *message = tr("This game has no save to export yet.");
        return false;
    default:
        *message = tr("Export failed. Check that the game files folder can be written.");
        return false;
    }
}
#else
bool EdenServices::save_transfer_available() { return false; }
pe::ui::SaveSource EdenServices::save_import_source(std::uint64_t) { return pe::ui::SaveSource::none; }
bool EdenServices::save_import(std::uint64_t, std::string*) { return false; }
bool EdenServices::save_export(std::uint64_t, std::string*) { return false; }
#endif

// Mods (headless/mods.h): what the game files folder's mods/<title ID>/ holds for a game, which
// of them are switched off and which cheats are chosen (settings_store.h).
std::vector<pe::ui::Mod> EdenServices::mods(std::uint64_t title_id) {
    std::vector<pe::ui::Mod> result;
    const auto off = Eden::LoadDisabledMods(title_id);
    const auto chosen = Eden::LoadChosenCheats(title_id);
    for (const Eden::Mods::Mod& mod : Eden::Mods::List(Eden::AssetsPath("mods"), title_id)) {
        std::string kind;
        const auto add = [&kind](const char* text) {
            if (!kind.empty()) kind += ", ";
            kind += tr(text);
        };
        if (mod.kinds & Eden::Mods::kCode) add(TR("Patch"));
        if (mod.kinds & Eden::Mods::kFiles) add(TR("Files"));
        if (mod.kinds & Eden::Mods::kCheats) add(TR("Cheats"));
        result.push_back({mod.name, kind, std::find(off.begin(), off.end(), mod.name) == off.end(), {}});
        for (const auto& cheat : mod.cheats)
            result.back().cheats.push_back(
                {cheat.name, std::find(chosen.begin(), chosen.end(), cheat.id) != chosen.end()});
    }
    return result;
}

bool EdenServices::set_cheat_enabled(std::uint64_t title_id, const std::string& name, const std::string& cheat,
                                     bool enabled) {
    bool saved = false;
    for (const Eden::Mods::Mod& mod : Eden::Mods::List(Eden::AssetsPath("mods"), title_id))
        if (mod.name == name)
            saved = Eden::SaveChosenCheats(
                title_id, Eden::Mods::ChooseCheat(mod, cheat, enabled, Eden::LoadChosenCheats(title_id)));
    if (!saved) Eden::Report("settings", "Could not write the game's cheats");
    return saved;
}

bool EdenServices::set_mod_enabled(std::uint64_t title_id, const std::string& name, bool enabled) {
    const bool saved = Eden::SaveModEnabled(title_id, name, enabled);
    if (!saved) Eden::Report("settings", "Could not write the game's mods");
    return saved;
}

bool EdenServices::mods_enabled(std::uint64_t title_id) { return Eden::LoadModsEnabled(title_id); }

bool EdenServices::set_mods_enabled(std::uint64_t title_id, bool enabled) {
    const bool saved = Eden::SaveModsEnabled(title_id, enabled);
    if (!saved) Eden::Report("settings", "Could not write the game's mods switch");
    return saved;
}

std::string EdenServices::mods_folder(std::uint64_t title_id) {
    return "mods/" + Eden::Mods::TitleName(title_id) + "/";
}

bool EdenServices::make_mods_folder(std::uint64_t title_id) {
    const std::string root = Eden::AssetsPath("mods");
    if (!Eden::Mods::TitleFolder(root, title_id).empty()) return true;
    (void)mkdir(root.c_str(), 0777);
    return mkdir(Eden::Mods::TitleFolderToCreate(root, title_id).c_str(), 0777) == 0;
}

bool EdenServices::load_image(const std::string& path, pe::gfx::Image* image) {
    // Covers have full paths; the launcher's own art is named from its ui folder.
    return pe::gfx::load_tga(!path.empty() && path[0] == '/' ? path : Eden::AppFile("ui/" + path), image);
}
