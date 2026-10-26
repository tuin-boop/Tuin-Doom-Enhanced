/* DERP: DOOM's i_sound.h, the sound and music device interface that
   platform/i_sdl_sound.c and platform/i_music.c implement. Heretic's own
   s_sound.c drives it. */
#ifndef DERP_HERETIC_I_SOUND_H
#define DERP_HERETIC_I_SOUND_H
#include "doomdef.h"
#include "sounds.h"

// DOOM's names for Heretic's sound table and effects volume (0-15).
extern sfxinfo_t S_sfx[];
extern int snd_MaxVolume;
#define snd_SfxVolume snd_MaxVolume

void I_InitSound(void);
void I_UpdateSound(void);
void I_SubmitSound(void);
void I_ShutdownSound(void);
void I_SetChannels(void);
int I_GetSfxLumpNum(sfxinfo_t *sfxinfo);
int I_StartSound(int id, int vol, int sep, int pitch, int priority);
void I_StopSound(int handle);
int I_SoundIsPlaying(int handle);
void I_UpdateSoundParams(int handle, int vol, int sep, int pitch);
// Where a positioned sound is in the map, for muffling and distance.
// Handle 0 places the sound the next I_StartSound starts.
void I_SoundAt(int handle, fixed_t x, fixed_t y, fixed_t z);

void I_InitMusic(void);
void I_ShutdownMusic(void);
void I_SetMusicVolume(int volume);
void I_PauseSong(int handle);
void I_ResumeSong(int handle);
int I_RegisterSong(void *data);
void I_PlaySong(int handle, int looping);
void I_StopSong(int handle);
void I_UnRegisterSong(int handle);
#endif
