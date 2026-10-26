/* The room's echoes and tail, run in the mixer with the audio lock held.
 * The mixer runs two: the listener's own room, and the bigger space next
 * door heard through the way into it (beyond.h). */
#ifndef DOOM_AUDIO_REVERB_H
#define DOOM_AUDIO_REVERB_H
#include "room.h"

typedef struct reverb_s reverb_t;

/* A reverb at an output rate, ringing nothing; NULL without memory. */
reverb_t *Reverb_New(int rate);
void Reverb_Free(reverb_t *reverb);
/* Forgets everything still ringing. */
void Reverb_Clear(reverb_t *reverb);
/* The room to move toward; echoes crossfade and the tail glides there. */
void Reverb_SetRoom(reverb_t *reverb, const room_t *room);
/* Adds the reverb of a mono send to interleaved stereo, scaled by amount. */
void Reverb_Process(reverb_t *reverb, const float *send, float *stereo, int frames, float amount);
#endif
