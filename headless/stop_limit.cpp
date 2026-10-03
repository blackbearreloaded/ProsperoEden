// SPDX-License-Identifier: GPL-3.0-or-later
#include "stop_limit.h"
#include <condition_variable>
#include <fcntl.h>
#include <mutex>
#include <thread>
#include <unistd.h>
#include <utility>

#ifdef PS5_NATIVE
extern "C" int eden_restart_app(void);
extern "C" int sceKernelDebugOutText(int, const char*);
#endif

namespace Eden::StopLimit {
namespace {
struct State {
    std::mutex mutex;
    std::condition_variable wake;
    bool watching = false;
    bool armed = false;
    std::chrono::steady_clock::time_point deadline;
    std::chrono::milliseconds limit = kLimit;
    void (*at_limit)() noexcept = nullptr;
    std::string note;
};
// Never destroyed: the watcher is a detached thread and must not meet a destroyed mutex at exit.
State& state() {
    static State* const instance = new State;
    return *instance;
}

void Watch() {
    State& s = state();
    std::unique_lock lock(s.mutex);
    for (;;) {
        if (!s.armed) {
            s.wake.wait(lock);
            continue;
        }
        const auto deadline = s.deadline;
        if (s.wake.wait_until(lock, deadline) != std::cv_status::timeout) continue;
        // Timed out: still the same stop?
        if (!s.armed || s.deadline != deadline) continue;
        s.armed = false;
        const auto action = s.at_limit ? s.at_limit : &RestartNow;
        lock.unlock();
        action(); // on the console this returns only when the system refuses the restart
        lock.lock();
    }
}
} // namespace

void Start(std::string note) noexcept {
    State& s = state();
    std::lock_guard lock(s.mutex);
    s.note = std::move(note);
    if (s.watching) return;
    try {
        std::thread(Watch).detach();
        s.watching = true;
    } catch (...) {
        // No watcher: a stop then takes as long as it takes, as before.
    }
}

void Begin() noexcept {
    State& s = state();
    std::lock_guard lock(s.mutex);
    if (s.armed) return;
    s.armed = true;
    s.deadline = std::chrono::steady_clock::now() + s.limit;
    s.wake.notify_all();
}

void End() noexcept {
    State& s = state();
    std::lock_guard lock(s.mutex);
    s.armed = false;
    s.wake.notify_all();
}

void RestartNow() noexcept {
#ifdef PS5_NATIVE
    // No stdio here: a thread that is stuck in the stop may hold one of its locks.
    sceKernelDebugOutText(0, "[ProsperoEden] shutdown: The game did not stop in time; starting ProsperoEden again\n");
    const std::string& note = state().note; // set once, before any game
    if (!note.empty()) {
        const int fd = open(note.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd >= 0) {
            (void)!write(fd, "1\n", 2);
            close(fd);
        }
    }
    eden_restart_app();
#endif
}

void Configure(void (*at_limit)() noexcept, std::chrono::milliseconds limit) noexcept {
    State& s = state();
    std::lock_guard lock(s.mutex);
    s.at_limit = at_limit;
    s.limit = limit;
}
} // namespace Eden::StopLimit
