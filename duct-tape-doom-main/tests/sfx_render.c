/* Renders the made-up effects to WAV files without the game, for listening:

     doom-sfx-render <iwad> [-kind name|impacts|ambient|walk|hidden|all] [-room dry|room|hall|stone|all]
                     [-rate Hz] [-level L] [-gun on|off] [-armor 0-2] [-heft 0-2] [-ring 1]
                     [-kit on|off] [-metal current|friction|tread]
                     [-movement-send current|legacy] [-out dir]
   With no -rate, use each effect's native game-bank rate.

   Each file plays the WAD's pistol three times, each shot with the effect
   it would set off (a new seed every time, so the variation shows), through
   the mixer as the game has it: 8-bit samples held between frames, DOOM's
   effect gains, the room reverb and the limiter. A lamp or screen hums
   before each shot and the effect cuts it off. "pistol" is the shot alone,
   for reference. Files are <out>/<kind>-<room>.wav. The level is the
   effect's share of a point-blank shot (DOOM volume 15 is 1). With -gun off
   the shots are left out and the effects play alone.

   The ambient sounds (lamp, crt, nukage, ...) play for ten seconds each, at
   a level of 0.35 times the kind's own (lamps play lower) unless -level says
   otherwise, with one pistol shot at six
   seconds to judge them against. The lamp also plays under DOOM's light
   specials: lamp-flicker (a broken light), lamp-strobe and lamp-glow.

   -kind walk renders footsteps per floor (walk-stone, walk-metal, ...):
   six walking, ten running and a landing, as the game layers them, with
   the kit -armor (0 none, 1 green, 2 blue) and -heft (0 fist or pistol,
   1 a slung gun, 2 rockets or BFG) say. -ring scales how much the kit's
   metal rings (0 leaves the noise alone). -kit off leaves only the boot
   contact, with the same steps, timing and room, for comparing floor
   sounds; its files are walk-<floor>-boots-<room>.wav. Kit defaults to on.
   -metal current (the default), friction or tread selects the metal-floor
   contact: the current sound, dry sole friction or heel-to-tread contact.
   -movement-send current (the default) sends the boots and kit to the room
   at their actual level; legacy uses the previous square-root send for
   comparisons. It changes only the walk layers' room send, at full game
   SFX volume; direct sounds and the pistol are unchanged.

   -kind hidden renders the sounds inside a base's walls (pipe-rush,
   steam, ... of audio/hidden_synth.h), three variations each after
   one pistol shot: dry is the sound itself, close; the rooms hear it from
   behind a wall, muffled and quiet, off to one side. hidden-scene is a
   minute of them, now and then, over the room tone, as the base around the
   player might sound. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include "audio/ambient_synth.h"
#include "audio/hidden_synth.h"
#include "audio/impact_synth.h"
#include "audio/resample.h"
#include "audio/reverb.h"

#define RATE 44100
#define SFX_GAIN 0.75f		/* i_sdl_sound.c's */
#define LIMIT 0.9f
#define LIMIT_RELEASE_SECONDS 0.15f
#define SHOTS 3
#define HUM_LEVEL 0.35f		/* a lamp overhead, as the hum will play */
#define AMBIENT_SECONDS 10
#define AMBIENT_SHOT 6.0f
#define TIC_RATE 35

static int gun_on = 1;
static int kit_on = 1;
static int movement_send_legacy = 0;

typedef struct { const unsigned char *data; int length, rate; } sample_t;

static void fail (const char *message, const char *what)
{
    fprintf (stderr, message, what);
    fputc ('\n', stderr);
    exit (1);
}

static unsigned read16 (const unsigned char *p) { return p[0] | p[1] << 8; }
static unsigned read32 (const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }

/* The last DSPISTOL in the WAD, as DMX holds it. */
static sample_t load_pistol (const char *path)
{
    FILE *f = fopen (path, "rb");
    if (!f) fail ("Can't open %s", path);
    fseek (f, 0, SEEK_END);
    long size = ftell (f);
    rewind (f);
    unsigned char *wad = malloc (size);
    if (!wad || size < 12 || fread (wad, 1, size, f) != (size_t)size) fail ("Can't read %s", path);
    fclose (f);
    unsigned count = read32 (wad + 4), dir = read32 (wad + 8);
    if (dir > (unsigned)size || count > (size - dir) / 16) fail ("%s is not a WAD", path);
    for (int n = (int)count - 1; n >= 0; --n)
    {
	const unsigned char *entry = wad + dir + 16 * n;
	char name[9] = {0};
	for (int i = 0; i < 8; ++i) name[i] = (char)toupper (entry[8 + i]);
	unsigned pos = read32 (entry), len = read32 (entry + 4);
	if (strcmp (name, "DSPISTOL") || pos > (unsigned)size || len > size - pos || len < 8) continue;
	const unsigned char *lump = wad + pos;
	unsigned frames = read32 (lump + 4);
	if (read16 (lump) != 3 || frames > len - 8) break;
	return (sample_t){ lump + 8, (int)frames, (int)read16 (lump + 2) };
    }
    fail ("%s has no DSPISTOL", path);
    return (sample_t){0};
}

/* Rooms as room.c measures them: 1 a 256-unit tech room, mostly smooth
   panels; 2 a big hall, half rough; 3 the tech room's shape built all of
   stone (room.c's stone: absorbs 0.1, dark 0.45, scatter 0.65; Sabine gives
   its decay). */
static room_t room_preset (int kind)
{
    room_t room = {0};
    int hall = kind == 2, stone = kind == 3;
    static const float room_walls[8] = { 128, 160, 200, 140, 128, 170, 210, 150 };
    static const float hall_walls[8] = { 420, 560, 700, 480, 400, 620, 680, 520 };
    const float *walls = hall ? hall_walls : room_walls;
    for (int k = 0; k < 8; ++k)
    {
	room.delay[k] = 2 * walls[k] / 10700.0f;
	room.gain[k] = 0.9f * 0.35f / (1 + walls[k] / 320);
	room.pan[k] = -sinf (k * 3.14159265f / 4);
    }
    float above = hall ? 215 : 87;
    room.delay[8] = 2 * above / 10700.0f;
    room.gain[8] = (stone ? 0.9f : 0.7f) * 0.25f / (1 + above / 320);
    room.decay = hall ? 1.8f : stone ? 0.87f : 0.7f;
    room.damping = hall ? 0.25f : stone ? 0.45f : 0.35f;
    room.late = hall ? 0.9f : stone ? 0.57f : 0.56f;
    room.scatter = hall ? 0.4f : stone ? 0.65f : 0.15f;
    for (int k = 0; k < ROOM_TAPS; ++k) room.spread[k] = room.scatter, room.dark[k] = room.damping;
    return room;
}

/* One sound in the mix: from frame start, held samples, mono to both sides. */
typedef struct { sample_t sample; long start, stop; float dry, send; } voice_t;

static void mix (const voice_t *v, float *stereo, float *send, long frames)
{
    double step = v->sample.rate / (double)RATE;
    for (long n = v->start < 0 ? 0 : v->start; n < frames && n < v->stop; ++n)
    {
	double position = (n - v->start) * step;
	if (position >= v->sample.length) break;
	float value = Resample_At (v->sample.data, v->sample.length, position, RESAMPLE_ORIGINAL);
	stereo[2 * n] += value * v->dry;
	stereo[2 * n + 1] += value * v->dry;
	send[n] += value * v->send;
    }
}

static unsigned char *render_8bit (int kind, unsigned seed, int rate, int *length, int hum)
{
    *length = hum ? (int)(1.0f * rate) : Impact_Length (kind, rate);
    float *buffer = calloc ((size_t)*length, sizeof(float));
    unsigned char *data = malloc ((size_t)*length);
    if (!buffer || !data) fail ("Out of memory%s", "");
    if (hum) Impact_Hum (kind == IMPACT_SCREEN_DIES, seed, rate, buffer, *length);
    else Impact_Render (kind, seed, rate, buffer, *length);
    Impact_To8Bit (buffer, data, *length);
    free (buffer);
    return data;
}

static void put16 (FILE *f, unsigned v) { fputc (v & 255, f); fputc (v >> 8 & 255, f); }
static void put32 (FILE *f, unsigned v) { put16 (f, v & 0xffff); put16 (f, v >> 16); }

static void write_wav (const char *path, const float *stereo, long frames)
{
    FILE *f = fopen (path, "wb");
    if (!f) fail ("Can't write %s", path);
    fwrite ("RIFF", 1, 4, f); put32 (f, 36 + frames * 4);
    fwrite ("WAVEfmt ", 1, 8, f); put32 (f, 16); put16 (f, 1); put16 (f, 2);
    put32 (f, RATE); put32 (f, RATE * 4); put16 (f, 4); put16 (f, 16);
    fwrite ("data", 1, 4, f); put32 (f, frames * 4);
    for (long i = 0; i < frames * 2; ++i)
    {
	float v = stereo[i] * 32767;
	put16 (f, (unsigned)(int16_t)(v >= 32767 ? 32767 : v <= -32768 ? -32768 : lrintf (v)));
    }
    fclose (f);
}

/* The room's reverb, the mixer's limiter, and the file. Room 0 dry, else room_preset's. */
static void finish (float *stereo, float *send, long frames, int room, const char *name, const char *out)
{
    if (room)
    {
	room_t preset = room_preset (room);
	reverb_t *reverb = Reverb_New (RATE);
	if (!reverb) fail ("Out of memory%s", "");
	Reverb_SetRoom (reverb, &preset);
	for (long n = 0; n < frames; n += 512)
	    Reverb_Process (reverb, send + n, stereo + 2 * n, (int)(frames - n < 512 ? frames - n : 512), 1);
	Reverb_Free (reverb);
    }
    /* The mixer's limiter. */
    float gain = 1, release = 1 - expf (-1.0f / (LIMIT_RELEASE_SECONDS * RATE)), peak = 0;
    for (long n = 0; n < frames; ++n)
    {
	float p = fmaxf (fabsf (stereo[2 * n]), fabsf (stereo[2 * n + 1]));
	peak = fmaxf (peak, p);
	gain += (1 - gain) * release;
	if (p * gain > LIMIT) gain = LIMIT / p;
	stereo[2 * n] *= gain; stereo[2 * n + 1] *= gain;
    }
    static const char *const rooms[] = { "dry", "room", "hall", "stone" };
    char path[1024];
    snprintf (path, sizeof(path), "%s/%s-%s.wav", out, name, rooms[room]);
    write_wav (path, stereo, frames);
    printf ("%-28s peak %.2f%s\n", path, peak, peak > LIMIT ? " (limited)" : "");
    free (stereo); free (send);
}

static unsigned char *to_8bit (float *buffer, int length)
{
    unsigned char *data = malloc ((size_t)length);
    if (!data) fail ("Out of memory%s", "");
    Impact_To8Bit (buffer, data, length);
    free (buffer);
    return data;
}

/* DOOM's light specials, per tic, 0 the sector's darkest to 1 its brightest
   (p_lights.c's timings): a broken light, bright 1-64 tics and dark 1-8; a
   strobe, dark 35 and bright 5; a glow sweeping 8 levels a tic over 96. */
enum { STEADY, FLICKER, STROBE, GLOW };

static float *light_pattern (int pattern, int tics)
{
    float *light = malloc ((size_t)tics * sizeof(float));
    if (!light) fail ("Out of memory%s", "");
    unsigned state = 12345;
    int left = 0, bright = 1;
    for (int t = 0; t < tics; ++t)
    {
	state = state * 1103515245u + 12345u;
	int roll = (int)(state >> 16 & 255);
	if (pattern == GLOW)
	{
	    int step = t % 24;
	    light[t] = (step < 12 ? 12 - step : step - 12) / 12.0f;
	    continue;
	}
	if (pattern == STEADY) { light[t] = 1; continue; }
	if (left <= 0)
	{
	    bright = !bright;
	    left = pattern == FLICKER ? (bright ? (roll & 63) + 1 : (roll & 7) + 1) : (bright ? 5 : 35);
	}
	--left;
	light[t] = (float)bright;
    }
    return light;
}

/* Ten seconds of an ambient kind through the mixer, one shot at six. */
static void render_ambient (const sample_t *pistol, int kind, int pattern, const char *name,
			    int room, int rate, float level, const char *out)
{
    long frames = (long)AMBIENT_SECONDS * RATE;
    float *stereo = calloc ((size_t)frames * 2, sizeof(float)), *send = calloc ((size_t)frames, sizeof(float));
    int length = AMBIENT_SECONDS * rate, tics = AMBIENT_SECONDS * TIC_RATE;
    float *buffer = calloc ((size_t)length, sizeof(float)), *light = light_pattern (pattern, tics);
    if (!stereo || !send || !buffer) fail ("Out of memory%s", "");
    Ambient_Render (kind, 7919u * (unsigned)kind + 3, rate, buffer, length, pattern == STEADY ? NULL : light, tics);
    unsigned char *data = to_8bit (buffer, length);
    voice_t ambient = { { data, length, rate }, 0, frames, SFX_GAIN * level, SFX_GAIN * sqrtf (level) };
    mix (&ambient, stereo, send, frames);
    voice_t gun = { *pistol, (long)(AMBIENT_SHOT * RATE), frames, SFX_GAIN, SFX_GAIN };
    if (gun_on) mix (&gun, stereo, send, frames);
    finish (stereo, send, frames, room, name, out);
    free (data); free (light);
}

/* kind -1 is the pistol alone; room 0 dry, 1 room, 2 hall, 3 stone. */
static void render (const sample_t *pistol, int kind, int room, int rate, float level, const char *out)
{
    int dies = kind == IMPACT_LAMP_DIES || kind == IMPACT_SCREEN_DIES;
    float before = dies ? 1.0f : 0.4f, after = fmaxf (1.4f, (kind < 0 ? 0 : Impact_Length (kind, rate) / (float)rate) + 0.4f);
    long period = (long)((before + after) * RATE), frames = period * SHOTS;
    float *stereo = calloc ((size_t)frames * 2, sizeof(float)), *send = calloc ((size_t)frames, sizeof(float));
    unsigned char *made[SHOTS * 2] = {0};
    if (!stereo || !send) fail ("Out of memory%s", "");

    for (int s = 0; s < SHOTS; ++s)
    {
	long shot = s * period + (long)(before * RATE);
	voice_t gun = { *pistol, shot, frames, SFX_GAIN, SFX_GAIN };
	if (gun_on) mix (&gun, stereo, send, frames);
	if (kind < 0) continue;
	unsigned seed = 1000u * (unsigned)kind + 17u * (unsigned)s + 1;
	int length;
	made[2 * s] = render_8bit (kind, seed, rate, &length, 0);
	voice_t effect = { { made[2 * s], length, rate }, shot, frames, SFX_GAIN * level, SFX_GAIN * sqrtf (level) };
	mix (&effect, stereo, send, frames);
	if (!dies) continue;
	/* The hum up to the shot, where the dying sample takes over. */
	made[2 * s + 1] = render_8bit (kind, seed, rate, &length, 1);
	voice_t hum = { { made[2 * s + 1], length, rate }, shot - (long)(before * RATE), shot,
			SFX_GAIN * HUM_LEVEL, SFX_GAIN * sqrtf (HUM_LEVEL) };
	mix (&hum, stereo, send, frames);
    }
    finish (stereo, send, frames, room, kind < 0 ? "pistol" : Impact_Name (kind), out);
    for (int i = 0; i < SHOTS * 2; ++i) free (made[i]);
}

/* Footsteps on a floor kind: six walking, ten running, a landing; the
   pistol once after the walk to judge them against. */
static void render_walk (const sample_t *pistol, int floor, int armor, int heft, int room, int rate, const char *out)
{
    long frames = (long)(9.0f * RATE);
    float *stereo = calloc ((size_t)frames * 2, sizeof(float)), *send = calloc ((size_t)frames, sizeof(float));
    if (!stereo || !send) fail ("Out of memory%s", "");
    float t = 0.3f;
    for (int s = 0; s < 17; ++s)
    {
	float force = s < 6 ? 0 : s < 16 ? 1 : 1.8f;
	if (s == 16) t += 0.8f;
	impact_layer_t layers[4];
	int count = Step_Layers (floor, force, armor, heft, layers);
	for (int l = 0; l < count; ++l)
	{
	    if (!kit_on && l > 0) continue;
	    int length;
	    unsigned char *data = render_8bit (layers[l].kind, 7717u * (unsigned)s + 31u * (unsigned)l + 5, rate, &length, 0);
	    voice_t v = { { data, length, rate }, (long)(t * RATE), frames, SFX_GAIN * layers[l].level,
		SFX_GAIN * (movement_send_legacy ? sqrtf (layers[l].level) : layers[l].level) };
	    mix (&v, stereo, send, frames);
	    free (data);
	}
	t += s < 5 ? 0.5f : 0.33f;
	if (s == 5)
	{
	    voice_t gun = { *pistol, (long)((t + 0.1f) * RATE), frames, SFX_GAIN, SFX_GAIN };
	    if (gun_on) mix (&gun, stereo, send, frames);
	    t += 0.8f;
	}
    }
    char name[64];
    static const char *const kit[3] = { "light", "slung", "heavy" };
    if (kit_on)
	snprintf (name, sizeof(name), "walk-%s-%s-armor%d", Impact_Name (floor) + 5, kit[heft], armor);
    else
	snprintf (name, sizeof(name), "walk-%s-boots", Impact_Name (floor) + 5);
    finish (stereo, send, frames, room, name, out);
}

#define BEHIND_LEVEL 0.3f	/* a sound behind a wall, of a point-blank shot */
#define BEHIND_HZ 900.0f	/* the mixer's muffling for a sound through a wall nearby */
#define SCENE_SECONDS 60

/* A sound behind a wall, as the mixer plays it: DMX's panning (separation
   1-254) and two one-pole lowpasses at cutoff Hz. */
static void mix_behind (const voice_t *v, int separation, float cutoff, float *stereo, float *send, long frames)
{
    double step = v->sample.rate / (double)RATE;
    float keep = expf (-2 * 3.14159265f * cutoff / RATE), low[2] = { 0, 0 };
    float left = fminf (1, (254 - separation) / 127.0f), right = fminf (1, separation / 127.0f);
    for (long n = v->start < 0 ? 0 : v->start; n < frames && n < v->stop; ++n)
    {
	double position = (n - v->start) * step;
	if (position >= v->sample.length) break;
	float value = Resample_At (v->sample.data, v->sample.length, position, RESAMPLE_ORIGINAL);
	low[0] = value + (low[0] - value) * keep;
	low[1] = low[0] + (low[1] - low[0]) * keep;
	stereo[2 * n] += low[1] * v->dry * left;
	stereo[2 * n + 1] += low[1] * v->dry * right;
	send[n] += low[1] * v->send;
    }
}

static unsigned char *render_hidden_8bit (int kind, unsigned seed, int rate, int *length)
{
    *length = Hidden_Length (kind, rate);
    float *buffer = calloc ((size_t)*length, sizeof(float));
    if (!buffer) fail ("Out of memory%s", "");
    Hidden_Render (kind, seed, rate, buffer, *length);
    return to_8bit (buffer, *length);
}

/* Three variations of a hidden kind after one shot: room 0 close and dry,
   else behind a wall off to the left in that room. level < 0 is the default. */
static void render_hidden (const sample_t *pistol, int kind, int room, int rate, float level, const char *out)
{
    float each = Hidden_Length (kind, rate) / (float)rate + 0.5f;
    long frames = (long)((1.2f + SHOTS * each) * RATE);
    float *stereo = calloc ((size_t)frames * 2, sizeof(float)), *send = calloc ((size_t)frames, sizeof(float));
    if (!stereo || !send) fail ("Out of memory%s", "");
    voice_t gun = { *pistol, (long)(0.2f * RATE), frames, SFX_GAIN, SFX_GAIN };
    if (gun_on) mix (&gun, stereo, send, frames);
    float l = level >= 0 ? level : room ? BEHIND_LEVEL : 0.6f;
    for (int s = 0; s < SHOTS; ++s)
    {
	int length;
	unsigned char *data = render_hidden_8bit (kind, 977u * (unsigned)s + 13u, rate, &length);
	voice_t v = { { data, length, rate }, (long)((1.2f + s * each) * RATE), frames, SFX_GAIN * l, SFX_GAIN * sqrtf (l) };
	if (room) mix_behind (&v, 60, BEHIND_HZ, stereo, send, frames);
	else mix (&v, stereo, send, frames);
	free (data);
    }
    char name[64];
    snprintf (name, sizeof(name), "hidden-%s", Hidden_Name (kind));
    finish (stereo, send, frames, room, name, out);
}

static float scene_unit (unsigned *state)
{
    *state = *state * 1103515245u + 12345u;
    return (*state >> 8 & 0xffff) / 65536.0f;
}

/* A minute of the base behind the walls over the room tone, a shot at half
   time: the kinds come up now and then, from either side, through walls
   near and far. */
static void render_hidden_scene (const sample_t *pistol, int room, int rate, float level, const char *out)
{
    long frames = (long)SCENE_SECONDS * RATE;
    int length = SCENE_SECONDS * rate;
    float *stereo = calloc ((size_t)frames * 2, sizeof(float)), *send = calloc ((size_t)frames, sizeof(float));
    float *tone = calloc ((size_t)length, sizeof(float));
    if (!stereo || !send || !tone) fail ("Out of memory%s", "");
    Ambient_Render (AMBIENT_ROOM, 11, rate, tone, length, NULL, SCENE_SECONDS * TIC_RATE);
    unsigned char *bed_data = to_8bit (tone, length);
    float bed_level = HUM_LEVEL * Ambient_Level (AMBIENT_ROOM);
    voice_t bed = { { bed_data, length, rate }, 0, frames, SFX_GAIN * bed_level, SFX_GAIN * sqrtf (bed_level) };
    mix (&bed, stereo, send, frames);
    voice_t gun = { *pistol, (long)(SCENE_SECONDS / 2 * RATE), frames, SFX_GAIN, SFX_GAIN };
    if (gun_on) mix (&gun, stereo, send, frames);
    /* How often each kind comes up: drips and settling most, the press seldom. */
    static const int weight[HIDDEN_KINDS] = { 2, 1, 2, 4, 1, 4 };
    int total = 0;
    for (int k = 0; k < HIDDEN_KINDS; ++k) total += weight[k];
    unsigned state = 2024;
    for (float t = 1.5f; t < SCENE_SECONDS - 2; t += 1.5f - 4 * logf (fmaxf (scene_unit (&state), 1e-4f)))
    {
	int roll = (int)(scene_unit (&state) * total), kind = 0;
	while (roll >= weight[kind]) roll -= weight[kind++];
	int structural = kind == HIDDEN_PRESS || kind == HIDDEN_DUCT, data_length;
	float cutoff = structural ? 350 + 350 * scene_unit (&state) : 600 + 800 * scene_unit (&state);
	float l = (level >= 0 ? level : BEHIND_LEVEL) * (0.5f + 0.5f * scene_unit (&state));
	int separation = 40 + (int)(176 * scene_unit (&state));
	unsigned char *data = render_hidden_8bit (kind, state, rate, &data_length);
	voice_t v = { { data, data_length, rate }, (long)(t * RATE), frames, SFX_GAIN * l, SFX_GAIN * sqrtf (l) };
	mix_behind (&v, separation, cutoff, stereo, send, frames);
	free (data);
    }
    finish (stereo, send, frames, room, "hidden-scene", out);
    free (bed_data);
}

int main (int argc, char **argv)
{
    const char *kind = "all", *room = "all", *out = ".";
    int rate = 0, rate_given = 0, armor = 0, heft = 0;
    float level = -1;
    if (argc < 2)
    {
	fprintf (stderr, "usage: %s <iwad> [-kind name|pistol|impacts|ambient|walk|hidden|all] [-room dry|room|hall|stone|all] [-rate Hz] [-level L] [-gun on|off] [-armor 0-2] [-heft 0-2] [-ring 1] [-kit on|off] [-metal current|friction|tread] [-movement-send current|legacy] [-out dir]\nWithout -rate, use each effect's native game-bank rate.\n", argv[0]);
	return 2;
    }
    for (int i = 2; i + 1 < argc; i += 2)
    {
	if (!strcmp (argv[i], "-kind")) kind = argv[i + 1];
	else if (!strcmp (argv[i], "-room")) room = argv[i + 1];
	else if (!strcmp (argv[i], "-rate")) { rate = atoi (argv[i + 1]); rate_given = 1; }
	else if (!strcmp (argv[i], "-level")) level = (float)atof (argv[i + 1]);
	else if (!strcmp (argv[i], "-out")) out = argv[i + 1];
	else if (!strcmp (argv[i], "-armor")) armor = atoi (argv[i + 1]);
	else if (!strcmp (argv[i], "-heft")) heft = atoi (argv[i + 1]);
	else if (!strcmp (argv[i], "-ring")) Step_SetGearRing ((float)atof (argv[i + 1]));
	else if (!strcmp (argv[i], "-gun")) gun_on = strcmp (argv[i + 1], "off") != 0;
	else if (!strcmp (argv[i], "-kit"))
	{
	    if (!strcmp (argv[i + 1], "on")) kit_on = 1;
	    else if (!strcmp (argv[i + 1], "off")) kit_on = 0;
	    else fail ("Unknown kit setting %s", argv[i + 1]);
	}
	else if (!strcmp (argv[i], "-metal"))
	{
	    if (!strcmp (argv[i + 1], "current")) Step_SetMetalStyle (STEP_METAL_CURRENT);
	    else if (!strcmp (argv[i + 1], "friction")) Step_SetMetalStyle (STEP_METAL_FRICTION);
	    else if (!strcmp (argv[i + 1], "tread")) Step_SetMetalStyle (STEP_METAL_TREAD);
	    else fail ("Unknown metal style %s", argv[i + 1]);
	}
	else if (!strcmp (argv[i], "-movement-send"))
	{
	    if (!strcmp (argv[i + 1], "current")) movement_send_legacy = 0;
	    else if (!strcmp (argv[i + 1], "legacy")) movement_send_legacy = 1;
	    else fail ("Unknown movement send %s", argv[i + 1]);
	}
	else fail ("Unknown option %s", argv[i]);
    }
    if (rate_given && (rate < 4000 || rate > 48000)) fail ("Rate %s is out of range", "");
    sample_t pistol = load_pistol (argv[1]);
    Resample_Init ();
    static const char *const rooms[] = { "dry", "room", "hall", "stone" };
    int found = 0;
    int all = !strcmp (kind, "all");
    for (int k = -1; k < IMPACT_KINDS; ++k)
    {
	const char *name = k < 0 ? "pistol" : Impact_Name (k);
	if (!all && strcmp (kind, "impacts") && strcmp (kind, name)) continue;
	for (int r = 0; r < 4; ++r)
	    if (!strcmp (room, "all") || !strcmp (room, rooms[r]))
		render (&pistol, k, r, rate ? rate : Impact_Rate(k), level < 0 ? 0.6f : level, out), found = 1;
    }
    static const struct { int kind, pattern; const char *name; } lamps[] = {
	{ AMBIENT_LAMP, FLICKER, "lamp-flicker" }, { AMBIENT_LAMP, STROBE, "lamp-strobe" },
	{ AMBIENT_LAMP, GLOW, "lamp-glow" },
    };
    int lamp_count = (int)(sizeof(lamps) / sizeof(lamps[0]));
    for (int k = 0; k < AMBIENT_KINDS + lamp_count; ++k)
    {
	int ambient_kind = k < AMBIENT_KINDS ? k : lamps[k - AMBIENT_KINDS].kind;
	int pattern = k < AMBIENT_KINDS ? STEADY : lamps[k - AMBIENT_KINDS].pattern;
	const char *name = k < AMBIENT_KINDS ? Ambient_Name (k) : lamps[k - AMBIENT_KINDS].name;
	if (!all && strcmp (kind, "ambient") && strcmp (kind, name)) continue;
	for (int r = 0; r < 4; ++r)
	    if (!strcmp (room, "all") || !strcmp (room, rooms[r]))
		render_ambient (&pistol, ambient_kind, pattern, name, r, rate ? rate : 11025,
				level < 0 ? HUM_LEVEL * Ambient_Level (ambient_kind) : level, out), found = 1;
    }
    for (int floor = IMPACT_STEP_STONE; floor <= IMPACT_STEP_SOFT; ++floor)
    {
	if (strcmp (kind, "walk")) break;
	for (int r = 0; r < 4; ++r)
	    if (!strcmp (room, "all") || !strcmp (room, rooms[r]))
		render_walk (&pistol, floor, armor, heft, r, rate ? rate : 11025, out), found = 1;
    }
    for (int k = 0; k <= HIDDEN_KINDS; ++k)
    {
	const char *name = k < HIDDEN_KINDS ? Hidden_Name (k) : "scene";
	char full[64];
	snprintf (full, sizeof(full), "hidden-%s", name);
	if (!all && strcmp (kind, "hidden") && strcmp (kind, name) && strcmp (kind, full)) continue;
	for (int r = 0; r < 4; ++r)
	{
	    if (strcmp (room, "all") && strcmp (room, rooms[r])) continue;
	    if (k < HIDDEN_KINDS) render_hidden (&pistol, k, r, rate ? rate : 11025, level, out);
	    else render_hidden_scene (&pistol, r, rate ? rate : 11025, level, out);
	    found = 1;
	}
    }
    if (!found) fail ("No kind or room called %s", kind);
    return 0;
}
