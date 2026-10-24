#!/usr/bin/env bash
# Luigi's Mansion for the PlayStation 5 - run one of these tools with its output in a log file.
#
#   ps5/tools/run-logged.sh LOG TOOL [ARGUMENTS...]
#
# Writes everything TOOL prints to LOG, then "exit <status>" as LOG's last line, and prints
# LOG's last lines. (wsl.exe joins its arguments into one shell line, which loses the quoting of
# an inline `bash -c '...; echo $?'`: this keeps it in a file.)
set -uo pipefail
log=$1
shift
mkdir -p "$(dirname "$log")"
"$@" > "$log" 2>&1
status=$?
echo "exit $status" >> "$log"
tail -25 "$log"
exit "$status"
