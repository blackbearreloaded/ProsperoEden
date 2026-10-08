// ProsperoEden - Launcher: a QR code of a text (an address), for a phone to open it.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <vector>

namespace pe::ui
{

struct QrCode
{
    int size = 0;            // modules a side; 0: none
    std::vector<bool> dark;  // row by row
    bool at(int x, int y) const
    {
        return dark[static_cast<std::size_t>(y * size + x)];
    }
};

// The QR code of a text (error correction M); empty when it is too long for one.
QrCode make_qr_code(const std::string &text);

} // namespace pe::ui
