/*
 * ps5-native-app-boilerplate - SHA-256 for the self-update kit.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * See self_update_sha256.h. Written from FIPS 180-4.
 */
#include "self_update_sha256.h"

#include <string.h>

static const uint32_t self_update_sha256_k[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
    0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
    0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
    0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
    0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
    0xc67178f2u};

static uint32_t rotate(uint32_t value, unsigned bits)
{
    return (value >> bits) | (value << (32u - bits));
}

static void compress(uint32_t state[8], const unsigned char block[64])
{
    uint32_t w[64];
    uint32_t v[8];
    unsigned i;
    for (i = 0; i < 16; ++i)
        w[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) | (uint32_t)block[i * 4 + 3];
    for (i = 16; i < 64; ++i)
    {
        const uint32_t s0 = rotate(w[i - 15], 7) ^ rotate(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = rotate(w[i - 2], 17) ^ rotate(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    memcpy(v, state, sizeof(v));
    for (i = 0; i < 64; ++i)
    {
        const uint32_t s1 = rotate(v[4], 6) ^ rotate(v[4], 11) ^ rotate(v[4], 25);
        const uint32_t choice = (v[4] & v[5]) ^ (~v[4] & v[6]);
        const uint32_t t1 = v[7] + s1 + choice + self_update_sha256_k[i] + w[i];
        const uint32_t s0 = rotate(v[0], 2) ^ rotate(v[0], 13) ^ rotate(v[0], 22);
        const uint32_t majority = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
        const uint32_t t2 = s0 + majority;
        v[7] = v[6];
        v[6] = v[5];
        v[5] = v[4];
        v[4] = v[3] + t1;
        v[3] = v[2];
        v[2] = v[1];
        v[1] = v[0];
        v[0] = t1 + t2;
    }
    for (i = 0; i < 8; ++i)
        state[i] += v[i];
}

void self_update_sha256_start(self_update_sha256 *hash)
{
    static const uint32_t initial[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    memcpy(hash->state, initial, sizeof(initial));
    hash->length = 0;
    hash->held = 0;
}

void self_update_sha256_add(self_update_sha256 *hash, const void *data, size_t size)
{
    const unsigned char *bytes = (const unsigned char *)data;
    hash->length += size;
    while (size != 0)
    {
        size_t take = sizeof(hash->block) - hash->held;
        if (take > size)
            take = size;
        memcpy(hash->block + hash->held, bytes, take);
        hash->held += take;
        bytes += take;
        size -= take;
        if (hash->held == sizeof(hash->block))
        {
            compress(hash->state, hash->block);
            hash->held = 0;
        }
    }
}

void self_update_sha256_finish(self_update_sha256 *hash, unsigned char digest[32])
{
    const uint64_t bits = hash->length * 8u;
    unsigned i;
    hash->block[hash->held++] = 0x80;
    if (hash->held > 56)
    {
        memset(hash->block + hash->held, 0, sizeof(hash->block) - hash->held);
        compress(hash->state, hash->block);
        hash->held = 0;
    }
    memset(hash->block + hash->held, 0, 56 - hash->held);
    for (i = 0; i < 8; ++i)
        hash->block[56 + i] = (unsigned char)(bits >> (56 - 8 * i));
    compress(hash->state, hash->block);
    for (i = 0; i < 8; ++i)
    {
        digest[i * 4] = (unsigned char)(hash->state[i] >> 24);
        digest[i * 4 + 1] = (unsigned char)(hash->state[i] >> 16);
        digest[i * 4 + 2] = (unsigned char)(hash->state[i] >> 8);
        digest[i * 4 + 3] = (unsigned char)hash->state[i];
    }
}

void self_update_sha256_hex(const unsigned char digest[32], char text[65])
{
    static const char digits[] = "0123456789abcdef";
    unsigned i;
    for (i = 0; i < 32; ++i)
    {
        text[i * 2] = digits[digest[i] >> 4];
        text[i * 2 + 1] = digits[digest[i] & 15];
    }
    text[64] = '\0';
}

int self_update_sha256_matches(const unsigned char digest[32], const char *text)
{
    char own[65];
    unsigned i;
    unsigned difference = 0;
    if (text == NULL || strlen(text) != 64)
        return 0;
    self_update_sha256_hex(digest, own);
    for (i = 0; i < 64; ++i)
    {
        char c = text[i];
        if (c >= 'A' && c <= 'F')
            c = (char)(c - 'A' + 'a');
        difference |= (unsigned)(c ^ own[i]);
    }
    return difference == 0;
}
