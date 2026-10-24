#!/usr/bin/env bash
# Luigi's Mansion for the PlayStation 5 - Luigi's Mansion Recompiled's sources, with the console's changes.
#
#   ps5/tools/setup-source.sh    check out vendor/luigis-mansion-recompiled and apply ps5/patches to it
#
# Luigi's Mansion Recompiled (rob646464) is a submodule at an upstream revision, with Aurora as
# a submodule of its own. What the console needs changed is kept here as patches, not a fork:
#
#   ps5/patches/lm-recomp.patch        the PS5's folders, disc, exit, updater, crash handler,
#                                      settings menu on the DualSense, 60 fps and libc++ 18
#   ps5/patches/aurora-9000-ps5.patch  the console's surface for Dawn, and .dds texture codes
#                                      (PS5_Dusklight's), placed last in lm-recomp's own series
#                                      of Aurora patches, which its CMake applies
#
# A patch already applied is left alone, so this can run before every build. To change the
# port's edits: edit vendor/luigis-mansion-recompiled, then save them with
#   git -C vendor/luigis-mansion-recompiled diff -- . ':(exclude)extern/aurora' \
#       ':(exclude)patches/aurora/9000-ps5.patch' > ps5/patches/lm-recomp.patch
#
# SPDX-License-Identifier: MIT
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
lm="$root/vendor/luigis-mansion-recompiled"

[[ -f $lm/CMakeLists.txt ]] || git -C "$root" submodule update --init vendor/luigis-mansion-recompiled
[[ -f $lm/extern/aurora/CMakeLists.txt ]] || git -C "$lm" submodule update --init --depth 1 extern/aurora

patch="$root/ps5/patches/lm-recomp.patch"
if ! git -C "$lm" apply --reverse --check "$patch" 2>/dev/null; then
    git -C "$lm" apply --check "$patch" || {
        echo "setup-source.sh: lm-recomp.patch does not apply to $lm (a different upstream revision?)" >&2
        exit 2
    }
    git -C "$lm" apply --whitespace=nowarn "$patch"
    echo "setup-source.sh: applied lm-recomp.patch" >&2
fi
cmp -s "$root/ps5/patches/aurora-9000-ps5.patch" "$lm/patches/aurora/9000-ps5.patch" ||
    cp "$root/ps5/patches/aurora-9000-ps5.patch" "$lm/patches/aurora/9000-ps5.patch"
