// SPDX-License-Identifier: GPL-3.0-or-later
// A save store: a place that keeps the save data of a profile's games besides the console, as one
// packed file (a zip, save_archive.h) per game. Each kind of server that can keep save data is a
// backend that implements this interface (backends.h); save_sync.h decides what to do with a
// game's save data, the same way for every backend, and does it through this. A backend only
// talks to its server.
//
// A store is made for a profile's entry in save-sync.json, with a folder of its own on the console
// for what it keeps between starts (the device it is to the server, for example).
#pragma once

#include "remote/source.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Eden::Remote {

// The console's save data of a game, packed: what the store compares with its own copy.
struct LocalSave {
    bool present = false;      // false: the console has none of the game's save data
    std::string file_name;     // the packed file's name ("0100000000010000.zip")
    std::string hash;          // the content hash of the packed file (save_archive.h)
    std::int64_t updated = 0;  // the newest change of its files, seconds since 1970 (UTC)
    std::uint64_t size = 0;    // its files' size
};

enum class SaveAction : std::uint8_t {
    none,      // both have the same, or neither has any
    upload,    // the console's is newer, or the store has none
    download,  // the store's is newer, or the console has none
    conflict,  // both changed since they were last the same: the player chooses
};

// What the store says about a game's save data.
struct SavePlan {
    SaveAction action = SaveAction::none;
    std::string reason;          // why, in English, for the log
    std::string remote;          // the store's own name for its copy; empty without one
    std::int64_t remote_updated = 0; // when its copy was made, seconds since 1970 (UTC)
    std::string remote_hash;     // its content hash (save_archive.h); empty when not known
    std::string remote_device;   // what made it ("Pixel 8"); empty when not known
    std::uint64_t remote_size = 0;
};

class SaveStore {
  public:
    virtual ~SaveStore() = default;
    // Where it is, as the player reads it ("http://nas:3000").
    virtual std::string address() const = 0;
    // Signs in and gets ready for a sync. Blocking (the network); false with *error, in English.
    virtual bool prepare(const Stopped& stopped, std::string* error) = 0;
    // Who it is signed in as ("mario"), once prepared; empty when the server does not say.
    virtual std::string user() const = 0;
    // After prepare() failed: whether it was because the server is older than the backend takes,
    // with the server's version and the oldest one taken.
    virtual bool too_old(std::string* version, std::string* needed) const {
        (void)version;
        (void)needed;
        return false;
    }
    // The Switch games it can keep save data of, as a download source lists its games (only
    // game.id and what tells a game apart need to be there).
    virtual bool games(std::vector<SourceGame>* games, const Stopped& stopped, std::string* error) = 0;
    // What to do with a game's save data (game: one of games()).
    virtual bool compare(const SourceGame& game, const LocalSave& local, SavePlan* plan, std::string* error) = 0;
    // Keeps the packed file `path` as the game's save data. overwrite: also when the store's copy
    // changed since this console last had it (the player chose the console's in a conflict).
    // Without it, such a copy refuses the upload: false with *newer set (a conflict after all; a
    // store may only find it here).
    virtual bool upload(const SourceGame& game, const LocalSave& local, const std::string& path, bool overwrite,
                        bool* newer, std::string* error) = 0;
    // The store's copy (plan: from compare()) into the file `path`.
    virtual bool download(const SourceGame& game, const SavePlan& plan, const std::string& path,
                          std::string* error) = 0;
    // A sync of a game is over: done tells whether all of it worked.
    virtual void finish(bool done) = 0;
};

} // namespace Eden::Remote
