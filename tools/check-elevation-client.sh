#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mkdir -p "$root/build/host"
clang++-18 -std=c++20 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    "$root/headless/elevation/test_elevation.cpp" -o "$root/build/host/test-elevation"
"$root/build/host/test-elevation"
echo 'Resident/one-shot Lapy client PASS'
