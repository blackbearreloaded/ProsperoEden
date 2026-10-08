// SPDX-License-Identifier: GPL-3.0-or-later
// Pairing: a profile's save store signed in without typing (as RFC 8628's device flow). The
// console asks the server for a code and shows it with an address (as a QR code); the player opens
// that on a phone, signs in to the server as themself and approves; the console, asking now and
// then, gets a sign-in of its own, which goes into the profile's entry in save-sync.json. A backend
// that can do it has a Pairing in its line of backends.cpp (backends.h).
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include <nlohmann/json.hpp>

namespace Eden::Remote {

struct PairingStart {
    std::string device_code; // the console's name for the request (asked about by poll)
    std::string user_code;   // what the player sees, to tell it is this console's request
    std::string address;     // where the player approves it (the QR code), the code in it
    int expires_in = 0;      // seconds the request lasts
    int interval = 5;        // seconds between polls
};

enum class PairingState : std::uint8_t {
    pending,  // not yet approved: ask again after `interval` seconds
    approved, // `entry` has the sign-in
    denied,   // the player said no
    expired,  // nobody approved it in time
    failed,   // see error
};

struct PairingResult {
    PairingState state = PairingState::failed;
    std::string error;    // failed: what went wrong, in English
    nlohmann::json entry; // approved: what the profile's entry in save-sync.json gets ("token", ...)
    std::string user;     // approved: who it signed in as, when the server says
    int interval = 0;     // pending: the seconds to wait now (more when it asked too often)
};

struct Pairing {
    // A request for a code from the server an entry names (its "url"). folder: the profile's own
    // (backends.h), for what the backend keeps about the console. False with *error. stopped: true
    // once it is cancelled (the player went back, a game starts): what it sends stops then.
    bool (*start)(const nlohmann::json& settings, const std::string& folder, PairingStart* start, std::string* error,
                  const std::function<bool()>& stopped);
    // Whether the request was approved yet.
    PairingResult (*poll)(const nlohmann::json& settings, const std::string& folder, const PairingStart& start,
                          const std::function<bool()>& stopped);
};

} // namespace Eden::Remote
