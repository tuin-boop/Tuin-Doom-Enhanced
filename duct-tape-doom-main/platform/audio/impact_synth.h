/* Effects the WAD doesn't have, made up from nothing: sparks off hit tech,
 * lamps and screens dying, glass breaking. Each is rendered once as a mono
 * sample at a rate and held as unsigned 8-bit, like DOOM's own effects, so
 * it plays through the mixer as one of them. A seed picks the variation, so
 * no two hits sound alike. The sound starts at the moment of the hit. */
#ifndef DOOM_AUDIO_IMPACT_SYNTH_H
#define DOOM_AUDIO_IMPACT_SYNTH_H

enum {
    IMPACT_SPARK_CRACKLE,  /* random cracks thinning out */
    IMPACT_SPARK_FIZZ,     /* cracks over a sizzling hiss */
    IMPACT_SPARK_ZAP,      /* a short arc buzz, then cracks */
    IMPACT_LAMP_DIES,      /* the hum sputters, a pop, a few bits of tube */
    IMPACT_SCREEN_DIES,    /* a burst of static, the tube thumps off */
    IMPACT_GLASS_TINKLE,   /* shards landing, no break */
    IMPACT_GLASS_SHATTER,  /* the break alone, no shards falling */
    IMPACT_GLASS_BREAK,    /* the shatter, then shards landing */
    IMPACT_GLASS_BIG,      /* a longer shatter, a high pane's worth of shards */
    IMPACT_GLASS_FRACTURE, /* a branching crack front, then its shards */
    IMPACT_GLASS_FRACTURE_BIG, /* a big pane's crack front, then its shards */
    IMPACT_STEP_STONE,     /* a footstep: heel and toe, a little grit */
    IMPACT_STEP_METAL,     /* with a faint ring of the plate */
    IMPACT_STEP_WOOD,      /* a hollow knock */
    IMPACT_STEP_SOFT,      /* muffled, on grass, dirt and carpet */
    IMPACT_SPLASH_WATER,   /* a step in water */
    IMPACT_SPLASH_THICK,   /* a step in nukage, slime, blood or lava */
    IMPACT_RUN_STONE,      /* a running footfall: ball of the foot first, harder */
    IMPACT_RUN_METAL,
    IMPACT_RUN_WOOD,
    IMPACT_RUN_SOFT,
    IMPACT_GEAR_LIGHT,     /* kit jolted by a step: a padded bump, buckles */
    IMPACT_GEAR_SLUNG,     /* a long gun knocking on its sling */
    IMPACT_GEAR_HEAVY,     /* a heavy weapon thudding against the kit, rounds rattling */
    IMPACT_GEAR_ARMOR,     /* armor: a hard click over a dull knock */
    IMPACT_GEAR_LIGHT_RUN, /* the same four swung by a running step: more bump and collisions */
    IMPACT_GEAR_SLUNG_RUN,
    IMPACT_GEAR_HEAVY_RUN,
    IMPACT_GEAR_ARMOR_RUN,
    IMPACT_FLYBY,          /* a bullet whizzing past the player's head */
    IMPACT_GORE_BURST,     /* a kill's wet burst, drops pattering down */
    IMPACT_GORE_SLAP,      /* a body slapping into a floor or wall */
    IMPACT_KINDS
};

/* Short name for files and settings. */
const char *Impact_Name(int kind);
/* Native bank rate: glass keeps its bright collision detail at 22 kHz;
   the other made-up effects retain DOOM's 11 kHz bandwidth. */
int Impact_Rate(int kind);
/* Frames the kind needs at the rate. */
int Impact_Length(int kind, int rate);
/* Adds the kind into out (mono, about -1..1). */
void Impact_Render(int kind, unsigned seed, int rate, float *out, int frames);
/* The steady hum of a lamp, or a screen's, that a dying one cuts off. */
void Impact_Hum(int screen, unsigned seed, int rate, float *out, int frames);
/* As DOOM holds an effect: unsigned 8-bit, 128 the middle. */
void Impact_To8Bit(const float *in, unsigned char *out, int frames);

/* The pieces, in spark_synth.c, glass_synth.c, fracture_synth.c and step_synth.c. */
void Flyby_Render(unsigned seed,int rate,float *out,int frames);
void Spark_Render(int kind, unsigned seed, int rate, float *out, int frames);
void Glass_Render(int kind, unsigned seed, int rate, float *out, int frames);
void Fracture_Render(int kind, unsigned seed, int rate, float *out, int frames);
void Step_Render(int kind, unsigned seed, int rate, float *out, int frames);
/* In flyby_synth.c. */
void Flyby_Render(unsigned seed, int rate, float *out, int frames);
/* In gore_synth.c. */
void Gore_Render(int kind, unsigned seed, int rate, float *out, int frames);
/* What one step plays: the footfall on the floor kind (IMPACT_STEP_ or
   IMPACT_SPLASH_) and the kit rattling after it, each a kind at a level.
   force: 0 walking, 1 running, up to 2 a hard landing. armor 0-2 and heft
   (Game_WeaponHeft) say what the kit is. Returns how many, at most 3. */
typedef struct { int kind; float level; } impact_layer_t;
/* How much the kit's metal rings, 1 as made; for doom-sfx-render's
   comparisons. Set before rendering. */
void Step_SetGearRing(float amount);
/* Alternative supported-metal contacts for offline auditioning. The game
   keeps CURRENT until a replacement has been chosen. Set before rendering. */
enum { STEP_METAL_CURRENT, STEP_METAL_FRICTION, STEP_METAL_TREAD };
void Step_SetMetalStyle(int style);
int Step_Layers(int floor, float force, int armor, int heft, impact_layer_t *layers);
/* Count tiny bits still settling from start on, thinning out over a second. */
void Glass_Debris(float *out, int frames, int rate, unsigned seed, float start, int count, float amp);
/* Count shards let go over spread seconds from sill..sill+pane units above
   the floor, each ticking where it lands and bouncing on; tonal 0 is only
   the scratch of each landing, 1 mostly its ring. */
void Glass_Shards(float *out, int frames, int rate, unsigned seed, int count,
                  float sill, float pane, float spread, float amp, float tonal);
#endif
