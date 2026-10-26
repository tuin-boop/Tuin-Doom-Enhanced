/* Finds the bigger space beyond the listener's room (beyond.h). The rays
 * that run out of the room together, much farther than its middle reach,
 * go through an opening (or down a long corridor). Halfway from the opening
 * to the far wall the space is measured as a room of its own, the
 * farthest-reaching runs first.
 * Only a space that is clearly bigger and rings longer counts, so stepping
 * from a hall into the corridor hears the hall, and the corridor seen from
 * the hall adds nothing. */
#include <math.h>
#include <string.h>
#include "beyond.h"

#define ESCAPE_TIMES 2.0f   /* a ray this many times the room's middle reach runs out of it, */
#define ESCAPE_MORE 256.0f  /* and at least this much farther */
#define BIGGER 2.0f         /* the space beyond holds this many times the room's air, */
#define LONGER 1.2f         /* and rings this much longer */
#define OPEN_SHARE 0.125f   /* an opening filling an eighth of the circle is wide open */
#define FALLOFF 512.0f      /* an opening this much farther off than close by couples half */
#define SPEED 10700.0f      /* sound, map units per second, as room.c */
#define TRIES 2             /* openings measured at most, the farthest-reaching first */
#define PI_F 3.14159265f

static float middle_reach(const room_outline_t *o)
{
    float v[ROOM_RAYS];
    memcpy(v, o->reach, sizeof(v));
    for (int i = 1; i < ROOM_RAYS; ++i)
        for (int j = i; j > 0 && v[j - 1] > v[j]; --j) { float t = v[j]; v[j] = v[j - 1]; v[j - 1] = t; }
    return 0.5f * (v[ROOM_RAYS / 2 - 1] + v[ROOM_RAYS / 2]);
}

/* How far the floor plan reaches from its point toward (dx, dy), between its rays. */
static float reach_toward(const room_outline_t *o, float dx, float dy)
{
    float a = atan2f(dy, dx);
    if (a < 0) a += 2 * PI_F;
    float f = a / (2 * PI_F) * ROOM_RAYS, t = f - floorf(f);
    int i = (int)f % ROOM_RAYS;
    return o->reach[i] * (1 - t) + o->reach[(i + 1) % ROOM_RAYS] * t;
}

/* Whether the run of rays from best, best_length long, opens on a space
   clearly bigger than the room; if so, b has it. */
static int beyond_run(const room_t *room, const room_outline_t *outline, int best, int best_length, float facing, beyond_t *b)
{
    /* The walls either side of it are about as far as the opening. */
    float before = outline->reach[(best + ROOM_RAYS - 1) % ROOM_RAYS];
    float after = outline->reach[(best + best_length) % ROOM_RAYS], opening = fminf(before, after);
    b->near = *outline;
    for (int k = 0; k < best_length; ++k) b->near.reach[(best + k) % ROOM_RAYS] = opening;

    int centre = (best + best_length / 2) % ROOM_RAYS;
    float angle = centre * 2 * PI_F / ROOM_RAYS, along = opening + 0.5f * (outline->reach[centre] - opening);
    Room_MeasureAt(outline->x + cosf(angle) * along, outline->y + sinf(angle) * along, facing, &b->room, &b->outline);
    if (b->outline.volume < BIGGER * outline->volume || b->room.decay < LONGER * room->decay) return 0;

    float open = fminf(best_length / (float)ROOM_RAYS / OPEN_SHARE, 1);
    b->coupling = sqrtf(open) / (1 + fmaxf(opening - 64, 0) / FALLOFF);
    /* All of it comes in by the opening: its echoes from there, a little
       spread, and later by the way there. */
    float pan = -sinf(angle - facing), delay = along / SPEED;
    for (int k = 0; k < ROOM_TAPS; ++k)
    {
        b->room.pan[k] = fmaxf(-1, fminf(1, pan + 0.25f * b->room.pan[k]));
        b->room.delay[k] += delay;
    }
    return 1;
}

void Beyond_Measure(const room_t *room, const room_outline_t *outline, float facing, beyond_t *b)
{
    memset(b, 0, sizeof(*b));
    b->room.decay = 0.2f;
    b->near = *outline;
    if (outline->volume <= 0) return;
    float middle = middle_reach(outline), edge = fmaxf(ESCAPE_TIMES * middle, middle + ESCAPE_MORE);
    int escaping[ROOM_RAYS], start = -1;
    for (int i = 0; i < ROOM_RAYS; ++i)
    {
        escaping[i] = outline->reach[i] > edge;
        if (!escaping[i] && start < 0) start = i;
    }
    /* Out in the open every way: nothing beyond to hear through. */
    if (start < 0) return;

    /* The runs of rays out of the room together, and how far past it each
       reaches all told; a long corridor's own ends are runs too. */
    int runs = 0, run_start[ROOM_RAYS], run_length[ROOM_RAYS], length = 0;
    float run_weight[ROOM_RAYS], weight = 0;
    for (int k = 1; k <= ROOM_RAYS; ++k)
    {
        int i = (start + k) % ROOM_RAYS;
        if (escaping[i]) { if (!length) run_start[runs] = i; ++length; weight += outline->reach[i] - middle; continue; }
        if (length) { run_length[runs] = length; run_weight[runs++] = weight; }
        length = 0; weight = 0;
    }
    /* The farthest-reaching first; the first of them that opens on a
       bigger space is the way out. */
    for (int tries = 0; tries < TRIES && runs > 0; ++tries)
    {
        int pick = 0;
        for (int r = 1; r < runs; ++r) if (run_weight[r] > run_weight[pick]) pick = r;
        if (beyond_run(room, outline, run_start[pick], run_length[pick], facing, b)) return;
        run_weight[pick] = run_weight[--runs]; run_start[pick] = run_start[runs]; run_length[pick] = run_length[runs];
    }
    b->coupling = 0;
    b->near = *outline;
}

float Beyond_Share(const beyond_t *b, float x, float y)
{
    if (b->coupling <= 0) return 0;
    float dx = x - b->near.x, dy = y - b->near.y;
    if (hypotf(dx, dy) < reach_toward(&b->near, dx, dy)) return 0;
    dx = x - b->outline.x; dy = y - b->outline.y;
    return hypotf(dx, dy) < reach_toward(&b->outline, dx, dy) ? 1 : 0;
}
