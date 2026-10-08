// SPDX-License-Identifier: GPL-3.0-or-later
// The RomM backend of the download sources (https://github.com/rommapp/romm): the Switch games of
// a RomM server. Its entry in config/remote/sources.json, with the server as romm_client.h has it:
//
//   { "type": "romm", "name": "Home",
//     "url": "http://192.168.1.20:3000",
//     "token": "rmm_...",                      a RomM client API token (Profile > Client tokens),
//     "username": "me", "password": "secret",  or the account's name and password instead
//     "platform": "switch" }                   optional: the platform's slug on the server
//
// It needs a current RomM: the game list as pages with each ROM's files and their categories (the
// game, its updates and DLC; manuals, mods and the like are left out), and a file downloaded by
// its own id (/api/roms/<file id>/files/content/<name>), which goes on from where it was (Range).
#pragma once

#include "remote/source.h"

#include <memory>
#include <string>

#include <nlohmann/json.hpp>

namespace Eden::Remote::Romm {

std::unique_ptr<Source> MakeSource(const nlohmann::json& settings, std::string* error);

} // namespace Eden::Remote::Romm
