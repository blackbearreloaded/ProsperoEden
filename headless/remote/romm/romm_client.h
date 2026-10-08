// SPDX-License-Identifier: GPL-3.0-or-later
// A RomM server (https://github.com/rommapp/romm) as each of its backends talks to it: its address,
// the sign-in and the Switch games it has. The download source (romm_source.h) is one such
// backend; others (the save data of a profile) sign in with their own entry and use the same.
//
// What an entry of a config file says about the server:
//
//   "url": "http://192.168.1.20:3000",
//   "token": "rmm_...",                      a RomM client API token (Profile > Client tokens),
//   "username": "me", "password": "secret",  or the account's name and password instead
//   "platform": "switch"                     optional: the platform's slug on the server
#pragma once

#include "remote/source.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace Eden::Remote::Romm {

class Client {
  public:
    // From an entry of `file` (the config file's name, for what goes wrong: "sources.json").
    // nullptr with *error when the entry is not usable.
    static std::unique_ptr<Client> Make(const nlohmann::json& settings, const std::string& file, std::string* error);

    // "http://nas:3000", without a / at the end.
    const std::string& url() const { return url_; }

    // A JSON answer of the server to a GET of `path` ("/api/..."), signed in; false with *error.
    bool Get(const std::string& path, const Stopped& stopped, std::string* body, std::string* error) const;
    // A file of at most `limit` bytes: the server's own (a path, signed in) or another site's (a
    // whole address, not signed in). False when it did not come whole with status 200.
    bool Fetch(const std::string& path_or_url, std::size_t limit, const Stopped& stopped, std::string* body) const;
    // An answer of the server: its status and body (at most a JSON answer's size).
    struct Answer {
        int status = 0;
        std::string body;
    };
    // A request with a JSON body ("POST", "PUT"; body empty: none); false with *error when no
    // whole answer came (any status is an answer).
    // stopped: asked along the transfer; true stops it (a game starts, the menu closes).
    bool Send(const char* method, const std::string& path, const std::string& body, Answer* answer,
              std::string* error, const Stopped& stopped = {}) const;
    // A file sent as the form field `field` ("saveFile") of a POST or PUT, named `name`.
    bool Upload(const char* method, const std::string& path, const std::string& field, const std::string& file,
                const std::string& name, Answer* answer, std::string* error, const Stopped& stopped = {}) const;
    // A file of the server (a GET of `path`) into the file `file`; false with *error when it did
    // not come whole with status 200.
    bool Download(const std::string& path, const std::string& file, std::string* error,
                  const Stopped& stopped = {}) const;
    // The server's id of the platform asked for ("switch"); false with *error.
    bool Platform(const Stopped& stopped, std::int64_t* id, std::string* error) const;
    // All the games of the platform, with their files.
    bool Games(std::vector<SourceGame>* games, const Stopped& stopped, std::string* error) const;

    // The header value of the sign-in ("Bearer ...", "Basic ..."); empty without one.
    const std::string& authorization() const { return authorization_; }
    // What an answer's status means, for the screen. path: what was asked for ("/api/..."), so a
    // refusal (403) can name the token's scope that is missing.
    std::string StatusError(int status, const std::string& path = {}) const;

  private:
    Client(std::string url, std::string authorization, std::string platform, std::string file)
        : url_(std::move(url)), authorization_(std::move(authorization)), platform_(std::move(platform)),
          file_(std::move(file)) {}

    const std::string url_;
    const std::string authorization_;
    const std::string platform_;
    const std::string file_;
};

// ---- the parts, for tests ----
// The server's address as typed, made usable: "nas:3000/" is http://nas:3000.
std::string NormalUrl(std::string url);
// Adds the games of a page of /api/roms to games; false when the answer is no such page. listed:
// the entries the page had; total: the games the server has.
bool ParsePage(const std::string& json, std::vector<SourceGame>* games, std::size_t* listed = nullptr,
               std::size_t* total = nullptr);

} // namespace Eden::Remote::Romm
