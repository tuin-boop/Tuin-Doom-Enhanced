#ifndef DOOM_TOASTY_CHANCE_H
#define DOOM_TOASTY_CHANCE_H
#include <stdint.h>
/* Cosmetic randomness must never advance Doom's gameplay/demo RNG. */
typedef struct {
    uint32_t random;
    unsigned level, gibs, chosen;
    int initialized;
} doom_toasty_state;
static inline int doom_toasty_gib(doom_toasty_state *state, unsigned level) {
    if (!state->initialized || state->level != level) {
        uint32_t value;
        do {
            state->random=state->random*1664525u+1013904223u;
            value=state->random;
        } while(value==0); /* 2^32-1 is divisible by three. */
        state->level=level;
        state->gibs=0;
        state->chosen=1+value%3;
        state->initialized=1;
    }
    if(state->gibs>=3)return 0;
    return ++state->gibs==state->chosen;
}
#endif
