# Play DOOM on your Mac

Double-click **Play DOOM.app**. No installation is needed, and you can move
the app to Applications. If you have the source code rather than the app,
build it first as described in [BUILDING.md](BUILDING.md).

**On Windows and Linux**, put your `.wad` files into the `wads` folder next to
`doom.exe` (or `doom`) and start the game. Base games are found automatically;
with several, the game asks which one to play, and also offers the included
Freedoom. One custom map WAD in the folder is loaded on top of the base game;
with several, the game asks. A `.deh` or `.bex` patch with the same name as the
map is applied too. With no WADs in the folder, Freedoom starts. The rules below
are the same on the Mac.

At startup, choose **Choose WAD Folder…**, then select the folder directly
containing your `.wad` files. The loader finds base games and custom WADs;
if there are several, it asks which one to play. Put the required DOOM or
DOOM II base game in the same folder as your custom map. Without a base game
in the folder, compatible DOOM episode maps use the included Freedoom Phase 1.
DOOM II maps need a DOOM II base game or Freedoom Phase 2. The loader remembers
the last folder. Cancel quits; **Play Freedoom** launches the bundled game.

Custom maps made for vanilla DOOM or "limit-removing" ports work, including
their DEHACKED patches: a patch inside the WAD is applied automatically, and a
`.deh` or `.bex` file with the same name next to the WAD is loaded with it.
Maps for Boom, MBF21, GZDoom or other extended engines are not supported.

Press **Esc**, choose **New Game**, choose an episode, then choose a difficulty.
Use the arrow keys and Return to select menu items.

| Action | Control |
| --- | --- |
| Move forward/back | W / S |
| Move sideways | A / D |
| Turn | Mouse, or left/right arrows |
| Look up/down | Move the mouse vertically |
| Fire | Left mouse button, or Control |
| Open doors / use switches | Space |
| Run | Hold Shift |
| Select weapon | 1–7 |
| Map | Tab |
| Menu / release mouse | Esc |
| Save / load | F2 / F3, or the menu |
| Fullscreen | F11 |
| Graphics settings | F4 |
| Screenshot | F7 |
| Flashlight (3D renderer) | F |
| Cheat menu (fly, no monsters, map warp) | F5 |
| Quit | Command-Q |

On keyboards with media function keys, hold **Fn** when using the F keys.
Change sound volume and mouse sensitivity under **Options**.

Movement accelerates and changes direction faster, with less glide after
releasing the keys and reduced camera/weapon bob. Walking and running
retain their original top speeds. Launch with `-classicmovement` to restore the
original feel; demo playback and recording always use the original movement.

The **3D** renderer draws the level at the window's actual pixel
resolution, including Retina pixels. Resize the window to any aspect ratio,
or press F11 to fill your display. It uses 4× antialiasing when supported and
interpolates the camera and moving sprites between the original game ticks.
Enemies have subtle floor shadows made from their animated WAD sprite silhouettes.
Shadows fade and spread beneath airborne enemies and stay within their sector's
floor geometry. Player shots and enemy ranged attacks briefly light nearby
walls, floors, ceilings and sprites with a color taken from the WAD's flash
artwork. Glowing projectiles carry matching colored lights as they travel and
through their bright impact frames: green balls cast green light, for example.
Walls and closed doors block these lights. Nearby enemies also cast a shadow
away from the strongest light. These effects are available in the 3D renderer.

Press **F** to toggle the flashlight in the 3D renderer. Its warm, soft-edged beam
follows mouse look and stops at walls and closed doors. It is carried at your
right shoulder, a little behind your eyes, so wall edges, door frames and
ledges cast visible shadows, as muzzle flashes and projectiles do; it backs
out of walls you hug and sways gently with your weapon as you walk. The
artwork under the beam supplies a subtle ambient tint: red surfaces warm the
view and green surfaces add a green tint. The color fades smoothly as you aim
elsewhere or switch the flashlight off. With **Classic flashlight** off, the
beam also bounces a soft light in the color of what it hits onto the
surroundings. Aiming into sky produces no tint.
Launch with `-flashlight` to start with the light on. `-flashlighttint 0` disables
the tint, `-flashlighttint 1` selects the default, and `-flashlighttint 2` makes it stronger.

Press **F4** for the settings panel. It opens inside the game as a text-mode
style panel over the paused, dimmed view. Its options are sorted into tabs, the
description of the highlighted option is shown at the bottom, and options that
only refine another one are greyed out while that one is off.

Use **Up/Down** (or the mouse wheel) to choose an option, **Left/Right** or
**Return** to change it, **Tab** (or **1**–**6**) to switch tabs, **R** to
restore the defaults, and **Esc** or **F4** to close the panel and apply the
changes. The mouse works too: click an option to change it (right-click goes
backwards), click a slider to set it, and click a tab to open it. **F7** still
takes a screenshot.

**Display**

- **Renderer:** accelerated 3D or the classic 320×200 software view.
- **Resolution:** native pixels, 75%, or 50% for a lighter GPU workload.
- **Anti-aliasing (MSAA):** off by default, or 2x, 4x or 8x as far as the GPU offers; smooths the edges of walls, floors and ledges while texels and sprites stay crisp.
- **Field of view:** 60–120 degrees, measured against the classic 4:3 view.
- **Widescreen:** show more world at the sides rather than stretching it.
- **Mouse look, crosshair and FPS display:** optional toggles. With mouse look, **Invert mouse look** (off by default) tilts the view down when you push the mouse forward, and **Classic aim** (off by default) lets bullets autoaim as in DOOM instead of going where you look.
- **Retro scanlines** and **Doom palette colors:** the palette option quantizes the final image, including colored lighting, fog and bloom, to the WAD palette; off by default.

**Textures**

- **Walls and floors:** crisp pixels, smooth bilinear, or sharp bilinear, which keeps texels flat and only softens the step between them (**Edge softness** sets how far). **Palette mipmaps** keep distant smooth or sharp surfaces from shimmering.
- **Sprites:** crisp pixels (default) or soft bilinear filtering.
- **Bump detail and metal gloss:** derived from the WAD artwork, plus rounded sprite lighting with rim light on backlit enemies; affects dynamic lights only. Optional **varied highlights** make them tight on smooth artwork and broad on busy artwork.
- **Emissive textures:** glowing lamp strips, computer displays, lava and nukage, with soft bloom and colored light on nearby surfaces. The room's own light takes their color where they light it, monsters and the weapon included, and wall panels light only what they face. A lamp lights as strongly as its sector's light level says, so a strip the mapper set only a notch above its room stays a dim fixture. In blinking and strobing sectors the lamps switch off and on, light and all; in glowing and fire-flickering ones they fade up and down with the sector.
- **Glowing eyes:** monsters' eyes glow in the dark, with a little bloom when emissive textures are on. The stock monsters' glowing pixels are picked by hand per frame; other artwork is searched for eyes painted in a color the body never uses.
- **Glossy monitor screens:** screens in computer textures get curved CRT glass that bends the picture and catches highlights.
- **Window glass:** windows found in the map's geometry get a pane of glass with grime, painted shine streaks and highlights from lamps, shots and the flashlight. The first shot, projectile or blast through a window, yours or a monster's, shatters it into large shards that tumble, catch reflections and bounce; shots and sight still pass exactly as in vanilla. **Glass reflections** mirror the room in the window you look at, monsters and yourself included, at the liquid reflections' resolution, and show a snapshot of the surroundings in every other window; it costs a few milliseconds only while you look at a window.

**Lighting**

- **Baked at level load:** static lights from torches, lamps and glowing textures; sun shadows; bounce light; sky light, a fill from the visible sky tinted by its upper rows; ambient occlusion in corners, ledges and alcoves; decoration shadows from columns, trees and hanging bodies (changing it re-bakes the level); and texel-aligned baked light, which steps once per texture pixel instead of in coarse 4–5 unit cells.
- **Soft light seams and contact shading:** sector light blends over a short band across openings, and floors, ceilings and wall bases darken near walls.
- **Sector light flow:** brighter sectors light the floors and walls beyond their openings, so flickering rooms flicker faintly next door. Doors count as closed, as they start.
- **Door light spill:** when a door opens, light from the brighter room spills through it, scaled by how far the door is open.
- **Moving doors re-light:** baked lamp light and sunlight follow doors, lifts and lowering walls. As a door rises, the light beyond it steps in under its edge in 8-unit steps, re-baked around the door on a background thread. Bounce, sky light, light flow and sunbeams still use the level's starting state.
- **Eye adaptation:** moving between bright and dark areas briefly over- or underexposes the view, stepped in sixteenths like Doom's light levels.
- **Room color from bounce:** light from colored lamps, bounced around the room, also tints its corners the lamps never reach directly.
- **Classic flashlight:** on by default; a shorter, warmer beam that brightens what it hits in hard steps like Doom's light levels, up to a little past full brightness. Off, the beam is longer and whiter and bounces light off what it hits.
- **Flashlight tint:** the middle is the default, and fully left disables it.
- **Flashlight silhouettes:** things in the beam throw a hard-edged sprite silhouette onto the wall behind them.
- **Player shadow:** your own sprite's shadow, cast from the sun or the strongest light above you.

**Atmosphere**

- **Atmospheric fog:** height fog with soft cloud layers over pits, liquids and damaging floors. Toggle it off for a clearer view or a lighter GPU workload.
- **Sunbeams** (with sun shadows, optionally **scattering** brighter toward the sun), **sun in the sky** and **dust motes** that glint in sunbeams and the flashlight beam.
- **Heat haze:** rows of Doom pixels shimmer sideways at the tic rate above lava, red-hot damaging floors and flames.
- **Embers:** a few sparks rise from torches and fires and cool as they climb; Heretic's wind carries them off.
- **Blood splats and pools:** hitscan hits leave splats on the floor and on walls close behind the target, and corpses grow a pool in steps. **Wet blood shine** makes fresh blood and wound edges glint under dynamic lights; floor blood dries matte.
- **Wound holes:** bullets and pellets shoot see-through holes in monsters, with ragged bloody edges and dark rims. The holes follow the animation, widen with close hits, close when seen from the side and stay on the corpse. Drawing only.
- **Varied deaths:** each death is retimed for that kill, falls away from the blow and settles as it lands; now and then a hit off the middle turns the monster or a hard push steps it back. **Dying turn to face you** (off by default) turns a monster killed facing away toward you first.
- **Procedural falls:** monsters on two legs, rigged by hand or by joints found in their art, fall the way the hit says: a low or weak hit sweeps the legs, a hard one is a chest blast, a killing head shot cuts the strings. The fixed choices play one fall every time, for judging it.
- **Death reactions** and **Hit flinches:** dying monsters jolt with the killing blow and living ones lean with every hit and spring back; **Reaction strength** sets how far.
- **Soft effect sprites:** explosions, puffs and teleport fog no longer slice into walls, and dissolve on an ordered dither where they meet floors and ceilings.
- **Shootable lights:** a shot, fireball or blast into a lamp, light strip or screen breaks it. It sputters out, and a moment later the room loses the light it gave, down to near black in rooms lit only by their lamps. Lamps count near misses; screens need a hit on the glass. Render-only: monsters see as before, and a reload brings the lights back. Needs emissive textures.
- **Kill shots carry on:** a bullet that kills what it hits glances on past the body, so a fight can shoot out the lights behind the monsters. Sparks and lights only; the game still sees the shot stop.
- **Liquids:** reflections of the scene in nearby water (rendered a second time at half resolution), optionally in **retro PS1 style** at one-eighth resolution with palette colors; nukage, slime, blood and lava do not mirror the room; caustics on the surrounding walls and optionally the ceilings; damp shores, where the walls and banks just above the liquid turn darker; **glowing liquid edges**, the strip the game paints on its edge textures carried onto every wall meeting nukage, slime or lava; **shore ripples** running out from the walls; and splashes with ripple rings where things land or wade.

**Sound**

- **Music:** SoundFont (default), OPL FM, or on the Mac the system synthesizer; see below.
- **Sample playback:** Original holds each 11 kHz sample, raw and gritty, as vanilla does; Smooth and Clean read between the samples and take out the metallic sizzle. With either, **Made-up treble** puts back treble the samples never had.
- **Sparks, dying lights and glass:** sounds DOOM never had, synthesized at startup as 8-bit samples: tech walls crackle where they are shot, lamps sputter out, screens die in static and windows break. **Breaking glass** picks Shatter (a hard crack and the clatter of larger pieces) or Fracture (a crack front branching through the pane); both go on into the pieces falling and bouncing.
- **Footsteps and splashes:** heavy boots that sound by the floor (stone, metal, wood, grass and carpet), harder when running or landing, with your kit settling after each step by the weapon you carry and your armour. Liquids splash or gloop, and monsters wading through them splash too. Monsters on dry floors stay silent, as in vanilla.
- **Near misses:** an enemy's bullet from far off that passes close by whizzes past your head.
- **Ambience:** lamps and computers hum, nukage and blood bubble, lava rumbles, fire crackles, water drips and wind blows under the sky, wherever the map's textures show them. Window glass dulls the wind until it is shot, shot-out fixtures go quiet, and loud effects push it all down for a moment. **Ambience volume** sets how loud it is next to the effects.
- **Room acoustics:** echoes and a reverb tail measured from the room around you; **Echo and reverb** sets how loud.
- **Muffled behind walls** and **Duller with distance:** sounds you can't see find their way through the map's openings and come through duller the longer the way round, and more so through closed doors and glass. Far sounds lose their treble. Both change only the tone, not the loudness.
- **Natural distance falloff:** far sounds fade as in air, half as strong per doubling of distance, instead of DOOM's straight fade; gunfire and blasts carry farther, faint and dull. DOOM only.

**Performance** (off by default; the first two need baked static lights)

- **Bake-only static lights:** torches, lamps and glowing textures stop costing per-frame light work; shots, explosions and the flashlight stay dynamic.
- **Grid lighting for monsters:** monsters and items take static light from a grid baked at level load instead of tracing rays every frame.
- **Unblocked glowing-texture light:** light from lamp strips and glowing flats skips the per-pixel wall tests; cheaper near light panels, but their highlights can show on shiny surfaces through walls.
- **Fewer glowing-texture lights:** only the 8 nearest lamp strips and glowing flats light their surroundings per pixel, not 24; farther ones keep their baked glow but lose their moving highlights.

All settings are saved in `graphics.cfg`, one `key value` per line. Each F4 option lists its key beside it in `platform/settings_panel.cpp` (switches are `0` or `1`).

Animated flats and walls (NUKAGE, FWATER, falls) crossfade between frames,
door and lift movement is interpolated between game tics, and bullets leave
fading marks and a burst of wall-colored debris. Pickups pulse softly and glow
without lighting their surroundings; exploding barrels light the room, throw
sparks and leave a scorch mark on the floor. Torches, candles, lamps and
other glowing decorations cast colored light sized to their artwork; flames
flicker in DOOM's own stepped fire-flicker rhythm. With atmospheric fog on, the
sky hazes toward the horizon.

Emissive textures keep selected pixels bright in dark rooms, using the original
WAD artwork and palette. They add soft bloom around visible glowing pixels and
cast colored light onto nearby walls, floors and sprites. Solid walls and closed
doors block the light. Custom masks can be placed in `Emissive/walls/NAME.png` or
`Emissive/flats/NAME.png` beside `graphics.cfg`. Use uppercase WAD texture names
and the exact original texture dimensions. White pixels emit, black pixels
receive normal lighting, and gray pixels give partial emission; transparent
mask pixels do not emit. Masks override automatic detection. Restart after
changing masks. A black mask disables emission for one texture.

Atmospheric fog uses an analytic exponential height falloff, plus three or four
scrolling cloud layers derived from the map's sector data. Floors at least 24
units below all neighboring floors are tagged as pits. NUKAGE, FWATER, SLIME,
BLOOD and LAVA flats get haze tinted by the flat's average palette color;
damaging sectors get toxic haze. Layers reuse the floor's BSP polygons, follow
moving floors, and fade against walls and sprites using the scene depth.
They are a subtle floor effect and become less visible at grazing angles.
Outdoor sectors get thinner mist; the global fog density drops while the
player is in a sector with a sky ceiling. This global density is a stylistic
approximation, rather than integration through each sector along the ray.
Flashlight and colored-light glows retain their existing inexpensive shadow
approximation. The weapon and HUD stay clear of fog. Launch with `-fog 0` to
disable it.

**Doom palette colors** is a separate F4 toggle. A 32×32×32 RGB lookup quantizes
the final image after fog and bloom to colors in the current WAD palette,
including damage and bonus palette shifts. Launch with `-palette 1` to enable
it or `-palette 0` to disable it. This art pass intentionally changes colored
lights and can produce visible color bands.

Mouse look tilts the camera, and bullets go where you look: from your eye along
the view, hitting floors and ceilings too, so you can miss. **Classic aim** in F4
brings back DOOM's automatic vertical aiming for bullets, following the view
only when there is no target. Rockets, plasma and the BFG aim as in DOOM either
way, and demos and netgames keep vanilla aiming.
Graphics choices are saved in `graphics.cfg`. F4 and F7 replace the original
sound-menu and end-game shortcuts; those actions are still in the game menu.
Screenshots capture the full display resolution and go into `Screenshots`
inside the save folder below.

A Mac app built with Freedoom bundled (see [BUILDING.md](BUILDING.md)) includes
**Freedoom: Phase 1**, which supplies free levels, graphics, sounds and music
for the DOOM engine. Its credits and license are included inside the app. The
app never contains the commercial DOOM game data.

Saves, settings and the launch log live in:

`~/Library/Application Support/Local Games/DOOM/`

Saves belong to this native build; old DOS/Linux DOOM saves are incompatible.

To use an original DOOM or DOOM II IWAD you own, launch the executable with an
absolute path:

```sh
"./Play DOOM.app/Contents/MacOS/Play DOOM" -iwad "/absolute/path/to/doom.wad"
```

For an exact initial window size, use `-width 1920 -height 1080` (in window
coordinates; Retina displays may supply more physical pixels). You can also
use `-fullscreen`, `-fov 100`, or `-classic`. Sizes between 320×240 and
8192×8192 are accepted; resizing adjusts the render targets automatically.

Music plays from a SoundFont: sampled General MIDI instruments, close to the
Roland Sound Canvas the DOOM scores were written on. The bundled bank is
GeneralUser GS. Put another `.sf2` in the `soundfonts` folder beside `doom.cfg`
and it is used instead (the first one, alphabetically), or launch with
`-soundfont bank.sf2`. **F4 → Sound → Music** switches to OPL: an emulated OPL3
FM chip with the WAD's own instrument bank, as on a DOS Sound Blaster. On the
Mac it also offers the system synthesizer. `-opl`, `-soundfont` and
`-systemsynth` (Mac) pick one for a single session. Other options for custom WADs: `-deh file.deh` applies extra
patches, `-nodeh` ignores patches inside WADs, and `-blockmap` rebuilds each
map's collision blockmap.

On macOS the app renders with Metal. The same 3D
renderer runs on Linux and Windows through Vulkan, with the same music. Multiplayer is
not implemented. Build instructions are in [BUILDING.md](BUILDING.md).
