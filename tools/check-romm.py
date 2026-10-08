#!/usr/bin/env python3
# ProsperoEden - Check of the save sync and the RomM backends against real RomM servers in Docker.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
"""check-romm.py [RomM version ...]

For each RomM version (default: the oldest the app takes, Eden::Remote::Romm::kMinimumVersion,
the newest it was checked with, and one older than the oldest, which is to be refused), starts
a RomM in Docker (tools/romm-test/docker-compose.yml) with a few fake Switch games, makes an
admin and a player with client API tokens, scans the library, and runs
headless/romm_live_check.cpp against it as the player, its downloads written by ftpsrv, the
console's FTP server, built for Linux in a container (tools/romm-test/ftpsrv.Dockerfile); then
takes it all down again. The first run of a version downloads its image (about 1 GB).

Needs Docker with Compose, and what tools/check-remote.py needs to build (libcurl's, OpenSSL's
and nlohmann/json's headers; CURL_INCLUDE, JSON_INCLUDE and CURL_LIBRARY as there). Without
Docker or the headers it says so and is skipped. It runs only when asked: with ROMM_CHECK=1 (also from make test)
or with versions as arguments; ROMM_KEEP=1 leaves the servers running (their port is printed).
"""

import hashlib
import importlib.util
import json
import os
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
import zlib
from base64 import b64encode
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADLESS = ROOT / "headless"
COMPOSE = ROOT / "tools/romm-test/docker-compose.yml"
SCAN = ROOT / "tools/romm-test/scan.py"
MINIZ = ROOT / "third_party/miniz"
# The newest RomM the save sync was checked with, and one older than kMinimumVersion.
NEWEST = "5.3.1"
TOO_OLD = "4.9.0"
SCOPES = ["platforms.read", "roms.read", "assets.read", "assets.write", "devices.read", "devices.write", "me.read"]

# The fake games: a game with its title ID in its name (and a cover set in RomM), one in a folder
# with its update, DLC, a mod and a manual, one more, a large one (downloaded, stopped part way and
# gone on with), and what is no game for the console: a .nsz and an update on its own. The NSPs and
# XCIs are laid out as real ones, their NCAs named after their SHA-256 (package()), so that the
# check of a download's contents (remote/stream_check.h) has something to check.
GAMES = {
    "Alpha Quest [0100000000010000][v0].nsp": 300_000,
    "Beta Racer/Beta Racer.xci": 6 << 20,
    "Beta Racer/update/Beta Racer [UPD][v65536].nsp": 500_000,
    "Beta Racer/dlc/Beta Racer [DLC].nsp": 200_000,
    "Beta Racer/mod/Beta Racer Mod.nsp": 10_000,
    "Beta Racer/manual/Beta Racer.pdf": 1_000,
    "Gamma.nsp": 6 << 20,
    "Delta Big.nsp": 400 << 20,
    "Theta.nsz": 10_000,
    "Zeta/update/Zeta [UPD][v65536].nsp": 10_000,
}


def partition(magic, entry_size, files):
    """An NSP's PFS0 (entries of 0x18 bytes) or an XCI's HFS0 (0x40) header for files, a list of
    (name, size): the header, and where each file's data begins after it."""
    names = b"".join(name.encode() + b"\0" for name, _ in files)
    names += b"\0" * (-len(names) % 0x20)
    entries, offset, name_at = b"", 0, 0
    for name, size in files:
        entries += struct.pack("<QQI", offset, size, name_at) + b"\0" * (entry_size - 20)
        offset += size
        name_at += len(name) + 1
    return magic + struct.pack("<III", len(files), len(names), 0) + entries + names


def package(path, size):
    """A game file of `size` bytes as an NSP or XCI is laid out, of random NCAs, each named after
    the first half of its SHA-256 as a game's are (no keys needed to check them), and a contents
    list (.cnmt.nca), which is not."""
    small = [4096, 1024] if size < 100_000 else [65536, 4096]
    cnmt = os.urandom(16).hex() + ".cnmt.nca"
    placeholder = "0" * 32 + ".nca"
    # The headers do not change size with the names: the first NCA takes up what is left.
    layout = lambda first, second: [(first, 0), (cnmt, small[1]), (second, small[0])]
    xci = path.suffix == ".xci"

    def headers(files):
        if not xci:
            return [partition(b"PFS0", 0x18, files)]
        secure = partition(b"HFS0", 0x40, files)
        empty = partition(b"HFS0", 0x40, [])
        root = partition(b"HFS0", 0x40, [("update", len(empty)), ("normal", len(empty)),
                                          ("secure", len(secure) + sum(s for _, s in files))])
        start = bytearray(0x200)
        start[0x100:0x104] = b"HEAD"
        start[0x130:0x138] = struct.pack("<Q", 0x200)
        return [bytes(start), root, empty, empty, secure]

    overhead = sum(map(len, headers(layout(placeholder, placeholder)))) + sum(small)
    files = layout(placeholder, placeholder)
    files[0] = (placeholder, size - overhead)
    names = []
    with path.open("wb") as out:
        out.seek(sum(map(len, headers(files))))
        for _, length in files:
            digest = hashlib.sha256()
            for at in range(0, length, 8 << 20):
                chunk = os.urandom(min(8 << 20, length - at))
                digest.update(chunk)
                out.write(chunk)
            names.append(digest.hexdigest()[:32] + ".nca")
        files = [(names[0], files[0][1]), files[1], (names[2], files[2][1])]
        out.seek(0)
        out.write(b"".join(headers(files)))


def cover():
    """Alpha Quest's cover: a red PNG of a cover's shape (RomM makes smaller copies of it)."""
    width, height = 120, 160
    raw = b"".join(b"\x00" + b"\xc0\x20\x20" * width for _ in range(height))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def remote_check():
    spec = importlib.util.spec_from_file_location("check_remote", ROOT / "tools/check-remote.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def minimum_version():
    text = (HEADLESS / "remote/romm/romm_saves.h").read_text()
    return re.search(r'kMinimumVersion\[\] = "([0-9.]+)"', text).group(1)


def free_port():
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def ask(url, path, body=None, user=None, method=None):
    request = urllib.request.Request(url + path, method=method or ("POST" if body is not None else "GET"))
    if body is not None:
        request.data = json.dumps(body).encode()
        request.add_header("Content-Type", "application/json")
    if user:
        request.add_header("Authorization", "Basic " + b64encode(f"{user[0]}:{user[1]}".encode()).decode())
    with urllib.request.urlopen(request, timeout=30) as answer:
        return json.loads(answer.read() or b"null")


def put_cover(url, rom_id, user):
    """Sets a game's cover in RomM (its artwork), as a player would in its web interface."""
    boundary = "prosperoeden-check"
    body = (f"--{boundary}\r\nContent-Disposition: form-data; name=\"artwork\"; filename=\"cover.png\"\r\n"
            f"Content-Type: image/png\r\n\r\n").encode() + cover() + f"\r\n--{boundary}--\r\n".encode()
    request = urllib.request.Request(f"{url}/api/roms/{rom_id}", data=body, method="PUT")
    request.add_header("Content-Type", f"multipart/form-data; boundary={boundary}")
    request.add_header("Authorization", "Basic " + b64encode(f"{user[0]}:{user[1]}".encode()).decode())
    try:
        with urllib.request.urlopen(request, timeout=30) as answer:
            return answer.status == 200
    except urllib.error.HTTPError as refused:
        print(f"check-romm: the cover was not set: {refused.code} {refused.read()[:300]!r}")
        return False


def build(work):
    """The check program, built as tools/check-remote.py builds its own."""
    check = remote_check()
    compiler = os.environ.get("HOST_CXX", "c++")
    c_compiler = os.environ.get("HOST_CC", "cc")
    includes = [f"-I{HEADLESS}", f"-I{MINIZ}"]
    for name in ("CURL_INCLUDE", "JSON_INCLUDE"):
        if os.environ.get(name):
            includes.append(f"-I{os.environ[name]}")
    if not os.environ.get("JSON_INCLUDE") and (found := check.find_json()):
        includes.append(f"-I{found}")
    if not check.can_build(compiler, includes,
                           "#include <curl/curl.h>\n#include <openssl/evp.h>\n#include <nlohmann/json.hpp>\n"):
        return None
    flags = ["-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter"]
    objects = [work / "http.o"]
    subprocess.run([c_compiler, *flags, "-DREMOTE_HTTP_HOST=1", *includes, "-c", str(HEADLESS / "remote/http.c"),
                    "-o", str(objects[0])], check=True)
    for name in ("miniz", "miniz_tdef", "miniz_tinfl", "miniz_zip"):
        objects.append(work / f"{name}.o")
        subprocess.run([c_compiler, "-std=c11", "-O1", "-w", "-c", str(MINIZ / f"{name}.c"), "-o", str(objects[-1])],
                       check=True)
    sources = ["romm_live_check.cpp", "remote/remote.cpp", "remote/backends.cpp", "remote/ftp.cpp",
               "remote/stream_check.cpp", "remote/save_archive.cpp", "remote/save_sync.cpp", "remote/romm/romm_client.cpp",
               "remote/romm/romm_source.cpp", "remote/romm/romm_saves.cpp"]
    binary = work / "romm_live_check"
    subprocess.run([compiler, "-std=c++20", *flags, "-fno-rtti", *includes, *[str(HEADLESS / s) for s in sources],
                    *map(str, objects), os.environ.get("CURL_LIBRARY", "-lcurl"), "-lcrypto", "-pthread", "-o", str(binary)],
                   check=True)
    return binary


def run_version(version, binary, work):
    """Starts a RomM of `version`, checks against it, and takes it down; True when it passed."""
    project = "prosperoeden-romm-check-" + re.sub(r"[^a-z0-9]", "", version.lower())
    library = work / f"library-{version}"
    for name, size in GAMES.items():
        path = library / "roms/switch" / name
        path.parent.mkdir(parents=True, exist_ok=True)
        if path.suffix in (".nsp", ".xci"):
            package(path, size)
            continue
        with path.open("wb") as out:
            for at in range(0, size, 8 << 20):
                out.write(os.urandom(min(8 << 20, size - at)))
    port = free_port()
    ftp_port = free_port()
    env = dict(os.environ, ROMM_VERSION=version, ROMM_PORT=str(port), ROMM_LIBRARY=str(library),
               FTP_PORT=str(ftp_port), CHECK_WORK=str(work), CHECK_USER=f"{os.getuid()}:{os.getgid()}")
    compose = ["docker", "compose", "-p", project, "-f", str(COMPOSE)]
    url = f"http://127.0.0.1:{port}"
    try:
        print(f"check-romm: starting RomM {version} on port {port}", flush=True)
        started = subprocess.run([*compose, "up", "-d", "--quiet-pull"], env=env, capture_output=True, text=True)
        if started.returncode != 0:
            print(f"check-romm: RomM {version} or ftpsrv did not start:\n{started.stderr[-2000:]}")
            return False
        for _ in range(180):
            try:
                if ask(url, "/api/heartbeat")["SYSTEM"]["VERSION"]:
                    break
            except (urllib.error.URLError, ConnectionError, KeyError, ValueError):
                time.sleep(1)
        else:
            print(f"check-romm: RomM {version} did not start")
            return False
        admin, player = ("admin", "admin-check"), ("player", "player-check")
        ask(url, "/api/users", {"username": admin[0], "email": "admin@example.org", "password": admin[1], "role": "admin"})
        ask(url, "/api/users", {"username": player[0], "email": "player@example.org", "password": player[1],
                                "role": "editor"}, user=admin)
        subprocess.run([*compose, "cp", str(SCAN), "romm:/tmp/scan.py"], env=env, check=True, capture_output=True)
        scanned = subprocess.run([*compose, "exec", "-T", "romm", "python", "/tmp/scan.py", *admin], env=env,
                                 capture_output=True, text=True, timeout=300)
        print("check-romm:", scanned.stdout.strip().splitlines()[-1] if scanned.stdout.strip() else scanned.stderr[-500:])
        for rom in ask(url, "/api/roms?limit=50", user=admin)["items"]:
            if any(f["file_name"].startswith("Alpha Quest") for f in rom.get("files", [])) or \
                    str(rom.get("fs_name", "")).startswith("Alpha Quest"):
                put_cover(url, rom["id"], admin)
        token = ask(url, "/api/client-tokens", {"name": "check", "scopes": SCOPES}, user=player)["raw_token"]
        bare = ask(url, "/api/client-tokens", {"name": "bare", "scopes": ["roms.read"]}, user=player)["raw_token"]
        folder = work / f"consoles-{version}"
        folder.mkdir()
        # ftpsrv answers once it listens.
        for _ in range(100):
            try:
                with socket.create_connection(("127.0.0.1", ftp_port), timeout=1):
                    break
            except OSError:
                time.sleep(0.1)
        else:
            print("check-romm: ftpsrv did not start")
            return False
        return subprocess.run([str(binary), url, token, bare, str(folder), str(library), str(ftp_port)],
                              timeout=1200).returncode == 0
    finally:
        if os.environ.get("ROMM_KEEP"):
            print(f"check-romm: RomM {version} left running at {url} (docker compose -p {project} down -v)")
        else:
            subprocess.run([*compose, "down", "-v"], env=env, capture_output=True)


def main():
    # Asked for, not part of every make test: it pulls several server images, and for its length
    # an FTP server without a sign-in listens on this computer's network.
    if os.environ.get("ROMM_CHECK") != "1" and not sys.argv[1:]:
        print("check-romm: SKIPPED (ROMM_CHECK=1 runs it against RomM servers in Docker)")
        return 0
    if shutil.which("docker") is None or subprocess.run(["docker", "compose", "version"],
                                                        capture_output=True).returncode != 0:
        print("check-romm: SKIPPED (no Docker with Compose)")
        return 0
    versions = sys.argv[1:] or [minimum_version(), NEWEST, TOO_OLD]
    with tempfile.TemporaryDirectory(prefix="romm-check-") as work:
        work = Path(work)
        binary = build(work)
        if binary is None:
            print("check-romm: SKIPPED (no libcurl, OpenSSL or nlohmann/json headers: see tools/check-remote.py)")
            return 0
        results = {version: run_version(version, binary, work) for version in dict.fromkeys(versions)}
    for version, passed in results.items():
        print(f"check-romm: RomM {version}: {'PASS' if passed else 'FAIL'}")
    return 0 if all(results.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
