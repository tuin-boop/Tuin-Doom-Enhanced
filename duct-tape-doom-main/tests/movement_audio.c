/* Exercise the real movement producer and SDL mixer without a WAD or audio
   device. Banks and acoustics are fixtures; samples still traverse the real
   routing, channel allocation, gains, filtering and stereo mixer. */
#include "../platform/audio/steps.c"
#include "../platform/i_sdl_sound.c"

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "Check failed at line %d: %s\n", __LINE__, #condition); \
    exit(1); } } while (0)
enum { FRAMES = 128, SAMPLE_FRAMES = 512 };
int gametic = 100, consoleplayer = 1, gamemap = 1, snd_SfxVolume = 15;
int numflats = 2, firstflat, numlumps;
unsigned r_levelserial = 1;
gamestate_t gamestate = GS_LEVEL;
player_t players[MAXPLAYERS];
thinker_t thinkercap;
lumpinfo_t *lumpinfo;
void **lumpcache;
sfxinfo_t S_sfx[NUMSFX];
static sector_t floors[2];
static subsector_t fixture_subsectors[2];
static mobj_t listener, monster;
static lumpinfo_t flat_names[2];
static byte bank_data[IMPACT_KINDS][SAMPLE_FRAMES];
static made_sample_t bank[IMPACT_KINDS];
static float sends[2][FRAMES];
static int reverb_ids[2];

/* Deterministic external fixtures. The two production files above remain
   intact, including ownership decisions and all public sound entry points. */
int Game_HasEffect(game_effect effect) { return effect == GAME_EFFECT_NATURAL_FALLOFF; }
int Game_ThinkerIsMobj(const thinker_t *thinker) { return thinker == &monster.thinker; }
int Game_WeaponHeft(int weapon) { (void)weapon; return 2; }
float Game_SoundCarry(int id) { (void)id; return 1; }
void Game_SfxLoaded(int id, void *data) { (void)id; (void)data; }
void Game_TraceShots(game_shot_traced traced) { (void)traced; }
game_surface Game_FlatSurface(const char *name) { return name[0] == 'W' ? GAME_SURFACE_LIQUID : GAME_SURFACE_STONE; }
game_liquid Game_LiquidFlat(const char *name) { (void)name; return GAME_LIQUID_WATER; }
int M_CheckParm(char *argument) { (void)argument; return 0; }
int P_FluidWeaponsEnabled(void) { return 0; }
int (*S_WeaponSound)(int,int,int,int,int,int);
int W_CheckNumForName(char *name) { (void)name; return -1; }
int ModSounds_Get(const char *name,unsigned choice,mod_sound_view *out,char *error,int size)
{ (void)name;(void)choice;(void)out;(void)error;(void)size;return 0; }
int ModSounds_Has(const char *name) { (void)name;return 0; }
void ModSounds_UseAddon(int enabled) { (void)enabled; }
void ModSounds_Clear(void) {}
int I_GetSfxLumpNum(sfxinfo_t *sfx) { (void)sfx; return 0; }
int W_LumpLength(int lump) { (void)lump; return 0; }
void *W_CacheLumpNum(int lump, int tag) { (void)lump; (void)tag; return NULL; }
void MadeBank_Build(void) {}
int MadeBank_Variants(int kind) { (void)kind; return MADE_STEP_VARIANTS; }
const made_sample_t *MadeBank_Impact(int kind, int variant) { (void)variant; return &bank[kind]; }
const made_sample_t *MadeBank_Strike(int variant) { (void)variant; return &bank[0]; }
const made_sample_t *MadeBank_Hidden(int kind, int variant) { (void)kind; (void)variant; return &bank[0]; }
void Ambience_Update(ambience_voice_t voices[AMBIENT_KINDS][AMBIENCE_LAYERS], float level, int walls, int distance,
                     ambience_strike_t *strike, ambience_voice_t *body)
{ (void)level; (void)walls; (void)distance; memset(voices, 0, sizeof(ambience_voice_t) * AMBIENT_KINDS * AMBIENCE_LAYERS); memset(strike, 0, sizeof(*strike)); memset(body, 0, sizeof(*body)); }
void AmbienceVoices_Set(const ambience_voice_t voices[AMBIENT_KINDS][AMBIENCE_LAYERS]) { (void)voices; }
void AmbienceVoices_SetComputerBody(const ambience_voice_t *body) { (void)body; }
void AmbienceVoices_AlternativeComputer(int enabled) { (void)enabled; }
void AmbienceVoices_Clear(void) {}
void AmbienceVoices_Mix(float *stereo, float *send, int frames, int rate, int mode, float gain, float ducking)
{ (void)stereo; (void)send; (void)frames; (void)rate; (void)mode; (void)gain; (void)ducking; }
void Ambience_Silence(float x, float y, float z, float radius) { (void)x; (void)y; (void)z; (void)radius; }
void Ambience_Glaze(int line, int whole) { (void)line; (void)whole; }
void Ambience_ShowGlass(int shown) { (void)shown; }
void Ambience_Rain(float amount) { (void)amount; }
int Hidden_Tic(hidden_event_t *event) { (void)event; return 0; }
float Occlusion_Cutoff(fixed_t x, fixed_t y, fixed_t z, int walls, int distance)
{ (void)x; (void)y; (void)z; (void)walls; (void)distance; return OCCLUSION_OPEN_HZ; }
void Room_Measure(room_t *room, room_outline_t *outline) { memset(room, 0, sizeof(*room)); memset(outline, 0, sizeof(*outline)); }
void Beyond_Measure(const room_t *room, const room_outline_t *outline, float facing, beyond_t *space)
{ (void)room; (void)outline; (void)facing; space->coupling = 0.3f; }
float Beyond_Share(const beyond_t *space, float x, float y) { (void)space; (void)x; (void)y; return 0.25f; }
int Flyby_Near(const mobj_t *ear, const mobj_t *shooter, const mobj_t *hit,
               float x1, float y1, float z1, float x2, float y2, float z2, float *x, float *y, float *z, float *level)
{ (void)ear; (void)shooter; (void)hit; (void)x1; (void)y1; (void)z1; (void)x2; (void)y2; (void)z2; (void)x; (void)y; (void)z; (void)level; return 0; }
reverb_t *Reverb_New(int rate) { (void)rate; return NULL; }
void Reverb_Clear(reverb_t *reverb) { (void)reverb; }
void Reverb_SetRoom(reverb_t *reverb, const room_t *room) { (void)reverb; (void)room; }
void Reverb_Process(reverb_t *reverb, const float *send, float *stereo, int frames, float amount)
{ (void)stereo; (void)amount; CHECK(frames == FRAMES); memcpy(sends[reverb == beyond_reverb], send, sizeof(sends[0])); }
void MixerMusic_UnRegister(void) {}
void MixerMusic_Play(boolean loop) { (void)loop; }
void MixerMusic_Stop(void) {}
void MixerMusic_Pause(void) {}
void MixerMusic_Resume(void) {}
void MixerMusic_SetVolume(int volume) { (void)volume; }
boolean MixerMusic_Register(const void *data, int size, int rate, boolean soundfont)
{ (void)data; (void)size; (void)rate; (void)soundfont; return false; }
void MixerMusic_Render(float *stereo, int frames) { (void)stereo; (void)frames; }
sf2_bank_t *MixerMusic_UseBank(sf2_bank_t *new_bank) { (void)new_bank; return NULL; }
sf2_bank_t *SF2_Open(const char *path, int rate, int voices) { (void)path; (void)rate; (void)voices; return NULL; }
void SF2_Close(sf2_bank_t *old_bank) { (void)old_bank; }

static int near(float a, float b) { return fabsf(a - b) < 1e-6f; }
static void reset_channels(void)
{
    memset(channels, 0, sizeof(channels));
    limiter_gain = duck = 1;
    coupling = coupling_target = 0.3f;
}
static mix_channel_t *latest(void) { return find(next_handle - 1); }
static void render(float *stereo)
{
    audio(NULL, stream, FRAMES * 2 * sizeof(float), 0);
    CHECK(SDL_GetAudioStreamData(stream, stereo, FRAMES * 2 * sizeof(float)) == FRAMES * 2 * sizeof(float));
}

/* Exact dry samples must survive the policy change, with only the two room
   inputs reduced. This includes real source-layer gains, source distance,
   effects volume, stereo panning, resampling and filtering. */
static void check_mix(void)
{
    float original[2 * FRAMES], personal[2 * FRAMES], original_send[2][FRAMES];
    impact_layer_t layers[3];
    int count = Step_Layers(IMPACT_STEP_METAL, 0, 2, 2, layers);
    CHECK(count == 3);
    for (int l = 0; l < count; ++l)
        for (int volume = 0; volume <= 15; volume += 5)
        {
            snd_SfxVolume = volume;
            reset_channels();
            play_kind(layers[l].kind, 0, 0, 0, layers[l].level, 1, 0);
            render(original); memcpy(original_send, sends, sizeof(sends));
            reset_channels();
            play_kind(layers[l].kind, 0, 0, 0, layers[l].level, 1, 1);
            CHECK(near(latest()->send, SFX_GAIN * volume / 15.0f * layers[l].level));
            render(personal);
            CHECK(memcmp(original, personal, sizeof(original)) == 0);
            float correction = sqrtf(volume / 15.0f * layers[l].level);
            for (int room = 0; room < 2; ++room)
                for (int n = 0; n < FRAMES; ++n)
                    CHECK(near(sends[room][n], original_send[room][n] * correction));
        }
    snd_SfxVolume = 15;
    reset_channels();
    play_kind(IMPACT_STEP_METAL, 640, 0, 0, 0.2f, 1, 1);
    CHECK(near(latest()->send, SFX_GAIN * 0.2f * sqrtf(distance_gain(640))));
}

/* A real tic can contain personal movement and a distant monster splash.
   Their producer ownership must reach the actual mixer, including when
   both sources use exactly the same water-splash sample kind. */
static void check_ownership(void)
{
    int own_steps = 0, own_gear = 0, world_splashes = 0;
    for (int wet_floor = 0; wet_floor <= 1; ++wet_floor)
    {
        floors[0].floorpic = wet_floor;
        for (int tic = 0; tic < 40; ++tic)
        {
            ++gametic;
            listener.x += 8 * FRACUNIT;
            monster.x = listener.x + (640 + tic % 8 * 8) * FRACUNIT;
            reset_channels();
            I_UpdateSound();
            for (int c = 0; c < MIX_CHANNELS; ++c)
            {
                mix_channel_t *channel = &channels[c];
                if (!channel->handle) continue;
                int kind = -1;
                for (int k = 0; k < IMPACT_KINDS; ++k) if (channel->sample.data == bank[k].data) kind = k;
                CHECK(kind >= 0);
                if (near(channel->send_scale, 1))
                {
                    CHECK(kind == IMPACT_SPLASH_WATER);
                    float distance = listener_distance(monster.x / (float)FRACUNIT, 0);
                    CHECK(near(channel->send, SFX_GAIN * sqrtf(0.5f * distance_gain(distance))));
                    ++world_splashes;
                }
                else
                {
                    CHECK(near(channel->send, (channel->left + channel->right) / (1 + 126.0f / 127)));
                    if (kind >= IMPACT_GEAR_LIGHT && kind <= IMPACT_GEAR_ARMOR_RUN) ++own_gear;
                    else { CHECK(kind == (wet_floor ? IMPACT_SPLASH_WATER : IMPACT_STEP_STONE)); ++own_steps; }
                }
            }
        }
    }
    CHECK(own_steps > 0 && own_gear > own_steps && world_splashes > 0);
}

static void check_reuse_and_world(void)
{
    reset_channels();
    play_kind(IMPACT_GEAR_ARMOR, 0, 0, 0, 0.03f, 1, 1);
    CHECK(latest() == &channels[0] && channels[0].send_scale < 1);
    I_StopSound(channels[0].handle);
    samples[1] = (sample_t){ bank[0].data, SAMPLE_FRAMES, MADE_RATE };
    int handle = I_StartSound(1, 3, 80, 128, 0);
    CHECK(find(handle) == &channels[0]);
    CHECK(near(channels[0].send_scale, 1));
    CHECK(near(channels[0].send, SFX_GAIN * sqrtf(3.0f / 15)));
    I_UpdateSoundParams(handle, 6, 150, 128);
    CHECK(near(channels[0].send, SFX_GAIN * sqrtf(6.0f / 15)));
    I_StopSound(handle);
    I_MadeSound(IMPACT_GLASS_TINKLE, listener.x / (float)FRACUNIT, 0, 0);
    CHECK(latest() == &channels[0] && near(channels[0].send_scale, 1));
    CHECK(near(channels[0].send, SFX_GAIN * sqrtf(0.6f)));
    I_StopSound(channels[0].handle);
    play_at(&bank[0], listener.x / (float)FRACUNIT, 0, 0, 0.1f, 1);
    CHECK(near(channels[0].send_scale, 1));
    CHECK(near(channels[0].send, SFX_GAIN * sqrtf(0.1f)));
    I_StopSound(channels[0].handle);
    play_kind(IMPACT_GEAR_ARMOR, listener.x / (float)FRACUNIT, 0, 0, 0.03f, 1, 1);
    CHECK(near(channels[0].send, SFX_GAIN * 0.03f));
    /* The same slot also resets its policy when stolen under full load. */
    for (int i = 1; i < MIX_CHANNELS; ++i) start_sample(samples[1], 15, 128, 128, OCCLUSION_OPEN_HZ, 0, 1, 1, 0, 0);
    CHECK(I_StartSound(1, 8, 128, 128, 0) == channels[0].handle);
    CHECK(near(channels[0].send_scale, 1));
    CHECK(near(channels[0].send, SFX_GAIN * sqrtf(8.0f / 15)));
}

int main(void)
{
    SDL_AudioSpec spec = { SDL_AUDIO_F32, 2, OUTPUT_RATE };
    stream = SDL_CreateAudioStream(&spec, &spec);
    CHECK(stream != NULL);
    room_reverb = (reverb_t *)&reverb_ids[0]; beyond_reverb = (reverb_t *)&reverb_ids[1];
    for (int k = 0; k < IMPACT_KINDS; ++k)
    {
        bank[k] = (made_sample_t){ bank_data[k], SAMPLE_FRAMES, MADE_RATE };
        for (int n = 0; n < SAMPLE_FRAMES; ++n) bank_data[k][n] = (byte)(128 + 8 * sinf(n * 0.3f));
    }
    memcpy(flat_names[0].name, "DRY", 3); memcpy(flat_names[1].name, "WET", 3); lumpinfo = flat_names;
    floors[1].floorpic = 1;
    fixture_subsectors[0].sector = &floors[0]; fixture_subsectors[1].sector = &floors[1];
    listener.subsector = &fixture_subsectors[0]; monster.subsector = &fixture_subsectors[1];
    listener.player = &players[consoleplayer]; players[consoleplayer].mo = &listener;
    players[consoleplayer].armortype = 2;
    monster.flags = MF_SHOOTABLE; monster.health = 100;
    thinkercap.next = &monster.thinker; monster.thinker.next = &thinkercap;
    Resample_Init();
    check_mix(); check_ownership(); check_reuse_and_world();
    SDL_DestroyAudioStream(stream); stream = NULL;
    puts("Movement direct samples unchanged; personal source/volume sends linear; monster/world routing and reused channels preserved.");
    return 0;
}
