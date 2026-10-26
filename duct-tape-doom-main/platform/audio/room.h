/* Room acoustics: what the reverb hears of the room around the listener,
 * measured from the map by room.c and played by reverb.c. */
#ifndef DOOM_AUDIO_ROOM_H
#define DOOM_AUDIO_ROOM_H
#include <math.h>

/* glibc hides it from the POSIX-only build. */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Eight wall echoes around the listener, then one off the ceiling. */
#define ROOM_TAPS 9

typedef struct {
    float delay[ROOM_TAPS];  /* round trip to the wall, seconds */
    float gain[ROOM_TAPS];   /* echo level: distance and wall material */
    float spread[ROOM_TAPS]; /* 0 a clean echo off smooth walls .. 1 smeared by rough ones */
    float dark[ROOM_TAPS];   /* how much treble the echo's walls take: 0 metal .. 1 fabric */
    float pan[ROOM_TAPS];    /* -1 left .. 1 right, as the listener faces */
    float decay;             /* seconds for the late tail to fall 60 dB */
    float damping;           /* 0 bright (metal) .. 1 dark (wood, flesh) */
    float scatter;           /* 0 smooth surfaces, a ringing tail .. 1 rough (stone, dirt), a flat wash */
    float late;              /* late tail level */
} room_t;

/* The rays a measurement casts round its point to find the walls. */
#define ROOM_RAYS 32
/* The floor plan a measurement found: from (x, y), how far the walls are
   along each ray (ray i heads i * 2 pi / ROOM_RAYS radians, as map angles
   turn), and the room's volume in cubic metres. */
typedef struct { float x, y, volume, reach[ROOM_RAYS]; } room_outline_t;

/* Measures the room around the console player; a dry room outside levels.
   outline (or NULL) gets its floor plan. */
void Room_Measure(room_t *room, room_outline_t *outline);
/* The same for an ear above the floor at (x, y), facing (radians, a map angle). */
void Room_MeasureAt(float x, float y, float facing, room_t *room, room_outline_t *outline);
#endif
