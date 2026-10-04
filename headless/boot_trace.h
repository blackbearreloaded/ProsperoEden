// SPDX-License-Identifier: GPL-3.0-or-later
// Diagnostic builds: a start-up trace for crashes reported by testers.
//
// Each line goes to the kernel log at once (prefix "[ProsperoEden diag]") and is kept in memory.
// Before filesystem access exists, the lines are also written to /download0/boot-trace.txt with a
// plain synchronous write: no thread is started there, because the elevation helper accepts only a
// single-threaded process. Once the logs folder exists (Ready), the whole trace so far is written to
// <logs>/boot-trace.txt and every later line is appended and synced, so a crash leaves the last step
// on disk; the previous run's /download0 trace is kept beside it as boot-trace.sandbox-prev.txt.
// A fatal signal before the crash handler is installed is written to the trace too.
#pragma once
#include <cerrno>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <string>
#include <unistd.h>

#include "diagnostics.h"

extern "C" int sysctlbyname(const char* name, void* old, size_t* old_length, const void* value, size_t length);

namespace Eden::BootTrace {
namespace detail {
inline std::string& Memory() {
    static std::string text;
    return text;
}
inline int& File() {
    static int fd = -1;
    return fd;
}
inline timespec& Start() {
    static timespec start = [] {
        timespec now{};
        clock_gettime(CLOCK_MONOTONIC, &now);
        return now;
    }();
    return start;
}
inline long Milliseconds() {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - Start().tv_sec) * 1000L + (now.tv_nsec - Start().tv_nsec) / 1000000L;
}
inline void WriteAll(int fd, const char* data, size_t length) {
    while (fd >= 0 && length > 0) {
        const ssize_t written = write(fd, data, length);
        if (written <= 0) {
            if (written < 0 && errno == EINTR) continue;
            return;
        }
        data += written;
        length -= static_cast<size_t>(written);
    }
}
inline void Emit(const char* line) {
#if defined(__PROSPERO__)
    (void)sceKernelDebugOutText(0, line);
#endif
}
// Before the crash handler: a fatal signal names itself in the trace and the kernel log, then the
// default action runs (the system's crash report). Only async-signal-safe calls.
inline void OnFatal(int signal, siginfo_t* info, void*) {
    char line[160];
    const int length = std::snprintf(line, sizeof(line), "[ProsperoEden diag] +%ldms FATAL signal %d at %p (before the crash handler)\n",
                                     Milliseconds(), signal, info ? info->si_addr : nullptr);
    Emit(line);
    if (length > 0) WriteAll(File(), line, static_cast<size_t>(length));
    if (File() >= 0) (void)fsync(File());
    std::signal(signal, SIG_DFL);
    (void)raise(signal);
}
} // namespace detail

// One step: "+123ms text".
inline void Line(const char* format, ...) {
    char body[700];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(body, sizeof(body), format, arguments);
    va_end(arguments);
    char line[800];
    const int length = std::snprintf(line, sizeof(line), "[ProsperoEden diag] +%ldms %s\n", detail::Milliseconds(), body);
    if (length <= 0) return;
    detail::Emit(line);
    detail::Memory().append(line, static_cast<size_t>(length) < sizeof(line) ? static_cast<size_t>(length) : sizeof(line) - 1);
    if (detail::File() >= 0) {
        detail::WriteAll(detail::File(), line, static_cast<size_t>(length));
        (void)fsync(detail::File());
    }
}

// The first lines, before anything else runs: build, firmware, process.
inline void Begin(const char* version, const char* build) {
    (void)detail::Start();
    unsigned int sdk = 0;
    size_t size = sizeof(sdk);
    const bool known = sysctlbyname("kern.sdk_version", &sdk, &size, nullptr, 0) == 0;
    struct sigaction action{};
    action.sa_sigaction = detail::OnFatal;
    action.sa_flags = SA_SIGINFO;
    for (int signal : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT}) (void)sigaction(signal, &action, nullptr);
    // The sandbox's own folder: written synchronously, no thread. The previous run's trace is kept.
    (void)std::rename("/download0/boot-trace.txt", "/download0/boot-trace.prev.txt");
    detail::File() = open("/download0/boot-trace.txt", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    Line("diagnostic build %s (%s), firmware sdk_version %s0x%08x, pid %d, uid %d/%d", version, build,
         known ? "" : "unknown ", sdk, static_cast<int>(getpid()), static_cast<int>(getuid()), static_cast<int>(geteuid()));
}

// The logs folder exists: the trace moves there and stays open, synced after every line.
inline void Ready(const std::string& logs_folder, bool filesystem_access) {
    if (!filesystem_access) {
        Line("no filesystem access: the trace stays in /download0 (only the kernel log can be collected)");
        return;
    }
    const std::string path = logs_folder + "/boot-trace.txt";
    (void)std::rename(path.c_str(), (logs_folder + "/boot-trace.prev.txt").c_str());
    // The last run's sandbox trace (that run may have died before it had filesystem access), copied
    // where it can be fetched; with filesystem access the sandbox is seen under /mnt/sandbox.
    if (int previous = open("/mnt/sandbox/PPSA99008_000/download0/boot-trace.prev.txt", O_RDONLY); previous >= 0) {
        const int copy = open((logs_folder + "/boot-trace.sandbox-prev.txt").c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
        char buffer[4096];
        for (ssize_t got; (got = read(previous, buffer, sizeof(buffer))) > 0;) detail::WriteAll(copy, buffer, static_cast<size_t>(got));
        if (copy >= 0) close(copy);
        close(previous);
    }
    const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        Line("could not open %s: %s", path.c_str(), std::strerror(errno));
        return;
    }
    if (detail::File() >= 0) close(detail::File());
    detail::File() = fd;
    detail::WriteAll(fd, detail::Memory().data(), detail::Memory().size());
    (void)fsync(fd);
    Line("trace file %s", path.c_str());
}
} // namespace Eden::BootTrace
