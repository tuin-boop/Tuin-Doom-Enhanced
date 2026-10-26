/* The bigger space next door. From a corridor or a small room, some of the
 * rays room.c casts run out through an opening into a hall, much farther
 * than the rest. That hall is heard through the opening: the listener's own
 * sounds reach into it and ring there faintly, and sounds made inside it
 * ring as the hall does, not as the listener's room. beyond.c finds the
 * space from the listener's floor plan and measures it from inside; the
 * mixer plays it through a second reverb, all of it from the opening. */
#ifndef DOOM_AUDIO_BEYOND_H
#define DOOM_AUDIO_BEYOND_H
#include "room.h"

typedef struct {
    room_t room;             /* the space as heard: its echoes and tail, from the opening */
    float coupling;          /* 0 nothing bigger beyond .. 1 a wide opening close by */
    room_outline_t outline;  /* its floor plan, measured from inside it */
    room_outline_t near;     /* the listener's room's, ending at the opening */
} beyond_t;

/* Finds the space beyond the listener's room, from the room and floor plan
   Room_Measure gave, for a listener facing (radians, a map angle). */
void Beyond_Measure(const room_t *room, const room_outline_t *outline, float facing, beyond_t *beyond);
/* How much a sound at (x, y) is in the space beyond rather than in the
   listener's room: 0 here or anywhere else, 1 in there. */
float Beyond_Share(const beyond_t *beyond, float x, float y);
#endif
