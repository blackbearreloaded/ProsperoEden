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
    unsupported_version = 2,
    unsupported_capability = 3,
    target_mismatch = 4,
    unavailable = 5,
    prepare_failed = 6,
    apply_failed = 7,
    rollback_failed = 8,
    transport_error = 9,
    protocol_error = 10,
    timeout = 11,
};

// Call once during single-threaded startup. A resident upstream Lapy service
// gets the first bounded opportunity; otherwise the packaged upstream helper
// is sent to the local elfldr. Only ok permits /data use.
[[nodiscard]] Status request(Capability capability,
                             const char *helper_path = "/app0/lapy.elf") noexcept;
[[nodiscard]] const char *path() noexcept;
} // namespace elevation
