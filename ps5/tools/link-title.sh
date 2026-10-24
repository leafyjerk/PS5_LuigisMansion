#!/usr/bin/env bash
# Luigi's Mansion for the PlayStation 5 - link the title and package its folder.
#
#   link-title.sh BUILD_DIR PS5_VULKAN_DIR SDK OBJECT...
#
# CMake's link of lmrecomp arrives here (ps5/cmake/ps5-lm.cmake sets
# CMAKE_CXX_LINK_EXECUTABLE). The link is PS5_Vulkan's: its RADV release archive, its link
# recipe (tools/radv-link.sh: the platform layer of the SDK pin, the heap and thread wraps, the
# libc names bound to ps5_*), its CRT, its native tool (ELF -> the console's fake SELF) and its
# libc.prx. The title folder, dist/<TITLE_ID>/, gets the signed eboot.bin, sce_sys/,
# pipelines.bin and the licences.
#
# After PS5_Dusklight's ps5/tools/link-title.sh (itself after the PS5 Vulkan Template's,
# Copyright (C) 2026 Mihawk).
# SPDX-License-Identifier: MIT

set -euo pipefail
[[ -n ${LINK_TRACE:-} ]] && set -x

work=$1 vulkan=$2 sdk=$3
shift 3
# CMake's link line arrives as it would go to the compiler driver: -Xlinker only introduces a
# linker argument there, and the groups go (the link below makes one group of everything)
objects=()
for argument in "$@"; do
    case $argument in
    -Xlinker | --start-group | --end-group) ;;
    # zlib is in the RADV archive already (Mesa's own, for its shader cache), whole
    */libz.a | -lz) ;;
    # the driver's flags CMake adds for the executable; the link below has its own
    -fuse-ld=* | -static-libstdc++ | -ldl | -lpthread | -lm | -pthread | -O* | -g*) ;;
    *) objects+=("$argument") ;;
    esac
done
port=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
ps5="$port/ps5"
lm=${LM_SOURCE_DIR:?LM_SOURCE_DIR is the lm-recomp checkout}
archive=${RADV_ARCHIVE:-$vulkan/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a}
export PS5_CLANG=${PS5_CLANG:-$(command -v clang || true)}
tool="$vulkan/build/host/ps5-native-tool"
native="$vulkan/tooling/native"
param="$ps5/sce_sys/param.json"

for file in "$archive" "$tool" "$param" "$sdk/bin/prospero-lld" "$vulkan/tools/radv-link.sh" \
        "$vulkan/runtime/libc.prx"; do
    [[ -e $file ]] || { echo "missing: $file" >&2; exit 2; }
done
mkdir -p "$work/link/obj" "$work/link/stubs"
cc() { PS5_PAYLOAD_SDK="$sdk" sh "$vulkan/tooling/prospero-clang18" "$@"; }

# The CRT, as in PS5_Vulkan's CTS title.
cc -std=c++20 -O2 -fno-exceptions -fno-rtti -c "$native/app_crt.cpp" -o "$work/link/obj/app_crt.o"
# RADV calls AGC, which the SDK has no stubs for: these name its imports.
stub() {
    local library=$1 source=$2
    cc -std=c11 -O2 -fPIC -c "$vulkan/$source" -o "$work/link/obj/${library}_stub.o"
    "$sdk/bin/prospero-lld" --shared -soname "${library}.prx" \
        -o "$work/link/stubs/${library}.so" "$work/link/obj/${library}_stub.o"
}
stub libSceAgc vendor/ps5/sdk/stubs/agc_canary_link_stub.c
stub libSceAgcDriver vendor/ps5/sdk/stubs/agc_driver_canary_link_stub.c

# shellcheck source=/dev/null
source "$vulkan/tools/radv-link.sh"
radv_link_recipe "$vulkan" "$sdk" "$archive" || exit 2
# std::jthread and std::stop_token are libc++'s experimental library in the SDK's LLVM 18
experimental=()
[[ -f $sdk/target/lib/libc++experimental.a ]] && experimental=("$sdk/target/lib/libc++experimental.a")
# libc names no module a title loads exports, which the libraries linked in call:
# ps5/src/shims.c and lm_shims.c answer each as ps5dl_<name>, kept local like the recipe's own
# bound names
shims=(isatty getresuid getresgid dl_iterate_phdr fchown fork vfork setsid pathconf getnameinfo gai_strerror lstat
    strsignal getcwd chdir)
{ printf '{\n    local:\n'; printf '        %s;\n' "${shims[@]}"; printf '};\n'; } > "$work/link/shims-local.map"
for name in "${shims[@]}"; do
    radv_link_flags+=("--defsym=$name=ps5dl_$name")
done
radv_link_flags+=(--version-script "$work/link/shims-local.map")
# The game's log: printf and its kin into stderr; exit and its kin close the title instead
# (the exit system call is SIGSYS for a title), with the log written first (ps5/src/log.c)
for name in printf vprintf puts putchar exit _exit _Exit raise; do
    radv_link_flags+=("--wrap=$name")
done
# A thread_local declared in a header without its initialiser is reached through a wrapper that
# calls the variable's initialisation function (_ZTH<name>) if one exists: a weak reference.
# Where none exists, other systems leave the reference null; a title's link instead asks the
# console's modules for it, and the native tool refuses an import no stub exports. Each such
# name that nothing defines is null here, and local (a title exports nothing). (The Super Mario
# Galaxy port met the same with SoftFloat's state.)
tls_names=$("$sdk/bin/llvm-nm" --no-sort "${objects[@]/#--*/}" 2>/dev/null |
    awk '$NF ~ /^_ZTH/ { print $(NF-1), $NF }' | sort -u || true)
tls_null=$(comm -23 <(awk '$1 == "w" || $1 == "U" { print $2 }' <<<"$tls_names" | sort -u) \
    <(awk '$1 != "w" && $1 != "U" { print $2 }' <<<"$tls_names" | sort -u))
if [[ -n $tls_null ]]; then
    { printf '{\n    local:\n'; printf '        %s;\n' $tls_null; printf '};\n'; } > "$work/link/tls-null-local.map"
    for name in $tls_null; do
        radv_link_flags+=("--defsym=$name=0")
    done
    radv_link_flags+=(--version-script "$work/link/tls-null-local.map")
    echo "link-title.sh: $(wc -w <<<"$tls_null") thread_local initialisation references are null" >&2
fi
lld_arguments=("${radv_linker_script[@]}" --eh-frame-hdr "${radv_link_flags[@]}"
    --version-script "$native/app-symbols.map" --exclude-libs=ALL --no-dependent-libraries
    -e _start -o "$work/link/llvm-pie.elf"
    "$work/link/obj/app_crt.o" --start-group "${objects[@]}" "${experimental[@]}" --end-group
    "$work/link/stubs/libSceAgc.so" "$work/link/stubs/libSceAgcDriver.so"
    "${radv_link_inputs[@]}"
    --as-needed "$sdk"/target/lib/*.so)
"$sdk/bin/prospero-lld" "${lld_arguments[@]}"

# For the builder's kit (ps5/builder/make-kit.sh): with PS5_KIT_DIR set, the link as data. Each
# file or folder the link reads is copied into $PS5_KIT_DIR/link/files/ and the arguments go one
# a line into an .args file: {KIT} for the kit's folder, {WORK} for where the builder links, and
# {GAME}/libgame.a, linked whole, in place of the translated game's objects (the player's own,
# compiled by the builder). The player's link is then this one. (The Super Mario Galaxy port's
# builder works the same way.)
kit_index=0
kit_record() {
    local list=$PS5_KIT_DIR/link/$1 argument out prefix path name next="" game_done=false
    shift
    : > "$list"
    for argument in "$@"; do
        out=$argument
        if [[ $next == work ]]; then
            out="{WORK}/${argument##*/}"
            next=""
        elif [[ $next == literal ]]; then
            next=""
        else
            case $argument in
            -o | --out | --in) next=work ;;
            --file-name) next=literal ;;
            */recomp_gen/*.o)
                $game_done && continue
                game_done=true
                printf '%s\n' --whole-archive "{GAME}/libgame.a" --no-whole-archive >> "$list"
                continue ;;
            *)
                prefix="" path=$argument
                if [[ $argument == --*=* ]]; then
                    prefix=${argument%%=*}= path=${argument#*=}
                fi
                # a path: with a folder in it, or an input's suffix
                if [[ -e $path && ($path == */* || $path =~ \.(a|o|so|ld|map)$) ]]; then
                    kit_index=$((kit_index + 1))
                    name="$kit_index-${path##*/}"
                    cp -R "$path" "$PS5_KIT_DIR/link/files/$name"
                    out="$prefix{KIT}/link/files/$name"
                fi ;;
            esac
        fi
        printf '%s\n' "$out" >> "$list"
    done
}
if [[ -n ${PS5_KIT_DIR:-} ]]; then
    rm -rf "$PS5_KIT_DIR/link"
    mkdir -p "$PS5_KIT_DIR/link/files"
    # what the SDK's prospero-lld wrapper adds around them, for the bare ld.lld
    kit_record lld.args -m elf_x86_64 -pie --eh-frame-hdr -z max-page-size=0x4000 -mllvm -emulated-tls \
        "${lld_arguments[@]}" --hash-style=gnu
fi
# A title whose localeconv is still the console's reads every fraction as 0: refuse it here
if grep -q " T ps5_localeconv$" <<<"$("$sdk/bin/llvm-nm" --defined-only "$sdk/target/lib/libps5platform.a" 2>/dev/null)" &&
        grep -q " U localeconv$" <<<"$("$sdk/bin/llvm-nm" "$work/link/llvm-pie.elf" 2>/dev/null)"; then
    echo "link-title.sh: localeconv is not bound to the platform layer's ps5_localeconv" >&2
    exit 1
fi
# A title loads neither libkernel_sys's exports nor libScePosixForWebKit's: an import only their
# stubs define links, and is null at run time, so its first call jumps to address 0. Refuse a
# title that still imports one, here rather than on the console.
null_imports=$(comm -23 \
    <("$sdk/bin/llvm-nm" -D --undefined-only "$work/link/llvm-pie.elf" |
        awk '$1 == "U" { sub(/@.*/, "", $2); print $2 }' | sort -u) \
    <(for library in "$sdk"/target/lib/*.so "$work/link/stubs/libSceAgc.so" "$work/link/stubs/libSceAgcDriver.so"; do
        case ${library##*/} in libkernel_sys.so | libScePosixForWebKit.so) continue ;; esac
        "$sdk/bin/llvm-nm" -D --defined-only "$library" 2>/dev/null | awk '{ print $NF }'
    done | sort -u))
if [[ -n $null_imports ]]; then
    echo "link-title.sh: imports that no module a title loads exports (null at run time): ${null_imports//$'\n'/ }" >&2
    echo "link-title.sh: answer them in ps5/src/shims.c or bind them to the platform layer" >&2
    exit 1
fi
native_link_arguments=(link --in "$work/link/llvm-pie.elf" --out "$work/eboot.elf.new"
    --stub-dir "$sdk/target/lib" --stub "$work/link/stubs/libSceAgc.so"
    --stub "$work/link/stubs/libSceAgcDriver.so" --module-sdk 0x02000009
    --companion-sdk 0x08050001 --file-name eboot.elf)
"$tool" "${native_link_arguments[@]}"
if [[ -n ${PS5_KIT_DIR:-} ]]; then
    kit_record native-link.args "${native_link_arguments[@]}"
fi

# The title folder
title_id=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["titleId"])' "$param")
app="$port/dist/$title_id"
mkdir -p "$app/sce_sys" "$app/sce_module"
"$tool" self --sign --in "$work/eboot.elf.new" --out "$app/eboot.bin" --magic 0x1D3D154F
"$tool" self --inspect --file "$app/eboot.bin" > /dev/null
cp "$param" "$app/sce_sys/param.json"
for asset in icon0.png pic0.dds pic1.dds snd0.at9; do
    [[ -f $ps5/sce_sys/$asset ]] && cp "$ps5/sce_sys/$asset" "$app/sce_sys/$asset"
done
(cd "$vulkan/runtime" && sha256sum --check --strict --quiet libc.prx.sha256)
cp "$vulkan/runtime/libc.prx" "$app/sce_module/libc.prx"
# The game's shader precache list, beside the program as on the PC
cp "$lm/data/pipelines.bin" "$app/pipelines.bin"
mv "$work/eboot.elf.new" "$work/eboot.elf"
# The linked ELF, to symbolise a crash address with (llvm-symbolizer --obj=... ADDR-0x400000),
# also kept by the eboot's hash: a crash report comes from whichever build was on the console
cp "$work/link/llvm-pie.elf" "$port/dist/$title_id.elf"
mkdir -p "$port/build/elf-history"
eboot_hash=$(sha256sum "$app/eboot.bin" | cut -c1-12)
cp "$work/link/llvm-pie.elf" "$port/build/elf-history/$title_id-$eboot_hash.elf"
echo "$eboot_hash" > "$app/sce_sys/build-id.txt"
printf '==> %s: %s (eboot.bin %s bytes)\n' "$title_id" "$app" "$(stat -c %s "$app/eboot.bin")"
# The licences beside eboot.bin
mkdir -p "$app/licenses"
cp "$lm/NOTICE.md" "$app/licenses/LuigisMansionRecompiled-NOTICE.md"
cp "$lm/decomp/LICENSE" "$app/licenses/lm-decomp-LICENSE.txt"
cp "$lm/extern/aurora/LICENSE" "$app/licenses/Aurora-LICENSE.txt"
cp "$lm/packaging/LLVM-LICENSE.TXT" "$app/licenses/LLVM-LICENSE.txt"
cp "$vulkan/LICENSE" "$app/licenses/PS5_Vulkan-LICENSE.txt"
cp "$ps5"/licenses/*.txt "$app/licenses/"
[[ -f $port/LICENSE ]] && cp "$port/LICENSE" "$app/licenses/PS5_LuigisMansion.txt"
