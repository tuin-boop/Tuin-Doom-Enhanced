/* Plays the ambient loops: each voice reads its own take of its kind's loop
 * from the made bank, wrapping round its end, through the same two-stage
 * lowpass the effects use for muffling. A kind's layers differ by their
 * takes and where in them they start, not by pitch, which would beat. Gains
 * glide over a twentieth of a second. */
#include <math.h>
#include <string.h>
#include "ambience_voices.h"
#include "made_bank.h"
#include "resample.h"

#define GLIDE_SECONDS 0.05f
#define MUFFLE_GLIDE_SECONDS 0.08f
#define COMPUTER_FADE_SECONDS 0.12f
/* Split the capped computer presence before the common palette level. */
#define COMPUTER_BODY_SHARE 0.55f
#define COMPUTER_ACTIVITY_SHARE 0.83f
/* The original bleep playback level underplays a continuous machine texture.
   Raise only this palette by 8 dB, after the shared presence cap is applied. */
#define COMPUTER_PLAYBACK_GAIN 2.51188643f
#define PI_F 3.14159265f

typedef struct {
    double position;
    float left, right, send, cutoff;              /* where it is */
    float to_left, to_right, to_send, to_cutoff;  /* where it goes */
    float low[2];
    int started;
} voice_t;
static voice_t voices[AMBIENT_KINDS][AMBIENCE_LAYERS];
static voice_t computer_activity[AMBIENCE_LAYERS], computer_body;
static int alternative_computers;
static float computer_blend;

static void target(voice_t *v, const ambience_voice_t *t, float scale)
{
    v->to_left = scale * t->left; v->to_right = scale * t->right; v->to_send = scale * t->send;
    if (t->cutoff > 0) v->to_cutoff = t->cutoff;
}

void AmbienceVoices_Set(const ambience_voice_t targets[AMBIENT_KINDS][AMBIENCE_LAYERS])
{
    for (int k = 0; k < AMBIENT_KINDS; ++k)
        for (int l = 0; l < AMBIENCE_LAYERS; ++l)
        {
            voice_t *v = &voices[k][l];
            const ambience_voice_t *t = &targets[k][l];
            v->to_left = t->left; v->to_right = t->right; v->to_send = t->send;
            if (t->cutoff > 0) v->to_cutoff = t->cutoff;
            if (k == AMBIENT_COMPUTER) target(&computer_activity[l], t, COMPUTER_ACTIVITY_SHARE * COMPUTER_PLAYBACK_GAIN);
        }
}

void AmbienceVoices_SetComputerBody(const ambience_voice_t *body)
{
    ambience_voice_t silent = {0};
    target(&computer_body, body ? body : &silent, COMPUTER_BODY_SHARE * COMPUTER_PLAYBACK_GAIN);
}

void AmbienceVoices_AlternativeComputer(int enabled) { alternative_computers = enabled != 0; }

static void silence(voice_t *v)
{
    v->left = v->right = v->send = v->to_left = v->to_right = v->to_send = 0;
}

void AmbienceVoices_Clear(void)
{
    for (int k = 0; k < AMBIENT_KINDS; ++k)
        for (int l = 0; l < AMBIENCE_LAYERS; ++l)
        {
            silence(&voices[k][l]);
        }
    for (int l = 0; l < AMBIENCE_LAYERS; ++l) silence(&computer_activity[l]);
    silence(&computer_body);
}

/* The ambience is mixed at the loops' own rate, then the sum - left, right
   and reverb send - is read up to the output rate once, the way the chosen
   effect playback reads a sample. Four times fewer samples per voice, and a
   sinc reading three channels instead of every voice's. */
#define BUS 2048
static float bus[3][BUS];
static int filled;
static double read_at = RESAMPLE_REACH;

/* Hidden palettes keep their cursor but must not retain the loudness or
   muffling of a computer the listener has since left or destroyed. Their
   next audible transition is already covered by the palette crossfade. */
static void follow_hidden(voice_t *v)
{
    v->left = v->to_left; v->right = v->to_right; v->send = v->to_send;
    v->cutoff = v->to_cutoff > 0 ? v->to_cutoff : 20000;
    v->low[0] = v->low[1] = 0;
}

/* Runs a voice for n samples at the loops' rate, adding it to the bus from
   at. Open, it skips the muffling filter, which can't be quite open at 11 kHz. */
static void voice_run(voice_t *v, const made_sample_t *loop, int at, int n, float weight_from, float weight_to)
{
    if (weight_from + weight_to <= 0) { follow_hidden(v); return; }
    if (v->left + v->right + v->to_left + v->to_right < 1e-5f) { v->left = v->right = v->send = 0; return; }
    if (v->to_cutoff <= 0) v->to_cutoff = 20000;
    float glide = 1 - expf(-n / (GLIDE_SECONDS * MADE_RATE)), muffle_glide = 1 - expf(-n / (MUFFLE_GLIDE_SECONDS * MADE_RATE));
    /* Linear across the run toward the glided value: no steps. */
    float left = v->left + (v->to_left - v->left) * glide, right = v->right + (v->to_right - v->right) * glide;
    float sendto = v->send + (v->to_send - v->send) * glide;
    v->cutoff *= powf(v->to_cutoff / v->cutoff, muffle_glide);
    int open = v->cutoff >= 0.45f * MADE_RATE;
    float keep = expf(-2 * PI_F * fminf(v->cutoff, 0.45f * MADE_RATE) / MADE_RATE);
    int position = (int)v->position;
    for (int i = 0; i < n; ++i)
    {
        float x = (i + 1) / (float)n, value = (loop->data[position] - 128) / 128.0f;
        v->low[0] = value + (v->low[0] - value) * keep;
        v->low[1] = v->low[0] + (v->low[1] - v->low[0]) * keep;
        if (!open) value = v->low[1];
        value *= weight_from + (weight_to - weight_from) * x;
        bus[0][at + i] += value * (v->left + (left - v->left) * x);
        bus[1][at + i] += value * (v->right + (right - v->right) * x);
        bus[2][at + i] += value * (v->send + (sendto - v->send) * x);
        if (++position >= loop->length) position = 0;
    }
    v->position = position;
    v->left = left; v->right = right; v->send = sendto;
}

static void start(voice_t *v, const made_sample_t *loop, int offset)
{
    if (v->started) return;
    v->position = offset % loop->length;
    v->cutoff = v->to_cutoff > 0 ? v->to_cutoff : 20000;
    v->started = 1;
}

/* Every voice for n more samples at the end of the bus. */
static void bus_run(int n)
{
    for (int c = 0; c < 3; ++c) memset(bus[c] + filled, 0, (size_t)n * sizeof(float));
    float next_blend = alternative_computers ? fminf(1, computer_blend + n / (COMPUTER_FADE_SECONDS * MADE_RATE))
                                            : fmaxf(0, computer_blend - n / (COMPUTER_FADE_SECONDS * MADE_RATE));
    for (int k = 0; k < AMBIENT_KINDS; ++k)
        for (int l = 0; l < AMBIENCE_LAYERS; ++l)
        {
            voice_t *v = &voices[k][l];
            const made_sample_t *loop = MadeBank_Loop(k, l);
            if (!loop) continue;
            /* Each starts somewhere else in its loop; muffling starts where it should be. */
            start(v, loop, k * 7919 + l * 3571);
            voice_run(v, loop, filled, n, k == AMBIENT_COMPUTER ? 1 - computer_blend : 1,
                                        k == AMBIENT_COMPUTER ? 1 - next_blend : 1);
        }
    if (computer_blend + next_blend > 0)
    {
        for (int l = 0; l < AMBIENCE_LAYERS; ++l)
        {
            const made_sample_t *loop = MadeBank_ComputerActivity(l);
            if (!loop) continue;
            start(&computer_activity[l], loop, AMBIENT_COMPUTER * 7919 + l * 3571);
            voice_run(&computer_activity[l], loop, filled, n, computer_blend, next_blend);
        }
        const made_sample_t *body = MadeBank_ComputerBody();
        if (body)
        {
            start(&computer_body, body, AMBIENT_COMPUTER * 7919);
            voice_run(&computer_body, body, filled, n, computer_blend, next_blend);
        }
    }
    else
    {
        for (int l = 0; l < AMBIENCE_LAYERS; ++l) follow_hidden(&computer_activity[l]);
        follow_hidden(&computer_body);
    }
    computer_blend = next_blend;
    filled += n;
}

void AmbienceVoices_Mix(float *stereo, float *send, int frames, int rate, int resampling, float gain, float duck)
{
    double ratio = MADE_RATE / (double)rate;
    for (int done = 0; done < frames;)
    {
        int count = frames - done < 256 ? frames - done : 256;
        /* Mixed far enough ahead for the reading's reach past the last frame. */
        int need = (int)(read_at + count * ratio) + RESAMPLE_REACH + 2;
        if (need > filled) bus_run(need - filled);
        for (int i = 0; i < count; ++i)
        {
            double at = read_at + i * ratio;
            float scale = gain * duck;
            stereo[2 * (done + i)] += scale * Resample_AtFloat(bus[0], filled, at, resampling);
            stereo[2 * (done + i) + 1] += scale * Resample_AtFloat(bus[1], filled, at, resampling);
            send[done + i] += scale * Resample_AtFloat(bus[2], filled, at, resampling);
        }
        read_at += count * ratio;
        /* Keep only what the next readings reach back to. */
        int drop = (int)read_at - RESAMPLE_REACH - 1;
        if (drop > 0)
        {
            for (int c = 0; c < 3; ++c) memmove(bus[c], bus[c] + drop, (size_t)(filled - drop) * sizeof(float));
            filled -= drop; read_at -= drop;
        }
        done += count;
    }
}
