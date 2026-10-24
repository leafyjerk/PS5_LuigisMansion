#!/usr/bin/env bash
# Luigi's Mansion for the PlayStation 5 - copy the built title to the console over FTP.
#
#   ps5/tools/deploy.sh            (after build.sh)
#
# Uploads dist/<TITLE_ID>/ to /data/homebrew/<TITLE_ID>/ on the console's FTP server (PS5_HOST,
# e.g. 192.168.1.253:2121, or a line PS5_HOST=... in .env at the top of this repository), and
# the disc image (gamedata/GLME01.iso) when the console has none of that size. The title's files
# go first and sce_sys/ last, so ShadowMountPlus registers the title only once it is complete (it
# needs sce_sys/icon0.png). userdata/ (saves, settings, caches, log.txt) is never touched.
#
# SPDX-License-Identifier: MIT
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
[[ -f $root/.env ]] && source "$root/.env"
host=${PS5_HOST:?set PS5_HOST=address:port of the console FTP server, or put it in .env}
disc=$root/gamedata/GLME01.iso
title_id=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["titleId"])' "$root/ps5/sce_sys/param.json")
dist=$root/dist/$title_id
remote="ftp://$host/data/homebrew/$title_id"
[[ -f $dist/eboot.bin ]] || { echo "no build at $dist: run ps5/tools/build.sh" >&2; exit 2; }

curl -s --max-time 10 "ftp://$host/data/homebrew/" > /dev/null ||
    { echo "the console's FTP server at $host does not answer" >&2; exit 2; }

remote_size() {
    curl -s --max-time 20 -I "$remote/$1" 2>/dev/null | tr -d '\r' | awk '/Content-Length/ { print $2 }'
}
put() {
    local rel=$1 source=${2:-$dist/$1}
    for attempt in 1 2 3; do
        if curl -s --ftp-create-dirs -T "$source" "$remote/${rel// /%20}"; then
            printf '  %-28s %s bytes\n' "$rel" "$(stat -c %s "$source")"
            return
        fi
        echo "  retrying $rel ($attempt)" >&2
    done
    echo "FAILED: $rel" >&2
    exit 1
}

echo "==> $title_id to $host"
# Everything but sce_sys/, then the disc, then sce_sys/
(cd "$dist" && find . -type f ! -path './sce_sys/*' ! -name GLME01.iso | sed 's|^\./||' | sort) | while read -r rel; do
    put "$rel"
done
if [[ -f $disc ]]; then
    size=$(stat -c %s "$disc")
    if [[ $(remote_size GLME01.iso) == "$size" ]]; then
        echo "  GLME01.iso                   already there"
    else
        echo "  GLME01.iso                   uploading $((size / 1048576)) MB..."
        # Under another name until it is whole: a half-copied disc would be found and opened
        curl -s --ftp-create-dirs -T "$disc" "$remote/GLME01.iso.part"
        curl -s -Q "RNFR /data/homebrew/$title_id/GLME01.iso.part" \
            -Q "RNTO /data/homebrew/$title_id/GLME01.iso" "ftp://$host/" > /dev/null
        echo "  GLME01.iso                   $size bytes"
    fi
fi
(cd "$dist" && find sce_sys -type f | sort) | while read -r rel; do
    put "$rel"
done
# The home screen shows the copy of the art the console made when it registered the title
# (/user/appmeta/<TITLE_ID>): a title already registered keeps its old pictures unless that copy
# is refreshed too (as the Super Mario Galaxy port's deploy does)
for art in icon0.png pic0.dds pic1.dds; do
    [[ -f $dist/sce_sys/$art ]] || continue
    curl -s -T "$dist/sce_sys/$art" "ftp://$host/user/appmeta/$title_id/$art" 2>/dev/null &&
        echo "  appmeta/$art" || true
done
echo "==> done: start \"$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["localizedParameters"]["en-US"]["titleName"])' "$root/ps5/sce_sys/param.json")\" on the console"
