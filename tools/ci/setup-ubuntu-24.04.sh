#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Host tools of the release build on Ubuntu 24.04, the GitHub-hosted runner of
# .github/workflows/release.yml (docs/BUILDING.md). Ubuntu 24.04 has LLVM 18 but not what the
# releases are built with besides it, so this adds:
#   - LLVM 21 from apt.llvm.org (the Payload SDK's wrappers and RADV's host tools);
#   - Meson from PyPI (Mesa needs 1.4 or later; Ubuntu 24.04 has 1.3);
#   - the SPIR-V LLVM translator for LLVM 21, built from source into PREFIX (it is in neither
#     package source); a PREFIX that already has it, such as a restored cache, is kept.
# Needs sudo. Prints the environment the build needs as NAME=value lines on file descriptor 3
# when that is open (the workflow appends them to $GITHUB_ENV).
#   setup-ubuntu-24.04.sh PREFIX
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
prefix=${1:?the folder the SPIR-V LLVM translator is installed in}
meson_version=1.10.1
translator_tag=v21.1.5
translator_commit=a76fd0702de564476fb31e8a15f1415f05ad8951
llvm_key=6084F3CF814B57C1CF12EFD515CF4D18AF4F7421
export DEBIAN_FRONTEND=noninteractive

. /etc/os-release
[[ $VERSION_ID == 24.04 ]] || { echo "this is for Ubuntu 24.04, not $VERSION_ID" >&2; exit 2; }

# apt.llvm.org, with its signing key checked against the fingerprint above.
key=$(mktemp)
curl --fail --silent --show-error --location --retry 5 https://apt.llvm.org/llvm-snapshot.gpg.key -o "$key"
found=$(gpg --show-keys --with-colons "$key" | awk -F: '$1 == "fpr" { print $10; exit }')
[[ $found == "$llvm_key" ]] || { echo "apt.llvm.org's key is $found, expected $llvm_key" >&2; exit 1; }
sudo install -D -m 0644 "$key" /etc/apt/keyrings/apt.llvm.org.asc
rm -f "$key"
echo "deb [signed-by=/etc/apt/keyrings/apt.llvm.org.asc] https://apt.llvm.org/noble/ llvm-toolchain-noble-21 main" |
    sudo tee /etc/apt/sources.list.d/llvm-21.list > /dev/null
sudo apt-get update
# shellcheck disable=SC2046 # one package name per word
sudo apt-get install --yes --no-install-recommends $(grep -v '^#' "$here/ubuntu-24.04-packages.txt")

# Meson, for this user (pipx on the runner; pip elsewhere).
if [[ $(meson --version 2>/dev/null) != "$meson_version" ]]; then
    if command -v pipx > /dev/null; then
        pipx install --force "meson==$meson_version"
    else
        python3 -m pip install --user --break-system-packages "meson==$meson_version"
    fi
fi
export PATH="$HOME/.local/bin:$PATH"
[[ $(meson --version) == "$meson_version" ]] || { echo "meson $meson_version is not the one on PATH" >&2; exit 1; }

# Mesa's build asks `llvm-config` for LLVM; the runner has none, or an older one.
sudo ln -sf /usr/bin/llvm-config-21 /usr/local/bin/llvm-config

# The SPIR-V LLVM translator, against LLVM 21.
if [[ ! -f $prefix/lib/pkgconfig/LLVMSPIRVLib.pc || $(cat "$prefix/TAG" 2>/dev/null) != "$translator_tag" ]]; then
    work=$(mktemp -d)
    git clone --quiet --depth 1 --branch "$translator_tag" https://github.com/KhronosGroup/SPIRV-LLVM-Translator "$work/source"
    [[ $(git -C "$work/source" rev-parse HEAD) == "$translator_commit" ]] ||
        { echo "SPIRV-LLVM-Translator $translator_tag is not $translator_commit" >&2; exit 1; }
    rm -rf "$prefix"
    cmake -S "$work/source" -B "$work/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER=clang-21 -DCMAKE_CXX_COMPILER=clang++-21 \
        -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
        -DLLVM_DIR=/usr/lib/llvm-21/lib/cmake/llvm -DLLVM_INCLUDE_TESTS=OFF \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_INSTALL_PREFIX="$prefix" > "$work/configure.log" 2>&1 ||
        { tail -n 30 "$work/configure.log" >&2; exit 1; }
    cmake --build "$work/build" > "$work/build.log" 2>&1 || { grep -E 'error|FAILED' "$work/build.log" | head -n 30 >&2; exit 1; }
    cmake --install "$work/build" > /dev/null
    echo "$translator_tag" > "$prefix/TAG"
    rm -rf "$work"
fi
export PKG_CONFIG_PATH="$prefix/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
echo "SPIR-V LLVM translator $(pkg-config --modversion LLVMSPIRVLib), LLVM $(llvm-config --version), $(ld.lld-21 --version)"
echo "meson $(meson --version), $(cmake --version | head -n 1), ninja $(ninja --version)"

if { true >&3; } 2> /dev/null; then
    {
        echo "PKG_CONFIG_PATH=$PKG_CONFIG_PATH"
        # The Payload SDK's prospero-* wrappers: LLVM 21, whatever else the machine has.
        echo "LLVM_CONFIG=/usr/bin/llvm-config-21"
    } >&3
fi
