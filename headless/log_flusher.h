// SPDX-License-Identifier: GPL-3.0-or-later
// Log streams without storage stalls. On the console a small write to a file under /data takes
// ~25 ms or more, and the GL driver's batch summary line on the unbuffered stderr held up present
// for ~300 ms every 10000 draws. The streams' descriptors cannot be pointed at a pipe there
// (fcntl(F_DUPFD): EINVAL, dup2: EPERM), so both streams are fully buffered and a background
// thread writes them out once a second. A thread that prints only waits for the storage when it
// prints while that write is going on, or when a second's lines overfill the buffer. Lines still
// buffered when the process is killed are lost (a second's worth); the crash handler writes them
// out (crash_report.cpp).
#pragma once
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>

namespace Eden {
class LogFlusher {
public:
    LogFlusher() = default;
    LogFlusher(const LogFlusher&) = delete;
    LogFlusher& operator=(const LogFlusher&) = delete;
    ~LogFlusher() { Stop(); }

    // The streams must already write to their log files.
    bool Start() {
        static char stderr_buffer[64 * 1024], stdout_buffer[64 * 1024];
        if (worker.joinable()) return true;
        if (std::setvbuf(stderr, stderr_buffer, _IOFBF, sizeof(stderr_buffer)) != 0 ||
            std::setvbuf(stdout, stdout_buffer, _IOFBF, sizeof(stdout_buffer)) != 0) return false;
        worker = std::thread([this] { Run(); });
        return true;
    }

    // Writes out what is buffered; the streams stay buffered.
    void Stop() {
        if (!worker.joinable()) return;
        {
            std::lock_guard lock{mutex};
            stopping = true;
        }
        wake.notify_all();
        worker.join();
    }

private:
    void Run() {
        for (bool stop = false; !stop;) {
            {
                std::unique_lock lock{mutex};
                stop = wake.wait_for(lock, std::chrono::seconds(1), [this] { return stopping; });
            }
            std::fflush(stderr);
            std::fflush(stdout);
        }
    }

    std::mutex mutex;
    std::condition_variable wake;
    bool stopping = false;
    std::thread worker;
};
}
