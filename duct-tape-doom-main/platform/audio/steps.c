/* Footsteps. A step falls every stride walked on the ground, the stride
 * growing with speed, so walking steps about twice a second and running
 * three times, harder. Landing from a fall is a step too, harder the faster
 * the fall. Each step plays the footfall and the kit after it. Monsters are followed only
 * while they wade, for their splashes. See steps.h. */
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "doomstat.h"
#include "p_local.h"
#include "w_wad.h"
#include "impact_synth.h"
#include "steps.h"
#include "../game_profile.h"
#include "../game_materials.h"

#define WADERS 64
#define MONSTER_STRIDE 44.0f  /* monsters walk in jumps; a splash every so many units */
#define WADE_REACH 1024.0f

extern int numflats;


/* What a floor is made of (Game_FlatSurface), as a step: the same class
   the room's echo hears, so the two can't disagree. Water splashes light,
   other liquids thick. */
static int step_kind(const char *name)
{
    switch (Game_FlatSurface(name))
    {
    case GAME_SURFACE_LIQUID: return Game_LiquidFlat(name) == GAME_LIQUID_THICK ? IMPACT_SPLASH_THICK : IMPACT_SPLASH_WATER;
    case GAME_SURFACE_METAL: return IMPACT_STEP_METAL;
    case GAME_SURFACE_WOOD: return IMPACT_STEP_WOOD;
    case GAME_SURFACE_FLESH: case GAME_SURFACE_SOIL: case GAME_SURFACE_FABRIC: return IMPACT_STEP_SOFT;
    default: return IMPACT_STEP_STONE;
    }
}

/* What a floor flat sounds like underfoot, named once per WAD. */
static int floor_kind(int pic)
{
    static signed char *kinds;
    static int known;
    if (known != numflats)
    {
        free(kinds);
        kinds = malloc((size_t)(numflats > 0 ? numflats : 1));
        known = kinds ? numflats : 0;
        for (int i = 0; i < known; ++i)
        {
            char name[9];
            for (int c = 0; c < 8; ++c) name[c] = (char)toupper((unsigned char)lumpinfo[firstflat + i].name[c]);
            name[8] = 0;
            kinds[i] = (signed char)step_kind(name);
        }
    }
    return pic >= 0 && pic < known ? kinds[pic] : IMPACT_STEP_STONE;
}
static int wet(int kind) { return kind == IMPACT_SPLASH_WATER || kind == IMPACT_SPLASH_THICK; }
static float units(fixed_t v) { return v / (float)FRACUNIT; }
static int grounded(const mobj_t *mo) { return mo->z <= mo->floorz + FRACUNIT / 2; }

static int emit(step_event_t *events, int count, int max, int kind, const mobj_t *mo, float level)
{
    if (count >= max) return count;
    events[count] = (step_event_t){ kind, units(mo->x), units(mo->y), units(mo->z), level,
                                  mo == players[consoleplayer].mo };
    return count + 1;
}
/* A step of the player's: the footfall and the kit rattling after it
   (Step_Layers), by how hard it lands and what they carry. */
static int emit_step(step_event_t *events, int count, int max, int kind, const mobj_t *mo, float force)
{
    const player_t *player = mo->player;
    impact_layer_t layers[3];
    int layer_count = Step_Layers(kind, force, player ? player->armortype : 0,
                                  player ? Game_WeaponHeft(player->readyweapon) : 0, layers);
    for (int i = 0; i < layer_count; ++i) count = emit(events, count, max, layers[i].kind, mo, layers[i].level);
    return count;
}

/* The player: a step every stride on the ground, and on landing. */
static int player_steps(step_event_t *events, int count, int max)
{
    static const mobj_t *last_mo;
    static float last_x, last_y, last_z, walked;
    static int was_grounded = 1;
    const mobj_t *mo = players[consoleplayer].mo;
    if (!mo) return count;
    float x = units(mo->x), y = units(mo->y), z = units(mo->z);
    int ground = grounded(mo), kind = floor_kind(mo->subsector->sector->floorpic);
    if (mo != last_mo) { last_mo = mo; last_x = x; last_y = y; last_z = z; was_grounded = ground; walked = 0; return count; }
    float moved = hypotf(x - last_x, y - last_y), fall = last_z - z;
    last_x = x; last_y = y; last_z = z;
    if (moved > 64) { walked = 0; was_grounded = ground; return count; }  /* teleported */
    /* Landing: harder the faster the fall (the last tic's drop). */
    if (ground && !was_grounded && fall > 6) count = emit_step(events, count, max, kind, mo, 1 + fminf((fall - 6) / 14, 1));
    was_grounded = ground;
    if (!ground) return count;
    if (moved < 0.5f) { walked = 0; return count; }
    /* Steps a second: about two walking (8 units a tic), three running (16). */
    float stride = moved * TICRATE / (1.4f + 0.1f * moved);
    if (walked == 0) walked = 0.6f * stride;  /* the first comes soon after setting off */
    walked += moved;
    /* Force: 0 at a walk (8 units a tic), 1 at a run (16). */
    if (walked >= stride) { walked -= stride; count = emit_step(events, count, max, kind, mo, fminf(fmaxf((moved - 8) / 8, 0), 1)); }
    return count;
}

/* Monsters wading: tracked while they stand in a liquid near the listener. */
typedef struct { const mobj_t *mo; float x, y, walked; int seen; } wader_t;
static wader_t waders[WADERS];

static int monster_splashes(step_event_t *events, int count, int max)
{
    const mobj_t *listener = players[consoleplayer].mo;
    if (!listener) return count;
    float lx = units(listener->x), ly = units(listener->y);
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next)
    {
        if (!Game_ThinkerIsMobj(th)) continue;
        const mobj_t *mo = (const mobj_t *)th;
        if (mo->player || !(mo->flags & MF_SHOOTABLE) || (mo->flags & MF_CORPSE) || mo->health <= 0 || !grounded(mo)) continue;
        int kind = floor_kind(mo->subsector->sector->floorpic);
        float x = units(mo->x), y = units(mo->y);
        if (!wet(kind) || hypotf(x - lx, y - ly) > WADE_REACH) continue;
        wader_t *w = NULL, *free_slot = NULL;
        for (int i = 0; i < WADERS && !w; ++i)
        {
            if (waders[i].mo == mo) w = &waders[i];
            else if (!waders[i].mo && !free_slot) free_slot = &waders[i];
        }
        if (!w)
        {
            if (!free_slot) continue;
            *free_slot = (wader_t){ mo, x, y, 0.5f * MONSTER_STRIDE, gametic };
            continue;
        }
        float moved = hypotf(x - w->x, y - w->y);
        w->x = x; w->y = y; w->seen = gametic;
        if (moved > 64) continue;
        w->walked += moved;
        if (w->walked >= MONSTER_STRIDE) { w->walked -= MONSTER_STRIDE; count = emit(events, count, max, kind, mo, 0.5f); }
    }
    /* Forget those that left the liquid, died or were removed. */
    for (int i = 0; i < WADERS; ++i) if (waders[i].mo && waders[i].seen != gametic) waders[i].mo = NULL;
    return count;
}

int Steps_Tic(step_event_t *events, int max)
{
    static unsigned level = ~0u;
    if (gamestate != GS_LEVEL || !players[consoleplayer].mo || !players[consoleplayer].mo->subsector) return 0;
    if (level != r_levelserial) { level = r_levelserial; memset(waders, 0, sizeof(waders)); }
    int count = player_steps(events, 0, max);
    return monster_splashes(events, count, max);
}
