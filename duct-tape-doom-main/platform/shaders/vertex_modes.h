// Vertex mode bits (Vertex::mode in C++, vMode in the shaders), shared by
// both sides like feature_flags.h. scene3d.h and common.glsl include it.
#ifndef DOOM_VERTEX_MODES_H
#define DOOM_VERTEX_MODES_H

#define MODE_SCREEN          1u    // already in clip space: HUD, weapon, crosshair
#define MODE_SPRITE          2u    // a sprite: clamped texels, drawn after the opaque world
#define MODE_SUNBEAM         2u    // mist pass only: a sunbeam ribbon, not a mist layer
#define MODE_FUZZ            4u    // spectre (MF_SHADOW): darkened and translucent
#define MODE_FACET_NORMAL    8u    // non-sprite: packed constant triangle normal in statics
#define MODE_RELIT           8u    // per-pixel light from the silhouette normals
#define MODE_FLIPPED         16u   // mirrored sprite frame: flip the normals too
#define MODE_LIQUID          16u   // non-sprite floors only: distance-stable liquid detail
#define MODE_GLASS_SHARD     4u    // glass pass only: a tumbling polygon with a full 3D face normal
#define MODE_GLASS_CRACK     2u    // glass pass only: a fracture edge reached by the spreading crack
#define MODE_GLASS_HELD      8u    // glass pass only: a fragment still attached to the pane
#define MODE_GLASS_DARK      16u   // glass pass only: the shadow beside a fracture edge
#define MODE_MIRROR_FLOOR    32u   // a liquid floor sampling the reflection pass
#define MODE_PICKUP          64u   // pickups: a soft pulse and some bloom
#define MODE_EXCAVATED_FLAT  128u  // non-sprite: crater retains its parent floor/ceiling bake
#define MODE_GROUNDED        128u  // darkened feet of things resting on the floor
#define MODE_STAINED_GLASS   64u  // map surfaces only: leaded stained glass (Image::leaded)
#define MODE_BLOOD           256u  // blood decal; vTint carries wetness and drying
#define MODE_ROCK           512u  // non-sprite: continuous excavation material
#define MODE_HARD_SILHOUETTE 512u  // shadow pass: keep the sprite's pixel outline
#define MODE_SOFT            1024u // effect sprite: dithers out at floors, drawn in front of walls
#define MODE_STIPPLED        1024u // decal pass only: a hard silhouette's edge breaks into a stipple
#define MODE_WET_SPRITE      2048u // airborne blood glints like floor blood (needs MODE_RELIT)
#define MODE_GLOWING_EYES    4096u // a living monster: its eyes glow (eyeGlow in indexed.glsl)
#define MODE_DEAD_GLOW       8192u // a shot-out lamp or screen: its glowing texels go dark (shot_lights.cpp)
#define MODE_VOXEL           16384u // sprite lighting on native voxel mesh faces
#define MODE_LAMP_OFF        16384u // temporarily unpowered, without physical glass damage
// Bits from here up carry sector+1 for map-space lighting; 0 means none.
#define MODE_SECTOR_SHIFT    15
#define MODE_WOUND           8192u // sprites only: blood paint from the WAD palette, preserving coverage and normals
#define MODE_WOUND_PALETTE_SHIFT 14
#define MODE_WOUND_STAIN     (1u<<22) // blend blood into the source paint instead of replacing it

#endif
