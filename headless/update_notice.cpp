// SPDX-License-Identifier: GPL-3.0-or-later
#include "update_notice.h"

#include <cstdio>
#include <mutex>
#include <pthread.h>
#include <string>

#include "diagnostics.h"
#include "storage_paths.h"
#include "update_check/update_check.h"
#ifdef EDEN_DEV_PROFILE
#include <fstream>
#include <iterator>
#endif

namespace Eden::UpdateNotice {
namespace {
// libcurl and OpenSSL want more stack than a default thread of the console has.
constexpr std::size_t kStackSize = 1024 * 1024;

std::mutex lock;
bool started = false;
bool found = false;  // a newer release nobody has been told about yet
std::string newer;

void* Check(void*) {
    char title[10]{};
    char installed[12]{};
    update_check_result result{};
    // The app's own title ID and content version: its param.json, which the package carries
    // (/app0 in the sandbox, the install folder with filesystem access).
    if (!update_check_read_param(AppFile("sce_sys/param.json").c_str(), title, installed)) {
        Report("update check", "The app's param.json could not be read");
        return nullptr;
    }
#ifdef EDEN_DEV_PROFILE
    // A development run can pretend to be another version: update-installed.txt in the app folder.
    if (std::ifstream pretend(AppFile("update-installed.txt")); pretend) {
        std::string text;
        pretend >> text;
        unsigned parts[3];
        if (update_check_version_parse(text.c_str(), parts) == 1)
            std::snprintf(installed, sizeof(installed), "%s", text.c_str());
    }
    // And it can be given the catalog's answer, to see the notification before the catalog lists
    // a newer release: update-answer.json in the app folder, read after the real request.
    std::string answer;
    if (std::ifstream pretend(AppFile("update-answer.json")); pretend)
        answer.assign(std::istreambuf_iterator<char>(pretend), std::istreambuf_iterator<char>());
#endif
    update_check_run(title, installed, &result);
#ifdef EDEN_DEV_PROFILE
    if (!answer.empty()) {
        char line[200];
        std::snprintf(line, sizeof(line), "real answer: state=%d (%s) http=%d error=%d; update-answer.json is used",
                      static_cast<int>(result.state), update_check_reason_text(result.reason), result.http_status,
                      result.platform_error);
        Report("update check", line);
        update_check_evaluate(answer.data(), answer.size(), installed, &result);
    }
#endif
    char line[256];
    std::snprintf(line, sizeof(line), "state=%d (%s) installed=%s catalog=%s version=%s http=%d error=%d",
                  static_cast<int>(result.state), update_check_reason_text(result.reason), result.installed,
                  result.available[0] ? result.available : "-", result.version[0] ? result.version : "-",
                  result.http_status, result.platform_error);
    Report("update check", line);
    if (result.state == UPDATE_CHECK_AVAILABLE) {
        const std::lock_guard guard(lock);
        newer = result.version[0] != '\0' ? result.version : result.available;
        found = true;
    }
    return nullptr;
}
} // namespace

void Start() {
    {
        const std::lock_guard guard(lock);
        if (started) return;
        started = true;
    }
    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes) != 0) return;
    pthread_attr_setstacksize(&attributes, kStackSize);
    pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
    pthread_t thread;
    if (pthread_create(&thread, &attributes, Check, nullptr) != 0)
        Report("update check", "The thread could not start");
    pthread_attr_destroy(&attributes);
}

bool Take(std::string* version) {
    const std::lock_guard guard(lock);
    if (!found) return false;
    found = false;
    if (version) *version = newer;
    return true;
}
} // namespace Eden::UpdateNotice
