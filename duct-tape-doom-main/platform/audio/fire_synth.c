/* Heat and air. Lava rumbles low and heaves up heavy bubbles - welling
 * noise, not a tone, which would sound like a beam - that burst and
 * sizzle; a fire wall roars and crackles, a loud pop now and then; cracked
 * magma rock only ticks over a faint rumble. Wind under the sky gusts and
 * sometimes whistles; rain hisses and patters, and taps on the windows. */
#include <math.h>
#include "ambient_synth.h"
#include "synth_kit.h"

/* Noise below cutoff, heaving by a wandering swell. */
static void rumble(float *out, int frames, int rate, rng_t *r, float cutoff, float amp)
{
    svf_t body = svf_make(cutoff, 0.8f, rate);
    drift_t swell = drift_make(r, 0.4f, 1.0f, 1.0f, 4.0f, rate);
    for (int n = 0; n < frames; ++n)
    {
        svf_run(&body, rng_noise(r));
        out[n] += amp * drift_next(&swell, r) * body.low;
    }
}

/* Sharp ticks at random, mostly quiet, a few loud: u cubed. */
static void ticks(float *out, int frames, int rate, rng_t *r, float per_second, float amp, float lo, float hi)
{
    for (int n = 0; n < frames; ++n)
        if (rng_unit(r) < per_second / rate)
        {
            float u = rng_unit(r);
            add_click(out, frames, n, rate, r, amp * (0.1f + 0.9f * u * u * u), lo, hi);
        }
}

static void lava(float *out, int frames, int rate, rng_t *r)
{
    rumble(out, frames, rate, r, 90, 0.5f);
    for (float t = rng_range(r, 0.2f, 1.5f); t * rate < frames; t += rng_range(r, 0.8f, 2.5f))
    {
        /* A heavy bubble swelling: low noise welling up, no tone in it. */
        float swell = rng_range(r, 0.15f, 0.4f);
        svf_t well = svf_make(100, 0.9f, rate);
        int start = (int)(t * rate), length = (int)(swell * rate);
        for (int n = 0; n < length && start + n < frames; ++n)
        {
            float x = n / (float)length;
            if (n % 32 == 0) svf_tune(&well, 100 + 150 * x, 0.9f, rate);
            svf_run(&well, rng_noise(r));
            out[start + n] += 0.6f * x * x * well.low;
        }
        float burst = t + swell;
        int at = (int)(burst * rate);
        add_thump(out, frames, at, rate, 160, 60, 0.02f, 0.3f);
        add_crackle(out, frames, rate, r, burst, burst + 0.35f, 120, 0.12f, 0.2f, 1500, 4500);
    }
    ticks(out, frames, rate, r, 3, 0.2f, 1500, 4000);
}

static void fire(float *out, int frames, int rate, rng_t *r)
{
    svf_t low = svf_make(600, 0.7f, rate), mid = svf_make(1200, 0.8f, rate);
    drift_t flutter = drift_make(r, 0.3f, 1.0f, 0.04f, 0.15f, rate);
    for (int n = 0; n < frames; ++n)
    {
        float noise = rng_noise(r);
        svf_run(&low, noise); svf_run(&mid, noise);
        out[n] += drift_next(&flutter, r) * (0.35f * low.low + 0.12f * mid.band);
    }
    ticks(out, frames, rate, r, 10, 0.6f, 1000, 4000);
    for (float t = rng_range(r, 0.5f, 2); t * rate < frames; t += rng_range(r, 1, 3))
    {
        int at = (int)(t * rate);
        add_thump(out, frames, at, rate, 200, 80, 0.01f, 0.25f);
        add_crackle(out, frames, rate, r, t, t + 0.08f, 300, 0.03f, 0.5f, 1200, 4000);
    }
}

static void magma(float *out, int frames, int rate, rng_t *r)
{
    rumble(out, frames, rate, r, 70, 0.25f);
    ticks(out, frames, rate, r, 2, 0.3f, 2500, 5000);
}

/* Gusts lull down to calm, the band of air wanders low..high, and whistle
   is how much it sings through a gap. It sings only now and then, and
   softly: a tone that keeps coming back grates. */
static void wind(float *out, int frames, int rate, rng_t *r, float calm, float low, float high, float whistling)
{
    svf_t air = svf_make(500, 1.2f, rate), whistle = svf_make(800, 15, rate);
    drift_t gust = drift_make(r, calm, 1.0f, 2.0f, 6.0f, rate);
    drift_t pitch = drift_make(r, low, high, 1.5f, 4.0f, rate);
    drift_t sing = drift_make(r, -1.6f, 1.0f, 4.0f, 10.0f, rate);
    for (int n = 0; n < frames; ++n)
    {
        float g = drift_next(&gust, r), p = drift_next(&pitch, r), s = fmaxf(drift_next(&sing, r), 0);
        if (n % 64 == 0) { svf_tune(&air, p, 1.2f, rate); svf_tune(&whistle, 2 * p, 15, rate); }
        float noise = rng_noise(r);
        svf_run(&air, noise); svf_run(&whistle, noise);
        out[n] += g * (0.4f * air.band + 0.25f * air.low + whistling * s * g * whistle.band);
    }
}

/* Rain: the hiss of countless drops, noise in the upper band swelling a
   little with the gusts, a soft wash under it, and over it the patter of
   single drops ticking on stone, now and then a heavier drip. */
static void rain(float *out, int frames, int rate, rng_t *r)
{
    svf_t hiss = svf_make(2600, 0.7f, rate), wash = svf_make(700, 0.6f, rate);
    drift_t swell = drift_make(r, 0.7f, 1.0f, 2.0f, 6.0f, rate);
    for (int n = 0; n < frames; ++n)
    {
        float noise = rng_noise(r), s = drift_next(&swell, r);
        svf_run(&hiss, noise); svf_run(&wash, noise);
        out[n] += s * (0.22f * hiss.band + 0.1f * wash.low);
    }
    ticks(out, frames, rate, r, 60, 0.25f, 2000, 5000);
    ticks(out, frames, rate, r, 4, 0.5f, 600, 1800);
}

/* Rain on a window pane, heard from inside: hundreds of drops a second,
   each a soft splat of noise a few milliseconds long, too many to tell
   apart, so they blur into a patter; the glass dulls them, so most of it is
   low and soft, a little bright spray on top. Now and then a fatter drop
   plips out of it; under it all the faint trickle of water running down.
   The patter thickens and thins as the gusts throw it. No single loud
   tick, no knock behind it: that is gravel, not rain. */
static void glass_rain(float *out, int frames, int rate, rng_t *r)
{
    svf_t patter = svf_make(1800, 0.7f, rate), body = svf_make(650, 0.6f, rate);
    svf_t plip = svf_make(1300, 2.0f, rate), trickle = svf_make(1100, 1.2f, rate);
    drift_t gust = drift_make(r, 0.45f, 1.0f, 1.5f, 4.0f, rate), run = drift_make(r, 0.4f, 1.0f, 2.0f, 6.0f, rate);
    float fine = 0, fat = 0, fine_keep = expf(-1.0f / (0.0015f * rate)), fat_keep = expf(-1.0f / (0.005f * rate));
    for (int n = 0; n < frames; ++n)
    {
        float g = drift_next(&gust, r);
        if (rng_unit(r) < 600.0f * g / rate) fine += rng_range(r, 0.3f, 1.0f);
        if (rng_unit(r) < 5.0f * g / rate)
        {
            fat += rng_range(r, 0.6f, 1.0f);
            svf_tune(&plip, rng_range(r, 1000, 1700), 2.0f, rate);
        }
        fine *= fine_keep; fat *= fat_keep;
        float splat = rng_noise(r) * fine, drop = rng_noise(r) * fat;
        svf_run(&patter, splat); svf_run(&body, splat); svf_run(&plip, drop);
        svf_run(&trickle, rng_noise(r));
        out[n] += 0.16f * patter.band + 0.22f * body.low + 0.2f * plip.band + 0.04f * drift_next(&run, r) * trickle.band;
    }
}

/* The room tone: low air moving, a faint breath of hiss over it, swelling
   slowly. No hum: a steady tone everywhere would grate. */
static void room(float *out, int frames, int rate, rng_t *r)
{
    svf_t low = svf_make(180, 0.7f, rate), air = svf_make(800, 0.6f, rate);
    drift_t swell = drift_make(r, 0.75f, 1.0f, 4.0f, 10.0f, rate);
    for (int n = 0; n < frames; ++n)
    {
        float noise = rng_noise(r);
        svf_run(&low, noise); svf_run(&air, noise);
        out[n] += drift_next(&swell, r) * (0.6f * low.low + 0.06f * air.band);
    }
}

void Fire_Render(int kind, unsigned seed, int rate, float *out, int frames)
{
    rng_t r = rng_seed(seed);
    switch (kind)
    {
    case AMBIENT_LAVA: lava(out, frames, rate, &r); break;
    case AMBIENT_FIRE: fire(out, frames, rate, &r); break;
    case AMBIENT_MAGMA: magma(out, frames, rate, &r); break;
    case AMBIENT_WIND: wind(out, frames, rate, &r, 0.15f, 300, 900, 0.2f); break;
    case AMBIENT_RAIN: rain(out, frames, rate, &r); break;
    case AMBIENT_GLASS_RAIN: glass_rain(out, frames, rate, &r); break;
    case AMBIENT_ROOM: room(out, frames, rate, &r); break;
    }
}
