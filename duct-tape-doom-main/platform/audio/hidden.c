/* When and where a sound behind the walls plays (hidden.h). At level start
 * the one-sided walls are read for what lives behind them. Every so often
 * one kind is drawn from those the walls in reach hold, and a wall of it,
 * the longer the likelier, plays it from a point along its face. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "doomstat.h"
#include "p_local.h"
#include "r_data.h"
#include "../game_materials.h"
#include "hidden.h"
#include "hidden_synth.h"
#include "map.h"

#define REACH 1100.0f       /* walls farther off than this aren't heard */
#define NEAR 48.0f          /* nor one the listener stands against */
#define SHORTEST 48.0f      /* walls shorter than this are trim, not walls */
#define LOWEST 32.0f        /* nor walls of a room this low */
#define DARK 160            /* sectors this dark or darker sweat */
#define FULL_WALL 512.0f    /* this much of a kind's wall in reach is as likely as any more */
#define GAP_SECONDS 9.0f    /* the mean wait between two sounds, */
#define LEAST_SECONDS 3.0f  /* and the least */
#define CALM_SECONDS 10.0f  /* how long after the player last fired or was hurt */
#define FIRST_SECONDS 4.0f  /* nothing this soon into a level */

/* How each kind plays: how often it comes up, how loud, how far it
   carries, and how much treble gets through the wall. */
static const struct { float weight, level, reach, cutoff; } play[HIDDEN_KINDS] = {
    [HIDDEN_PIPE_RUSH] = { 2, 0.30f, 1, 800 },
    [HIDDEN_STEAM] = { 1, 0.25f, 1, 1200 },
    [HIDDEN_DUCT] = { 2, 0.35f, 1, 600 },
    [HIDDEN_SETTLE] = { 4, 0.30f, 1, 1000 },
    [HIDDEN_PRESS] = { 1, 0.45f, 1.5f, 400 },
    [HIDDEN_DRIP] = { 4, 0.25f, 1, 1200 },
};

typedef struct { float x1, y1, x2, y2, length, floor, ceiling; int kinds; } wall_t;

static wall_t *walls;
static int count, capacity, due, busy, last_kind = -1;
static unsigned built = ~0u, state = 0x6d2b79f5u;

static float unit(void)
{
    state = state * 1103515245u + 12345u;
    return (state >> 8 & 0xffffff) / 16777216.0f;
}
static int tics(float seconds) { return (int)(seconds * TICRATE); }
static void schedule(void)
{
    due = gametic + tics(LEAST_SECONDS - (GAP_SECONDS - LEAST_SECONDS) * logf(fmaxf(unit(), 1e-4f)));
}

static void build(void)
{
    built = r_levelserial; count = 0; last_kind = -1;
    due = gametic + tics(FIRST_SECONDS); busy = gametic - tics(CALM_SECONDS);
    char name[9];
    for (int i = 0; i < numlines; ++i)
    {
        const line_t *line = &lines[i];
        /* Only a one-sided wall has a behind. */
        if (line->sidenum[0] < 0 || line->sidenum[1] >= 0) continue;
        const side_t *side = &sides[line->sidenum[0]];
        const sector_t *s = side->sector;
        float x1 = line->v1->x / (float)FRACUNIT, y1 = line->v1->y / (float)FRACUNIT;
        float x2 = line->v2->x / (float)FRACUNIT, y2 = line->v2->y / (float)FRACUNIT, length = hypotf(x2 - x1, y2 - y1);
        if (length < SHORTEST || side->midtexture <= 0 || Map_Ceiling(s) - Map_Floor(s) < LOWEST) continue;
        R_TextureName(side->midtexture, name);
        int kinds = Game_HiddenWall(name);
        if (s->lightlevel > DARK) kinds &= ~(1 << HIDDEN_DRIP);
        if (!kinds) continue;
        if (count == capacity)
        {
            int more = capacity ? 2 * capacity : 256;
            wall_t *grown = realloc(walls, sizeof(*walls) * (size_t)more);
            if (!grown) break;
            walls = grown; capacity = more;
        }
        walls[count++] = (wall_t){ x1, y1, x2, y2, length, Map_Floor(s), Map_Ceiling(s), kinds };
    }
    int holding[HIDDEN_KINDS] = {0};
    for (int i = 0; i < count; ++i)
        for (int k = 0; k < HIDDEN_KINDS; ++k) holding[k] += walls[i].kinds >> k & 1;
    fprintf(stderr, "Behind the walls:");
    for (int k = 0; k < HIDDEN_KINDS; ++k)
        if (holding[k]) fprintf(stderr, " %s %d", Hidden_Name(k), holding[k]);
    fprintf(stderr, " walls.\n");
}

/* Whether the wall is in reach of the listener at (x, y). */
static int in_reach(const wall_t *w, float x, float y)
{
    float dx = w->x2 - w->x1, dy = w->y2 - w->y1;
    float t = ((x - w->x1) * dx + (y - w->y1) * dy) / (w->length * w->length);
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    float d = hypotf(w->x1 + t * dx - x, w->y1 + t * dy - y);
    return d >= NEAR && d <= REACH;
}

int Hidden_Tic(hidden_event_t *event)
{
    const player_t *player = &players[consoleplayer];
    const mobj_t *listener = player->mo;
    if (gamestate != GS_LEVEL || !listener) return 0;
    if (built != r_levelserial) build();
    if (player->attackdown || player->damagecount) busy = gametic;
    if (gametic < due) return 0;
    if (!count) { due = gametic + tics(GAP_SECONDS); return 0; }
    if (gametic - busy < tics(CALM_SECONDS)) { due = gametic + TICRATE; return 0; }
    schedule();

    /* The kinds the walls in reach hold, and how much wall of each. */
    float x = listener->x / (float)FRACUNIT, y = listener->y / (float)FRACUNIT;
    float amount[HIDDEN_KINDS] = {0}, chance[HIDDEN_KINDS], total = 0;
    for (int i = 0; i < count; ++i)
        if (in_reach(&walls[i], x, y))
            for (int k = 0; k < HIDDEN_KINDS; ++k)
                if (walls[i].kinds & 1 << k) amount[k] += walls[i].length;
    for (int pass = 0; pass < 2 && total <= 0; ++pass)
        for (int k = 0; k < HIDDEN_KINDS; ++k)
        {
            /* Not the last one again, unless nothing else is here. */
            chance[k] = amount[k] > 0 && (pass || k != last_kind) ? play[k].weight * fminf(amount[k] / FULL_WALL, 1) : 0;
            total += chance[k];
        }
    if (total <= 0) return 0;
    int kind = 0;
    for (float roll = unit() * total; kind < HIDDEN_KINDS - 1 && roll >= chance[kind]; ++kind) roll -= chance[kind];

    /* A wall of it, the more of it the likelier; somewhere along its face. */
    const wall_t *w = NULL;
    float roll = unit() * amount[kind];
    for (int i = 0; i < count && !w; ++i)
        if (walls[i].kinds & 1 << kind && in_reach(&walls[i], x, y) && (roll -= walls[i].length) <= 0) w = &walls[i];
    if (!w) return 0;
    float t = 0.15f + 0.7f * unit(), dx = w->x2 - w->x1, dy = w->y2 - w->y1;
    /* A line's front is on its right; the sound sits just off the face. */
    float px = w->x1 + t * dx + 4 * dy / w->length, py = w->y1 + t * dy - 4 * dx / w->length;
    float pz = kind == HIDDEN_PRESS ? w->floor : kind == HIDDEN_DRIP ? w->floor + 8
             : w->floor + fminf(64, 0.5f * (w->ceiling - w->floor));
    last_kind = kind;
    *event = (hidden_event_t){ kind, state >> 8, px, py, pz, play[kind].level * (0.6f + 0.4f * unit()),
                               play[kind].reach, play[kind].cutoff };
    return 1;
}
