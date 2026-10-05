#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
template="$root/../ps5-native-app-boilerplate"
sdk="$PS5_PAYLOAD_SDK"
output=$1
shift
libraries=()
radv_archive=""
for argument in "$@"; do
    case "$argument" in
        */libvulkan_radeon.ps5.a) radv_archive="$argument" ;;
        # These functions are supplied by the native libc/kernel providers.
        -Xlinker|-pthread|-lpthread|-ldl|-lrt|-lm) ;;
        # Native titles load libkernel, not the web-process provider. Preserve
        # Eden's qualified sysconf/pthread imports when consuming the GL SDK.
        -lkernel_web) libraries+=("$sdk/target/lib/libkernel.so") ;;
        -Wl,*) IFS=, read -r -a flags <<< "${argument#-Wl,}"; libraries+=("${flags[@]}") ;;
        *) libraries+=("$argument") ;;
    esac
done
radv_link_flags=()
radv_link_inputs=()
tls_flags=(--defsym=__cxa_thread_atexit_impl=0)
if [[ -n $radv_archive ]]; then
    source "$root/tools/radv-link-eden.sh"
    eden_radv_link_recipe "$radv_archive"
    # RADV's platform provides the real thread-local destructor registration.
    tls_flags=()
fi
# The SDK's dlfcn wrappers explicitly return unavailable when these optional
# weak hooks are null. This static frontend supplies no dynamic-loader hooks.
# Mesa's entry-point tables name every Vulkan function through a weak reference
# that stays null when RADV does not implement it (radv_EnumeratePhysicalDevices:
# the common runtime's is used). The title converter imports every entry of the
# dynamic symbol table and refuses one no SDK stub exports, so those references
# are kept out of it, as PS5_Vulkan links its titles (tools/check-vulkan-runtime.sh
# there): --no-dynamic-linker does it for LLD 18 and 19 (which warn about the -z
# value and ignore it), -z nodynamic-undefined-weak for LLD 21 and later. LLD 20 has
# neither way. Which LLD runs is the SDK's choice (prospero-llvm-config: $LLVM_CONFIG,
# else the newest of LLVM 21 to 18 installed).
lld="$template/.deps/native/ps5-payload-sdk/bin/prospero-lld"
lld_major=$("$lld" --version | sed -n 's/.*LLD \([0-9][0-9]*\)\..*/\1/p' | head -1)
if [[ $lld_major == 20 ]]; then
    echo "The PS5 SDK links with LLD 20, which cannot keep Mesa's undefined weak entry points" >&2
    echo "out of the dynamic symbol table. Install LLVM 18, 19, 21 or 22 for it, or set" >&2
    echo "LLVM_CONFIG to that version's llvm-config (docs/BUILDING.md)." >&2
    exit 1
fi
"$lld" \
    "${tls_flags[@]}" "${radv_link_flags[@]}" -L "$sdk/target/lib" \
    --defsym=__dlopen=0 --defsym=__dlsym=0 --defsym=__dladdr=0 \
    --defsym=__dlclose=0 --defsym=__dlerror=0 \
    -T "$template/tooling/native/ps5-pie.ld" -T "$root/tools/unwind.ld" \
    --eh-frame-hdr --gc-sections --version-script "$root/tools/app-symbols.map" -e _start \
    --no-dynamic-linker -z nodynamic-undefined-weak \
    --error-limit=0 -Map="$output.map" \
    --wrap=aligned_alloc --wrap=malloc --wrap=calloc --wrap=realloc --wrap=free \
    --wrap=posix_memalign --wrap=malloc_usable_size \
    -o "$output" --start-group "${libraries[@]}" "${radv_link_inputs[@]}" \
    "$sdk/target/lib/libc++.a" "$sdk/target/lib/libc++abi.a" "$sdk/target/lib/libunwind.a" \
    --end-group --as-needed "$sdk/target/lib/libSceLibcInternal.so" "$sdk/target/lib/libkernel.so" \
    "$sdk/target/lib/libc.a" "$sdk/target/lib/libSceNet.so"
