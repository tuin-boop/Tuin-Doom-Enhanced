/* Footsteps and splashes. A step carries a heavy man in combat boots: dense
 * filtered noise for the impact and his weight, a quiet tone under it, the
 * sole settling into its tail, colored by the floor - a short response on
 * metal, a knock on wood, grit on stone, a crunch on grass - and driven so
 * it punches. Running lands on the ball of the foot, harder. A
 * moment after each step his kit settles: buckles, a slung gun, armor. In
 * water a step is a splash with droplets falling back; in nukage, slime and
 * blood a thick gloop and the foot pulling out. Kept short: they play all
 * the time. */
#include <math.h>
#include "impact_synth.h"
#include "synth_kit.h"

/* Lowpassed noise, a soft muffled push. */
static void hush(float *out, int frames, int at, int rate, rng_t *r, float cutoff, float seconds, float amp)
{
    svf_t low = svf_make(cutoff, 0.7f, rate);
    int length = (int)(seconds * 6 * rate);
    for (int n = 0; n < length && at + n < frames; ++n)
    {
        float t = n / (float)rate, env = (1 - expf(-t / 0.003f)) * expf(-t / seconds);
        svf_run(&low, rng_noise(r));
        if (at + n >= 0) out[at + n] += amp * env * low.low * 1.5f;
    }
}

/* One contact of the boot: noise between lo and hi, its weight around
   centre, rising in attack seconds and dying over tau. */
static void contact(float *out, int frames, int at, int rate, rng_t *r, float lo, float centre, float hi,
                    float attack, float tau, float amp)
{
    svf_t high = svf_make(lo, 0.6f, rate), low = svf_make(hi, 0.6f, rate), peak = svf_make(centre, 0.9f, rate);
    int length = (int)(tau * 7 * rate);
    for (int n = 0; n < length && at + n < frames; ++n)
    {
        float t = n / (float)rate, env = (1 - expf(-t / attack)) * expf(-t / tau);
        svf_run(&high, rng_noise(r));
        svf_run(&low, high.high);
        svf_run(&peak, low.low);
        if (at + n >= 0) out[at + n] += amp * env * (0.5f * low.low + peak.band);
    }
}

/* A scrape that catches and slips: band noise, its level jumping at
   uneven moments, so it rasps without buzzing at any pitch. */
static void scrape(float *out, int frames, int at, int rate, rng_t *r, float centre, float seconds, float amp)
{
    svf_t band = svf_make(centre, 0.7f, rate);
    int length = (int)(seconds * rate), next = 0;
    float level = 1;
    for (int n = 0; n < length && at + n < frames; ++n)
    {
        float x = n / (float)length, env = sinf(SYNTH_PI * x) * (1 - x);
        if (n >= next) { level = 0.3f + 0.7f * rng_unit(r); next = n + 1 + (int)(rng_range(r, 0.002f, 0.009f) * rate); }
        svf_run(&band, rng_noise(r));
        if (at + n >= 0) out[at + n] += amp * env * level * band.band;
    }
}

/* Grit underfoot: grains of sand and chips of stone, each a tick of plain
   noise well under a millisecond, too short and too broad to ring. */
static void grit(float *out, int frames, int at, int rate, rng_t *r, int count, float spread, float amp)
{
    for (int k = 0; k < count; ++k)
    {
        svf_t high = svf_make(rng_range(r, 900, 1600), 0.5f, rate);
        int start = at + (int)(rng_range(r, 0, spread) * rate), length = (int)(rng_range(r, 0.0003f, 0.0009f) * rate) + 1;
        float a = amp * rng_range(r, 0.3f, 1);
        for (int n = 0; n < length + 4 && start + n < frames; ++n)
        {
            svf_run(&high, n < length ? rng_noise(r) * (1 - n / (float)length) : 0);
            if (start + n >= 0) out[start + n] += a * high.high;
        }
    }
}

/* How each floor takes a boot, over the same heavy body: where the heel's
   impact sits, and what the floor adds. Stone is dense and dead: a dry
   sole snap and grit, nothing rings. Metal rests on concrete or earth:
   the backing stops it flexing and booming like a suspended deck. The
   rubber sole gives it a dull push and only a short, faint metallic edge.
   Fixed pitches per floor, nudged a little, so it stays one floor. Fades
   are the whole audible tail. */
typedef struct { float centre, hi; } floor_voice_t;
static const floor_voice_t voices[4] = {
    { 350, 2000 },  /* stone */
    { 350, 2000 },  /* metal: the same boot; the plate adds the rest */
    { 320, 1800 },  /* wood */
    { 240, 900 },   /* soft */
};

/* A supported metal surface: pitch, strength, audible fade. The backing
   strongly suppresses low panel modes and damps the surface vibration
   within 6-18 ms. The boot supplies the weight, without a hollow ring. */
static const float plate[6][3] = {
    { 237, 0.02f, 0.012f }, { 412, 0.03f, 0.014f },
    { 689, 0.3f, 0.018f }, { 1130, 0.35f, 0.014f },
    { 1847, 0.25f, 0.01f }, { 2930, 0.15f, 0.006f },
};

/* One plate mode struck by the squashing sole: it swells over the contact
   instead of starting at full swing, so nothing clicks. */
static void plate_mode(float *out, int frames, int at, int rate, float freq, float amp, float seconds,
                       float squash, float phase)
{
    if (freq >= 0.47f * rate || at >= frames) return;
    float w = 2 * SYNTH_PI * freq / rate;
    int length = (int)(seconds * rate * 9.2f);  /* down 80 dB */
    for (int n = 0; n < length && at + n < frames; ++n)
    {
        float t = n / (float)rate, env = (1 - expf(-t / squash)) * expf(-t / seconds);
        if (at + n >= 0) out[at + n] += amp * env * sinf(w * n + phase);
    }
}

/* A rubber sole on the plate: a dull thup of the sole itself, then the
   modes, each weaker the higher it sits above what a contact of squash
   seconds can reach. Harder steps squash shorter and reach higher. */
static void strike_plate(float *out, int frames, int at, int rate, rng_t *r, float nudge, float squash, float amp)
{
    float reach = 1 / (SYNTH_PI * squash);
    contact(out, frames, at, rate, r, 200, 550, 1400, squash, 0.012f / 3.5f, 0.5f * amp);
    for (int m = 0; m < 6; ++m)
    {
        float f = plate[m][0] * nudge, over = f / reach;
        plate_mode(out, frames, at, rate, f, plate[m][1] * amp * rng_range(r, 0.8f, 1.15f) / (1 + over * over),
                   plate[m][2] / 3.5f, squash, rng_range(r, 0, 6.28f));
    }
}

static int metal_style = STEP_METAL_CURRENT;
void Step_SetMetalStyle(int style)
{
    metal_style = style >= STEP_METAL_CURRENT && style <= STEP_METAL_TREAD ? style : STEP_METAL_CURRENT;
}

/* Smooth steel over solid ground: the rubber presses down, then briefly
   grips and releases as the foot rolls forward. All broad noise, with no
   ringing surface modes. The common boot already carries the weight. */
static void metal_friction(float *out, int frames, int rate, rng_t *r, int run, int second)
{
    contact(out, frames, 0, rate, r, 180, 480, 1200, 0.0025f, 0.004f, run ? 0.45f : 0.35f);
    int release = second + (int)(rng_range(r, 0.002f, 0.006f) * rate);
    scrape(out, frames, release, rate, r, rng_range(r, 1100, 1700),
           rng_range(r, run ? 0.025f : 0.035f, run ? 0.035f : 0.05f), run ? 0.42f : 0.32f);
}

/* A firm heel and successive rubber tread lugs seating on supported steel.
   Broad, damped contacts carry the texture; nothing excites a panel mode. */
static void metal_tread(float *out, int frames, int rate, rng_t *r, int run, int second)
{
    float tint = rng_range(r, 0.94f, 1.06f);
    int count = run ? 3 : 4;
    float spacing = run ? 0.003f : 0.008f;
    int start = second - (int)((run ? 0.002f : 0.004f) * rate);
    if (start < 0) start = 0;

    contact(out, frames, 0, rate, r, 300, 900 * tint, 2000,
            run ? 0.0010f : 0.0014f, 0.0030f, run ? 0.8f : 0.65f);

    /* Soft, broad lug contacts: walking rolls through them, while running
       compresses them into the sole-settle contact. */
    for (int k = 0; k < count; ++k)
    {
        float jitter = rng_range(r, -0.0006f, 0.0006f);
        int at = start + (int)((k * spacing + jitter) * rate);
        float centre = rng_range(r, 650, 950) * tint;
        float amp = (run ? 0.58f : 0.68f) * rng_range(r, 0.88f, 1.12f);
        amp *= 1.0f - 0.13f * k;
        if (at < 0) at = 0;
        contact(out, frames, at, rate, r, 350, centre, 1800,
                0.0014f, run ? 0.0024f : 0.0028f, amp);
    }
}

static void floor_color(int floor, int run, float *out, int frames, int rate, rng_t *r, int second)
{
    float nudge = rng_range(r, 0.97f, 1.03f);
    switch (floor)
    {
    case IMPACT_STEP_STONE:
        contact(out, frames, 0, rate, r, 1500, 2500, 4000, 0.0005f, 0.005f / 3.5f, 0.45f);  /* sole snap */
        grit(out, frames, (int)(0.003f * rate), rate, r, (int)rng_range(r, 2, 5.99f), 0.04f, 0.25f);
        break;
    case IMPACT_STEP_METAL:
        /* Rubber spreads the load over milliseconds. The supported metal
           gives a short surface response, with no hollow deck ring.
           Running is firmer; the settling sole is softer again. */
        strike_plate(out, frames, 0, rate, r, nudge, run ? 0.0009f : 0.0012f, run ? 1.5f : 1.3f);
        strike_plate(out, frames, second, rate, r, nudge, 0.0018f, 0.35f);
        break;
    case IMPACT_STEP_WOOD:
        add_partial(out, frames, 0, rate, 135 * nudge, 0.5f, 0.05f, 0);
        add_partial(out, frames, 0, rate, 290 * nudge, 0.22f, 0.03f, 0);
        break;
    case IMPACT_STEP_SOFT:
        grit(out, frames, 0, rate, r, (int)rng_range(r, 2, 6.99f), 0.05f, 0.2f);  /* dirt and grass crunching */
        break;
    }
}

/* What the floor adds rides on top of the driven boot: room for it. */
#define STEP_HEADROOM 0.7f

/* Bring the boot itself to one peak, so every step and every floor hits
   with the same body; what the floor adds goes on top. */
static void level(float *out, int frames)
{
    float peak = 1e-6f;
    for (int n = 0; n < frames; ++n) peak = fmaxf(peak, fabsf(out[n]));
    for (int n = 0; n < frames; ++n) out[n] *= 1.1f / peak;
}

/* Round the boot's peaks off, so it is dense and punches and its tail comes
   up under it. The floor's ring and grit stay clean, outside it. */
static void drive(float *out, int frames, float amount, float gain)
{
    float norm = gain / tanhf(amount);
    for (int n = 0; n < frames; ++n) out[n] = norm * tanhf(amount * out[n]);
}

/* One footfall, "THUD-um": the first contact with the body's weight under
   it as low noise and a quiet tone, and the second, darker and softer,
   landing in its tail. Walking, the heel lands and the sole settles 35-55
   ms later; running, the two merge into one broad hit and the foot pushes
   off with a scrape. Everything varies a little, so it stays one boot. */
static void footfall(int floor, int run, float *out, int frames, int rate, rng_t *r)
{
    const floor_voice_t *v = &voices[floor - IMPACT_STEP_STONE];
    float vary = rng_range(r, 0.9f, 1.1f), bright = run ? 1.2f : 1;
    int second = (int)(rng_range(r, run ? 0.010f : 0.035f, run ? 0.025f : 0.055f) * rate);
    contact(out, frames, 0, rate, r, 150, v->centre * bright, v->hi * bright, 0.0015f, 0.019f * vary, 1);
    contact(out, frames, 0, rate, r, 55, 150, 400, 0.002f, 0.03f * vary, 1.0f);   /* the body */
    add_thump(out, frames, 0, rate, 90, 80, 0.03f * vary, 0.28f);
    contact(out, frames, second, rate, r, 120, 0.75f * v->centre, 0.7f * v->hi, 0.003f, 0.017f * vary, run ? 0.6f : 0.5f);
    level(out, frames);
    drive(out, frames, run ? 2.4f : 1.8f, 1);
    if (floor == IMPACT_STEP_METAL && metal_style != STEP_METAL_CURRENT)
    {
        /* The two auditions share the same boot and final gain. Their
           surface details use a copy so changing the model cannot change
           the body's level. Each supplies its own release or tread roll. */
        rng_t surface = *r;
        if (metal_style == STEP_METAL_FRICTION) metal_friction(out, frames, rate, &surface, run, second);
        else metal_tread(out, frames, rate, &surface, run, second);
    }
    else
    {
        floor_color(floor, run, out, frames, rate, r, second);
        if (run) scrape(out, frames, (int)(rng_range(r, 0.04f, 0.07f) * rate), rate, r, rng_range(r, 900, 1400), 0.06f, 0.22f);
        else scrape(out, frames, second, rate, r, rng_range(r, 1200, 2000), rng_range(r, 0.05f, 0.1f), 0.09f);
    }
    float gain = STEP_HEADROOM * rng_range(r, 0.89f, 1.12f);
    for (int n = 0; n < frames; ++n) out[n] *= gain;
}

/* The kit after a step: the body's jolt shaking it 15-35 ms after the foot.
   Mostly a padded bump - pouches, magazines, webbing - then a hard click
   and now and then a darker, weaker one or two as loose hardware settles.
   What it is shows in the balance and in a few fixed ringing modes, the
   same in every take so it stays the same kit; only how hard it's struck
   and when its parts collide change. Running swings it harder: more of the
   bump and the collisions, the ringing held back. Times are the whole
   audible fade. */
typedef struct {
    float bump_lo, bump_centre, bump_hi, bump_fade, bump_amp;
    float click_centre, click_fade, click_amp;
    float mode[4], mode_fade[4], mode_amp[4];
    int settles, rounds;
} gear_voice_t;

static const gear_voice_t kits[4] = {
    /* buckles: small sharp contacts, a little metal */
    { 180, 500, 1300, 0.03f, 1, 2000, 0.006f, 0.5f,
      { 740, 1210, 1970, 3130 }, { 0.02f, 0.017f, 0.009f, 0.006f }, { 0.12f, 0.1f, 0.07f, 0.05f }, 2, 0 },
    /* slung gun: a darker knock, one or two loose contacts */
    { 150, 350, 900, 0.04f, 1.2f, 1300, 0.007f, 0.4f,
      { 520, 850, 1380, 2190 }, { 0.022f, 0.018f, 0.01f, 0.007f }, { 0.12f, 0.09f, 0.06f, 0.04f }, 2, 0 },
    /* heavy weapon: a broad bump, low restrained ringing, rounds rattling */
    { 120, 300, 1300, 0.05f, 1.3f, 1000, 0.008f, 0.32f,
      { 310, 540, 890, 0 }, { 0.025f, 0.02f, 0.012f, 0 }, { 0.1f, 0.07f, 0.05f, 0 }, 1, 1 },
    /* armor: a short hard click over a dull, damped knock */
    { 150, 280, 700, 0.02f, 0.8f, 2200, 0.004f, 0.7f,
      { 620, 0, 0, 0 }, { 0.005f, 0, 0, 0 }, { 0.04f, 0, 0, 0 }, 1, 0 },
};

static float gear_ring = 1;
void Step_SetGearRing(float amount) { gear_ring = amount; }

/* A hard contact of the kit: a click of bright noise and the kit's modes. */
static void kit_contact(const gear_voice_t *v, float *out, int frames, int at, int rate, rng_t *r,
                        float dark, float amp, float ring)
{
    float c = v->click_centre * dark;
    contact(out, frames, at, rate, r, 0.45f * c, c, 1.75f * c, 0.0005f, v->click_fade / 3.5f, v->click_amp * amp);
    for (int m = 0; m < 4 && v->mode[m] > 0; ++m)
        add_partial(out, frames, at, rate, v->mode[m] * rng_range(r, 0.99f, 1.01f),
                    v->mode_amp[m] * amp * ring * rng_range(r, 0.7f, 1.2f), v->mode_fade[m] / 4, rng_range(r, 0, 6.28f));
}

static void gear(int kind, float *out, int frames, int rate, rng_t *r)
{
    int hard = kind >= IMPACT_GEAR_LIGHT_RUN;
    const gear_voice_t *v = &kits[(kind - IMPACT_GEAR_LIGHT) % 4];
    int at = (int)(rng_range(r, 0.015f, 0.035f) * rate);
    float ring = gear_ring * (hard ? 0.6f : 1), u = rng_unit(r);
    contact(out, frames, at, rate, r, v->bump_lo, v->bump_centre, v->bump_hi, 0.001f,
            v->bump_fade * (hard ? 1.2f : 1) / 3.5f * rng_range(r, 0.9f, 1.1f), v->bump_amp);
    kit_contact(v, out, frames, at + (int)(rng_range(r, 0, 0.004f) * rate), rate, r, 1, rng_range(r, 0.8f, 1), ring);
    /* Loose parts settling, each later, darker and weaker; running, a short rattle now and then. */
    int settles = hard ? (u < 0.1f ? 0 : u < 0.6f ? 1 : 2) : (u < 0.4f ? 0 : u < 0.85f ? 1 : 2);
    if (settles > v->settles) settles = v->settles;
    float strength = hard ? 0.6f : 0.5f;
    for (int k = 0; k < settles; ++k, strength *= hard ? 0.6f : 0.5f)
    {
        at += (int)(rng_range(r, k ? 0.008f : 0.012f, k ? 0.02f : 0.03f) * rate);
        kit_contact(v, out, frames, at, rate, r, 0.7f, strength, ring);
    }
    if (hard && rng_unit(r) < 0.3f)
        for (int k = 0; k < 2; ++k, strength *= 0.6f)
        {
            at += (int)(rng_range(r, 0.006f, 0.012f) * rate);
            kit_contact(v, out, frames, at, rate, r, 0.8f, strength, ring);
        }
    if (v->rounds)
        add_crackle(out, frames, rate, r, at / (float)rate + 0.005f, at / (float)rate + (hard ? 0.08f : 0.04f),
                    hard ? 400 : 200, 0.03f, 0.12f, 1500, 3500);
    /* Every take at one loudness, struck a little harder or softer. */
    double sum = 0;
    int window = (int)(0.12f * rate);
    for (int n = 0; n < window && n < frames; ++n) sum += out[n] * out[n];
    float gain = 0.12f / (float)sqrt(sum / window + 1e-12) * rng_range(r, 0.89f, 1.12f);
    for (int n = 0; n < frames; ++n) out[n] *= gain;
}

/* Water: a burst of droplets in a band, a plunk under it, a few bloops. */
static void splash(float *out, int frames, int rate, rng_t *r)
{
    svf_t band = svf_make(rng_range(r, 1200, 1900), 0.6f, rate);
    int length = (int)(0.18f * rate), grain = (int)(0.002f * rate) + 1;
    float level = 1;
    for (int n = 0; n < length && n < frames; ++n)
    {
        float t = n / (float)rate, env = (1 - expf(-t / 0.003f)) * expf(-t / 0.05f);
        if (n % grain == 0) { float u = rng_unit(r); level = 0.3f + 0.7f * u * u; }
        svf_run(&band, rng_noise(r));
        out[n] += 0.55f * env * level * band.band * 1.5f;
    }
    add_thump(out, frames, 0, rate, 220, 120, 0.02f, 0.25f);
    for (int k = (int)rng_range(r, 2, 4.99f); k > 0; --k)
        add_bubble(out, frames, (int)(rng_range(r, 0.03f, 0.18f) * rate), rate, rng_range(r, 700, 1800),
                   rng_range(r, 0.12f, 0.28f), 0.1f, 1);
}

/* Thick liquid: a low gloop, a bloop or two, and the foot pulling out. */
static void gloop(float *out, int frames, int rate, rng_t *r)
{
    hush(out, frames, 0, rate, r, 700, 0.06f, 0.55f);
    for (int k = (int)rng_range(r, 1, 2.99f); k > 0; --k)
        add_bubble(out, frames, (int)(rng_range(r, 0.01f, 0.08f) * rate), rate, rng_range(r, 150, 350),
                   0.35f, 0.05f, 1.3f);
    hush(out, frames, (int)(rng_range(r, 0.12f, 0.2f) * rate), rate, r, 500, 0.04f, 0.25f);
}

void Step_Render(int kind, unsigned seed, int rate, float *out, int frames)
{
    rng_t r = rng_seed(seed);
    if (kind == IMPACT_SPLASH_WATER) splash(out, frames, rate, &r);
    else if (kind == IMPACT_SPLASH_THICK) gloop(out, frames, rate, &r);
    else if (kind >= IMPACT_GEAR_LIGHT) gear(kind, out, frames, rate, &r);
    else if (kind >= IMPACT_RUN_STONE) footfall(kind - IMPACT_RUN_STONE + IMPACT_STEP_STONE, 1, out, frames, rate, &r);
    else footfall(kind, 0, out, frames, rate, &r);
}

/* Running steps land harder and the kit swings more; a landing more still.
   The footfall is the loud part: on stone the weapon's kit sits 16 dB under
   a walking step and 10 under a running one, armor adding about 2 dB. */
#define KIT_LEVEL 0.146f
int Step_Layers(int floor, float force, int armor, int heft, impact_layer_t *layers)
{
    int count = 0, wet = floor == IMPACT_SPLASH_WATER || floor == IMPACT_SPLASH_THICK;
    float swing = 0.33f + 0.67f * fminf(force, 1) + 0.4f * fmaxf(force - 1, 0);
    float hit = (0.146f + 0.043f * fminf(force, 1) + 0.065f * fmaxf(force - 1, 0)) / STEP_HEADROOM;
    if (wet) layers[count++] = (impact_layer_t){ floor, 0.5f + 0.15f * fminf(force, 2) };
    else layers[count++] = (impact_layer_t){ force >= 0.5f ? floor - IMPACT_STEP_STONE + IMPACT_RUN_STONE : floor,
                                             hit };
    int hard = force >= 0.5f ? IMPACT_GEAR_LIGHT_RUN - IMPACT_GEAR_LIGHT : 0;
    heft = heft < 0 ? 0 : heft > 2 ? 2 : heft;
    layers[count++] = (impact_layer_t){ IMPACT_GEAR_LIGHT + heft + hard, KIT_LEVEL * swing };
    if (armor > 0) layers[count++] = (impact_layer_t){ IMPACT_GEAR_ARMOR + hard, (armor > 1 ? 0.7f : 0.5f) * KIT_LEVEL * swing };
    return count;
}
