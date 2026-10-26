DERP, the DOOM Engine Remaster Project. Each download includes **Freedoom**, so you can
play straight away; add your own WADs to play DOOM, DOOM II, Heretic or custom maps.

## Download

| System | File | How to play |
| --- | --- | --- |
| macOS 11 or newer (Apple Silicon and Intel) | `DERP-mac.zip` | Unzip and open **Play DOOM.app**. Choose your WAD folder or click **Play Freedoom**. |
| Windows 10/11, 64-bit | `DERP-windows.zip` | Unzip, drop your `.wad` files into the `wads` folder and run `doom.exe`. |
| Linux, x86-64 | `DERP-linux.tar.gz` | Unpack, drop your `.wad` files into `wads` and run `./doom`. |

`DERP-source.tar.gz` is the complete source of these downloads: the
game's code plus the exact SDL and Nuked OPL3 sources it is built with, so it
builds without an internet connection. GitHub's "Source code" archives hold
the game's code only.

With no WADs in the `wads` folder, Freedoom starts. With a base game (such as
`doom.wad` or `doom2.wad`) and a custom map WAD, the map is loaded on top of the
game. Controls and options are in `PLAY.md`.

**macOS:** the app is not notarized by Apple, so the first launch is blocked
with "Apple could not verify…". Click **Done**, open **System Settings → Privacy &
Security**, scroll down and click **Open Anyway** next to the message about
Play DOOM, then confirm. After that it opens normally. (Alternatively, run
`xattr -dr com.apple.quarantine "Play DOOM.app"` in Terminal.)

**Windows:** the program is not code-signed, so SmartScreen may say "Windows
protected your PC". Click **More info**, then **Run anyway**.

**Linux:** you need a Vulkan driver for the 3D renderer; without one the game
falls back to the classic renderer.

## Before you play

This is a pre-release. The Mac version is the most tested. The Windows and Linux
versions are built automatically but have had little real play, so please
[report problems](https://github.com/robongrui/derp/issues).

Supported maps: vanilla and limit-removing, including DEHACKED patches. Boom,
MBF21 and GZDoom maps are not supported. Heretic plays from your own `heretic.wad`
or `HERETIC.WAD`; Hexen is not supported yet. Multiplayer is not implemented.
