/*
 * ps5-native-app-boilerplate - The console side of the self-update kit.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * self_update_console(): HTTPS through libcurl (console_curl.c), Ed25519
 * through the OpenSSL that libcurl already links, the helper through the
 * payload loader on loopback port 9021, and /download0 for the one number kept
 * between launches. Build with PACBREW_PACKAGES += libcurl and
 * APP_WRAP_SYMBOLS += fcntl, and ship the helper as /app0/self-updater.elf
 * (APP_ROOT_FILES). The app needs a positive downloadDataSize for /download0.
 */
#include "self_update.h"

#include "console_curl.h"
#include "update_check.h"
#include "self_update_protocol.h"

#include <curl/curl.h>
#include <fcntl.h>
#include <openssl/evp.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

int sceKernelOpen(const char *path, int flags, mode_t mode);
int sceKernelClose(int descriptor);
int64_t sceKernelRead(int descriptor, void *buffer, size_t length);
int64_t sceKernelWrite(int descriptor, const void *buffer, size_t length);
uint64_t sceKernelGetProcessTime(void);
int sceNetConnect(int socket, const void *address, uint32_t address_length);
int sceNetSend(int socket, const void *data, size_t length, int flags);
int sceNetRecv(int socket, void *data, size_t length, int flags);
int sceNetSetsockopt(int socket, int level, int option, const void *value, uint32_t size);
int sceNetSocket(const char *name, int domain, int type, int protocol);
int sceNetSocketClose(int socket);

#ifndef SELF_UPDATE_HELPER_PATH
#define SELF_UPDATE_HELPER_PATH "/app0/self-updater.elf"
#endif
#ifndef SELF_UPDATE_SEQUENCE_PATH
#define SELF_UPDATE_SEQUENCE_PATH "/download0/self-update-sequence"
#endif
#ifndef SELF_UPDATE_PARAM_PATH
#define SELF_UPDATE_PARAM_PATH "/app0/sce_sys/param.json"
#endif

enum
{
    self_update_connect_ms = 10000, /* covers the name lookup too */
    self_update_small_ms = 20000,
    self_update_redirects = 5
};

static pthread_once_t self_update_curl_once = PTHREAD_ONCE_INIT;
static CURLcode self_update_curl_started = CURLE_FAILED_INIT;
static char self_update_agent[48] = "homebrew-self-update/1";

static void start_curl(void)
{
    self_update_curl_started = curl_global_init(CURL_GLOBAL_DEFAULT);
}

static CURL *new_request(const char *url)
{
    CURL *easy;
    (void)pthread_once(&self_update_curl_once, start_curl);
    if (self_update_curl_started != CURLE_OK)
        return NULL;
    easy = curl_easy_init();
    if (easy == NULL)
        return NULL;
    console_curl_setup(easy); /* no signals, the console's CA list, non-blocking sockets */
    (void)curl_easy_setopt(easy, CURLOPT_URL, url);
    (void)curl_easy_setopt(easy, CURLOPT_USERAGENT, self_update_agent);
    (void)curl_easy_setopt(easy, CURLOPT_PROTOCOLS_STR, "https");
    (void)curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 0L);
    (void)curl_easy_setopt(easy, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_1_1);
    (void)curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT_MS, (long)self_update_connect_ms);
    return easy;
}

/* ---- Small files --------------------------------------------------------------------------- */

typedef struct memory_sink
{
    char *body;
    size_t capacity;
    size_t used;
    int overflow;
} memory_sink;

static size_t on_small(char *data, size_t size, size_t count, void *user)
{
    memory_sink *sink = (memory_sink *)user;
    const size_t bytes = size * count;
    if (bytes > sink->capacity - sink->used)
    {
        sink->overflow = 1;
        return 0;
    }
    memcpy(sink->body + sink->used, data, bytes);
    sink->used += bytes;
    return bytes;
}

static int console_fetch(void *user, const char *url, char *body, size_t capacity, size_t *length,
                         int *status)
{
    memory_sink sink = {body, capacity, 0, 0};
    long code = 0;
    CURLcode result;
    CURL *easy = new_request(url);
    (void)user;
    *length = 0;
    *status = 0;
    if (easy == NULL)
        return -(10000 + (int)CURLE_FAILED_INIT);
    (void)curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, (long)self_update_small_ms);
    (void)curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, on_small);
    (void)curl_easy_setopt(easy, CURLOPT_WRITEDATA, &sink);
    result = curl_easy_perform(easy);
    (void)curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(easy);
    if (result != CURLE_OK || sink.overflow)
        return -(10000 + (int)(result != CURLE_OK ? result : CURLE_WRITE_ERROR));
    *status = (int)code;
    *length = sink.used;
    return 0;
}

/* ---- The release --------------------------------------------------------------------------- */

typedef struct stream_sink
{
    self_update_sink sink;
    void *user;
    uint64_t limit;
    uint64_t received;
    int stopped;
} stream_sink;

static size_t on_stream(char *data, size_t size, size_t count, void *user)
{
    stream_sink *stream = (stream_sink *)user;
    const size_t bytes = size * count;
    if (bytes > stream->limit - stream->received || stream->sink(stream->user, data, bytes) != 1)
    {
        stream->stopped = 1;
        return 0;
    }
    stream->received += bytes;
    return bytes;
}

static int console_download(void *user, const char *url, uint64_t limit, self_update_sink sink,
                            void *sink_user)
{
    static char current[SELF_UPDATE_MAX_URL]; /* one download at a time */
    int hop;
    (void)user;
    if (!self_update_url_allowed(url, 0))
        return -1;
    (void)snprintf(current, sizeof(current), "%s", url);
    for (hop = 0; hop <= self_update_redirects; ++hop)
    {
        stream_sink stream = {sink, sink_user, limit, 0, 0};
        long code = 0;
        char *next = NULL;
        CURLcode result;
        CURL *easy = new_request(current);
        if (easy == NULL)
            return -(10000 + (int)CURLE_FAILED_INIT);
        (void)curl_easy_setopt(easy, CURLOPT_BUFFERSIZE, 256L * 1024L);
        /* No overall limit for a large file: give up when nothing arrives for a while. */
        (void)curl_easy_setopt(easy, CURLOPT_LOW_SPEED_LIMIT, 1L);
        (void)curl_easy_setopt(easy, CURLOPT_LOW_SPEED_TIME, 30L);
        (void)curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, on_stream);
        (void)curl_easy_setopt(easy, CURLOPT_WRITEDATA, &stream);
        result = curl_easy_perform(easy);
        (void)curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &code);
        if (result == CURLE_OK && code >= 301 && code <= 308 && code != 304 && code != 305 &&
            code != 306 && curl_easy_getinfo(easy, CURLINFO_REDIRECT_URL, &next) == CURLE_OK &&
            next != NULL && self_update_url_allowed(next, 1) && stream.received == 0)
        {
            (void)snprintf(current, sizeof(current), "%s", next);
            curl_easy_cleanup(easy);
            continue;
        }
        curl_easy_cleanup(easy);
        if (result != CURLE_OK || stream.stopped)
            return -(10000 + (int)(result != CURLE_OK ? result : CURLE_WRITE_ERROR));
        return code == 200 ? 0 : -(int)code;
    }
    return -1; /* too many redirects */
}

/* ---- The signature ------------------------------------------------------------------------- */

static int console_verify(void *user, const unsigned char key[32],
                          const unsigned char signature[64], const void *message, size_t length)
{
    int valid = 0;
    EVP_PKEY *public_key = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, NULL, key, 32);
    EVP_MD_CTX *context = EVP_MD_CTX_new();
    (void)user;
    if (public_key != NULL && context != NULL &&
        EVP_DigestVerifyInit(context, NULL, NULL, NULL, public_key) == 1)
        valid =
            EVP_DigestVerify(context, signature, 64, (const unsigned char *)message, length) == 1;
    EVP_MD_CTX_free(context);
    EVP_PKEY_free(public_key);
    return valid;
}

/* ---- The helper ---------------------------------------------------------------------------- */

typedef struct net_address
{
    uint8_t length;
    uint8_t family;
    uint16_t port;
    uint32_t address;
    uint16_t virtual_port;
    uint8_t zero[6];
} net_address;

static long channel_send(void *user, const void *data, size_t size)
{
    return (long)sceNetSend((int)(intptr_t)user, data, size, 0);
}

static long channel_receive(void *user, void *data, size_t size)
{
    return (long)sceNetRecv((int)(intptr_t)user, data, size, 0);
}

static void channel_close(void *user)
{
    (void)sceNetSocketClose((int)(intptr_t)user);
}

/* The loader reads the program up to the end of its section table and gives the rest of the
 * connection to the program as its standard input and output. */
static int console_open_helper(void *user, self_update_channel *channel)
{
    static unsigned char buffer[65536];
    /* The helper reports five times a second while it works; this long a silence means it is
     * gone. */
    const int io_timeout_us = 60000000;
    const int connect_timeout_us = 5000000;
    const uint16_t port = SELF_UPDATE_LOADER_PORT;
    const net_address address = {sizeof(net_address), 2, (uint16_t)((port << 8) | (port >> 8)),
                                 0x0100007f,          0, {0}};
    int program;
    int socket;
    int sent = 0;
    (void)user;
    program = sceKernelOpen(SELF_UPDATE_HELPER_PATH, O_RDONLY, 0);
    if (program < 0)
        return 0;
    socket = sceNetSocket("self_update", 2, 1, 6);
    if (socket >= 0 &&
        sceNetSetsockopt(socket, 0xffff, 0x1105, &io_timeout_us, sizeof(io_timeout_us)) >= 0 &&
        sceNetSetsockopt(socket, 0xffff, 0x1106, &io_timeout_us, sizeof(io_timeout_us)) >= 0 &&
        sceNetSetsockopt(socket, 0xffff, 0x1109, &connect_timeout_us, sizeof(connect_timeout_us)) >=
            0 &&
        sceNetConnect(socket, &address, sizeof(address)) >= 0)
    {
        sent = 1;
        for (;;)
        {
            const int64_t count = sceKernelRead(program, buffer, sizeof(buffer));
            size_t done = 0;
            if (count == 0)
                break;
            if (count < 0)
            {
                sent = 0;
                break;
            }
            while (sent && done < (size_t)count)
            {
                const int moved = sceNetSend(socket, buffer + done, (size_t)count - done, 0);
                if (moved <= 0)
                    sent = 0;
                else
                    done += (size_t)moved;
            }
            if (!sent)
                break;
        }
    }
    (void)sceKernelClose(program);
    if (!sent)
    {
        if (socket >= 0)
            (void)sceNetSocketClose(socket);
        return 0;
    }
    channel->send = channel_send;
    channel->receive = channel_receive;
    channel->close = channel_close;
    channel->user = (void *)(intptr_t)socket;
    return 1;
}

/* ---- Kept between launches ----------------------------------------------------------------- */

static int console_load_sequence(void *user, uint64_t *sequence)
{
    char text[24];
    int64_t count;
    const int descriptor = sceKernelOpen(SELF_UPDATE_SEQUENCE_PATH, O_RDONLY, 0);
    (void)user;
    if (descriptor < 0)
        return 0;
    count = sceKernelRead(descriptor, text, sizeof(text) - 1);
    (void)sceKernelClose(descriptor);
    if (count <= 0)
        return 0;
    text[count] = '\0';
    *sequence = strtoull(text, NULL, 10);
    return 1;
}

static int console_save_sequence(void *user, uint64_t sequence)
{
    char text[24];
    int length;
    int64_t written;
    const int descriptor =
        sceKernelOpen(SELF_UPDATE_SEQUENCE_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    (void)user;
    if (descriptor < 0)
        return 0;
    length = snprintf(text, sizeof(text), "%llu\n", (unsigned long long)sequence);
    written = sceKernelWrite(descriptor, text, (size_t)length);
    (void)sceKernelClose(descriptor);
    return written == length;
}

static uint64_t console_now_ms(void *user)
{
    (void)user;
    return sceKernelGetProcessTime() / 1000u;
}

const self_update_platform *self_update_console(void)
{
    static const self_update_platform platform = {
        console_fetch,         console_download,      console_verify, console_open_helper,
        console_load_sequence, console_save_sequence, console_now_ms, NULL};
    return &platform;
}

self_update_check_result self_update_check_self(self_update_offer *offer)
{
    char title[10];
    char version[12];
    if (offer == NULL || !update_check_read_param(SELF_UPDATE_PARAM_PATH, title, version))
    {
        if (offer != NULL)
            memset(offer, 0, sizeof(*offer));
        return SELF_UPDATE_UNKNOWN;
    }
    (void)snprintf(self_update_agent, sizeof(self_update_agent), "homebrew-self-update/1 (%s)",
                   title);
    return self_update_check(self_update_console(), title, version, offer);
}
