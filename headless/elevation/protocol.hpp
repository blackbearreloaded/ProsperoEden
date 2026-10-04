/*
 * ProsperoEden - Upstream Lapy one-shot helper protocol.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>

#include "elevation.hpp"

namespace elevation::wire
{
inline constexpr int io_timeout_us = 5'000'000;

enum class Kind : std::uint32_t
{
    request = 1,
    prepare = 2,
    prepared = 3,
    response = 4,
};

struct Message
{
    std::uint32_t magic = 0x31564c45;
    std::uint16_t version = 1;
    std::uint16_t size = 24;
    Kind kind = Kind::request;
    Capability capability = Capability::filesystem;
    std::uint32_t pid = 0;
    Status status = Status::ok;
};
static_assert(sizeof(Message) == 24);
static_assert(std::endian::native == std::endian::little);

constexpr Status validate(const Message &message) noexcept
{
    if (message.magic != Message{}.magic || message.size != sizeof(Message))
        return Status::invalid_request;
    if (message.version != Message{}.version)
        return Status::unsupported_version;
    if (message.kind < Kind::request || message.kind > Kind::response || message.pid <= 1 ||
        message.pid > INT32_MAX || message.status > Status::protocol_error)
        return Status::invalid_request;
    if (message.capability != Capability::filesystem)
        return Status::unsupported_capability;
    return Status::ok;
}

constexpr bool matches(const Message &message, const Message &request, Kind kind) noexcept
{
    return validate(message) == Status::ok && message.kind == kind && message.pid == request.pid &&
           message.capability == request.capability;
}

template <typename Byte, typename Operation>
bool transfer(Byte *bytes, std::size_t size, Operation operation) noexcept
{
    while (size != 0)
    {
        const auto count = operation(bytes, size);
        if (count <= 0 || static_cast<std::size_t>(count) > size)
            return false;
        bytes += count;
        size -= static_cast<std::size_t>(count);
    }
    return true;
}
} // namespace elevation::wire
