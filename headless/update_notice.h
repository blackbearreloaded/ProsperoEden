// SPDX-License-Identifier: GPL-3.0-or-later
// The update check: once per launch the app asks the homebrew.page catalog whether a newer release
// of itself is listed, and the launcher shows the answer as a notification. The check is the
// PS5 Native App Boilerplate's (headless/update_check, copied from its examples/update-check at
// commit 1dbe974): one HTTPS GET of https://homebrew.page/api/v1/apps/<title ID>.json through
// libcurl and OpenSSL with the console's certificate list, and a comparison of the catalog's
// content version with this build's (sce_sys/param.json). It sends the title ID and nothing
// else, downloads nothing else and installs nothing. No answer, or one that cannot be used, is
// silence.
#pragma once
#include <string>

namespace Eden::UpdateNotice {
// Starts the check on a thread of its own; only the first call of a launch does anything.
void Start();
// The newer release's name ("1.000.050"), once: false when there is nothing (yet) to tell.
bool Take(std::string* version);
} // namespace Eden::UpdateNotice
