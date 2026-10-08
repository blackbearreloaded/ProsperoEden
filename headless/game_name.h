// SPDX-License-Identifier: GPL-3.0-or-later
// A game's name as the Library shows it. A game carries a name for each of up to sixteen
// languages, and what is in them is the publisher's: one can be empty, hold bytes that are not
// text, or use characters that look like plain letters but are others (the Roman numeral "\u2161"
// of a sequel, full-width Latin letters). Such a name was drawn as question marks.
//
//   UsableName   whether a name is text with at least one character that has a shape
//   PlainName    the name with look-alike characters as the plain letters they stand for
#pragma once
#include <cstddef>
#include <string>
#include <string_view>

namespace Eden {
// The next character of UTF-8 text; false at the end and for bytes that are not UTF-8 (too long
// a form, half of a pair, past the last character there is).
inline bool NextNameCharacter(std::string_view text, std::size_t& index, char32_t& character) {
    if (index >= text.size()) return false;
    const auto byte = [&](std::size_t at) { return static_cast<unsigned char>(text[at]); };
    const unsigned char lead = byte(index);
    const int length = lead < 0x80 ? 1 : lead >= 0xc2 && lead < 0xe0 ? 2 : lead >= 0xe0 && lead < 0xf0 ? 3 :
                       lead >= 0xf0 && lead < 0xf5 ? 4 : 0;
    if (length == 0 || index + static_cast<std::size_t>(length) > text.size()) return false;
    char32_t value = length == 1 ? lead : lead & (0xffu >> (length + 1));
    for (int i = 1; i < length; ++i) {
        if ((byte(index + i) & 0xc0) != 0x80) return false;
        value = (value << 6) | (byte(index + i) & 0x3f);
    }
    static constexpr char32_t kSmallest[] = {0, 0, 0x80, 0x800, 0x10000};
    if (value < kSmallest[length] || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
    index += static_cast<std::size_t>(length);
    character = value;
    return true;
}

inline bool UsableName(std::string_view name) {
    bool shaped = false;
    for (std::size_t index = 0; index < name.size();) {
        char32_t c = 0;
        if (!NextNameCharacter(name, index, c)) return false;
        // Control characters, the replacement character and private-use ones have no shape of
        // their own; a question mark is what a name that could not be written turns into.
        const bool blank = c <= 0x20 || (c >= 0x7f && c <= 0xa0) || c == U'?' || c == 0xfffd || c == 0x3000 ||
                           (c >= 0x2000 && c <= 0x200f) || (c >= 0xe000 && c <= 0xf8ff) || c >= 0xf0000;
        shaped |= !blank;
    }
    return shaped;
}

inline std::string PlainName(std::string_view name) {
    static constexpr const char* kRoman[] = {"I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI", "XII"};
    static constexpr const char* kSmallRoman[] = {"i", "ii", "iii", "iv", "v", "vi", "vii", "viii", "ix", "x", "xi", "xii"};
    std::string plain;
    for (std::size_t index = 0; index < name.size();) {
        const std::size_t start = index;
        char32_t c = 0;
        if (!NextNameCharacter(name, index, c)) return std::string(name);  // not text: left as it is
        if (c >= 0x2160 && c <= 0x216b) plain += kRoman[c - 0x2160];
        else if (c >= 0x2170 && c <= 0x217b) plain += kSmallRoman[c - 0x2170];
        else if (c >= 0xff01 && c <= 0xff5e) plain += static_cast<char>(c - 0xfee0);  // full-width Latin
        else if (c == 0x3000) plain += ' ';
        else plain.append(name.substr(start, index - start));
    }
    while (!plain.empty() && plain.back() == ' ') plain.pop_back();
    const std::size_t first = plain.find_first_not_of(' ');
    return first == std::string::npos ? std::string{} : plain.substr(first);
}
}  // namespace Eden
