// SPDX-License-Identifier: GPL-3.0-or-later
// Save sync: where each player profile (profiles.h) keeps its save data besides the console, in
// config/remote/save-sync.json. The app writes the file with an entry for every profile and keeps
// it in step with them (a profile added, renamed or removed); the player fills an entry in (over
// FTP, like sources.json) with a server and its sign-in:
//
//   { "version": 1,
//     "auto": true,                                        sync before a game starts and after it ends
//     "profiles": [
//       { "profile": "1F1E1D1C1B1A19181716151413121110",   the profile's ID, as its save folder has it
//         "__profile_name": "Player 1",                    for the reader only; the app writes it
//         "type": "romm",                                  the backend (remote/backends.h); "": none
//         "url": "http://192.168.1.20:3000",
//         "token": "rmm_..." },                            and what else the backend reads
//       ... ] }
//
// Fields that start with "__" are the app's, there to be read: what is written into them changes
// nothing and is written over. The profile's ID is what tells the entries apart: a name may
// change, the ID does not. An entry says nothing about the download sources: a profile may keep
// its save data on another server, or as another user of the same one, than the games come from.
//
// What the player wrote stays as it is, fields the app does not know and their order included:
// the app adds the entry of a new profile, takes out the entry of one that is gone (and a second
// entry of the same profile), writes the names and puts the entries in the profiles' order, and
// writes the file only when that changed something. A file that is not JSON is left alone: no
// profile syncs until it is readable again.
#pragma once
#include <filesystem>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
#include "settings_store.h"
#include "storage_paths.h"

namespace Eden::SaveSync {
using Json = nlohmann::ordered_json;

inline std::string File() { return ConfigFile("remote") + "/save-sync.json"; }

// A profile as the file names it: its ID (Profiles::Profile::Key) and its name.
struct Owner {
    std::string id;
    std::string name;
};

// A profile's entry as read: the backend's type ("" when it has none) and the whole entry, which
// the backend reads its settings from.
struct Entry {
    std::string profile;
    std::string profile_name;
    std::string type;
    Json settings;
};

struct Config {
    bool readable = false; // false: the file is there but no JSON of this kind (error says what)
    std::string error;
    bool automatic = true; // "auto"
    std::vector<Entry> entries;
};

namespace Detail {
inline std::string Text(const Json& object, const char* key) {
    const auto found = object.find(key);
    return found != object.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

// The file as JSON; null when there is none, a discarded value when it is no JSON object.
inline Json Load(const std::string& file, bool* exists) {
    std::string text;
    *exists = Settings::ReadFile(file, text);
    if (!*exists) return Json{};
    Json document = Json::parse(text, nullptr, false);
    return document.is_object() ? document : Json(Json::value_t::discarded);
}
} // namespace Detail

// The file as it is now.
inline Config Read(const std::string& file = File()) {
    Config config;
    bool exists = false;
    const Json document = Detail::Load(file, &exists);
    if (!exists) {
        config.readable = true;
        return config;
    }
    if (document.is_discarded()) {
        config.error = "save-sync.json is no JSON object";
        return config;
    }
    const auto profiles = document.find("profiles");
    if (profiles != document.end() && !profiles->is_array()) {
        config.error = "\"profiles\" in save-sync.json is no list";
        return config;
    }
    config.readable = true;
    if (const auto automatic = document.find("auto"); automatic != document.end() && automatic->is_boolean())
        config.automatic = automatic->get<bool>();
    if (profiles == document.end()) return config;
    for (const Json& item : *profiles) {
        if (!item.is_object()) continue;
        Entry entry;
        entry.profile = Detail::Text(item, "profile");
        if (entry.profile.empty()) continue;
        // The first entry of a profile is the one that counts.
        bool seen = false;
        for (const Entry& other : config.entries) seen = seen || other.profile == entry.profile;
        if (seen) continue;
        entry.profile_name = Detail::Text(item, "__profile_name");
        entry.type = Detail::Text(item, "type");
        entry.settings = item;
        config.entries.push_back(std::move(entry));
    }
    return config;
}

// The entry of a profile; nullptr when the file has none.
inline const Entry* Find(const Config& config, const std::string& profile) {
    for (const Entry& entry : config.entries)
        if (entry.profile == profile) return &entry;
    return nullptr;
}

// Brings the file in step with the profiles (in Eden's order); it is made when there is none.
// False when it could not be read or written; it is then as it was.
inline bool Reconcile(const std::vector<Owner>& owners, const std::string& file = File()) {
    bool exists = false;
    const Json before = Detail::Load(file, &exists);
    if (exists && before.is_discarded()) return false;
    Json document = exists ? before : Json::object();
    if (!document.contains("version")) document["version"] = 1;
    if (!document.contains("auto")) document["auto"] = true;
    const auto listed = document.find("profiles");
    if (listed != document.end() && !listed->is_array()) return false;
    const Json old = listed != document.end() ? *listed : Json::array();
    Json profiles = Json::array();
    for (const Owner& owner : owners) {
        Json entry;
        for (const Json& item : old) {
            if (item.is_object() && Detail::Text(item, "profile") == owner.id) {
                entry = item;
                break;
            }
        }
        if (entry.is_null()) entry = Json{{"profile", owner.id}, {"__profile_name", owner.name}, {"type", ""},
                                          {"url", ""}, {"token", ""}};
        entry["__profile_name"] = owner.name;
        profiles.push_back(std::move(entry));
    }
    document["profiles"] = std::move(profiles);
    if (exists && document == before) return true;
    std::error_code ignored;
    std::filesystem::create_directories(std::filesystem::path(file).parent_path(), ignored);
    return Settings::WriteFile(file, document.dump(2, ' ', false, Json::error_handler_t::replace) + "\n");
}

// Sets what a profile's entry says about its server (pairing, Settings > Save sync): `fields`
// ("type", "url", "token", "__server_user"...) replace its sign-in, and a sign-in typed in before
// ("token", "username", "password", and whose it was: "__server_user") goes; the profile's name
// and fields of the player's own stay. Empty fields unlink it (type, url and token empty). False
// when the file cannot be read or written, or has no entry of the profile.
inline bool SetEntry(const std::string& profile, const Json& fields, const std::string& file = File()) {
    bool exists = false;
    Json document = Detail::Load(file, &exists);
    if (!exists || document.is_discarded() || !document.contains("profiles") || !document["profiles"].is_array())
        return false;
    for (Json& entry : document["profiles"]) {
        if (!entry.is_object() || Detail::Text(entry, "profile") != profile) continue;
        for (const char* key : {"token", "username", "password", "__server_user"}) entry.erase(key);
        if (fields.empty()) {
            entry["type"] = "";
            entry["url"] = "";
            entry["token"] = "";
        }
        for (const auto& [key, value] : fields.items()) entry[key] = value;
        return Settings::WriteFile(file, document.dump(2, ' ', false, Json::error_handler_t::replace) + "\n");
    }
    return false;
}

} // namespace Eden::SaveSync
