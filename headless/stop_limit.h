// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <chrono>
#include <string>

// A limit on how long a game may take to stop, counted from the moment the player asks to leave
// (Touchpad + L1, also while the game is still loading) or the game ends by itself.
//
// Stopping a game normally takes about a second. Some games take minutes: the emulator waits for
// guest threads or services that never finish. A player who asked to leave should not sit through
// that, so a stop that is still going at the limit ends the other way: the app asks the PS5 to
// start it again (src/lifecycle.c, eden_restart_app), which ends this process and opens a fresh
// one at the launcher. The game loses nothing by it: its save is on disk once the game has
// committed it, and it commits while it runs, not while it is being stopped.
namespace Eden::StopLimit {
inline constexpr std::chrono::seconds kLimit{10};

// Once, at start, before any game: starts the watcher (on the caller's scheduling, not a pinned
// game thread's). `note` is a file the watcher leaves when it acts; the next process finds it.
void Start(std::string note) noexcept;
// A game's stop began: the shortcut was pressed, the game ended itself, or its session failed.
// A second call during the same stop keeps the first deadline.
void Begin() noexcept;
// The game is gone and the app carries on: called before every launcher and every next game.
void End() noexcept;
// What happens at the limit, at once (development check of the restart itself).
void RestartNow() noexcept;
// Checks only: another action at the limit, and another limit.
void Configure(void (*at_limit)() noexcept, std::chrono::milliseconds limit) noexcept;
} // namespace Eden::StopLimit
