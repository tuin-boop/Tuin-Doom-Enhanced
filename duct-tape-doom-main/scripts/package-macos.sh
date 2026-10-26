#!/bin/sh
# Copy the built Mac app with its licenses into an output folder and sign it ad hoc.
# Usage: package-macos.sh [build dir] [output dir]
# FREEDOOM_DIR may name an unpacked Freedoom release (default: build/game-data).
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=${1:-"$project_dir/build/play"}
output_dir=${2:-"$project_dir"}
freedoom_dir=${FREEDOOM_DIR:-"$project_dir/build/game-data"}
output_app="$output_dir/Play DOOM.app"
source_app="$build_dir/Play DOOM.app"

if [ ! -x "$source_app/Contents/MacOS/Play DOOM" ]; then
    echo "Build the SDL app first; see BUILDING.md." >&2
    exit 1
fi
# The Heretic program goes in Contents/MacOS: the launcher hands Heretic WADs over.
if [ ! -x "$build_dir/derp-heretic" ]; then
    echo "Build derp-heretic too; see BUILDING.md." >&2
    exit 1
fi
mkdir -p "$output_dir"
rm -rf "$output_app"
ditto "$source_app" "$output_app"
cp "$build_dir/derp-heretic" "$output_app/Contents/MacOS/derp-heretic"
resources="$output_app/Contents/Resources"
mkdir -p "$resources/Licenses"
cp "$project_dir/LICENSE.TXT" "$resources/Licenses/GPL-3.txt"
cp "$project_dir/NOTICE.txt" "$resources/Licenses/NOTICE.txt"
cp "$project_dir/THIRD_PARTY.md" "$resources/Licenses/THIRD_PARTY.md"
cp "$project_dir/third_party/SDL3-NOTICES.txt" "$resources/Licenses/SDL3-third-party-notices.txt"
cp "$project_dir/PLAY.md" "$resources/PLAY.md"
if [ -f "$build_dir/_deps/tinysoundfont-src/LICENSE" ]; then
    cp "$build_dir/_deps/tinysoundfont-src/LICENSE" "$resources/Licenses/TinySoundFont-MIT.txt"
fi
if [ -f "$build_dir/_deps/nukedopl3-src/LICENSE" ]; then
    cp "$build_dir/_deps/nukedopl3-src/LICENSE" "$resources/Licenses/Nuked-OPL3-LGPL-2.1.txt"
fi
for sdl_license in "$build_dir/_deps/sdl3-src/LICENSE.txt" "$project_dir/build/dependencies/SDL3-3.4.18/LICENSE.txt"; do
    if [ -f "$sdl_license" ]; then
        cp "$sdl_license" "$resources/Licenses/SDL3.txt"
        break
    fi
done
if [ -f "$freedoom_dir/COPYING.txt" ]; then
    mkdir -p "$resources/Licenses/Freedoom"
    for item in COPYING.txt CREDITS.txt CREDITS-MUSIC.txt README.html; do
        cp "$freedoom_dir/$item" "$resources/Licenses/Freedoom/$item"
    done
fi
codesign --force --sign - "$output_app/Contents/MacOS/derp-heretic"
codesign --force --sign - "$output_app"
echo "Ready: $output_app"
