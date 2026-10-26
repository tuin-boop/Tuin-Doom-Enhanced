/* Names of the ambient kinds, which synth plays each, and how loud. */
#include <stdlib.h>
#include "ambient_synth.h"

static const char *const names[AMBIENT_KINDS] = {
    "lamp", "crt", "computer", "teleporter", "nukage", "slimefall", "blood", "water",
    "lava", "fire", "magma", "wind", "rain", "glass-rain", "room",
};
/* Brings each kind to about -18 to -24 dB, so all fill the 8 bits alike and
   none hisses; magma stays the faintest, and water's drops peak where the
   rest do over its silence. */
static const float gains[AMBIENT_KINDS] = {
    0.56f, 0.75f, 1.0f, 0.86f, 1.93f, 3.16f, 1.6f, 1.6f, 1.64f, 2.0f, 4.7f, 1.2f, 1.6f, 1.4f, 2.0f,
};

/* How loud each plays next to the rest. A steady lamp buzz everywhere grates,
   so lamps play far under the liquids; a computer's bleeps are made loud to
   fill the 8 bits and played back down, a little under where they were first
   heard. Standing water's drips play up, or they go unheard. Outside the
   wind is the bed the nukage bubbles in, not the other way round. Rain plays
   under the wind's level: a steady hiss everywhere tires the ear; on the
   windows it taps under it again, heard up close by the glass. */
static const float levels[AMBIENT_KINDS] = {
    0.35f, 1, 0.14f, 1, 0.8f, 1, 1, 1.8f, 1, 1, 1, 1.0f, 0.9f, 0.8f, 0.25f,
};

float Ambient_Level(int kind) { return kind >= 0 && kind < AMBIENT_KINDS ? levels[kind] : 0; }
const char *Ambient_Name(int kind) { return kind >= 0 && kind < AMBIENT_KINDS ? names[kind] : "?"; }

static void render(int kind, unsigned seed, int rate, float *out, int frames, const float *light, int tics)
{
    if (kind <= AMBIENT_TELEPORTER) Machine_Render(kind, seed, rate, out, frames, light, tics);
    else if (kind <= AMBIENT_WATER) Liquid_Render(kind, seed, rate, out, frames);
    else Fire_Render(kind, seed, rate, out, frames);
}

void Ambient_Render(int kind, unsigned seed, int rate, float *out, int frames,
                    const float *light, int tics)
{
    if (kind < 0 || kind >= AMBIENT_KINDS) return;
    float *own = calloc((size_t)frames, sizeof(float));
    if (!own) return;
    render(kind, seed, rate, own, frames, light, tics);
    for (int n = 0; n < frames; ++n) out[n] += gains[kind] * own[n];
    free(own);
}
