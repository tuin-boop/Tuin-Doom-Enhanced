/* Reads a level for its ambient sound sources: lamps, screens and computers,
 * liquids, fire walls, lava and magma, teleporter pads, torches and standing
 * lamps among the things, and the open air under the sky with the openings
 * the wind and rain come in by, where the rain taps on any glass in them.
 * Sources of a kind close together join into a spot. */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "p_local.h"
#include "r_data.h"
#include "w_wad.h"
#include "../game_profile.h"
#include "../game_materials.h"
#include "ambience_spots.h"
#include "map.h"

#define CELL 128.0f      /* sources of a kind within a cell join into one spot */


spots_t ambience_spots[AMBIENT_KINDS];
unsigned ambience_built = ~0u;

/* A sector's light special: the range it swings through, if it swings. */
typedef struct { short low, high; char living; } swing_t;
static swing_t *swings;

/* A flat's area or a wall's face, in square units, reaches a full voice. */
const reach_t ambience_reach[AMBIENT_KINDS] = {
    [AMBIENT_LAMP] = { 4096, 32, 384 },
    [AMBIENT_CRT] = { 8192, 48, 448 },
    [AMBIENT_COMPUTER] = { 16384, 64, 640 },
    [AMBIENT_TELEPORTER] = { 4096, 48, 448 },
    [AMBIENT_NUKAGE] = { 65536, 96, 1024 },
    [AMBIENT_SLIMEFALL] = { 8192, 96, 1024 },
    [AMBIENT_BLOOD] = { 65536, 96, 896 },
    [AMBIENT_WATER] = { 32768, 64, 768 },
    [AMBIENT_LAVA] = { 65536, 128, 1280 },
    [AMBIENT_FIRE] = { 16384, 96, 896 },
    [AMBIENT_MAGMA] = { 16384, 64, 640 },
    [AMBIENT_WIND] = { 4096, 128, 896 },
    [AMBIENT_RAIN] = { 4096, 128, 768 },
    [AMBIENT_GLASS_RAIN] = { 4096, 48, 512 },
};

static float units(fixed_t v) { return v / (float)FRACUNIT; }

/* Adds a source to its spot, which it returns (NULL when out of memory).
   Openings to the sky keep spots of their own, apart from the open air. */
static spot_t *add_spot(int kind, float x, float y, float z, float size, int sector, int portal)
{
    if (kind < 0 || size <= 0) return NULL;
    if (kind == AMBIENT_WIND && !Game_HasEffect(GAME_EFFECT_WIND)) return NULL; /* the game's own wind */
    spots_t *list = &ambience_spots[kind];
    int cx = (int)floorf(x / CELL), cy = (int)floorf(y / CELL), cz = (int)floorf(z / CELL) + (portal ? 1 << 20 : 0);
    spot_t *spot = NULL;
    for (int i = 0; i < list->count && !spot; ++i)
        if (list->spot[i].cx == cx && list->spot[i].cy == cy && list->spot[i].cz == cz) spot = &list->spot[i];
    if (!spot)
    {
        if (list->count == list->capacity)
        {
            int capacity = list->capacity ? 2 * list->capacity : 64;
            spot_t *grown = realloc(list->spot, (size_t)capacity * sizeof(spot_t));
            if (!grown) return NULL;
            list->spot = grown; list->capacity = capacity;
        }
        spot = &list->spot[list->count++];
        *spot = (spot_t){ .light = 1, .cx = cx, .cy = cy, .cz = cz, .sector = sector, .portal = portal,
                          .line = -1, .muffle = 1, .cutoff = 20000, .muffle_to = 1, .cutoff_to = 20000, .heard = -1000,
                          .measured = -1000 };
    }
    spot->x += x * size; spot->y += y * size; spot->z += z * size; spot->size += size;
    /* A spot's lamps follow the light of the sector most of them are in. */
    if (size > spot->biggest) { spot->biggest = size; spot->sector = sector; }
    return spot;
}
static spot_t *add(int kind, float x, float y, float z, float size, int sector)
{
    return add_spot(kind, x, y, z, size, sector, 0);
}


/* Lamps, consoles, liquids, hot rock and teleporter pads; light fixtures,
   computers and screens, falls of liquid, fire and hot rock on walls: each
   game's names say which (game_materials.h). */
static int flat_kind(const char *name, int ceiling) { return Game_AmbientFlat(name, ceiling); }
static int wall_kind(const char *name) { return Game_AmbientWall(name); }

static void flat_name(int pic, char name[9])
{
    for (int i = 0; i < 8; ++i) name[i] = (char)toupper((unsigned char)lumpinfo[firstflat + pic].name[i]);
    name[8] = 0;
}

/* Every subsector's floor and ceiling, as a spot of its area. */
static void read_flats(void)
{
    for (int i = 0; i < numsubsectors; ++i)
    {
        const subsector_t *ss = &subsectors[i];
        const sector_t *sector = ss->sector;
        float area = 0, cx = 0, cy = 0;
        for (int j = 0; j < ss->numlines; ++j)
        {
            const vertex_t *a = segs[ss->firstline + j].v1, *b = segs[ss->firstline + (j + 1) % ss->numlines].v1;
            float ax = units(a->x), ay = units(a->y), bx = units(b->x), by = units(b->y), cross = ax * by - bx * ay;
            area += cross; cx += (ax + bx) * cross; cy += (ay + by) * cross;
        }
        if (fabsf(area) < 1) continue;
        cx /= 3 * area; cy /= 3 * area; area = 0.5f * fabsf(area);
        char name[9];
        int s = (int)(sector - sectors);
        flat_name(sector->floorpic, name);
        add(flat_kind(name, 0), cx, cy, Map_Floor(sector), area, s);
        /* Under the sky the wind always blows, a little above head height;
           the rain, when it rains, falls there. */
        if (Map_Sky(sector))
        {
            add(AMBIENT_WIND, cx, cy, Map_Floor(sector) + 64, area, s);
            add(AMBIENT_RAIN, cx, cy, Map_Floor(sector) + 32, area, s);
            continue;
        }
        flat_name(sector->ceilingpic, name);
        add(flat_kind(name, 1), cx, cy, Map_Ceiling(sector), area, s);
    }
}

/* A wall section from bottom to top, a spot every 64 units along it. */
static void wall(int texture, const line_t *line, float bottom, float top, int sector)
{
    if (texture <= 0 || top <= bottom) return;
    char name[9];
    R_TextureName(texture, name);
    int kind = wall_kind(name);
    if (kind < 0) return;
    float ax = units(line->v1->x), ay = units(line->v1->y), dx = units(line->v2->x) - ax, dy = units(line->v2->y) - ay;
    float length = hypotf(dx, dy);
    int pieces = (int)ceilf(length / 64);
    for (int p = 0; p < pieces; ++p)
    {
        float t = (p + 0.5f) / pieces;
        add(kind, ax + dx * t, ay + dy * t, 0.5f * (bottom + top), length / pieces * (top - bottom), sector);
    }
}

/* Where rain would tap on a pane, should one stand in this opening: on the
   inside of it, a spot of its own, heard only while the glass is whole
   (Ambience_Glaze) and it rains. */
static void pane(const line_t *line, float x, float y, float z, float size, int sector)
{
    spots_t *list = &ambience_spots[AMBIENT_GLASS_RAIN];
    if (list->count == list->capacity)
    {
        int capacity = list->capacity ? 2 * list->capacity : 64;
        spot_t *grown = realloc(list->spot, (size_t)capacity * sizeof(spot_t));
        if (!grown) return;
        list->spot = grown; list->capacity = capacity;
    }
    list->spot[list->count++] = (spot_t){ .x = x * size, .y = y * size, .z = z * size, .size = size, .biggest = size,
                                          .light = 1, .sector = sector, .portal = 1, .line = (int)(line - lines),
                                          .muffle = 1, .cutoff = 20000, .muffle_to = 1, .cutoff_to = 20000,
                                          .heard = -1000, .measured = -1000 };
}

/* An opening from under a ceiling to under the sky - a window, a doorway
   out - is where the wind comes in, a few units out on the sky side, so a
   pane in it stands between it and anyone inside. */
static void portal(const line_t *line)
{
    const sector_t *front = line->frontsector, *back = line->backsector;
    if (!front || !back || Map_Sky(front) == Map_Sky(back)) return;
    float bottom, top;
    Map_Gap(front, back, &bottom, &top);
    if (top - bottom < 8) return;
    /* It belongs to the sky side, so a pane in the opening stands between it and anyone inside. */
    const sector_t *sky = Map_Sky(front) ? front : back;
    float ax = units(line->v1->x), ay = units(line->v1->y), dx = units(line->v2->x) - ax, dy = units(line->v2->y) - ay;
    float length = hypotf(dx, dy), out = Map_Sky(front) ? 4 : -4;  /* the front lies to the right */
    if (length < 1) return;
    top = fminf(top, bottom + 128);
    for (int kind = AMBIENT_WIND; kind <= AMBIENT_RAIN; ++kind)
        add_spot(kind, ax + 0.5f * dx + out * dy / length, ay + 0.5f * dy - out * dx / length, 0.5f * (bottom + top),
                 2 * length * (top - bottom), (int)(sky - sectors), 1);
    pane(line, ax + 0.5f * dx - out * dy / length, ay + 0.5f * dy + out * dx / length, 0.5f * (bottom + top),
         length * (top - bottom), (int)((sky == front ? back : front) - sectors));
}

static void read_walls(void)
{
    for (int i = 0; i < numlines; ++i)
    {
        const line_t *line = &lines[i];
        portal(line);
        for (int side = 0; side < 2; ++side)
        {
            if (line->sidenum[side] < 0) continue;
            const side_t *s = &sides[line->sidenum[side]];
            const sector_t *front = s->sector, *back = side ? line->frontsector : line->backsector;
            int sector = (int)(front - sectors);
            if (!back) { wall(s->midtexture, line, Map_Floor(front), Map_Ceiling(front), sector); continue; }
            if (Map_Ceiling(back) < Map_Ceiling(front) && !(Map_Sky(front) && Map_Sky(back)))
                wall(s->toptexture, line, Map_Ceiling(back), Map_Ceiling(front), sector);
            if (Map_Floor(back) > Map_Floor(front))
                wall(s->bottomtexture, line, Map_Floor(front), Map_Floor(back), sector);
            float bottom, top;
            Map_Gap(front, back, &bottom, &top);
            wall(s->midtexture, line, bottom, fminf(top, Map_Ceiling(front)), sector);
        }
    }
}

/* Standing lamps buzz, torches and burning barrels crackle like the fire
   walls, a candelabra faintly. Known by their sprites, so DEHACKED things
   that look like them sound like them. Standing lamps buzz in a steady light
   too: they are right there in the room, not a strip in the ceiling. */
static void read_things(void)
{
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next)
    {
        if (!Game_ThinkerIsMobj(th)) continue;
        const mobj_t *mo = (const mobj_t *)th;
        float x = units(mo->x), y = units(mo->y), z = units(mo->z), tall = units(mo->height);
        int sector = mo->subsector ? (int)(mo->subsector->sector - sectors) : 0;
        switch (Game_Decoration(mo->sprite))
        {
        case GAME_DECORATION_LAMP:
        {
            spot_t *lamp = add(AMBIENT_LAMP, x, y, z + 0.8f * tall, 4096, sector);
            if (lamp) lamp->standing = 1;
            break;
        }
        case GAME_DECORATION_TORCH:
            add(AMBIENT_FIRE, x, y, z + 0.7f * tall, 6000, sector); break;
        case GAME_DECORATION_SHORT_TORCH:
            add(AMBIENT_FIRE, x, y, z + 0.7f * tall, 4000, sector); break;
        case GAME_DECORATION_CANDLES:
            add(AMBIENT_FIRE, x, y, z + 0.8f * tall, 1200, sector); break;
        default: break;
        }
    }
}

/* The sectors whose light flickers, strobes or glows, and between what. */
static void store_swing(int sector, int low, int high, int smooth, void *data)
{
    (void)smooth; (void)data;
    swings[sector] = (swing_t){ (short)low, (short)high, 1 };
}

static void read_swings(void)
{
    free(swings);
    swings = calloc((size_t)numsectors, sizeof(swing_t));
    if (!swings) return;
    Game_LightSwings(store_swing, NULL);
}

void Ambience_Build(void)
{
    for (int k = 0; k < AMBIENT_KINDS; ++k) ambience_spots[k].count = 0;
    read_flats();
    read_walls();
    read_things();
    read_swings();
    for (int k = 0; k < AMBIENT_KINDS; ++k)
        for (int i = 0; i < ambience_spots[k].count; ++i)
        {
            spot_t *s = &ambience_spots[k].spot[i];
            s->x /= s->size; s->y /= s->size; s->z /= s->size;
        }
    ambience_built = r_levelserial;
    fprintf(stderr, "Ambience:");
    for (int k = 0; k < AMBIENT_KINDS; ++k)
        if (ambience_spots[k].count) fprintf(stderr, " %s %d", Ambient_Name(k), ambience_spots[k].count);
    fprintf(stderr, " spots.\n");
}

float Ambience_LampLight(const spot_t *s, int *living)
{
    const swing_t *swing = swings && s->sector >= 0 && s->sector < numsectors ? &swings[s->sector] : NULL;
    *living = swing && swing->living;
    if (!*living) return 1;
    float f = (sectors[s->sector].lightlevel - swing->low) / (float)(swing->high - swing->low);
    return f < 0 ? 0 : f > 1 ? 1 : f;
}
