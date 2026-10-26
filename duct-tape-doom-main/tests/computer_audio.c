/* The optional computer bed and activity keep their native loop lengths,
   render safely into a shared buffer, and obey the mixer's runtime switch. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../platform/audio/computer_synth.h"
#include "../platform/audio/made_bank.h"
#include "../platform/audio/ambience_voices.h"
#include "../platform/audio/resample.h"

enum { GUARD = 32 };
typedef void (*render_fn)(unsigned seed, int rate, float *out, int frames);
static const char *current_part = "computer bank";
static int current_rate, current_frames;
static unsigned current_seed;

static void check(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "%s: %s, rate %d, frames %d, seed %u\n", message,
                current_part, current_rate, current_frames, current_seed);
        exit(1);
    }
}

static double energy(const float *samples, int first, int end)
{
    double sum = 0;
    for (int n = first; n < end; ++n) sum += (double)samples[n] * samples[n];
    return sum;
}

static float prefix(int pattern, int frame)
{
    if (pattern == 1) return 0.125f;
    if (pattern == 2) return 0.08f + 0.07f * sinf(frame * 0.017f);
    return 0;
}

/* Even an empty rendering points at writable guards, so a stray first or
   last sample write is caught without depending on a memory checker. */
static float *render_guarded(render_fn render, unsigned seed, int rate, int frames, int pattern)
{
    const float canary = 5731.125f;
    float *storage = malloc((size_t)(frames + 2 * GUARD) * sizeof(float));
    current_rate = rate; current_frames = frames; current_seed = seed;
    check(storage != NULL, "Unable to allocate a guarded computer rendering");
    for (int n = 0; n < frames + 2 * GUARD; ++n) storage[n] = canary;
    float *out = storage + GUARD;
    for (int n = 0; n < frames; ++n) out[n] = prefix(pattern, n);
    render(seed, rate, out, frames);
    for (int n = 0; n < GUARD; ++n)
        check(storage[n] == canary && storage[GUARD + frames + n] == canary,
              "Computer synthesis wrote outside its requested frame range");
    for (int n = 0; n < frames; ++n)
        check(isfinite(out[n]), "Computer synthesis produced a nonfinite sample");
    return storage;
}

static void check_synth(render_fn render, const char *name, int continuous)
{
    static const int rates[] = { 8000, 11025 };
    static const unsigned seeds[] = { 0, 1, 0x93d0f25bu };
    static const int short_frames[] = { 0, 1, 2, 3, 7, 31, 80 };
    current_part = name;
    for (size_t r = 0; r < sizeof(rates) / sizeof(rates[0]); ++r)
    {
        int rate = rates[r], frames = 32 * rate;
        for (size_t s = 0; s < sizeof(seeds) / sizeof(seeds[0]); ++s)
            for (size_t n = 0; n < sizeof(short_frames) / sizeof(short_frames[0]); ++n)
            {
                float *short_render = render_guarded(render, seeds[s], rate, short_frames[n], 0);
                free(short_render);
            }

        unsigned seed = 0x93d0f25bu;
        float *a = render_guarded(render, seed, rate, frames, 0), *samples = a + GUARD;
        float *b = render_guarded(render, seed, rate, frames, 0);
        check(memcmp(samples, b + GUARD, (size_t)frames * sizeof(float)) == 0,
              "One seed must produce repeatable computer audio");
        free(b);
        b = render_guarded(render, seed ^ 0x6c8e9cf5u, rate, frames, 0);
        double total = energy(samples, 0, frames), difference = 0, peak = 0, dc = 0;
        for (int n = 0; n < frames; ++n)
        {
            double delta = (double)samples[n] - b[GUARD + n];
            difference += delta * delta;
            dc += samples[n];
            if (fabs(samples[n]) > peak) peak = fabs(samples[n]);
        }
        check(total > 1e-8 && peak > 1e-5, "A computer rendering must contain audible energy");
        check(peak < 128, "Computer synthesis must remain bounded before quantisation");
        check(difference > total * 1e-5, "Different seeds must produce different computer takes");
        check(fabs(dc / frames) < fmax(1e-5, sqrt(total / frames) * 0.2),
              "Computer synthesis must not accumulate a large DC offset");
        if (continuous)
        {
            int window = rate / 4;
            for (int first = 0; first + window <= frames; first += window)
                check(energy(samples, first, first + window) > total / frames * window * 0.0001,
                      "The computer body must remain present between activity events");
        }
        free(b);
        for (int pattern = 1; pattern <= 2; ++pattern)
        {
            float *mixed = render_guarded(render, seed, rate, frames, pattern);
            for (int n = 0; n < frames; ++n)
            {
                double expected = (double)samples[n] + prefix(pattern, n);
                check(fabs(mixed[GUARD + n] - expected) < 1e-4 * (1 + fabs(expected)),
                      "Computer synthesis must add to audio already in the buffer");
            }
            free(mixed);
        }
        free(a);
    }
}

static double bank_energy(const made_sample_t *sample, int first, int end)
{
    double sum = 0;
    for (int n = first; n < end; ++n)
    {
        double value = (sample->data[n] - 128) / 128.0;
        sum += value * value;
    }
    return sum;
}

static void check_native_sample(const made_sample_t *sample, int seconds)
{
    check(sample && sample->data, "The built computer bank must expose each sample");
    check(sample->rate == 11025 && sample->length == seconds * 11025,
          "Computer samples must keep their native 11.025 kHz duration");
    current_rate = sample->rate; current_frames = sample->length;
    check(bank_energy(sample, 0, sample->length) > sample->length * 1e-8,
          "Unsigned 8-bit bank quantisation must retain the computer sound");
}

static int compare_energy(const void *a, const void *b)
{
    double first = *(const double *)a, second = *(const double *)b;
    return (first > second) - (first < second);
}

/* HDD and relay events may stand above the body. Between them the activity
   should recede, rather than turning fine mechanical detail into an almost
   continuous series of foreground chimes. Measure the quantised production
   takes, including their loop overlap, so this also catches bank-level gain
   changes that make those gaps disappear. */
static void check_activity_gaps(const made_sample_t *activity, double body_mean_energy)
{
    int window = activity->rate / 4, count = activity->length / window;
    double *windows = malloc((size_t)count * sizeof(double));
    check(windows != NULL, "Unable to measure the computer activity gaps");
    for (int n = 0; n < count; ++n)
        windows[n] = bank_energy(activity, n * window, (n + 1) * window) / window;
    qsort(windows, (size_t)count, sizeof(double), compare_energy);
    double median = windows[count / 2];
    free(windows);
    check(median < body_mean_energy * 0.15 * 0.15,
          "Fine computer activity must leave quiet gaps beneath the continuous body");
}

static void check_bank(void)
{
    current_part = "computer bank"; current_rate = current_frames = 0; current_seed = 0;
    check(MadeBank_ComputerBody() == NULL && MadeBank_ComputerActivity(0) == NULL,
          "An unbuilt bank must not expose alternative computer samples");
    MadeBank_Build();

    const made_sample_t *body = MadeBank_ComputerBody();
    check_native_sample(body, 32);
    double total = bank_energy(body, 0, body->length), steps = 0;
    int window = body->rate / 4;
    for (int first = 0; first + window <= body->length; first += window)
        check(bank_energy(body, first, first + window) > total / body->length * window * 0.0001,
              "The quantised computer body must not contain silent gaps");
    for (int n = 1; n < body->length; ++n)
    {
        double step = (int)body->data[n] - body->data[n - 1];
        steps += step * step;
    }
    double seam = abs((int)body->data[0] - body->data[body->length - 1]);
    check(seam <= fmax(4, 8 * sqrt(steps / (body->length - 1))),
          "The computer body's loop boundary must not introduce a large click");

    static const int seconds[AMBIENCE_LAYERS] = { 32, 37, 43 };
    for (int layer = 0; layer < AMBIENCE_LAYERS; ++layer)
    {
        const made_sample_t *activity = MadeBank_ComputerActivity(layer);
        check_native_sample(activity, seconds[layer]);
        check_activity_gaps(activity, total / body->length);
        const made_sample_t *original = MadeBank_Loop(AMBIENT_COMPUTER, layer);
        check(original && original->data && original->rate == 11025 && original->length == 8 * 11025,
              "The original computer loops must retain their 8-second bank entries");
        for (int other = 0; other < layer; ++other)
        {
            const made_sample_t *earlier = MadeBank_ComputerActivity(other);
            int common = activity->length < earlier->length ? activity->length : earlier->length;
            double difference = 0;
            for (int n = 0; n < common; ++n)
            {
                double delta = ((int)activity->data[n] - earlier->data[n]) / 128.0;
                difference += delta * delta;
            }
            check(difference > fmax(1e-8, bank_energy(activity, 0, common) * 1e-5),
                  "Each activity layer must contain an independent take");
        }
    }
}

/* Measure the actual stereo mix and reverb send. Every other target stays
   zero, so the body can be heard and switched off independently of events. */
static void mix_measure(int frames, int rate, int mode, double result[3], double peak[3])
{
    float *stereo = calloc((size_t)frames * 2, sizeof(float));
    float *send = calloc((size_t)frames, sizeof(float));
    current_rate = rate; current_frames = frames;
    check(stereo && send, "Unable to allocate a computer mixer buffer");
    AmbienceVoices_Mix(stereo, send, frames, rate, mode, 1, 1);
    result[0] = result[1] = result[2] = 0;
    if (peak) peak[0] = peak[1] = peak[2] = 0;
    for (int n = 0; n < frames; ++n)
    {
        check(isfinite(stereo[2 * n]) && isfinite(stereo[2 * n + 1]) && isfinite(send[n]),
              "The computer mixer must produce finite output");
        result[0] += (double)stereo[2 * n] * stereo[2 * n];
        result[1] += (double)stereo[2 * n + 1] * stereo[2 * n + 1];
        result[2] += (double)send[n] * send[n];
        if (peak)
        {
            peak[0] = fmax(peak[0], fabs(stereo[2 * n]));
            peak[1] = fmax(peak[1], fabs(stereo[2 * n + 1]));
            peak[2] = fmax(peak[2], fabs(send[n]));
        }
    }
    free(stereo); free(send);
}

static void mix_energy(int frames, int rate, int mode, double result[3])
{
    mix_measure(frames, rate, mode, result, NULL);
}

static void check_switch(void)
{
    static const int rates[] = { 8000, 11025 };
    static const int modes[] = { RESAMPLE_ORIGINAL, RESAMPLE_CLEAN };
    const ambience_voice_t body = { 0.6f, 0.4f, 0.25f, 20000 };
    ambience_voice_t targets[AMBIENT_KINDS][AMBIENCE_LAYERS] = { 0 };
    Resample_Init();
    current_part = "computer mixer"; current_seed = 0;
    for (size_t r = 0; r < sizeof(rates) / sizeof(rates[0]); ++r)
        for (size_t m = 0; m < sizeof(modes) / sizeof(modes[0]); ++m)
        {
            int rate = rates[r], frames = rate / 4;
            double quiet[3], sounding[3], ignored[3];
            current_rate = rate; current_frames = frames;
            AmbienceVoices_Clear();
            AmbienceVoices_AlternativeComputer(0);
            AmbienceVoices_Set(targets);
            AmbienceVoices_SetComputerBody(&body);
            mix_energy(rate * 2 / 5, rate, modes[m], ignored);
            mix_energy(frames, rate, modes[m], quiet);
            for (int c = 0; c < 3; ++c)
                check(quiet[c] < 1e-9, "A body target must not leak through while the alternative is off");

            AmbienceVoices_AlternativeComputer(1);
            AmbienceVoices_SetComputerBody(&body);
            mix_energy(rate * 2 / 5, rate, modes[m], ignored);
            mix_energy(frames, rate, modes[m], sounding);
            for (int c = 0; c < 3; ++c)
                check(sounding[c] > frames * 1e-9,
                      "The alternative must play its body and reverb send with all activity targets zero");

            AmbienceVoices_AlternativeComputer(0);
            AmbienceVoices_SetComputerBody(&body);
            mix_energy(rate * 2 / 5, rate, modes[m], ignored);
            mix_energy(frames, rate, modes[m], quiet);
            for (int c = 0; c < 3; ++c)
                check(quiet[c] < sounding[c] * 0.0001 + 1e-10,
                      "Switching the alternative off must fade its body and reverb send to quiet");
        }
    AmbienceVoices_Clear();
}

/* A source can disappear while its palette is hidden. Its old gain must
   settle there, so revealing that palette does not replay a stale sound.
   Include the complete first transition in each quiet measurement. */
static void check_hidden_targets(void)
{
    const int rate = 11025, mode = RESAMPLE_CLEAN, transition = rate * 2 / 5;
    const ambience_voice_t audible = { 0.6f, 0.4f, 0.25f, 20000 };
    ambience_voice_t targets[AMBIENT_KINDS][AMBIENCE_LAYERS] = { 0 };
    double measured[3], peak[3];
    current_part = "hidden alternative computer targets";
    AmbienceVoices_Clear();
    AmbienceVoices_AlternativeComputer(1);
    AmbienceVoices_Set(targets);
    AmbienceVoices_SetComputerBody(&audible);
    mix_energy(transition, rate, mode, measured);
    mix_energy(rate / 4, rate, mode, measured);
    for (int c = 0; c < 3; ++c)
        check(measured[c] > rate / 4 * 1e-9, "The body must be audible before testing its hidden target");
    AmbienceVoices_AlternativeComputer(0);
    mix_energy(transition, rate, mode, measured);
    AmbienceVoices_SetComputerBody(NULL);
    AmbienceVoices_Set(targets);
    mix_energy(rate, rate, mode, measured);
    AmbienceVoices_AlternativeComputer(1);
    mix_measure(transition, rate, mode, measured, peak);
    for (int c = 0; c < 3; ++c)
        check(peak[c] < 1e-7, "Revealing a hidden body with no remaining source must not replay its old gain");

    current_part = "hidden original computer targets";
    AmbienceVoices_Clear();
    AmbienceVoices_AlternativeComputer(0);
    AmbienceVoices_SetComputerBody(NULL);
    for (int layer = 0; layer < AMBIENCE_LAYERS; ++layer)
        targets[AMBIENT_COMPUTER][layer] = audible;
    AmbienceVoices_Set(targets);
    /* Longer than the classic loop: its intermittent bleeps must be heard
       before their targets are cleared in the hidden palette. */
    mix_energy(9 * rate, rate, mode, measured);
    for (int c = 0; c < 3; ++c)
        check(measured[c] > 9 * rate * 1e-9, "The original computer must be audible before testing its hidden target");
    AmbienceVoices_AlternativeComputer(1);
    mix_energy(transition, rate, mode, measured);
    memset(targets, 0, sizeof(targets));
    AmbienceVoices_Set(targets);
    AmbienceVoices_SetComputerBody(NULL);
    mix_energy(rate, rate, mode, measured);
    AmbienceVoices_AlternativeComputer(0);
    mix_measure(transition, rate, mode, measured, peak);
    for (int c = 0; c < 3; ++c)
        check(peak[c] < 1e-7, "Revealing the original computer with no remaining source must not replay an old bleep");
    AmbienceVoices_Clear();
}

int main(void)
{
    check_synth(Computer_BodyRender, "computer body", 1);
    check_synth(Computer_ActivityRender, "computer activity", 0);
    check_bank();
    check_switch();
    check_hidden_targets();
    puts("Computer guarded/additive synthesis, native loop bank, continuous body and runtime switching passed.");
    return 0;
}
