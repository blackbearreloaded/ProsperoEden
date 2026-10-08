// SPDX-License-Identifier: GPL-3.0-or-later
// The PS5's own on-screen keyboard (its text entry dialog), for a game that asks the player for
// text. The system draws it over the app and takes the controller while it is open; a USB or
// Bluetooth keyboard the PS5 supports types into it too.
#pragma once
#include <atomic>
#include <cstddef>
#include <string>

namespace Eden {
struct TextRequest {
    std::u16string title;        // what is asked for, shown above the text
    std::u16string placeholder;  // shown while the text is empty
    std::u16string initial;      // the text to start from
    std::size_t max_length = 0;  // in UTF-16 units; 0: no limit of the game's
    bool numbers = false;        // a number pad
    bool password = false;       // the text is hidden
};

enum class TextOutcome {
    accepted,     // the player confirmed the text
    cancelled,    // the player closed the keyboard, or `stop` was set
    unavailable,  // the keyboard could not be opened
};

struct TextAnswer {
    TextOutcome outcome = TextOutcome::unavailable;
    std::u16string text;
};

// Opens the keyboard and waits until the player closes it or `stop` is set (it is then closed).
// One keyboard at a time: a second call waits for the first. Unavailable on the PC builds.
TextAnswer AskSystemKeyboard(const TextRequest& request, const std::atomic<bool>& stop);
}  // namespace Eden
