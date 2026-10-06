// SPDX-License-Identifier: GPL-3.0-or-later
// The RomM backend of the download sources (https://github.com/rommapp/romm): the Switch games of
// a RomM server. Its entry in config/remote/sources.json:
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

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace Eden::Remote::Romm {

std::unique_ptr<Source> Make(const nlohmann::json& settings, std::string* error);

// ---- the parts, for tests ----
// The server's address as typed, made usable: "nas:3000/" is http://nas:3000.
std::string NormalUrl(std::string url);
// Adds the games of a page of /api/roms to games; false when the answer is no such page. listed:
// the entries the page had; total: the games the server has.
bool ParsePage(const std::string& json, std::vector<SourceGame>* games, std::size_t* listed = nullptr,
               std::size_t* total = nullptr);

} // namespace Eden::Remote::Romm
