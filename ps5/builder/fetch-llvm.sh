#!/usr/bin/env bash
# Luigi's Mansion for the PlayStation 5 - the LLVM the builder's kits carry: the official LLVM
# 22.1.8 release (Apache-2.0 WITH LLVM-exception) for Linux and for Windows, of which a kit
# needs clang, ld.lld, llvm-ar and clang's own headers.
#
#   ps5/builder/fetch-llvm.sh       -> .deps/llvm/{linux,windows}/{bin,lib/clang/22}, .deps/llvm/LICENSE.TXT
#
# The same release as the title's build (apt.llvm.org's 22.1.8, ps5/tools/setup-stack.sh): the
# kit's link is the build's own, recorded.
#
# SPDX-License-Identifier: MIT
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
version=22.1.8
major=${version%%.*}
deps="$root/.deps/llvm"
base="https://github.com/llvm/llvm-project/releases/download/llvmorg-$version"
mkdir -p "$deps"

fetch() {
    local platform=$1 archive=$2
    shift 2
    local out="$deps/$platform"
    [[ -f $out/.llvm-$version ]] && return
    rm -rf "$out" "$deps/extract-$platform"
    mkdir -p "$out" "$deps/extract-$platform"
    echo "fetching $archive" >&2
    curl -sSL --retry 3 -o "$deps/$archive" "$base/$archive"
    local top=${archive%.tar.xz}
    tar -xJf "$deps/$archive" -C "$deps/extract-$platform" --wildcards "$@" "$top/LICENSE.TXT" 2>/dev/null ||
        tar -xJf "$deps/$archive" -C "$deps/extract-$platform" --wildcards "$@"
    local tree="$deps/extract-$platform/$top"
    mkdir -p "$out/bin" "$out/lib/clang"
    cp -a "$tree/bin/." "$out/bin/"
    cp -a "$tree/lib/clang/$major" "$out/lib/clang/$major"
    [[ -f $tree/LICENSE.TXT ]] && cp "$tree/LICENSE.TXT" "$deps/LICENSE.TXT"
    rm -rf "$deps/extract-$platform" "$deps/$archive"
    touch "$out/.llvm-$version"
}
fetch linux "LLVM-$version-Linux-X64.tar.xz" \
    "LLVM-$version-Linux-X64/bin/clang-$major" "LLVM-$version-Linux-X64/bin/lld" "LLVM-$version-Linux-X64/bin/llvm-ar" \
    "LLVM-$version-Linux-X64/lib/clang/$major/include/*"
fetch windows "clang+llvm-$version-x86_64-pc-windows-msvc.tar.xz" \
    "clang+llvm-$version-x86_64-pc-windows-msvc/bin/clang.exe" "clang+llvm-$version-x86_64-pc-windows-msvc/bin/lld.exe" \
    "clang+llvm-$version-x86_64-pc-windows-msvc/bin/llvm-ar.exe" \
    "clang+llvm-$version-x86_64-pc-windows-msvc/lib/clang/$major/include/*"
# The licence (the release archives keep it out of the folders taken above; Luigi's Mansion
# Recompiled ships the same text for the clang it bundles)
[[ -f $deps/LICENSE.TXT ]] || cp "$root/vendor/luigis-mansion-recompiled/packaging/LLVM-LICENSE.TXT" "$deps/LICENSE.TXT"
ls -la "$deps"/*/bin
