/* Sounds from inside the walls of a tech base, heard and never seen: water
 * running through a pipe, a valve letting off steam, air
 * swelling in a duct, the structure settling, heavy machinery working far
 * below, condensation dripping. Each is rendered once like the impact
 * effects (mono at a rate, then held as 8-bit) and meant to play rarely,
 * muffled through the wall it lives behind, so the base around the player
 * seems to go on and to be working. A seed picks the variation. */
#ifndef DOOM_AUDIO_HIDDEN_SYNTH_H
#define DOOM_AUDIO_HIDDEN_SYNTH_H

enum {
    HIDDEN_PIPE_RUSH,   /* water running through a pipe for a few seconds, a trickle after */
    HIDDEN_STEAM,       /* a valve letting off a short hiss */
    HIDDEN_DUCT,        /* air swelling in a duct and settling, a far fan beating in it */
    HIDDEN_SETTLE,      /* the structure settling: a creak, or a tick or two */
    HIDDEN_PRESS,       /* heavy machinery far below: a stamp, the ram's sigh back */
    HIDDEN_DRIP,        /* condensation dripping into a puddle, once or twice */
    HIDDEN_KINDS
};

/* Short name for files. */
const char *Hidden_Name(int kind);
/* Frames the kind needs at the rate. */
int Hidden_Length(int kind, int rate);
/* Adds the kind into out (mono, peaking near 0.8). */
void Hidden_Render(int kind, unsigned seed, int rate, float *out, int frames);
#endif
