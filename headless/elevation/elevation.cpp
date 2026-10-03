/*
 * ProsperoEden - PS5-Lapy-JB-Daemon cooperative elevation client.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Implements the application-side contract published by PS5-Lapy-JB-Daemon.
 * Lapy remains an external upstream component; ProsperoEden contains no
 * kernel-state manipulation or bundled elevation payload.
 */
#include "elevation.hpp"

#include <array>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
constexpr char request_path[] = "/download0/elevate_proc";
constexpr char result_path[] = "/download0/lapy_owned_result";
constexpr unsigned elevation_polls = 200;
constexpr useconds_t elevation_poll_interval_us = 50000;

class File
{
  public:
    explicit File(int descriptor = -1) noexcept : descriptor_{descriptor}
    {
    }
    ~File()
    {
        if (descriptor_ >= 0)
            (void)close(descriptor_);
    }
    File(const File &) = delete;
    File &operator=(const File &) = delete;
    [[nodiscard]] int get() const noexcept
    {
        return descriptor_;
    }
    int release() noexcept
    {
        const int descriptor = descriptor_;
        descriptor_ = -1;
        return descriptor;
    }

  private:
    int descriptor_;
};

bool write_all(int descriptor, const char *bytes, std::size_t length) noexcept
{
    while (length != 0)
    {
        const auto count = write(descriptor, bytes, length);
        if (count <= 0 || static_cast<std::size_t>(count) > length)
            return false;
        bytes += count;
        length -= static_cast<std::size_t>(count);
    }
    return true;
}

bool publish_request(pid_t pid) noexcept
{
    std::array<char, 80> temporary{};
    std::array<char, 64> body{};
    const int body_length =
        std::snprintf(body.data(), body.size(), "{\"PID\":%ld}\n", static_cast<long>(pid));
    const int path_length = std::snprintf(temporary.data(), temporary.size(),
                                          "/download0/.elevate_proc.%ld", static_cast<long>(pid));
    if (body_length <= 0 || static_cast<std::size_t>(body_length) >= body.size() ||
        path_length <= 0 || static_cast<std::size_t>(path_length) >= temporary.size())
    {
        errno = EOVERFLOW;
        return false;
    }

    (void)unlink(temporary.data());
    File output{open(temporary.data(), O_WRONLY | O_CREAT | O_EXCL, 0644)};
    if (output.get() < 0 ||
        !write_all(output.get(), body.data(), static_cast<std::size_t>(body_length)))
    {
        const int saved = errno ? errno : EIO;
        (void)unlink(temporary.data());
        errno = saved;
        return false;
    }
    const int descriptor = output.release();
    if (close(descriptor) != 0 || rename(temporary.data(), request_path) != 0)
    {
        const int saved = errno ? errno : EIO;
        (void)unlink(temporary.data());
        errno = saved;
        return false;
    }
    return true;
}

bool verify_data(pid_t pid, int &open_error) noexcept
{
    std::array<char, 80> path{};
    constexpr std::array<char, 8> token{'L', 'A', 'P', 'Y', 'O', 'W', 'N', '\n'};
    std::array<char, token.size()> actual{};
    const int length =
        std::snprintf(path.data(), path.size(), "/data/.lapy_probe_%ld", static_cast<long>(pid));
    if (length <= 0 || static_cast<std::size_t>(length) >= path.size())
    {
        open_error = EOVERFLOW;
        return false;
    }
    (void)unlink(path.data());
    File file{open(path.data(), O_RDWR | O_CREAT | O_EXCL, 0600)};
    open_error = file.get() < 0 ? errno : 0;
    const bool passed =
        file.get() >= 0 && write_all(file.get(), token.data(), token.size()) &&
        lseek(file.get(), 0, SEEK_SET) == 0 &&
        read(file.get(), actual.data(), actual.size()) == static_cast<ssize_t>(actual.size()) &&
        actual == token;
    (void)unlink(path.data());
    return passed;
}

bool wait_for_elevation(pid_t pid, int &open_error, bool &proof_failed) noexcept
{
    for (unsigned poll = 0; poll < elevation_polls; ++poll)
    {
        if (verify_data(pid, open_error))
            return true;
        if (open_error == 0)
        {
            proof_failed = true;
            return false;
        }
        (void)usleep(elevation_poll_interval_us);
    }
    return false;
}

void report_result(int descriptor, bool data_ok, int open_error) noexcept
{
    std::array<char, 96> result{};
    const int length = std::snprintf(result.data(), result.size(), "DATA_OK=%d OPEN_ERRNO=%d\n",
                                     data_ok, open_error);
    if (length > 0 && static_cast<std::size_t>(length) < result.size())
        (void)write_all(descriptor, result.data(), static_cast<std::size_t>(length));
}
} // namespace

elevation::Status elevation::request(Capability capability) noexcept
{
    if (capability != Capability::filesystem)
        return Status::unsupported_capability;

    // Lapy opens this descriptor through the sandbox after the request, so the
    // app must create it before credentials and root directories are changed.
    File result{open(result_path, O_WRONLY | O_CREAT | O_TRUNC, 0644)};
    if (result.get() < 0)
        return Status::unavailable;
    (void)fchmod(result.get(), 0644);

    if (seteuid(geteuid()) != 0)
        return Status::prepare_failed;
    const pid_t pid = getpid();
    if (pid <= 1 || !publish_request(pid))
        return Status::transport_error;
    int open_error = 0;
    bool proof_failed = false;
    const bool data_ok = wait_for_elevation(pid, open_error, proof_failed);
    report_result(result.get(), data_ok, open_error);
    return data_ok ? Status::ok : proof_failed ? Status::apply_failed : Status::timeout;
}
