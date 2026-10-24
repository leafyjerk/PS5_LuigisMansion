#!/usr/bin/env bash
# Luigi's Mansion for the PlayStation 5 - what the builder app is made from, for Linux and Windows.
#
#   ps5/builder/fetch-app-deps.sh     -> .deps/app/{SDL,imgui,fonts}
#
#   SDL         SDL3 at the release Aurora pins, unpatched (the app's window, input, file
#               dialogs and process control; Zlib)
#   imgui       Dear ImGui v1.92.9b (the app's interface; MIT)
#   fonts       Creepster (the title's lettering; SIL Open Font Licence 1.1, Google Fonts) and
#               DejaVu Sans (the rest; Bitstream Vera licence)
#   dr_mp3      David Reid's MP3 decoder (dr_libs; public domain or MIT-0): the app's music
#
# SPDX-License-Identifier: MIT
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
deps="$root/.deps/app"
versions="$root/vendor/luigis-mansion-recompiled/extern/aurora/cmake/AuroraDependencyVersions.cmake"
sdl_tag=$(sed -n 's/^_aurora_dependency_version(AURORA_SDL3_REF "refs\/tags\/\([^"]*\)".*/\1/p' "$versions")
imgui_tag=v1.92.9b
mkdir -p "$deps/fonts"

# Linux desktop headers SDL builds its X11/Wayland/D-Bus support against (it loads the libraries
# themselves at run time), and the Windows cross compiler
need=()
for package in libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxinerama-dev libxss-dev \
        libxfixes-dev libxtst-dev libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev \
        libegl-dev libgl-dev libdbus-1-dev libudev-dev libdrm-dev libgbm-dev libasound2-dev libpulse-dev \
        mingw-w64 fonts-dejavu-core; do
    dpkg -s "$package" > /dev/null 2>&1 || need+=("$package")
done
((${#need[@]})) && sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq "${need[@]}" > /dev/null

[[ -d $deps/SDL/.git ]] || git clone -q --depth 1 -b "$sdl_tag" https://github.com/libsdl-org/SDL.git "$deps/SDL"
[[ -d $deps/imgui/.git ]] || git clone -q --depth 1 -b "$imgui_tag" https://github.com/ocornut/imgui.git "$deps/imgui"

dr_libs=51e61d308dde6b437fce0c5fabb32cd86b40f4d7
mkdir -p "$deps/dr_libs"
[[ -f $deps/dr_libs/dr_mp3.h ]] ||
    curl -sSL -o "$deps/dr_libs/dr_mp3.h" "https://raw.githubusercontent.com/mackron/dr_libs/$dr_libs/dr_mp3.h"
grep -q "dr_mp3 - v" "$deps/dr_libs/dr_mp3.h" || { echo "fetch-app-deps.sh: dr_mp3.h did not download" >&2; exit 1; }

fonts="https://raw.githubusercontent.com/google/fonts/main/ofl/creepster"
[[ -f $deps/fonts/Creepster-Regular.ttf ]] || curl -sSL -o "$deps/fonts/Creepster-Regular.ttf" "$fonts/Creepster-Regular.ttf"
[[ -f $deps/fonts/Creepster-OFL.txt ]] || curl -sSL -o "$deps/fonts/Creepster-OFL.txt" "$fonts/OFL.txt"
cp -u /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf /usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf "$deps/fonts/"
cp -u /usr/share/doc/fonts-dejavu-core/copyright "$deps/fonts/DejaVu-LICENSE.txt"
echo "SDL $sdl_tag, imgui $imgui_tag"
ls -la "$deps/fonts"
