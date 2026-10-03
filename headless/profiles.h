// SPDX-License-Identifier: GPL-3.0-or-later
// Player profiles: who is playing, so two people keep their own save data in the same game.
//
// A profile is one of the emulated console's users. Eden keeps up to eight in profiles.dat and
// stores a game's account save data under the user's ID (nand/user/save/0000000000000000/<ID>/
// <title>), so separate saves need nothing more than choosing the user before a game starts. This
// header reads and writes that file in Eden's own layout (a 0x10-byte header, then eight entries of
// 0xC8 bytes: the ID twice, a time stamp, a 32-byte name and 0x80 bytes of other data, kept as they
// are), and keeps the choice in the settings file:
//
//   "profiles": {"current": "<ID>", "first": "<ID>", "users": {"<PS5 user>": "<ID>"}}
//   "profile_settings": {"<ID>": {...the settings of a profile that is not the first...}}
//
// "current" is the profile games start with. "users" remembers which profile each PS5 user chose
// last; that profile is the one the menu opens with for that user. "first" is the profile that
// existed before profiles could be chosen: everything saved until then is its own, the recently
// played games and the settings included, so nothing moves and nothing is lost. Every profile has
// its own settings (settings_store.h, Settings::Load): the ones under Settings and each game's own.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <vector>
#include "settings_store.h"
#include "storage_paths.h"

namespace Eden::Profiles {
inline constexpr std::size_t kMax = 8;
inline constexpr std::size_t kNameBytes = 32;
inline constexpr std::size_t kHeaderBytes = 0x10;
inline constexpr std::size_t kEntryBytes = 0xC8;
inline constexpr std::size_t kFileBytes = kHeaderBytes + kMax * kEntryBytes;

struct Profile {
    std::array<std::uint8_t, 16> id{};
    std::string name;
    // The time stamp and the data Eden keeps with a user (its icon among it), as read.
    std::array<std::uint8_t, kEntryBytes - 16 - 16 - kNameBytes> rest{};

    // The ID as Eden's save paths spell it: the upper 64 bits, then the lower.
    std::string Key() const {
        std::uint64_t low = 0, high = 0;
        std::memcpy(&low, id.data(), sizeof(low));
        std::memcpy(&high, id.data() + 8, sizeof(high));
        char text[33];
        std::snprintf(text, sizeof(text), "%016llX%016llX", static_cast<unsigned long long>(high),
                      static_cast<unsigned long long>(low));
        return text;
    }
    bool Valid() const {
        return std::any_of(id.begin(), id.end(), [](std::uint8_t byte) { return byte != 0; });
    }
};

inline std::string File() {
    return UserDir() + "/nand/system/save/8000000000000010/su/avators/profiles.dat";
}

// A name as the file holds it: at most 31 bytes, cut at a whole UTF-8 character.
inline std::string FitName(std::string_view name) {
    std::string fitted(name.substr(0, std::min(name.size(), kNameBytes - 1)));
    while (!fitted.empty() && name.size() > fitted.size() &&
           (static_cast<unsigned char>(name[fitted.size()]) & 0xC0) == 0x80)
        fitted.pop_back();
    return fitted;
}

// The profiles of a file, in Eden's order (the file's, without its empty entries). Empty when
// there is no file yet, or one that is not Eden's.
inline std::vector<Profile> Read(const std::string& file = File()) {
    std::vector<Profile> profiles;
    std::ifstream in(file, std::ios::binary);
    std::array<char, kFileBytes> data{};
    if (!in.read(data.data(), data.size())) return profiles;
    for (std::size_t index = 0; index < kMax; ++index) {
        const char* entry = data.data() + kHeaderBytes + index * kEntryBytes;
        Profile profile;
        std::memcpy(profile.id.data(), entry, 16);
        if (!profile.Valid()) continue;
        const char* name = entry + 32 + 8;
        profile.name.assign(name, strnlen(name, kNameBytes));
        // The time stamp sits before the name and the other data after it.
        std::memcpy(profile.rest.data(), entry + 32, 8);
        std::memcpy(profile.rest.data() + 8, entry + 32 + 8 + kNameBytes, profile.rest.size() - 8);
        profiles.push_back(std::move(profile));
    }
    return profiles;
}

// Writes the file whole, beside the old one first, so a failure leaves the old one.
inline bool Write(const std::vector<Profile>& profiles, const std::string& file = File()) {
    if (profiles.empty() || profiles.size() > kMax) return false;
    std::array<char, kFileBytes> data{};
    for (std::size_t index = 0; index < profiles.size(); ++index) {
        const Profile& profile = profiles[index];
        if (!profile.Valid() || profile.name.empty()) return false;
        char* entry = data.data() + kHeaderBytes + index * kEntryBytes;
        std::memcpy(entry, profile.id.data(), 16);
        std::memcpy(entry + 16, profile.id.data(), 16);
        std::memcpy(entry + 32, profile.rest.data(), 8);
        const std::string name = FitName(profile.name);
        std::memcpy(entry + 32 + 8, name.data(), name.size());
        std::memcpy(entry + 32 + 8 + kNameBytes, profile.rest.data() + 8, profile.rest.size() - 8);
    }
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(file).parent_path(), error);
    const std::string staged = file + ".new";
    {
        std::ofstream out(staged, std::ios::binary | std::ios::trunc);
        if (!out.write(data.data(), data.size()) || !out.flush()) return false;
    }
    std::filesystem::rename(staged, file, error);
    if (error) std::remove(staged.c_str());
    return !error;
}

// A profile with a new ID.
inline Profile Make(std::string_view name) {
    Profile profile;
    profile.name = FitName(name);
    std::random_device device;
    do {
        for (std::size_t at = 0; at < profile.id.size(); at += 4) {
            const std::uint32_t value = device();
            std::memcpy(profile.id.data() + at, &value, 4);
        }
    } while (!profile.Valid());
    return profile;
}

inline int IndexOf(const std::vector<Profile>& profiles, std::string_view key) {
    for (std::size_t index = 0; index < profiles.size(); ++index)
        if (profiles[index].Key() == key) return static_cast<int>(index);
    return -1;
}

// "Player 2": the first such name no profile has.
inline std::string FreeName(const std::vector<Profile>& profiles) {
    for (int number = 1;; ++number) {
        const std::string name = "Player " + std::to_string(number);
        if (std::none_of(profiles.begin(), profiles.end(), [&](const Profile& p) { return p.name == name; }))
            return name;
    }
}

// ---- the choice, in the settings file ----

inline std::string SettingsValue(const Settings::Json& document, const char* pointer) {
    return Settings::String(document, Settings::Json::json_pointer(pointer));
}

// What the menu and the games use: the profiles, and which one is chosen. It makes sure there is
// a file with one profile at least, and settles the choice: the one saved, else the one this PS5
// user chose last, else the first. `user` is the PS5 user in front (negative: unknown).
struct State {
    std::vector<Profile> profiles;
    int current = 0;
};
inline State Resolve(int user = -1, const std::string& file = File(),
                     const std::string& settings = SettingsFile()) {
    State state;
    state.profiles = Read(file);
    if (state.profiles.empty()) {
        // Before any game ran: the profile Eden would have made on the first one.
        state.profiles.push_back(Make("Player 1"));
        if (!Write(state.profiles, file)) return state;
    }
    Settings::Json document = Settings::LoadWhole(settings);
    const std::string saved = SettingsValue(document, "/profiles/current");
    const std::string first = SettingsValue(document, "/profiles/first");
    const std::string users = user >= 0 ? "/profiles/users/" + std::to_string(user) : std::string{};
    int chosen = -1;
    // A PS5 user who chose a profile before gets it back, whoever played last.
    if (!users.empty()) chosen = IndexOf(state.profiles, SettingsValue(document, users.c_str()));
    if (chosen < 0) chosen = IndexOf(state.profiles, saved);
    if (chosen < 0) chosen = 0;
    state.current = chosen;
    const std::string key = state.profiles[static_cast<std::size_t>(chosen)].Key();
    // The profile that was there before profiles could be chosen owns what was saved until then.
    const std::string owner = first.empty() ? state.profiles.front().Key() : first;
    if (saved != key || first != owner) {
        document["version"] = 1;
        document["profiles"]["current"] = key;
        document["profiles"]["first"] = owner;
        (void)Settings::WriteWhole(document, settings);
    }
    return state;
}

// Chooses a profile, and remembers it for the PS5 user in front.
inline bool Choose(const Profile& profile, int user = -1, const std::string& settings = SettingsFile()) {
    Settings::Json document = Settings::LoadWhole(settings);
    document["version"] = 1;
    document["profiles"]["current"] = profile.Key();
    if (SettingsValue(document, "/profiles/first").empty()) document["profiles"]["first"] = profile.Key();
    if (user >= 0) document["profiles"]["users"][std::to_string(user)] = profile.Key();
    return Settings::WriteWhole(document, settings);
}

// A new profile starts with the settings of the one that made it (its recently played games and
// its save data stay its own): nobody has to set the picture up again.
inline bool Seed(const Profile& profile, const std::string& settings = SettingsFile()) {
    Settings::Json from = Settings::Load(settings);
    for (const char* key : Settings::kSharedKeys) from.erase(key);
    from.erase("library");
    Settings::Json whole = Settings::LoadWhole(settings);
    whole["version"] = 1;
    whole["profile_settings"][profile.Key()] = std::move(from);
    return Settings::WriteWhole(whole, settings);
}

// Its settings go with a profile that is taken off the list (its save data stays).
inline bool Forget(const Profile& profile, const std::string& settings = SettingsFile()) {
    Settings::Json whole = Settings::LoadWhole(settings);
    const Settings::Json::json_pointer at("/profile_settings");
    if (!whole.contains(at) || !whole.at(at).contains(profile.Key())) return true;
    whole.at(at).erase(profile.Key());
    return Settings::WriteWhole(whole, settings);
}

// The chosen profile's place in Eden's list (Settings::values.current_user), for a game start.
inline int CurrentIndex(const std::string& file = File(), const std::string& settings = SettingsFile()) {
    const std::vector<Profile> profiles = Read(file);
    return std::max(0, IndexOf(profiles, SettingsValue(Settings::LoadWhole(settings), "/profiles/current")));
}
} // namespace Eden::Profiles
