#!/usr/bin/env python3
# ProsperoEden - Host check of the download sources (headless/remote) against a stand-in RomM server.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
"""check-remote.py

Builds headless/remote_check.cpp (the download sources with their RomM backend) with the host's
C++ compiler and libcurl and runs it against tools/romm-mock-server.py, with tools/ftp-mock-server.py
writing the downloads as the console's FTP server does. Needs libcurl's headers (libcurl4-openssl-dev) and nlohmann/json's
(nlohmann-json3-dev, or the copy Eden's build fetched); CURL_INCLUDE, JSON_INCLUDE and
CURL_LIBRARY name other places for them. Without libcurl's headers it says so and is skipped.
"""

import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADLESS = ROOT / "headless"


def find_json():
    """A folder holding nlohmann/json.hpp: the system's, or the copy Eden's build fetched."""
    for folder in ("/usr/include", "/usr/local/include"):
        if (Path(folder) / "nlohmann/json.hpp").exists():
            return None
    # This checkout's build cache (docs/BUILDING.md), where Eden's configure step put its packages.
    for base in [ROOT / ".deps", *sorted(Path.home().glob(".cache/ps5-eden-headless.*"))]:
        if base.is_dir():
            for header in base.glob("**/single_include/nlohmann/json.hpp"):
                return header.parent.parent
    return None


def can_build(compiler, includes, source):
    probe = subprocess.run([compiler, "-x", "c++", "-std=c++20", *includes, "-fsyntax-only", "-"],
                           input=source, text=True, capture_output=True)
    return probe.returncode == 0


def main():
    compiler = os.environ.get("HOST_CXX", "c++")
    c_compiler = os.environ.get("HOST_CC", "cc")
    includes = [f"-I{HEADLESS}"]
    for name in ("CURL_INCLUDE", "JSON_INCLUDE"):
        if os.environ.get(name):
            includes.append(f"-I{os.environ[name]}")
    if not os.environ.get("JSON_INCLUDE") and (json := find_json()):
        includes.append(f"-I{json}")
    library = os.environ.get("CURL_LIBRARY", "-lcurl")
    if not can_build(compiler, includes, "#include <curl/curl.h>\n"):
        print("check-remote: SKIPPED (no libcurl headers: install libcurl4-openssl-dev or set CURL_INCLUDE)")
        return 0
    if not can_build(compiler, includes, "#include <nlohmann/json.hpp>\n"):
        print("check-remote: SKIPPED (no nlohmann/json.hpp: install nlohmann-json3-dev or set JSON_INCLUDE)")
        return 0
    with tempfile.TemporaryDirectory(prefix="remote-check-") as work:
        work = Path(work)
        flags = ["-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter"]
        http = work / "http.o"
        subprocess.run([c_compiler, *flags, "-DREMOTE_HTTP_HOST=1", *includes, "-c",
                        str(HEADLESS / "remote/http.c"), "-o", str(http)], check=True)
        binary = work / "remote_check"
        subprocess.run([compiler, "-std=c++20", *flags, "-fno-rtti", *includes,
                        str(HEADLESS / "remote_check.cpp"), str(HEADLESS / "remote/remote.cpp"),
                        str(HEADLESS / "remote/backends.cpp"), str(HEADLESS / "remote/ftp.cpp"),
                        str(HEADLESS / "remote/romm/romm_client.cpp"), str(HEADLESS / "remote/romm/romm_source.cpp"),
                        str(http),
                        library, "-pthread", "-o", str(binary)], check=True)
        port_file = work / "port"
        ftp_port_file = work / "ftp-port"
        server = subprocess.Popen([sys.executable, str(ROOT / "tools/romm-mock-server.py"), str(port_file)])
        ftp = subprocess.Popen([sys.executable, str(ROOT / "tools/ftp-mock-server.py"), str(ftp_port_file)])
        try:
            for _ in range(100):
                if all(f.exists() and f.read_text() for f in (port_file, ftp_port_file)):
                    break
                time.sleep(0.05)
            files = work / "files"
            files.mkdir()
            url = f"http://127.0.0.1:{port_file.read_text()}"
            return subprocess.run([str(binary), url, str(files), ftp_port_file.read_text()], timeout=180).returncode
        finally:
            for process in (server, ftp):
                process.terminate()
                process.wait()


if __name__ == "__main__":
    sys.exit(main())
