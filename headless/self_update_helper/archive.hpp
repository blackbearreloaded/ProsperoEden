// ps5-native-app-boilerplate - ZIP artifacts: validated from the directory, then unpacked.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>

namespace self_update
{
constexpr std::size_t kArchiveEntries = 100000;
constexpr std::size_t kArchivePath = 512;

struct ArchiveInfo
{
    std::uint64_t unpacked = 0; // What the directory declares; enforced while unpacking.
    std::size_t files = 0;
};
// Reads the archive's directory only. The app is the one folder in it that
// holds sce_sys/param.json and eboot.bin (at the top, in a folder named after
// the title, or up to three folders deep); what lies outside is never unpacked.
// Inside it, absolute and parent paths, links and special files, encryption,
// duplicates and anything over the limits are refused.
bool inspect_archive(const std::string &path, std::string_view title, ArchiveInfo &out,
                     std::string &error);
// Where the time of an unpack went, for the log.
struct ExtractTimes
{
    std::uint64_t write_ms = 0, sync_ms = 0, total_ms = 0;
    std::size_t files = 0;
};
// Unpacks the title's folder as destination, which must not exist. On failure
// the caller removes destination. written counts unpacked bytes for progress.
bool extract_archive(const std::string &path, std::string_view title,
                     const std::string &destination, const std::atomic<bool> &cancelled,
                     std::atomic<std::uint64_t> &written, std::string &error,
                     ExtractTimes *times = nullptr);
} // namespace self_update
