// SPDX-License-Identifier: GPL-3.0-or-later
// A download source: a place that has Switch games and hands them out file by file. Each kind of
// source (a kind of game server) is a backend that implements this interface; remote.h lists the
// games of every source set up, downloads them and keeps what it needs between starts, the same
// way for every backend. A backend only talks to its server.
//
// A new backend: a class deriving from Source in a folder of its own (remote/<type>/), and a line
// in backends.cpp that makes it for its "type" in sources.json.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Eden::Remote {

// What a file of a game is: the game itself goes to roms/, its updates and DLC to updates/.
enum class FileKind : std::uint8_t { game, update, dlc };

struct SourceFile {
    std::string id;         // the source's own name for it (it fetches it by this)
    std::string name;       // its file name, also on the console
    std::uint64_t size = 0; // 0 when the source does not know
    FileKind kind = FileKind::game;
};

struct SourceGame {
    std::string id;    // the source's own name for the game
    std::string name;  // as the source names it
    // What tells the game apart on any source, as far as this one knows (remote.h, SameGame):
    std::string title_id;                     // its Switch title ID ("0100000000010000"); empty when unknown
    std::map<std::string, std::string> ids;   // its ids at metadata providers: "igdb" -> "1234",
                                              // "screenscraper" -> "195863"... (lower-case provider names)
    bool identified = false;                  // its name comes from such metadata, not from a file name
    std::string cover; // the source's own name for its cover, which changes when the picture does;
                       // empty without one
    std::vector<SourceFile> files; // its files the console can use (other kinds are left out)
};

// Where a download's bytes go (remote.cpp).
class Receiver {
  public:
    virtual ~Receiver() = default;
    // Before the first byte: from_start when the file comes from its beginning, not from the
    // offset asked for (a source that cannot go on). False stops the transfer.
    virtual bool begin(bool from_start) = 0;
    // The next bytes; false stops the transfer.
    virtual bool take(const void* data, std::size_t size) = 0;
    // Asked often while the transfer runs: true stops it (a game starts, a cancel).
    virtual bool stopped() = 0;
};

// Asked often during a list or a cover: true stops it.
using Stopped = std::function<bool()>;

class Source {
  public:
    virtual ~Source() = default;
    // Where it is, as the player reads it ("http://nas:3000").
    virtual std::string address() const = 0;
    // All of its Switch games. Blocking (the network); false with *error, in English.
    virtual bool list(std::vector<SourceGame>* games, std::string* error, const Stopped& stopped) = 0;
    // A game's cover as a picture file's bytes (PNG or JPEG); false when there is none.
    virtual bool cover(const SourceGame& game, std::string* picture, const Stopped& stopped) = 0;
    // A file of a game from `offset` on, into the receiver (or from its start: Receiver::begin).
    // True when all of it came; false with *error otherwise, or when the receiver stopped it.
    virtual bool fetch(const SourceGame& game, const SourceFile& file, std::uint64_t offset, Receiver& receiver,
                       std::string* error) = 0;
};

// Makes the source of a "type" from its entry in sources.json (backends.cpp). nullptr with *error
// when the type is unknown or the entry is not usable.
std::unique_ptr<Source> MakeSource(const std::string& type, const nlohmann::json& settings, std::string* error);

} // namespace Eden::Remote
