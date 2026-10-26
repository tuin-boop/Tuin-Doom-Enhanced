/* Contained electromechanical computer ambience. The steady component uses
 * periodic motor tones and narrow noise bands, without broad fan hiss.
 * Activity stays separate so three spatial takes cannot stack the motor hum. */
#include <stdlib.h>
#include "computer_synth.h"
#include "synth_kit.h"

#define COMPUTER_TAU 6.28318530717958647692

/* Double precision rotation keeps the motor tones in phase at their wrap.
 * Each tone and modulation completes whole cycles in the requested period. */
typedef struct { double s, c, ds, dc; float amp; } periodic_t;

static periodic_t periodic_make(double cycles, double phase, float amp, int frames)
{
    double step = COMPUTER_TAU * cycles / frames;
    periodic_t p = { sin(phase), cos(phase), sin(step), cos(step), amp };
    return p;
}

static float periodic_next(periodic_t *p)
{
    double s = p->s;
    float value = (float)s * p->amp;
    p->s = s * p->dc + p->c * p->ds;
    p->c = p->c * p->dc - s * p->ds;
    return value;
}

static periodic_t periodic_tone(float frequency, float amp, rng_t *r, int rate, int frames)
{
    double cycles = floor((double)frequency * frames / rate + 0.5);
    if (frequency >= 0.45f * rate || cycles < 1) amp = 0;
    return periodic_make(cycles, rng_range(r, 0, 2 * SYNTH_PI), amp, frames);
}

/* Filter a repeating noise cycle twice, keeping the filter state between
 * passes. The first pass settles its circular history; the second is a
 * periodic, narrow grain rather than a handful of audible pitched partials. */
static void periodic_grain(float *out, float *scratch, float center, float width,
                           float rms, float depth, double cycles, double phase,
                           rng_t *r, int rate, int frames)
{
    float q = center / (2.5f * width);
    svf_t first = svf_make(center, q, rate), second = svf_make(center, q, rate);
    rng_noise(r);
    rng_t cycle = *r;
    double sum = 0, squares = 0;
    for (int pass = 0; pass < 2; ++pass)
    {
        rng_t noise = cycle;
        for (int n = 0; n < frames; ++n)
        {
            svf_run(&first, rng_noise(&noise));
            svf_run(&second, first.band);
            if (pass)
            {
                scratch[n] = second.band;
                sum += second.band;
                squares += (double)second.band * second.band;
            }
        }
    }
    double mean = sum / frames, variance = squares / frames - mean * mean;
    float scale = variance > 1e-15 ? rms / (float)sqrt(variance) : 0;
    periodic_t modulation = periodic_make(cycles, phase, 1, frames);
    for (int n = 0; n < frames; ++n)
        out[n] += (scratch[n] - (float)mean) * scale * (1 + depth * periodic_next(&modulation));
}

void Computer_BodyRender(unsigned seed, int rate, float *out, int frames)
{
    if (!out || frames <= 0 || rate <= 0) return;
    rng_t r = rng_seed(seed);
    periodic_t motor[4];
    motor[0] = periodic_tone(61.1f, 0.110f, &r, rate, frames);
    motor[1] = periodic_tone(62.925f, 0.017f, &r, rate, frames);
    motor[2] = periodic_tone(131.225f, 0.034f, &r, rate, frames);
    motor[3] = periodic_tone(197.3f, 0.017f, &r, rate, frames);
    periodic_t edge = periodic_tone(1041.725f, 0.006f, &r, rate, frames);
    periodic_t slow3 = periodic_make(3, 0.8, 1, frames);
    periodic_t slow7 = periodic_make(7, 1.4, 1, frames);
    double flutter_cycles = floor(1.825 * frames / rate + 0.5);
    periodic_t flutter = periodic_make(flutter_cycles, 0.1, 1, frames);
    for (int n = 0; n < frames; ++n)
    {
        float a = 0;
        for (int k = 0; k < 4; ++k) a += periodic_next(&motor[k]);
        float flutter_value = periodic_next(&flutter);
        float slow = 1 + 0.065f * periodic_next(&slow3) + 0.027f * periodic_next(&slow7);
        out[n] += a * slow * (1 + 0.035f * flutter_value) + periodic_next(&edge);
    }
    float *scratch = malloc((size_t)frames * sizeof(float));
    if (!scratch) return;
    periodic_grain(out, scratch, 112, 24, 0.018f, 0.08f, 5, 0, &r, rate, frames);
    periodic_grain(out, scratch, 554, 36, 0.033f, 0.09f, flutter_cycles, 0.7, &r, rate, frames);
    periodic_grain(out, scratch, 839, 47, 0.021f, 0.06f, 9, 0.4, &r, rate, frames);
    free(scratch);
}

/* Small mechanical touches continue between the larger events. Their soft
 * attack and short, imperfect modes should read as head/logic motion rather
 * than a regular metronome or a high spark crackle. */
static void micro_touch(float *out, int frames, int at, int rate, rng_t *r)
{
    float seconds = rng_range(r, 0.025f, 0.075f);
    int length = (int)(seconds * rate);
    float f1 = rng_range(r, 325, 475);
    float decay = rng_range(r, 0.004f, 0.009f), amp = rng_range(r, 0.014f, 0.035f);
    svf_t grain = svf_make(rng_range(r, 850, 1250), 0.85f, rate);
    for (int n = 0; n < length && at + n < frames; ++n)
    {
        float t = n / (float)rate;
        float envelope = (1 - expf(-t / 0.0018f)) * fminf((seconds - t) / 0.010f, 1);
        float modes = f1 < 0.45f * rate ? 0.30f * sinf(2 * SYNTH_PI * f1 * t) : 0;
        svf_run(&grain, rng_noise(r));
        out[at + n] += amp * envelope * (modes * expf(-t / decay)
                    + 0.45f * grain.band * expf(-t / 0.007f));
    }
}

/* A head movement is a little scrape followed by damped paper/metal modes.
 * Adjacent small seeks in a read burst vary in strength and hesitation. */
static void head_seek(float *out, int frames, int at, int rate, rng_t *r,
                       float amp, int large)
{
    float seconds = large ? rng_range(r, 0.040f, 0.060f) : rng_range(r, 0.022f, 0.036f);
    float attack = large ? 0.0012f : 0.00065f;
    float d1 = large ? 0.013f : 0.0065f, d2 = large ? 0.019f : 0.009f;
    float frequencies[3] = { 790, 1580, 2720 }, modes[3] = { 0.18f, 0.09f, 0.025f };
    float decays[3] = { 0.010f, 0.006f, 0.0038f }, phases[3];
    for (int k = 0; k < 3; ++k)
    {
        frequencies[k] *= rng_range(r, 0.94f, 1.06f);
        phases[k] = rng_range(r, -0.25f, 0.25f);
    }
    svf_t grain = svf_make(rng_range(r, 1650, 2250), 0.7f, rate);
    svf_t scrape = svf_make(rng_range(r, 800, 1250), 0.7f, rate);
    int length = (int)(seconds * rate);
    for (int n = 0; n < length && at + n < frames; ++n)
    {
        float t = n / (float)rate, noise = rng_noise(r);
        svf_run(&grain, noise); svf_run(&scrape, noise);
        float x = 0.95f * grain.band * expf(-t / d1)
                + 0.60f * scrape.band * expf(-t / d2);
        for (int k = 0; k < 3; ++k)
            if (frequencies[k] < 0.45f * rate)
                x += modes[k] * sinf(2 * SYNTH_PI * frequencies[k] * t + phases[k]) * expf(-t / decays[k]);
        float envelope = (1 - expf(-t / attack)) * fminf((seconds - t) / 0.008f, 1);
        out[at + n] += amp * envelope * x;
    }
}

/* A firm contact attack with a short chassis knock and just a few contact
 * bounces. This is deliberately rarer than either fine motion or HDD reads. */
static void relay_clack(float *out, int frames, int at, int rate, rng_t *r, float amp)
{
    add_click(out, frames, at, rate, r, 0.72f * amp, 950, 2600);
    add_partial(out, frames, at, rate, rng_range(r, 270, 360), 0.43f * amp, 0.011f, 0);
    add_partial(out, frames, at, rate, rng_range(r, 630, 820), 0.26f * amp, 0.007f, 0.2f);
    add_partial(out, frames, at, rate, rng_range(r, 1280, 1680), 0.11f * amp, 0.004f, 0);
    int bounces = rng_unit(r) < 0.45f ? 2 : 1;
    for (int k = 0; k < bounces; ++k)
        add_click(out, frames, at + (int)(rng_range(r, 0.004f, 0.012f) * rate), rate,
                  r, rng_range(r, 0.15f, 0.26f) * amp, 1000, 2800);
}

/* The audition's rounded, low transistor processing gesture: small internal
 * state changes, flutter and friction, without a glassy inharmonic register.
 * These last longer than contacts and must not turn every tiny touch into
 * a pitched notification. */
static void electrical_detail(float *out, int frames, int at, int rate, rng_t *r)
{
    static const float pattern[] = { 1, 1, 0.89f, 1.08f, 1.08f, 0.96f, 1, 0.93f, 1.04f, 1 };
    float seconds = rng_range(r, 0.45f, 0.91f), carrier = rng_range(r, 347, 408);
    float clock = rng_range(r, 15, 23), state_rate = rng_range(r, 16, 23);
    float amp = rng_range(r, 0.70f, 0.90f), phase = 0, frequency = carrier;
    float duty = rng_range(r, 0.30f, 0.36f), partials[7];
    float smooth = 1 - expf(-1 / (0.004f * rate));
    for (int h = 1; h <= 7; ++h)
        partials[h - 1] = 2 * sinf(SYNTH_PI * h * duty) / (SYNTH_PI * h) * expf(-0.36f * (h - 1));
    int length = (int)(seconds * rate);
    svf_t grain = svf_make(rng_range(r, 650, 950), 0.7f, rate);
    for (int n = 0; n < length && at + n < frames; ++n)
    {
        float t = n / (float)rate, u = t / seconds;
        int state = (int)(t * state_rate) % 10;
        frequency += (carrier * pattern[state] - frequency) * smooth;
        phase += 2 * SYNTH_PI * fminf(frequency, 0.35f * rate) / rate;
        float rounded = 0;
        for (int h = 1; h <= 7; ++h)
            if (h * frequency < 0.45f * rate) rounded += partials[h - 1] * sinf(h * phase);
        float flutter = powf(0.5f + 0.5f * sinf(2 * SYNTH_PI * clock * t + 0.2f), 0.8f);
        svf_run(&grain, rng_noise(r));
        float x = (0.68f * rounded + 0.17f * sinf(phase + 0.40f * sinf(1.73f * phase)))
                * (0.32f + 0.68f * flutter);
        float friction = (0.45f + 0.55f * sinf(SYNTH_PI * u)) * (0.4f + 0.6f * flutter);
        x = 0.76f * x + 0.15f * grain.band * friction;
        float envelope = fminf(1, t / 0.022f) * fminf(1, (seconds - t) / 0.095f);
        envelope = sinf(0.5f * SYNTH_PI * envelope);
        out[at + n] += amp * x * envelope * envelope * (0.84f + 0.16f * sinf(SYNTH_PI * u)) * (1 - 0.17f * u);
    }
}

void Computer_ActivityRender(unsigned seed, int rate, float *out, int frames)
{
    if (!out || frames <= 0 || rate <= 0) return;
    float *own = calloc((size_t)frames, sizeof(float));
    if (!own) return;
    rng_t r = rng_seed(seed);
    float end = frames / (float)rate;
    /* Three pooled takes together supply one fine, quiet layer of motion. */
    for (float t = rng_range(&r, 0.04f, 0.22f); t < end; t += rng_range(&r, 0.45f, 0.95f))
        micro_touch(own, frames, (int)(t * rate), rate, &r);
    /* Per-take intervals leave space when all three spatial voices play:
     * combined reads about every 3-6 s, relays 8-15 s, larger detail 5-10 s. */
    for (float t = rng_range(&r, 0.6f, 8.0f); t < end; t += rng_range(&r, 10.0f, 17.0f))
    {
        float duration = rng_range(&r, 0.17f, 0.56f), stop = t + duration;
        head_seek(own, frames, (int)(t * rate), rate, &r, rng_range(&r, 0.85f, 1.05f), 1);
        float strength = rng_range(&r, 0.38f, 0.63f);
        float read = t + rng_range(&r, 0.050f, 0.083f);
        for (int k = 1; read < stop; ++k)
        {
            strength = 0.64f * strength + 0.36f * rng_range(&r, 0.20f, 0.80f);
            head_seek(own, frames, (int)(read * rate), rate, &r, strength, 0);
            read += rng_range(&r, 0.012f, 0.028f);
            if (k % 4 == 0) read += rng_range(&r, 0.018f, 0.045f);
        }
        if (rng_unit(&r) < 0.55f)
            head_seek(own, frames, (int)((stop + rng_range(&r, 0.07f, 0.13f)) * rate), rate, &r, 0.32f, 0);
    }
    for (float t = rng_range(&r, 4.0f, 16.0f); t < end; t += rng_range(&r, 27.0f, 43.0f))
    {
        relay_clack(own, frames, (int)(t * rate), rate, &r, rng_range(&r, 0.85f, 1.1f));
        if (rng_unit(&r) < 0.65f)
            relay_clack(own, frames, (int)((t + rng_range(&r, 0.07f, 0.19f)) * rate), rate, &r, rng_range(&r, 0.45f, 0.72f));
    }
    for (float t = rng_range(&r, 2.0f, 12.0f); t < end; t += rng_range(&r, 17.0f, 29.0f))
        electrical_detail(own, frames, (int)(t * rate), rate, &r);
    for (int n = 0; n < frames; ++n) out[n] += own[n];
    free(own);
}
