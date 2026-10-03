// SPDX-License-Identifier: GPL-3.0-or-later
// Host check of the update check's decision with this app's version numbers
// (update_check/update_check.h, built without its network side).
#include "update_check/update_check.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

namespace {
update_check_result Decide(const std::string& json, const char* installed) {
    update_check_result result{};
    update_check_evaluate(json.data(), json.size(), installed, &result);
    return result;
}

std::string Answer(const char* status, const char* content_version, const char* version) {
    std::string json = std::string("{\"title_id\":\"PPSA99008\",\"status\":\"") + status + "\",\"content_version\":";
    json += content_version ? std::string("\"") + content_version + "\"" : std::string("null");
    json += std::string(",\"version\":\"") + version + "\",\"page\":\"https://homebrew.page/ps5/prosperoeden\"}";
    return json;
}
} // namespace

int main() {
    // The app's content version (sce_sys/param.json) is NN.NNN.NNN; a release's name is not one.
    unsigned parts[3];
    assert(update_check_version_parse("01.000.040", parts) == 1 && parts[0] == 1 && parts[1] == 0 && parts[2] == 40);
    assert(update_check_version_parse("v1.000.040", parts) == 0);

    // The request names the title and nothing else.
    char url[96];
    assert(update_check_url(url, sizeof(url), "PPSA99008") != 0);
    assert(std::strcmp(url, "https://homebrew.page/api/v1/apps/PPSA99008.json") == 0);
    assert(update_check_url(url, sizeof(url), "../PPSA99008") == 0);

    // A newer release is listed: its name is what the notification shows.
    update_check_result result = Decide(Answer("available", "01.000.050", "v1.000.050"), "01.000.040");
    assert(result.state == UPDATE_CHECK_AVAILABLE && std::strcmp(result.version, "v1.000.050") == 0);
    assert(std::strcmp(result.available, "01.000.050") == 0 && std::strcmp(result.installed, "01.000.040") == 0);
    // The numbers compare as numbers, field by field.
    assert(Decide(Answer("available", "01.001.000", "v1.001.000"), "01.000.990").state == UPDATE_CHECK_AVAILABLE);
    assert(Decide(Answer("available", "02.000.000", "v2.000.000"), "01.999.999").state == UPDATE_CHECK_AVAILABLE);

    // The same release, or an older one than this build: nothing to say.
    assert(Decide(Answer("available", "01.000.040", "v1.000.040"), "01.000.040").state == UPDATE_CHECK_UP_TO_DATE);
    assert(Decide(Answer("available", "01.000.040", "v1.000.040"), "01.000.050").state == UPDATE_CHECK_UP_TO_DATE);

    // Anything else is silence: the catalog knows no content version (a release from before the
    // app carried one), the app is not released there, or the answer is not the documented one.
    result = Decide(Answer("available", nullptr, "v1.000.040"), "01.000.040");
    assert(result.state == UPDATE_CHECK_UNKNOWN && result.reason == UPDATE_CHECK_NO_CATALOG_VERSION);
    result = Decide(Answer("coming_soon", "01.000.050", "v1.000.050"), "01.000.040");
    assert(result.state == UPDATE_CHECK_UNKNOWN && result.reason == UPDATE_CHECK_NOT_AVAILABLE);
    result = Decide(Answer("available", "1.0.50", "v1.000.050"), "01.000.040");
    assert(result.state == UPDATE_CHECK_UNKNOWN && result.reason == UPDATE_CHECK_BAD_RESPONSE);
    result = Decide("<html>not found</html>", "01.000.040");
    assert(result.state == UPDATE_CHECK_UNKNOWN && result.reason == UPDATE_CHECK_BAD_RESPONSE);
    result = Decide(Answer("available", "01.000.050", "v1.000.050"), "development");
    assert(result.state == UPDATE_CHECK_UNKNOWN && result.reason == UPDATE_CHECK_BAD_INSTALLED_VERSION);

    std::puts("update check: versions, request address, newer/same/older and silent answers PASS");
    return 0;
}
