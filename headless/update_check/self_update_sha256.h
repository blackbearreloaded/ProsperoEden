/*
 * ps5-native-app-boilerplate - SHA-256 for the self-update kit.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A plain FIPS 180-4 SHA-256, shared by the app and the helper so neither needs
 * a library for it. C11; also compiles as C++.
 */
#ifndef PS5_BOILERPLATE_SELF_UPDATE_SHA256_H
#define PS5_BOILERPLATE_SELF_UPDATE_SHA256_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct self_update_sha256
    {
        uint32_t state[8];
        uint64_t length; /* bytes hashed so far */
        unsigned char block[64];
        size_t held;
    } self_update_sha256;

    void self_update_sha256_start(self_update_sha256 *hash);
    void self_update_sha256_add(self_update_sha256 *hash, const void *data, size_t size);
    void self_update_sha256_finish(self_update_sha256 *hash, unsigned char digest[32]);

    /* Writes 64 lowercase hexadecimal characters and a terminator. */
    void self_update_sha256_hex(const unsigned char digest[32], char text[65]);

    /* 1 when `text` is exactly 64 hexadecimal characters naming `digest` (either case). */
    int self_update_sha256_matches(const unsigned char digest[32], const char *text);

#ifdef __cplusplus
}
#endif

#endif
