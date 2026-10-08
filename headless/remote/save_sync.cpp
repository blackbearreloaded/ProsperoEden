// SPDX-License-Identifier: GPL-3.0-or-later
// A game's save data kept in step with a profile's save store; see save_sync.h.
#include "save_sync.h"

#include "remote/backends.h"
#include "remote/remote.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

namespace Eden::Remote {
namespace {

namespace fs = std::filesystem;

// The backups of a game's replaced save data that are kept.
constexpr std::size_t kBackups = 3;

SyncResult Result(SyncOutcome outcome, std::string message = {}, const std::string& user = {}) {
    SyncResult result;
    result.outcome = outcome;
    result.message = std::move(message);
    result.user = user;
    return result;
}

SyncResult Failed(std::string message, const std::string& user = {}) {
    return Result(SyncOutcome::failed, std::move(message), user);
}

void Remove(const std::string& path) {
    std::error_code ignored;
    fs::remove_all(path, ignored);
}

// What each game's save data was when the console last had it in step with the store: the
// content hash of the console's and of the store's copy (the same save data, but packed another
// way, as JKSV's zips are, has another hash). It is the store's, as the server and user it is
// (another one, paired since, knows nothing of it). <store>/synced.json:
//   {"server": "<address> <user>",
//    "games": {"<title ID>": {"console": "<hash>", "store": "<hash>", "game": "<the store's id of it>"}}}
// The store's game it was in step with counts too: the same title as another game of the store
// (another file of it, found first once the store's list changed) knows nothing of it either.
struct InStep {
    std::string console;
    std::string store;
    std::string game;
};

nlohmann::json SyncedFile(const std::string& folder, const std::string& server) {
    std::ifstream in(folder + "/synced.json");
    nlohmann::json synced = nlohmann::json::parse(in, nullptr, false);
    if (!synced.is_object() || !synced.contains("server") || synced["server"] != server || !synced.contains("games") ||
        !synced["games"].is_object())
        synced = {{"server", server}, {"games", nlohmann::json::object()}};
    return synced;
}

InStep Synced(const std::string& folder, const std::string& server, const std::string& title) {
    const nlohmann::json synced = SyncedFile(folder, server);
    const auto found = synced["games"].find(title);
    if (found == synced["games"].end() || !found->is_object()) return {};
    const auto text = [&](const char* key) {
        const auto value = found->find(key);
        return value != found->end() && value->is_string() ? value->get<std::string>() : std::string{};
    };
    return {text("console"), text("store"), text("game")};
}

// A file written whole or not at all: to a file beside it, then put in its place.
bool WriteWhole(const std::string& file, const std::string& text) {
    const std::string staged = file + ".tmp";
    {
        std::ofstream out(staged, std::ios::binary | std::ios::trunc);
        out << text;
        out.close();
        if (!out) {
            std::remove(staged.c_str());
            return false;
        }
    }
    if (std::rename(staged.c_str(), file.c_str()) == 0) return true;
    std::remove(staged.c_str());
    return false;
}

void KeepSynced(const std::string& folder, const std::string& server, const std::string& title, const InStep& now) {
    nlohmann::json synced = SyncedFile(folder, server);
    const nlohmann::json entry = {{"console", now.console}, {"store", now.store}, {"game", now.game}};
    if (synced["games"].contains(title) && synced["games"][title] == entry) return;
    synced["games"][title] = entry;
    (void)WriteWhole(folder + "/synced.json", synced.dump(2) + "\n");
}

// The store's copy, unpacked, in place of the console's; the console's goes to the backups first
// and comes back when the new one cannot be put in place.
bool Replace(const std::string& zip, const SyncPlaces& places, const std::string& title, std::string* error) {
    const std::string incoming = places.save + ".incoming";
    Remove(incoming);
    if (!SaveArchive::Unpack(zip, incoming, title, error)) {
        Remove(incoming);
        return false;
    }
    std::error_code status;
    const bool had = fs::exists(places.save, status);
    std::string backup;
    if (had) {
        char stamp[32];
        const std::time_t now = std::time(nullptr);
        std::tm utc{};
        gmtime_r(&now, &utc);
        std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &utc);
        const std::string folder = places.backups + "/" + title;
        // A backup of the same second keeps its name: this one gets another.
        backup = folder + "/" + stamp;
        for (int n = 2; fs::exists(backup, status); ++n) backup = folder + "/" + stamp + "-" + std::to_string(n);
        std::error_code made;
        fs::create_directories(folder, made);
        if (made || std::rename(places.save.c_str(), backup.c_str()) != 0) {
            Remove(incoming);
            *error = "Cannot move the console's save data to " + backup;
            return false;
        }
        // The newest few stay, by when they were made (not by their names: the console's clock may
        // have been set back since). This one is the newest whatever its name.
        fs::last_write_time(backup, fs::file_time_type::clock::now(), status);
        std::vector<std::string> names;
        if (places.lister(folder, &names)) {
            std::vector<std::pair<fs::file_time_type, std::string>> kept;
            for (const std::string& name : names)
                if (folder + "/" + name != backup) kept.emplace_back(fs::last_write_time(folder + "/" + name, status), name);
            std::sort(kept.begin(), kept.end());
            for (std::size_t index = 0; index + kBackups < kept.size() + 1; ++index)
                Remove(folder + "/" + kept[index].second);
        }
    } else {
        std::error_code made;
        fs::create_directories(fs::path(places.save).parent_path(), made);
    }
    if (std::rename(incoming.c_str(), places.save.c_str()) != 0) {
        if (had) std::rename(backup.c_str(), places.save.c_str());
        Remove(incoming);
        *error = "Cannot put the save data in place";
        return false;
    }
    return true;
}

} // namespace

std::string TitleFolder(std::uint64_t title_id) {
    char text[17];
    std::snprintf(text, sizeof(text), "%016llX", static_cast<unsigned long long>(title_id));
    return text;
}

SyncResult SyncSaveData(const std::string& type, const nlohmann::json& settings, const SyncGame& game,
                        const SyncPlaces& places, const SyncChooser& choose, const Stopped& stopped) {
    if (game.title_id == 0) return Failed("The game's title ID is not known");
    std::string error;
    std::unique_ptr<SaveStore> store = MakeSaveStore(type, settings, places.store, &error);
    if (!store) return Failed(error);
    if (!store->prepare(stopped, &error)) {
        SyncResult result = Failed(error);
        result.too_old = store->too_old(&result.version, &result.needed);
        return result;
    }
    const std::string user = store->user();

    // The game on the store: as a download source's game is found on the console.
    std::vector<SourceGame> games;
    if (!store->games(&games, stopped, &error)) return Failed(error, user);
    const std::string normal = NormalName(game.name);
    const SourceGame* found = nullptr;
    for (const SourceGame& candidate : games) {
        Game made;
        if (AsGame({}, candidate, &made) && SameAsLocal(made, game.title_id, normal, game.file)) {
            found = &candidate;
            break;
        }
    }
    if (found == nullptr) return Result(SyncOutcome::no_game, {}, user);

    // The console's save data, packed.
    const std::string title = TitleFolder(game.title_id);
    SaveArchive::Folder folder;
    if (!SaveArchive::List(places.save, title, places.lister, &folder, &error)) return Failed(error, user);
    std::error_code made;
    fs::create_directories(places.work, made);
    const std::string packed = places.work + "/" + title + ".zip";
    const std::string fetched = places.work + "/" + title + "-store.zip";
    Remove(packed);
    Remove(fetched);
    LocalSave local;
    local.file_name = title + ".zip";
    if (!folder.files.empty()) {
        if (!SaveArchive::Hash(folder, &local.hash, &error)) return Failed(error, user);
        local.present = true;
        local.updated = folder.newest;
        local.size = folder.bytes;
    }
    const std::string server = store->address() + " " + user;

    SavePlan plan;
    if (!store->compare(*found, local, &plan, &error)) {
        store->finish(false);
        return Failed(error, user);
    }
    // What changed since this console's last sync, by the save data itself: the console's (its
    // hash is not the one it last had in step) and the store's (its copy's hash is not that one).
    // A store may only compare times when it no longer knows what this console had (RomM: when
    // another device's version came in since, or the console was registered again).
    InStep synced = Synced(places.store, server, title);
    if (!synced.game.empty() && synced.game != found->id) synced = {};
    bool overwrite = false;
    if (local.present && !plan.remote_hash.empty() && plan.action != SaveAction::none) {
        const bool console_changed = local.hash != synced.console;
        const bool store_changed = plan.remote_hash != synced.store;
        if (local.hash == plan.remote_hash || (!console_changed && !store_changed)) {
            plan.action = SaveAction::none;
        } else if (console_changed && !store_changed) {
            plan.action = SaveAction::upload; // over the copy this console had: nothing of another's lost
            overwrite = true;
        } else if (!console_changed && store_changed) {
            plan.action = SaveAction::download;
        } else {
            plan.action = SaveAction::conflict;
        }
    } else if (plan.action == SaveAction::download && local.present && local.hash != synced.console) {
        plan.action = SaveAction::conflict; // the store's copy not known: changed here, it is not replaced
    } else if (plan.action == SaveAction::upload && plan.remote.empty()) {
        overwrite = true; // the store has none of the game's: nothing to lose (another emulator's stays)
    }
    // A conflict: the player chooses; nothing is overwritten without that.
    SaveAction action = plan.action;
    bool kept = false;
    const auto resolve = [&] {
        const SyncChoice choice = choose ? choose(plan, local) : SyncChoice::neither;
        kept = choice == SyncChoice::neither;
        action = kept ? SaveAction::none : choice == SyncChoice::console ? SaveAction::upload : SaveAction::download;
        overwrite = choice == SyncChoice::console;
    };
    if (action == SaveAction::conflict) resolve();
    // A plan to send what the console does not have is no plan.
    if (action == SaveAction::upload && !local.present) action = SaveAction::none;

    SyncResult result = Result(kept ? SyncOutcome::kept : SyncOutcome::same, {}, user);
    bool done = true;
    if (action == SaveAction::upload) {
        // Packed only to go up: a compare needs its hash only.
        bool newer = false;
        done = SaveArchive::Pack(folder, packed, &error) &&
               store->upload(*found, local, packed, overwrite, &newer, &error);
        result.outcome = SyncOutcome::uploaded;
        // The store found a newer copy only now: a conflict after all.
        if (!done && newer && !overwrite) {
            plan.action = SaveAction::conflict;
            resolve();
            done = true;
            result.outcome = kept ? SyncOutcome::kept : SyncOutcome::same;
            if (action == SaveAction::upload) {
                done = store->upload(*found, local, packed, true, &newer, &error);
                result.outcome = SyncOutcome::uploaded;
            }
        }
    }
    InStep now;
    if (result.outcome == SyncOutcome::same && local.present && !plan.remote_hash.empty() &&
        plan.action == SaveAction::none)
        now = {local.hash, local.hash == plan.remote_hash ? local.hash : synced.store, {}};
    if (action == SaveAction::upload && done) now = {local.hash, local.hash, {}};
    if (action == SaveAction::download) {
        done = store->download(*found, plan, fetched, &error) && Replace(fetched, places, title, &error);
        // What the console has now, as its folder is hashed: a zip packed another way (JKSV's,
        // its files at the top) has another hash than the folder it unpacks to.
        SaveArchive::Folder unpacked;
        if (done && SaveArchive::List(places.save, title, places.lister, &unpacked, &error) &&
            SaveArchive::Hash(unpacked, &now.console, &error))
            now.store = plan.remote_hash;
        result.outcome = SyncOutcome::downloaded;
    }
    now.game = found->id;
    if (done && !now.console.empty() && !now.store.empty()) KeepSynced(places.store, server, title, now);
    store->finish(done);
    Remove(packed);
    Remove(fetched);
    if (!done) return Failed(error, user);
    return result;
}

} // namespace Eden::Remote
