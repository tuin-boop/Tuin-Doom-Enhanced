/* Glass must keep its sharp attack and a real settling tail at every
   supported render rate, then reach the mixer at its intended native rate. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../platform/audio/impact_synth.h"
#include "../platform/audio/made_bank.h"
#include "../platform/audio/ambience.h"

enum { GUARD = 32 };
static int current_kind = IMPACT_GLASS_TINKLE, current_rate, current_frames;
static unsigned current_seed;

static void check(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "%s: %s, rate %d, frames %d, seed %u\n", message,
                Impact_Name(current_kind), current_rate, current_frames, current_seed);
        exit(1);
    }
}

static double energy(const float *samples, int first, int end)
{
    double sum = 0;
    for (int n = first; n < end; ++n) sum += (double)samples[n] * samples[n];
    return sum;
}

/* Return an owned, guarded rendering. Zero frames still has writable guard
   memory on both sides, so an unconditional first-sample write is detected. */
static float prefix(int pattern, int frame)
{
    if (pattern == 1) return 0.125f;
    if (pattern == 2) return 0.08f + 0.07f * sinf(frame * 0.017f) + 0.03f * (frame % 17) / 16;
    return 0;
}

static float *render_pattern(int kind, unsigned seed, int rate, int frames, int pattern)
{
    const float canary = 5731.125f;
    float *storage = malloc((size_t)(frames + 2 * GUARD) * sizeof(float));
    current_kind = kind; current_rate = rate; current_frames = frames; current_seed = seed;
    check(storage != NULL, "Unable to allocate audio regression buffer");
    for (int n = 0; n < frames + 2 * GUARD; ++n) storage[n] = canary;
    float *out = storage + GUARD;
    for (int n = 0; n < frames; ++n) out[n] = prefix(pattern, n);
    Impact_Render(kind, seed, rate, out, frames);
    for (int n = 0; n < GUARD; ++n)
        check(storage[n] == canary && storage[GUARD + frames + n] == canary,
              "Glass rendering wrote outside its requested frame range");
    for (int n = 0; n < frames; ++n)
        check(isfinite(out[n]), "Glass rendering produced a nonfinite sample");
    return storage;
}

static float *render(int kind, unsigned seed, int rate, int frames)
{
    return render_pattern(kind, seed, rate, frames, 0);
}

static void check_additive(int kind)
{
    const int rate = 22050, frames = Impact_Length(kind, rate);
    const unsigned seed = 0x7a53fd92u;
    float *own = render(kind, seed, rate, frames);
    for (int pattern = 1; pattern <= 2; ++pattern)
    {
        float *mixed = render_pattern(kind, seed, rate, frames, pattern);
        for (int n = 0; n < frames; ++n)
        {
            double expected = (double)own[GUARD + n] + prefix(pattern, n);
            check(fabs(mixed[GUARD + n] - expected) < 1e-4 * (1 + fabs(expected)),
                  "Glass must add its effect without fading or replacing audio already in the buffer");
        }
        free(mixed);
    }
    free(own);
}

static void check_full(int kind, unsigned seed, int rate)
{
    int frames = Impact_Length(kind, rate);
    check(frames > 0, "A glass effect must have a positive duration");
    float *a = render(kind, seed, rate, frames), *sample = a + GUARD;
    float *b = render(kind, seed, rate, frames);
    check(memcmp(sample, b + GUARD, (size_t)frames * sizeof(float)) == 0,
          "Repeated glass renders must be identical for one seed");
    free(b);
    b = render(kind, seed ^ 0x6c8e9cf5u, rate, frames);
    current_seed = seed;
    double total = energy(sample, 0, frames), difference = 0, dc = 0, peak = 0;
    for (int n = 0; n < frames; ++n)
    {
        double delta = (double)sample[n] - b[GUARD + n];
        difference += delta * delta; dc += sample[n];
        if (fabs(sample[n]) > peak) peak = fabs(sample[n]);
    }
    check(total > 1e-8 && peak > 1e-4, "A complete glass effect must contain audible energy");
    check(difference > total * 0.01, "Different seeds must create meaningfully different glass takes");
    check(peak < 128, "Glass synthesis must remain bounded before soft clipping");
    double rms = sqrt(total / frames);
    check(fabs(dc / frames) < fmax(1e-5, rms * 0.05), "Glass audio must not carry excessive DC offset");
    if (kind != IMPACT_GLASS_TINKLE)
        check(energy(sample, 0, rate / 20) > total * 0.0001,
              "Breaking glass must start with the impact instead of a silent lead-in");
    if (kind >= IMPACT_GLASS_BREAK)
    {
        double late = energy(sample, (int)(0.8 * rate), frames);
        double minimum = kind == IMPACT_GLASS_BIG || kind == IMPACT_GLASS_FRACTURE_BIG ? 0.01 : 0.001;
        check(late > total * minimum, "Full glass breaks must retain separated landings beyond 0.8 seconds");
    }
    int tail_frames = rate / 20;
    double tail_rms = sqrt(energy(sample, frames - tail_frames, frames) / tail_frames);
    if (tail_rms >= rms * 0.3)
        fprintf(stderr, "Last 50 ms RMS / whole-sample RMS: %.5f (must be below 0.3)\n", tail_rms / rms);
    check(tail_rms < rms * 0.3, "Glass effects must settle to a quiet tail");
    check(fabs(sample[frames - 1]) < fmax(1e-4, peak * 0.005),
          "Glass effects must end quietly without a cut-off click");
    free(a); free(b);
}

static void check_bank(void)
{
    current_rate = 0; current_frames = 0; current_seed = 0;
    check(MadeBank_Impact(IMPACT_GLASS_BIG, 0) == NULL, "An unbuilt bank must not expose glass samples");
    MadeBank_Build();
    for (int kind = IMPACT_GLASS_TINKLE; kind <= IMPACT_GLASS_FRACTURE_BIG; ++kind)
    {
        current_kind = kind;
        check(MadeBank_Variants(kind) == MADE_VARIANTS, "Glass must retain the bank's seeded take count");
        for (int variant = 0; variant < MadeBank_Variants(kind); ++variant)
        {
            const made_sample_t *sample = MadeBank_Impact(kind, variant);
            check(sample && sample->data && sample->rate == 22050,
                  "Every glass bank take must reach the mixer at 22050 Hz");
            check(sample->length == Impact_Length(kind, 22050), "Glass bank data must keep its native duration");
            int nonzero = 0;
            for (int n = 0; n < sample->length; ++n) nonzero |= sample->data[n] != 128;
            check(nonzero, "Glass bank quantisation must not erase the effect");
              if(kind!=IMPACT_GLASS_TINKLE) {
                  int attack=(int)(.18f*sample->rate);double energy=0;
                  for(int n=0;n<attack;++n){double x=(sample->data[n]-128)/127.0;energy+=x*x;}
                  check(sqrt(energy/attack)>.065, "Glass attack must remain audible after shortening ringing");
              }
        }
    }
    const made_sample_t *step = MadeBank_Impact(IMPACT_STEP_STONE, 0);
    check(step && step->rate == 11025 && step->length == (int)(0.3f * 11025),
          "Footstep bank rate and duration must remain unchanged");
    check(MadeBank_Variants(IMPACT_STEP_STONE) == MADE_STEP_VARIANTS,
          "Footsteps must retain their larger variation bank");
    for (int kind = 0; kind < AMBIENT_KINDS; ++kind)
        for (int layer = 0; layer < AMBIENCE_LAYERS; ++layer)
        {
            const made_sample_t *sample = MadeBank_Loop(kind, layer);
            int seconds = kind == AMBIENT_WIND ? 32 : 8;
            check(sample && sample->rate == 11025 && sample->length == seconds * 11025,
                  "Ambient bank rates and loop durations must remain unchanged");
        }
    const made_sample_t *strike = MadeBank_Strike(0);
    check(strike && strike->rate == 11025 && strike->length == (int)(0.08f * 11025),
          "Lamp strike bank rate and duration must remain unchanged");
}

int main(void)
{
    static const int rates[] = { 8000, 11025, 22050, 44100 };
    static const unsigned seeds[] = { 0, 1, 0x93d0f25bu };
    static const float duration[] = { 0.9f, 0.7f, 1.8f, 2.1f, 1.8f, 2.1f };
    for (int kind = 0; kind < IMPACT_KINDS; ++kind)
    {
        current_kind = kind;
        int expected = kind >= IMPACT_GLASS_TINKLE && kind <= IMPACT_GLASS_FRACTURE_BIG ? 22050 : 11025;
        check(Impact_Rate(kind) == expected, "Only glass effects must change to a 22050 Hz native rate");
    }
    for (int kind = IMPACT_GLASS_TINKLE; kind <= IMPACT_GLASS_FRACTURE_BIG; ++kind)
        for (size_t r = 0; r < sizeof(rates) / sizeof(rates[0]); ++r)
        {
            int rate = rates[r]; current_kind = kind; current_rate = rate;
            check(Impact_Length(kind, rate) == (int)(duration[kind - IMPACT_GLASS_TINKLE] * rate),
                  "Glass effect lengths must preserve their intended real-time duration");
            const int short_frames[] = { 0, 1, 2, 3, 7, 31, rate / 1000, rate / 100 };
            for (size_t s = 0; s < sizeof(seeds) / sizeof(seeds[0]); ++s)
            {
                for (size_t n = 0; n < sizeof(short_frames) / sizeof(short_frames[0]); ++n)
                {
                    float *guarded = render(kind, seeds[s], rate, short_frames[n]);
                    free(guarded);
                }
                check_full(kind, seeds[s], rate);
            }
        }
    for (int kind = IMPACT_GLASS_TINKLE; kind <= IMPACT_GLASS_FRACTURE_BIG; ++kind) check_additive(kind);
    check_bank();
    puts("Glass audio guarded/additive renders, seeded variation, attack/tail energy and native bank rates passed.");
    return 0;
}
