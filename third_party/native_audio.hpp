/*
 * Small native AudioOut/file helpers.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The helpers deliberately keep the platform ABI visible. They are intended
 * to be copied into a project created from ps5-native-app-boilerplate.
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

extern "C"
{
    int sceKernelOpen(const char *path, int flags, std::uint16_t mode);
    int sceKernelClose(int descriptor);
    std::int64_t sceKernelRead(int descriptor, void *buffer, std::size_t length);

    int sceAudioOutInit(void);
    int sceAudioOutOpen(int user_id, int type, int index, std::uint32_t length,
                        std::uint32_t frequency, std::uint32_t format);
    int sceAudioOutClose(int handle);
    int sceAudioOutOutput(int handle, const void *samples);
    int sceAudioOutSetVolume(int handle, int flags, const int *volumes);
}

namespace ps5::native_audio
{
constexpr std::size_t kAudioOutGrain = 256;
constexpr std::uint32_t kAudioOutRate = 48000;
constexpr std::uint32_t kAudioOutStereoS16 = 1;

inline std::size_t read_file(const char *path, std::span<std::uint8_t> destination) noexcept
{
    constexpr int kOpenReadOnly = 0;
    const int descriptor = sceKernelOpen(path, kOpenReadOnly, 0);
    if (descriptor < 0)
    {
        return 0;
    }

    std::size_t total = 0;
    while (total < destination.size())
    {
        const std::int64_t result =
            sceKernelRead(descriptor, destination.data() + total, destination.size() - total);
        if (result <= 0)
        {
            break;
        }
        total += static_cast<std::size_t>(result);
    }
    sceKernelClose(descriptor);
    return total;
}

class AudioOut final
{
  public:
    AudioOut() noexcept = default;
    AudioOut(const AudioOut &) = delete;
    AudioOut &operator=(const AudioOut &) = delete;

    ~AudioOut() noexcept
    {
        close();
    }

    bool open() noexcept
    {
        if (handle_ >= 0 || sceAudioOutInit() < 0)
        {
            return false;
        }

        handle_ = sceAudioOutOpen(0xff, 0, 0, static_cast<std::uint32_t>(kAudioOutGrain),
                                  kAudioOutRate, kAudioOutStereoS16);
        if (handle_ < 0)
        {
            return false;
        }

        std::array<int, 8> volumes{};
        volumes.fill(0x8000);
        if (sceAudioOutSetVolume(handle_, 3, volumes.data()) < 0)
        {
            close();
            return false;
        }
        return true;
    }

    bool push(std::span<const std::int16_t> samples, std::size_t channels) noexcept
    {
        if (handle_ < 0 || (channels != 1 && channels != 2) || samples.size() % channels != 0)
        {
            return false;
        }

        const std::size_t frames = samples.size() / channels;
        for (std::size_t frame = 0; frame < frames; ++frame)
        {
            block_[pending_++] = samples[frame * channels];
            block_[pending_++] =
                channels == 2 ? samples[frame * channels + 1] : block_[pending_ - 1];
            if (pending_ == block_.size())
            {
                if (sceAudioOutOutput(handle_, block_.data()) < 0)
                {
                    return false;
                }
                pending_ = 0;
            }
        }
        return true;
    }

    bool flush() noexcept
    {
        if (handle_ < 0 || pending_ == 0)
        {
            return true;
        }

        std::fill(block_.begin() + static_cast<std::ptrdiff_t>(pending_), block_.end(), 0);
        const bool result = sceAudioOutOutput(handle_, block_.data()) >= 0;
        pending_ = 0;
        return result;
    }

    void close() noexcept
    {
        if (handle_ < 0)
        {
            return;
        }
        sceAudioOutOutput(handle_, nullptr);
        sceAudioOutClose(handle_);
        handle_ = -1;
        pending_ = 0;
    }

  private:
    int handle_ = -1;
    std::size_t pending_ = 0;
    std::array<std::int16_t, kAudioOutGrain * 2> block_{};
};
} // namespace ps5::native_audio
