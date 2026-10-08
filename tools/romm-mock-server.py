#!/usr/bin/env python3
# ProsperoEden - A small stand-in for a RomM server, for tools/check-remote.py.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
"""romm-mock-server.py <port file>

Serves the parts of RomM's API that headless/remote/romm/ uses, on 127.0.0.1 and a free port it
writes to <port file>: /api/platforms, /api/roms (a page), /api/roms/<file id>/files/content/<name> (with
Range) and a cover. It asks for "Authorization: Bearer rmm_test" or "Basic" me:secret. Files are
made of a pattern of their id, so the check can tell every byte. ROM 13 is sent slowly (to cancel
it), ROM 11's game file ignores Range (as a server that cannot resume), ROM 14's update is larger
than its game and comes with a mod, and ROM 15 is an update on its own. ROM 10 has a ScreenScraper
id (ROM 16 is a second copy of it) and ROM 11 a title ID; asked as localhost, ROM 10's file has
another name. A page has three games at most, whatever was asked for.
"""

import base64
import http.server
import json
import re
import sys
import threading
import time
import urllib.parse
import zlib
import struct

TOKEN = "Bearer rmm_test"
BASIC = "Basic " + base64.b64encode(b"me:secret").decode()


def pattern(file_id, size):
    return bytes(((i * 7 + file_id) & 0xFF) for i in range(size))


def png():
    # A 2x2 red picture.
    raw = b"".join(b"\x00" + b"\xff\x00\x00" * 2 for _ in range(2))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 2, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


# file id -> (name, size, category as RomM gives it; None at the top of a ROM reads as the game)
FILES = {
    100: ("Alpha Quest [0100000000010000][v0].nsp", 10_000_000, None),
    110: ("Beta Racer.xci", 6_000_000, "game"),
    111: ("Beta Racer [UPD][v65536].nsp", 100_000, "update"),
    120: ("Gamma.nsz", 1000, "game"),
    130: ("Slow Delta.nsp", 20_000_000, "game"),
    140: ("Epsilon.nsp", 50_000, "game"),
    141: ("Epsilon [UPD][v131072].nsp", 150_000, "update"),
    142: ("Epsilon [DLC].nsp", 20_000, "dlc"),
    143: ("Epsilon Mod.nsp", 10_000, "mod"),
    150: ("Zeta [UPD][v65536].nsp", 40_000, "update"),
    160: ("Alpha Quest (Rev 1).nsp", 1000, "game"),
}
ROMS = [
    {"id": 10, "name": "Alpha Quest", "fs_name": FILES[100][0], "fs_size_bytes": FILES[100][1], "files": [100],
     "path_cover_small": "/assets/romm/resources/roms/4/10/cover/small.png?ts=2026-10-01 12:00:00",
     "is_identified": True, "ss_id": 1000, "igdb_id": None},
    {"id": 11, "name": "Beta Racer", "fs_name": "Beta Racer", "fs_size_bytes": 6_100_000, "files": [110, 111],
     "path_cover_small": "", "is_identified": True, "title_id": "0100000000011000"},
    {"id": 12, "name": "Gamma", "fs_name": FILES[120][0], "fs_size_bytes": 1000, "files": [120]},
    {"id": 13, "name": None, "fs_name": FILES[130][0], "fs_name_no_ext": "Slow Delta", "fs_size_bytes": FILES[130][1],
     "files": [130]},
    # Its update is larger than the game, and a mod comes along that is not downloaded.
    {"id": 14, "name": "Epsilon", "fs_name": "Epsilon", "fs_size_bytes": 230_000, "files": [140, 141, 142, 143]},
    # An update on its own: no game to list.
    {"id": 15, "name": "Zeta Update", "fs_name": FILES[150][0], "fs_size_bytes": 40_000, "files": [150]},
    # A second copy of ROM 10's game on the same server.
    {"id": 16, "name": "Alpha Quest", "fs_name": FILES[160][0], "fs_size_bytes": 1000, "files": [160],
     "is_identified": True, "ss_id": 1000},
]
# The most games a page has, whatever was asked for (as a server or a proxy may cap it).
MOST_PER_PAGE = 3
NO_RANGE = {110}
# Asked as "localhost" (the check's second source), ROM 10's file has another name: the same game
# all the same, by its ScreenScraper id.
OTHER_NAMES = {100: "Alpha Quest (Office).nsp"}


def file_name(file_id, host):
    if host.startswith("localhost") and file_id in OTHER_NAMES:
        return OTHER_NAMES[file_id]
    return FILES[file_id][0]
SLOW = {130}
requests = []


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def send(self, status, body, kind="application/json", headers=()):
        self.send_response(status)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        for name, value in headers:
            self.send_header(name, value)
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        url = urllib.parse.urlparse(self.path)
        query = urllib.parse.parse_qs(url.query)
        requests.append((url.path, self.headers.get("Range")))
        if url.path == "/assets/romm/resources/roms/4/10/cover/small.png":
            return self.send(200, png(), "image/png")
        if self.headers.get("Authorization") not in (TOKEN, BASIC):
            return self.send(401, b'{"detail":"Unauthorized"}')
        if url.path == "/api/platforms":
            return self.send(200, json.dumps([{"id": 2, "slug": "gba", "fs_slug": "gba"},
                                              {"id": 4, "slug": "switch", "fs_slug": "switch"}]).encode())
        if url.path == "/api/roms":
            if query.get("platform_ids") != ["4"]:
                return self.send(200, json.dumps({"items": [], "total": 0, "limit": 50, "offset": 0}).encode())
            limit = min(int(query.get("limit", ["50"])[0]), MOST_PER_PAGE)
            offset = int(query.get("offset", ["0"])[0])
            items = []
            for rom in ROMS:
                item = dict(rom)
                item["files"] = [{"id": f, "file_name": file_name(f, self.headers.get("Host", "")), "file_size_bytes": FILES[f][1],
                                  "category": FILES[f][2], "is_top_level": FILES[f][2] in (None, "game")}
                                 for f in rom["files"]]
                items.append(item)
            page = items[offset:offset + limit]
            return self.send(200, json.dumps({"items": page, "total": len(items), "limit": limit, "offset": offset}).encode())
        match = re.fullmatch(r"/api/roms/(\d+)/files/content/(.+)", url.path)
        if match:
            file_id = int(match.group(1))
            if file_id not in FILES:
                return self.send(404, b'{"detail":"File not found"}')
            _, size, _ = FILES[file_id]
            name = file_name(file_id, self.headers.get("Host", ""))
            if urllib.parse.unquote(match.group(2)) != name:
                return self.send(404, b'{"detail":"wrong name"}')
            data = pattern(file_id, size)
            start = 0
            ranged = self.headers.get("Range")
            if ranged and file_id not in NO_RANGE:
                start = int(re.fullmatch(r"bytes=(\d+)-", ranged).group(1))
                if start >= size:
                    return self.send(416, b"", headers=[("Content-Range", f"bytes */{size}")])
            body = data[start:]
            self.send_response(206 if start else 200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(body)))
            if start:
                self.send_header("Content-Range", f"bytes {start}-{size - 1}/{size}")
            self.end_headers()
            step = 65536
            for at in range(0, len(body), step):
                try:
                    self.wfile.write(body[at:at + step])
                except (BrokenPipeError, ConnectionResetError):
                    return
                if file_id in SLOW:
                    time.sleep(0.05)
            return
        if url.path == "/requests":
            return self.send(200, json.dumps(requests).encode())
        return self.send(404, b'{"detail":"Not Found"}')


def main():
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    server.daemon_threads = True
    with open(sys.argv[1], "w") as out:
        out.write(str(server.server_address[1]))
    server.serve_forever()


if __name__ == "__main__":
    main()
