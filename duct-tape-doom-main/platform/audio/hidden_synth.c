/* The sounds inside a tech base's walls (hidden_synth.h). Everything here
 * will be heard through a wall, so what matters is the body and the timing,
 * not the treble: the pipe's low modes, the press's blow, the creak's
 * slip-and-catch. Metal only barely rings, nothing glides in pitch, and nothing
 * sounds like a person made it: no knocking, no steps. */
#include <math.h>
#include <stdlib.h>
#include "hidden_synth.h"
#include "synth_kit.h"

#define PEAK 0.8f

static const char *const names[HIDDEN_KINDS] = {
    "pipe-rush", "steam", "duct", "settle", "press", "drip",
};
/* Seconds each kind runs, its longest variation and what rings after. */
static const float seconds[HIDDEN_KINDS] = { 6.0f, 1.8f, 7.5f, 2.0f, 4.0f, 3.0f };

const char *Hidden_Name(int kind) { return kind >= 0 && kind < HIDDEN_KINDS ? names[kind] : "?"; }
int Hidden_Length(int kind, int rate) { return kind >= 0 && kind < HIDDEN_KINDS ? (int)(seconds[kind] * rate) : 0; }

/* 0 before start, easing up over rise, held, easing down over fall. */
static float swell(float t, float start, float rise, float hold, float fall)
{
    if (t < start) return 0;
    t -= start;
    if (t < rise) return 0.5f - 0.5f * cosf(SYNTH_PI * t / rise);
    t -= rise;
    if (t < hold) return 1;
    t -= hold;
    if (t < fall) return 0.5f + 0.5f * cosf(SYNTH_PI * t / fall);
    return 0;
}

/* Water let into a pipe somewhere: the pipe's two lowest modes and the rush
   over them, churning; air caught in it gurgling through; then what is left
   trickling out. */
static void pipe_rush(float *out, int frames, int rate, rng_t *r)
{
    float rise = rng_range(r, 0.3f, 0.8f), hold = rng_range(r, 1.2f, 2.6f), fall = rng_range(r, 0.6f, 1.2f);
    float body = rng_range(r, 170, 320), running = rise + hold + fall;
    svf_t mode1 = svf_make(body, 3.5f, rate), mode2 = svf_make(body * 2.31f, 3, rate);
    svf_t rush = svf_make(rng_range(r, 600, 1100), 0.8f, rate);
    drift_t churn = drift_make(r, 0.55f, 1, 0.05f, 0.25f, rate);
    int end = (int)(running * rate);
    for (int n = 0; n < frames && n < end; ++n)
    {
        float t = n / (float)rate, x = rng_noise(r) * drift_next(&churn, r) * swell(t, 0, rise, hold, fall);
        svf_run(&mode1, x); svf_run(&mode2, x); svf_run(&rush, x);
        out[n] += 0.9f * mode1.band + 0.5f * mode2.band + 0.6f * rush.band;
    }
    for (float t = rng_range(r, 0.1f, 0.3f); t < running; t += rng_range(r, 0.05f, 0.35f))
        add_bubble(out, frames, (int)(t * rate), rate, rng_range(r, 280, 700),
                   0.25f * swell(t, 0, rise, hold, fall) * rng_range(r, 0.3f, 1), 0.04f, 1);
    float t = rise + hold + 0.6f * fall;
    for (int k = 3 + (int)(rng_unit(r) * 4); k > 0; --k)
    {
        t += rng_range(r, 0.05f, 0.22f);
        add_bubble(out, frames, (int)(t * rate), rate, rng_range(r, 500, 1100), 0.12f * rng_range(r, 0.4f, 1), 0.08f, 1);
    }
}

/* A valve snapping open, spitting, settling to a steady jet, then shut. */
static void steam(float *out, int frames, int rate, rng_t *r)
{
    float length = rng_range(r, 0.35f, 1.3f), release = rng_range(r, 0.1f, 0.3f);
    svf_t hiss = svf_make(rng_range(r, 2200, 3600), 0.7f, rate), edge = svf_make(1400, 0.7f, rate);
    drift_t flutter = drift_make(r, 0.7f, 1, 0.015f, 0.06f, rate);
    int end = (int)((length + release) * rate);
    for (int n = 0; n < frames && n < end; ++n)
    {
        float t = n / (float)rate;
        float env = (1 - expf(-t / 0.01f)) * (0.75f + 0.5f * expf(-t / 0.08f));
        if (t > length) env *= expf(-(t - length) / (0.35f * release));
        float x = rng_noise(r) * env * drift_next(&flutter, r);
        svf_run(&hiss, x); svf_run(&edge, x);
        out[n] += hiss.band + 0.5f * edge.high;
    }
    add_thump(out, frames, 0, rate, 110, 55, 0.04f, 0.3f);
}

/* Air pushed through a duct swelling and settling again, the duct booming
   softly, a fan's blades beating steadily somewhere along it; a damper
   clunks as it starts. */
static void duct(float *out, int frames, int rate, rng_t *r)
{
    float rise = rng_range(r, 1.2f, 2.4f), hold = rng_range(r, 0.5f, 2.0f), fall = rng_range(r, 1.5f, 2.5f);
    float beat = rng_range(r, 3.5f, 6.5f), boom = rng_range(r, 90, 150), phase = rng_unit(r);
    svf_t air = svf_make(rng_range(r, 220, 380), 0.7f, rate), body = svf_make(boom, 2, rate);
    svf_t blade = svf_make(0.8f * boom, 1.2f, rate);
    drift_t gust = drift_make(r, 0.75f, 1, 0.3f, 0.9f, rate);
    int end = (int)((rise + hold + fall) * rate);
    for (int n = 0; n < frames && n < end; ++n)
    {
        float t = n / (float)rate, on = swell(t, 0, rise, hold, fall) * drift_next(&gust, r), x = rng_noise(r) * on;
        if ((phase += beat / rate) >= 1) phase -= 1;
        float pulse = 0.5f - 0.5f * cosf(2 * SYNTH_PI * phase);
        svf_run(&air, x); svf_run(&body, x); svf_run(&blade, x * pulse * pulse);
        out[n] += 0.7f * air.low + 0.6f * body.band + 0.9f * blade.band;
    }
    add_thump(out, frames, (int)(0.05f * rate), rate, 140, 70, 0.05f, 0.25f);
}

/* The structure giving a little: mostly a tick of metal with the heat, now
   and then one or two more; else a joint creaking, slipping and catching
   at a rate it keeps. */
static void settle(float *out, int frames, int rate, rng_t *r)
{
    if (rng_unit(r) < 0.55f)
    {
        int ticks = 1 + (rng_unit(r) < 0.4f) + (rng_unit(r) < 0.15f);
        float t = 0.02f, ring = rng_range(r, 900, 1700);
        for (int k = 0; k < ticks; ++k)
        {
            float amp = k ? rng_range(r, 0.4f, 0.8f) : 1;
            add_click(out, frames, (int)(t * rate), rate, r, amp, 1200, 3000);
            add_partial(out, frames, (int)(t * rate), rate, ring * rng_range(r, 0.97f, 1.03f), 0.12f * amp, 0.025f, 0);
            t += rng_range(r, 0.25f, 0.8f);
        }
        return;
    }
    float start = 0.03f, length = rng_range(r, 0.25f, 0.6f), slips = rng_range(r, 22, 55), next = start;
    svf_t joint = svf_make(rng_range(r, 280, 520), 6, rate), skin = svf_make(rng_range(r, 1000, 1700), 5, rate);
    int end = (int)((start + length + 0.15f) * rate);
    for (int n = 0; n < frames && n < end; ++n)
    {
        float t = n / (float)rate, x = 0;
        if (t >= next && t < start + length)
        {
            x = sinf(SYNTH_PI * (t - start) / length) * rng_range(r, 0.5f, 1);
            next += rng_range(r, 0.75f, 1.25f) / slips;
        }
        svf_run(&joint, x); svf_run(&skin, x);
        out[n] += joint.band + 0.35f * skin.band;
    }
}

/* A heavy press far below: the stamp and the floor carrying it, then the
   hydraulics sighing as the ram goes back up, and it seating. */
static void press(float *out, int frames, int rate, rng_t *r)
{
    add_thump(out, frames, (int)(0.03f * rate), rate, rng_range(r, 70, 90), rng_range(r, 34, 42), 0.16f, 1);
    float back = rng_range(r, 1.0f, 1.5f), sighing = rng_range(r, 0.6f, 1.0f);
    svf_t rumble = svf_make(rng_range(r, 110, 160), 0.8f, rate), sigh = svf_make(rng_range(r, 600, 1000), 1.2f, rate);
    for (int n = 0; n < frames; ++n)
    {
        float t = n / (float)rate, x = rng_noise(r);
        svf_run(&rumble, t < 0.03f ? 0 : x * expf(-(t - 0.03f) / 0.35f));
        svf_run(&sigh, 0.25f * x * swell(t, back, 0.25f * sighing, 0.3f * sighing, 0.45f * sighing));
        out[n] += 0.8f * rumble.low + sigh.band;
    }
    add_thump(out, frames, (int)((back + sighing) * rate), rate, 90, 50, 0.08f, 0.3f);
}

/* Condensation: a drop into a puddle, often a second from the same spot. */
static void drip(float *out, int frames, int rate, rng_t *r)
{
    float pitch = rng_range(r, 700, 1400), t = 0.03f;
    for (int drops = 1 + (rng_unit(r) < 0.5f); drops > 0; --drops)
    {
        add_click(out, frames, (int)(t * rate), rate, r, 0.3f, 2500, 4500);
        add_bubble(out, frames, (int)(t * rate), rate, pitch * rng_range(r, 0.95f, 1.05f), 1, 0.1f, 1);
        t += rng_range(r, 1.0f, 2.2f);
    }
}

void Hidden_Render(int kind, unsigned seed, int rate, float *out, int frames)
{
    if (kind < 0 || kind >= HIDDEN_KINDS || frames <= 0) return;
    float *buffer = calloc((size_t)frames, sizeof(float));
    if (!buffer) return;
    rng_t r = rng_seed(seed * 31u + (unsigned)kind * 7919u + 1);
    switch (kind)
    {
    case HIDDEN_PIPE_RUSH: pipe_rush(buffer, frames, rate, &r); break;
    case HIDDEN_STEAM: steam(buffer, frames, rate, &r); break;
    case HIDDEN_DUCT: duct(buffer, frames, rate, &r); break;
    case HIDDEN_SETTLE: settle(buffer, frames, rate, &r); break;
    case HIDDEN_PRESS: press(buffer, frames, rate, &r); break;
    default: drip(buffer, frames, rate, &r); break;
    }
    /* Every variation peaks alike; whoever plays them sets how loud each kind is. */
    float peak = 0;
    for (int n = 0; n < frames; ++n) peak = fmaxf(peak, fabsf(buffer[n]));
    float gain = peak > 1e-6f ? PEAK / peak : 0;
    for (int n = 0; n < frames; ++n) out[n] += gain * buffer[n];
    free(buffer);
}
