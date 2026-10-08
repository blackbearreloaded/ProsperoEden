#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds build/stubs/libSceCommonDialog.so, the libSceCommonDialog import facade the PS5 keyboard
# links against (tools/stubs/libSceCommonDialog.c), with the Payload SDK's compiler.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
sdk="$root/../ps5-native-app-boilerplate/.deps/native/ps5-payload-sdk"
source="$root/tools/stubs/libSceCommonDialog.c"
out="$root/build/stubs/libSceCommonDialog.so"
[[ -f $out && $out -nt $source ]] && exit 0
test -x "$sdk/bin/prospero-clang" || { echo "Missing the Payload SDK; run make deps" >&2; exit 1; }
mkdir -p "$root/build/stubs"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
PS5_PAYLOAD_SDK="$sdk" "$sdk/bin/prospero-clang" -fPIC -c "$source" -o "$work/libSceCommonDialog.o"
"$sdk/bin/prospero-lld" --shared -soname libSceCommonDialog.sprx -o "$work/libSceCommonDialog.so" "$work/libSceCommonDialog.o"
mv "$work/libSceCommonDialog.so" "$out"
