/* The openings of a level and one search over them from the listener. See
 * acoustic_graph.h. An opening's crossing is read live at every search, so
 * doors, lifts and breaking glass change what gets through as they happen. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "doomstat.h"
#include "p_local.h"
#include "acoustic_graph.h"
#include "map.h"

#define UNITS_PER_DB 16.0f  /* how far a decibel of loss weighs when choosing the way */
#define MOST_LOSS 30.0f     /* searched this far past reach in loss, then given up */
#define OPEN_HZ 20000.0f
#define FOLLOW_MOVED 16.0f  /* the search follows the listener once they move this far, */
#define FOLLOW_TICS 8       /* or this many tics pass */
#define FOLLOW_REACH 1280.0f /* the farthest an ambient sound carries; effects reach 1200 */

/* An opening: a two-sided line between sectors a and b. */
typedef struct { int line, a, b; float ax, ay, bx, by; } opening_t;
static opening_t *graph_openings;
static int opening_count, *sector_first, *sector_openings;
static unsigned built = ~0u;

/* The last search, per opening: the way's length, its loss in dB, the
   lowpass left after it, where it crosses this opening (the next leg starts
   there, so no way is shorter than straight), where it leaves the listener's
   own sector, and what the search ranked it by. stamp marks those reached. */
typedef struct { float length, loss, cutoff, cost, cross_x, cross_y, entry_x, entry_y, loss_before, cutoff_before; unsigned stamp; } way_t;
static way_t *ways;
static unsigned search;

/* Window panes per line: 0 none, 1 whole, 2 broken. */
static unsigned char *glass;
static int glass_lines;
static unsigned glass_serial = ~0u;
static int glass_shown = 1;
static int follow_dirty = 1;

static float units(fixed_t v) { return v / (float)FRACUNIT; }

/* The nearest point of an opening to (x, y), and how far it is. */
static float nearest(const opening_t *o, float x, float y, float *px, float *py)
{
    float ex = o->bx - o->ax, ey = o->by - o->ay, len2 = ex * ex + ey * ey;
    float t = len2 > 0 ? ((x - o->ax) * ex + (y - o->ay) * ey) / len2 : 0;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    *px = o->ax + ex * t; *py = o->ay + ey * t;
    return hypotf(x - *px, y - *py);
}

static void build(void)
{
    free(graph_openings); free(sector_first); free(sector_openings); free(ways);
    opening_count = 0;
    graph_openings = malloc((size_t)(numlines > 0 ? numlines : 1) * sizeof(opening_t));
    sector_first = calloc((size_t)numsectors + 1, sizeof(int));
    if (!graph_openings || !sector_first) { built = ~0u; return; }
    for (int i = 0; i < numlines; ++i)
    {
        const line_t *line = &lines[i];
        if (!line->frontsector || !line->backsector || line->frontsector == line->backsector) continue;
        opening_t *o = &graph_openings[opening_count++];
        o->line = i; o->a = (int)(line->frontsector - sectors); o->b = (int)(line->backsector - sectors);
        o->ax = units(line->v1->x); o->ay = units(line->v1->y); o->bx = units(line->v2->x); o->by = units(line->v2->y);
        ++sector_first[o->a + 1]; ++sector_first[o->b + 1];
    }
    /* Each sector's openings, packed one sector after another. */
    for (int s = 0; s < numsectors; ++s) sector_first[s + 1] += sector_first[s];
    sector_openings = malloc((size_t)(2 * opening_count + 1) * sizeof(int));
    int *fill = calloc((size_t)numsectors, sizeof(int));
    ways = calloc((size_t)opening_count + 1, sizeof(way_t));
    if (!sector_openings || !fill || !ways) { free(fill); built = ~0u; return; }
    for (int i = 0; i < opening_count; ++i)
    {
        sector_openings[sector_first[graph_openings[i].a] + fill[graph_openings[i].a]++] = i;
        sector_openings[sector_first[graph_openings[i].b] + fill[graph_openings[i].b]++] = i;
    }
    free(fill);
    search = 0;
    built = r_levelserial;
}

/* What crossing an opening costs now: a step or a change of light costs
   nothing, a doorway a little, a window or narrow gap more and some treble,
   a shut door or lift most, and glass in it a lot and nearly all treble. */
static float crossing(const opening_t *o, float *cutoff)
{
    const sector_t *a = &sectors[o->a], *b = &sectors[o->b];
    *cutoff = OPEN_HZ;
    if (Map_Sky(a) && Map_Sky(b)) return 0;
    float bottom, top;
    Map_Gap(a, b, &bottom, &top);
    float h = top - bottom, width = hypotf(o->bx - o->ax, o->by - o->ay), db;
    float room = fminf(Map_Ceiling(a) - Map_Floor(a), Map_Ceiling(b) - Map_Floor(b));
    if (h < 1) { db = 24; *cutoff = 350; }
    else if (h < 24 || width < 16) { db = 8; *cutoff = 2000; }
    else if (h < 56 || width < 40) { db = 3; *cutoff = 5000; }
    else if (h >= 0.8f * room && width >= 64) db = 0;
    else db = 0.5f;
    if (glass_shown && glass && o->line < glass_lines && glass[o->line] == 1) { db += 18; *cutoff = fminf(*cutoff, 500); }
    return db;
}

/* The search's queue: openings by cost, cheapest on top; stale entries are skipped. */
typedef struct { float cost; int opening; } entry_t;
static entry_t *heap;
static int heap_count, heap_capacity;

static void push(float cost, int opening)
{
    if (heap_count == heap_capacity)
    {
        int capacity = heap_capacity ? 2 * heap_capacity : 1024;
        entry_t *grown = realloc(heap, (size_t)capacity * sizeof(entry_t));
        if (!grown) return;
        heap = grown; heap_capacity = capacity;
    }
    int i = heap_count++;
    while (i > 0 && heap[(i - 1) / 2].cost > cost) { heap[i] = heap[(i - 1) / 2]; i = (i - 1) / 2; }
    heap[i] = (entry_t){ cost, opening };
}
static entry_t pop(void)
{
    entry_t top = heap[0], last = heap[--heap_count];
    int i = 0;
    for (;;)
    {
        int child = 2 * i + 1;
        if (child >= heap_count) break;
        if (child + 1 < heap_count && heap[child + 1].cost < heap[child].cost) ++child;
        if (heap[child].cost >= last.cost) break;
        heap[i] = heap[child]; i = child;
    }
    if (heap_count) heap[i] = last;
    return top;
}

/* loss_before and cutoff_before are the way's up to this opening, not
   counting crossing it: a sound inside a shut door's sector is heard from
   its graph_openings, not through the door it is. */
static void reach(int q, float length, float loss, float cutoff, float cross_x, float cross_y, float entry_x, float entry_y,
                  float loss_before, float cutoff_before)
{
    float cost = length + UNITS_PER_DB * loss;
    way_t *w = &ways[q];
    if (w->stamp == search && w->cost <= cost) return;
    *w = (way_t){ length, loss, cutoff, cost, cross_x, cross_y, entry_x, entry_y, loss_before, cutoff_before, search };
    push(cost, q);
}

void Acoustic_Listen(float x, float y, int sector, float range)
{
    if (built != r_levelserial) build();
    if (built != r_levelserial || sector < 0 || sector >= numsectors) return;
    if (++search == 0) { for (int i = 0; i < opening_count; ++i) ways[i].stamp = 0; search = 1; }
    heap_count = 0;
    for (int k = sector_first[sector]; k < sector_first[sector + 1]; ++k)
    {
        int p = sector_openings[k];
        float px, py, cutoff, d = nearest(&graph_openings[p], x, y, &px, &py), db = crossing(&graph_openings[p], &cutoff);
        reach(p, d, db, cutoff, px, py, px, py, 0, OPEN_HZ);
    }
    float limit = range + UNITS_PER_DB * MOST_LOSS;
    while (heap_count)
    {
        entry_t e = pop();
        const way_t *w = &ways[e.opening];
        if (w->stamp != search || e.cost > w->cost) continue;
        if (e.cost > limit) break;
        const opening_t *p = &graph_openings[e.opening];
        int sides[2] = { p->a, p->b };
        for (int s = 0; s < 2; ++s)
            for (int k = sector_first[sides[s]]; k < sector_first[sides[s] + 1]; ++k)
            {
                int q = sector_openings[k];
                if (q == e.opening) continue;
                float cutoff, db = crossing(&graph_openings[q], &cutoff), nx, ny;
                float hop = nearest(&graph_openings[q], w->cross_x, w->cross_y, &nx, &ny);
                reach(q, w->length + hop, w->loss + db, fminf(w->cutoff, cutoff), nx, ny, w->entry_x, w->entry_y, w->loss, w->cutoff);
            }
    }
}

#define MOST_WAYS 32

int Acoustic_Heard(float x, float y, int sector, float (*fade)(float distance, const void *data),
                   const void *data, acoustic_heard_t *out)
{
    if (built != r_levelserial || sector < 0 || sector >= numsectors) return 0;
    /* A way in is a neighboring sector: a doorway drawn as several lines, or a
       boundary of steps, is one way, the best of its lines. Different ways add
       up, so two windows into the courtyard - each its own frame sector - let
       in more wind than one. */
    struct { int from; float power, dull, far, fx, fy; } ways_in[MOST_WAYS];
    int count = 0;
    for (int k = sector_first[sector]; k < sector_first[sector + 1]; ++k)
    {
        int q = sector_openings[k];
        const way_t *w = &ways[q];
        if (w->stamp != search) continue;
        float d = w->length + hypotf(x - w->cross_x, y - w->cross_y);
        float g = fade(d, data) * powf(10, -w->loss / 20);
        if (g <= 0) continue;
        int from = graph_openings[q].a == sector ? graph_openings[q].b : graph_openings[q].a, i = 0;
        while (i < count && ways_in[i].from != from) ++i;
        if (i == count)
        {
            if (count == MOST_WAYS) continue;
            ways_in[count++].power = 0;
            ways_in[i].from = from;
        }
        if (g * g <= ways_in[i].power) continue;
        ways_in[i].power = g * g; ways_in[i].dull = logf(w->cutoff); ways_in[i].far = d;
        ways_in[i].fx = w->entry_x; ways_in[i].fy = w->entry_y;
    }
    float power = 0, dull = 0, fx = 0, fy = 0, far = 0;
    for (int i = 0; i < count; ++i)
    {
        float p = ways_in[i].power;
        power += p; dull += p * ways_in[i].dull; far += p * ways_in[i].far;
        fx += p * ways_in[i].fx; fy += p * ways_in[i].fy;
    }
    if (power <= 0) return 0;
    *out = (acoustic_heard_t){ sqrtf(power), expf(dull / power), fx / power, fy / power, far / power };
    return 1;
}

int Acoustic_Way(float x, float y, int sector, acoustic_way_t *out)
{
    if (built != r_levelserial || sector < 0 || sector >= numsectors) return 0;
    const sector_t *s = &sectors[sector];
    int shut = Map_Ceiling(s) - Map_Floor(s) < 1, found = 0;
    float best = 0;
    for (int k = sector_first[sector]; k < sector_first[sector + 1]; ++k)
    {
        const way_t *w = &ways[sector_openings[k]];
        if (w->stamp != search) continue;
        float length = w->length + hypotf(x - w->cross_x, y - w->cross_y);
        float loss = shut ? w->loss_before : w->loss, cost = length + UNITS_PER_DB * loss;
        if (found && cost >= best) continue;
        found = 1; best = cost;
        *out = (acoustic_way_t){ length, loss, shut ? w->cutoff_before : w->cutoff };
    }
    return found;
}

/* The search follows the console player: again once they have moved, changed
   sector or a few tics have passed, so doors and glass tell at once. */
void Acoustic_Follow(void)
{
    static float x, y;
    static int tic = -1000, at = -1;
    static unsigned level = ~0u;
    const mobj_t *listener = players[consoleplayer].mo;
    if (gamestate != GS_LEVEL || !listener || !listener->subsector) return;
    float lx = units(listener->x), ly = units(listener->y);
    int here = (int)(listener->subsector->sector - sectors);
    if (!follow_dirty && level == r_levelserial && at == here && fabsf(lx - x) + fabsf(ly - y) <= FOLLOW_MOVED &&
        gametic >= tic && gametic - tic < FOLLOW_TICS) return;
    Acoustic_Listen(lx, ly, here, FOLLOW_REACH);
    x = lx; y = ly; tic = gametic; at = here; level = r_levelserial;
    follow_dirty = 0;
}

void Acoustic_Glass(int line, int whole)
{
    if (glass_serial != r_levelserial || glass_lines != numlines)
    {
        free(glass);
        glass = calloc((size_t)(numlines > 0 ? numlines : 1), 1);
        glass_lines = glass ? numlines : 0;
        glass_serial = r_levelserial;
    }
    if (line >= 0 && line < glass_lines && glass[line] != (whole ? 1 : 2))
    {
        glass[line] = whole ? 1 : 2;
        /* The break sound starts now: it must not inherit the intact pane's
           muffling from the enemy's gunshot earlier in this same tic. */
        follow_dirty = 1;
    }
}
int Acoustic_GlassWhole(int line)
{
    return glass_shown && glass && glass_serial == r_levelserial && line >= 0 && line < glass_lines && glass[line] == 1;
}
void Acoustic_ShowGlass(int shown)
{
    if (glass_shown == (shown != 0)) return;
    glass_shown = shown != 0;
    follow_dirty = 1;
}
