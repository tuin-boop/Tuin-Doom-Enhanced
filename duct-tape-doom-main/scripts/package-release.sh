#!/bin/sh
# Assemble a Windows or Linux release folder: the game, Freedoom as the default
# game, the GeneralUser GS SoundFont for the music, an empty wads folder for
# the player's own WADs, the docs and licenses.
# Usage: package-release.sh <windows|linux> <build dir> <Freedoom dir> <output dir>
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
platform=$1
build_dir=$2
freedoom_dir=$3
stage="$4/DERP-$platform"

program=
for candidate in "$build_dir/Release/doom.exe" "$build_dir/doom.exe" "$build_dir/doom"; do
    if [ -f "$candidate" ]; then
        program=$candidate
        break
    fi
done
if [ -z "$program" ]; then
    echo "Build the SDL app first; see BUILDING.md." >&2
    exit 1
fi

rm -rf "$stage"
mkdir -p "$stage/wads" "$stage/licenses/Freedoom"
cp "$program" "$stage/"
# The Heretic program beside it: the launcher hands Heretic WADs over.
heretic=
for candidate in "$(dirname -- "$program")/derp-heretic.exe" "$(dirname -- "$program")/derp-heretic"; do
    if [ -f "$candidate" ]; then
        heretic=$candidate
        break
    fi
done
if [ -z "$heretic" ]; then
    echo "Build derp-heretic too; see BUILDING.md." >&2
    exit 1
fi
cp "$heretic" "$stage/"
cp "$freedoom_dir/freedoom1.wad" "$stage/default.wad"
cp "$project_dir/README.md" "$project_dir/PLAY.md" "$stage/"
cp "$project_dir/LICENSE.TXT" "$stage/licenses/GPL-3.txt"
cp "$project_dir/NOTICE.txt" "$stage/licenses/NOTICE.txt"
cp "$project_dir/THIRD_PARTY.md" "$stage/licenses/THIRD_PARTY.md"
cp "$build_dir/_deps/sdl3-src/LICENSE.txt" "$stage/licenses/SDL3.txt"
cp "$project_dir/third_party/SDL3-NOTICES.txt" "$stage/licenses/SDL3-third-party-notices.txt"
cp "$build_dir/_deps/nukedopl3-src/LICENSE" "$stage/licenses/Nuked-OPL3-LGPL-2.1.txt"
cp "$build_dir/_deps/tinysoundfont-src/LICENSE" "$stage/licenses/TinySoundFont-MIT.txt"
mkdir -p "$stage/soundfonts"
cp "$build_dir/soundfonts/GeneralUser-GS.sf2" "$build_dir/soundfonts/GeneralUser-GS-LICENSE.txt" "$stage/soundfonts/"
for item in COPYING.txt CREDITS.txt CREDITS-MUSIC.txt README.html; do
    cp "$freedoom_dir/$item" "$stage/licenses/Freedoom/$item"
done
cat > "$stage/wads/PUT-WADS-HERE.txt" <<'EOF'
Put your .wad files in this folder, then start the game.

- A base game (doom.wad, doom2.wad, freedoom1.wad, freedoom2.wad, ...) is found
  automatically. With several, the game asks which one to play.
- A custom map WAD is loaded on top of the base game. A .deh or .bex patch
  with the same name next to it is applied too.
- With no WADs here, the included Freedoom game starts.
EOF
echo "Ready: $stage"
