// SPDX-License-Identifier: GPL-3.0-or-later
// The backends, by their "type" in the config files; see backends.h.
#include "remote/backends.h"

#include "remote/romm/romm_saves.h"
#include "remote/romm/romm_source.h"

namespace Eden::Remote {

const std::vector<Backend>& Backends() {
    static const std::vector<Backend> backends = {
        {"romm", Romm::MakeSource, Romm::MakeSaves, &Romm::kPairing},
    };
    return backends;
}

const Backend* FindBackend(const std::string& type) {
    for (const Backend& backend : Backends())
        if (type == backend.type) return &backend;
    return nullptr;
}

std::unique_ptr<Source> MakeSource(const std::string& type, const nlohmann::json& settings, std::string* error) {
    const Backend* backend = FindBackend(type);
    if (backend == nullptr) {
        *error = type.empty() ? "A source in sources.json has no \"type\"" : "Unknown source type \"" + type + "\"";
        return nullptr;
    }
    if (backend->source == nullptr) {
        *error = "A \"" + type + "\" server has no games to download";
        return nullptr;
    }
    return backend->source(settings, error);
}

std::unique_ptr<SaveStore> MakeSaveStore(const std::string& type, const nlohmann::json& settings,
                                         const std::string& folder, std::string* error) {
    const Backend* backend = FindBackend(type);
    if (backend == nullptr) {
        *error = "Unknown type \"" + type + "\" in save-sync.json";
        return nullptr;
    }
    if (backend->saves == nullptr) {
        *error = "A \"" + type + "\" server cannot keep save data";
        return nullptr;
    }
    return backend->saves(settings, folder, error);
}

} // namespace Eden::Remote
