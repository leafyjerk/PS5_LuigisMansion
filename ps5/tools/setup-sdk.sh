#!/usr/bin/env bash
# Luigi's Mansion for the PlayStation 5 - the payload SDK this title builds with, at a pinned revision.
#
#   ps5/tools/setup-sdk.sh       install it into .deps/native/ps5-payload-sdk (once a revision), print the path
#
# The SDK is Mihawk's fork, ../PS5_PayloadSDK: the upstream release (John Törnblom's
# ps5-payload-dev/sdk v0.42) with the PS5 platform layer (libps5platform.a, include/ps5platform)
# installed over it. The pinned revision is exported with git archive, so a build never depends
# on the fork's working tree. After PS5_Dusklight's and the PS5 Vulkan Template's setup-sdk.sh.
#
# SPDX-License-Identifier: MIT
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cache="$root/.deps/native"
sdk="$cache/ps5-payload-sdk"
sdk_fork="${PS5_PAYLOAD_SDK_FORK:-$root/../PS5_PayloadSDK}"
# The revision PS5_Dusklight and Super Mario Galaxy pin: the platform layer's C-locale
# localeconv (6b63a2a) and its /data elevation (611893f)
sdk_revision=611893fc25ef3e138f5bc2c90d829d8b58174cd2

if [[ ! -f $sdk/.ps5-sdk-revision || $(<"$sdk/.ps5-sdk-revision") != "$sdk_revision" ]]; then
    git -C "$sdk_fork" cat-file -e "$sdk_revision^{commit}" 2>/dev/null || {
        echo "the payload SDK fork at $sdk_fork does not have $sdk_revision (ps5/tools/setup-stack.sh clones it)" >&2
        exit 2
    }
    mkdir -p "$cache"
    sdk_tree=$(mktemp -d)
    git -C "$sdk_fork" archive "$sdk_revision" | tar -x -C "$sdk_tree"
    bash "$sdk_tree/platform/tools/setup-sdk.sh" "$sdk" "$sdk_revision" "$cache" >&2
    rm -rf -- "$sdk_tree"
fi
[[ -x $sdk/bin/prospero-lld && -d $sdk/target/include ]] || { echo "the pinned PS5 payload SDK is incomplete" >&2; exit 2; }
echo "$sdk"
