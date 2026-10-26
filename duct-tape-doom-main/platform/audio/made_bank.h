/* The made-up sounds as the mixer plays them: every impact kind in a few
 * seeded variations (steps in more), the sounds behind the walls likewise,
 * and every ambient kind as a few seamless loops, all made
 * once at startup as 8-bit samples like DOOM's own. Glass uses 22 kHz for
 * its sharp collisions; other effects and the ambience stay at 11 kHz. */
#ifndef DOOM_AUDIO_MADE_BANK_H
#define DOOM_AUDIO_MADE_BANK_H

#define MADE_RATE 11025
#define MADE_VARIANTS 6
#define MADE_STEP_VARIANTS 16  /* steps and kit play all the time; more takes, fewer repeats */
#define MADE_LOOP_SECONDS 8   /* the wind's are longer */

typedef struct { const unsigned char *data; int length, rate; } made_sample_t;

/* Renders everything; once, before the mixer runs. */
void MadeBank_Build(void);
/* How many variations an impact kind has. */
int MadeBank_Variants(int kind);
/* A variation of an impact kind (audio/impact_synth.h); NULL before the build. */
const made_sample_t *MadeBank_Impact(int kind, int variant);
/* A take of an ambient kind's loop (audio/ambient_synth.h); each layer of
   the kind (audio/ambience.h) plays its own. */
const made_sample_t *MadeBank_Loop(int kind, int layer);
/* Separately built optional computer palette. The 32 s periodic body plays
   once; the three spatial activity takes last 32, 37 and 43 s. Original loops
   above retain their existing samples and durations. NULL before the build. */
const made_sample_t *MadeBank_ComputerBody(void);
const made_sample_t *MadeBank_ComputerActivity(int layer);
/* A variation of a sound behind the walls (audio/hidden_synth.h); NULL before the build. */
const made_sample_t *MadeBank_Hidden(int kind, int variant);
/* A tube striking again as its light comes back: a tick and a crackle. */
const made_sample_t *MadeBank_Strike(int variant);
#endif
