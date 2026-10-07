// SPDX-License-Identifier: GPL-3.0-or-later
// The RomM backend of the download sources; see romm_source.h.
#include "romm_source.h"

#include "remote/http.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <utility>

namespace Eden::Remote::Romm {
namespace {

using Json = nlohmann::json;

// Games asked for per page of /api/roms, and the most pages read.
constexpr int kPage = 200;
constexpr int kMostPages = 100;
// The most a list or a cover may be.
constexpr std::size_t kMostJson = 64u << 20;
constexpr std::size_t kMostCover = 8u << 20;

// RomM's fields with a game's id at a metadata provider, and the provider's name for SourceGame::ids.
constexpr std::pair<const char*, const char*> kProviders[] = {
    {"igdb_id", "igdb"},           {"ss_id", "screenscraper"},     {"moby_id", "mobygames"},
    {"launchbox_id", "launchbox"}, {"hasheous_id", "hasheous"},    {"tgdb_id", "thegamesdb"},
    {"ra_id", "retroachievements"}, {"sgdb_id", "steamgriddb"},    {"hltb_id", "howlongtobeat"},
    {"flashpoint_id", "flashpoint"}, {"libretro_id", "libretro"},
};

std::string Lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::string Text(const Json& object, const char* key) {
    const auto found = object.find(key);
    return found != object.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

bool Flag(const Json& object, const char* key) {
    const auto found = object.find(key);
    return found != object.end() && found->is_boolean() && found->get<bool>();
}

std::int64_t Number(const Json& object, const char* key) {
    const auto found = object.find(key);
    if (found == object.end()) return 0;
    if (found->is_number_integer()) return found->get<std::int64_t>();
    if (found->is_number_unsigned()) return static_cast<std::int64_t>(found->get<std::uint64_t>());
    return 0;
}

std::string StatusError(int status) {
    if (status == 401 || status == 403) return "RomM did not accept the token or password in sources.json";
    if (status == 404) return "The server has no such page (is the address in sources.json RomM's?)";
    return "The server answered with status " + std::to_string(status);
}

struct Memory {
    std::string body;
    std::size_t limit = 0;
    const Stopped* stopped = nullptr;
};

int MemoryTake(void* user, const void* data, std::size_t size) {
    Memory& memory = *static_cast<Memory*>(user);
    if (memory.body.size() + size > memory.limit) return 0;
    memory.body.append(static_cast<const char*>(data), size);
    return 1;
}

int MemoryStop(void* user) {
    const Memory& memory = *static_cast<const Memory*>(user);
    return memory.stopped != nullptr && *memory.stopped && (*memory.stopped)() ? 1 : 0;
}

// A file's bytes, into the download's receiver.
struct Transfer {
    Receiver* receiver = nullptr;
    int status = 0;
};

int TransferBegin(void* user, int status, std::uint64_t) {
    Transfer& transfer = *static_cast<Transfer*>(user);
    transfer.status = status;
    // 206: from the offset asked for; 200: the whole file. Anything else is an error page.
    if (status != 200 && status != 206) return 0;
    return transfer.receiver->begin(status == 200) ? 1 : 0;
}

int TransferTake(void* user, const void* data, std::size_t size) {
    return static_cast<Transfer*>(user)->receiver->take(data, size) ? 1 : 0;
}

int TransferStop(void* user) { return static_cast<Transfer*>(user)->receiver->stopped() ? 1 : 0; }

class RommSource final : public Source {
  public:
    RommSource(std::string url, std::string authorization, std::string platform)
        : url_(std::move(url)), authorization_(std::move(authorization)), platform_(std::move(platform)) {}

    std::string address() const override { return url_; }

    bool list(std::vector<SourceGame>* games, std::string* error, const Stopped& stopped) override {
        std::string body;
        if (!Get("/api/platforms", stopped, &body, error)) return false;
        const Json platforms = Json::parse(body, nullptr, false);
        if (!platforms.is_array()) {
            *error = "The server's platform list cannot be read (is the address in sources.json RomM's?)";
            return false;
        }
        std::int64_t platform = 0;
        for (const Json& item : platforms)
            if (item.is_object() && (Lower(Text(item, "slug")) == platform_ || Lower(Text(item, "fs_slug")) == platform_))
                platform = Number(item, "id");
        if (platform == 0) {
            *error = "The server has no \"" + platform_ + "\" platform";
            return false;
        }
        games->clear();
        // From where the last page ended: a server may hand out fewer games a page than asked for.
        std::size_t offset = 0;
        for (int page = 0; page < kMostPages; ++page) {
            const std::string query = "/api/roms?platform_ids=" + std::to_string(platform) +
                "&with_files=true&limit=" + std::to_string(kPage) + "&offset=" + std::to_string(offset) +
                "&order_by=name&order_dir=asc&with_char_index=false&with_filter_values=false&with_rom_id_index=false";
            if (!Get(query, stopped, &body, error)) return false;
            std::size_t listed = 0;
            std::size_t total = 0;
            if (!ParsePage(body, games, &listed, &total)) {
                *error = "The server's game list cannot be read";
                return false;
            }
            offset += listed;
            if (listed == 0 || offset >= total) break;
        }
        return true;
    }

    bool cover(const SourceGame& game, std::string* picture, const Stopped& stopped) override {
        if (game.cover.empty()) return false;
        // The server's own pictures may need the sign-in; another site's (IGDB) never gets it. Its
        // "?ts=..." only tells versions of the picture apart.
        const bool own = !game.cover.starts_with("http://") && !game.cover.starts_with("https://");
        const std::string url = own ? url_ + game.cover.substr(0, game.cover.find('?')) : game.cover;
        Memory memory;
        memory.limit = kMostCover;
        memory.stopped = &stopped;
        remote_http_request request{};
        request.url = url.c_str();
        request.authorization = own ? authorization_.c_str() : nullptr;
        request.timeout_ms = 30000;
        request.sink = MemoryTake;
        request.stop = MemoryStop;
        request.user = &memory;
        remote_http_result result{};
        if (remote_http_get(&request, &result) != 0 || result.status != 200) return false;
        *picture = std::move(memory.body);
        return true;
    }

    bool fetch(const SourceGame&, const SourceFile& file, std::uint64_t offset, Receiver& receiver,
               std::string* error) override {
        char escaped[1024];
        if (remote_http_escape(file.name.c_str(), escaped, sizeof(escaped)) != 0) {
            *error = "The file name is too long";
            return false;
        }
        // The file by its own id, as RomM's feeds give it.
        const std::string url = url_ + "/api/roms/" + file.id + "/files/content/" + escaped;
        for (;;) {
            Transfer transfer;
            transfer.receiver = &receiver;
            remote_http_request request{};
            request.url = url.c_str();
            request.authorization = authorization_.c_str();
            request.resume_from = offset;
            request.raw = 1;
            request.begin = TransferBegin;
            request.sink = TransferTake;
            request.stop = TransferStop;
            request.user = &transfer;
            remote_http_result result{};
            const int got = remote_http_get(&request, &result);
            // 416: the file on the server is shorter than what was begun: it starts again.
            if (transfer.status == 416 && offset > 0 && !receiver.stopped()) {
                offset = 0;
                continue;
            }
            if (transfer.status != 200 && transfer.status != 206) {
                *error = transfer.status == 0 ? std::string{result.error} : StatusError(transfer.status);
                return false;
            }
            if (got != 0) {
                *error = result.error[0] ? std::string{result.error} : "The download stopped";
                return false;
            }
            return true;
        }
    }

  private:
    // A JSON answer from the server; false with *error when there is none.
    bool Get(const std::string& path, const Stopped& stopped, std::string* body, std::string* error) const {
        Memory memory;
        memory.limit = kMostJson;
        memory.stopped = &stopped;
        const std::string url = url_ + path;
        remote_http_request request{};
        request.url = url.c_str();
        request.authorization = authorization_.c_str();
        request.timeout_ms = 60000;
        request.sink = MemoryTake;
        request.stop = MemoryStop;
        request.user = &memory;
        remote_http_result result{};
        if (remote_http_get(&request, &result) != 0) {
            *error = MemoryStop(&memory) ? "Stopped" : result.stopped ? "The server's answer is too large" : result.error;
            return false;
        }
        if (result.status != 200) {
            *error = StatusError(result.status);
            return false;
        }
        *body = std::move(memory.body);
        return true;
    }

    const std::string url_;
    const std::string authorization_;
    const std::string platform_;
};

} // namespace

std::unique_ptr<Source> Make(const nlohmann::json& settings, std::string* error) {
    const std::string url = NormalUrl(Text(settings, "url"));
    if (url.empty()) {
        *error = "No server address (\"url\") in sources.json";
        return nullptr;
    }
    std::string authorization;
    const std::string token = Text(settings, "token");
    const std::string user = Text(settings, "username");
    if (!token.empty()) {
        authorization = "Bearer " + token;
    } else if (!user.empty()) {
        char basic[600];
        if (remote_http_basic(user.c_str(), Text(settings, "password").c_str(), basic, sizeof(basic)) != 0) {
            *error = "The user name or password in sources.json is too long";
            return nullptr;
        }
        authorization = basic;
    }
    std::string platform = Lower(Text(settings, "platform"));
    if (platform.empty()) platform = "switch";
    return std::make_unique<RommSource>(url, authorization, platform);
}

std::string NormalUrl(std::string url) {
    while (!url.empty() && std::isspace(static_cast<unsigned char>(url.back()))) url.pop_back();
    while (!url.empty() && std::isspace(static_cast<unsigned char>(url.front()))) url.erase(url.begin());
    while (!url.empty() && url.back() == '/') url.pop_back();
    if (url.empty()) return url;
    if (url.find("://") == std::string::npos) url = "http://" + url;
    const std::string scheme = Lower(url.substr(0, url.find("://")));
    if (scheme != "http" && scheme != "https") return {};
    return scheme + url.substr(url.find("://"));
}

bool ParsePage(const std::string& text, std::vector<SourceGame>* games, std::size_t* listed, std::size_t* total) {
    if (listed) *listed = 0;
    if (total) *total = 0;
    const Json json = Json::parse(text, nullptr, false);
    const auto items = json.is_object() ? json.find("items") : json.end();
    if (!json.is_object() || items == json.end() || !items->is_array()) return false;
    if (total) *total = static_cast<std::size_t>(std::max<std::int64_t>(Number(json, "total"), 0));
    if (listed) *listed = items->size();
    for (const Json& item : *items) {
        if (!item.is_object() || Flag(item, "missing_from_fs") || Number(item, "id") <= 0) continue;
        SourceGame game;
        game.id = std::to_string(Number(item, "id"));
        game.name = Text(item, "name");
        if (game.name.empty()) game.name = Text(item, "fs_name_no_ext");
        if (game.name.empty()) game.name = Text(item, "fs_name");
        // What RomM's metadata says the game is: its title ID (read with the server's keys, when it
        // has them) and its id at each metadata provider it matched.
        game.title_id = Text(item, "title_id");
        game.identified = Flag(item, "is_identified");
        for (const auto& [field, provider] : kProviders) {
            const auto found = item.find(field);
            if (found == item.end()) continue;
            if (found->is_number_integer() && found->get<std::int64_t>() > 0) game.ids[provider] = std::to_string(found->get<std::int64_t>());
            else if (found->is_string() && !found->get<std::string>().empty()) game.ids[provider] = found->get<std::string>();
        }
        // RomM says what each file is (its category): the game, its updates and DLC are kept, the
        // rest (manuals, mods, soundtracks...) stays on the server. A file without a category is the
        // game when it is at the top of the ROM, as RomM itself takes it.
        if (const auto files = item.find("files"); files != item.end() && files->is_array()) {
            for (const Json& entry : *files) {
                if (!entry.is_object() || Number(entry, "id") <= 0) continue;
                SourceFile file;
                file.id = std::to_string(Number(entry, "id"));
                file.name = Text(entry, "file_name");
                file.size = static_cast<std::uint64_t>(std::max<std::int64_t>(Number(entry, "file_size_bytes"), 0));
                std::string category = Lower(Text(entry, "category"));
                if (category.empty() && (!entry.contains("is_top_level") || Flag(entry, "is_top_level"))) category = "game";
                if (category == "game") file.kind = FileKind::game;
                else if (category == "update") file.kind = FileKind::update;
                else if (category == "dlc") file.kind = FileKind::dlc;
                else continue;
                game.files.push_back(std::move(file));
            }
        }
        std::string cover = Text(item, "path_cover_small");
        if (cover.empty()) cover = Text(item, "path_cover_large");
        if (!cover.empty() && cover.front() != '/' && !cover.starts_with("http")) cover = "/assets/romm/resources/" + cover;
        game.cover = !cover.empty() ? cover : Text(item, "url_cover");
        games->push_back(std::move(game));
    }
    return true;
}

} // namespace Eden::Remote::Romm
