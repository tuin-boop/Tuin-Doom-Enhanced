/* Gore: a kill's wet burst and a body slapping into a floor or wall. Both
 * are filtered noise only, no tones and no sweeps: a soft wet body, a
 * slosh of spray, then drops pattering down around it. Played quietly
 * under the monster's own death sound. */
#include <math.h>
#include "impact_synth.h"
#include "synth_kit.h"

/* Noise through a band (lo..hi), rising over attack seconds and dying over
   tau, its level jumping every grain seconds when grain > 0 (a slosh, not
   a hiss). */
static void wet(float *out, int frames, int at, int rate, rng_t *r, float lo, float hi,
                float attack, float tau, float grain, float amp)
{
    svf_t high = svf_make(lo, 0.6f, rate), low = svf_make(hi, 0.6f, rate);
    int length = (int)(tau * 6 * rate), every = grain > 0 ? (int)(grain * rate) + 1 : 0;
    float level = 1;
    for (int n = 0; n < length && at + n < frames; ++n)
    {
        float t = n / (float)rate, env = (1 - expf(-t / attack)) * expf(-t / tau);
        if (every && n % every == 0) { float u = rng_unit(r); level = 0.3f + 0.7f * u * u; }
        svf_run(&high, rng_noise(r));
        svf_run(&low, high.high);
        if (at + n >= 0) out[at + n] += amp * env * level * low.low;
    }
}

/* Drops landing over start..end seconds, fewer and softer as they go. */
static void patter(float *out, int frames, int rate, rng_t *r, int count, float start, float end, float amp)
{
    for (int k = 0; k < count; ++k)
    {
        float when = rng_range(r, start, end), fade = 1 - 0.6f * (when - start) / (end - start + 1e-6f);
        wet(out, frames, (int)(when * rate), rate, r, rng_range(r, 500, 900), rng_range(r, 1800, 3200),
            0.0008f, rng_range(r, 0.003f, 0.008f), 0, amp * fade * rng_range(r, 0.4f, 1));
    }
}

/* Every take at one loudness over its first 0.12 seconds. */
static void level(float *out, int frames, int rate, rng_t *r, float target)
{
    double sum = 0;
    int window = (int)(0.12f * rate);
    for (int n = 0; n < window && n < frames; ++n) sum += out[n] * out[n];
    float gain = target / (float)sqrt(sum / window + 1e-12) * rng_range(r, 0.89f, 1.12f);
    for (int n = 0; n < frames; ++n) out[n] *= gain;
}

void Gore_Render(int kind, unsigned seed, int rate, float *out, int frames)
{
    rng_t r = rng_seed(seed);
    if (kind == IMPACT_GORE_SLAP)
    {
        /* The body's weight, a wet slap on top, a few drops shaken off. */
        wet(out, frames, 0, rate, &r, 40, rng_range(&r, 160, 220), 0.001f, rng_range(&r, 0.05f, 0.07f), 0, 1.0f);
        wet(out, frames, 0, rate, &r, 400, rng_range(&r, 2000, 2800), 0.0006f, 0.015f, 0, 0.5f);
        patter(out, frames, rate, &r, (int)rng_range(&r, 2, 5.99f), 0.04f, 0.25f, 0.12f);
        level(out, frames, rate, &r, 0.16f);
    }
    else
    {
        /* A soft wet body, the spray sloshing out, then the drops coming down. */
        wet(out, frames, 0, rate, &r, 60, rng_range(&r, 700, 1000), 0.002f, 0.04f, 0, 0.6f);
        wet(out, frames, (int)(0.004f * rate), rate, &r, 600, rng_range(&r, 1100, 1500), 0.003f, 0.07f, 0.002f, 0.4f);
        patter(out, frames, rate, &r, (int)rng_range(&r, 6, 12.99f), 0.08f, 0.4f, 0.12f);
        level(out, frames, rate, &r, 0.12f);
    }
}
