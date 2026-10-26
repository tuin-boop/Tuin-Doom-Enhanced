/* Glass breaking as a fracture. The pane flexes and rings for a few
 * milliseconds; then a crack front branches through it, every crack a tiny
 * two-sided click that splits into more a fraction of a millisecond later -
 * hundreds of them, mostly faint, a few loud. The pane's own ring stops dead,
 * for it no longer exists, and the cracks ring a bank of fragment modes
 * instead, each only a few of them, big cracks the lower ones. Sparse, heavy
 * collisions follow the sharp crack front rather than a continuous fine
 * rattle. The crack front uses impulse excitation; the shared landing voices
 * also contain short contact-pressure and surface transients. Listeners hear breaking
 * in the timing of many small impulses, not in a noise burst (Warren and
 * Verbrugge, 1984); fracture swaps the object's modes for its fragments'
 * (Zheng and James, 2010). After it the shards fall as in glass_synth.c. */
#include <math.h>
#include <stdlib.h>
#include "impact_synth.h"
#include "synth_kit.h"

#define PANE_MODES 6
#define POOL 32
#define MAX_CRACKS 2000
/* How long pieces go on knocking after the pane fails, in seconds. */
#define KNOCKING 0.6f

typedef struct { float t, energy; } crack_t;

/* The crack front from start, branching until window runs out or the cracks
   grow too faint; returns how many. Big cracks split into several, faint
   ones rarely, so the front spreads, then dies out: a normal pane makes some
   330 cracks over 80 ms, a big one 1100 over 90 ms. The bounded queue is
   traversed by branch depth; each resulting impulse retains its event time.
   The cap limits synthesis work, not a physical fracture-energy budget. */
static int avalanche(rng_t *r, float start, float window, float size, crack_t *cracks)
{
    int count = 0, queued = 1;
    cracks[0] = (crack_t){ start, 1 };
    while (count < queued)
    {
        crack_t c = cracks[count++];
        float mean = (0.4f + 2.0f * sqrtf(c.energy)) * (0.8f + 0.4f * size);
        int children = (int)mean + (rng_unit(r) < mean - (int)mean);
        for (; children > 0 && queued < MAX_CRACKS; --children)
        {
            crack_t child = { c.t + 0.0005f * powf(24, rng_unit(r)), c.energy * rng_range(r, 0.5f, 0.9f) };
            if (child.energy > 0.0005f && child.t < start + window) cracks[queued++] = child;
        }
    }
    return count;
}

/* One crack: a two-sided click, +1 -0.7 +0.15, 50-300 microseconds long. */
static void click(float *out, int length, int at, int rate, rng_t *r, float amp)
{
    static const float shape[3] = { 1, -0.7f, 0.15f };
    int lobe = (int)(rng_range(r, 0.00005f, 0.0003f) * rate / 3 + 0.5f);
    if (lobe < 1) lobe = 1;
    if (rng_unit(r) < 0.5f) amp = -amp;
    for (int i = 0; i < 3 * lobe; ++i)
        if (at + i >= 0 && at + i < length) out[at + i] += amp * shape[i / lobe];
}

/* size 0.7 a normal pane, 1 a big one: a longer, larger crack front. */
static void fracture(float *out, int frames, int rate, rng_t *r, float size, float amp)
{
    float fail = rng_range(r, 0.004f, 0.010f), window = 0.05f + 0.05f * size;
    int length = (int)((fail + KNOCKING + 0.01f) * rate);
    if (length > frames) length = frames;
    float *cracks = calloc((size_t)length, sizeof(float));
    float *inputs = calloc((size_t)length * POOL, sizeof(float));
    crack_t *list = malloc(MAX_CRACKS * sizeof(crack_t));
    if (!cracks || !inputs || !list) { free(cracks); free(inputs); free(list); return; }

    /* The pane flexes once before the crack front. Its short, low body gives
       the break weight while the sharp front still defines the attack. */
    float flex = rng_range(r, 0.003f, 0.006f);
    for (int n = 0; n < (int)(3 * flex * rate) && n < frames; ++n)
    {
        float t = n / (float)rate;
        float pulse = t < flex ? -sinf(SYNTH_PI * t / flex) : 0.4f * sinf(SYNTH_PI * (t - flex) / (2 * flex));
        out[n] += 0.2f * amp * pulse;
    }
    svf_t pane[PANE_MODES];
    float pane_gain[PANE_MODES];
    for (int m = 0; m < PANE_MODES; ++m)
    {
        pane[m] = svf_make(fminf(100 * powf(20, rng_unit(r)), 0.45f * rate), rng_range(r, 15, 40), rate);
        pane_gain[m] = rng_range(r, 0.4f, 1.0f);
    }
    int fade_at = (int)(fail * rate), fade = (int)(0.001f * rate) + 1;
    for (int n = 0; n < fade_at + fade && n < frames; ++n)
    {
        float in = n == 0 ? 1.0f : 0, ring = 0;
        for (int m = 0; m < PANE_MODES; ++m) { svf_run(&pane[m], in); ring += pane_gain[m] * pane[m].band; }
        float gate = n < fade_at ? 1 : 1 - (n - fade_at) / (float)fade;
        out[n] += 0.18f * amp * gate * ring;
    }
    add_thump(out, frames, fade_at, rate, 600, 230, 0.012f, 0.3f * amp);

    /* The fragments' modes, low to high, irregularly spaced. */
    float top_hz = 11000, freq[POOL];
    for (int m = 0; m < POOL; ++m) freq[m] = 600 * powf(top_hz / 600, rng_unit(r));
    for (int i = 1; i < POOL; ++i)
        for (int j = i; j > 0 && freq[j - 1] > freq[j]; --j) { float x = freq[j]; freq[j] = freq[j - 1]; freq[j - 1] = x; }

    /* The crack front, each crack ringing two to six fragments. */
    int count = avalanche(r, fail, window, size, list);
    /* A large break also excites the bright edge of a fragment. Use the
       existing collision times, preserving the separated heavy clatter;
       another cloud of little events would bring back the sandy wash. */
    rng_t edge_rng = rng_seed(r->state ^ 0x6c8e9cf5u);
    for (int i = 0; i < count; ++i)
    {
        int at = (int)(list[i].t * rate);
        float onset = expf(-(list[i].t - fail) / 0.012f);
        float level = powf(list[i].energy, 0.8f) * (1 + 0.35f * onset);
        click(cracks, length, at, rate, r, level);
        float centre = (1 - powf(list[i].energy, 0.3f) + rng_range(r, -0.15f, 0.15f)) * (POOL - 1);
        for (int k = (int)rng_range(r, 2, 6.99f); k > 0; --k)
        {
            int m = (int)(centre + rng_range(r, -3, 3));
            m = m < 0 ? 0 : m >= POOL ? POOL - 1 : m;
            click(inputs + (size_t)m * length, length, at, rate, r, level);
        }
        if (list[i].energy > 0.2f)
        {
            int edge = POOL - 1 - i % 3;
            click(inputs + (size_t)edge * length, length, at, rate, &edge_rng, 0.55f * level);
        }
    }

    /* A few substantial pieces keep knocking as the pane comes apart. Their
       lower modes and separated impacts carry the break into the landings;
       a dense high-frequency stream here sounds like glass being ground up. */
    for (float t = fail + 0.005f;;)
    {
        float since = t - fail, per_second = 260 * expf(-since / 0.08f) + 45 * expf(-since / 0.25f) + 10 * expf(-since / 0.5f);
        t += -logf(fmaxf(rng_unit(r), 1e-6f)) / per_second;
        if (t > fail + KNOCKING) break;
        int at = (int)(t * rate);
        /* Early on the pieces are bigger and hit harder. */
        float u = rng_unit(r), energy = (0.08f + 0.65f * u * u) * (1 + expf(-since / 0.1f));
        float level = powf(fminf(energy, 1), 0.8f);
        click(cracks, length, at, rate, r, 0.5f * level);
        float centre = (1 - powf(energy, 0.3f) + rng_range(r, -0.2f, 0.2f)) * (POOL - 1);
        for (int k = (int)rng_range(r, 2, 4.99f); k > 0; --k)
        {
            int m = (int)(centre + rng_range(r, -3, 3));
            m = m < 0 ? 0 : m >= POOL ? POOL - 1 : m;
            click(inputs + (size_t)m * length, length, at, rate, r, level);
        }
        if (energy > 0.2f)
        {
            int edge = POOL - 1 - at % 3;
            click(inputs + (size_t)edge * length, length, at, rate, &edge_rng, 0.4f * level);
        }
    }

    /* The cracks themselves, their lowest end taken off. */
    float keep = expf(-2 * SYNTH_PI * 300 / rate), low = 0;
    for (int n = 0; n < length; ++n)
    {
        low = cracks[n] + (low - cracks[n]) * keep;
        out[n] += 0.35f * amp * (cracks[n] - low);
    }
    /* Larger fragments retain a short lower clink past each crack. */
    for (int m = 0; m < POOL; ++m)
    {
        float physical_q = rng_range(r, 40, 110);
        float decay = physical_q / (SYNTH_PI * freq[m]);
        float weight = fminf((physical_q / 45) * (5000 / freq[m]), 4) * rng_range(r, 0.4f, 1.0f);
        /* Keep the same physical modes at each rate; omit modes outside its
           bandwidth and compensate the filter's bilinear decay warping. */
        if (freq[m] >= 0.45f * rate) continue;
        float q = fmaxf(1.5f, 0.5f * rate * sinf(2 * SYNTH_PI * freq[m] / rate) * decay);
        svf_t mode = svf_make(freq[m], q, rate);
        int ring = length + (int)(9.2f * decay * rate);
        const float *in = inputs + (size_t)m * length;
        for (int n = 0; n < ring && n < frames; ++n)
        {
            svf_run(&mode, n < length ? in[n] : 0);
            out[n] += 0.25f * amp * weight * mode.band * mode.k * 20;
        }
    }
    free(cracks); free(inputs); free(list);
}

void Fracture_Render(int kind, unsigned seed, int rate, float *out, int frames)
{
    rng_t r = rng_seed(seed);
    switch (kind)
    {
    case IMPACT_GLASS_FRACTURE:
        fracture(out, frames, rate, &r, 0.7f, 0.7f);
        Glass_Shards(out, frames, rate, seed, 18, 44, 88, 0.15f, 0.9f, 0.72f);
        Glass_Debris(out, frames, rate, seed, 0.8f, 5, 0.08f);
        break;
    case IMPACT_GLASS_FRACTURE_BIG:
        fracture(out, frames, rate, &r, 1.0f, 0.7f);
        Glass_Shards(out, frames, rate, seed, 26, 48, 112, 0.18f, 0.9f, 0.72f);
        Glass_Debris(out, frames, rate, seed, 0.9f, 7, 0.08f);
        break;
    }
}
