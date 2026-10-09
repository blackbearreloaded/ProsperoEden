// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <chrono>
#include <cstdio>
#include <string>
#if defined(__PROSPERO__)
extern "C" int sceKernelDebugOutText(int, const char*);
#endif
namespace Eden {
// Only concise lifecycle/error text belongs here, never key or ticket contents.
inline void Report(const char* stage, const char* message) {
    char line[1024];
    std::snprintf(line, sizeof(line), "[ProsperoEden] %s: %.900s\n", stage, message);
    std::fputs(line, stderr);
#if defined(__PROSPERO__)
    // Written out within a second, or at once when so set (log_flusher.h); flushing here would
    // wait for the storage.
    (void)sceKernelDebugOutText(0, line);
#else
    std::fflush(stderr);
#endif
}
// A shutdown step with the milliseconds since the previous one (some games take 14-16 s).
inline void ReportStep(const char* stage, const char* message) {
    using Clock = std::chrono::steady_clock;
    static Clock::time_point previous = Clock::now();
    const auto now = Clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - previous).count();
    previous = now;
    Report(stage, (std::string(message) + " (+" + std::to_string(ms) + " ms)").c_str());
}
}
