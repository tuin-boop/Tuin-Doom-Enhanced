/* A shot primes the acoustic route through a pane, then breaks it in the
   same tic. The break must be heard through the newly opened window. */
#include <stdio.h>
#include "p_local.h"
#include "doomstat.h"
#include "r_state.h"
#include "../platform/audio/acoustic_graph.h"
#include "../platform/audio/occlusion.h"
#include "../platform/audio/map.h"

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "Check failed at line %d: %s\n", __LINE__, #condition); \
    return 1; } } while (0)

int numlines = 1, numsectors = 2, gametic = 100, consoleplayer, skyflatnum = -1;
unsigned r_levelserial = 1;
gamestate_t gamestate = GS_LEVEL;
player_t players[MAXPLAYERS];
static sector_t rooms[2];
static line_t opening;
sector_t *sectors = rooms;
line_t *lines = &opening;
static subsector_t sub[2];
static vertex_t ends[2];
static mobj_t listener;

subsector_t *R_PointInSubsector(fixed_t x, fixed_t y)
{
    (void)y;
    return &sub[x >= 64 * FRACUNIT];
}

/* These belong to occlusion's same-sector branch; every test sound is in
   the other room and uses the real acoustic graph instead. */
void Map_Gather(float x, float y) { (void)x; (void)y; }
int Map_Cross(float x, float y, float cx, float cy, float length, crossing_t *out, int max)
{
    (void)x; (void)y; (void)cx; (void)cy; (void)length; (void)out; (void)max;
    return 0;
}

static float heard_cutoff(void)
{
    return Occlusion_Cutoff(128 * FRACUNIT, 0, 32 * FRACUNIT, 1, 0);
}

int main(void)
{
    for (int s = 0; s < 2; ++s)
    {
        rooms[s].ceilingheight = 128 * FRACUNIT;
        sub[s].sector = &rooms[s];
    }
    ends[0].x = ends[1].x = 64 * FRACUNIT;
    ends[0].y = -64 * FRACUNIT; ends[1].y = 64 * FRACUNIT;
    opening.v1 = &ends[0]; opening.v2 = &ends[1];
    opening.frontsector = &rooms[0]; opening.backsector = &rooms[1];
    opening.flags = ML_TWOSIDED;
    listener.subsector = &sub[0]; players[0].mo = &listener;

    Acoustic_Glass(0, 1);
    CHECK(heard_cutoff() == 500); /* Enemy gunshot through intact glass. */
    Acoustic_Glass(0, 0);
    CHECK(heard_cutoff() == OCCLUSION_OPEN_HZ); /* The break, same tic. */
    gametic += 8;
    CHECK(heard_cutoff() == OCCLUSION_OPEN_HZ);

    /* Restoring a pane or toggling window glass also takes effect at once. */
    Acoustic_Glass(0, 1);
    CHECK(heard_cutoff() == 500);
    Acoustic_ShowGlass(0);
    CHECK(heard_cutoff() == OCCLUSION_OPEN_HZ);
    Acoustic_ShowGlass(1);
    CHECK(heard_cutoff() == 500);
    Acoustic_Glass(0, 0);
    CHECK(heard_cutoff() == OCCLUSION_OPEN_HZ);
    puts("Window breaks and glass settings update acoustic routes immediately.");
    return 0;
}
