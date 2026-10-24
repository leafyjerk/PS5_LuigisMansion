#!/usr/bin/env bash
# Luigi's Mansion for the PlayStation 5 - the builder as a release zip: the app, lm-builder and
# the kit in one folder.
#
#   ps5/builder/package.sh linux|windows VERSION
#       -> dist/builder/LuigisMansion-PS5-Builder-VERSION-<platform>.zip
#
# Needs build/builder-tools/<platform> (build-tools.sh) and build/builder-kit-<platform>
# (make-kit.sh). After the Super Mario Galaxy PS5 port's package-windows.sh.
#
# SPDX-License-Identifier: MIT
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
platform=${1:?usage: package.sh linux|windows VERSION}
version=${2:?usage: package.sh linux|windows VERSION}
name="Luigi's Mansion PS5 Builder"
case $platform in
linux) exe="" app="$name" ;;
windows) exe=".exe" app="$name.exe" ;;
*) echo "package.sh: linux or windows" >&2; exit 2 ;;
esac
tools="$root/build/builder-tools/$platform"
kit="$root/build/builder-kit-$platform"
deps="$root/.deps/app"
for input in "$tools/$app" "$tools/lm-builder$exe" "$kit/flags/game-cc1.txt" "$kit/bin/clang$exe"; do
    [[ -e $input ]] || { echo "package.sh: missing $input" >&2; exit 2; }
done
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
folder="$stage/$name"
mkdir -p "$folder/licenses"
cp "$tools/$app" "$tools/lm-builder$exe" "$folder/"
cp -R "$kit" "$folder/kit"
# The app's own parts' licences (the title's are in kit/title/licenses)
cp "$root/LICENSE" "$folder/licenses/PS5_LuigisMansion-GPL-3.0.txt"
cp "$deps/fonts/Creepster-OFL.txt" "$folder/licenses/Creepster-OFL.txt"
cp "$deps/fonts/DejaVu-LICENSE.txt" "$folder/licenses/DejaVu.txt"
cp "$deps/SDL/LICENSE.txt" "$folder/licenses/SDL3-Zlib.txt"
cp "$deps/imgui/LICENSE.txt" "$folder/licenses/DearImGui-MIT.txt"
cp "$root/.deps/llvm/LICENSE.TXT" "$folder/licenses/LLVM.txt"
cp "$root/ps5/licenses/MIT.txt" "$folder/licenses/nod-MIT.txt"
cp "$root/CREDITS.md" "$folder/CREDITS.md"

if [[ $platform == windows ]]; then
    readme_run="1. Open \"$name.exe\". If SmartScreen says it doesn't know the app, choose
   More info, then Run anyway."
    keep="Keep this folder together: the app needs lm-builder.exe and kit\\ beside it."
else
    python3 "$root/ps5/builder/app/make-app-icon.py" "$folder/icon.png" > /dev/null
    cat > "$folder/install-desktop-entry.sh" <<'EOF'
#!/bin/sh
# Adds Luigi's Mansion PS5 Builder to your desktop's app menu (optional).
here=$(cd -- "$(dirname -- "$0")" && pwd)
mkdir -p "$HOME/.local/share/applications"
cat > "$HOME/.local/share/applications/luigis-mansion-ps5-builder.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=Luigi's Mansion PS5 Builder
Comment=Makes the PS5 title from your own Luigi's Mansion disc
Exec="$here/Luigi's Mansion PS5 Builder"
Icon=$here/icon.png
Terminal=false
Categories=Game;Utility;
DESKTOP
echo "Added to your app menu."
EOF
    chmod +x "$folder/install-desktop-entry.sh" "$folder/$app" "$folder/lm-builder"
    readme_run="1. Open \"$name\" (double-click it, or run ./\"$name\" in a terminal).
   ./install-desktop-entry.sh adds it to your app menu if you like."
    keep="Keep this folder together: the app needs lm-builder and kit/ beside it."
fi
readme=$(cat <<EOF
Luigi's Mansion PS5 Builder $version ($platform)

Makes the PlayStation 5 title PPSA99351 from your own Luigi's Mansion disc
(GameCube, USA, GLME01): .iso, .gcm, .rvz, .wia, .gcz, .ciso, .nfs or .tgc.

$readme_run
2. Drop your disc image onto the window (or click Choose...) and click Build.
   About 5 GB free is needed; it takes a few minutes.
3. Copy the PPSA99351 folder it makes to /data/homebrew/ on your PS5 over FTP
   (it holds the game's disc image as GLME01.iso, so it is about 1.5 GB).
4. Start Luigi's Mansion from the home screen. Click the touch pad for the
   settings.

$keep
The title you build contains code translated from your disc: it is for your
own console, please don't share it.

https://github.com/leafyjerk/PS5_LuigisMansion
Not affiliated with or endorsed by Nintendo. Credits: CREDITS.md.
Licences: licenses/ and kit/title/licenses/.
EOF
)
if [[ $platform == windows ]]; then
    printf '%s\n' "$readme" | sed $'s/$/\r/' > "$folder/README.txt"
else
    printf '%s\n' "$readme" > "$folder/README.txt"
fi
zip="$root/dist/builder/LuigisMansion-PS5-Builder-$version-$platform.zip"
mkdir -p "$(dirname "$zip")"
rm -f "$zip"
(cd "$stage" && zip -q -r -X -9 "$zip" "$name")
echo "$zip ($(du -h "$zip" | cut -f1))"
