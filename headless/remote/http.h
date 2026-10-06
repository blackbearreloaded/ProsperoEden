/*
 * ProsperoEden - HTTP for the download sources (remote.h) that are web servers: one GET at a
 * time, to memory or to a file.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * libcurl as the update check uses it (headless/update_check/console_curl.c: the console's name
 * lookup, certificate list and non-blocking sockets). It is linked into the update check's object,
 * so it shares that object's libcurl and console_curl.c (headless/CMakeLists.txt). Servers on the
 * home network often have no certificate, so plain http is allowed besides https.
 */
#ifndef PROSPEROEDEN_REMOTE_HTTP_H
#define PROSPEROEDEN_REMOTE_HTTP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* Takes the next part of the answer's body; 1 to go on, 0 to stop the transfer. */
    typedef int (*remote_http_sink)(void *user, const void *data, size_t size);
    /* Asked now and then while the transfer runs: nonzero stops it (cancel, pause). */
    typedef int (*remote_http_stop)(void *user);
    /* Told the status and Content-Length (0: not said) before the first part of the body, so a
     * sink can tell a part (206) from the whole file (200) or an error page; 1 to go on. */
    typedef int (*remote_http_begin)(void *user, int status, uint64_t length);

    typedef struct remote_http_request
    {
        const char *url;
        const char *authorization; /* the whole header value ("Bearer ...", "Basic ..."); NULL: none */
        uint64_t resume_from;      /* nonzero: ask for the body from this byte on (Range) */
        long timeout_ms;           /* 0: no overall limit (a large file): only a stalled one ends */
        remote_http_begin begin;     /* NULL: not asked */
        remote_http_sink sink;
        remote_http_stop stop;       /* NULL: never stopped */
        void *user;
    } remote_http_request;

    typedef struct remote_http_result
    {
        int status;          /* the HTTP status; 0 when no answer came */
        int curl_code;       /* CURLcode, 0 when the transfer itself worked */
        int stopped;         /* the sink or stop ended it */
        uint64_t length;     /* Content-Length of the answer, 0 when it did not say */
        char error[160];     /* what went wrong, in English, for the log and the screen */
    } remote_http_result;

    /* Runs the request, blocking. Returns 0 when the whole answer arrived (any status), -1 when
     * the transfer failed or was stopped; result->status is the status of what did arrive. */
    int remote_http_get(const remote_http_request *request, remote_http_result *result);

    /* Writes the "Basic ..." value for a user name and password into out. 0 when it fits. */
    int remote_http_basic(const char *user, const char *password, char *out, size_t size);

    /* Percent-encodes a path segment (a file name) into out. 0 when it fits. */
    int remote_http_escape(const char *segment, char *out, size_t size);

#ifdef __cplusplus
}
#endif

#endif
