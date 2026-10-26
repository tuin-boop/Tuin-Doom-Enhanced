/* Where the ambient sounds come from, and how each reaches the listener. At
 * level start the map's textures are read for sound sources: lamps, screens
 * and computers, liquids, fire walls, lava and magma, teleporter pads, and
 * the open air under the sky, where the wind always blows; indoors it is
 * heard through the openings, dull through window glass until the glass
 * breaks. Sources of a kind close together join into one spot. Every tic
 * each kind gets up to AMBIENCE_LAYERS voices fed by all its spots in reach,
 * loud by their size and distance and muffled like any sound behind walls;
 * a faint room tone runs under it all. */
#ifndef DOOM_AUDIO_AMBIENCE_H
#define DOOM_AUDIO_AMBIENCE_H
#include "ambient_synth.h"

/* Voices per kind: its spots are shared among them, each playing its own take. */
#define AMBIENCE_LAYERS 3

typedef struct {
    float left, right, send;  /* gains, before the mixer's own effect gain */
    float cutoff;             /* lowpass in Hz, as occlusion.h */
} ambience_voice_t;

typedef struct { int due; float x, y, z, level; } ambience_strike_t;

/* The voices for this tic, for a listener at the console player; all
   silent outside a level. level scales them all (the F4 amount times the
   effects volume). strike: a lamp coming back on in a flickering sector.
   computer_body, when non-NULL, receives the combined computer field before
   the alternative mix divides its budget between body and activity. */
void Ambience_Update(ambience_voice_t voices[AMBIENT_KINDS][AMBIENCE_LAYERS], float level, int walls, int distance,
                     ambience_strike_t *strike, ambience_voice_t *computer_body);
/* Glowing fixtures shot out around (x, y, z): their hum, buzz or chatter
   stops for the rest of the level. */
void Ambience_Silence(float x, float y, float z, float radius);
/* A window pane on a map line, whole or broken: through a whole one every
   ambient sound comes quieter and dull. Again when it breaks. */
void Ambience_Glaze(int line, int whole);
/* Whether window glass is shown at all; without it the wind always blows. */
void Ambience_ShowGlass(int shown);
/* How hard it rains, 0-1: the rain is heard under the sky and through the
   openings to it, as the wind is, whether or not the game has wind, and
   tapping on the whole panes of windows looking out under it. */
void Ambience_Rain(float amount);
#endif
