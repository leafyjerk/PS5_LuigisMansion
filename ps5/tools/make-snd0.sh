#!/usr/bin/env bash
# Luigi's Mansion for the PlayStation 5 - the home screen's music (sce_sys/snd0.at9).
#
#   ps5/tools/make-snd0.sh --at9-tool ENCODER [AUDIO]
#
# The PS5's home screen plays sce_sys/snd0.at9 while the title is selected. It has to be ATRAC9
# (48 kHz stereo, at most 192 kb/s and 2 MiB, looped), and no free ATRAC9 encoder exists: FFmpeg
# only decodes it. So this needs an encoder you are legally permitted to use (ENCODER); none is
# included, downloaded or linked here.
#
# AUDIO defaults to the builder's own music, "Sneaky Waltz" (ps5/builder/app/music). The work is
# PS5_Vulkan's tools/prepare-assets.sh: an excerpt of up to 87 s, normalised to -28 LUFS, encoded,
# a whole-track loop added, and checked against the shell's limits. The result goes to
# ps5/sce_sys/snd0.at9, where ps5/tools/build.sh (and the builder's kit) pick it up.
#
# SPDX-License-Identifier: MIT
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
vulkan=$(cd -- "${PS5_VULKAN_DIR:-$root/../PS5_Vulkan}" && pwd)
encoder=""
audio="$root/ps5/builder/app/music/sneaky-waltz.mp3"
while (($#)); do
    case $1 in
    --at9-tool) encoder=${2:?--at9-tool needs a value}; shift 2 ;;
    -h | --help) sed -n '2,16p' "$0"; exit 0 ;;
    *) audio=$1; shift ;;
    esac
done
[[ -n $encoder ]] || { echo "make-snd0.sh: needs --at9-tool ENCODER (an ATRAC9 encoder you may legally use)" >&2; exit 2; }
command -v ffmpeg > /dev/null || sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq ffmpeg > /dev/null

duration=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$audio")
duration=$(python3 -c "print(min(87.0, float('$duration')))")
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cp "$root/ps5/sce_sys/param.json" "$work/param.json"
bash "$vulkan/tools/prepare-assets.sh" --audio "$audio" --audio-duration "$duration" --at9-tool "$encoder" \
    --output-directory "$work"
cp "$work/snd0.at9" "$root/ps5/sce_sys/snd0.at9"
# The loudness the shell plays it at, as prepare-assets.sh measured it
python3 - "$work/param.json" "$root/ps5/sce_sys/param.json" <<'EOF'
import json, sys
made, ours = json.load(open(sys.argv[1])), json.load(open(sys.argv[2]))
ours.setdefault("pubtools", {})["loudnessSnd0"] = made.get("pubtools", {}).get("loudnessSnd0", "-28.00")
json.dump(ours, open(sys.argv[2], "w"), indent=2)
open(sys.argv[2], "a").write("\n")
EOF
echo "ps5/sce_sys/snd0.at9: $(stat -c %s "$root/ps5/sce_sys/snd0.at9") bytes (rebuild the title and the kits to include it)"
