#include "p_fluid.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "doomdef.h"
#include "p_local.h"
#include "audio/flyby.h"
#include "doomstat.h"
#include "i_sound.h"
#include "i_mixer_music.h"
#include "i_music.h"
#include "m_argv.h"
#include "w_wad.h"
#include "z_zone.h"
#include "audio/ambience.h"
#include "audio/ambience_voices.h"
#include "audio/beyond.h"
#include "audio/flyby.h"
#include "audio/hidden.h"
#include "audio/impact_synth.h"
#include "audio/made_bank.h"
#include "audio/occlusion.h"
#include "audio/steps.h"
#include "audio/resample.h"
#include "audio/treble.h"
#include "audio/reverb.h"
#include "game_profile.h"
#include "audio/sf2_synth.h"
#include "audio/sound_options.h"
#include "mod_soundbank.h"
#include "s_sound.h"
#include "i_system.h"
#include "../assets/toasty/voice.h"
#include <SDL3/SDL.h>

#define OUTPUT_RATE 44100
#define MIX_CHANNELS 32
/* SoundFont voices, allocated up front: GeneralUser GS peaks at 286 (D_SHAWN). */
#define SF2_VOICES 512
/* Effects at full volume, center: headroom for a few at once before the limiter. */
#define SFX_GAIN 0.75f
#define LIMIT 0.9f
#define LIMIT_RELEASE_SECONDS 0.15f
/* How fast a sound's muffling follows it moving behind or out from a wall. */
#define MUFFLE_GLIDE_SECONDS 0.08f
/* Ambience at full F4 amount and full effects volume, next to a point-blank shot. */
#define AMBIENCE_LEVEL 0.35f
/* How fast a sound's share of the space beyond follows it, and how fast
   the way into that space opens and closes as the listener moves. */
#define BEYOND_GLIDE_SECONDS 0.15f
#define COUPLING_GLIDE_SECONDS 0.5f
/* Ambience ducks under loud effects at once and comes back over this long. */
#define DUCK_RELEASE_SECONDS 1.2f
typedef struct { const byte *data; int length, rate; const float *pcm; int pcm_channels; } sample_t;
typedef struct {
    sample_t sample;
    double position, step;
    float left, right, send;
    float send_scale;  /* 1 for world sounds; personal source gain correction */
    /* Lowpass: cutoff glides to target; two one-pole stages of state. */
    float cutoff, target, low[2];
    float low_right[2],gain;
    unsigned group;
    /* How much of it is in the space beyond (beyond.h) rather than the
       listener's room, gliding to its target. */
    float beyond, beyond_target;
    treble_t treble;
    int handle, measured;
    /* Where a placed sound is, for the natural falloff, how far it carries
       (I_SoundReach), and how it was last panned and pitched. */
    float x, y, reach;
    int placed, separation, pitch;
} mix_channel_t;
static sample_t samples[NUMSFX];
static mix_channel_t channels[MIX_CHANNELS];
static SDL_AudioStream *stream;
static int next_handle = 1;
static int acoustics = 1, muffle_walls = 1, muffle_distance = 1;
static int resampling = RESAMPLE_ORIGINAL;
static float treble_amount = 1;
static int hidden_machinery=1,adjacent_reverb=1;
void I_SetAtmosphereExtras(int hidden,int adjacent){hidden_machinery=hidden!=0;adjacent_reverb=adjacent!=0;}
static float next_cutoff = OCCLUSION_OPEN_HZ;
static float next_x, next_y;
static int next_placed, natural_falloff = 1;
static float reverb_amount = 1;
/* The listener's room, and the bigger space beyond it (measured on the game
   thread; the mixer only has its coupling). */
static reverb_t *room_reverb, *beyond_reverb;
static beyond_t beyond;
static float coupling, coupling_target, next_beyond;
static float limiter_gain = 1;
static unsigned named_starts;
unsigned I_NamedSoundStarts(void) {return named_starts;}
static int fluid_sound_addon=1;
static int fluid_weapon_sound(int id,int volume,int separation,int pitch,int priority,int weapon);
int I_StartNamedSound(const char *logical,int volume,int separation,int pitch,int priority);
static int made_impacts = 1, glass_fracture, ambience = 1, footsteps = 1;
static float ambience_amount = 1, duck = 1;

static unsigned little16(const byte *p) { return p[0] | (unsigned)p[1] << 8; }
static uint32_t little32(const byte *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
/* How much of a sound reaches the listener from distance units away (DOOM's
   rough distance), 0-1. DOOM fades it in a straight line from 160 to 1200
   units. The natural falloff loses half its strength each time the distance
   doubles, as in air, and eases out to nothing at the same 1200, so nothing
   new is heard and nothing pops off. */
static int natural(void) { return natural_falloff && Game_HasEffect(GAME_EFFECT_NATURAL_FALLOFF); }
static float distance_gain(float distance)
{
    if (distance < 160) return 1;
    if (distance >= 1200) return 0;
    if (!natural()) return (1200 - distance) / 1040;
    float t = distance <= 900 ? 0 : (distance - 900) / 300;
    return 160 / distance * (1 - t * t * (3 - 2 * t));
}
static float listener_distance(float x, float y)
{
    const mobj_t *listener = players[consoleplayer].mo;
    if (!listener) return 0;
    float adx = fabsf(x - listener->x / (float)FRACUNIT), ady = fabsf(y - listener->y / (float)FRACUNIT);
    return adx + ady - SDL_min(adx, ady) / 2;
}
/* The game's own sounds at a point keep its volume unless the natural
   falloff sets it from where they are. DOOM's boss maps (map 8) play every
   sound at full strength; they stay as they are. */
static int falls_off(void) { return natural() && gamemap != 8; }
/* Loud effects carry farther, but only as the natural falloff hears them:
   DOOM's own straight fade keeps its 1200 units. */
static float reach_of(int id) { return falls_off() && id > 0 && id < NUMSFX ? SDL_max(Game_SoundCarry(id), 1.0f) : 1; }
fixed_t I_SoundReach(int id) { return (fixed_t)(reach_of(id) * FRACUNIT); }

/* volume is DOOM's 0-15 effects scale, already lowered with distance.
   Panning follows DMX: full level on both sides at the center (separation
   128), fading on the far side only. The reverb send falls off slower than
   the dry sound, so far sounds come out wetter. */
static void gains(mix_channel_t *channel, float volume, int separation, int pitch)
{
    float level = SDL_clamp(volume, 0.0f, 15.0f) / 15.0f * channel->gain;
    separation = SDL_clamp(separation, 1, 254);
    channel->left = SFX_GAIN * level * SDL_min(1.0f, (254 - separation) / 127.0f);
    channel->right = SFX_GAIN * level * SDL_min(1.0f, separation / 127.0f);
    channel->send = SFX_GAIN * sqrtf(level) * channel->send_scale;
    channel->separation = separation; channel->pitch = pitch;
    channel->step = channel->sample.rate / (double)OUTPUT_RATE * pow(2.0, (pitch - 128) / 64.0);
    Treble_Tune(&channel->treble, (float)(0.5 * channel->step * OUTPUT_RATE), OUTPUT_RATE);
}

static void audio(void *userdata, SDL_AudioStream *output, int additional, int total)
{
    (void)userdata; (void)total;
    float buffer[512 * 2], send[512], send_beyond[512];
    int frames = (additional + (int)sizeof(float)*2 - 1) / ((int)sizeof(float)*2);
    while (frames > 0)
    {
        int count = SDL_min(frames, 512);
        memset(buffer, 0, (size_t)count * 2 * sizeof(float));
        memset(send, 0, (size_t)count * sizeof(float));
        memset(send_beyond, 0, (size_t)count * sizeof(float));
        coupling += (coupling_target - coupling) * (1 - expf(-count / (COUPLING_GLIDE_SECONDS * OUTPUT_RATE)));
        for (int c = 0; c < MIX_CHANNELS; ++c)
        {
            mix_channel_t *channel = &channels[c];
            if (!channel->handle) continue;
            channel->cutoff *= powf(channel->target / channel->cutoff, 1 - expf(-count / (MUFFLE_GLIDE_SECONDS * OUTPUT_RATE)));
            float keep = expf(-2 * SDL_PI_F * channel->cutoff / OUTPUT_RATE);
            /* A sound fills the space it is in and spills through the
               opening into the other: a little of the listener's own into
               the space beyond, a little of that space's into their room. */
            channel->beyond += (channel->beyond_target - channel->beyond) * (1 - expf(-count / (BEYOND_GLIDE_SECONDS * OUTPUT_RATE)));
            float there = channel->beyond;
            float send_here = channel->send * (1 - there + coupling * there);
            float send_there = channel->send * (there + coupling * sqrtf(coupling) * (1 - there));
            /* Held samples bring their own sizzle up there instead. */
            int treble = resampling != RESAMPLE_ORIGINAL && treble_amount > 0;
            for (int i = 0; i < count; ++i)
            {
                int position = (int)channel->position;
                if (position >= channel->sample.length) { channel->handle = 0; break; }
                float value = channel->sample.pcm ? Resample_Float(channel->sample.pcm,channel->sample.length,channel->sample.pcm_channels,0,channel->position,RESAMPLE_CLEAN)
                    : Resample_At(channel->sample.data, channel->sample.length, channel->position, resampling);
                float right=value;
                if(channel->sample.pcm&&channel->sample.pcm_channels==2)
                    right=Resample_Float(channel->sample.pcm,channel->sample.length,2,1,channel->position,RESAMPLE_CLEAN);
                if (treble&&!channel->sample.pcm) value = Treble_Process(&channel->treble, value, treble_amount);
                if(!channel->sample.pcm||channel->sample.pcm_channels==1)right=value;
                channel->low[0] = value + (channel->low[0] - value) * keep;
                channel->low[1] = channel->low[0] + (channel->low[1] - channel->low[0]) * keep;
                value = channel->low[1];
                channel->low_right[0]=right+(channel->low_right[0]-right)*keep;
                channel->low_right[1]=channel->low_right[0]+(channel->low_right[1]-channel->low_right[0])*keep;
                right=channel->low_right[1];
                buffer[2*i] += value * channel->left;
                buffer[2*i+1] += right * channel->right;
                send[i] += (value+right)*.5f * send_here;
                send_beyond[i] += (value+right)*.5f * send_there;
                channel->position += channel->step;
            }
        }
        /* Ambience ducks under what the effects play, then fades back. */
        float peak = 0;
        for (int i = 0; i < 2 * count; ++i) peak = SDL_max(peak, fabsf(buffer[i]));
        float target = 1 / (1 + 1.5f * peak);
        duck += (target - duck) * (target < duck ? 0.5f : 1 - expf(-count / (DUCK_RELEASE_SECONDS * OUTPUT_RATE)));
        AmbienceVoices_Mix(buffer, send, count, OUTPUT_RATE, resampling, SFX_GAIN, duck);
        if (acoustics && room_reverb && beyond_reverb)
        {
            Reverb_Process(room_reverb, send, buffer, count, reverb_amount);
            /* The space beyond is only heard through its opening. */
            Reverb_Process(beyond_reverb, send_beyond, buffer, count, reverb_amount * sqrtf(coupling));
        }
        MixerMusic_Render(buffer, count);
        /* A peak limiter: drops at once to keep the mix under LIMIT, recovers
           slowly, so a burst of shots ducks the mix instead of crackling. */
        float release = 1 - expf(-1.0f / (LIMIT_RELEASE_SECONDS * OUTPUT_RATE));
        for (int i = 0; i < count; ++i)
        {
            float peak = SDL_max(fabsf(buffer[2*i]), fabsf(buffer[2*i+1]));
            limiter_gain += (1 - limiter_gain) * release;
            if (peak * limiter_gain > LIMIT) limiter_gain = LIMIT / peak;
            buffer[2*i] = SDL_clamp(buffer[2*i] * limiter_gain, -1.0f, 1.0f);
            buffer[2*i+1] = SDL_clamp(buffer[2*i+1] * limiter_gain, -1.0f, 1.0f);
        }
        if (!SDL_PutAudioStreamData(output, buffer, count * 2 * (int)sizeof(float))) return;
        frames -= count;
    }
}

static void play_kind(int kind, float x, float y, float z, float level, float reach, int personal);
/* A shot that passes close by whizzes; one a tic, so a shotgun's pellets or a
   chaingun's burst don't pile up. */
static int near_miss_sounds=1;
static void shot_traced(const mobj_t *shooter, const mobj_t *hit, float x1, float y1, float z1,
                        float x2, float y2, float z2)
{
    static int last = -1;
    float x, y, z, level;
    if (!near_miss_sounds || !made_impacts || gametic == last) return;
    if (!Flyby_Near(players[consoleplayer].mo, shooter, hit, x1, y1, z1, x2, y2, z2, &x, &y, &z, &level)) return;
    last = gametic;
    play_kind(IMPACT_FLYBY, x, y, z, level, 1, 0);
}
void I_SetFluidSoundAddon(int enabled) {
    if(fluid_sound_addon!=enabled&&stream) {
        SDL_LockAudioStream(stream);
        for(int i=0;i<MIX_CHANNELS;++i)if(channels[i].group)channels[i].handle=0;
        SDL_UnlockAudioStream(stream);
    }
    fluid_sound_addon=enabled;ModSounds_UseAddon(enabled);
}
void I_SetNearMissSounds(int enabled){near_miss_sounds=enabled!=0;}
void I_InitSound(void)
{
    if (M_CheckParm("-nosound")) return;
    int packs=M_CheckParm("-modsounds");
    if(packs) {
        int loaded=0;char error[512];
        for(int p=packs+1;p<myargc&&myargv[p][0]!='-';++p) {
            if(!ModSounds_AddArchive(myargv[p],error,sizeof(error)))I_Error("Sound pack: %s",error);
            ++loaded;
        }
        if(!loaded)I_Error("-modsounds requires at least one PK3");
        if(M_CheckParm("-fluid-sounds"))S_WeaponSound=fluid_weapon_sound;
    }
    for (int i = 1; i < NUMSFX; ++i)
    {
        int lump = I_GetSfxLumpNum(&S_sfx[i]);
        int length = W_LumpLength(lump);
        byte *data = W_CacheLumpNum(lump, PU_SOUND);
        Game_SfxLoaded(i, data);
        if (length < 8 || little16(data) != 3) continue;
        uint32_t size = little32(data + 4);
        if (!size || size > (uint32_t)(length - 8)) continue;
        samples[i] = (sample_t){ data + 8, (int)size, (int)little16(data + 2) };
    }
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
    { fprintf(stderr, "Audio unavailable: %s\n", SDL_GetError()); return; }
    SDL_AudioSpec spec = { SDL_AUDIO_F32, 2, OUTPUT_RATE };
    MadeBank_Build();
    room_reverb = Reverb_New(OUTPUT_RATE);
    beyond_reverb = Reverb_New(OUTPUT_RATE);
    Resample_Init();
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audio, NULL);
    if (!stream) { fprintf(stderr, "Audio unavailable: %s\n", SDL_GetError()); return; }
    SDL_ResumeAudioStreamDevice(stream);
    Game_TraceShots(shot_traced);
    fprintf(stderr, "SDL sound output ready.\n");
}
void I_ShutdownSound(void)
{
    Game_TraceShots(NULL);
    SDL_DestroyAudioStream(stream); stream = NULL;
    memset(channels,0,sizeof(channels));S_WeaponSound=NULL;ModSounds_Clear();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}
static void play_at(const made_sample_t *sample, float x, float y, float z, float level, float reach);
static void play_through(const made_sample_t *sample, float x, float y, float z, float level, float reach, float most, int personal);

/* Every tic the ambience voices follow the listener; every other the room is
   measured for the reverb. */
void I_UpdateSound(void)
{
    static int measured = -1, ambient = -1;
    if (!stream) return;
    if (gametic != ambient)
    {
        ambient = gametic;
        ambience_voice_t voices[AMBIENT_KINDS][AMBIENCE_LAYERS];
        ambience_voice_t computer_body;
        ambience_strike_t strike;
        float level = ambience ? ambience_amount * AMBIENCE_LEVEL * SDL_clamp(snd_SfxVolume, 0, 15) / 15.0f : 0;
        Ambience_Update(voices, level, muffle_walls, muffle_distance, &strike, &computer_body);
        SDL_LockAudioStream(stream);
        AmbienceVoices_Set(voices);
        AmbienceVoices_SetComputerBody(&computer_body);
        SDL_UnlockAudioStream(stream);
        if (strike.due) play_at(MadeBank_Strike(gametic), strike.x, strike.y, strike.z, strike.level, 1);
        step_event_t steps[8];
        int count = footsteps ? Steps_Tic(steps, 8) : 0;
        for (int i = 0; i < count; ++i)
            play_kind(steps[i].kind, steps[i].x, steps[i].y, steps[i].z, steps[i].level, 1, steps[i].personal);
        /* Now and then something behind the walls, with the ambience. */
        hidden_event_t hidden;
        if (ambience && hidden_machinery && Hidden_Tic(&hidden))
            play_through(MadeBank_Hidden(hidden.kind, (int)(hidden.take % MADE_VARIANTS)), hidden.x, hidden.y, hidden.z,
                         hidden.level * ambience_amount, hidden.reach, hidden.cutoff, 0);
    }
    if (!acoustics || (gametic >= measured && gametic - measured < 2)) return;
    measured = gametic;
    room_t room;
    room_outline_t outline;
    Room_Measure(&room, &outline);
    const mobj_t *listener = players[consoleplayer].mo;
    if(adjacent_reverb)Beyond_Measure(&room, &outline, listener ? listener->angle * (2 * SDL_PI_F / 4294967296.0f) : 0, &beyond);
    else memset(&beyond,0,sizeof(beyond));
    SDL_LockAudioStream(stream);
    if (room_reverb) Reverb_SetRoom(room_reverb, &room);
    if (beyond_reverb) Reverb_SetRoom(beyond_reverb, &beyond.room);
    coupling_target = beyond.coupling;
    SDL_UnlockAudioStream(stream);
}
void I_SetSoundOptions(int enabled, float amount, int walls, int distance, int falloff)
{
    if (stream) SDL_LockAudioStream(stream);
    if (enabled && !acoustics && room_reverb && beyond_reverb) { Reverb_Clear(room_reverb); Reverb_Clear(beyond_reverb); }
    acoustics = enabled != 0;
    reverb_amount = SDL_clamp(amount, 0.0f, 2.0f);
    muffle_walls = walls != 0;
    muffle_distance = distance != 0;
    natural_falloff = falloff != 0;
    if (stream) SDL_UnlockAudioStream(stream);
}
void I_SetSfxPlayback(int mode, float treble)
{
    if (stream) SDL_LockAudioStream(stream);
    resampling = SDL_clamp(mode, RESAMPLE_ORIGINAL, RESAMPLE_CLEAN);
    treble_amount = SDL_clamp(treble, 0.0f, 2.0f);
    if (stream) SDL_UnlockAudioStream(stream);
}

static mix_channel_t *find(int handle)
{
    for (int i = 0; i < MIX_CHANNELS; ++i)
        if (channels[i].handle == handle) return &channels[i];
    return NULL;
}
/* Measured when a sound starts, then every other tic as it or the listener moves. */
void I_SoundAt(int handle, fixed_t x, fixed_t y, fixed_t z)
{
    if (!stream) return;
    if (!handle)
    {
        next_cutoff = Occlusion_Cutoff(x, y, z, muffle_walls, muffle_distance);
        next_x = x / (float)FRACUNIT; next_y = y / (float)FRACUNIT; next_placed = 1;
        next_beyond = Beyond_Share(&beyond, next_x, next_y);
        return;
    }
    float fx = x / (float)FRACUNIT, fy = y / (float)FRACUNIT;
    SDL_LockAudioStream(stream);
    mix_channel_t *channel = find(handle);
    if (channel && channel->placed)
    {
        channel->x = fx; channel->y = fy;
        float volume = snd_SfxVolume * distance_gain(listener_distance(fx, fy) / channel->reach);
        if (falls_off()) gains(channel, volume, channel->separation, channel->pitch);
    }
    int due = channel && (gametic < channel->measured || gametic - channel->measured >= 2);
    if (due) channel->measured = gametic;
    SDL_UnlockAudioStream(stream);
    if (!due) return;
    float cutoff = Occlusion_Cutoff(x, y, z, muffle_walls, muffle_distance), share = Beyond_Share(&beyond, fx, fy);
    SDL_LockAudioStream(stream);
    if ((channel = find(handle))) { channel->target = cutoff; channel->beyond_target = share; }
    SDL_UnlockAudioStream(stream);
}
void I_SubmitSound(void) { }
void I_SetChannels(void) { }
/* Takes the free channel, or the quietest (the oldest of equals), so a
   faint far-off sound gives way to a near one. */
static int start_sample(sample_t sample, float volume, int separation, int pitch, float cutoff, float there, float send_scale, float gain, unsigned group, int limit)
{
    if (!stream || (!sample.data&&!sample.pcm)) return 0;
    SDL_LockAudioStream(stream);
    if(limit>0) {
        int active=0;for(int i=0;i<MIX_CHANNELS;++i)if(channels[i].handle&&channels[i].group==group)++active;
        if(active>=limit){SDL_UnlockAudioStream(stream);return 0;}
    }
    int slot = 0;
    for (int i = 0; i < MIX_CHANNELS; ++i)
    {
        if (!channels[i].handle) { slot = i; break; }
        float level = channels[i].left + channels[i].right, quietest = channels[slot].left + channels[slot].right;
        if (level < quietest || (level == quietest && channels[i].handle < channels[slot].handle)) slot = i;
    }
    mix_channel_t *channel = &channels[slot];
    channel->sample = sample;channel->gain=gain;channel->group=group;
    channel->position = 0;
    channel->cutoff = channel->target = cutoff;
    channel->beyond = channel->beyond_target = there;
    channel->low[0] = channel->low[1] = 0;
    channel->low_right[0]=channel->low_right[1]=0;
    Treble_Reset(&channel->treble);
    channel->measured = gametic;
    channel->placed = 0;
    channel->reach = 1;
    channel->send_scale = send_scale;
    if (next_handle == INT_MAX) next_handle = 1;
    channel->handle = next_handle++;
    gains(channel, volume, separation, pitch);
    int handle = channel->handle;
    SDL_UnlockAudioStream(stream);
    return handle;
}
int I_StartSound(int id,int volume,int separation,int pitch,int priority)
{
    (void)priority;
    float cutoff = next_cutoff, x = next_x, y = next_y, level = (float)volume, there = next_beyond;
    int placed = next_placed;
    next_cutoff = OCCLUSION_OPEN_HZ; next_placed = 0; next_beyond = 0;
    if (!stream || id < 1 || id >= NUMSFX || !samples[id].data) return 0;
    float reach = reach_of(id);
    if (placed && falls_off()) level = snd_SfxVolume * distance_gain(listener_distance(x, y) / reach);
    int handle = start_sample(samples[id], level, separation, pitch, cutoff, placed ? there : 0, 1, 1, 0, 0);
    SDL_LockAudioStream(stream);
    mix_channel_t *channel = find(handle);
    if (channel) { channel->placed = placed; channel->x = x; channel->y = y; channel->reach = reach; }
    SDL_UnlockAudioStream(stream);
    return handle;
}
int I_StartNamedSound(const char *logical,int volume,int separation,int pitch,int priority)
{
    if(!strcmp(logical,"DuctTape/Toasty")) {
        sample_t sample={toasty_voice,sizeof(toasty_voice),22050,NULL,0};
        int handle=start_sample(sample,volume,128,128,OCCLUSION_OPEN_HZ,0,1,1,0x544f4153u,1);
        if(M_CheckParm("-modsoundtrace"))fprintf(stderr,"Toasty audio: handle %d, %d samples at %d Hz, volume %d\n",handle,sample.length,sample.rate,volume);
        return handle;
    }
    (void)priority;static unsigned choice=1;char error[512];mod_sound_view view;
    choice=choice*1664525u+1013904223u;
    if(!ModSounds_Get(logical,choice,&view,error,sizeof(error))) {fprintf(stderr,"Named sound: %s\n",error);return 0;}
    sample_t sample={0};
    if(view.fallback_lump) {
        int lump=W_CheckNumForName((char*)view.fallback_lump);if(lump<0)return 0;
        int length=W_LumpLength(lump);const byte *data=W_CacheLumpNum(lump,PU_SOUND);
        if(length<8||little16(data)!=3||little32(data+4)>(unsigned)(length-8))return 0;
        sample=(sample_t){data+8,(int)little32(data+4),(int)little16(data+2),NULL,0};
    } else sample=(sample_t){NULL,view.frames,view.rate,view.pcm,view.channels};
    int handle=start_sample(sample,volume,separation,pitch,next_cutoff,next_beyond,1,view.volume,view.group,view.limit);
    next_cutoff=OCCLUSION_OPEN_HZ;next_beyond=0;next_placed=0;
    if(handle){
        ++named_starts;
        if(M_CheckParm("-modsoundtrace"))fprintf(stdout,"Named sound played: %s (%d Hz, %d frames, %s)\n",logical,sample.rate,sample.length,view.pcm?"decoded PK3 audio":"IWAD audio");
    }
    return handle;
}
static int fluid_weapon_sound(int id,int volume,int separation,int pitch,int priority,int weapon)
{
    if(!P_FluidWeaponsEnabled()&&!fluid_sound_addon)return -1;
    const char *name=NULL;
    switch(id) {
        case sfx_sgcock:
            if(P_FluidWeaponsEnabled()&&ModSounds_Has("MRIntW/HQ/ShotgunPumpDown"))return 0;
            name="ShotgunPump";break;
        case sfx_pistol:name=weapon==wp_chaingun?"ChaingunFire":"PistolFire";break;
        case sfx_shotgn:name="ShotgunFire";break;
        case sfx_dshtgn:name="SSGFire";break;
        case sfx_dbopn:name="SSGOpen";break;
        case sfx_dbload:name="SSGLoad";break;
        case sfx_dbcls:name="SSGClose";break;
        case sfx_rlaunc:name="RLFire";break;
        case sfx_plasma:name="PlasmaFire";break;
        case sfx_bfg:name="BFGFireStart";break;
        case sfx_punch:name="FistPuch";break;
        case sfx_sawup:name="SawUp";break;
        case sfx_sawidl:name="SawIdle";break;
        case sfx_sawful:name="SawFull";break;
        case sfx_sawhit:name="SawHit";break;
        default:return -1;
    }
    char logical[128];snprintf(logical,sizeof(logical),"MRIntW/HQ/%s",name);
    if(!ModSounds_Has(logical))snprintf(logical,sizeof(logical),"MRIntW/%s",name);
    if(!ModSounds_Has(logical))return -1;
    return I_StartNamedSound(logical,volume,separation,pitch,priority);
}
/* A made-up sound at a map point, loud and panned as DOOM does it for a
   thing there (S_AdjustSoundParams, or the natural falloff), muffled like
   any effect. level scales the effects volume; reach how much farther than
   DOOM's sounds it carries, as a louder sound would. Made-up sounds are
   placed once: those that follow a thing are short. most caps the treble,
   for a sound heard through a wall whatever the way to it. Personal
   movement keeps source loudness and the effects volume linear in its
   room send; only distance makes it relatively wetter. */
static void play_through(const made_sample_t *sample, float x, float y, float z, float level, float reach, float most, int personal)
{
    const mobj_t *listener = players[consoleplayer].mo;
    if (!stream || !sample || !listener || gamestate != GS_LEVEL || level <= 0) return;
    float dx = x - listener->x / (float)FRACUNIT, dy = y - listener->y / (float)FRACUNIT;
    float distance = listener_distance(x, y);
    if (distance > 1200 * reach) return;
    float volume = snd_SfxVolume * level * distance_gain(distance / reach);
    float source = SDL_clamp(snd_SfxVolume, 0, 15) / 15.0f * level;
    float send_scale = personal ? sqrtf(SDL_clamp(source, 0.0f, 1.0f)) : 1;
    /* A sound at the listener (their own steps) has no direction: centred,
       turning toward where it is over the first 64 units. */
    float angle = atan2f(dy, dx) - listener->angle * (2 * SDL_PI_F / 4294967296.0f);
    int separation = 128 - (int)(96 * sinf(angle) * SDL_min(distance / 64, 1.0f));
    float cutoff = SDL_min(most, Occlusion_Cutoff((fixed_t)(x * FRACUNIT), (fixed_t)(y * FRACUNIT), (fixed_t)(z * FRACUNIT),
                                                  muffle_walls, muffle_distance));
    start_sample((sample_t){ sample->data, sample->length, sample->rate }, volume, separation, 128, cutoff,
                 Beyond_Share(&beyond, x, y), send_scale, 1, 0, 0);
}

static void play_at(const made_sample_t *sample, float x, float y, float z, float level, float reach)
{
    play_through(sample, x, y, z, level, reach, OCCLUSION_OPEN_HZ, 0);
}

/* A variation of the kind, never one of the last two of it. */
static void play_kind(int kind, float x, float y, float z, float level, float reach, int personal)
{
    static int last[IMPACT_KINDS][2], started;
    static unsigned turn = 0x1234567u;
    if (kind < 0 || kind >= IMPACT_KINDS) return;
    if (!started) { started = 1; for (int k = 0; k < IMPACT_KINDS; ++k) last[k][0] = last[k][1] = -1; }
    int count = MadeBank_Variants(kind), variant;
    do { turn = turn * 1103515245u + 12345u; variant = (int)((turn >> 16) % (unsigned)count); }
    while (variant == last[kind][0] || variant == last[kind][1]);
    last[kind][1] = last[kind][0];
    last[kind][0] = variant;
    play_through(MadeBank_Impact(kind, variant), x, y, z, level, reach, OCCLUSION_OPEN_HZ, personal);
}
void I_MadeSound(int kind, float x, float y, float z)
{
    if (!made_impacts || kind < 0 || kind >= IMPACT_KINDS) return;
    if (glass_fracture && kind == IMPACT_GLASS_BREAK) kind = IMPACT_GLASS_FRACTURE;
    if (glass_fracture && kind == IMPACT_GLASS_BIG) kind = IMPACT_GLASS_FRACTURE_BIG;
    float level = kind <= IMPACT_SPARK_ZAP ? 0.6f : kind <= IMPACT_SCREEN_DIES ? 0.7f
                : kind == IMPACT_GLASS_TINKLE ? 0.6f : kind == IMPACT_GORE_BURST ? 0.5f
                : kind == IMPACT_GORE_SLAP ? 0.7f : 1.0f;
    /* A window breaking is loud: heard across a courtyard, where an
       enemy's stray shot can take it out. */
    int breaking = kind >= IMPACT_GLASS_SHATTER && kind <= IMPACT_GLASS_FRACTURE_BIG;
    play_kind(kind, x, y, z, level, breaking ? 2.0f : 1.0f, 0);
}
void I_AmbienceSilence(float x, float y, float z, float radius) { Ambience_Silence(x, y, z, radius); }
void I_AmbienceGlaze(int line, int whole) { Ambience_Glaze(line, whole); }
void I_AmbienceShowGlass(int shown) { Ambience_ShowGlass(shown); }
void I_AmbienceRain(float amount) { Ambience_Rain(amount); }
void I_SetMadeSounds(int impacts, int glass, int ambient, float amount, int steps)
{
    if (stream) SDL_LockAudioStream(stream);
    made_impacts = impacts != 0;
    footsteps = steps != 0;
    glass_fracture = glass == 1;
    if (!ambient && ambience) AmbienceVoices_Clear();
    ambience = ambient != 0;
    ambience_amount = SDL_clamp(amount, 0.0f, 2.0f);
    if (stream) SDL_UnlockAudioStream(stream);
}
void I_SetAlternativeComputerSounds(int enabled)
{
    if (stream) SDL_LockAudioStream(stream);
    AmbienceVoices_AlternativeComputer(enabled);
    if (stream) SDL_UnlockAudioStream(stream);
}
void I_StopSound(int handle)
{
    if (!stream) return;
    SDL_LockAudioStream(stream);
    for (int i = 0; i < MIX_CHANNELS; ++i)
        if (channels[i].handle == handle) channels[i].handle = 0;
    SDL_UnlockAudioStream(stream);
}
int I_SoundIsPlaying(int handle)
{
    if (!stream || !handle) return 0;
    int playing = 0;
    SDL_LockAudioStream(stream);
    for (int i = 0; i < MIX_CHANNELS; ++i)
        if (channels[i].handle == handle) playing = 1;
    SDL_UnlockAudioStream(stream);
    return playing;
}
void I_UpdateSoundParams(int handle, int volume, int separation, int pitch)
{
    if (!stream) return;
    SDL_LockAudioStream(stream);
    for (int i = 0; i < MIX_CHANNELS; ++i)
    {
        mix_channel_t *channel = &channels[i];
        if (channel->handle != handle) continue;
        float level = channel->placed && falls_off()
            ? snd_SfxVolume * distance_gain(listener_distance(channel->x, channel->y) / channel->reach) : (float)volume;
        gains(channel, level, separation, pitch);
    }
    SDL_UnlockAudioStream(stream);
}

/* OPL and SoundFont music render inside the mixer above, so these take
   the stream lock. */
static void lock_music(void) { if (stream) SDL_LockAudioStream(stream); }
static void unlock_music(void) { if (stream) SDL_UnlockAudioStream(stream); }

void I_Mixer_UnRegisterSong(void) { lock_music(); MixerMusic_UnRegister(); unlock_music(); }
void I_Mixer_SetMusicVolume(int volume) { lock_music(); MixerMusic_SetVolume(volume); unlock_music(); }
void I_Mixer_PauseSong(void) { lock_music(); MixerMusic_Pause(); unlock_music(); }
void I_Mixer_ResumeSong(void) { lock_music(); MixerMusic_Resume(); unlock_music(); }
void I_Mixer_StopSong(void) { lock_music(); MixerMusic_Stop(); unlock_music(); }
void I_Mixer_PlaySong(int looping) { lock_music(); MixerMusic_Play(looping != 0); unlock_music(); }
int I_Mixer_RegisterSong(void *data, int soundfont)
{
    int size = 0;
    if (!stream) return 0;
    for (int i = 0; i < numlumps; ++i)
        if (lumpcache[i] == data) { size = W_LumpLength(i); break; }
    lock_music();
    int registered = MixerMusic_Register(data, size, OUTPUT_RATE, soundfont != 0);
    unlock_music();
    return registered;
}

/* Reading a bank takes a while, so only the swap holds the lock. */
int I_Mixer_LoadSoundFont(const char *path)
{
    sf2_bank_t *bank = SF2_Open(path, OUTPUT_RATE, SF2_VOICES);
    if (!bank) return 0;
    lock_music();
    sf2_bank_t *last = MixerMusic_UseBank(bank);
    unlock_music();
    SF2_Close(last);
    return 1;
}

