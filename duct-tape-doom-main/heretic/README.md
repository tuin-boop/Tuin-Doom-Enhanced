# DERP: Heretic

DERP plays Heretic too. Raven's game
runs on the same 3D renderer, lighting, sound and music as the DOOM program,
and all of it is still worked out from the WAD you play: no replacement
textures, models or recorded sounds.

It is new: it plays, but it has had far less play than DOOM, and not every
level has been played through.

## What you need

Your own copy of Heretic. Nothing from the game is in this repository or in
any download. The 2025 *Heretic + Hexen* release from GOG or Steam contains
both versions this has been tried with:

- `heretic.wad`, the 2025 game WAD;
- `dos/base/heretic/HERETIC.WAD`, the original DOS v1.3 (*Shadow of the
  Serpent Riders*).

The shareware `heretic1.wad` should work but has not been tried.

## Building and starting it

Build the SDL app as described in [`BUILDING.md`](../BUILDING.md). That also
builds the Heretic program, `derp-heretic`, and on the Mac puts it inside
`Play DOOM.app`.

Then either:

- start **Play DOOM**, choose the folder with your Heretic WAD and pick it.
  The launcher hands Heretic WADs over to the Heretic program by itself; or
- start it from a terminal with the full path to the WAD:

  ```
  build/play/derp-heretic -iwad /path/to/heretic.wad
  ```

  `-warp 1 1` jumps straight to E1M1.

Settings and saves go to their own folder, separate from DOOM's
(`Local Games/Heretic` in your user's application data). **F4** opens the
same graphics panel as in DOOM.

## Controls

| Action | Keys |
| --- | --- |
| Move and strafe | W A S D |
| Turn | ← → and the mouse |
| Look up and down | the mouse, or M / B, N to centre |
| Fire | Ctrl or the left mouse button |
| Use, open | Space |
| Run | Shift |
| Fly up, down, level out | U / J / K |
| Inventory left, right, use | [ / ] / Enter |
| Cheat menu: fly, no clipping, god mode, warp | F5 (while flying, the inventory keys change the map) |

There is no flashlight on F: the Torch item is Heretic's light, and in the 3D
renderer it burns as a flickering flame you carry.

## What works so far

- Both WADs, in the 3D renderer and the classic one. Not every level has
  been played through yet.
- Lighting from Heretic's own artwork: lava falls and lava floors, the cracks
  in the hot rock, the orange gems set into the walls, the torches and
  chandeliers, and leaded stained glass whose shards glint and whose colours
  fall onto the floor in front of it.
- Water that reflects, sludge and lava that bubble and steam, with caustics on
  the walls around them.
- Heretic's sounds and music through DERP's mixer, room echo, muffling and the
  made-up ambience where Heretic has none of its own: lava rumbling, water
  lapping, teleport pads, crackling torches. Heretic's own wind and waterfall
  sounds play as in the original.
- Shots lighting the room, wall marks, blood, and exploding pods.
- Saving and loading, and mouse look that Heretic's aiming follows.

## What doesn't yet

- Some effects were made for DOOM's world, with its computer screens and tech
  walls, and need rethinking for Heretic.
- Glowing monster eyes are guessed from the artwork; DOOM's are made by hand.
- Shots aimed at floors and ceilings with mouse look don't hit them yet.
- The 2025 add-ons (the enhanced maps, *Faith Renewed*) are not supported;
  the launcher still offers them, and they may not load or play right.
- Hexen is not supported yet; the launcher says so instead of trying.
- Windows and Linux build in CI but have not been played yet.
- No multiplayer, as in DOOM.

## Where the code is

- [`heretic/`](.): Raven's game code, ported to 64-bit systems and to DERP's
  platform layer. Most changes are marked with a `DERP:` comment.
- [`heretic/derp/`](derp/): small headers that give the shared code the names it
  knows from DOOM.
- [`platform/games/heretic/`](../platform/games/heretic/): what the shared
  renderer and sound code need to know about Heretic, and the rules for its
  textures (what glows, what is liquid, what things sound like).

## License

Raven Software re-released the Heretic source under the GNU GPL in 2008. This
port uses it under GPL 3 like the rest of the project. The details, and why
Raven's "GPLv2" is read as "version 2 or later", are in
[`THIRD_PARTY.md`](../THIRD_PARTY.md).
