/*
 * ProsperoEden - resident/one-shot Lapy client regression.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdarg>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "elevation.cpp"

namespace test
{
struct State
{
    bool result_open_fails{};
    bool prepare_fails{};
    bool request_open_fails{};
    bool corrupt_read{};
    bool resident_claimed{};
    unsigned data_open_failures{};
    unsigned data_open_calls{};
    unsigned sleeps{};
    unsigned prepare_calls{};
    unsigned socket_calls{};
    unsigned closes{};
    bool helper_read{};
    std::string request;
    std::string result;
    std::string data;
    std::vector<std::uint8_t> replies;
    std::vector<std::uint8_t> sent;
    std::size_t received{};
};

State state;

void reset()
{
    state = {};
}

bool starts_with(const char *value, std::string_view prefix)
{
    return std::string_view{value}.starts_with(prefix);
}

void helper_replies(elevation::Status status = elevation::Status::ok)
{
    elevation::wire::Message prepare{};
    prepare.pid = 4242;
    prepare.kind = elevation::wire::Kind::prepare;
    auto response = prepare;
    response.kind = elevation::wire::Kind::response;
    response.status = status;
    state.replies.resize(2 * sizeof(prepare));
    std::memcpy(state.replies.data(), &prepare, sizeof(prepare));
    std::memcpy(state.replies.data() + sizeof(prepare), &response, sizeof(response));
}
} // namespace test

extern "C"
{
    pid_t getpid() noexcept
    {
        return 4242;
    }

    uid_t geteuid() noexcept
    {
        return 1000;
    }

    int seteuid(uid_t) noexcept
    {
        ++test::state.prepare_calls;
        if (test::state.prepare_fails)
        {
            errno = EPERM;
            return -1;
        }
        return 0;
    }

    int open(const char *path, int flags, ...)
    {
        (void)flags;
        if (std::strcmp(path, "/download0/lapy_owned_result") == 0)
        {
            if (test::state.result_open_fails)
            {
                errno = ENOENT;
                return -1;
            }
            return 10;
        }
        if (test::starts_with(path, "/download0/.elevate_proc."))
        {
            if (test::state.request_open_fails)
            {
                errno = EIO;
                return -1;
            }
            return 11;
        }
        if (test::starts_with(path, "/data/.lapy_probe_"))
        {
            ++test::state.data_open_calls;
            if (test::state.data_open_calls <= test::state.data_open_failures)
            {
                errno = EACCES;
                return -1;
            }
            return 12;
        }
        if (std::strcmp(path, "/app0/lapy.elf") == 0)
            return 13;
        errno = ENOENT;
        return -1;
    }

    ssize_t write(int descriptor, const void *buffer, size_t size)
    {
        const auto count = std::min(size, std::size_t{3});
        const auto bytes = std::string_view{static_cast<const char *>(buffer), count};
        if (descriptor == 10)
            test::state.result.append(bytes);
        else if (descriptor == 11)
            test::state.request.append(bytes);
        else if (descriptor == 12)
            test::state.data.append(bytes);
        else
        {
            errno = EBADF;
            return -1;
        }
        return static_cast<ssize_t>(count);
    }

    ssize_t read(int descriptor, void *buffer, size_t size)
    {
        if (descriptor == 13)
        {
            if (test::state.helper_read)
                return 0;
            test::state.helper_read = true;
            const char elf[] = "\x7f" "ELF";
            const auto count = std::min(size, sizeof(elf) - 1);
            std::memcpy(buffer, elf, count);
            return static_cast<ssize_t>(count);
        }
        if (descriptor != 12)
        {
            errno = EBADF;
            return -1;
        }
        const auto count = std::min(size, test::state.data.size());
        std::memcpy(buffer, test::state.data.data(), count);
        if (test::state.corrupt_read && count != 0)
            static_cast<char *>(buffer)[0] = 'X';
        return static_cast<ssize_t>(count);
    }

    off_t lseek(int descriptor, off_t offset, int whence) noexcept
    {
        return descriptor == 12 && offset == 0 && whence == SEEK_SET ? 0 : -1;
    }

    int close(int)
    {
        ++test::state.closes;
        return 0;
    }

    int fchmod(int descriptor, mode_t mode) noexcept
    {
        return descriptor == 10 && mode == 0644 ? 0 : -1;
    }

    int rename(const char *, const char *) noexcept
    {
        return 0;
    }

    int unlink(const char *path) noexcept
    {
        if (std::strcmp(path, "/download0/elevate_proc") == 0 && test::state.resident_claimed)
        {
            errno = ENOENT;
            return -1;
        }
        return 0;
    }

    int usleep(useconds_t)
    {
        ++test::state.sleeps;
        return 0;
    }

    int sceNetSocket(const char *, int, int, int)
    {
        ++test::state.socket_calls;
        return 20;
    }

    int sceNetSocketClose(int)
    {
        ++test::state.closes;
        return 0;
    }

    int sceNetConnect(int, const void *address, std::uint32_t length)
    {
        assert(length == sizeof(NetSockaddrIn));
        const auto &endpoint = *static_cast<const NetSockaddrIn *>(address);
        assert(endpoint.address == 0x0100007f && endpoint.port == 0x3d23);
        return 0;
    }

    int sceNetSetsockopt(int, int, int, const void *, std::uint32_t)
    {
        return 0;
    }

    int sceNetSend(int, const void *bytes, std::size_t size, int)
    {
        const auto count = std::min(size, std::size_t{7});
        const auto *start = static_cast<const std::uint8_t *>(bytes);
        test::state.sent.insert(test::state.sent.end(), start, start + count);
        return static_cast<int>(count);
    }

    int sceNetRecv(int, void *bytes, std::size_t size, int)
    {
        const auto remaining = test::state.replies.size() - test::state.received;
        if (remaining == 0)
            return -1;
        const auto count = std::min({size, std::size_t{3}, remaining});
        std::memcpy(bytes, test::state.replies.data() + test::state.received, count);
        test::state.received += count;
        return static_cast<int>(count);
    }
}

int main()
{
    using elevation::Capability;
    using elevation::Status;

    test::reset();
    assert(elevation::request(Capability::filesystem) == Status::ok);
    assert(std::strcmp(elevation::path(), "existing") == 0);
    assert(test::state.prepare_calls == 0 && test::state.socket_calls == 0);

    test::reset();
    test::state.data_open_failures = 1;
    assert(elevation::request(Capability::filesystem) == Status::ok);
    assert(std::strcmp(elevation::path(), "resident") == 0);
    assert(test::state.prepare_calls == 1 && test::state.socket_calls == 0);
    assert(test::state.request == "{\"PID\":4242}\n");
    assert(test::state.result == "DATA_OK=1 OPEN_ERRNO=0\n");

    test::reset();
    test::state.data_open_failures = 51;
    test::helper_replies();
    assert(elevation::request(Capability::filesystem) == Status::ok);
    assert(std::strcmp(elevation::path(), "helper") == 0);
    assert(test::state.prepare_calls == 2 && test::state.socket_calls == 1);
    assert(test::state.sent.size() == 4 + 2 * sizeof(elevation::wire::Message));

    test::reset();
    test::state.data_open_failures = 1000;
    test::state.resident_claimed = true;
    assert(elevation::request(Capability::filesystem) == Status::timeout);
    assert(std::strcmp(elevation::path(), "resident") == 0);
    assert(test::state.socket_calls == 0);

    test::reset();
    test::state.data_open_failures = 1;
    test::state.result_open_fails = true;
    test::helper_replies(Status::target_mismatch);
    assert(elevation::request(Capability::filesystem) == Status::target_mismatch);
    assert(test::state.socket_calls == 1 && test::state.prepare_calls == 1);

    test::reset();
    test::state.data_open_failures = 1;
    test::state.prepare_fails = true;
    assert(elevation::request(Capability::filesystem) == Status::prepare_failed);
    assert(test::state.socket_calls == 0);

    test::reset();
    test::state.corrupt_read = true;
    assert(elevation::request(Capability::filesystem) == Status::apply_failed);
    assert(test::state.socket_calls == 0);

    test::reset();
    assert(elevation::request(static_cast<Capability>(2)) == Status::unsupported_capability);
    assert(test::state.data_open_calls == 0 && test::state.socket_calls == 0);

    test::reset();
    test::state.data_open_failures = 1;
    test::state.result_open_fails = true;
    assert(elevation::request(Capability::filesystem, nullptr) == Status::invalid_request);

    elevation::wire::Message request{};
    request.pid = 4242;
    const std::array<std::uint8_t, 24> golden{'E', 'L', 'V', '1', 1, 0, 24, 0, 1, 0, 0, 0,
                                              1, 0, 0, 0, 0x92, 0x10, 0, 0, 0, 0, 0, 0};
    assert(std::memcmp(&request, golden.data(), golden.size()) == 0);
}
