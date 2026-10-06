// SPDX-License-Identifier: GPL-3.0-or-later
// The backends of the download sources, by their "type" in sources.json (source.h).
#include "remote/source.h"

#include "remote/romm/romm_source.h"

namespace Eden::Remote {

std::unique_ptr<Source> MakeSource(const std::string& type, const nlohmann::json& settings, std::string* error) {
    if (type == "romm") return Romm::Make(settings, error);
    *error = type.empty() ? "A source in sources.json has no \"type\"" : "Unknown source type \"" + type + "\"";
    return nullptr;
}

} // namespace Eden::Remote
