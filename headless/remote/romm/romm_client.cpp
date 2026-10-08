// SPDX-License-Identifier: GPL-3.0-or-later
// A RomM server as its backends talk to it; see romm_client.h.
#include "romm_client.h"

#include "remote/http.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <utility>

namespace Eden::Remote::Romm {
namespace {

using Json = nlohmann::json;

// Games asked for per page of /api/roms, and the most pages read.
constexpr int kPage = 200;
constexpr int kMostPages = 100;
// The most a JSON answer may be.
constexpr std::size_t kMostJson = 64u << 20;

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

// A GET into memory; the transfer's result, as remote_http_get gives it.
int Request(const std::string& url, const char* authorization, long timeout_ms, Memory& memory,
            remote_http_result* result) {
    remote_http_request request{};
    request.url = url.c_str();
    request.authorization = authorization;
    request.timeout_ms = timeout_ms;
    request.sink = MemoryTake;
    request.stop = MemoryStop;
    request.user = &memory;
    return remote_http_get(&request, result);
}

} // namespace

std::unique_ptr<Client> Client::Make(const nlohmann::json& settings, const std::string& file, std::string* error) {
    const std::string url = NormalUrl(Text(settings, "url"));
    if (url.empty()) {
        *error = "No server address (\"url\") in " + file;
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
            *error = "The user name or password in " + file + " is too long";
            return nullptr;
        }
        authorization = basic;
    }
    std::string platform = Lower(Text(settings, "platform"));
    if (platform.empty()) platform = "switch";
    return std::unique_ptr<Client>(new Client(url, authorization, platform, file));
}

std::string Client::StatusError(int status, const std::string& path) const {
    if (status == 401) return "RomM did not accept the token or password in " + file_;
    if (status == 403) {
        // A refusal: RomM 4.9 answers a wrong token or password so as well, so both are named, with
        // the scope a client API token needs for what was asked.
        static constexpr std::pair<const char*, const char*> kScopes[] = {
            {"/api/platforms", "platforms.read"}, {"/api/roms", "roms.read"}, {"/api/saves", "assets.read and assets.write"},
            {"/api/sync", "assets.read and devices.read"}, {"/api/devices", "devices.write"}};
        for (const auto& [prefix, scope] : kScopes)
            if (path.starts_with(prefix))
                return "RomM did not accept the token or password in " + file_ + ", or the token lacks the scope " +
                       scope;
        return "RomM did not accept the token or password in " + file_ + ", or did not let it do this";
    }
    if (status == 404) return "The server has no such page (is the address in " + file_ + " RomM's?)";
    return "The server answered with status " + std::to_string(status);
}

bool Client::Get(const std::string& path, const Stopped& stopped, std::string* body, std::string* error) const {
    Memory memory;
    memory.limit = kMostJson;
    memory.stopped = &stopped;
    remote_http_result result{};
    if (Request(url_ + path, authorization_.c_str(), 60000, memory, &result) != 0) {
        *error = MemoryStop(&memory) ? "Stopped" : result.stopped ? "The server's answer is too large" : result.error;
        return false;
    }
    if (result.status != 200) {
        *error = StatusError(result.status, path);
        return false;
    }
    *body = std::move(memory.body);
    return true;
}

bool Client::Fetch(const std::string& path_or_url, std::size_t limit, const Stopped& stopped, std::string* body) const {
    // The server's own files may need the sign-in; another site's (IGDB) never gets it.
    const bool own = !path_or_url.starts_with("http://") && !path_or_url.starts_with("https://");
    Memory memory;
    memory.limit = limit;
    memory.stopped = &stopped;
    remote_http_result result{};
    if (Request(own ? url_ + path_or_url : path_or_url, own ? authorization_.c_str() : nullptr, 30000, memory,
                &result) != 0 ||
        result.status != 200)
        return false;
    *body = std::move(memory.body);
    return true;
}

bool Client::Send(const char* method, const std::string& path, const std::string& body, Answer* answer,
                  std::string* error, const Stopped& stopped) const {
    Memory memory;
    memory.limit = kMostJson;
    memory.stopped = &stopped;
    const std::string url = url_ + path;
    remote_http_request request{};
    request.url = url.c_str();
    request.authorization = authorization_.c_str();
    request.timeout_ms = 60000;
    request.method = method;
    if (!body.empty()) {
        request.body = body.c_str();
        request.body_size = body.size();
        request.content_type = "application/json";
    }
    request.sink = MemoryTake;
    request.stop = MemoryStop;
    request.user = &memory;
    remote_http_result result{};
    if (remote_http_run(&request, &result) != 0) {
        *error = MemoryStop(&memory) ? "Stopped" : result.stopped ? "The server's answer is too large" : result.error;
        return false;
    }
    answer->status = result.status;
    answer->body = std::move(memory.body);
    return true;
}

bool Client::Upload(const char* method, const std::string& path, const std::string& field, const std::string& file,
                    const std::string& name, Answer* answer, std::string* error, const Stopped& stopped) const {
    Memory memory;
    memory.limit = kMostJson;
    memory.stopped = &stopped;
    const std::string url = url_ + path;
    remote_http_request request{};
    request.url = url.c_str();
    request.authorization = authorization_.c_str();
    request.method = method;
    request.file_field = field.c_str();
    request.file_path = file.c_str();
    request.file_name = name.c_str();
    request.sink = MemoryTake;
    request.stop = MemoryStop;
    request.user = &memory;
    remote_http_result result{};
    if (remote_http_run(&request, &result) != 0) {
        *error = MemoryStop(&memory) ? "Stopped" : result.stopped ? "The server's answer is too large" : result.error;
        return false;
    }
    answer->status = result.status;
    answer->body = std::move(memory.body);
    return true;
}

namespace {
// A file's bytes into a file on the console.
struct FileSink {
    FILE* file = nullptr;
    int status = 0;
    const Stopped* stopped = nullptr;
};

int FileStop(void* user) {
    const FileSink& sink = *static_cast<const FileSink*>(user);
    return sink.stopped != nullptr && *sink.stopped && (*sink.stopped)() ? 1 : 0;
}

int FileBegin(void* user, int status, std::uint64_t) {
    static_cast<FileSink*>(user)->status = status;
    return status == 200 ? 1 : 0;
}

int FileTake(void* user, const void* data, std::size_t size) {
    return std::fwrite(data, 1, size, static_cast<FileSink*>(user)->file) == size ? 1 : 0;
}
} // namespace

bool Client::Download(const std::string& path, const std::string& file, std::string* error,
                      const Stopped& stopped) const {
    FileSink sink;
    sink.stopped = &stopped;
    sink.file = std::fopen(file.c_str(), "wb");
    if (sink.file == nullptr) {
        *error = "Cannot write " + file;
        return false;
    }
    const std::string url = url_ + path;
    remote_http_request request{};
    request.url = url.c_str();
    request.authorization = authorization_.c_str();
    request.raw = 1;
    request.begin = FileBegin;
    request.sink = FileTake;
    request.stop = FileStop;
    request.user = &sink;
    remote_http_result result{};
    const int got = remote_http_run(&request, &result);
    const bool closed = std::fclose(sink.file) == 0;
    if (got == 0 && sink.status == 200 && closed) return true;
    std::remove(file.c_str());
    if (sink.status != 0 && sink.status != 200) *error = StatusError(sink.status);
    else if (FileStop(&sink)) *error = "Stopped";
    else if (!closed || (result.stopped && sink.status == 200)) *error = "Cannot write " + file;
    else *error = result.error[0] ? std::string{result.error} : "The download stopped";
    return false;
}

bool Client::Platform(const Stopped& stopped, std::int64_t* id, std::string* error) const {
    std::string body;
    if (!Get("/api/platforms", stopped, &body, error)) return false;
    const Json platforms = Json::parse(body, nullptr, false);
    if (!platforms.is_array()) {
        *error = "The server's platform list cannot be read (is the address in " + file_ + " RomM's?)";
        return false;
    }
    *id = 0;
    for (const Json& item : platforms)
        if (item.is_object() && (Lower(Text(item, "slug")) == platform_ || Lower(Text(item, "fs_slug")) == platform_))
            *id = Number(item, "id");
    if (*id == 0) {
        *error = "The server has no \"" + platform_ + "\" platform";
        return false;
    }
    return true;
}

bool Client::Games(std::vector<SourceGame>* games, const Stopped& stopped, std::string* error) const {
    std::int64_t platform = 0;
    if (!Platform(stopped, &platform, error)) return false;
    games->clear();
    // From where the last page ended: a server may hand out fewer games a page than asked for.
    std::size_t offset = 0;
    std::string body;
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
