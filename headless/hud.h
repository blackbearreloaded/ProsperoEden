// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace Eden {
// Counts contiguous presentation intervals; loading gaps are never reported as FPS.
struct HudClock {
    double start{-1}, last{-1}, fps{-1}, worst_ms{}, window_worst_ms{};
    unsigned frames{};
    void Present(double now) {
        if (last >= 0 && now - last > 0.5) {
            start = last = now;
            fps = -1;
            worst_ms = 0;
            window_worst_ms = 0;
            frames = 0;
            return;
        }
        if (last >= 0) window_worst_ms = std::max(window_worst_ms, (now - last) * 1000.0);
        last = now;
        if (start < 0) { start = now; return; }
        ++frames;
        if (now - start >= 1.0) {
            fps = frames / (now - start);
            worst_ms = window_worst_ms;
            window_worst_ms = 0;
            frames = 0;
            start = now;
        }
    }
};

// Keep startup work behind the loading screen until presentation is sustained.
struct StartupGate {
    double last{-1}, first{-1};
    unsigned smooth_frames{};
    bool Ready(double now) {
        if (first < 0) first = now;
        smooth_frames = last >= 0 && now > last && now - last <= 0.1 ? smooth_frames + 1 : 0;
        last = now;
        return smooth_frames >= 8 || now - first >= 5.0;
    }
};
inline uint32_t HudGlyph(char c) {
    switch (c) {
    case '0': return 0x7b6f;
    case '1': return 0x2c97;
    case '2': return 0x73e7;
    case '3': return 0x73cf;
    case '4': return 0x5bc9;
    case '5': return 0x79cf;
    case '6': return 0x79ef;
    case '7': return 0x7292;
    case '8': return 0x7bef;
    case '9': return 0x7bcf;
    case 'F': return 0x79a4;
    case 'P': return 0x6ba4;
    case 'S': return 0x79cf;
    case 'L': return 0x4927;
    case 'O': return 0x7b6f;
    case 'A': return 0x2bed;
    case 'D': return 0x6b6e;
    case 'I': return 0x7497;
    case 'N': return 0x5ffd;
    case 'G': return 0x796f;
    case 'V': return 0x5b6a;
    case 'K': return 0x5bad;
    case 'W': return 0x5fed;
    case 'B': return 0x6bae;
    case 'C': return 0x7927;
    case 'E': return 0x79e7;
    case 'H': return 0x5bed;
    case 'J': return 0x126f;
    case 'M': return 0x5f6d;
    case 'Q': return 0x7b79;
    case 'R': return 0x6bad;
    case 'T': return 0x7492;
    case 'U': return 0x5b6f;
    case 'X': return 0x5aad;
    case 'Y': return 0x5a92;
    case 'Z': return 0x72a7;
    case '.': return 0x0002;
    case '-': return 0x01c0;
    default: return 0;
    }
}
inline std::array<uint32_t, 24> HudText(std::string_view text) {
    std::array<uint32_t, 24> glyphs{};
    for (size_t i = 0; i < text.size() && i < glyphs.size(); ++i)
        glyphs[i] = HudGlyph(text[i]);
    return glyphs;
}
// The overlay's width and where its text starts, in a picture 1080 rows high. Where it is drawn
// they are scaled to the picture, and `width` then carries the picture's height to the shader
// (vulkan_hud_draw.inc).
struct HudSnapshot {
    std::array<uint32_t, 24> glyphs{};
    uint32_t width{};
    uint32_t x{28}, y{30}, loading{};
};
// The step a game's start is at, named under the loading screen's scene so that a start that is
// stuck can be told from one that is slow (main.cpp sets it; both backends draw it). A step that
// lasts gets its seconds added.
namespace Loading {
enum class Step : uint32_t { none, game, graphics, reading_shaders, shaders, starting };
inline std::atomic<uint32_t> step{0}, built{0}, total{0};
inline std::atomic<int64_t> since{0};  // when the step began, in milliseconds
inline int64_t Now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline void Set(Step next) {
    since.store(Now(), std::memory_order_relaxed);
    step.store(static_cast<uint32_t>(next), std::memory_order_relaxed);
}
inline void Shaders(size_t done, size_t of) {
    built.store(static_cast<uint32_t>(done), std::memory_order_relaxed);
    total.store(static_cast<uint32_t>(of), std::memory_order_relaxed);
    if (step.load(std::memory_order_relaxed) != static_cast<uint32_t>(Step::shaders)) Set(Step::shaders);
}
// At most 24 characters, of the overlay's letters (HudGlyph).
inline std::string Line(int64_t now = Now()) {
    const char* name = "";
    switch (static_cast<Step>(step.load(std::memory_order_relaxed))) {
    case Step::none: return {};
    case Step::game: name = "READING THE GAME"; break;
    case Step::graphics: name = "STARTING GRAPHICS"; break;
    case Step::reading_shaders: name = "READING SHADERS"; break;
    case Step::shaders: {
        char text[32];
        std::snprintf(text, sizeof(text), "SHADERS %u OF %u", built.load(std::memory_order_relaxed) % 100000u,
                      total.load(std::memory_order_relaxed) % 100000u);
        return text;
    }
    case Step::starting: name = "STARTING THE GAME"; break;
    }
    const int64_t seconds = (now - since.load(std::memory_order_relaxed)) / 1000;
    if (seconds < 20) return name;
    char text[32];
    std::snprintf(text, sizeof(text), "%s %lld S", name, static_cast<long long>(std::min<int64_t>(seconds, 9999)));
    return text;
}
} // namespace Loading
// The loading screen: the shader draws its scene from the time alone (loading_scene.glsl).
// `loading` carries the milliseconds since loading began, plus one; x and y carry the size of the
// picture, set where it is drawn (vulkan_hud_draw.inc); the glyphs carry the step's name.
inline HudSnapshot MakeLoadingSnapshot(double seconds) {
    HudSnapshot snapshot{};
    snapshot.glyphs = HudText(Loading::Line());
    snapshot.width = 1920;
    snapshot.x = 1920;
    snapshot.y = 1080;
    snapshot.loading = 1 + static_cast<uint32_t>(std::max(0.0, seconds) * 1000.0);
    return snapshot;
}
// When the next picture of the loading screen is due. With nothing else to do (idle) the GPU
// thread draws one for every display refresh; while it works on the game's own commands it draws
// only a few a second, so the animation never slows the game's start.
struct LoadingPace {
    double next{-1}, last{-1};
    bool Due(double now, bool idle) {
        if (last >= 0 && (idle ? now < next : now - last < 0.1)) return false;
        next = last < 0 || now - next > 0.05 ? now + 1.0 / 60.0 : next + 1.0 / 60.0;
        last = now;
        return true;
    }
};
inline std::array<char, 25> FormatHudText(const HudClock& clock, double speed,
                                          const char* backend) {
    std::array<char, 25> text{};
    if (clock.fps < 0) std::snprintf(text.data(), text.size(), "%s F-- S-- W--", backend);
    else std::snprintf(text.data(), text.size(), "%s F%.0f S%.0f W%.0f",
                       backend, clock.fps, speed, clock.worst_ms);
    return text;
}
inline HudSnapshot MakeHudSnapshot(const HudClock& clock, double speed) {
    const auto text = FormatHudText(clock, speed, "VLK");
    const std::string_view value{text.data()};
    return {HudText(value), static_cast<uint32_t>(value.size() * 16 + 24)};
}
// Read on the renderer thread; the scheduler captures the returned value per frame.
HudSnapshot GetVulkanHud();
} // namespace Eden
