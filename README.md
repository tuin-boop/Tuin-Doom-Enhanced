# Tuin-Doom-Enhanced

A customized Windows build based on **Duct-Tape DOOM**, retaining its renderer and adding native Fluid Weapons/FluidMonsters support, performance improvements, a modern HUD, and gameplay presentation features.

**[Download the latest Windows release](https://github.com/tuin-boop/Tuin-Doom-Enhanced/releases/latest)** · [Build instructions](BUILDING.md) · [Detailed change notes](docs/changes)

![Launcher artwork](launcher/hero.png)

## Getting started

1. Download `Tuin-Doom-Enhanced.exe` from Releases.
2. Run `Tuin-Doom-Enhanced.exe` and select your own compatible Doom, Doom II, Final Doom, or Freedoom IWAD.
3. Add any compatible map WADs, then click **PLAY DOOM**.
4. Press **F4** for settings and bundled-mod toggles; **F5** opens the cheat/test menu.

The Windows launcher includes the engine and the supplied FluidWeapons, FluidWeapons SFX add-on, and FluidMonsters packages. You do not need to add those three manually. **Commercial game IWADs are not included.** A Vulkan-capable graphics driver and Windows x64 with .NET Framework 4.5+ are required.

## Latest update

The October 28 release adds **Camera Director (F9)** with saved wall-camera layouts, per-level takes, replay and take deletion; an **Arcade / Hall of Fame** mode with per-level scores; a smoothly interpolated slow-motion weapon wheel; and Toasty once per level on a randomly chosen early gib. It also includes key pickup hands, helmet mugshots, HUD refinements and hold-to-peek controls.

Surface-coloured destruction, refined weapon impacts, liquid splashes and native VoxelDoom support remain included. Load VoxelDoom v2.4 separately through the launcher; it is not bundled.

Type `johnromerogod` for the optional sticky/drunken rocket cheat. Right-click launches 25 rockets along your aim before they begin wandering; child rockets never multiply. A shaking Doomguy popup and Toasty sound mark the toggle.

[Full release notes](docs/changes/RELEASE-2026-10-28.md). Older releases remain in [release history](https://github.com/tuin-boop/Tuin-Doom-Enhanced/releases). Existing save/settings folder names are retained.

Compatible classic/Boom map WADs are supported; arbitrary GZDoom/UDMF maps and general ZScript are not supported. Native voxel models do not implement the pack's scripts or sprite-specific deformation. Destruction can bypass map progression; performance depends on map complexity and accumulated damage.

## Features added in this version

### Weapons and bundled mods

- All three bundled mods can be enabled or disabled independently in-game through **F4 > Bundled mods**. Settings persist; no level restart is required.
- Native Fluid Weapons presentation for all nine classic weapon slots: fists, pistol, shotgun, super shotgun, chaingun, rocket launcher, plasma rifle, BFG and chainsaw.
- Recoil, animated weapon parts, pumping, reload hands, shell/bullet catches, muzzle flashes, plasma overlays, BFG charge effects, melee motion and chainsaw animations.
- Original draw/holster poses with separate support hands, frame changes, alternate poses, rotation, scaling and pivots. Gloved fists have their own sequences.
- Smooth display-frame interpolation between the original 35-Hz switch poses, without slowing their animation timing. Firing and auxiliary actions can interrupt settling.
- Right mouse / **R** weapon fidgets and inspections, including pistol slide/catch and grip, shotgun pump, SSG reload, chaingun spin, rocket cap, plasma nudge, BFG idle, fist bump and chainsaw starter.
- Corrected SSG manual-fidget timing and hand frames; normal reload remains separate.
- Widescreen weapon framing prevents reload hands being cut off by the old 4:3 boundary.
- Alternating fist attacks with direction-sensitive hand selection; turning/walking sway, breathing, idle gestures and jump bounce.
- Corrected chaingun barrel-tip pivots and layering. Holding right mouse preserves spin between primary-fire bursts; firing resumes while it remains held. Spin sound tracks barrel speed.
- Corrected firing/recovery presentation, including continuous plasma fire; fast-pistol option.
- Selectable glove colors, custom highlight/shadow colors and bare/gloved fists.
- Native playback of the supplied weapon SFX, including named sounds, aliases and random choices.
- Invisibility now applies the weapon fuzz effect to the weapon's layers.
- Hold **Q** for the weapon wheel; release to select. Slot 1 toggles fists/chainsaw without requiring Berserk.

### Monsters and movement

- Native FluidMonsters walking for 15 ground-monster types, retaining the original AI action cadence.
- Recovery from slightly overlapping spawn positions that could trap monsters when taking small movement steps.
- Additional fixes for separating overlapping monsters and escaping existing wall overlap, plus a door/corner chase correction.
- Living monster sprites fit within floor/ceiling space where possible, reducing apparent sinking and ceiling clipping without changing collision height.
- Local single-player movement preset with autorun, jumping, mouse look and limited air steering. Demo/network modes retain their documented compatibility restrictions.
- Typed **notarget** cheat and F5 No target toggle.

### Lighting and water

- Automatic emission control based on actual emissive surface coverage, reducing excessive color flooding on maps such as SIGIL while leaving sparse fixtures at their normal strength.
- A local light accumulation limit moderates overlapping lights. Lava/slime retain visible self-glow and a minimum emitted-light contribution.
- Colored light from all six key cards/skulls, green/blue armor and major powerups. Pickup lights disappear with their items.
- Red/green lighting from stock paired SW1/SW2 switches, following the active state without retaining the old color.
- Broken computer/light presentation remains broken across sector flicker changes.
- Calmer distant liquid animation and highlights; nearby liquid artwork and animation remain intact.
- Reduced liquid light spilling onto vertical walls, bounded reflection distortion and fading reflection edges.
- Sky lighting excludes nearby local glow through the corrected fog path, preventing that source of sky pulsing.
- Shorter settled-corpse shadow footprints.
- Removed unwanted bloom from ordinary pickups while retaining intended special-item glow.

### HUD and event messages

- Classic, modern compact and modern tactical HUD choices, configurable scale/layout and animated-face placement.
- Widescreen edge anchoring for health/armor and ammo/keys; proportions are preserved.
- Top-right **Kills / Secrets / Items** counters, with completed nonempty categories highlighted.
- Corrected duplicate HUD statistics appearing through a rendering pass.
- Doom-style animated notices with category colors, three text variants per event category, simultaneous-message stacking and an overflow queue.
- Messages for killing a Cyberdemon or Spider Mastermind, discovering a secret, finding a secret exit and discovering each weapon for the first time.
- Pickup messages for soulspheres, megaspheres, invulnerability, Berserk, invisibility, radiation suits, computer maps and light-amplification visors.
- Completion messages for all kills, all secrets, all counted items and overall 100% completion. Empty categories do not independently award messages.
- Combat notices for surviving a close call, multi-kills, barrel-chain kills, fist kills against tough monsters and a damage-free full completion. Repeatable combat events have cooldowns.
- First-weapon discovery and completion tracking survive save/load appropriately. Combat notices can be disabled.
- High-resolution **Toasty** portrait and sound with a 2% cosmetic chance on monster gib deaths; **F5 > TEST TOASTY** previews it. Cosmetic randomness does not consume Doom's gameplay RNG.

### Performance and diagnostics

- CPU lighting work spread across worker threads, with persistent workers for background baking.
- Faster bake-ray traversal through a spatial bounding tree, retaining exact intersection checks.
- Exact light/shadow caching, selective invalidation for moving sectors and LRU eviction instead of flushing stationary-light caches.
- Faster surface-light merging and reusable geometry, shadow and lighting buffers to reduce allocation spikes.
- Door-opening/closing optimizations aimed at the reproduced Tech Gone Bad save-game slowdown.
- In-game **FPS and frame-time display**, plus developer profiling and cache/ray/thread verification modes.
- These performance changes retain the renderer and graphics settings; there is no blanket reduction of resolution, shadows or reflection quality.

### Launcher, maps and compatibility

- Single-executable Windows launcher with the supplied image header, dark green styling and highlighted Play button.
- IWAD browsing/scanning, ordered add-on loading, drag-and-drop and saved choices.
- The redundant bundled-mod text has been removed from the launcher.
- Startup/title artwork clipping corrected for rerelease assets.
- In-game map selection and fullscreen toggle (**F11**).
- SIGIL episode/map routing, sky, secret route, ending and MP3 soundtrack support with a compatible Ultimate Doom IWAD.
- Corrected positional sound attenuation on map 8; intentionally global sounds retain their behavior.
- Material-gallery test maps covering stock wall/door/floor/ceiling materials and liquids, plus switch-gallery maps showing both switch states. See [test-maps](test-maps).

## Validation and limits

For this release, all three shared geometry contract suites pass and the published engine source builds successfully on Windows. Saved-game replays cover plasma impacts, excavated stairs and damaged moving doors. Remaining door-opening stalls are still being investigated; this release does not claim to eliminate every slowdown.

Historical validation: the full suite passed **24/24 tests** before the earlier interpolation correction; the three relevant weapon/texture suites passed again afterward. Fullscreen switching captures covered all nine weapons at 2560×1440. Historical measurements and their conditions are in [docs/changes](docs/changes).

Performance depends on the map, hardware, settings and active effects. This is **not a guaranteed 60-FPS minimum**. The earlier Tech Gone Bad door comparison improved from about 51 FPS average during movement to 93–96 FPS in repeated fullscreen tests on the test PC; those figures are workload-specific, not a universal benchmark.

This is targeted native support for the supplied mod versions, **not a general ZScript/DECORATE runtime**. Full Fluid Weapons option/animation parity is still incomplete, including special mid-fire switch/reload branches, optional draw starter-cord mode, dual wield and ID24 weapon variants. General world casing physics, arbitrary custom shaders and voxel mods are not implemented. Some reported visual situations were not reproduced exactly; historical notes identify those limits. A shutdown crash reported against an older build was not reproduced or diagnosed.

## Source and credits

- **Duct-Tape DOOM — https://github.com/robongrui/duct-tape-doom** — original engine and renderer by Robert Heidenreich. Its lighting, reflections and environmental systems are the foundation of this build, not newly invented features of this fork.
- DOOM source by **id Software**; original notices are retained.
- **MR's Fluid Weapons** by mickromash and contributors, its SFX add-on, and **FluidMonsters**: original bundled archives and their credits/notices are retained in [launcher/mods](launcher/mods).
- Two focused liquid-light/distant-water ideas were adapted from the supplied DERP 0.5.0; this is not a wholesale merge of that version.
- SDL3, Nuked OPL3, TinySoundFont and other dependencies retain their licenses in [licenses](licenses) and their source trees.
- The supplied Toasty portrait/audio are separate media assets; the engine's GPL does not relicense those assets.

See [LICENSE](LICENSE), [licenses/NOTICE.txt](licenses/NOTICE.txt), and [licenses/THIRD_PARTY.md](licenses/THIRD_PARTY.md). Original per-component licensing and attribution requirements remain applicable.
