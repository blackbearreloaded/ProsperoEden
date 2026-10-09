/*
 * ps5-native-app-boilerplate - Self-update for apps listed on homebrew.page.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Lets a listed app replace itself with its newest release:
 *
 *   1. self_update_check_self()  asks the catalog, verifies its signature, and
 *                                fills an offer when a newer release exists;
 *   2. the app asks its user;
 *   3. self_update_start()       downloads the release and hands it to the
 *                                helper, which checks and unpacks it;
 *   4. self_update_apply()       tells the helper to go ahead; the app then
 *                                closes itself, and the helper replaces the
 *                                app's files and posts a notification.
 *
 * The app stays in its sandbox and does the network transfer. The helper
 * (examples/self-update-helper, built as self-updater.elf and shipped in the
 * app's folder) is sent
 * to the console's payload loader and does the file work outside the sandbox.
 * With no loader listening, self_update_start() fails cleanly and the app can
 * fall back to telling the user about the update.
 *
 * C11; also compiles as C++. Needs update_check.{h,c}, console_curl.{h,c},
 * self_update_sha256.{h,c}, self_update_protocol.h and, on the console,
 * self_update_ps5.c. See docs/SELF_UPDATE.md.
 */
#ifndef PS5_BOILERPLATE_SELF_UPDATE_H
#define PS5_BOILERPLATE_SELF_UPDATE_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* The catalog's own site. Its mirror (UPDATE_CHECK_MIRROR_API) is asked when this gives no
 * catalog that verifies; see self_update_check. */
#define SELF_UPDATE_API "https://homebrew.page/api/v1/"
#define SELF_UPDATE_MAX_MANIFEST (512u * 1024u)
#define SELF_UPDATE_MAX_APP_FILE 65536u
/* GitHub's redirect to its release file host carries a signed query of about a thousand
 * characters. */
#define SELF_UPDATE_MAX_URL 4096u

    /* ---- What the console provides (and what tests replace) ------------------------------ */

    /* A connection to a helper that has just been started. send and receive return the bytes
     * moved, or 0 or less when the connection ended or timed out. */
    typedef struct self_update_channel
    {
        long (*send)(void *user, const void *data, size_t size);
        long (*receive)(void *user, void *data, size_t size);
        void (*close)(void *user);
        void *user;
    } self_update_channel;

    /* Receives a download in order. Returns 1 to go on, 0 to stop the transfer. */
    typedef int (*self_update_sink)(void *user, const void *data, size_t size);

    typedef struct self_update_platform
    {
        /* GET a small file into body. 0: an answer arrived (see *status); negative: failed. */
        int (*fetch)(void *user, const char *url, char *body, size_t capacity, size_t *length,
                     int *status);
        /* GET a large file into sink, refusing more than limit bytes. Redirects are followed
         * only where self_update_url_allowed() says so. 0: complete with status 200. */
        int (*download)(void *user, const char *url, uint64_t limit, self_update_sink sink,
                        void *sink_user);
        /* 1 when signature is the Ed25519 signature of message under key. */
        int (*verify)(void *user, const unsigned char key[32], const unsigned char signature[64],
                      const void *message, size_t length);
        /* Sends the helper to the payload loader. 1: channel is open. */
        int (*open_helper)(void *user, self_update_channel *channel);
        /* The highest catalog sequence accepted so far, kept across launches. 1: read/saved. */
        int (*load_sequence)(void *user, uint64_t *sequence);
        int (*save_sequence)(void *user, uint64_t sequence);
        /* Milliseconds from any fixed start, never going back. */
        uint64_t (*now_ms)(void *user);
        void *user;
    } self_update_platform;

    /* The console's own: libcurl, OpenSSL, the payload loader on loopback port 9021, and
     * /download0 for the sequence. Defined in self_update_ps5.c. */
    const self_update_platform *self_update_console(void);

    /* ---- 1. Is there an update? ------------------------------------------------------------ */

    typedef enum self_update_check_result
    {
        SELF_UPDATE_AVAILABLE = 0,      /* the offer is filled and verified */
        SELF_UPDATE_UP_TO_DATE = 1,     /* the catalog has nothing newer */
        SELF_UPDATE_UNKNOWN = 2,        /* no answer, or the app isn't listed: show nothing */
        SELF_UPDATE_UNTRUSTED = 3,      /* the catalog's signature or a file's hash didn't verify */
        SELF_UPDATE_NOT_INSTALLABLE = 4 /* newer, but not something this kit can install */
    } self_update_check_result;

    typedef struct self_update_offer
    {
        char title[10];
        char name[64];      /* the app's name in the catalog */
        char installed[12]; /* the running app's content version */
        char available[12]; /* the catalog's content version */
        char version[40];   /* the release's name for display, e.g. "1.4.0" */
        char artifact[512]; /* the release ZIP on GitHub */
        char sha256[65];    /* its SHA-256, from the signed catalog */
        uint64_t size;      /* its size in bytes; 0 when the catalog doesn't know */
        char page[160];     /* the app's page on homebrew.page */
        /* ProsperoEden: what the developer wrote on the release, as the catalog gives it
           (release_notes: plain text, lines split by \n, list items starting "- ", at most
           4,000 characters); empty when the release has none. notes_truncated: the catalog cut
           them, the rest is on the release's page. */
        char notes[16384];
        int notes_truncated;
    } self_update_offer;

    /* Blocking; call it from a worker thread, once per launch. */
    self_update_check_result self_update_check(const self_update_platform *platform,
                                               const char *title_id, const char *installed,
                                               self_update_offer *offer);

    /* The same for the running app, read from /app0/sce_sys/param.json (self_update_ps5.c). */
    self_update_check_result self_update_check_self(self_update_offer *offer);

    /* 1 when a download may be made from url. redirected: 0 for the address the catalog lists
     * (github.com only), 1 for where GitHub sends it (its release file host as well). */
    int self_update_url_allowed(const char *url, int redirected);

    /* ---- 2. The update itself --------------------------------------------------------------- */

    typedef enum self_update_phase
    {
        SELF_UPDATE_IDLE = 0,
        SELF_UPDATE_STARTING,    /* starting the helper */
        SELF_UPDATE_DOWNLOADING, /* done/total are bytes of the archive */
        SELF_UPDATE_UNPACKING,   /* done/total are bytes unpacked */
        SELF_UPDATE_READY,       /* staged: call self_update_apply() or self_update_cancel() */
        SELF_UPDATE_APPLYING,    /* the helper has the go-ahead: close the app now */
        SELF_UPDATE_CANCELLED,
        SELF_UPDATE_FAILED /* error says why; nothing was changed */
    } self_update_phase;

    typedef struct self_update_status
    {
        self_update_phase phase;
        uint64_t done;
        uint64_t total;     /* 0 while it isn't known */
        double rate;        /* bytes per second, smoothed; 0 until it is known */
        char time_left[32]; /* "about 20 s left"; empty until it is known */
        char error[160];
    } self_update_status;

    typedef struct self_update_job
    {
        /* Private. Zero it once (a static or `= {0}`) before self_update_start(). */
        const self_update_platform *platform;
        self_update_offer offer;
        self_update_channel channel;
        pthread_mutex_t lock;
        pthread_t thread;
        int started;
        int channel_open;
        int cancel;
        self_update_phase phase;
        uint64_t done;
        uint64_t total;
        char error[160];
        uint64_t rate_time;
        uint64_t rate_done;
        double rate;
        self_update_phase rate_phase;
    } self_update_job;

    /* Starts the download and staging on a thread of its own. 1: started. */
    int self_update_start(self_update_job *job, const self_update_platform *platform,
                          const self_update_offer *offer);

    /* Where the job is; cheap, for every frame. */
    void self_update_poll(self_update_job *job, self_update_status *status);

    /* Stops the job. Until self_update_apply() has returned 1, this leaves the app untouched. */
    void self_update_cancel(self_update_job *job);

    /* In phase READY: gives the helper the go-ahead. 1: the helper is waiting for the app to
     * close; end the app now (on the console: sceSystemServiceLoadExec("exit", NULL)). The
     * helper replaces the files once the app is gone and posts a notification. */
    int self_update_apply(self_update_job *job);

    /* Waits for the job's thread; call it after cancel or failure before reusing the job. */
    void self_update_finish(self_update_job *job);

    /* "about 20 s left" from bytes left and a speed; empty when it can't be said. */
    void self_update_time_left(uint64_t done, uint64_t total, double rate, char *out, size_t size);

#ifdef __cplusplus
}
#endif

#endif
