/* What the made-up effects are called and how long they ring, the hum the
 * dying lights cut off, and the 8-bit form they play in. */
#include <math.h>
#include "ambient_synth.h"
#include "impact_synth.h"

static const char *const names[IMPACT_KINDS] = {
    "spark-crackle", "spark-fizz", "spark-zap", "lamp-dies", "screen-dies",
    "glass-tinkle", "glass-shatter", "glass-break", "glass-big", "glass-fracture", "glass-fracture-big",
    "step-stone", "step-metal", "step-wood", "step-soft", "splash-water", "splash-thick",
    "run-stone", "run-metal", "run-wood", "run-soft", "gear-light", "gear-slung", "gear-heavy", "gear-armor",
    "gear-light-run", "gear-slung-run", "gear-heavy-run", "gear-armor-run", "flyby",
    "gore-burst", "gore-slap",
};
static const float seconds[IMPACT_KINDS] = { 0.5f, 0.5f, 0.55f, 0.9f, 0.6f, 0.9f, 0.7f, 1.8f, 2.1f, 1.8f, 2.1f,
                                                0.3f, 0.3f, 0.3f, 0.3f, 0.4f, 0.4f,
                                                0.3f, 0.3f, 0.3f, 0.3f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f,
                                                0.3f, 0.5f, 0.4f };

const char *Impact_Name(int kind) { return kind >= 0 && kind < IMPACT_KINDS ? names[kind] : "?"; }
int Impact_Rate(int kind) { return kind >= IMPACT_GLASS_TINKLE && kind <= IMPACT_GLASS_FRACTURE_BIG ? 22050 : 11025; }
int Impact_Length(int kind, int rate) { return kind >= 0 && kind < IMPACT_KINDS ? (int)(seconds[kind] * rate) : 0; }

void Impact_Render(int kind, unsigned seed, int rate, float *out, int frames)
{
    if (kind <= IMPACT_SCREEN_DIES) Spark_Render(kind, seed, rate, out, frames);
    else if (kind <= IMPACT_GLASS_BIG) Glass_Render(kind, seed, rate, out, frames);
    else if (kind <= IMPACT_GLASS_FRACTURE_BIG) Fracture_Render(kind, seed, rate, out, frames);
    else if (kind == IMPACT_FLYBY) Flyby_Render(seed, rate, out, frames);
    else if (kind >= IMPACT_GORE_BURST) Gore_Render(kind, seed, rate, out, frames);
    else Step_Render(kind, seed, rate, out, frames);
}

/* The hum the lamp or screen played while it was alive, at the level the
   dying sample was made for. */
void Impact_Hum(int screen, unsigned seed, int rate, float *out, int frames)
{
    Machine_Render(screen ? AMBIENT_CRT : AMBIENT_LAMP, seed, rate, out, frames, 0, 0);
}

/* Cracks peak at twice the rest of a spark: past 0.6 they round off into
   the 8 bits instead of clipping square. */
void Impact_To8Bit(const float *in, unsigned char *out, int frames)
{
    for (int n = 0; n < frames; ++n)
    {
        float x = in[n], a = fabsf(x);
        if (a > 0.6f) x = copysignf(0.6f + 0.4f * tanhf((a - 0.6f) / 0.4f), x);
        float v = 128 + 127 * x;
        out[n] = (unsigned char)(v <= 0 ? 0 : v >= 255 ? 255 : lrintf(v));
    }
}
