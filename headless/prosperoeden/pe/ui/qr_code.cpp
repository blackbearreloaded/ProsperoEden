// ProsperoEden - Launcher: a QR code of a text; see qr_code.hpp. Made by Project Nayuki's QR Code
// generator library (third_party/qrcodegen, MIT), compiled here.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pe/ui/qr_code.hpp"

#include <cstdint>

#include "qrcodegen/qrcodegen.c"

namespace pe::ui
{

QrCode make_qr_code(const std::string &text)
{
    std::vector<std::uint8_t> code(qrcodegen_BUFFER_LEN_MAX);
    std::vector<std::uint8_t> work(qrcodegen_BUFFER_LEN_MAX);
    QrCode qr;
    if (!qrcodegen_encodeText(text.c_str(), work.data(), code.data(), qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN,
                              qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true))
        return qr;
    qr.size = qrcodegen_getSize(code.data());
    qr.dark.resize(static_cast<std::size_t>(qr.size * qr.size));
    for (int y = 0; y < qr.size; ++y)
        for (int x = 0; x < qr.size; ++x)
            qr.dark[static_cast<std::size_t>(y * qr.size + x)] = qrcodegen_getModule(code.data(), x, y);
    return qr;
}

} // namespace pe::ui
