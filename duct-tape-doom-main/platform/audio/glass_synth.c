/* A pane fails, its pieces catch the frame, then strike the floor and nod
 * over their edges. Each piece keeps its own inharmonic modes throughout;
 * each contact has a short pressure transient as well as modal vibration.
 * The late crash comes from continuing collisions, not a long noise tail. */
#include <math.h>
#include <stdlib.h>
#include "impact_synth.h"
#include "synth_kit.h"

#define GRAVITY (35.0f * 35.0f)
#define MODES 10

typedef struct {
    int modes;
    float size, freq[MODES], q[MODES], gain[MODES];
} shard_t;

/* Frequencies describe a piece of glass, independently of the sample rate.
   A low-rate render omits inaudible modes rather than shrinking the object. */
static shard_t shard_make(rng_t *r, int rate, float size)
{
    static const float ratio[MODES] = { 0.6f, 1.54f, 2.35f, 3.25f, 4.5f, 5.8f, 7.1f, 8.9f, 10.7f, 12.9f };
    shard_t s = {0};
    s.size = size;
    float base = rng_range(r, 500, 850) * powf(3.6f, 1 - size);
    for (int m = 0; m < MODES; ++m)
    {
        float freq = base * ratio[m] * rng_range(r, 0.91f, 1.09f);
        float decay = rng_range(r, 0.006f, 0.014f) * (0.65f + 0.55f * size)
                    / (1 + powf(freq / 3400, 1.3f));
        float gain = rng_range(r, 0.45f, 1.0f);
        // Small fragments click and crunch; a strong low fundamental reads
        // as a bell or a metal plate rather than broken window glass.
        if (freq < 1800) gain *= 0.22f;
        if (m > 6) gain *= 0.8f;
        if (freq > 900 && freq < 1800) gain *= 0.8f;
        if (freq > 1500 && freq < 5500) gain *= 1.3f;
        if (freq > 5500) gain *= 1.3f;
        if (freq < 0.45f * rate)
        {
            int k = s.modes++;
            s.freq[k] = freq;
            /* Compensate the bilinear filter's frequency warping so high
               partials keep the specified short decay near Nyquist. */
            s.q[k] = fmaxf(1.5f, 0.5f * rate * sinf(2 * SYNTH_PI * freq / rate) * decay);
            s.gain[k] = gain;
        }
    }
    return s;
}

/* The frame/pane has a short, broad response, not a pitched bass thump. */
static shard_t pane_make(rng_t *r, int rate)
{
    static const float freq[] = { 360, 780, 1380, 2240, 3700, 6100 };
    shard_t s = {0};
    s.size = 1;
    for (int m = 0; m < 6; ++m)
    {
        float hz = freq[m] * rng_range(r, 0.88f, 1.12f);
        if (hz < 0.45f * rate)
        {
            int k = s.modes++;
            s.freq[k] = hz;
            s.q[k] = rng_range(r, 2, 5);
            s.gain[k] = rng_range(r, 0.5f, 1.0f) * (m == 0 ? 0.4f : 1);
        }
    }
    return s;
}

/* An offline modal voice: summing each contact's impulse response preserves
   the same ringing state as a continuously running bank. Contact position
   and hardness change the gains/excitation, never the piece's frequencies. */
static void ping(float *out, int frames, int at, int rate, rng_t *r,
                 const shard_t *s, float amp, float tonal)
{
    if (at >= frames) return;
    svf_t band[MODES];
    float weight[MODES], longest = 0;
    for (int m = 0; m < s->modes; ++m)
    {
        band[m] = svf_make(s->freq[m], s->q[m], rate);
        weight[m] = s->gain[m] * sqrtf(s->q[m] / 45) * rng_range(r, 0.6f, 1.25f);
        longest = fmaxf(longest, 2 * s->q[m] / (rate * sinf(2 * SYNTH_PI * s->freq[m] / rate)));
    }
    float hardness = rng_range(r, 0.65f, 1.0f);
    float contact = rng_range(r, 0.0002f, 0.00065f) * (1.15f - 0.35f * hardness) * rate;
    contact = fmaxf(contact, 1.5f);
    float excite_gain = sqrtf(0.00027f * rate / contact);
    float scratch_keep = expf(-1 / (rng_range(r, 0.0015f, 0.004f) * rate));
    float scratch = 1, previous = 0, polarity = rng_unit(r) < 0.5f ? -1 : 1;
    svf_t surface = svf_make(1100 + 1000 * hardness, 0.7f, rate);
    int length = (int)(fminf(9.2f * longest + 0.006f, 0.35f) * rate);
    for (int n = 0; n < length && at + n < frames; ++n)
    {
        float noise = rng_noise(r);
        /* Integrate the short force pulse over a sample: contact duration
           still varies when a low-rate sample cannot resolve its peak. */
        float force = n < contact ? (cosf(SYNTH_PI * n / contact)
                    - cosf(SYNTH_PI * fminf((n + 1) / contact, 1))) * contact / SYNTH_PI : 0;
        float excite = force * (0.5f * noise + 0.5f * polarity) * excite_gain;
        float pressure = (force - previous) * fminf(0.5f * contact, 6) * polarity;
        previous = force;
        float ring = 0;
        for (int m = 0; m < s->modes; ++m)
        {
            svf_run(&band[m], excite);
            ring += weight[m] * band[m].band * band[m].k;
        }
        svf_run(&surface, noise * scratch);
        scratch *= scratch_keep;
        float acceleration = 0.55f * pressure + 0.65f * surface.high;
        if (at + n >= 0) out[at + n] += amp * (tonal * 12 * ring + (1 - 0.65f * tonal) * acceleration);
    }
}

static void shards(float *out, int frames, int rate, rng_t *r, int count,
                   float sill, float pane, float spread, float amp, float tonal, int frame)
{
    for (int i = 0; i < count; ++i)
    {
        float u = rng_unit(r);
        float size = i % 6 == 0 ? rng_range(r, 0.08f, 0.3f) : 0.35f + 0.65f * sqrtf(u);
        shard_t shard = shard_make(r, rate, size);
        float release = spread * powf(rng_unit(r), 1.5f);
        float height = fmaxf(1, sill + pane * powf(rng_unit(r), 1.5f));
        float up = rng_range(r, -80, 80);
        float velocity = sqrtf(up * up + 2 * GRAVITY * height);
        float t = release + (up + velocity) / GRAVITY;
        float level = amp * (0.35f + 0.65f * size) * powf(velocity / 480, 0.8f) * rng_range(r, 0.65f, 1.0f);
        if (frame && rng_unit(r) < 0.8f)
        {
            float frame_at = release + rng_range(r, 0.003f, 0.025f);
            ping(out, frames, (int)(frame_at * rate), rate, r, &shard, 0.7f * level, tonal);
            if (rng_unit(r) < 0.6f)
                ping(out, frames, (int)((frame_at + rng_range(r, 0.07f, 0.15f)) * rate), rate, r, &shard, 0.7f * level, tonal);
        }
        /* One restitution and flight speed belong to this piece. Rebounds
           slow and shorten together; they are not a new random tick train. */
        float restitution = rng_range(r, 0.4f, 0.55f);
        for (int bounce = 0; bounce < 4 && t * rate < frames; ++bounce)
        {
            ping(out, frames, (int)(t * rate), rate, r, &shard, level, tonal);
            /* A flat, irregular piece can nod across another edge within a
               contact, in addition to its larger ballistic rebound. */
            int edges = bounce == 0 ? 1 : (int)rng_range(r, 1, 3.99f);
            float edge_at = t, edge_level = level;
            for (int edge = 0; edge < edges; ++edge)
            {
                edge_at += rng_range(r, 0.004f, 0.018f);
                edge_level *= 0.75f;
                if (rng_unit(r) < 0.85f)
                    ping(out, frames, (int)(edge_at * rate), rate, r, &shard,
                         edge_level * rng_range(r, 0.4f, 0.8f), tonal);
            }
            velocity *= restitution;
            t += 2 * velocity / GRAVITY;
            level *= powf(restitution, 0.7f);
        }
    }
}

void Glass_Shards(float *out, int frames, int rate, unsigned seed, int count,
                  float sill, float pane, float spread, float amp, float tonal)
{
    rng_t r = rng_seed(seed);
    shards(out, frames, rate, &r, count, sill, pane, spread, amp, tonal, 1);
}

static void debris(float *out, int frames, int rate, rng_t *r, float start, int count, float amp)
{
    for (int i = 0; i < count; ++i)
    {
        float t = start - 0.3f * logf(fmaxf(rng_unit(r), 1e-6f));
        shard_t bit = shard_make(r, rate, rng_range(r, 0.05f, 0.25f));
        ping(out, frames, (int)(t * rate), rate, r, &bit, amp * rng_range(r, 0.35f, 1.0f), 0.7f);
    }
}

void Glass_Debris(float *out, int frames, int rate, unsigned seed, float start, int count, float amp)
{
    rng_t r = rng_seed(seed ^ 0x2545f491u);
    debris(out, frames, rate, &r, start, count, amp);
}

/* A few broad fracture packets build the crash over tens of milliseconds.
   The continuing frame/floor contacts are rendered by the same shard voices. */
static void shatter(float *out, int frames, int rate, rng_t *r, int big)
{
    shard_t pane = pane_make(r, rate);
    int attack = (int)(0.08f * rate);
    float *hit = calloc((size_t)attack, sizeof(float));
    if (hit)
    {
        ping(hit, attack, 0, rate, r, &pane, 0.9f, 0.65f);
        /* Calibrate the contact force before adding the other collisions.
           Random excitation must not turn one variant's frame hit into the
           whole crash while another variant has scarcely any attack. */
        float peak = 0;
        for (int n = 0; n < attack; ++n) peak = fmaxf(peak, fabsf(hit[n]));
        float level = peak > 0 ? 1 / peak : 1;
        for (int n = 0; n < attack && n < frames; ++n) out[n] += hit[n] * level;
        free(hit);
    }
    add_thump(out, frames, 0, rate, 520, 220, 0.012f, 0.25f);
    int pieces = big ? 14 : 10;
    for (int i = 0; i < pieces; ++i)
    {
        shard_t piece = shard_make(r, rate, rng_range(r, 0.25f, 0.9f));
        float t = rng_range(r, 0.002f, big ? 0.12f : 0.095f);
        float level = rng_range(r, 0.2f, 0.45f);
        ping(out, frames, (int)(t * rate), rate, r, &piece, level, 0.7f);
        ping(out, frames, (int)((t + rng_range(r, 0.004f, 0.017f)) * rate), rate, r, &piece, 0.45f * level, 0.7f);
    }
}

void Glass_Render(int kind, unsigned seed, int rate, float *out, int frames)
{
    rng_t r = rng_seed(seed);
    switch (kind)
    {
    case IMPACT_GLASS_TINKLE:
    {
        float *bits = calloc((size_t)frames, sizeof(float));
        if (!bits) break;
        shard_t contact = pane_make(&r, rate);
        ping(bits, frames, 0, rate, &r, &contact, 0.65f, 0.2f);
        shards(bits, frames, rate, &r, 9, 32, 48, 0.03f, 0.4f, 0.7f, 0);
        debris(bits, frames, rate, &r, 0.45f, 4, 0.08f);
        /* This small ancillary effect keeps its original short duration. */
        for (int n = 0; n < frames; ++n)
        {
            float fade = fmaxf(0, 1 - (n / (float)rate - 0.7f) / 0.15f);
            fade = fminf(1, fade);
            out[n] += bits[n] * fade * fade;
        }
        free(bits);
        break;
    }
    case IMPACT_GLASS_SHATTER:
        shatter(out, frames, rate, &r, 0);
        break;
    case IMPACT_GLASS_BREAK:
        shatter(out, frames, rate, &r, 0);
        Glass_Shards(out, frames, rate, seed, 18, 44, 88, 0.15f, 0.46f, 0.72f);
        debris(out, frames, rate, &r, 0.8f, 5, 0.08f);
        break;
    case IMPACT_GLASS_BIG:
        shatter(out, frames, rate, &r, 1);
        Glass_Shards(out, frames, rate, seed, 26, 48, 112, 0.18f, 0.44f, 0.72f);
        debris(out, frames, rate, &r, 0.9f, 7, 0.08f);
        break;
    }
}
