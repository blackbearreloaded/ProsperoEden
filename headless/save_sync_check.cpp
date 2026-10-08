// SPDX-License-Identifier: GPL-3.0-or-later
// Host check of save-sync.json (save_sync_config.h): made with an empty entry for each profile,
// kept in step with the profiles (added, renamed, removed, in their order) without touching what
// the player wrote, written only when something changed, and left alone when it is no JSON.
#include "save_sync_config.h"
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace {
std::string Contents(const std::string& file) {
    std::ifstream in(file);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}
} // namespace

int main() {
    using namespace Eden::SaveSync;
    char folder[] = "/tmp/eden-save-sync-XXXXXX";
    assert(mkdtemp(folder));
    const std::string file = std::string(folder) + "/config/remote/save-sync.json";
    const std::string one = "1F1E1D1C1B1A19181716151413121110";
    const std::string two = "AFAEADACABAAA9A8A7A6A5A4A3A2A1A0";
    const std::string three = "000102030405060708090A0B0C0D0E0F";

    // No file: nothing is set up, and that is readable.
    Config config = Read(file);
    assert(config.readable && config.automatic && config.entries.empty());

    // Made, with its folder, an empty entry for each profile.
    assert(Reconcile({{one, "Player 1"}, {two, "Kids"}}, file));
    config = Read(file);
    assert(config.readable && config.automatic && config.entries.size() == 2);
    assert(config.entries[0].profile == one && config.entries[0].profile_name == "Player 1" &&
           config.entries[0].type.empty() && config.entries[0].settings.contains("url") &&
           config.entries[0].settings.contains("token"));
    assert(config.entries[1].profile == two && config.entries[1].profile_name == "Kids");
    // The fields of an entry in the order a reader expects them.
    const std::string made = Contents(file);
    assert(made.find("\"profile\"") < made.find("\"__profile_name\"") && made.find("\"__profile_name\"") < made.find("\"type\"") &&
           made.find("\"type\"") < made.find("\"url\"") && made.find("\"url\"") < made.find("\"token\""));

    // The player fills one in, with fields of its own; the app's next look changes nothing.
    {
        Json document = Json::parse(made);
        document["auto"] = false;
        document["note"] = "mine";
        document["profiles"][1]["type"] = "romm";
        document["profiles"][1]["url"] = "http://nas:3000";
        document["profiles"][1]["token"] = "";
        document["profiles"][1]["username"] = "kids";
        document["profiles"][1]["password"] = "secret";
        std::ofstream(file) << document.dump(4);
    }
    const auto written = std::filesystem::last_write_time(file);
    const std::string filled = Contents(file);
    assert(Reconcile({{one, "Player 1"}, {two, "Kids"}}, file));
    assert(Contents(file) == filled && std::filesystem::last_write_time(file) == written);
    config = Read(file);
    assert(!config.automatic && config.entries[1].type == "romm" &&
           config.entries[1].settings["username"] == "kids" && config.entries[1].settings["password"] == "secret");
    const Entry* kids = Find(config, two);
    assert(kids && kids->settings["url"] == "http://nas:3000" && Find(config, three) == nullptr);

    // Renamed, a profile added, the profiles in another order: names written, the new profile's
    // entry empty, the entries in the profiles' order, the player's fields kept.
    assert(Reconcile({{two, "Children"}, {one, "Player 1"}, {three, "Guest"}}, file));
    config = Read(file);
    assert(config.entries.size() == 3 && config.entries[0].profile == two && config.entries[0].profile_name == "Children" &&
           config.entries[0].settings["username"] == "kids" && config.entries[1].profile == one &&
           config.entries[2].profile == three && config.entries[2].type.empty());
    assert(Json::parse(Contents(file))["note"] == "mine" && !config.automatic);

    // A profile removed: its entry goes; a second entry of the same profile goes too, the first
    // one counts.
    {
        Json document = Json::parse(Contents(file));
        Json twin = document["profiles"][0];
        twin["username"] = "someone else";
        document["profiles"].push_back(twin);
        std::ofstream(file) << document.dump(2);
    }
    config = Read(file);
    assert(config.entries.size() == 3 && Find(config, two)->settings["username"] == "kids");
    assert(Reconcile({{two, "Children"}, {three, "Guest"}}, file));
    config = Read(file);
    const Json reconciled = Json::parse(Contents(file));
    assert(config.entries.size() == 2 && reconciled["profiles"].size() == 2 &&
           config.entries[0].settings["username"] == "kids" && config.entries[1].profile == three);

    // Entries the file cannot tell apart are not entries.
    {
        Json document = reconciled;
        document["profiles"].push_back(Json{{"type", "romm"}});
        document["profiles"].push_back("text");
        std::ofstream(file) << document.dump(2);
    }
    assert(Read(file).entries.size() == 2);

    // Not JSON (a typing error over FTP): left as it is, and nothing syncs.
    { std::ofstream(file) << "{ \"profiles\": [ { \"profile\": "; }
    const std::string broken = Contents(file);
    config = Read(file);
    assert(!config.readable && !config.error.empty() && config.entries.empty());
    assert(!Reconcile({{two, "Children"}}, file) && Contents(file) == broken);
    // "profiles" that is no list is not the app's to replace either.
    { std::ofstream(file) << "{ \"profiles\": {} }"; }
    assert(!Read(file).readable && !Reconcile({{two, "Children"}}, file) && Contents(file) == "{ \"profiles\": {} }");

    std::filesystem::remove_all(folder);
    std::puts("save sync check: PASS");
    return 0;
}
