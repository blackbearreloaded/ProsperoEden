/*
 * ProsperoEden - PS5-Lapy-JB-Daemon cooperative elevation client.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <cstdint>

namespace elevation
{
enum class Capability : std::uint32_t
{
    filesystem = 1,
};

enum class Status : std::uint32_t
{
    ok = 0,
    invalid_request = 1,
    unsupported_capability = 3,
    unavailable = 5,
    prepare_failed = 6,
    apply_failed = 7,
    transport_error = 9,
    timeout = 11,
};

// Call once during single-threaded startup while an official Lapy owned-root
// daemon is waiting for /download0/elevate_proc. Only ok permits /data use.
[[nodiscard]] Status request(Capability capability) noexcept;
} // namespace elevation
