/* Now and then a sound from inside the walls around the listener
 * (hidden_synth.h): a kind the wall's face says lives behind it
 * (Game_HiddenWall), from a one-sided wall in reach, muffled as through it.
 * Rare and spread out, never the same kind twice running, and quiet while
 * the player fights: the base starts working again a while after it is
 * calm. Nothing here touches the game's random numbers, so demos and
 * saves are as they were. */
#ifndef DOOM_AUDIO_HIDDEN_H
#define DOOM_AUDIO_HIDDEN_H

typedef struct {
    int kind;         /* a HIDDEN_ kind */
    unsigned take;    /* picks the variation */
    float x, y, z;    /* on the wall's face, on the listener's side */
    float level;      /* a share of the effects volume */
    float reach;      /* how much farther than DOOM's sounds it carries */
    float cutoff;     /* the most treble that gets through the wall, Hz */
} hidden_event_t;

/* Every tic: 1 and the sound to play when one is due, else 0. */
int Hidden_Tic(hidden_event_t *event);
#endif
