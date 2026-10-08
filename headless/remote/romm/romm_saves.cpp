// SPDX-License-Identifier: GPL-3.0-or-later
// The RomM backend of the save stores; see romm_saves.h.
#include "romm_saves.h"

#include "remote/http.h"
#include "remote/romm/romm_client.h"
#include "remote/save_archive.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <utility>

namespace Eden::Remote::Romm {
namespace {

using Json = nlohmann::json;

// Where a game's save data is on the server: Argosy keeps Eden's there too.
constexpr const char* kSlot = "autosave";
constexpr const char* kEmulator = "eden";
// The versions of the slot the server keeps.
constexpr int kKept = 10;

std::string Text(const Json& object, const char* key) {
    const auto found = object.find(key);
    return found != object.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

std::int64_t Number(const Json& object, const char* key) {
    const auto found = object.find(key);
    if (found == object.end()) return 0;
    if (found->is_number_integer()) return found->get<std::int64_t>();
    if (found->is_number_unsigned()) return static_cast<std::int64_t>(found->get<std::uint64_t>());
    return 0;
}

std::string ReadAll(const std::string& file) {
    std::ifstream in(file, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

bool WriteAll(const std::string& file, const std::string& text) {
    std::error_code ignored;
    std::filesystem::create_directories(std::filesystem::path(file).parent_path(), ignored);
    const std::string staged = file + ".tmp";
    {
        std::ofstream out(staged, std::ios::binary | std::ios::trunc);
        out << text;
        out.close();
        if (!out) {
            std::remove(staged.c_str());
            return false;
        }
    }
    if (std::rename(staged.c_str(), file.c_str()) == 0) return true;
    std::remove(staged.c_str());
    return false;
}

// What the server says went wrong, for the log: a refusal by the scope it needs (path: what was
// asked for), else its "detail", else the status.
std::string Detail(const Client& client, const Client::Answer& answer, const std::string& path) {
    if (answer.status == 401 || answer.status == 403) return client.StatusError(answer.status, path);
    const Json json = Json::parse(answer.body, nullptr, false);
    if (json.is_object()) {
        const auto detail = json.find("detail");
        if (detail != json.end() && detail->is_string()) return detail->get<std::string>() + " (" + std::to_string(answer.status) + ")";
    }
    return client.StatusError(answer.status);
}

// What tells this console apart from others of the user: made once, kept beside the profiles'
// folders (folder: a profile's).
std::string ConsoleId(const std::string& folder) {
    const std::string file = std::filesystem::path(folder).parent_path().string() + "/console-id";
    std::string id = ReadAll(file);
    while (!id.empty() && std::isspace(static_cast<unsigned char>(id.back()))) id.pop_back();
    if (id.size() == 16) return id;
    // Unlike any other console's is all it needs to be: the time, to the nanosecond, mixed.
    std::uint64_t mixed = static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count()) ^
                          (static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()) << 17);
    mixed = std::mt19937_64(mixed)();
    char text[17];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(mixed));
    id = text;
    (void)WriteAll(file, id + "\n");
    return id;
}

// What the save sync asks for: the games, their save data, this console as a device, and the
// user's name for the menu.
constexpr const char* kScopes[] = {"platforms.read", "roms.read",     "assets.read", "assets.write",
                                   "devices.read",   "devices.write", "me.read"};

class RommSaves final : public SaveStore {
  public:
    RommSaves(std::unique_ptr<Client> client, std::string folder)
        : client_(std::move(client)), folder_(std::move(folder)) {}

    std::string address() const override { return client_->url(); }
    std::string user() const override { return user_; }
    bool too_old(std::string* version, std::string* needed) const override {
        if (too_old_.empty()) return false;
        *version = too_old_;
        *needed = kMinimumVersion;
        return true;
    }

    bool prepare(const Stopped& stopped, std::string* error) override {
        stopped_ = stopped; // every transfer of this sync asks it
        std::string body;
        if (!client_->Get("/api/heartbeat", stopped, &body, error)) return false;
        const Json heartbeat = Json::parse(body, nullptr, false);
        const Json system = heartbeat.is_object() ? heartbeat.value("SYSTEM", Json::object()) : Json::object();
        const std::string version = system.is_object() ? Text(system, "VERSION") : std::string{};
        too_old_.clear();
        if (!CanSync(version)) {
            too_old_ = version;
            *error = "RomM " + version + " is older than the save sync takes: it needs RomM " + kMinimumVersion +
                     " or newer";
            return false;
        }
        // Who it is, for the menu and for what was in step with whom (save_sync.cpp): a token may
        // not be allowed to say (the scope me.read), always the same way. A server that does not
        // answer now ends the sync instead of making it someone else.
        Client::Answer answer;
        if (!client_->Send(nullptr, "/api/users/me", {}, &answer, error, stopped_)) return false;
        if (answer.status >= 500) {
            *error = Detail(*client_, answer, "/api/users/me");
            return false;
        }
        if (answer.status == 200) {
            const Json me = Json::parse(answer.body, nullptr, false);
            user_ = me.is_object() ? Text(me, "username") : std::string{};
        }
        return Device(false, error);
    }

    bool games(std::vector<SourceGame>* games, const Stopped& stopped, std::string* error) override {
        return client_->Games(games, stopped, error);
    }

    bool compare(const SourceGame& game, const LocalSave& local, SavePlan* plan, std::string* error) override {
        *plan = SavePlan{};
        Json saves = Json::array();
        if (local.present)
            saves.push_back({{"rom_id", RomId(game)}, {"file_name", local.file_name}, {"slot", kSlot},
                             {"emulator", kEmulator}, {"content_hash", local.hash},
                             {"updated_at", FormatTime(local.updated)}, {"file_size_bytes", local.size}});
        Client::Answer answer;
        for (int attempt = 0;; ++attempt) {
            const Json payload = {{"device_id", device_}, {"saves", saves}, {"rom_ids", Json::array({RomId(game)})},
                                  {"emulators", Json::array({kEmulator})}};
            if (!client_->Send("POST", "/api/sync/negotiate", payload.dump(), &answer, error, stopped_)) return false;
            // The device was taken off the server (by its user, in RomM's settings): once more as a new one.
            if (answer.status == 404 && attempt == 0) {
                if (!Device(true, error)) return false;
                continue;
            }
            break;
        }
        if (answer.status != 200) {
            *error = Detail(*client_, answer, "/api/sync");
            return false;
        }
        const Json result = Json::parse(answer.body, nullptr, false);
        if (!result.is_object()) {
            *error = "The server's answer cannot be read";
            return false;
        }
        // The sync session (RomM shows it with the device), and the server's reason for the log.
        // What to do is not taken from its operations: RomM 5.3 pairs the console's save data
        // with another emulator's in the same slot, and compares times when it no longer knows
        // what the console had. The newest copy of the slot is looked up instead, and the save
        // sync compares it with the console's by their contents (save_sync.h).
        session_ = Number(result, "session_id");
        if (const auto operations = result.find("operations"); operations != result.end() && operations->is_array())
            for (const Json& operation : *operations)
                if (operation.is_object() && Number(operation, "rom_id") == RomId(game) && Text(operation, "slot") == kSlot &&
                    Text(operation, "emulator") == kEmulator)
                    plan->reason = Text(operation, "reason");
        Client::Answer listed;
        if (!client_->Send(nullptr, "/api/saves?rom_id=" + std::to_string(RomId(game)) + "&slot=" + kSlot, {}, &listed,
                           error, stopped_))
            return false;
        const Json slot = Json::parse(listed.body, nullptr, false);
        if (listed.status != 200 || !slot.is_array()) {
            *error = listed.status != 200 ? Detail(*client_, listed, "/api/saves") :
                                            "The server's list of save data cannot be read";
            return false;
        }
        const Json* newest = nullptr;
        for (const Json& save : slot) {
            if (!save.is_object() || Text(save, "slot") != kSlot || Text(save, "emulator") != kEmulator) continue;
            const auto key = [](const Json& s) { return std::pair{ParseTime(Text(s, "updated_at")), Number(s, "id")}; };
            if (newest == nullptr || key(save) > key(*newest)) newest = &save;
        }
        if (newest == nullptr) {
            plan->action = local.present ? SaveAction::upload : SaveAction::none;
            return true;
        }
        plan->remote = std::to_string(Number(*newest, "id"));
        plan->remote_hash = Text(*newest, "content_hash");
        plan->remote_updated = ParseTime(Text(*newest, "updated_at"));
        plan->remote_size = static_cast<std::uint64_t>(std::max<std::int64_t>(Number(*newest, "file_size_bytes"), 0));
        if (!local.present) plan->action = SaveAction::download;
        else if (local.hash == plan->remote_hash) plan->action = SaveAction::none;
        else plan->action = SaveAction::conflict; // the save sync tells which side changed
        // The device and size of the server's copy are shown when the player is asked only.
        if (plan->action == SaveAction::conflict) Describe(plan);
        return true;
    }

    bool upload(const SourceGame& game, const LocalSave& local, const std::string& path, bool overwrite, bool* newer,
                std::string* error) override {
        *newer = false;
        std::string query = "/api/saves?rom_id=" + std::to_string(RomId(game)) + "&emulator=" + kEmulator +
            "&slot=" + kSlot + "&device_id=" + device_ + "&overwrite=" + (overwrite ? "true" : "false") +
            "&autocleanup=true&autocleanup_limit=" + std::to_string(kKept) + "&content_hash=" + local.hash;
        if (session_ > 0) query += "&session_id=" + std::to_string(session_);
        Client::Answer answer;
        if (!client_->Upload("POST", query, "saveFile", path, local.file_name, &answer, error, stopped_)) {
            ++failed_;
            return false;
        }
        if (answer.status != 200 && answer.status != 201) {
            ++failed_;
            // RomM's negotiate compares times when the console last had an older version of the
            // slot; the upload knows better: another device's came in since.
            if (answer.status == 409) {
                *newer = true;
                *error = "The server has newer save data than this console last had";
            }
            else if (answer.status == 413) *error = "The save data is too large for the server";
            else *error = Detail(*client_, answer, "/api/saves");
            return false;
        }
        ++done_;
        return true;
    }

    bool download(const SourceGame&, const SavePlan& plan, const std::string& path, std::string* error) override {
        if (plan.remote.empty()) {
            *error = "The server has no save data of the game";
            return false;
        }
        // Not taken as this console's version until it is unpacked (finish).
        if (!client_->Download("/api/saves/" + plan.remote + "/content?device_id=" + device_ + "&optimistic=false",
                               path, error, stopped_) ||
            !SaveArchive::HashZip(path, &confirm_hash_, error)) {
            ++failed_;
            return false;
        }
        confirm_ = plan.remote;
        ++done_;
        return true;
    }

    void finish(bool done) override {
        Client::Answer answer;
        std::string error;
        if (done && !confirm_.empty()) {
            const Json payload = {{"device_id", device_}, {"content_hash", confirm_hash_}};
            if (!client_->Send("POST", "/api/saves/" + confirm_ + "/downloaded", payload.dump(), &answer, &error) ||
                answer.status != 200)
                done = false;
        }
        if (!done && failed_ == 0) failed_ = 1;
        if (session_ > 0) {
            const Json payload = {{"operations_completed", done_}, {"operations_failed", failed_}};
            (void)client_->Send("POST", "/api/sync/sessions/" + std::to_string(session_) + "/complete", payload.dump(),
                                &answer, &error);
        }
        session_ = 0;
        done_ = failed_ = 0;
        confirm_.clear();
        confirm_hash_.clear();
    }

  private:
    static std::int64_t RomId(const SourceGame& game) {
        std::int64_t id = 0;
        for (const char c : game.id) {
            if (!std::isdigit(static_cast<unsigned char>(c))) return 0;
            id = id * 10 + (c - '0');
        }
        return id;
    }

    // This console as a device of the user: the one registered before, else a new one. again: the
    // server forgot the one it had.
    bool Device(bool again, std::string* error) {
        const std::string state_file = folder_ + "/romm-device.json";
        const Json state = Json::parse(ReadAll(state_file), nullptr, false);
        if (!again && state.is_object() && Text(state, "url") == client_->url() && Text(state, "user") == user_ &&
            !Text(state, "device_id").empty()) {
            device_ = Text(state, "device_id");
            return true;
        }
        const Json payload = {{"name", "ProsperoEden (PS5)"}, {"platform", "ps5"}, {"client", "ProsperoEden"},
                              {"hostname", "prosperoeden-" + ConsoleId(folder_)}, {"sync_mode", "api"},
                              {"allow_existing", true}};
        Client::Answer answer;
        if (!client_->Send("POST", "/api/devices", payload.dump(), &answer, error, stopped_)) return false;
        const Json made = Json::parse(answer.body, nullptr, false);
        if ((answer.status != 200 && answer.status != 201) || !made.is_object() || Text(made, "device_id").empty()) {
            *error = Detail(*client_, answer, "/api/devices");
            return false;
        }
        device_ = Text(made, "device_id");
        if (!WriteAll(state_file, Json{{"url", client_->url()}, {"user", user_}, {"device_id", device_}}.dump(2) + "\n")) {
            *error = "Cannot write " + state_file;
            return false;
        }
        return true;
    }

    // The size of the server's copy and the device that made it, for the player's choice.
    void Describe(SavePlan* plan) const {
        std::string body, error;
        if (!client_->Get("/api/saves/" + plan->remote + "?device_id=" + device_, stopped_, &body, &error)) return;
        const Json save = Json::parse(body, nullptr, false);
        if (!save.is_object()) return;
        plan->remote_size = static_cast<std::uint64_t>(std::max<std::int64_t>(Number(save, "file_size_bytes"), 0));
        if (plan->remote_updated == 0) plan->remote_updated = ParseTime(Text(save, "updated_at"));
        // The device that made it, by its name in RomM.
        const std::string origin = Text(save, "origin_device_id");
        if (origin.empty() || !client_->Get("/api/devices/" + origin, stopped_, &body, &error)) return;
        const Json device = Json::parse(body, nullptr, false);
        if (device.is_object()) plan->remote_device = Text(device, "name");
    }

    const std::unique_ptr<Client> client_;
    const std::string folder_;
    std::string user_;
    std::string too_old_; // the server's version, when it is older than kMinimumVersion
    Stopped stopped_;     // from prepare: a game starts, the menu closes
    std::string device_;
    std::int64_t session_ = 0;
    int done_ = 0;
    int failed_ = 0;
    std::string confirm_;      // the server's copy downloaded, to be taken as this console's
    std::string confirm_hash_;
};

bool PairStart(const nlohmann::json& settings, const std::string& folder, PairingStart* start, std::string* error,
               const Stopped& stopped) {
    // Asked of the server without a sign-in: the entry needs its address only.
    Json entry = settings.is_object() ? settings : Json::object();
    entry.erase("token");
    entry.erase("username");
    entry.erase("password");
    std::unique_ptr<Client> client = Client::Make(entry, "save-sync.json", error);
    if (!client) return false;
    const Json payload = {{"client_device_identifier", "prosperoeden-" + ConsoleId(folder)},
                          {"name", "ProsperoEden (PS5)"},
                          {"client", "ProsperoEden"},
                          {"platform", "ps5"},
                          {"requested_scopes", kScopes}};
    Client::Answer answer;
    if (!client->Send("POST", "/api/auth/device/init", payload.dump(), &answer, error, stopped)) return false;
    const Json made = Json::parse(answer.body, nullptr, false);
    if (answer.status == 404 || answer.status == 405) {
        *error = std::string{"Pairing needs RomM "} + kMinimumVersion + " or newer";
        return false;
    }
    if ((answer.status != 200 && answer.status != 201) || !made.is_object() || Text(made, "device_code").empty()) {
        *error = Detail(*client, answer, "/api/auth");
        return false;
    }
    start->device_code = Text(made, "device_code");
    start->user_code = Text(made, "user_code");
    start->address = client->url() + Text(made, "verification_path_complete");
    // RomM's default when a server does not say (ten minutes).
    start->expires_in = Number(made, "expires_in") > 0 ? static_cast<int>(Number(made, "expires_in")) : 600;
    start->interval = std::max(1, static_cast<int>(Number(made, "interval")));
    return true;
}

PairingResult PairPoll(const nlohmann::json& settings, const std::string& folder, const PairingStart& start,
                       const Stopped& stopped) {
    PairingResult result;
    Json entry = settings.is_object() ? settings : Json::object();
    entry.erase("token");
    entry.erase("username");
    entry.erase("password");
    std::unique_ptr<Client> client = Client::Make(entry, "save-sync.json", &result.error);
    if (!client) return result;
    Client::Answer answer;
    if (!client->Send("POST", "/api/auth/device/token", Json{{"device_code", start.device_code}}.dump(), &answer,
                      &result.error, stopped))
        return result;
    const Json body = Json::parse(answer.body, nullptr, false);
    if (answer.status == 400) {
        const std::string detail = body.is_object() ? Text(body, "detail") : std::string{};
        if (detail == "authorization_pending" || detail == "slow_down") {
            result.state = PairingState::pending;
            // slow_down: 5 s more than the interval used so far (start.interval: the poller's own).
            result.interval = start.interval + (detail == "slow_down" ? 5 : 0);
        } else if (detail == "access_denied") {
            result.state = PairingState::denied;
        } else if (detail == "expired_token") {
            result.state = PairingState::expired;
        } else {
            result.error = Detail(*client, answer, "/api/auth");
        }
        return result;
    }
    if (answer.status != 200 || !body.is_object() || Text(body, "access_token").empty()) {
        result.error = Detail(*client, answer, "/api/auth");
        return result;
    }
    // Signed in: the token is bound to the device RomM made for this console, which the save sync
    // then is (RommSaves::Device finds it kept), and says whose it is.
    const std::string token = Text(body, "access_token");
    entry["token"] = token;
    std::string ignored;
    std::unique_ptr<Client> signed_in = Client::Make(entry, "save-sync.json", &ignored);
    Client::Answer me;
    if (signed_in && signed_in->Send(nullptr, "/api/users/me", {}, &me, &ignored, stopped) && me.status == 200) {
        const Json user = Json::parse(me.body, nullptr, false);
        result.user = user.is_object() ? Text(user, "username") : std::string{};
    }
    (void)WriteAll(folder + "/romm-device.json",
                   Json{{"url", client->url()}, {"user", result.user}, {"device_id", Text(body, "device_id")}}.dump(2) +
                       "\n");
    result.state = PairingState::approved;
    result.entry = {{"type", "romm"}, {"url", client->url()}, {"token", token}};
    if (entry.contains("platform")) result.entry["platform"] = entry["platform"]; // the source's, if it names one
    return result;
}

} // namespace

const Pairing kPairing{PairStart, PairPoll};

std::unique_ptr<SaveStore> MakeSaves(const nlohmann::json& settings, const std::string& folder, std::string* error) {
    std::unique_ptr<Client> client = Client::Make(settings, "save-sync.json", error);
    if (!client) return nullptr;
    if (client->authorization().empty()) {
        *error = "No sign-in (\"token\", or \"username\" and \"password\") in save-sync.json";
        return nullptr;
    }
    return std::make_unique<RommSaves>(std::move(client), folder);
}

std::int64_t ParseTime(const std::string& text) {
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    int used = 0;
    if (std::sscanf(text.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d%n", &year, &month, &day, &hour, &minute, &second, &used) != 6)
        return 0;
    std::size_t at = static_cast<std::size_t>(used);
    if (at < text.size() && text[at] == '.')
        for (++at; at < text.size() && std::isdigit(static_cast<unsigned char>(text[at]));) ++at;
    std::int64_t offset = 0;
    if (at < text.size() && (text[at] == '+' || text[at] == '-')) {
        int hours = 0, minutes = 0;
        if (std::sscanf(text.c_str() + at + 1, "%2d:%2d", &hours, &minutes) != 2) return 0;
        offset = (text[at] == '+' ? 1 : -1) * (hours * 3600 + minutes * 60);
    }
    // Days since 1970 of a date (Howard Hinnant's days_from_civil).
    const int y = year - (month <= 2);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const int year_of_era = y - era * 400;
    const int day_of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const int day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    const std::int64_t days = static_cast<std::int64_t>(era) * 146097 + day_of_era - 719468;
    return days * 86400 + hour * 3600 + minute * 60 + second - offset;
}

std::string FormatTime(std::int64_t seconds) {
    const std::time_t time = static_cast<std::time_t>(seconds);
    std::tm utc{};
    gmtime_r(&time, &utc);
    char text[32];
    std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return text;
}

bool CanSync(const std::string& version) {
    int have[3] = {};
    int need[3] = {};
    if (std::sscanf(version.c_str(), "%d.%d.%d", &have[0], &have[1], &have[2]) < 2) return true;
    (void)std::sscanf(kMinimumVersion, "%d.%d.%d", &need[0], &need[1], &need[2]);
    return std::lexicographical_compare(need, need + 3, have, have + 3) ||
           std::equal(need, need + 3, have);
}

} // namespace Eden::Remote::Romm
