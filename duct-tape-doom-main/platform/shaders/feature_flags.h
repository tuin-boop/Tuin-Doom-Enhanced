// Per-frame feature flags, shared by C++ (platform/scene3d/scene.cpp) and the
// shaders (through common.glsl), so both sides name the same bits. They travel
// as a float in the camera's flashlightTint.w (exact up to 2^24); shaders read
// them with uint(c.flashlightTint.w).
#ifndef DOOM_FEATURE_FLAGS_H
#define DOOM_FEATURE_FLAGS_H

#define FEATURE_SURFACE_DETAIL     1u     // bump detail and gloss from the artwork
#define FEATURE_RETRO_REFLECTIONS  2u     // PS1-style mirror: low resolution, palette colors
#define FEATURE_CAUSTICS           4u     // caustics on walls near liquids
#define FEATURE_TEXEL_BAKE         8u     // texel-aligned bake and sector light
#define FEATURE_CEILING_CAUSTICS   16u    // caustics on ceilings too
#define FEATURE_LIGHT_FLOW         32u    // light flows between neighboring sectors
#define FEATURE_BAKED_OCCLUSION    64u    // baked ambient occlusion
#define FEATURE_GLOSSY_SCREENS     128u   // curved CRT glass on computer screens
#define FEATURE_CAUSTICS_COMPUTED  256u   // caustics: computed layers, blended by depth
#define FEATURE_CAUSTICS_GROW      512u   // caustics: shapes grow with depth
#define FEATURE_CAUSTICS_ANGLE     1024u  // caustics: stronger the flatter the light strikes
#define FEATURE_CAUSTICS_SWAY      2048u  // caustics: the pattern sways
#define FEATURE_CAUSTICS_SPRITES   4096u  // caustics: things near liquids catch them too
#define FEATURE_CAUSTICS_SHOTS     8192u  // caustics: muzzle flashes and shots cast them
#define FEATURE_DAMP_SHORES        16384u // damp walls and banks above liquids
#define FEATURE_VARIED_HIGHLIGHTS  32768u // highlight sharpness follows the artwork's grain
#define FEATURE_WINDOW_LIGHT       65536u // sky openings light indoors (no indoor sky lift)
#define FEATURE_UNEVEN_TILES       131072u // painted floor tiles lie a little crooked
#define FEATURE_UNEVEN_WALLS       262144u // painted wall and ceiling tiles and bricks too
#define FEATURE_CLASSIC_FLASHLIGHT 524288u // the flashlight raises light like extralight, in steps
#define FEATURE_STIPPLED_SHADOWS   1048576u // flashlight shadow edges break into an ordered-dither stipple
#define FEATURE_GLOW_EDGES         2097152u // glowing liquids paint their edge strip on the walls they meet
#define FEATURE_WINDOW_STREAKS     4194304u // rain streaks under windows (world/wear.glsl)
#define FEATURE_LIVE_READOUTS      8388608u // LED readouts in computer textures change their cells

#define FEATURE_GLOW_EDGES 2097152u
#endif
