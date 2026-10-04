// ps5-native-app-boilerplate - ZIP artifacts: validated from the directory, then unpacked.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "archive.hpp"
#include "files.hpp"
#include "miniz/miniz.h"
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <mutex>
#include <pthread.h>
#include <time.h>
#include <set>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace self_update
{
namespace
{
constexpr std::size_t kMemoryBudget = 64u << 20;
constexpr std::size_t kBlockHeader = 16;

struct Entry
{
    mz_uint index = 0;
    std::string relative; // Below the title's folder; empty for the folder itself.
    bool directory = false;
    std::uint64_t size = 0;
};

// Every allocation the ZIP reader makes is charged to one budget, so a hostile
// directory cannot make it take the app's memory.
struct Budget
{
    std::size_t used = 0;
};
void *budget_realloc(void *opaque, void *address, std::size_t items, std::size_t size)
{
    auto &budget = *static_cast<Budget *>(opaque);
    if (size != 0 && items > (kMemoryBudget - kBlockHeader) / size)
        return nullptr;
    const std::size_t wanted = items * size;
    auto *block = address ? static_cast<unsigned char *>(address) - kBlockHeader : nullptr;
    std::size_t previous = 0;
    if (block)
        std::memcpy(&previous, block, sizeof(previous));
    if (wanted > kMemoryBudget - (budget.used - previous))
        return nullptr;
    auto *next = static_cast<unsigned char *>(std::realloc(block, wanted + kBlockHeader));
    if (!next)
        return nullptr;
    std::memcpy(next, &wanted, sizeof(wanted));
    budget.used = budget.used - previous + wanted;
    return next + kBlockHeader;
}
void *budget_alloc(void *opaque, std::size_t items, std::size_t size)
{
    return budget_realloc(opaque, nullptr, items, size);
}
void budget_free(void *opaque, void *address)
{
    if (!address)
        return;
    auto *block = static_cast<unsigned char *>(address) - kBlockHeader;
    std::size_t previous = 0;
    std::memcpy(&previous, block, sizeof(previous));
    static_cast<Budget *>(opaque)->used -= previous;
    std::free(block);
}

class Reader
{
  public:
    Reader() = default;
    Reader(const Reader &) = delete;
    Reader &operator=(const Reader &) = delete;
    ~Reader()
    {
        if (open_)
            mz_zip_reader_end(&zip_);
        if (descriptor_ >= 0)
            close(descriptor_);
    }
    bool open(const std::string &path)
    {
        descriptor_ = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW);
        struct stat info
        {
        };
        if (descriptor_ < 0 || fstat(descriptor_, &info) != 0 || !S_ISREG(info.st_mode) ||
            info.st_size <= 0)
            return false;
        mz_zip_zero_struct(&zip_);
        zip_.m_pRead = +[](void *opaque, mz_uint64 offset, void *buffer, std::size_t count)
        {
            const int descriptor = *static_cast<int *>(opaque);
            std::size_t done = 0;
            while (done < count)
            {
                const auto result = pread(descriptor, static_cast<char *>(buffer) + done,
                                          count - done, static_cast<off_t>(offset + done));
                if (result < 0 && errno == EINTR)
                    continue;
                if (result <= 0)
                    break;
                done += static_cast<std::size_t>(result);
            }
            return done;
        };
        zip_.m_pIO_opaque = &descriptor_;
        zip_.m_pAlloc = budget_alloc;
        zip_.m_pFree = budget_free;
        zip_.m_pRealloc = budget_realloc;
        zip_.m_pAlloc_opaque = &budget_;
        open_ = mz_zip_reader_init(&zip_, static_cast<mz_uint64>(info.st_size), 0) != 0;
        return open_;
    }
    mz_zip_archive *zip()
    {
        return &zip_;
    }

  private:
    mz_zip_archive zip_{};
    Budget budget_;
    int descriptor_ = -1;
    bool open_ = false;
};

bool clean_relative(std::string_view path)
{
    while (!path.empty())
    {
        const auto slash = path.find('/');
        const auto part = path.substr(0, slash);
        if (part.empty() || part == "." || part == "..")
            return false;
        if (slash == path.npos)
            break;
        path.remove_prefix(slash + 1);
    }
    return true;
}

bool list(Reader &reader, std::string_view title, std::vector<Entry> &entries, ArchiveInfo &info,
          std::string &error)
{
    auto *zip = reader.zip();
    const mz_uint count = mz_zip_reader_get_num_files(zip);
    if (count == 0 || count > kArchiveEntries)
    {
        error = "The archive is empty or has too many entries";
        return false;
    }
    error = "The archive isn't a valid app";
    // First the names: developers pack the app's folder in different ways
    // (at the top, inside a folder named after the title, inside a release
    // folder, beside a README). The app is the folder that holds
    // sce_sys/param.json; everything outside it is left in the archive.
    std::vector<std::string> names(count);
    std::vector<char> buffer(kArchivePath + 2);
    constexpr std::string_view kMetadata = "sce_sys/param.json";
    std::string prefix;
    int best = -1;
    bool ambiguous = false;
    for (mz_uint index = 0; index < count; ++index)
    {
        const mz_uint length = mz_zip_reader_get_filename(zip, index, nullptr, 0);
        if (length < 2 || length - 1 > kArchivePath)
            return false;
        mz_zip_reader_get_filename(zip, index, buffer.data(), static_cast<mz_uint>(buffer.size()));
        names[index].assign(buffer.data(), length - 1);
        const std::string_view name = names[index];
        if (!name.ends_with(kMetadata) ||
            (name.size() > kMetadata.size() && name[name.size() - kMetadata.size() - 1] != '/'))
            continue;
        const auto folder = name.substr(0, name.size() - kMetadata.size());
        // The shallowest candidate wins; at the same depth, the folder named
        // after the title; two equal candidates are refused.
        const int depth = static_cast<int>(std::count(folder.begin(), folder.end(), '/'));
        const bool named = folder.ends_with(std::string(title) + "/");
        const int score = 4 * (8 - std::min(depth, 8)) + (named ? 1 : 0);
        if (depth > 3 || score < best)
            continue;
        ambiguous = score == best;
        best = score;
        prefix = folder;
    }
    if (best < 0)
    {
        error = "The archive has no sce_sys/param.json";
        return false;
    }
    if (ambiguous ||
        !clean_relative(prefix.empty() ? std::string_view{}
                                       : std::string_view(prefix).substr(0, prefix.size() - 1)))
    {
        error = "The archive holds more than one app, or an unsafe path";
        return false;
    }
    std::set<std::string> seen;
    bool program = false;
    for (mz_uint index = 0; index < count; ++index)
    {
        std::string name = names[index];
        if (!name.starts_with(prefix))
            continue; // Not part of the app: never unpacked.
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(zip, index, &stat) || !stat.m_is_supported ||
            stat.m_is_encrypted)
            return false;
        for (const unsigned char c : name)
            if (c < 32 || c == 127 || c == '\\')
                return false;
        Entry entry;
        entry.index = index;
        entry.directory = name.ends_with('/');
        if (entry.directory != (stat.m_is_directory != 0))
            return false;
        if (entry.directory)
            name.pop_back();
        entry.relative = name.size() > prefix.size() ? name.substr(prefix.size()) : std::string{};
        if (entry.relative.empty())
        {
            if (!entry.directory)
                return false;
            continue; // The app's folder itself.
        }
        if (!clean_relative(entry.relative) || !seen.insert(entry.relative).second)
            return false;
        // Entries written on Unix carry a file type: only files and folders pass.
        if ((stat.m_version_made_by >> 8) == 3)
        {
            const auto type = (stat.m_external_attr >> 16) & 0170000u;
            if (type != 0 && type != (entry.directory ? 0040000u : 0100000u))
            {
                error = "The archive contains a link or a special file";
                return false;
            }
        }
        if (entry.directory)
        {
            if (stat.m_uncomp_size != 0)
                return false;
        }
        else
        {
            entry.size = stat.m_uncomp_size;
            if (entry.size > std::numeric_limits<std::uint64_t>::max() - info.unpacked)
                return false;
            info.unpacked += entry.size;
            ++info.files;
            program |= entry.relative == "eboot.bin";
        }
        entries.push_back(std::move(entry));
    }
    if (!program)
    {
        error = "The archive has no eboot.bin beside sce_sys";
        return false;
    }
    error.clear();
    return true;
}

// Creates the folders of relative below base. Each must be a real directory.
// Several workers unpack at once: the set of folders already made is shared,
// and two workers making the same folder is harmless.
bool make_parents(const std::string &base, std::string_view relative, bool last_is_directory,
                  std::set<std::string> &made, std::mutex &guard)
{
    std::size_t end = 0;
    for (;;)
    {
        end = relative.find('/', end);
        if (end == relative.npos && !last_is_directory)
            return true;
        const std::string part(relative.substr(0, end));
        bool known = false;
        {
            std::lock_guard lock(guard);
            known = made.contains(part);
        }
        if (!known)
        {
            if (!make_directory(base + "/" + part))
                return false;
            std::lock_guard lock(guard);
            made.insert(part);
        }
        if (end == relative.npos)
            return true;
        ++end;
    }
}

std::uint64_t now_ms()
{
    timespec time{};
    clock_gettime(CLOCK_MONOTONIC, &time);
    return static_cast<std::uint64_t>(time.tv_sec) * 1000u +
           static_cast<std::uint64_t>(time.tv_nsec) / 1000000u;
}

// The inflater hands over 32 KB at a time. Small writes are slow on the
// console's storage, so they are gathered and written a few megabytes at once.
struct Output
{
    int descriptor;
    std::uint64_t expected, written = 0;
    const std::atomic<bool> &cancelled;
    std::atomic<std::uint64_t> &total;
    std::vector<char> &buffer;
    std::size_t held = 0;
    std::uint64_t write_ms = 0;
    bool flush()
    {
        const auto started = now_ms();
        std::size_t done = 0;
        while (done < held)
        {
            const auto result = write(descriptor, buffer.data() + done, held - done);
            if (result < 0 && errno == EINTR)
                continue;
            if (result <= 0)
                return false;
            done += static_cast<std::size_t>(result);
        }
        held = 0;
        write_ms += now_ms() - started;
        return true;
    }
};
std::size_t write_output(void *opaque, mz_uint64 offset, const void *data, std::size_t count)
{
    auto &output = *static_cast<Output *>(opaque);
    if (output.cancelled.load() || offset != output.written ||
        count > output.expected - output.written)
        return 0;
    const char *bytes = static_cast<const char *>(data);
    std::size_t taken = 0;
    while (taken < count)
    {
        const std::size_t room = output.buffer.size() - output.held;
        const std::size_t piece = std::min(room, count - taken);
        std::memcpy(output.buffer.data() + output.held, bytes + taken, piece);
        output.held += piece;
        taken += piece;
        if (output.held == output.buffer.size() && !output.flush())
            return 0;
    }
    output.written += count;
    output.total.fetch_add(count);
    return count;
}

// Every unpacked file must be on the disk before the app is put in place: a
// power cut must never leave an installed app with empty files. Asking for
// that file by file, as each is written, costs a fraction of a second apiece
// on the console, which is minutes for an app of thousands of files. So the
// files are written first and made durable together at the end, by several
// workers at once, which lets the filesystem commit them in groups.
struct SyncWork
{
    const std::vector<std::string> *paths;
    std::atomic<std::size_t> next{0};
    std::atomic<bool> failed{false};
    const std::atomic<bool> *cancelled;
};
void *sync_worker(void *opaque)
{
    auto &work = *static_cast<SyncWork *>(opaque);
    for (;;)
    {
        const std::size_t index = work.next.fetch_add(1);
        if (index >= work.paths->size() || work.failed.load() || work.cancelled->load())
            return nullptr;
        const int descriptor = open((*work.paths)[index].c_str(), O_RDONLY | O_NOFOLLOW);
        if (descriptor < 0 || fsync(descriptor) != 0)
            work.failed = true;
        if (descriptor >= 0)
            close(descriptor);
    }
}
bool sync_files(const std::vector<std::string> &paths, const std::atomic<bool> &cancelled)
{
    SyncWork work{&paths, {}, {}, &cancelled};
    pthread_t workers[8];
    std::size_t started = 0;
    for (auto &worker : workers)
        if (pthread_create(&worker, nullptr, sync_worker, &work) == 0)
            workers[started++] = worker;
    if (started == 0)
        sync_worker(&work);
    for (std::size_t i = 0; i < started; ++i)
        pthread_join(workers[i], nullptr);
    return !work.failed.load() && !cancelled.load();
}
} // namespace

bool inspect_archive(const std::string &path, std::string_view title, ArchiveInfo &out,
                     std::string &error)
{
    Reader reader;
    if (!reader.open(path))
    {
        error = "The archive isn't a valid app";
        return false;
    }
    std::vector<Entry> entries;
    ArchiveInfo info;
    if (!list(reader, title, entries, info, error))
        return false;
    out = info;
    return true;
}

namespace
{
// Creating a file costs about a tenth of a second on the console's storage,
// whatever its size, so an app of thousands of small files spent minutes in a
// single queue. Workers take the entries in turn, each with its own reader of
// the archive, and the storage serves them side by side.
struct ExtractWork
{
    const std::string &path, &destination;
    const std::vector<Entry> &entries;
    const std::atomic<bool> &cancelled;
    std::atomic<std::uint64_t> &written;
    std::atomic<std::size_t> next{0};
    std::atomic<bool> failed{false};
    std::atomic<std::uint64_t> write_ms{0};
    std::mutex guard;
    std::set<std::string> made;
    ExtractWork(const std::string &archive, const std::string &folder,
                const std::vector<Entry> &list, const std::atomic<bool> &stop,
                std::atomic<std::uint64_t> &count)
        : path(archive), destination(folder), entries(list), cancelled(stop), written(count)
    {
    }
};
void *extract_worker(void *opaque)
{
    auto &work = *static_cast<ExtractWork *>(opaque);
    Reader reader;
    std::vector<char> buffer(1u << 20);
    if (!reader.open(work.path))
    {
        work.failed = true;
        return nullptr;
    }
    for (;;)
    {
        const std::size_t index = work.next.fetch_add(1);
        if (index >= work.entries.size() || work.failed.load() || work.cancelled.load())
            return nullptr;
        const Entry &entry = work.entries[index];
        if (entry.relative.empty())
            continue;
        if (!make_parents(work.destination, entry.relative, entry.directory, work.made, work.guard))
        {
            work.failed = true;
            return nullptr;
        }
        if (entry.directory)
            continue;
        const std::string target = work.destination + "/" + entry.relative;
        Output output{open(target.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0644),
                      entry.size,
                      0,
                      work.cancelled,
                      work.written,
                      buffer};
        // The reader checks the stored CRC-32; the callback enforces the declared size.
        bool ok = output.descriptor >= 0 &&
                  mz_zip_reader_extract_to_callback(reader.zip(), entry.index, write_output,
                                                    &output, 0) != 0 &&
                  output.written == output.expected && output.flush();
        if (output.descriptor >= 0)
            ok = close(output.descriptor) == 0 && ok;
        work.write_ms.fetch_add(output.write_ms);
        if (!ok)
        {
            work.failed = true;
            return nullptr;
        }
    }
}
} // namespace

bool extract_archive(const std::string &path, std::string_view title,
                     const std::string &destination, const std::atomic<bool> &cancelled,
                     std::atomic<std::uint64_t> &written, std::string &error, ExtractTimes *times)
{
    const auto started = now_ms();
    std::vector<Entry> entries;
    ArchiveInfo info;
    {
        Reader reader;
        if (!reader.open(path))
        {
            error = "The archive isn't a valid app";
            return false;
        }
        if (!list(reader, title, entries, info, error))
            return false;
    }
    error = "The app could not be unpacked";
    if (mkdir(destination.c_str(), 0755) != 0)
        return false;
    ExtractWork work{path, destination, entries, cancelled, written};
    pthread_t workers[8];
    std::size_t running = 0;
    for (auto &worker : workers)
        if (pthread_create(&worker, nullptr, extract_worker, &work) == 0)
            workers[running++] = worker;
    if (running == 0)
        extract_worker(&work);
    for (std::size_t i = 0; i < running; ++i)
        pthread_join(workers[i], nullptr);
    if (cancelled.load())
    {
        error = "Cancelled";
        return false;
    }
    if (work.failed.load())
        return false;
    // Files and folders alike are made durable together, before the app is put in place.
    std::vector<std::string> paths;
    for (const auto &entry : entries)
        if (!entry.directory && !entry.relative.empty())
            paths.push_back(destination + "/" + entry.relative);
    const std::size_t files = paths.size();
    for (const auto &directory : work.made)
        paths.push_back(destination + "/" + directory);
    paths.push_back(destination);
    const auto syncing = now_ms();
    if (!sync_files(paths, cancelled))
    {
        if (cancelled.load())
            error = "Cancelled";
        return false;
    }
    if (times)
        *times = {work.write_ms.load(), now_ms() - syncing, now_ms() - started, files};
    error.clear();
    return true;
}
} // namespace self_update
