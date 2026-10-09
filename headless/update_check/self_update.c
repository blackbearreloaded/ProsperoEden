/*
 * ps5-native-app-boilerplate - Self-update for apps listed on homebrew.page.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * See self_update.h. This file is C11 and also compiles as C++. It holds no
 * console calls: everything it needs from the console comes through
 * self_update_platform, so the same code runs in the host tests.
 *
 * Trust: the catalog's manifest is accepted only with a valid Ed25519
 * signature from one of the two catalog keys and a sequence that hasn't gone
 * back; the app's own file only when its SHA-256 is the one the manifest
 * lists; the release only when its size and SHA-256 are the ones that file
 * lists. The helper checks the release again before it touches anything.
 */
#include "self_update.h"

#include "update_check.h"
#include "self_update_protocol.h"
#include "self_update_sha256.h"

#include <stdio.h>
#include <string.h>

/* The catalog's signing keys (docs/api.md of ps5-homebrew-catalog): the one every deploy uses,
 * and the spare. A signature from either is accepted. */
static const unsigned char self_update_keys[2][32] = {
    {0x87, 0x39, 0x1b, 0xf1, 0x69, 0x8e, 0xce, 0xf1, 0x01, 0xbf, 0x5e,
     0x29, 0xdc, 0x85, 0x85, 0xee, 0x59, 0x47, 0xd5, 0x71, 0xe1, 0x94,
     0x70, 0xde, 0x74, 0x11, 0xc5, 0xd3, 0xb1, 0x37, 0xb5, 0xcf},
    {0x50, 0x9b, 0xcf, 0xab, 0x7e, 0xdf, 0xb4, 0xe5, 0xed, 0x23, 0x63,
     0x94, 0x88, 0x51, 0x7c, 0x6e, 0xf2, 0x65, 0x7c, 0x13, 0xb6, 0xb7,
     0xf2, 0xbf, 0x69, 0x9c, 0x89, 0x89, 0xd9, 0xb0, 0xdd, 0x7b}};

/* ---- Small readers ------------------------------------------------------------------------- */

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* The position just after `"key":` where the key starts a member (after '{' or ','), or NULL.
 * Used only on text that has already been verified against the catalog's signature. */
static const char *member(const char *json, size_t length, const char *key)
{
    const size_t key_length = strlen(key);
    size_t i;
    if (length < key_length + 3)
        return NULL;
    for (i = 1; i + key_length + 2 <= length; ++i)
    {
        size_t before = i;
        size_t after;
        if (json[i] != '"' || memcmp(json + i + 1, key, key_length) != 0 ||
            json[i + 1 + key_length] != '"')
            continue;
        while (before > 0 && is_space(json[before - 1]))
            --before;
        if (before == 0 || (json[before - 1] != '{' && json[before - 1] != ','))
            continue;
        after = i + key_length + 2;
        while (after < length && is_space(json[after]))
            ++after;
        if (after >= length || json[after] != ':')
            continue;
        ++after;
        while (after < length && is_space(json[after]))
            ++after;
        return after < length ? json + after : NULL;
    }
    return NULL;
}

/* A non-negative whole number member. 1: read. */
static int member_number(const char *json, size_t length, const char *key, uint64_t *out)
{
    const char *at = member(json, length, key);
    const char *end = json + length;
    uint64_t value = 0;
    int digits = 0;
    if (at == NULL)
        return 0;
    while (at < end && *at >= '0' && *at <= '9')
    {
        if (value > (UINT64_MAX - 9u) / 10u)
            return 0;
        value = value * 10u + (uint64_t)(*at - '0');
        ++at;
        ++digits;
    }
    if (digits == 0 || (at < end && (*at == '.' || *at == 'e' || *at == 'E')))
        return 0;
    *out = value;
    return 1;
}

/* A member whose value is a string of exactly 64 hexadecimal characters. 1: copied. */
static int member_digest(const char *json, size_t length, const char *key, char out[65])
{
    const char *at = member(json, length, key);
    size_t i;
    if (at == NULL || (size_t)(json + length - at) < 66 || at[0] != '"' || at[65] != '"')
        return 0;
    for (i = 0; i < 64; ++i)
    {
        const char c = at[1 + i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return 0;
        out[i] = c;
    }
    out[64] = '\0';
    return 1;
}

static int starts_with(const char *text, const char *prefix)
{
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

int self_update_url_allowed(const char *url, int redirected)
{
    size_t i;
    size_t length;
    if (url == NULL)
        return 0;
    length = strlen(url);
    if (length < 20 ||
        length >= (redirected ? SELF_UPDATE_MAX_URL : sizeof(((self_update_offer *)0)->artifact)))
        return 0;
    for (i = 0; i < length; ++i)
        if ((unsigned char)url[i] <= 0x20 || (unsigned char)url[i] >= 0x7f || url[i] == '\\' ||
            url[i] == '@')
            return 0;
    if (starts_with(url, "https://github.com/"))
        return 1;
    return redirected && starts_with(url, "https://release-assets.githubusercontent.com/");
}

void self_update_time_left(uint64_t done, uint64_t total, double rate, char *out, size_t size)
{
    double seconds;
    if (size == 0)
        return;
    out[0] = '\0';
    if (rate < 1024.0 || total <= done)
        return;
    seconds = (double)(total - done) / rate;
    if (seconds < 8.0)
        (void)snprintf(out, size, "a few seconds left");
    else if (seconds < 55.0)
        (void)snprintf(out, size, "about %d s left", ((int)((seconds + 4.999) / 5.0)) * 5);
    else if (seconds < 3600.0 * 3.0)
        (void)snprintf(out, size, "about %d min left", (int)((seconds + 59.999) / 60.0));
}

/* ---- 1. The check -------------------------------------------------------------------------- */

static void sha256_text(const void *data, size_t size, char text[65])
{
    self_update_sha256 hash;
    unsigned char digest[32];
    self_update_sha256_start(&hash);
    self_update_sha256_add(&hash, data, size);
    self_update_sha256_finish(&hash, digest);
    self_update_sha256_hex(digest, text);
}

static int same_digest(const char *left, const char *right)
{
    size_t i;
    unsigned difference = 0;
    for (i = 0; i < 64; ++i)
    {
        char a = left[i];
        char b = right[i];
        if (a >= 'A' && a <= 'F')
            a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'F')
            b = (char)(b - 'A' + 'a');
        difference |= (unsigned)(a ^ b);
    }
    return difference == 0;
}

/* ProsperoEden: whether a top-level member is the literal true (the catalog's own JSON, whose
   bytes the signed manifest vouches for). */
static int member_true(const char *json, size_t length, const char *key)
{
    char pattern[80];
    const char *at;
    const char *end = json + length;
    (void)snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    for (at = json; at + strlen(pattern) <= end; ++at)
    {
        if (memcmp(at, pattern, strlen(pattern)) != 0)
            continue;
        at += strlen(pattern);
        while (at < end && (*at == ' ' || *at == '\t' || *at == '\r' || *at == '\n'))
            ++at;
        if (at >= end || *at != ':')
            return 0;
        ++at;
        while (at < end && (*at == ' ' || *at == '\t' || *at == '\r' || *at == '\n'))
            ++at;
        return end - at >= 4 && memcmp(at, "true", 4) == 0;
    }
    return 0;
}

/* The check against one place the catalog is published at. */
static self_update_check_result check_at(const self_update_platform *platform, const char *title_id,
                                         const char *installed, self_update_offer *offer,
                                         const char *api)
{
    /* One check at a time: the buffers are shared. */
    static char manifest[SELF_UPDATE_MAX_MANIFEST];
    static char body[SELF_UPDATE_MAX_APP_FILE];
    unsigned char signature[80];
    char url[160];
    char address[160];
    char key[32];
    char listed[65];
    char actual[65];
    char format[16];
    char status_text[24];
    size_t manifest_length = 0;
    size_t signature_length = 0;
    size_t body_length = 0;
    size_t used;
    int status = 0;
    uint64_t sequence = 0;
    uint64_t highest = 0;
    update_check_result result;

    if (offer == NULL)
        return SELF_UPDATE_UNKNOWN;
    memset(offer, 0, sizeof(*offer));
    if (platform == NULL || title_id == NULL || installed == NULL)
        return SELF_UPDATE_UNKNOWN;
    used = update_check_url(url, sizeof(url), title_id);
    if (used == 0 || api == NULL) /* also refuses anything that isn't a title ID */
        return SELF_UPDATE_UNKNOWN;
    if (snprintf(url, sizeof(url), "%sapps/%s.json", api, title_id) >= (int)sizeof(url))
        return SELF_UPDATE_UNKNOWN;

    /* The manifest and its signature. */
    (void)snprintf(address, sizeof(address), "%smanifest.json", api);
    if (platform->fetch(platform->user, address, manifest, sizeof(manifest), &manifest_length,
                        &status) != 0 ||
        status != 200)
        return SELF_UPDATE_UNKNOWN;
    status = 0;
    (void)snprintf(address, sizeof(address), "%smanifest.sig", api);
    if (platform->fetch(platform->user, address, (char *)signature, sizeof(signature),
                        &signature_length, &status) != 0 ||
        status != 200)
        return SELF_UPDATE_UNKNOWN;
    if (signature_length != 64 || (platform->verify(platform->user, self_update_keys[0], signature,
                                                    manifest, manifest_length) != 1 &&
                                   platform->verify(platform->user, self_update_keys[1], signature,
                                                    manifest, manifest_length) != 1))
        return SELF_UPDATE_UNTRUSTED;
    if (!member_number(manifest, manifest_length, "sequence", &sequence))
        return SELF_UPDATE_UNTRUSTED;
    if (platform->load_sequence != NULL && platform->load_sequence(platform->user, &highest) == 1 &&
        sequence < highest)
        return SELF_UPDATE_UNTRUSTED; /* an older catalog than one already accepted */

    /* The app's own file, which must be the one the manifest lists. */
    (void)snprintf(key, sizeof(key), "apps/%s.json", title_id);
    if (!member_digest(manifest, manifest_length, key, listed))
        return SELF_UPDATE_UNKNOWN; /* not in the catalog */
    status = 0;
    if (platform->fetch(platform->user, url, body, sizeof(body), &body_length, &status) != 0 ||
        status != 200)
        return SELF_UPDATE_UNKNOWN;
    sha256_text(body, body_length, actual);
    if (!same_digest(listed, actual))
        return SELF_UPDATE_UNTRUSTED;
    if (platform->save_sequence != NULL && sequence > highest)
        (void)platform->save_sequence(platform->user, sequence);

    update_check_evaluate(body, body_length, installed, &result);
    if (result.state == UPDATE_CHECK_UP_TO_DATE)
        return SELF_UPDATE_UP_TO_DATE;
    if (result.state != UPDATE_CHECK_AVAILABLE)
        return SELF_UPDATE_UNKNOWN;

    (void)snprintf(offer->title, sizeof(offer->title), "%s", title_id);
    (void)snprintf(offer->installed, sizeof(offer->installed), "%s", installed);
    (void)snprintf(offer->available, sizeof(offer->available), "%s", result.available);
    (void)snprintf(offer->version, sizeof(offer->version), "%s", result.version);
    (void)snprintf(offer->page, sizeof(offer->page), "%s", result.page);
    if (update_check_json_string(body, body_length, "name", offer->name, sizeof(offer->name)) != 1)
        (void)snprintf(offer->name, sizeof(offer->name), "%s", title_id);
    /* Only a released ZIP on GitHub with a digest can be installed. */
    if (update_check_json_string(body, body_length, "status", status_text, sizeof(status_text)) !=
            1 ||
        strcmp(status_text, "available") != 0 ||
        update_check_json_string(body, body_length, "format", format, sizeof(format)) != 1 ||
        strcmp(format, "zip") != 0 ||
        update_check_json_string(body, body_length, "artifact_url", offer->artifact,
                                 sizeof(offer->artifact)) != 1 ||
        !self_update_url_allowed(offer->artifact, 0) ||
        !member_digest(body, body_length, "sha256", offer->sha256))
        return SELF_UPDATE_NOT_INSTALLABLE;
    if (!member_number(body, body_length, "size", &offer->size))
        offer->size = 0; /* null: the catalog doesn't know it */
    /* ProsperoEden: the release notes, covered by the same signature (null or missing: none). */
    if (update_check_json_string(body, body_length, "release_notes", offer->notes, sizeof(offer->notes)) != 1)
        offer->notes[0] = '\0';
    offer->notes_truncated = member_true(body, body_length, "release_notes_truncated");
    if (offer->size > SELF_UPDATE_MAX_ARCHIVE)
        return SELF_UPDATE_NOT_INSTALLABLE;
    return SELF_UPDATE_AVAILABLE;
}

self_update_check_result self_update_check(const self_update_platform *platform,
                                           const char *title_id, const char *installed,
                                           self_update_offer *offer)
{
    /* The catalog's own site first, then its mirror. A place counts only when its manifest is
     * signed by the catalog's keys, is not older than one already accepted, and lists the very
     * file it then serves: the mirror can make the catalog reachable and nothing else. An
     * unreachable site and a network's block page both end up here as "no answer". When neither
     * place answers, the result is the site's. */
    self_update_check_result first = SELF_UPDATE_UNKNOWN;
    int origin;
    for (origin = 0; origin < UPDATE_CHECK_ORIGINS; ++origin)
    {
        const self_update_check_result result =
            check_at(platform, title_id, installed, offer, update_check_api(origin));
        if (result == SELF_UPDATE_AVAILABLE || result == SELF_UPDATE_UP_TO_DATE ||
            result == SELF_UPDATE_NOT_INSTALLABLE)
            return result;
        if (origin == 0)
            first = result;
    }
    if (offer != NULL)
        memset(offer, 0, sizeof(*offer));
    return first;
}

/* ---- 2. The job ---------------------------------------------------------------------------- */

static void set_phase(self_update_job *job, self_update_phase phase)
{
    pthread_mutex_lock(&job->lock);
    if (job->phase != SELF_UPDATE_CANCELLED && job->phase != SELF_UPDATE_FAILED)
        job->phase = phase;
    pthread_mutex_unlock(&job->lock);
}

static void set_progress(self_update_job *job, uint64_t done, uint64_t total)
{
    pthread_mutex_lock(&job->lock);
    job->done = done;
    job->total = total;
    pthread_mutex_unlock(&job->lock);
}

static int cancelled(self_update_job *job)
{
    int value;
    pthread_mutex_lock(&job->lock);
    value = job->cancel;
    pthread_mutex_unlock(&job->lock);
    return value;
}

/* Ends the job: cancelled if that was asked, failed with the reason otherwise. Closing the
 * connection before "apply" is what makes the helper undo its work. */
static void stop(self_update_job *job, const char *reason)
{
    pthread_mutex_lock(&job->lock);
    if (job->cancel)
        job->phase = SELF_UPDATE_CANCELLED;
    else
    {
        job->phase = SELF_UPDATE_FAILED;
        (void)snprintf(job->error, sizeof(job->error), "%s", reason);
    }
    if (job->channel_open)
    {
        job->channel_open = 0;
        pthread_mutex_unlock(&job->lock);
        job->channel.close(job->channel.user);
        return;
    }
    pthread_mutex_unlock(&job->lock);
}

static int send_all(self_update_job *job, const void *data, size_t size)
{
    const char *bytes = (const char *)data;
    while (size != 0)
    {
        const long count = job->channel.send(job->channel.user, bytes, size);
        if (count <= 0)
            return 0;
        bytes += count;
        size -= (size_t)count;
    }
    return 1;
}

/* One line from the helper, without its end. 0: the connection ended or the line is too long. */
static int read_line(self_update_job *job, char *line, size_t size)
{
    size_t used = 0;
    for (;;)
    {
        char byte = 0;
        if (job->channel.receive(job->channel.user, &byte, 1) <= 0)
            return 0;
        if (byte == '\n')
        {
            line[used] = '\0';
            return 1;
        }
        if (used + 1 >= size)
            return 0;
        line[used++] = byte;
    }
}

typedef struct download_state
{
    self_update_job *job;
    self_update_sha256 hash;
    uint64_t received;
    int helper_lost;
} download_state;

static int on_download(void *user, const void *data, size_t size)
{
    download_state *state = (download_state *)user;
    const unsigned char *bytes = (const unsigned char *)data;
    if (cancelled(state->job))
        return 0;
    self_update_sha256_add(&state->hash, data, size);
    while (size != 0)
    {
        const size_t piece = size < SELF_UPDATE_PIECE ? size : SELF_UPDATE_PIECE;
        const unsigned char header[4] = {(unsigned char)piece, (unsigned char)(piece >> 8),
                                         (unsigned char)(piece >> 16),
                                         (unsigned char)(piece >> 24)};
        if (!send_all(state->job, header, sizeof(header)) || !send_all(state->job, bytes, piece))
        {
            state->helper_lost = 1;
            return 0;
        }
        bytes += piece;
        size -= piece;
        state->received += piece;
    }
    set_progress(state->job, state->received, state->job->offer.size);
    return 1;
}

static void *run_job(void *argument)
{
    self_update_job *job = (self_update_job *)argument;
    const self_update_offer *offer = &job->offer;
    static const unsigned char end_mark[4] = {0, 0, 0, 0};
    char line[SELF_UPDATE_LINE];
    char request[1024];
    download_state state;
    unsigned char digest[32];
    int length;

    if (job->platform->open_helper(job->platform->user, &job->channel) != 1)
    {
        stop(job, "The update helper couldn't be started. Is the payload loader running?");
        return NULL;
    }
    pthread_mutex_lock(&job->lock);
    job->channel_open = 1;
    pthread_mutex_unlock(&job->lock);

    length = snprintf(request, sizeof(request), "%s\nupdate\n%s\n%s\n%s\n%llu\n%s\n%s\n%s\n",
                      SELF_UPDATE_MAGIC, offer->title, offer->installed, offer->available,
                      (unsigned long long)offer->size, offer->sha256, offer->name, offer->version);
    if (length <= 0 || (size_t)length >= sizeof(request) ||
        !send_all(job, request, (size_t)length) || !read_line(job, line, sizeof(line)))
    {
        stop(job, "The update helper didn't answer");
        return NULL;
    }
    if (strcmp(line, "ready") != 0)
    {
        stop(job, starts_with(line, "fail ") ? line + 5 : "The update helper refused the update");
        return NULL;
    }

    set_phase(job, SELF_UPDATE_DOWNLOADING);
    set_progress(job, 0, offer->size);
    memset(&state, 0, sizeof(state));
    state.job = job;
    self_update_sha256_start(&state.hash);
    if (job->platform->download(job->platform->user, offer->artifact,
                                offer->size != 0 ? offer->size : SELF_UPDATE_MAX_ARCHIVE,
                                on_download, &state) != 0)
    {
        stop(job, state.helper_lost ? "The update helper stopped" : "The download failed");
        return NULL;
    }
    self_update_sha256_finish(&state.hash, digest);
    if ((offer->size != 0 && state.received != offer->size) ||
        !self_update_sha256_matches(digest, offer->sha256))
    {
        stop(job, "The download doesn't match the catalog's listing");
        return NULL;
    }
    if (cancelled(job) || !send_all(job, end_mark, sizeof(end_mark)))
    {
        stop(job, "The update helper stopped");
        return NULL;
    }

    set_phase(job, SELF_UPDATE_UNPACKING);
    set_progress(job, 0, 0);
    for (;;)
    {
        /* The helper reports five times a second, so a cancel is seen within that. */
        if (cancelled(job))
        {
            (void)send_all(job, "cancel\n", 7);
            stop(job, "");
            return NULL;
        }
        if (!read_line(job, line, sizeof(line)))
        {
            stop(job, "The update helper stopped");
            return NULL;
        }
        if (line[0] == 'p' && line[1] == ' ')
        {
            unsigned long long done = 0;
            unsigned long long total = 0;
            if (sscanf(line + 2, "%llu %llu", &done, &total) == 2)
                set_progress(job, done, total);
            continue;
        }
        if (strcmp(line, "staged") == 0)
            break;
        stop(job, starts_with(line, "fail ") ? line + 5 : "The update could not be unpacked");
        return NULL;
    }
    if (cancelled(job))
    {
        stop(job, "");
        return NULL;
    }
    set_phase(job, SELF_UPDATE_READY);
    return NULL;
}

int self_update_start(self_update_job *job, const self_update_platform *platform,
                      const self_update_offer *offer)
{
    if (job == NULL || platform == NULL || offer == NULL || job->started)
        return 0;
    memset(job, 0, sizeof(*job));
    job->platform = platform;
    job->offer = *offer;
    job->phase = SELF_UPDATE_STARTING;
    if (pthread_mutex_init(&job->lock, NULL) != 0)
        return 0;
    if (pthread_create(&job->thread, NULL, run_job, job) != 0)
    {
        (void)pthread_mutex_destroy(&job->lock);
        job->phase = SELF_UPDATE_IDLE;
        return 0;
    }
    job->started = 1;
    return 1;
}

void self_update_poll(self_update_job *job, self_update_status *status)
{
    uint64_t now;
    memset(status, 0, sizeof(*status));
    if (job == NULL || !job->started)
        return;
    now = job->platform->now_ms(job->platform->user);
    pthread_mutex_lock(&job->lock);
    /* The speed is measured over half-second steps and smoothed, so the time left settles
     * instead of jumping with every block that arrives. */
    if (job->phase != job->rate_phase || job->done < job->rate_done)
    {
        job->rate_phase = job->phase;
        job->rate = 0.0;
        job->rate_time = now;
        job->rate_done = job->done;
    }
    else if (now - job->rate_time >= 500u)
    {
        const double speed =
            (double)(job->done - job->rate_done) * 1000.0 / (double)(now - job->rate_time);
        job->rate = job->rate > 0.0 ? job->rate * 0.75 + speed * 0.25 : speed;
        job->rate_time = now;
        job->rate_done = job->done;
    }
    status->phase = job->phase;
    status->done = job->done;
    status->total = job->total;
    status->rate = job->rate;
    memcpy(status->error, job->error, sizeof(status->error));
    pthread_mutex_unlock(&job->lock);
    if (status->phase == SELF_UPDATE_DOWNLOADING || status->phase == SELF_UPDATE_UNPACKING)
        self_update_time_left(status->done, status->total, status->rate, status->time_left,
                              sizeof(status->time_left));
}

void self_update_cancel(self_update_job *job)
{
    int close_channel = 0;
    if (job == NULL || !job->started)
        return;
    pthread_mutex_lock(&job->lock);
    if (job->phase != SELF_UPDATE_APPLYING && job->phase != SELF_UPDATE_FAILED)
    {
        job->cancel = 1;
        /* Staged and waiting: nothing is running that would notice, so end it here. */
        if (job->phase == SELF_UPDATE_READY)
        {
            job->phase = SELF_UPDATE_CANCELLED;
            close_channel = job->channel_open;
            job->channel_open = 0;
        }
    }
    pthread_mutex_unlock(&job->lock);
    if (close_channel)
    {
        (void)job->channel.send(job->channel.user, "cancel\n", 7);
        job->channel.close(job->channel.user);
    }
}

int self_update_apply(self_update_job *job)
{
    char line[SELF_UPDATE_LINE];
    int ready;
    if (job == NULL || !job->started)
        return 0;
    pthread_mutex_lock(&job->lock);
    ready = job->phase == SELF_UPDATE_READY && job->channel_open;
    pthread_mutex_unlock(&job->lock);
    if (!ready)
        return 0;
    if (!send_all(job, "apply\n", 6) || !read_line(job, line, sizeof(line)) ||
        strcmp(line, "applying") != 0)
    {
        stop(job, "The update helper stopped");
        return 0;
    }
    /* The connection stays open: the app closing is what the helper waits for next. */
    set_phase(job, SELF_UPDATE_APPLYING);
    return 1;
}

void self_update_finish(self_update_job *job)
{
    if (job == NULL || !job->started)
        return;
    (void)pthread_join(job->thread, NULL);
    pthread_mutex_lock(&job->lock);
    if (job->channel_open && job->phase != SELF_UPDATE_APPLYING)
    {
        job->channel_open = 0;
        pthread_mutex_unlock(&job->lock);
        job->channel.close(job->channel.user);
    }
    else
        pthread_mutex_unlock(&job->lock);
    (void)pthread_mutex_destroy(&job->lock);
    job->started = 0;
}
