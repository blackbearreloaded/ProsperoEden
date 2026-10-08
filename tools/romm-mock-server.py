#!/usr/bin/env python3
# ProsperoEden - A stand-in for a RomM server, for the cases a real one does not show on demand.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
"""romm-mock-server.py <port file>

For tools/check-remote.py: what headless/remote_check.cpp checks that a real RomM (tools/check-romm.py)
cannot be made to show. Serves /api/platforms, /api/roms (a page) and
/api/roms/<file id>/files/content/<name> (with Range) on 127.0.0.1 and a free port it writes to
<port file>, for "Authorization: Bearer rmm_test". Files are made of a pattern of their id, so the
check can tell every byte.

- A page has three games at most, whatever was asked for (as a server or a proxy may cap it).
- ROM 10 has a ScreenScraper id and ROM 16 is a second copy of it; ROM 11 has a title ID (RomM
  needs metadata providers and the Switch's keys for those). Asked as localhost, ROM 10's file has
  another name: the same game on a second source under another file name.
- ROM 11's game file ignores Range (a server that cannot resume).
- ROM 14's update is larger than its game, and a mod comes along; ROM 12 is a .nsz and ROM 15 an
  update on its own (no games for the console).
"""

import http.server
import json
import re
import sys
import urllib.parse

TOKEN = "Bearer rmm_test"


def pattern(file_id, size):
    return bytes(((i * 7 + file_id) & 0xFF) for i in range(size))


# file id -> (name, size, category as RomM gives it; None at the top of a ROM reads as the game)
FILES = {
    100: ("Alpha Quest [0100000000010000][v0].nsp", 10_000_000, None),
    110: ("Beta Racer.xci", 6_000_000, "game"),
    111: ("Beta Racer [UPD][v65536].nsp", 100_000, "update"),
    120: ("Gamma.nsz", 1000, "game"),
    130: ("Delta.nsp", 1000, "game"),
    140: ("Epsilon.nsp", 50_000, "game"),
    141: ("Epsilon [UPD][v131072].nsp", 150_000, "update"),
    142: ("Epsilon [DLC].nsp", 20_000, "dlc"),
    143: ("Epsilon Mod.nsp", 10_000, "mod"),
    150: ("Zeta [UPD][v65536].nsp", 40_000, "update"),
    160: ("Alpha Quest (Rev 1).nsp", 1000, "game"),
}
ROMS = [
    {"id": 10, "name": "Alpha Quest", "fs_name": FILES[100][0], "files": [100], "is_identified": True,
     "ss_id": 1000, "igdb_id": None},
    {"id": 11, "name": "Beta Racer", "fs_name": "Beta Racer", "files": [110, 111], "is_identified": True,
     "title_id": "0100000000011000"},
    {"id": 12, "name": "Gamma", "fs_name": FILES[120][0], "files": [120]},
    {"id": 13, "name": None, "fs_name": FILES[130][0], "fs_name_no_ext": "Delta", "files": [130]},
    {"id": 14, "name": "Epsilon", "fs_name": "Epsilon", "files": [140, 141, 142, 143]},
    {"id": 15, "name": "Zeta Update", "fs_name": FILES[150][0], "files": [150]},
    {"id": 16, "name": "Alpha Quest", "fs_name": FILES[160][0], "files": [160], "is_identified": True,
     "ss_id": 1000},
]
MOST_PER_PAGE = 3
NO_RANGE = {110}
OTHER_NAMES = {100: "Alpha Quest (Office).nsp"}


def file_name(file_id, host):
    if host.startswith("localhost") and file_id in OTHER_NAMES:
        return OTHER_NAMES[file_id]
    return FILES[file_id][0]


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
        host = self.headers.get("Host", "")
        if self.headers.get("Authorization") != TOKEN:
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
                item["files"] = [{"id": f, "file_name": file_name(f, host), "file_size_bytes": FILES[f][1],
                                  "category": FILES[f][2], "is_top_level": FILES[f][2] in (None, "game")}
                                 for f in rom["files"]]
                items.append(item)
            page = items[offset:offset + limit]
            return self.send(200, json.dumps({"items": page, "total": len(items), "limit": limit,
                                              "offset": offset}).encode())
        match = re.fullmatch(r"/api/roms/(\d+)/files/content/(.+)", url.path)
        if match:
            file_id = int(match.group(1))
            if file_id not in FILES or urllib.parse.unquote(match.group(2)) != file_name(file_id, host):
                return self.send(404, b'{"detail":"File not found"}')
            size = FILES[file_id][1]
            start = 0
            ranged = self.headers.get("Range")
            if ranged and file_id not in NO_RANGE:
                start = int(re.fullmatch(r"bytes=(\d+)-", ranged).group(1))
                if start >= size:
                    return self.send(416, b"", headers=[("Content-Range", f"bytes */{size}")])
            body = pattern(file_id, size)[start:]
            headers = [("Content-Range", f"bytes {start}-{size - 1}/{size}")] if start else []
            try:
                return self.send(206 if start else 200, body, "application/octet-stream", headers)
            except (BrokenPipeError, ConnectionResetError):
                return None
        return self.send(404, b'{"detail":"Not Found"}')


def main():
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    server.daemon_threads = True
    with open(sys.argv[1], "w") as out:
        out.write(str(server.server_address[1]))
    server.serve_forever()


if __name__ == "__main__":
    main()
