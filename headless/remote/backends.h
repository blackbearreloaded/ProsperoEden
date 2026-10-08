// SPDX-License-Identifier: GPL-3.0-or-later
// The backends, by their "type" in the config files: what each kind of server can be used for.
// Each use has an interface of its own (a download source: source.h, a profile's save data:
// save_store.h), and a backend makes an
// object of it for an entry of its config file, or has none (nullptr) when its server cannot be
// used that way. A server's own folder (remote/<type>/) has what its uses share (romm_client.h).
//
// A new backend: a line in Backends() (backends.cpp) with what it makes. A new use: a column of
// Backend, a Make... below, and nullptr in it for the backends that cannot do it.
#pragma once

#include "remote/save_store.h"
#include "remote/source.h"

#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Eden::Remote {

struct Backend {
    const char* type; // "romm"
    // A download source from its entry in sources.json; nullptr: it has no games to download.
    std::unique_ptr<Source> (*source)(const nlohmann::json& settings, std::string* error);
    // A profile's save store from its entry in save-sync.json, keeping what it needs between
    // starts in `folder` (the profile's own); nullptr: it cannot keep save data.
    std::unique_ptr<SaveStore> (*saves)(const nlohmann::json& settings, const std::string& folder, std::string* error);
};

// All of them, in the order the menu offers them.
const std::vector<Backend>& Backends();
// The backend of a type; nullptr when there is none.
const Backend* FindBackend(const std::string& type);

// Makes the source of a "type" from its entry in sources.json. nullptr with *error when the type
// is unknown, cannot be a download source or the entry is not usable.
std::unique_ptr<Source> MakeSource(const std::string& type, const nlohmann::json& settings, std::string* error);
// The same for a profile's save store, from its entry in save-sync.json.
std::unique_ptr<SaveStore> MakeSaveStore(const std::string& type, const nlohmann::json& settings,
                                         const std::string& folder, std::string* error);

} // namespace Eden::Remote
