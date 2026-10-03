/*
 * ProsperoEden - Lapy cooperative client regression.
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
    unsigned data_open_failures{};
    unsigned data_open_calls{};
    unsigned sleeps{};
    unsigned prepare_calls{};
    unsigned closes{};
    std::string request;
    std::string result;
    std::string data;
    std::vector<std::string> unlinked;
    std::string renamed_from;
    std::string renamed_to;
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

    int rename(const char *old_path, const char *new_path) noexcept
    {
        test::state.renamed_from = old_path;
        test::state.renamed_to = new_path;
        return 0;
    }

    int unlink(const char *path) noexcept
    {
        test::state.unlinked.emplace_back(path);
        return 0;
    }

    int usleep(useconds_t)
    {
        ++test::state.sleeps;
        return 0;
    }
}

int main()
{
    using elevation::Capability;
    using elevation::Status;

    test::reset();
    test::state.data_open_failures = 1;
    assert(elevation::request(Capability::filesystem) == Status::ok);
    assert(test::state.prepare_calls == 1);
    assert(test::state.request == "{\"PID\":4242}\n");
    assert(test::state.renamed_from == "/download0/.elevate_proc.4242");
    assert(test::state.renamed_to == "/download0/elevate_proc");
    assert(test::state.data == "LAPYOWN\n");
    assert(test::state.result == "DATA_OK=1 OPEN_ERRNO=0\n");
    assert(test::state.data_open_calls == 2 && test::state.sleeps == 1);

    test::reset();
    assert(elevation::request(static_cast<Capability>(2)) == Status::unsupported_capability);
    assert(test::state.prepare_calls == 0 && test::state.closes == 0);

    test::reset();
    test::state.result_open_fails = true;
    assert(elevation::request(Capability::filesystem) == Status::unavailable);

    test::reset();
    test::state.prepare_fails = true;
    assert(elevation::request(Capability::filesystem) == Status::prepare_failed);
    assert(test::state.closes == 1);

    test::reset();
    test::state.request_open_fails = true;
    assert(elevation::request(Capability::filesystem) == Status::transport_error);

    test::reset();
    test::state.data_open_failures = 1000;
    assert(elevation::request(Capability::filesystem) == Status::timeout);
    assert(test::state.data_open_calls == 200 && test::state.sleeps == 200);
    assert(test::state.result == "DATA_OK=0 OPEN_ERRNO=" + std::to_string(EACCES) + "\n");

    test::reset();
    test::state.corrupt_read = true;
    assert(elevation::request(Capability::filesystem) == Status::apply_failed);
    assert(test::state.result == "DATA_OK=0 OPEN_ERRNO=0\n");
}


