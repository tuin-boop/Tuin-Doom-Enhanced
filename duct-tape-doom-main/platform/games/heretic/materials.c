/* Heretic's answers to platform/game_materials.h, from a contact sheet of
   the HERETIC.WAD walls and flats (v1.3 and the 2025 IWAD share them). */
#include <string.h>
#include "../../emissive.h"
#include "../../audio/ambient_synth.h"
#include "../../game_materials.h"

static int starts(const char *name, const char *prefix) { return !strncmp(name, prefix, strlen(prefix)); }

static int any(const char *name, const char *const *list)
{
    for (; *list; ++list) if (starts(name, *list)) return 1;
    return 0;
}

/* Lava falls and lava floors glow; LAVA1's cracks glow, the rock around them
   stays shaded; the orange gems set into the grey stone (GRSTNPBV,
   GRSTNPBW, BADPATCH) are small lamps; the stained glass windows (STNGLS*)
   glow softly in their own colours, as if lit from behind. */
int Game_GlowKind(const char *name, int flat)
{
    static const char *const lavaFlats[] = { "FLATHUH", "FLTLAVA", 0 };
    static const char *const lampWalls[] = { "GRSTNPBV", "GRSTNPBW", "BADPATCH", 0 };
    if (flat) return any(name, lavaFlats) ? DOOM_EMISSIVE_LAVA : DOOM_EMISSIVE_NONE;
    if (starts(name, "LAVAFL")) return DOOM_EMISSIVE_LAVA;
    if (!strcmp(name, "LAVA1")) return DOOM_EMISSIVE_MAGMA;
    if (any(name, lampWalls)) return DOOM_EMISSIVE_LAMP;
    if (starts(name, "STNGLS")) return DOOM_EMISSIVE_GLASS;
    return DOOM_EMISSIVE_NONE;
}

/* No machinery and no monitors in Heretic's world. */
int Game_SparkTexture(const char *name, int flat) { (void)name; (void)flat; return 0; }
int Game_ScreenTexture(const char *name, int flat) { (void)name; (void)flat; return 0; }

/* The animated liquids of Heretic's own terrain table (p_spec.c). */
game_liquid Game_LiquidFlat(const char *name)
{
    if (starts(name, "FLTWAWA") || starts(name, "FLTFLWW")) return GAME_LIQUID_WATER;
    if (starts(name, "FLTSLUD") || starts(name, "FLTLAVA") || starts(name, "FLATHUH")) return GAME_LIQUID_THICK;
    return GAME_LIQUID_NONE;
}

/* FLAT517: the Ice Grotto's blue ice floor (E2M4, E2M9...). */
int Game_IceFlat(const char *name) { return !strcmp(name, "FLAT517"); }
int Game_WornPlate(const char *name, int flat) { (void)name; (void)flat; return 0; }
int Game_MatteTexture(const char *name, int flat) { (void)name; (void)flat; return 0; }
int Game_HotFlat(const char *name) { return starts(name, "FLTLAVA") || starts(name, "FLATHUH"); }

/* Damage_LavaWimpy, Damage_LavaHefty, Scroll_EastLavaDamage, Damage_Sludge. */
int Game_DamagingFloor(int special)
{
    return special == 4 || special == 5 || special == 7 || special == 16;
}

static const char *const metalFlats[] = { "FLOOR08", "FLOOR28", "FLOOR29", "FLOOR30", 0 };
static const char *const woodFlats[] = { "FLOOR10", "FLOOR25", "FLAT508", 0 };
static const char *const soilFlats[] = { "FLOOR01", "FLOOR05", "FLOOR17", "FLOOR19", "FLOOR27",
    "FLAT510", "FLAT512", "FLAT516", 0 };

/* Riveted iron (METL, the switch plates, skull panels, the iron gates),
   timber (wooden doors and walls, half-timbered city walls, carved wood,
   roots); everything else is stone. */
game_surface Game_WallSurface(const char *name)
{
    static const char *const metals[] = { "METL", "SW1", "SW2", "SKULLSB", "GATMETL", 0 };
    static const char *const woods[] = { "DOORWOOD", "WOODWL", "CTYSTUC", "CTYSTCI", "CELTIC",
        "ROOTWALL", 0 };
    if (any(name, woods)) return GAME_SURFACE_WOOD;
    if (any(name, metals)) return GAME_SURFACE_METAL;
    return GAME_SURFACE_STONE;
}

game_surface Game_FlatSurface(const char *name)
{
    if (Game_LiquidFlat(name) != GAME_LIQUID_NONE) return GAME_SURFACE_LIQUID;
    if (any(name, soilFlats)) return GAME_SURFACE_SOIL;
    if (any(name, woodFlats)) return GAME_SURFACE_WOOD;
    if (any(name, metalFlats)) return GAME_SURFACE_METAL;
    return GAME_SURFACE_STONE;
}

/* Lava rumbles, sludge bubbles, water laps, teleport pads throb. */
int Game_AmbientFlat(const char *name, int ceiling)
{
    if (ceiling) return -1;
    if (Game_HotFlat(name)) return AMBIENT_LAVA;
    if (starts(name, "FLTSLUD")) return AMBIENT_NUKAGE;
    if (starts(name, "FLTWAWA") || starts(name, "FLTFLWW")) return AMBIENT_WATER;
    if (starts(name, "FLTTELE")) return AMBIENT_TELEPORTER;
    return -1;
}

/* Lava falls roar, LAVA1 ticks; the gems are silent. Waterfalls have their
   own sound in Heretic (MT_SOUNDWATERFALL things), so they get none here. */
int Game_AmbientWall(const char *name)
{
    if (starts(name, "LAVAFL")) return AMBIENT_FIRE;
    if (!strcmp(name, "LAVA1")) return AMBIENT_MAGMA;
    return -1;
}
/* Heretic plays its own ambient sequences; nothing is made up behind its walls. */
int Game_HiddenWall(const char *name) { (void)name; return 0; }
