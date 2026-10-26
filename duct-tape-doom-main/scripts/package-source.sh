#!/bin/sh
# Assemble the complete source of a release: this repository at the checked-out
# commit plus the exact SDL, Nuked OPL3 and TinySoundFont sources and the
# SoundFont bank the build downloaded, so the
# game, with or without changes to those libraries, builds without the internet.
# Usage: package-source.sh <build dir> <output dir>
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=$1
stage="$2/DERP-source"

for dependency in sdl3-src nukedopl3-src tinysoundfont-src; do
    if [ ! -d "$build_dir/_deps/$dependency" ]; then
        echo "$build_dir has no downloaded $dependency; configure with -DCMAKE_DISABLE_FIND_PACKAGE_SDL3=ON first." >&2
        exit 1
    fi
done

rm -rf "$stage"
mkdir -p "$2"
git -C "$project_dir" archive --format=tar --prefix=DERP-source/ HEAD | tar -x -C "$2"
mkdir -p "$stage/dependencies"
cp -R "$build_dir/_deps/sdl3-src" "$stage/dependencies/SDL3"
cp -R "$build_dir/_deps/nukedopl3-src" "$stage/dependencies/Nuked-OPL3"
cp -R "$build_dir/_deps/tinysoundfont-src" "$stage/dependencies/TinySoundFont"
if [ -d "$build_dir/soundfonts" ]; then
    cp -R "$build_dir/soundfonts" "$stage/dependencies/soundfonts"
fi
cat > "$stage/dependencies/README.txt" <<'EOF'
The libraries the game is linked with, exactly as the release build used them:

- SDL3: SDL 3.4.18 (zlib license; see SDL3/LICENSE.txt and
  ../third_party/SDL3-NOTICES.txt).
- Nuked-OPL3: Nuked OPL3 by Nuke.YKT (GNU LGPL 2.1 or later; see
  Nuked-OPL3/LICENSE).
- TinySoundFont: by Bernhard Schelling (MIT; see TinySoundFont/LICENSE).
- soundfonts: the GeneralUser GS bank by S. Christian Collins, used as found
  here instead of downloaded (its own license; see
  soundfonts/GeneralUser-GS-LICENSE.txt).

To build the game from this folder alone, from the folder above this one:

  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DDOOM_BUILD_SDL=ON \
    -DCMAKE_DISABLE_FIND_PACKAGE_SDL3=ON \
    -DFETCHCONTENT_SOURCE_DIR_SDL3="$PWD/dependencies/SDL3" \
    -DFETCHCONTENT_SOURCE_DIR_NUKEDOPL3="$PWD/dependencies/Nuked-OPL3" \
    -DFETCHCONTENT_SOURCE_DIR_TINYSOUNDFONT="$PWD/dependencies/TinySoundFont"
  cmake --build build --parallel --target doom

Changes made to either library here are built into the game. BUILDING.md
lists the system packages a Linux build needs.
EOF
echo "Ready: $stage"
