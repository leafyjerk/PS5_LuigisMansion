#!/usr/bin/env bash
# Luigi's Mansion for the PlayStation 5 - the builder's kit: everything the builder
# (ps5/builder/builder.cpp) needs on the player's computer except their disc.
#
#   ps5/builder/make-kit.sh linux|windows KIT_FOLDER
#
# Needs: a build of this tree that recorded its link (PS5_KIT_DIR=build/kit-record
# ps5/tools/build.sh), the builder's tools (build-tools.sh) and LLVM (fetch-llvm.sh).
#
# The kit, of which nothing comes from the game:
#   bin/                 clang, ld.lld, llvm-ar (LLVM 22.1.8, .deps/llvm/<platform>), nodtool,
#                        lmrecompile, ps5-native-tool (build/builder-tools/<platform>)
#   lib/clang/22/include clang's own headers
#   sdk/target/include   the PS5 payload SDK's headers
#   include/lm/          the one header the translated code includes (recomp/ctx.h)
#   recomp/              the recompiler's maps: the decomp's symbols and splits, lm-recomp's hooks
#   flags/game-cc1.txt   the compiler job the build ran for a translated file (clang -cc1, from the
#                        driver's -###), its paths made {KIT}, {GEN}, {WORK}, {IN}, {OUT}, {NAME}
#   link/                the title's link, recorded by ps5/tools/link-title.sh
#   title/               sce_sys/, sce_module/, pipelines.bin and licenses/ of dist/PPSA99351
#
# After the Super Mario Galaxy PS5 port's make-kit.sh (griches/PS5_MarioGalaxy).
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
platform=${1:?usage: make-kit.sh linux|windows KIT_FOLDER}
kit=${2:?usage: make-kit.sh linux|windows KIT_FOLDER}
case $platform in
linux) exe="" ;;
windows) exe=".exe" ;;
*) echo "make-kit.sh: linux or windows" >&2; exit 2 ;;
esac
llvm="$root/.deps/llvm/$platform"
tools="$root/build/builder-tools/$platform"
record="$root/build/kit-record"
build="$root/build/game"
lm="$root/vendor/luigis-mansion-recompiled"
sdk="$root/.deps/native/ps5-payload-sdk"
title_id=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["titleId"])' "$root/ps5/sce_sys/param.json")
for file in "$root/.deps/llvm/LICENSE.TXT" "$llvm/lib/clang/22/include/stddef.h" "$tools/nodtool$exe" \
        "$tools/lmrecompile$exe" "$tools/ps5-native-tool$exe" "$record/link/lld.args" "$record/link/native-link.args" \
        "$root/dist/$title_id/eboot.bin" "$build/build.ninja"; do
    [[ -e $file ]] || { echo "make-kit.sh: missing $file" >&2; exit 2; }
done

rm -rf "$kit"
mkdir -p "$kit/bin" "$kit/lib/clang" "$kit/sdk/target" "$kit/include/lm/recomp" "$kit/recomp" "$kit/flags" "$kit/title"

# Tools
if [[ $platform == linux ]]; then
    cp "$llvm/bin/clang-22" "$kit/bin/clang"
    cp "$llvm/bin/lld" "$kit/bin/ld.lld"
    cp "$llvm/bin/llvm-ar" "$kit/bin/llvm-ar"
else
    cp "$llvm/bin/clang.exe" "$kit/bin/clang.exe"
    cp "$llvm/bin/lld.exe" "$kit/bin/ld.lld.exe"
    cp "$llvm/bin/llvm-ar.exe" "$kit/bin/llvm-ar.exe"
fi
cp "$tools/nodtool$exe" "$tools/lmrecompile$exe" "$tools/ps5-native-tool$exe" "$kit/bin/"
cp -R "$llvm/lib/clang/22" "$kit/lib/clang/22"

# Headers, and the recompiler's maps
cp -R "$sdk/target/include" "$kit/sdk/target/include"
cp "$lm/src/recomp/ctx.h" "$kit/include/lm/recomp/ctx.h"
cp "$lm/decomp/config/GLME01/symbols.txt" "$lm/decomp/config/GLME01/splits.txt" "$lm/tools/recomp/hooks.txt" "$kit/recomp/"

# The compiler job: the build's command for one translated file, asked of the driver (-###)
command=$(ninja -C "$build" -t commands lmrecomp | grep -F "recomp_gen/funcs_000.cpp.o " | grep -F " -c " | head -1)
[[ -n $command ]] || { echo "make-kit.sh: no compile command for funcs_000.cpp in $build" >&2; exit 2; }
python3 - "$command" "$kit/flags/game-cc1.txt" "$build" "$root" "$sdk" "$lm" "$root/gamedata/recomp_gen" <<'EOF'
import os, shlex, subprocess, sys
command, out, build, root, sdk, lm, gen = sys.argv[1:]
args = shlex.split(command)
# "sh .../cc-init-array.sh .../prospero-clang++ FLAGS": the driver with the build's flags
driver = next(i for i, a in enumerate(args) if a.endswith("prospero-clang++"))
args = args[driver:]
probe = subprocess.run(args + ["-###"], cwd=build, capture_output=True, text=True)
jobs = [l for l in probe.stderr.splitlines() if '"-cc1"' in l]
if len(jobs) != 1:
    sys.exit(f"make-kit.sh: the driver gave {len(jobs)} compiler jobs:\n{probe.stderr}")
job = shlex.split(jobs[0])[1:]          # without the compiler's own path
real = lambda p: os.path.realpath(p)
resource = subprocess.run([os.path.realpath(os.popen("command -v clang").read().strip()), "--print-resource-dir"],
                          capture_output=True, text=True).stdout.strip()
# the input: the path (-main-file-name's value is the bare name)
source = next(a for a in reversed(job) if a.endswith("/funcs_000.cpp"))
mapping = [(real(resource), "{KIT}/lib/clang/22"), (resource, "{KIT}/lib/clang/22"),
           (real(sdk), "{KIT}/sdk"), (sdk, "{KIT}/sdk"),
           (real(gen), "{GEN}"), (gen, "{GEN}"),
           (real(lm) + "/src", "{KIT}/include/lm"), (lm + "/src", "{KIT}/include/lm"),
           (real(build), "{WORK}"), (build, "{WORK}")]
result, skip = [], 0
pairs = {"-dependency-file", "-MT", "-include", "-include-pch"}
includes = {"-I", "-isystem", "-internal-isystem", "-internal-externc-isystem", "-iquote"}
i = 0
while i < len(job):
    a = job[i]
    if a in ("-fno-use-init-array", "-sys-header-deps", "-MD"):
        i += 1; continue
    if a in pairs:
        i += 2; continue
    if a == "-o":
        result += ["-o", "{OUT}"]; i += 2; continue
    if a == "-main-file-name":
        result += [a, "{NAME}"]; i += 2; continue
    if a == source:
        result.append("{IN}"); i += 1; continue
    if a in includes or (a.startswith("-I") and len(a) > 2):
        path = job[i + 1] if a in includes else a[2:]
        mapped = path
        for old, new in mapping:
            if path == old or path.startswith(old + "/"):
                mapped = new + path[len(old):]
                break
        # a folder of the build the translation does not use (Aurora, SDL, ...)
        if mapped == path or mapped.startswith("{WORK}"):
            i += 2 if a in includes else 1
            continue
        result += [a, mapped] if a in includes else ["-I" + mapped]
        i += 2 if a in includes else 1
        continue
    for old, new in mapping:
        a = a.replace(old, new)
    result.append(a)
    i += 1
leaks = [a for a in result if "/home/" in a or root in a]
if leaks:
    sys.exit(f"make-kit.sh: paths the kit does not have: {leaks}")
open(out, "w").write("\n".join(result) + "\n")
print(f"flags/game-cc1.txt: {len(result)} arguments")
EOF

# The link
cp -R "$record/link" "$kit/link"

# The title's own files (the eboot and the disc are the player's)
for item in sce_sys sce_module licenses pipelines.bin; do
    cp -R "$root/dist/$title_id/$item" "$kit/title/$item"
done
rm -f "$kit/title/sce_sys/build-id.txt"
cp "$root/.deps/llvm/LICENSE.TXT" "$kit/title/licenses/LLVM-LICENSE.txt"
echo "$kit ($(du -sh "$kit" | cut -f1))"
