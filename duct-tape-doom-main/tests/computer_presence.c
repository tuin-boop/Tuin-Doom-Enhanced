/* Exercise the real ambience aggregation with a data-free map fixture.
   One computer body must follow the audible computer field and share its
   existing gain cap, even in a room containing many separate spots. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "p_local.h"
#include "doomstat.h"
#include "r_state.h"
#include "../platform/game_profile.h"
#include "../platform/audio/acoustic_graph.h"
#include "../platform/audio/ambience_spots.h"
#include "../platform/audio/occlusion.h"

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "Check failed at line %d: %s\n", __LINE__, #condition); \
    return 1; } } while (0)

int numsectors = 2, gametic = 100, consoleplayer, skyflatnum = -1;
unsigned r_levelserial = 1;
gamestate_t gamestate = GS_LEVEL;
player_t players[MAXPLAYERS];
static sector_t rooms[2];
sector_t *sectors = rooms;
static subsector_t listener_subsector;
static mobj_t listener;

spots_t ambience_spots[AMBIENT_KINDS];
unsigned ambience_built = 1;
/* The production reach and nominal source sizes for the fixture's kinds. */
const reach_t ambience_reach[AMBIENT_KINDS] = {
    [AMBIENT_COMPUTER] = { 16384, 64, 640 },
    [AMBIENT_CRT] = { 8192, 48, 448 },
};
static spot_t computers[12], crt;
static float ray_cutoff = OCCLUSION_OPEN_HZ;
static acoustic_heard_t route;
static int route_exists, unexpected_build;

/* No WAD parsing or synthesis is needed: only the spots, game state and
   acoustic results consumed by the real Ambience_Update are fixtures. */
void Ambience_Build(void) { ++unexpected_build; ambience_built = r_levelserial; }
float Ambience_LampLight(const spot_t *spot, int *living)
{
    (void)spot; *living = 0; return 1;
}
float Ambient_Level(int kind)
{
    return kind == AMBIENT_COMPUTER ? 0.14f : kind == AMBIENT_ROOM ? 0.25f : 1;
}
int Game_HasEffect(game_effect effect) { (void)effect; return 0; }
void Acoustic_Follow(void) {}
int Acoustic_Heard(float x, float y, int sector,
                   float (*fade)(float, const void *), const void *data,
                   acoustic_heard_t *out)
{
    (void)x; (void)y; (void)sector; (void)fade; (void)data;
    *out = route;
    return route_exists;
}
void Acoustic_Glass(int line, int whole) { (void)line; (void)whole; }
void Acoustic_ShowGlass(int shown) { (void)shown; }
int Acoustic_GlassWhole(int line) { (void)line; return 1; }
float Occlusion_Cutoff(fixed_t x, fixed_t y, fixed_t z, int walls, int distance)
{
    (void)x; (void)y; (void)z; (void)walls; (void)distance;
    return ray_cutoff;
}

static int near(float a, float b)
{
    return fabsf(a - b) <= 1e-5f * fmaxf(1, fmaxf(fabsf(a), fabsf(b)));
}
static int silent(ambience_voice_t voice)
{
    return voice.left == 0 && voice.right == 0 && voice.send == 0;
}
/* The send exposes the voice's gain before its left/right pan law. */
static float gain(ambience_voice_t voice) { return voice.send / 0.8f; }
static float computer_energy(ambience_voice_t voices[AMBIENT_KINDS][AMBIENCE_LAYERS])
{
    float energy = 0;
    for (int i = 0; i < AMBIENCE_LAYERS; ++i)
    {
        float g = gain(voices[AMBIENT_COMPUTER][i]);
        energy += g * g;
    }
    return energy;
}

static void fixture(int count)
{
    memset(ambience_spots, 0, sizeof(ambience_spots));
    memset(rooms, 0, sizeof(rooms));
    memset(&listener, 0, sizeof(listener));
    listener_subsector.sector = &rooms[0];
    listener.subsector = &listener_subsector;
    players[0].mo = &listener;
    gamestate = GS_LEVEL;
    numsectors = 2;
    gametic += 10;
    ambience_built = r_levelserial;
    route_exists = 0;
    ray_cutoff = OCCLUSION_OPEN_HZ;
    Ambience_Rain(0);
    /* Near, nominal-size sources: each contributes one full computer
       before the kind-wide cap. At this bearing the field pans left. */
    for (int i = 0; i < count; ++i)
        computers[i] = (spot_t){ .y = 64, .z = 41, .size = 16384,
            .line = -1, .heard = -1000, .measured = -1000,
            .muffle = 1, .muffle_to = 1,
            .cutoff = OCCLUSION_OPEN_HZ, .cutoff_to = OCCLUSION_OPEN_HZ };
    ambience_spots[AMBIENT_COMPUTER] = (spots_t){ computers, count, 12 };
}

int main(void)
{
    ambience_voice_t voices[AMBIENT_KINDS][AMBIENCE_LAYERS], body;
    ambience_strike_t strike;
    const float level = 0.7f, nominal = 0.14f * level;

    fixture(0);
    Ambience_Update(voices, level, 0, 0, &strike, &body);
    CHECK(silent(body));
    CHECK(near(voices[AMBIENT_ROOM][0].left, 0.25f * level));
    float room_gain = gain(voices[AMBIENT_ROOM][0]);
    /* Other nearby machines and the independent room tone cannot cause
       computer presence without an audible computer. */
    crt = (spot_t){ .z = 41, .size = 8192, .line = -1, .heard = -1000 };
    ambience_spots[AMBIENT_CRT] = (spots_t){ &crt, 1, 1 };
    Ambience_Update(voices, level, 0, 0, &strike, &body);
    CHECK(silent(body));
    CHECK(gain(voices[AMBIENT_CRT][0]) > 0);
    CHECK(near(gain(voices[AMBIENT_ROOM][0]), room_gain));

    fixture(1);
    Ambience_Update(voices, level, 0, 0, &strike, &body);
    CHECK(near(gain(body), nominal));
    CHECK(near(gain(body) * gain(body), computer_energy(voices)));
    CHECK(near(body.left, nominal));
    CHECK(near(body.right, nominal * 32.0f / 127));
    CHECK(near(body.cutoff, OCCLUSION_OPEN_HZ));
    CHECK(near(gain(voices[AMBIENT_ROOM][0]), room_gain));

    fixture(12);
    Ambience_Update(voices, level, 0, 0, &strike, &body);
    CHECK(gain(body) <= 1.5f * nominal + 1e-6f);
    CHECK(near(gain(body), 1.5f * nominal));
    CHECK(near(gain(body) * gain(body), computer_energy(voices)));
    for (int i = 0; i < AMBIENCE_LAYERS; ++i)
        CHECK(gain(voices[AMBIENT_COMPUTER][i]) > 0);
    CHECK(near(gain(voices[AMBIENT_ROOM][0]), room_gain));

    fixture(1);
    ray_cutoff = 500;
    Ambience_Update(voices, level, 1, 0, &strike, &body);
    CHECK(near(gain(body), nominal * 0.25f));
    CHECK(near(body.cutoff, 500));
    CHECK(near(body.left, voices[AMBIENT_COMPUTER][0].left));
    CHECK(near(body.right, voices[AMBIENT_COMPUTER][0].right));

    /* An indirect sound arrives from the opening, so the body must turn
       with that audible field and inherit its loss and lowpass. */
    fixture(1);
    computers[0].sector = 1;
    route_exists = 1;
    route = (acoustic_heard_t){ .gain = 0.5f, .cutoff = 1200,
        .from_y = -64, .distance = 128 };
    Ambience_Update(voices, level, 1, 0, &strike, &body);
    CHECK(near(gain(body), nominal * 0.5f));
    CHECK(body.right > body.left);
    CHECK(near(body.cutoff, 1200));
    CHECK(near(body.left, voices[AMBIENT_COMPUTER][0].left));
    CHECK(near(body.right, voices[AMBIENT_COMPUTER][0].right));
    CHECK(near(gain(body) * gain(body), computer_energy(voices)));

    fixture(1);
    computers[0].y = ambience_reach[AMBIENT_COMPUTER].range;
    Ambience_Update(voices, level, 0, 0, &strike, &body);
    CHECK(silent(body));
    CHECK(near(gain(voices[AMBIENT_ROOM][0]), room_gain));

    fixture(12);
    Ambience_Update(voices, level, 0, 0, &strike, &body);
    CHECK(!silent(body));
    gamestate = GS_INTERMISSION;
    Ambience_Update(voices, level, 0, 0, &strike, &body);
    CHECK(silent(body));
    CHECK(silent(voices[AMBIENT_ROOM][0]));
    gamestate = GS_LEVEL;
    Ambience_Update(voices, 0, 0, 0, &strike, &body);
    CHECK(silent(body));
    CHECK(silent(voices[AMBIENT_ROOM][0]));
    Ambience_Update(voices, level, 0, 0, &strike, &body);
    CHECK(!silent(body));
    ambience_spots[AMBIENT_COMPUTER].count = 0;
    Ambience_Update(voices, level, 0, 0, &strike, &body);
    CHECK(silent(body));
    CHECK(near(gain(voices[AMBIENT_ROOM][0]), room_gain));

    fixture(12);
    Ambience_Update(voices, level, 0, 0, &strike, &body);
    CHECK(!silent(body));
    Ambience_Silence(0, 64, 41, 16);
    Ambience_Update(voices, level, 0, 0, &strike, &body);
    CHECK(silent(body));
    CHECK(near(computer_energy(voices), 0));
    CHECK(near(gain(voices[AMBIENT_ROOM][0]), room_gain));
    CHECK(unexpected_build == 0);
    puts("Computer presence follows audible sources, shared density cap, routes and silence.");
    return 0;
}
