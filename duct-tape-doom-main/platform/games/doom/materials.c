/* DOOM's answers to platform/game_materials.h: the texture families of the
   DOOM IWADs, Final DOOM, SIGIL, Legacy of Rust and Freedoom. */
#include <ctype.h>
#include <string.h>
#include "../../emissive.h"
#include "../../screen_glass.h"
#include "../../audio/ambient_synth.h"
#include "../../audio/hidden_synth.h"
#include "../../game_materials.h"

static int starts(const char *name, const char *prefix) { return !strncmp(name, prefix, strlen(prefix)); }

static int prefixed(const char *name, const char *const *list)
{
    for (; *list; ++list)
    {
        size_t i = 0;
        while ((*list)[i] && toupper((unsigned char)name[i]) == (*list)[i]) ++i;
        if (!(*list)[i]) return 1;
    }
    return 0;
}

int Game_GlowKind(const char *name, int flat) { return doom_emissive_kind(name, flat); }
int Game_SparkTexture(const char *name, int flat) { return doom_spark_texture(name, flat); }
int Game_ScreenTexture(const char *name, int flat) { return doom_screen_texture(name, flat); }

/* Doom 2's SLIME13-16 share the name but are metal floors; map.cpp also
   asks that the flat animates, which leaves them out. */
game_liquid Game_LiquidFlat(const char *name)
{
    if (!strncmp(name, "FWATER", 6)) return GAME_LIQUID_WATER;
    if (!strncmp(name, "NUKAGE", 6) || !strncmp(name, "SLIME", 5) || !strncmp(name, "BLOOD", 5)
        || !strncmp(name, "LAVA", 4))
        return GAME_LIQUID_THICK;
    return GAME_LIQUID_NONE;
}

int Game_IceFlat(const char *name) { (void)name; return 0; }
/* COMPSPAN: the blank grey front of the computer desks (E1M1). */
int Game_WornPlate(const char *name, int flat) { return !flat && !strcmp(name, "COMPSPAN"); }
/* GRAY5's walls, columns and steps with the FLAT19 floors and FLAT18
   ceilings that go with them. */
int Game_MatteTexture(const char *name, int flat)
{
    return flat ? !strcmp(name, "FLAT19") || !strcmp(name, "FLAT18") : !strcmp(name, "GRAY5");
}
int Game_HotFlat(const char *name) { return !strncmp(name, "LAVA", 4); }

int Game_DamagingFloor(int special)
{
    return special == 4 || special == 5 || special == 7 || special == 11 || special == 16;
}

/* Single textures whose family says otherwise, checked first: checked by
   ear and eye in the maps, or plain in rendered views of them. Where a
   texture could be read as stone or metal it stays stone: metal rings, and
   on big floors that tires the ear. */
typedef struct { const char *name; game_surface surface; } named_surface;

static int named(const char *name, const named_surface *list, game_surface *surface)
{
    for (; list->name; ++list)
        if (!strcmp(name, list->name)) { *surface = list->surface; return 1; }
    return 0;
}

/* The IWADs' texture families; PWAD names that match none sound like stone. */
game_surface Game_WallSurface(const char *name)
{
    static const named_surface singles[] = {
        {"STEP5", GAME_SURFACE_STONE}, {"STEP6", GAME_SURFACE_STONE}, {"DOORRED2", GAME_SURFACE_STONE},
        {"DOORYEL2", GAME_SURFACE_STONE}, {"DOORBLU2", GAME_SURFACE_STONE},
        {"BIGDOOR5", GAME_SURFACE_WOOD}, {"BIGDOOR6", GAME_SURFACE_WOOD}, {"BIGDOOR7", GAME_SURFACE_WOOD},
        {"ZZWOLF5", GAME_SURFACE_WOOD}, {"ZZWOLF6", GAME_SURFACE_WOOD}, {"ZZWOLF7", GAME_SURFACE_WOOD},
        {"ZZWOLF10", GAME_SURFACE_METAL}, {"ZDOORF1", GAME_SURFACE_METAL}, {"ZDOORB1", GAME_SURFACE_METAL},
        {"MIDSPACE", GAME_SURFACE_METAL}, {"MIDBRONZ", GAME_SURFACE_METAL}, {NULL, GAME_SURFACE_STONE}};
    static const char *const metals[] = {"STARTAN", "STARG", "STARBR", "COMP", "METAL", "SUPPORT",
        "SHAWN", "DOOR", "BIGDOOR", "EXITDOOR", "LITE", "PIPE", "PLAT", "TEKWALL", "TEKGREN", "TEKLITE",
        "SLADWALL", "SILVER", "BRONZE", "SPACEW", "NUKE", "STEP", "MIDGRATE", "MIDBARS",
        "ICKDOOR", "TEKBRON", "BROWNPIP", "SPCDOOR", NULL};
    static const char *const woods[] = {"WOOD", "PANEL", "PANBOOK", "PANBORD", "PANCASE", NULL};
    static const char *const fleshes[] = {"SKIN", "SKSNAKE", "SKSPINE", "SP_FACE", "FLESH", "SLOPPY", "DBRAIN", NULL};
    /* A switch is a plate set into a wall the rest of its name tells:
       SW1STON1 is stone, SW1WOOD and SW1PANEL wood, SW1COMP metal. */
    static const char *const stone_switches[] = {"STON", "GSTON", "ROCK", "CMT", "MARB", "BRIK", "GARG",
        "LION", "SATYR", "VINE", "ZIM", "DIRT", NULL};
    game_surface surface;
    if (named(name, singles, &surface)) return surface;
    if (starts(name, "SW1") || starts(name, "SW2"))
    {
        if (prefixed(name + 3, stone_switches)) return GAME_SURFACE_STONE;
        if (starts(name + 3, "WOOD") || starts(name + 3, "PANEL")) return GAME_SURFACE_WOOD;
        if (starts(name + 3, "SKIN")) return GAME_SURFACE_FLESH;
        return GAME_SURFACE_METAL;
    }
    if (prefixed(name, woods)) return GAME_SURFACE_WOOD;
    if (prefixed(name, fleshes)) return GAME_SURFACE_FLESH;
    if (prefixed(name, metals)) return GAME_SURFACE_METAL;
    return GAME_SURFACE_STONE;
}

/* Only SLIME01-08 are sludge: DOOM II's SLIME09-12 are cracked hot rock and
   SLIME13-16 metal plates. FLAT14 is the blue carpet of E1M1 and E1M2;
   the brown hexes (E1M1's main floor among them), the E1M5 stair tiles
   and the brick paving are stone, the slatted slabs metal, the light
   panels metal housings. */
game_surface Game_FlatSurface(const char *name)
{
    static const named_surface singles[] = {
        {"FLOOR5_2", GAME_SURFACE_STONE}, {"FLOOR5_3", GAME_SURFACE_STONE}, {"FLOOR4_6", GAME_SURFACE_STONE},
        {"FLOOR4_8", GAME_SURFACE_STONE},
        {"FLOOR5_4", GAME_SURFACE_STONE}, {"FLAT20", GAME_SURFACE_STONE}, {"FLAT5_3", GAME_SURFACE_WOOD},
        {"CEIL5_2", GAME_SURFACE_METAL}, {"CEIL1_2", GAME_SURFACE_METAL}, {"CEIL1_3", GAME_SURFACE_METAL},
        {"FLAT2", GAME_SURFACE_METAL}, {"FLAT17", GAME_SURFACE_METAL}, {"FLOOR1_7", GAME_SURFACE_METAL},
        {"GRNLITE1", GAME_SURFACE_METAL}, {NULL, GAME_SURFACE_STONE}};
    static const char *const liquids[] = {"NUKAGE", "FWATER", "SWATER", "BLOOD", "LAVA", "WATER", NULL};
    static const char *const soils[] = {"GRASS", "RROCK19", "RROCK20", "MFLR8_3", "FLAT10", NULL};
    static const char *const woods[] = {"FLAT5_1", "FLAT5_2", "CEIL1_1", "CRATOP", NULL};
    static const char *const fleshes[] = {"SFLR", NULL};
    static const char *const fabrics[] = {"FLAT14", NULL};
    static const char *const metals[] = {"FLOOR4", "FLOOR5", "FLAT20", "FLAT22", "FLAT23",
        "CEIL3", "COMP", "GATE", "STEP", "TLITE", "SLIME13", "SLIME14", "SLIME15", "SLIME16", "CONS1", NULL};
    game_surface surface;
    if (named(name, singles, &surface)) return surface;
    if ((starts(name, "SLIME0") && name[6] >= '1' && name[6] <= '8') || prefixed(name, liquids))
        return GAME_SURFACE_LIQUID;
    if (prefixed(name, soils)) return GAME_SURFACE_SOIL;
    if (prefixed(name, woods)) return GAME_SURFACE_WOOD;
    if (prefixed(name, fleshes)) return GAME_SURFACE_FLESH;
    if (prefixed(name, fabrics)) return GAME_SURFACE_FABRIC;
    if (prefixed(name, metals)) return GAME_SURFACE_METAL;
    return GAME_SURFACE_STONE;
}

/* Lamps, the console flats, liquids, hot rock and teleporter pads. */
int Game_AmbientFlat(const char *name, int ceiling)
{
    int glow = doom_emissive_kind(name, 1);
    if (glow == DOOM_EMISSIVE_LAMP) return AMBIENT_LAMP;
    if (glow == DOOM_EMISSIVE_PANEL) return AMBIENT_COMPUTER;
    if (ceiling) return -1;
    if (glow == DOOM_EMISSIVE_LAVA) return AMBIENT_LAVA;
    if (glow == DOOM_EMISSIVE_MAGMA) return AMBIENT_MAGMA;
    if (starts(name, "NUKAGE") || starts(name, "SLIME")) return AMBIENT_NUKAGE;
    if (starts(name, "BLOOD")) return AMBIENT_BLOOD;
    if (starts(name, "FWATER")) return AMBIENT_WATER;
    if (starts(name, "GATE")) return AMBIENT_TELEPORTER;
    return -1;
}

/* Light fixtures and the lit strips beside key doors, computers and
   screens, falls of liquid, fire and hot rock. Walls with only a lamp or a
   few lights painted in (BSTONE3, the LED-dotted TEKWALL) stay quiet, and so
   does the Icon of Sin's wall. */
int Game_AmbientWall(const char *name)
{
    static const char *const lamps[] = { "TLITE", "LITE", "LIGHT", "AQLITE", "TEKLITE", "METLITE",
        "DOORBLU", "DOORRED", "DOORYEL", 0 };
    static const char *const falls[] = { "SFALL", "SLADFAL", "BLODGR", "BLODRIP", "WFALL", 0 };
    int glow = doom_emissive_kind(name, 0);
    for (int i = 0; falls[i]; ++i) if (starts(name, falls[i])) return AMBIENT_SLIMEFALL;
    if (glow == DOOM_EMISSIVE_LAVA) return starts(name, "DBRAIN") ? -1 : AMBIENT_FIRE;
    if (glow == DOOM_EMISSIVE_MAGMA) return AMBIENT_MAGMA;
    if (glow == DOOM_EMISSIVE_LAMP)
    {
        for (int i = 0; lamps[i]; ++i) if (starts(name, lamps[i])) return AMBIENT_LAMP;
        return -1;
    }
    if (glow == DOOM_EMISSIVE_PANEL)
    {
        if (starts(name, "COMP") || starts(name, "AQCOMP")) return AMBIENT_COMPUTER;
        if (starts(name, "PLANET") || starts(name, "SILVER3")) return AMBIENT_CRT;
    }
    return -1;
}

/* Behind a base's walls: pipes carry water and steam, the frame they hang
   on settles, and they sweat; tech panelling hides ducts, the frame and
   machinery farther off; the bases' brown and grey stone only sweats.
   Flesh, marble, wood and the rest hide nothing of this. */
int Game_HiddenWall(const char *name)
{
    static const char *const pipes[] = {"PIPE", "BROWNPIP", "SUPPORT", NULL};
    static const char *const panels[] = {"STARTAN", "STARG", "STARBR", "TEKWALL", "TEKGREN", "TEKBRON",
        "COMPTILE", "COMPSPAN", "SILVER1", "SILVER2", "SHAWN", "METAL", "BRONZE", "SPACEW", "GRAY",
        "ICKWALL", "MODWALL", NULL};
    static const char *const stones[] = {"BROWN", "STONE", NULL};
    if (prefixed(name, pipes)) return 1 << HIDDEN_PIPE_RUSH | 1 << HIDDEN_STEAM | 1 << HIDDEN_SETTLE | 1 << HIDDEN_DRIP;
    if (prefixed(name, panels)) return 1 << HIDDEN_DUCT | 1 << HIDDEN_SETTLE | 1 << HIDDEN_PRESS;
    if (prefixed(name, stones)) return 1 << HIDDEN_DRIP;
    return 0;
}
