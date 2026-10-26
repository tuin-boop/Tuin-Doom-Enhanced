/* What the shared code (renderer, audio, F4 panel) needs to know about the
   game it runs in, in neutral terms, so scene3d/ and audio/ name no game's
   MT_, SPR_, S_ or sfx_ constants. Each game program links its own answers:
   platform/games/<game>/profile.c. */
#ifndef DOOM_GAME_PROFILE_H
#define DOOM_GAME_PROFILE_H
#ifdef __cplusplus
extern "C" {
#endif
struct mobj_s;
struct thinker_s;

/* The program: its name (window title, save folder, message boxes), its
   config file, and what a new config file starts with. */
const char *Game_Title(void);
const char *Game_ConfigFile(void);
const char *Game_ConfigDefaults(void);
/* Which WADs this program plays: a DOOM_WAD_* kind (wad_probe.h). */
int Game_WadKind(void);

/* Whether a thinker is a map object (an mobj_t). */
int Game_ThinkerIsMobj(const struct thinker_s *thinker);

/* The sectors whose light flickers, strobes or glows, and between what
   levels. smooth: glowing and fire-flickering sectors, which fade rather
   than switch. */
typedef void (*game_swing_visit)(int sector, int low, int high, int smooth, void *data);
void Game_LightSwings(game_swing_visit visit, void *data);

/* What a thing type is, as far as lights, shadows, blood and glass care. */
typedef enum {
    GAME_THING_OTHER,
    GAME_THING_ROCKET,            /* explosive missile: blasts lights and glass */
    GAME_THING_BIG_SHOT,          /* the strongest energy missile (BFG ball) */
    GAME_THING_BLOOD,             /* airborne blood */
    GAME_THING_TELEPORT_FOG,      /* teleport and item respawn flashes */
    GAME_THING_BARREL,            /* exploding barrel */
    GAME_THING_UNCOUNTED_MONSTER  /* a monster outside the kill count that still casts a shadow */
} game_thing_role;
game_thing_role Game_ThingRole(int type);

/* An exploding barrel's place in its blast: the frame 0..GAME_BLAST_FRAMES-1,
   or -1 for anything else. From GAME_BLAST_BURST on, the explosion has hit. */
#define GAME_BLAST_FRAMES 5
#define GAME_BLAST_BURST 3
int Game_BlastFrame(const struct mobj_s *thing);

/* A shot's muzzle flash: how far and strong its light reaches (the
   defaults suit a pistol), and the state whose frame colours it (-1: the
   shooter's own frame). */
void Game_MuzzleFlash(int weapon, float *radius, float *strength, int *flashstate);

/* How much a weapon rattles on the player's back as they walk: 0 a light
   one (fist, pistol), 1 a long gun on a sling, 2 a heavy one. */
int Game_WeaponHeft(int weapon);

/* The game's own look up and down, if it has one (Heretic's lookdir, which
   its aiming follows): the view pitch in radians, positive up, and mouselook
   turning it. Game_LookPitch returns 0 when the game has none and the
   renderer keeps its own pitch (DOOM). */
int Game_LookPitch(float *pitch);
void Game_Look(float delta);

/* Sprites the effects borrow artwork from: a sprite number, or -1. */
typedef enum { GAME_SPRITE_BLOOD, GAME_SPRITE_PUFF, GAME_SPRITE_EXPLOSION, GAME_SPRITE_TORCH } game_sprite_role;

/* A torch the player carries (Heretic's Torch item): 1 while the game's
   fixed colormap is its light, with *flame how brightly it burns (0-1, the
   game's own flicker). The renderer lights the view with a carried flame
   and a faint lift instead of that colormap. */
int Game_Torch(float *flame);
int Game_Sprite(game_sprite_role role);

/* Effects made for DOOM's world that a game can do without: their F4
   options stay off and greyed out where Game_HasEffect says no. */
typedef enum {
    GAME_EFFECT_WINDOW_GLASS,     /* panes in windows, shattering */
    GAME_EFFECT_SHOOTABLE_LIGHTS, /* lamps and screens shot out, machinery sparks */
    GAME_EFFECT_FLASHLIGHT,       /* the F key's flashlight */
    GAME_EFFECT_WIND,             /* made-up wind under the sky and through openings */
    GAME_EFFECT_NATURAL_FALLOFF,  /* sounds fading with distance as in air, in place of the game's own curve */
    GAME_EFFECT_FREE_AIM,         /* mouselook bullets going where the view points (else the game's own aiming) */
    GAME_EFFECT_AUTHORED_EPISODES /* hand-picked looks for the game's own episodes (Game_Mood) */
} game_effect;
int Game_HasEffect(game_effect effect);

/* The outdoor light. fromSky: 1 reads the sun from the sky texture: its
   height where the sky paints its bright patch (low patches give long
   shadows), and from how bright the sky is, how strong it shines and in
   what colour (a dark sky gives soft moonlight in its own hue); 0 keeps it
   at 40 degrees in the bright columns' tint. shade: the share of light
   shaded outdoor surfaces lose; gain: the share sunlit ones win (walls by
   how squarely they face the sun). skyCast: how much of the outdoor light
   takes the sky's colour. haze: how far outdoor surfaces fade into the
   sky's horizon colour with distance (0 none, 1 strong). */
typedef struct { int fromSky; float shade, gain, skyCast, haze; } game_sun;
void Game_Sun(game_sun *sun);

/* Authored episodes (games/<game>/authored.c): looks hand-picked for the
   game's own maps, beyond what the WAD data gives, so episodes can differ in
   mood. Not DERP's derived look: F4 "Authored episodes" switches the whole
   layer off, and maps a custom WAD brings never get it. Game_Mood fills in
   mood for one of the game's own maps and leaves it alone for one it has
   nothing for (it starts as { 1, 1, 0, 0, -1, 1, { 1, 1, 1 } }).
   outdoor: what the light under the sky keeps (1 as mapped); sun: 0 hides
   the sun (no sunlight, disc or sunbeams); shineOut: window light turns
   round, the lit rooms shining out through their openings into the dark
   outdoors instead of the outdoors lighting the rooms; rain: it pours under
   the sky (F4 Rain); rainAngle: the way the wind blows the rain, in degrees
   as map angles go (0 east, 90 north), whatever the sun does; below 0 the
   level picks its own; rainAmount: how hard (1 a downpour, 0.5 half as
   many drops); skyTint: what the sky and the haze under it are drawn
   in (1, 1, 1 as painted). */
typedef struct { float outdoor; int sun, shineOut, rain; float rainAngle, rainAmount, skyTint[3]; } game_mood;
void Game_Mood(int episode, int map, game_mood *mood);

/* Also authored: windows one of the game's own maps has broken before the
   player comes, each the window nearest a spot (x, y). Fills at most max
   spots, returns how many. */
typedef struct { float x, y; } game_spot;
int Game_BrokenWindows(int episode, int map, game_spot *spots, int max);

/* How clearly water mirrors the room, 0 to 1: 0 is murky (DOOM: stained in
   the water's hue, only bright things punch through), 1 a clear pool that
   mirrors walls too. */
float Game_WaterClarity(void);

/* Where surface gloss comes from: 0 DOOM's palette ramps (greys and blues
   read as metal, greens as slime); 1 each texture's material
   (Game_WallSurface/Game_FlatSurface) and its texels' own colour, for
   palettes laid out differently from DOOM's. */
int Game_SurfaceGloss(void);

/* Wind a sector special blows (Heretic's Wind_East to Wind_West, which push
   things in the game), in map units per second toward east and north;
   0 where none. */
int Game_SectorWind(int special, float *east, float *north);

/* Decorations that light the room although their artwork isn't drawn
   fullbright (Heretic's chandeliers and serpent torches), by sprite number.
   Fullbright decorations light the room anyway. */
int Game_DecorationLight(int sprite);

/* Decorations the ambience gives a sound, by sprite number. */
typedef enum {
    GAME_DECORATION_NONE,
    GAME_DECORATION_LAMP,         /* standing lamp or lit column */
    GAME_DECORATION_TORCH,        /* tall torch or burning barrel */
    GAME_DECORATION_SHORT_TORCH,
    GAME_DECORATION_CANDLES
} game_decoration;
game_decoration Game_Decoration(int sprite);

/* Sound effects: I_GetSfxLumpNum (i_sound.h) is the game's, since lump
   names differ; Game_SfxLoaded tells the game where an effect's data sits. */
void Game_SfxLoaded(int id, void *data);
/* How much farther than the game's usual range an effect carries: 1 for
   most, more for the loud ones (gunfire, explosions), heard far off under
   the natural falloff. */
float Game_SoundCarry(int id);
/* Hitscan shots: traced is told of each, from the shooter's muzzle to where
   it stopped, in map units, and what it hit (NULL: a wall, floor or the end
   of its range), for sounds that follow the bullet. NULL stops telling. A
   game without hitscan tracing ignores it. */
typedef void (*game_shot_traced)(const struct mobj_s *shooter, const struct mobj_s *hit,
                                 float x1, float y1, float z1, float x2, float y2, float z2);
void Game_TraceShots(game_shot_traced traced);

/* The F4 panel's font: the WAD lump of character c ('!' to '_'), or -1,
   and how many pixels to add after each glyph (the art's own spacing). */
int Game_FontLump(int c);
int Game_FontSpacing(void);

/* The F4 panel's clicks: a sound number for S_StartSound. */
typedef enum {
    GAME_MENU_MOVE,    /* selection or tab moved */
    GAME_MENU_DENY,    /* option not available */
    GAME_MENU_CHANGE,  /* option changed */
    GAME_MENU_OPEN,
    GAME_MENU_CLOSE
} game_menu_sound;
int Game_MenuSound(game_menu_sound sound);

#ifdef __cplusplus
}
#endif
#endif
