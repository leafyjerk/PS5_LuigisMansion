#!/usr/bin/env bash
# Luigi's Mansion for the PlayStation 5 - the development loop: save the port's edits to
# Luigi's Mansion Recompiled, build, and deploy to the console.
#
#   ps5/tools/ship.sh
#
# 1. vendor/luigis-mansion-recompiled's changes -> ps5/patches/lm-recomp.patch
# 2. ps5/tools/build.sh
# 3. ps5/tools/deploy.sh (FTP; PS5_HOST)
#
# SPDX-License-Identifier: MIT
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
lm="$root/vendor/luigis-mansion-recompiled"
git -C "$lm" diff -- . ':(exclude)extern/aurora' ':(exclude)patches/aurora/9000-ps5.patch' > "$root/ps5/patches/lm-recomp.patch"
echo "lm-recomp.patch: $(grep -c '^diff --git' "$root/ps5/patches/lm-recomp.patch") files"
bash "$root/ps5/tools/build.sh"
bash "$root/ps5/tools/deploy.sh"
