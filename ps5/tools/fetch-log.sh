#!/usr/bin/env bash
# Luigi's Mansion for the PlayStation 5 - fetch the title's log from the console and symbolise its crash.
#
#   ps5/tools/fetch-log.sh
#
# Downloads /data/homebrew/<TITLE_ID>/userdata/log.txt to logs/<time>.txt (and logs/latest.txt),
# and resolves a "=== crash" backtrace's addresses against the ELF of the build that is on the
# console (its sce_sys/build-id.txt; build/elf-history, written by link-title.sh). The eboot
# loads at 0x400000.
#
# SPDX-License-Identifier: MIT
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
[[ -f $root/.env ]] && source "$root/.env"
host=${PS5_HOST:?set PS5_HOST=address:port (the console\'s FTP server) or put it in .env}
title_id=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["titleId"])' "$root/ps5/sce_sys/param.json")
remote="ftp://$host/data/homebrew/$title_id"
mkdir -p "$root/logs"
log=$root/logs/$(date +%Y%m%d-%H%M%S).txt
curl -s --max-time 30 -o "$log" "$remote/userdata/log.txt" || { echo "no log on the console" >&2; exit 2; }
cp "$log" "$root/logs/latest.txt"
build=$(curl -s --max-time 10 "$remote/sce_sys/build-id.txt" | tr -d '\r\n' || true)
elf=$root/build/elf-history/$title_id-$build.elf
echo "log: $log ($(wc -l < "$log") lines; build ${build:-unknown})"
cat "$log"

# The backtrace: hex addresses alone on a line, after "=== crash"
if grep -q "^=== crash" "$log"; then
    echo
    echo "=== backtrace, symbolised ($([[ -f $elf ]] && echo "$elf" || echo "no ELF for build $build"))"
    [[ -f $elf ]] || exit 0
    sed -n '/^=== crash/,$p' "$log" | grep -E '^[0-9a-f]+$' | while read -r address; do
        value=$((16#$address))
        if ((value < 0x400000 || value > 0x10000000)); then
            printf '%12s  (system)\n' "$address"
            continue
        fi
        printf '%12s  ' "$address"
        llvm-symbolizer --obj="$elf" -f -C -i "$(printf '0x%x' $((value - 0x400000)))" |
            paste - - | head -3 | sed "s|$root/||g" | tr '\n' ';'
        echo
    done
fi
