/*
 * ps5-native-app-boilerplate - What the app and the self-update helper say to each other.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The app sends the helper program to the console's payload loader; the same
 * connection then carries this conversation. Lines end with '\n'.
 *
 *   app     PSU1, update, <title ID>, <installed content version>,
 *           <new content version>, <size in bytes, 0 if unknown>, <sha256>,
 *           <app name>, <version name>            (nine lines)
 *   helper  ready                                 or  fail <reason>
 *   app     the archive in pieces: a four-byte little-endian length, then
 *           that many bytes (at most SELF_UPDATE_PIECE); a length of zero ends
 *   helper  p <bytes unpacked> <bytes to unpack>  five times a second
 *   helper  staged                                or  fail <reason>
 *   app     apply                                 or  cancel
 *   helper  applying
 *
 * After "applying" the app closes itself. The helper waits until the app is
 * gone, puts the new files in place and posts a notification. A connection
 * that ends before "apply", or "cancel" at any time before it, makes the
 * helper remove everything it wrote and leave the app untouched.
 */
#ifndef PS5_BOILERPLATE_SELF_UPDATE_PROTOCOL_H
#define PS5_BOILERPLATE_SELF_UPDATE_PROTOCOL_H

#define SELF_UPDATE_MAGIC "PSU1"
#define SELF_UPDATE_LINE 512u                 /* longest line either side accepts */
#define SELF_UPDATE_PIECE (1u << 20)          /* largest piece of the archive */
#define SELF_UPDATE_MAX_ARCHIVE 0x80000000ull /* 2 GiB, the catalog's own limit */
#define SELF_UPDATE_LOADER_PORT 9021

#endif
