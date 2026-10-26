/* The steady sounds of what the map's textures show: lamps buzzing, screens
 * and computers humming, nukage bubbling, lava and fire, water, wind, rain
 * on the windows. Made
 * up from nothing like the impact effects, for the WAD has none. Each kind
 * renders any length; a seed picks the variation, so two pools never
 * bubble in step. */
#ifndef DOOM_AUDIO_AMBIENT_SYNTH_H
#define DOOM_AUDIO_AMBIENT_SYNTH_H

enum {
    AMBIENT_LAMP,        /* LITE, TLITE, ceiling lights: a ballast's buzz */
    AMBIENT_CRT,         /* screens: mains hum, static, a whine where the rate holds it */
    AMBIENT_COMPUTER,    /* COMP walls: bleeps in little tunes */
    AMBIENT_TELEPORTER,  /* GATE flats: a low throb and a shimmer */
    AMBIENT_NUKAGE,      /* thick green bubbling, now and then a big blorp */
    AMBIENT_SLIMEFALL,   /* SFALL and the like: liquid pouring */
    AMBIENT_BLOOD,       /* slower and deeper than nukage */
    AMBIENT_WATER,       /* lapping, a drip now and then */
    AMBIENT_LAVA,        /* a low rumble, heavy bubbles bursting, a sizzle */
    AMBIENT_FIRE,        /* fire walls: a roar and crackling */
    AMBIENT_MAGMA,       /* cracked hot rock: faint ticks over a rumble */
    AMBIENT_WIND,        /* under the sky: gusts, a whistle */
    AMBIENT_RAIN,        /* under the sky while it rains: a hiss of drops, a patter, drips */
    AMBIENT_GLASS_RAIN,  /* window panes while it rains: drops tapping the glass, a trickle */
    AMBIENT_ROOM,        /* the room tone: faint dark air, when nothing else is heard */
    AMBIENT_KINDS
};

const char *Ambient_Name(int kind);
/* How loud the kind plays next to the others, 1 for most. */
float Ambient_Level(int kind);
/* Adds frames of the kind into out (mono, about -1..1). light is the
   sector's light per tic (35 a second), 0 its darkest and 1 its brightest,
   or NULL for steady; a lamp buzzes with it and cuts out when it drops. */
void Ambient_Render(int kind, unsigned seed, int rate, float *out, int frames,
                    const float *light, int tics);

/* The pieces, in machine_synth.c, liquid_synth.c and fire_synth.c. */
void Machine_Render(int kind, unsigned seed, int rate, float *out, int frames,
                    const float *light, int tics);
void Liquid_Render(int kind, unsigned seed, int rate, float *out, int frames);
void Fire_Render(int kind, unsigned seed, int rate, float *out, int frames);
#endif
