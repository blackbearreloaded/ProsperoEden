#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds the PS5 app and its PPSA99008 folder (after make prepare).
#   tools/build-package.sh release   build/release/PPSA99008, the configuration that ships
#   tools/build-package.sh dev ID    build/dev/PPSA99008 (EDEN_DEV_PACKAGE_DIR) with the development switches
#                                    (profiling counters, dev-settings.txt A/B switches) that
#                                    boots title ID on launch
set -euo pipefail
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
mode=${1:?usage: tools/build-package.sh release|dev TITLE_ID}
scratch=$(cat .local/headless-cache)
export PS5_PAYLOAD_SDK="$scratch/sdk"
export EDEN_PS5_VULKAN=ON EDEN_VULKAN_DRIVER=RADV
case $mode in
release)
    export EDEN_DEV_VULKAN=OFF EDEN_DEV_ROM_ID= EDEN_DEV_PROFILE=OFF EDEN_DEV_WAIT_CALLERS=OFF
    export EDEN_PACKAGE_DIR="$root/build/release/PPSA99008"
    ;;
dev)
    title=${2:?a development build boots one title: tools/build-package.sh dev TITLE_ID}
    [[ $title =~ ^[0-9A-Fa-f]{16}$ ]] || { echo "Invalid title ID: $title" >&2; exit 1; }
    export EDEN_DEV_VULKAN=ON EDEN_DEV_ROM_ID="$title" EDEN_DEV_PROFILE=ON EDEN_DEV_PROFILE_TITLE="$title"
    # The release-configuration source checks do not apply to development instrumentation.
    export EDEN_SKIP_SOURCE_CHECKS=1
    export EDEN_PACKAGE_DIR="${EDEN_DEV_PACKAGE_DIR:-$root/build/dev/PPSA99008}"
    ;;
*)
    echo "usage: tools/build-package.sh release|dev TITLE_ID" >&2
    exit 2
    ;;
esac
echo "== Eden for the PS5 ($mode, $(git rev-parse --short HEAD 2>/dev/null || echo source)$(git diff --quiet -- headless tools src 2>/dev/null || echo +dirty))"
bash tools/build-headless-native.sh --graphics
python3 -B tools/check-radv-native.py
echo "== Package $EDEN_PACKAGE_DIR"
rm -rf "$EDEN_PACKAGE_DIR"
bash tools/package-headless-native.sh --integration
if [[ $mode == release ]]; then
    python3 -B headless/check_package.py --check
elif [[ -f CANDIDATE.json ]]; then
    # Development layout: the console runner verifies this receipt before every run.
    python3 -B headless/check_package.py --freeze
    python3 -B headless/check_package.py --verify
else
    python3 -B headless/check_package.py --check
fi
echo "== Built $EDEN_PACKAGE_DIR"
