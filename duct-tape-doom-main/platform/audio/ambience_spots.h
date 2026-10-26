/* What ambience_map.c reads from a level and ambience.c plays: the spots,
 * sources of one kind close together, with what each currently sounds like
 * from where the listener stands. Private to the two. */
#ifndef DOOM_AUDIO_AMBIENCE_SPOTS_H
#define DOOM_AUDIO_AMBIENCE_SPOTS_H
#include "ambience.h"

typedef struct {
    float x, y, z, size;     /* where, and how much of the kind (square units) */
    float biggest, light;    /* the largest source's size; a lamp's light last tic */
    int cx, cy, cz, sector;  /* its cell; the sector of its largest source */
    int standing;            /* a standing lamp, which buzzes in a steady light too */
    int portal;              /* an opening to the sky the wind comes in by */
    int line;                /* the map line a window's rain taps on, else -1 */
    /* How it reaches the listener, eased so nothing jumps: loudness 0-1 and
       lowpass, and where they head; the tic it was last in reach. */
    float muffle, cutoff, muffle_to, cutoff_to;
    int heard;
    /* Where the listener stood at its last measurement, and when: it is only
       measured again once they have moved, or now and then for doors. */
    float measured_x, measured_y;
    int measured;
} spot_t;

typedef struct { spot_t *spot; int count, capacity; } spots_t;

/* Per kind: the size that reaches a full voice, how near is full and how
   far it carries, in map units. */
typedef struct { float full, near, range; } reach_t;

extern spots_t ambience_spots[AMBIENT_KINDS];
extern const reach_t ambience_reach[AMBIENT_KINDS];
extern unsigned ambience_built;

/* Reads the level's spots; once per level, when it is first heard. */
void Ambience_Build(void);
/* How lit a lamp spot's sector is in its swing, 0 dark to 1 bright, and
   whether it swings at all; 1 when steady. */
float Ambience_LampLight(const spot_t *s, int *living);
#endif
