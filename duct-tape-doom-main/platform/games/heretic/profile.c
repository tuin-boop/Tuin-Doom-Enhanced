/* Heretic's answers to platform/game_profile.h. */
#include <math.h>
#include "doomdef.h"
#include "p_local.h"
#include "sounds.h"
#include "i_sound.h"
#include "../../game_profile.h"
#include "../../wad_probe.h"

const char *Game_Title(void) { return "Heretic"; }
const char *Game_ConfigFile(void) { return "heretic.cfg"; }
int Game_WadKind(void) { return DOOM_WAD_HERETIC; }
const char *Game_ConfigDefaults(void)
{
    return "key_up 119\nkey_down 115\nkey_strafeleft 97\nkey_straferight 100\n"
           "key_use 32\nkey_fire 157\nmouse_sensitivity 5\n"
           "sfx_volume 10\nmusic_volume 7\nsnd_channels 8\n";
}

int Game_ThinkerIsMobj(const struct thinker_s *thinker)
{
    return thinker->function == (think_t)P_MobjThinker;
}

void Game_LightSwings(game_swing_visit visit, void *data)
{
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next)
    {
        const sector_t *sector = NULL;
        int low = 0, high = 0, smooth = 0;
        if (th->function == (think_t)T_LightFlash)
        { lightflash_t *l = (lightflash_t *)th; sector = l->sector; low = l->minlight; high = l->maxlight; }
        else if (th->function == (think_t)T_StrobeFlash)
        { strobe_t *l = (strobe_t *)th; sector = l->sector; low = l->minlight; high = l->maxlight; }
        else if (th->function == (think_t)T_Glow)
        { glow_t *l = (glow_t *)th; sector = l->sector; low = l->minlight; high = l->maxlight; smooth = 1; }
        if (sector && high > low) visit((int)(sector - sectors), low, high, smooth, data);
    }
}

game_thing_role Game_ThingRole(int type)
{
    switch (type)
    {
    case MT_PHOENIXFX1: return GAME_THING_ROCKET;
    case MT_HORNRODFX2: return GAME_THING_BIG_SHOT;
    case MT_BLOOD: return GAME_THING_BLOOD;
    case MT_TFOG: return GAME_THING_TELEPORT_FOG;
    case MT_POD: return GAME_THING_BARREL;
    default: return GAME_THING_OTHER;
    }
}

/* A pod bursts in four frames and explodes on the third (A_Explode in
   S_POD_DIE3), so its frames take the barrel's places 1 to 4. */
int Game_BlastFrame(const struct mobj_s *thing)
{
    int state;
    if (thing->type != MT_POD || !thing->state) return -1;
    state = (int)(thing->state - states);
    return state >= S_POD_DIE1 && state <= S_POD_DIE4 ? 1 + state - S_POD_DIE1 : -1;
}

void Game_MuzzleFlash(int weapon, float *radius, float *strength, int *flashstate)
{
    if (weapon == wp_phoenixrod || weapon == wp_skullrod) { *radius = 480; *strength = 2.6f; }
    else if (weapon == wp_crossbow || weapon == wp_blaster) { *radius = 448; *strength = 2.4f; }
    *flashstate = weapon >= 0 && weapon < NUMWEAPONS && wpnlev1info[weapon].flashstate
        ? wpnlev1info[weapon].flashstate : -1;
}

int Game_WeaponHeft(int weapon)
{
    if (weapon == wp_phoenixrod || weapon == wp_mace) return 2;
    if (weapon == wp_crossbow || weapon == wp_blaster || weapon == wp_skullrod) return 1;
    return 0;
}

/* lookdir 173 aims at slope 1 (P_SpawnPlayerMissile); -110 to 90 as the
   look keys allow. Mouselook adds to it, keeping the fraction for next time. */
int Game_LookPitch(float *pitch)
{
    *pitch = atanf(players[displayplayer].lookdir/173.0f);
    return 1;
}

void Game_Look(float delta)
{
    static float rest;
    player_t *player = &players[consoleplayer];
    float target = tanf(atanf(player->lookdir/173.0f)+delta)*173.0f+rest;
    int look;
    if (target > 90) target = 90;
    if (target < -110) target = -110;
    look = (int)lroundf(target);
    rest = target-look;
    player->lookdir = look;
    player->centering = false;
}

int Game_Sprite(game_sprite_role role)
{
    switch (role)
    {
    case GAME_SPRITE_BLOOD: return SPR_BLOD;
    case GAME_SPRITE_PUFF: return SPR_PUF2;
    case GAME_SPRITE_EXPLOSION: return SPR_XPL1;
    case GAME_SPRITE_TORCH: return SPR_TRCH;
    }
    return -1;
}

/* No glass in the windows, nothing to shoot out (torches and gems aren't
   bulbs), and no flashlight: the Torch item is Heretic's light. No made-up
   wind either: Heretic places its own wind sounds (MT_SOUNDWIND), and what
   the game has itself wins. Its aiming is its own too: autoaim, else the
   lookdir. */
int Game_HasEffect(game_effect effect)
{
    switch (effect)
    {
    case GAME_EFFECT_WINDOW_GLASS: case GAME_EFFECT_SHOOTABLE_LIGHTS: case GAME_EFFECT_FLASHLIGHT:
    case GAME_EFFECT_WIND: case GAME_EFFECT_NATURAL_FALLOFF: case GAME_EFFECT_FREE_AIM:
    case GAME_EFFECT_AUTHORED_EPISODES: return 0;
    }
    return 1;
}

/* The Torch (P_PlayerThink) sets colormaps 1 (brightest) to 7, wandering
   between them; running out it blinks to 0, which puts the flame out. */
int Game_Torch(float *flame)
{
    const player_t *player = &players[displayplayer];
    int map = player->fixedcolormap;
    *flame = 0;
    if (!player->powers[pw_infrared] || map < 0 || map > 7) return 0;
    *flame = map ? 1 - 0.06f * (map - 1) : 0;
    return 1;
}

/* The sun stands where the sky paints it (most of Heretic's skies glow low
   on the horizon), sunlit stone stands out from the shade, the light takes
   the sky's colour (grey, hell red, night blue), and distance fades into it. */
void Game_Sun(game_sun *sun) { *sun = (game_sun){ 1, 0.4f, 0.3f, 0.6f, 1 }; }

/* Heretic's maps keep the light they were made with. */
/* No authored episodes: Heretic keeps its derived look. */
void Game_Mood(int episode, int map, game_mood *mood) { (void)episode; (void)map; (void)mood; }
int Game_BrokenWindows(int episode, int map, game_spot *spots, int max) { (void)episode; (void)map; (void)spots; (void)max; return 0; }

/* Heretic's pools are clear and still: a fantasy mirror, not sewage. */
float Game_WaterClarity(void) { return 1; }

/* Heretic's palette has its own ramps, and its grey is mostly stone. */
int Game_SurfaceGloss(void) { return 1; }

/* Specials 40-51 (P_XYMovement): east, north, south, west, each light,
   medium and strong, as windTab's 5, 10 and 25 thrust. */
int Game_SectorWind(int special, float *east, float *north)
{
    static const float speed[3] = { 40, 80, 200 };
    float s;
    *east = *north = 0;
    if (special < 40 || special > 51) return 0;
    s = speed[(special - 40) % 3];
    switch ((special - 40) / 3)
    {
    case 0: *east = s; break;
    case 1: *north = s; break;
    case 2: *north = -s; break;
    default: *east = -s; break;
    }
    return 1;
}

/* The chandeliers' candles, the serpent torches' flames and the glowing
   orbs over the key statues; wall torches and braziers are fullbright. */
int Game_DecorationLight(int sprite)
{
    switch (sprite)
    {
    case SPR_CHDL: case SPR_SRTC: case SPR_KGZB: case SPR_KGZG: case SPR_KGZY: return 1;
    default: return 0;
    }
}

game_decoration Game_Decoration(int sprite)
{
    switch (sprite)
    {
    case SPR_CHDL: return GAME_DECORATION_CANDLES;
    case SPR_SRTC: case SPR_KFR1: return GAME_DECORATION_TORCH;
    case SPR_WTRH: return GAME_DECORATION_SHORT_TORCH;
    default: return GAME_DECORATION_NONE;
    }
}

/* Heretic names its effects by their full lump names. */
int I_GetSfxLumpNum(sfxinfo_t *sfx)
{
    int lump;
    if (sfx->link) sfx = sfx->link;
    lump = W_CheckNumForName(sfx->name);
    return lump >= 0 ? lump : W_GetNumForName("gldhit");
}

void Game_SfxLoaded(int id, void *data)
{
    S_sfx[id].snd_ptr = data;
}

float Game_SoundCarry(int id) { (void)id; return 1; }

/* Heretic's traces aren't hooked yet: no near misses. */
void Game_TraceShots(game_shot_traced traced) { (void)traced; }

/* FONTA, the menus' small font: FONTA_S, then '!' onward. Heretic draws
   it one pixel tighter than the patches are wide (MN_DrTextA). */
int Game_FontLump(int c)
{
    int start = W_CheckNumForName("FONTA_S"), end = W_CheckNumForName("FONTA_E");
    int lump = start + 1 + c - '!';
    return start >= 0 && end > lump && c >= '!' ? lump : -1;
}

int Game_FontSpacing(void) { return -1; }

int Game_MenuSound(game_menu_sound sound)
{
    switch (sound)
    {
    case GAME_MENU_MOVE: return sfx_switch;
    case GAME_MENU_DENY: return sfx_plroof;
    case GAME_MENU_CHANGE: return sfx_stnmov;
    case GAME_MENU_OPEN: return sfx_dorcls;
    case GAME_MENU_CLOSE: return sfx_dorcls;
    }
    return sfx_None;
}
