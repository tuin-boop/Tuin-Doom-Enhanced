/* What the game's wall textures and flats are, from their names: the rules
   the renderer and the sound code apply to artwork they recognise. Names are
   uppercase and NUL-terminated (at most eight characters). Each game program
   links its own tables: platform/games/<game>/materials.c. */
#ifndef DOOM_GAME_MATERIALS_H
#define DOOM_GAME_MATERIALS_H
#ifdef __cplusplus
extern "C" {
#endif

/* What in the artwork glows: a DOOM_EMISSIVE_* kind (emissive.h), whose
   pixel rule then picks the glowing pixels. flat: 1 for flats. */
int Game_GlowKind(const char *name, int flat);
/* Machinery that throws electric sparks when shot, glowing or not. */
int Game_SparkTexture(const char *name, int flat);
/* Textures that may hold monitor screens; the pixels decide where. */
int Game_ScreenTexture(const char *name, int flat);

/* A liquid floor: water mirrors the room; thick liquids get heavy splashes,
   bubbles and goo. Only flats the engine animates count as liquid. */
typedef enum { GAME_LIQUID_NONE, GAME_LIQUID_WATER, GAME_LIQUID_THICK } game_liquid;
game_liquid Game_LiquidFlat(const char *name);
/* Ice floors: smooth and glassy, their painted streaks cut deep grooves. */
int Game_IceFlat(const char *name);
/* Plain metal plates that use has worn: grime in their seams, dull
   blotches, scuffs near the floor and a duller sheen (world/wear.glsl). */
int Game_WornPlate(const char *name, int flat);
/* Grey concrete where gloss comes from the palette ramps (Game_SurfaceGloss
   0), which read grey as metal: these stay nearly matte, as stone. */
int Game_MatteTexture(const char *name, int flat);
/* Floors hot enough for heat haze. */
int Game_HotFlat(const char *name);
/* Sector specials that hurt whoever stands there (toxic mist). */
int Game_DamagingFloor(int special);

/* What walls and floors are made of, for room acoustics, footsteps
   (audio/steps.c: liquids splash, flesh, soil and fabric fall soft) and, in
   games without DOOM's palette ramps, gloss. */
typedef enum {
    GAME_SURFACE_STONE, GAME_SURFACE_METAL, GAME_SURFACE_WOOD,
    GAME_SURFACE_FLESH, GAME_SURFACE_SOIL, GAME_SURFACE_LIQUID,
    GAME_SURFACE_FABRIC
} game_surface;
game_surface Game_WallSurface(const char *name);
game_surface Game_FlatSurface(const char *name);

/* Ambient sound: an AMBIENT_* kind (audio/ambience_spots.h) or -1. */
int Game_AmbientFlat(const char *name, int ceiling);
int Game_AmbientWall(const char *name);
/* What lives behind a one-sided wall with this face, heard now and then
   (audio/hidden.h): a mask of 1 << HIDDEN_* kinds (audio/hidden_synth.h). */
int Game_HiddenWall(const char *name);

#ifdef __cplusplus
}
#endif
#endif
