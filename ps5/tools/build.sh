#!/usr/bin/env bash
# Luigi's Mansion for the PlayStation 5 - build the title into dist/<TITLE_ID>/.
#
#   ps5/tools/build.sh [ninja arguments]       (after setup-stack.sh and prepare-game.sh)
#
# Applies the console's patches to vendor/luigis-mansion-recompiled (setup-source.sh),
# configures it (once) with the payload SDK's CMake toolchain and ps5/cmake/ps5-lm.cmake (which
# gives it Dawn, SDL3 and nod for the console, and the title's link, ps5/tools/link-title.sh),
# and builds its static build, lmrecomp, from gamedata/recomp_gen. dist/<TITLE_ID>/ gets
# eboot.bin, sce_sys/, sce_module/, pipelines.bin, the licences and gamedata/GLME01.iso.
#
#   PS5_BUILD_DIR    the build tree (default build/game)
#   PS5_VULKAN_DIR   the PS5_Vulkan checkout (default ../PS5_Vulkan)
#   PS5_KIT_DIR      also record the link for the builder's kit (ps5/builder/make-kit.sh)
#
# Only the target lmrecomp is the console's: lm-recomp's other executables are the PC's.
#
# SPDX-License-Identifier: MIT
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
lm="$root/vendor/luigis-mansion-recompiled"
vulkan=$(cd -- "${PS5_VULKAN_DIR:-$root/../PS5_Vulkan}" && pwd)
build=${PS5_BUILD_DIR:-$root/build/game}
pregen="$root/gamedata/recomp_gen"

bash "$root/ps5/tools/setup-source.sh"
dawn=$(bash "$root/ps5/tools/setup-dawn.sh")
sdl=$(bash "$root/ps5/tools/setup-sdl.sh")
sdk=$(bash "$root/ps5/tools/setup-sdk.sh")
[[ -f $pregen/func_table.cpp ]] || { echo "no translated game in $pregen: run ps5/tools/prepare-game.sh" >&2; exit 2; }

# The home screen's pictures, drawn by ps5/art/make-title-art.py (committed; delete them to redraw)
if [[ ! -f $root/ps5/sce_sys/icon0.png || ! -f $root/ps5/sce_sys/pic0.dds || ! -f $root/ps5/sce_sys/pic1.dds ]]; then
    art=$(mktemp -d)
    python3 "$root/ps5/art/make-title-art.py" "$art"
    cp "$art/icon0.png" "$root/ps5/sce_sys/icon0.png"
    python3 "$root/ps5/tools/png-to-title-dds.py" "$art/pic0.png" "$root/ps5/sce_sys/pic0.dds"
    python3 "$root/ps5/tools/png-to-title-dds.py" "$art/pic1.png" "$root/ps5/sce_sys/pic1.dds"
    rm -rf "$art"
fi

ninja=$(command -v ninja)
export PS5_CLANG=${PS5_CLANG:-$(command -v clang)}
export PS5_KIT_DIR=${PS5_KIT_DIR:-}
if [[ ! -f $build/build.ninja ]]; then
    mkdir -p "$build"
    cmake -S "$lm" -B "$build" -G Ninja -DCMAKE_MAKE_PROGRAM="$ninja" \
        -DCMAKE_TOOLCHAIN_FILE="$sdk/toolchain/prospero.cmake" \
        -DCMAKE_PROJECT_lmport_INCLUDE="$root/ps5/cmake/ps5-lm.cmake" \
        -DPS5_PORT_DIR="$root" -DPS5_VULKAN_DIR="$vulkan" -DDAWN_DIR="$dawn" -DSDL_DIR="$sdl" \
        -DLM_RECOMP_PREGEN="$pregen" -DLM_DOL_VERSION=GLME01 \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_VERBOSE_MAKEFILE=OFF > "$build/configure.log" 2>&1 \
        || { tail -60 "$build/configure.log" >&2; exit 1; }
fi
# A kit records the link: make the link run again
[[ -n $PS5_KIT_DIR ]] && rm -f "$build/lmrecomp"
"$ninja" -C "$build" "${@:-lmrecomp}"

title_id=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["titleId"])' "$root/ps5/sce_sys/param.json")
if [[ -f $root/gamedata/GLME01.iso ]]; then
    # The disc beside eboot.bin, as the console wants it (a hard link: no second 1.4 GB copy)
    ln -f "$root/gamedata/GLME01.iso" "$root/dist/$title_id/GLME01.iso" 2>/dev/null ||
        cp "$root/gamedata/GLME01.iso" "$root/dist/$title_id/GLME01.iso"
fi
echo "ready: dist/$title_id"
