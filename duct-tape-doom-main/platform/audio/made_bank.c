/* Renders the made-up sounds once and keeps them as 8-bit samples. A loop
 * is rendered a little longer than it plays and its tail is faded into its
 * head, so it wraps without a seam; the hums repeat every second, so they
 * join in phase. */
#include <stdlib.h>
#include "ambience.h"
#include "computer_synth.h"
#include "hidden_synth.h"
#include "impact_synth.h"
#include "made_bank.h"
#include "synth_kit.h"

#define LOOP_FADE_SECONDS 0.5f

static made_sample_t impacts[IMPACT_KINDS][MADE_STEP_VARIANTS];
static made_sample_t loops[AMBIENT_KINDS][AMBIENCE_LAYERS];
static made_sample_t strikes[MADE_VARIANTS];
static made_sample_t hidden[HIDDEN_KINDS][MADE_VARIANTS];
static made_sample_t computer_body;
static made_sample_t computer_activity[AMBIENCE_LAYERS];

static made_sample_t keep(float *buffer, int length, int rate)
{
    unsigned char *data = malloc((size_t)length);
    made_sample_t sample = { data, data ? length : 0, rate };
    if (data) Impact_To8Bit(buffer, data, length);
    free(buffer);
    return sample;
}

/* The wind's gusts take seconds each; in a short loop the same pattern would
   come round again and again, which grates. */
static int loop_seconds(int kind) { return kind == AMBIENT_WIND ? 32 : MADE_LOOP_SECONDS; }

static made_sample_t loop(int kind, int layer)
{
    int length = loop_seconds(kind) * MADE_RATE, fade = (int)(LOOP_FADE_SECONDS * MADE_RATE);
    float *buffer = calloc((size_t)(length + fade), sizeof(float));
    if (!buffer) return (made_sample_t){ 0 };
    Ambient_Render(kind, 0x51ed270bu + 977u * (unsigned)kind + 7919u * (unsigned)layer, MADE_RATE, buffer, length + fade, NULL, 0);
    /* Equal power: the tail and the head are unrelated noise for most kinds. */
    for (int n = 0; n < fade; ++n)
    {
        float x = (n + 0.5f) / fade;
        buffer[n] = buffer[n] * sinf(0.5f * SYNTH_PI * x) + buffer[length + n] * cosf(0.5f * SYNTH_PI * x);
    }
    return keep(buffer, length, MADE_RATE);
}

static made_sample_t strike(unsigned seed)
{
    int length = (int)(0.08f * MADE_RATE);
    float *buffer = calloc((size_t)length, sizeof(float));
    if (!buffer) return (made_sample_t){ 0 };
    rng_t r = rng_seed(seed);
    add_click(buffer, length, 0, MADE_RATE, &r, 0.6f, 1500, 4000);
    add_crackle(buffer, length, MADE_RATE, &r, 0, 0.04f, 400, 0.01f, 0.45f, 1500, 4500);
    return keep(buffer, length, MADE_RATE);
}

static made_sample_t computer_body_loop(void)
{
    int length = 32 * MADE_RATE;
    float *buffer = calloc((size_t)length, sizeof(float));
    if (!buffer) return (made_sample_t){ 0 };
    /* All body partials and modulations complete whole cycles. Applying the
       equal-power noise overlap here would swell these coherent tones. */
    Computer_BodyRender(0xa361c059u, MADE_RATE, buffer, length);
    return keep(buffer, length, MADE_RATE);
}

static made_sample_t computer_activity_loop(int layer)
{
    static const int seconds[AMBIENCE_LAYERS] = { 32, 37, 43 };
    int length = seconds[layer] * MADE_RATE, fade = (int)(LOOP_FADE_SECONDS * MADE_RATE);
    float *buffer = calloc((size_t)(length + fade), sizeof(float));
    if (!buffer) return (made_sample_t){ 0 };
    Computer_ActivityRender(0xd09f713bu + 7919u * (unsigned)layer, MADE_RATE, buffer, length + fade);
    for (int n = 0; n < fade; ++n)
    {
        float x = (n + 0.5f) / fade;
        buffer[n] = buffer[n] * sinf(0.5f * SYNTH_PI * x) + buffer[length + n] * cosf(0.5f * SYNTH_PI * x);
    }
    return keep(buffer, length, MADE_RATE);
}

void MadeBank_Build(void)
{
    for (int kind = 0; kind < IMPACT_KINDS; ++kind)
        for (int v = 0; v < MadeBank_Variants(kind); ++v)
        {
            int rate = Impact_Rate(kind), length = Impact_Length(kind, rate);
            float *buffer = calloc((size_t)length, sizeof(float));
            if (!buffer) continue;
            Impact_Render(kind, 0x9e3779b1u * (unsigned)(kind * MADE_STEP_VARIANTS + v + 1), rate, buffer, length);
            if(kind>=IMPACT_GLASS_TINKLE&&kind<=IMPACT_GLASS_FRACTURE_BIG) {
                // Shorter non-metallic contacts have much less energy than the
                // former ringing voices. Set their attack loudness before 8-bit
                // conversion so the crunch survives gunfire and quantization.
                int attack=(int)(.18f*rate);if(attack>length)attack=length;
                double energy=0;
                for(int n=0;n<attack;++n)energy+=(double)buffer[n]*buffer[n];
                float rms=attack?sqrtf((float)(energy/attack)):0;
                float target=kind==IMPACT_GLASS_TINKLE?.055f:.14f;
                float gain=rms>.00001f?fminf(5.f,fmaxf(1.f,target/rms)):1.f;
                for(int n=0;n<length;++n)buffer[n]*=gain;
            }
            impacts[kind][v] = keep(buffer, length, rate);
        }
    for (int kind = 0; kind < AMBIENT_KINDS; ++kind)
        for (int layer = 0; layer < AMBIENCE_LAYERS; ++layer) loops[kind][layer] = loop(kind, layer);
    for (int v = 0; v < MADE_VARIANTS; ++v) strikes[v] = strike(0x2545f491u * (unsigned)(v + 1));
    for (int kind = 0; kind < HIDDEN_KINDS; ++kind)
        for (int v = 0; v < MADE_VARIANTS; ++v)
        {
            int length = Hidden_Length(kind, MADE_RATE);
            float *buffer = calloc((size_t)length, sizeof(float));
            if (!buffer) continue;
            Hidden_Render(kind, 0x7f4a7c15u * (unsigned)(kind * MADE_VARIANTS + v + 1), MADE_RATE, buffer, length);
            hidden[kind][v] = keep(buffer, length, MADE_RATE);
        }
    computer_body = computer_body_loop();
    for (int layer = 0; layer < AMBIENCE_LAYERS; ++layer)
        computer_activity[layer] = computer_activity_loop(layer);
}

int MadeBank_Variants(int kind)
{
    return (kind >= IMPACT_STEP_STONE && kind <= IMPACT_SPLASH_THICK) || (kind >= IMPACT_RUN_STONE && kind <= IMPACT_GEAR_ARMOR_RUN)
        ? MADE_STEP_VARIANTS : MADE_VARIANTS;
}

static const made_sample_t *ready(const made_sample_t *sample) { return sample->data ? sample : NULL; }

const made_sample_t *MadeBank_Impact(int kind, int variant)
{
    if (kind < 0 || kind >= IMPACT_KINDS) return NULL;
    return ready(&impacts[kind][(unsigned)variant % (unsigned)MadeBank_Variants(kind)]);
}
const made_sample_t *MadeBank_Loop(int kind, int layer)
{
    return kind >= 0 && kind < AMBIENT_KINDS ? ready(&loops[kind][(unsigned)layer % AMBIENCE_LAYERS]) : NULL;
}
const made_sample_t *MadeBank_Hidden(int kind, int variant)
{
    return kind >= 0 && kind < HIDDEN_KINDS ? ready(&hidden[kind][(unsigned)variant % MADE_VARIANTS]) : NULL;
}
const made_sample_t *MadeBank_Strike(int variant) { return ready(&strikes[(unsigned)variant % MADE_VARIANTS]); }
const made_sample_t *MadeBank_ComputerBody(void) { return ready(&computer_body); }
const made_sample_t *MadeBank_ComputerActivity(int layer)
{
    return ready(&computer_activity[(unsigned)layer % AMBIENCE_LAYERS]);
}
