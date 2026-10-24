#!/usr/bin/env bash
# Luigi's Mansion for the PlayStation 5 - the player's disc, ready for a build from source.
#
#   ps5/tools/prepare-game.sh DISC_IMAGE
#
# DISC_IMAGE is the player's own Luigi's Mansion for the USA (GLME01), in any form nod reads
# (.iso, .gcm, .rvz, .wia, .gcz, .ciso, .nfs, .tgc). Writes, never committed:
#
#   gamedata/GLME01.iso        the disc as a plain image (the console's disc reader, ps5/nod,
#                              takes .iso and .ciso only); it goes beside eboot.bin
#   gamedata/disc/             the disc extracted (sys/main.dol is what the recompiler reads)
#   gamedata/recomp_gen/       main.dol translated into C++ by lm-recomp's own recompiler
#                              (lmrecompile, built here for the host: a cross build cannot run the
#                              one it builds)
#
# SPDX-License-Identifier: MIT
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
disc=${1:?usage: prepare-game.sh DISC_IMAGE}
lm="$root/vendor/luigis-mansion-recompiled"
game="$root/gamedata"
bash "$root/ps5/tools/setup-source.sh"

# nodtool (encounter/nod's command line)
nodtool=$(command -v nodtool || echo "$HOME/.cargo/bin/nodtool")
[[ -x $nodtool ]] || "$HOME/.cargo/bin/cargo" install --locked nodtool

mkdir -p "$game"
"$nodtool" info "$disc" | tee "$game/disc-info.txt" | head -8
grep -q "GLME01" "$game/disc-info.txt" || {
    echo "prepare-game.sh: this is not Luigi's Mansion for the USA (GLME01)" >&2
    exit 2
}
if [[ ! -f $game/GLME01.iso ]]; then
    "$nodtool" convert "$disc" "$game/GLME01.iso.part"
    mv "$game/GLME01.iso.part" "$game/GLME01.iso"
fi
if [[ ! -f $game/disc/sys/main.dol ]]; then
    rm -rf "$game/disc"
    "$nodtool" extract "$game/GLME01.iso" "$game/disc"
fi

# The recompiler, for the host
tool="$root/build/host/lmrecompile"
mkdir -p "$(dirname "$tool")" "$game/recomp_gen"
if [[ ! -x $tool || $lm/src/recompiler/recompiler.cpp -nt $tool || $lm/tools/recomp/lmrecompile.cpp -nt $tool ]]; then
    clang++ -std=c++20 -O2 -I "$lm/src" "$lm/tools/recomp/lmrecompile.cpp" "$lm/src/recompiler/recompiler.cpp" -o "$tool"
fi
"$tool" --dol "$game/disc/sys/main.dol" --version GLME01 \
    --symbols "$lm/decomp/config/GLME01/symbols.txt" --splits "$lm/decomp/config/GLME01/splits.txt" \
    --hooks "$lm/tools/recomp/hooks.txt" --out "$game/recomp_gen"
echo "gamedata ready: $(ls "$game/recomp_gen" | wc -l) translated files, $(du -h "$game/GLME01.iso" | cut -f1) disc image"
