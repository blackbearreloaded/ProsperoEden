// SPDX-License-Identifier: GPL-3.0-or-later
// The RomM backend of the save stores (save_store.h): a profile's save data on a RomM server, as
// the user its entry in save-sync.json signs in as, with the server as romm_client.h has it:
//
//   { "profile": "<ID>", "__profile_name": "Player 1",
//     "type": "romm",
//     "url": "http://192.168.1.20:3000",
//     "token": "rmm_...",                      a RomM client API token with the scopes
//                                              platforms.read, roms.read, assets.read/write
//                                              and devices.read/write,
//     "username": "me", "password": "secret",  or the account's name and password instead
//     "platform": "switch" }                   optional: the platform's slug on the server
//
// It uses RomM's save sync (RomM 5.0 and newer): the console is a device of the user (registered
// once, kept in the profile's folder), a game's save data is the zip of its save folder in the
// slot "autosave" of the emulator "eden" (as Argosy keeps Eden's), and the server tells what to do
// with it (/api/sync/negotiate): it knows which version this console last had, so a save data
// changed on both sides is a conflict, not lost. Every upload is a new version on the server; it
// keeps the last ten of the slot.
#pragma once

#include "remote/pairing.h"
#include "remote/save_store.h"

#include <cstdint>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

namespace Eden::Remote::Romm {

// The oldest RomM the save sync takes: the oldest it was checked with (tools/check-romm.py). An
// older server is not synced with; every sync says so.
inline constexpr char kMinimumVersion[] = "5.0.0";

std::unique_ptr<SaveStore> MakeSaves(const nlohmann::json& settings, const std::string& folder, std::string* error);

// Pairing (pairing.h) by RomM's device authorization (RomM 5.0 and newer): the code is approved on
// the server's page /pair/device, signed in as the profile's user; the console gets a client API
// token bound to a device of its own, which the save sync then is.
extern const Pairing kPairing;

// ---- the parts, for tests ----
// A time as RomM writes it ("2026-10-08T12:34:56.123456+00:00", "...Z", or without a zone: UTC),
// in seconds since 1970; 0 when it is none.
std::int64_t ParseTime(const std::string& text);
// A time as RomM reads it ("2026-10-08T12:34:56Z").
std::string FormatTime(std::int64_t seconds);
// Whether a version RomM's heartbeat says ("4.9.0", "5.0.0-beta.1") is kMinimumVersion or newer;
// a version that is no number ("development") is taken to be.
bool CanSync(const std::string& version);

} // namespace Eden::Remote::Romm
