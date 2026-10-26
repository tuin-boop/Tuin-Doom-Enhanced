// Heretic source code, Copyright Activision 1996-1998 (Raven Software).
// Released under the GNU General Public License; see THIRD_PARTY.md.
// The music and sound effects API, split out of Raven's DOS I_IBM.C.

#include <stdlib.h>
#include "doomdef.h"
#include "r_local.h"
#include "p_local.h"
#include "sounds.h"
#include "i_sound.h"
#include "soundst.h"

// DERP: DMX took volumes 0-127; DERP's mixer takes DOOM's 0-15 effects scale.
#define MIXER_VOLUME(v) (((v)+4)>>3)

boolean S_StopSoundID(int sound_id, int priority);

extern void **lumpcache;

/*
===============================================================================

		MUSIC & SFX API

===============================================================================
*/

static channel_t channel[MAX_CHANNELS];

static int rs; //the current registered song.
int mus_song = -1;
int mus_lumpnum;
void *mus_sndptr;
byte *soundCurve;

extern sfxinfo_t S_sfx[];
extern musicinfo_t S_music[];

extern int snd_DesiredMusicDevice;
extern int snd_DesiredSfxDevice;
extern int snd_MaxVolume;
extern int snd_MusicVolume;
extern int snd_Channels;

extern int startepisode;
extern int startmap;

int AmbChan;

void S_Start(void)
{
	int i;

	S_StartSong((gameepisode-1)*9 + gamemap-1, true);

	//stop all sounds
	for(i=0; i < snd_Channels; i++)
	{
		if(channel[i].handle)
		{
			S_StopSound(channel[i].mo);
		}
	}
	memset(channel, 0, 8*sizeof(channel_t));
}

void S_StartSong(int song, boolean loop)
{
	if(song == mus_song)
	{ // don't replay an old song
		return;
	}
	if(rs)
	{
		I_StopSong(rs);
		I_UnRegisterSong(rs);
		Z_ChangeTag(lumpcache[mus_lumpnum], PU_CACHE);
		#ifdef __WATCOMC__
			_dpmi_unlockregion(mus_sndptr, lumpinfo[mus_lumpnum].size);
		#endif
	}
	if(song < mus_e1m1 || song > NUMMUSIC)
	{
		return;
	}
	mus_lumpnum = W_GetNumForName(S_music[song].name);
	mus_sndptr = W_CacheLumpNum(mus_lumpnum, PU_MUSIC);
	#ifdef __WATCOMC__
		_dpmi_lockregion(mus_sndptr, lumpinfo[mus_lumpnum].size);
	#endif
	rs = I_RegisterSong(mus_sndptr);
	I_PlaySong(rs, loop); //'true' denotes endless looping.
	mus_song = song;
}

void S_StartSound(mobj_t *origin, int sound_id)
{
	int dist, vol;
	int i;
	int sound;
	int priority;
	int sep;
	int angle;
	int absx;
	int absy;

	static int sndcount = 0;
	int chan;

	if(sound_id==0 || snd_MaxVolume == 0)
		return;
	if(origin == NULL)
	{
		origin = players[consoleplayer].mo;
	}
	// DERP: on the title screen and between levels there is no player to
	// hear from, and DOS read the null pointer's memory instead of crashing.
	// Those sounds come from the listener, like menu sounds.
	if(origin == NULL || players[consoleplayer].mo == NULL)
	{
		S_StartSoundAtVolume(NULL, sound_id, 127);
		return;
	}

// calculate the distance before other stuff so that we can throw out
// sounds that are beyond the hearing range.
	absx = abs(origin->x-players[consoleplayer].mo->x);
	absy = abs(origin->y-players[consoleplayer].mo->y);
	dist = absx+absy-(absx > absy ? absy>>1 : absx>>1);
	dist >>= FRACBITS;
//  dist = P_AproxDistance(origin->x-viewx, origin->y-viewy)>>FRACBITS;

	if(dist >= MAX_SND_DIST)
	{
//      dist = MAX_SND_DIST - 1;
	  return; //sound is beyond the hearing range...
	}
	if(dist < 0)
	{
		dist = 0;
	}
	priority = S_sfx[sound_id].priority;
	priority *= (10 - (dist/160));
	if(!S_StopSoundID(sound_id, priority))
	{
		return; // other sounds have greater priority
	}
	for(i=0; i<snd_Channels; i++)
	{
		if(origin->player)
		{
			i = snd_Channels;
			break; // let the player have more than one sound.
		}
		if(origin == channel[i].mo)
		{ // only allow other mobjs one sound
			S_StopSound(channel[i].mo);
			break;
		}
	}
	if(i >= snd_Channels)
	{
		if(sound_id >= sfx_wind)
		{
			if(AmbChan != -1 && S_sfx[sound_id].priority <=
				S_sfx[channel[AmbChan].sound_id].priority)
			{
				return; //ambient channel already in use
			}
			else
			{
				AmbChan = -1;
			}
		}
		for(i=0; i<snd_Channels; i++)
		{
			if(channel[i].mo == NULL)
			{
				break;
			}
		}
		if(i >= snd_Channels)
		{
			//look for a lower priority sound to replace.
			sndcount++;
			if(sndcount >= snd_Channels)
			{
				sndcount = 0;
			}
			for(chan=0; chan < snd_Channels; chan++)
			{
				i = (sndcount+chan)%snd_Channels;
				if(priority >= channel[i].priority)
				{
					chan = -1; //denote that sound should be replaced.
					break;
				}
			}
			if(chan != -1)
			{
				return; //no free channels.
			}
			else //replace the lower priority sound.
			{
				if(channel[i].handle)
				{
					if(I_SoundIsPlaying(channel[i].handle))
					{
						I_StopSound(channel[i].handle);
					}
					if(S_sfx[channel[i].sound_id].usefulness > 0)
					{
						S_sfx[channel[i].sound_id].usefulness--;
					}

					if(AmbChan == i)
					{
						AmbChan = -1;
					}
				}
			}
		}
	}
	if(S_sfx[sound_id].lumpnum == 0)
	{
		S_sfx[sound_id].lumpnum = I_GetSfxLumpNum(&S_sfx[sound_id]);
	}
	if(S_sfx[sound_id].snd_ptr == NULL)
	{
		S_sfx[sound_id].snd_ptr = W_CacheLumpNum(S_sfx[sound_id].lumpnum,
			PU_SOUND);
		#ifdef __WATCOMC__
		_dpmi_lockregion(S_sfx[sound_id].snd_ptr,
			lumpinfo[S_sfx[sound_id].lumpnum].size);
		#endif
	}

	// calculate the volume based upon the distance from the sound origin.
//      vol = (snd_MaxVolume*16 + dist*(-snd_MaxVolume*16)/MAX_SND_DIST)>>9;
	vol = soundCurve[dist];

	if(origin == players[consoleplayer].mo)
	{
		sep = 128;
	}
	else
	{
		// DERP: panned from the origin; Raven read channel[i].mo here, which
		// is the channel's previous sound and is often null.
		angle = R_PointToAngle2(players[consoleplayer].mo->x,
			players[consoleplayer].mo->y, origin->x, origin->y);
		angle = (angle-viewangle)>>24;
		sep = angle*2-128;
		if(sep < 64)
			sep = -sep;
		if(sep > 192)
			sep = 512-sep;
	}

	channel[i].pitch = (byte)(127+(M_Random()&7)-(M_Random()&7));
	I_SoundAt(0, origin->x, origin->y, origin->z);
	channel[i].handle = I_StartSound(sound_id, MIXER_VOLUME(vol), sep, channel[i].pitch, 0);
	channel[i].mo = origin;
	channel[i].sound_id = sound_id;
	channel[i].priority = priority;
	if(sound_id >= sfx_wind)
	{
		AmbChan = i;
	}
	if(S_sfx[sound_id].usefulness == -1)
	{
		S_sfx[sound_id].usefulness = 1;
	}
	else
	{
		S_sfx[sound_id].usefulness++;
	}
}

void S_StartSoundAtVolume(mobj_t *origin, int sound_id, int volume)
{
	int dist;
	int i;
	int sep;

	static int sndcount;
	int chan;

	if(sound_id == 0 || snd_MaxVolume == 0)
		return;
	if(origin == NULL)
	{
		origin = players[consoleplayer].mo;
	}

	if(volume == 0)
	{
		return;
	}
	volume = (volume*(snd_MaxVolume+1)*8)>>7;

// no priority checking, as ambient sounds would be the LOWEST.
	for(i=0; i<snd_Channels; i++)
	{
		if(channel[i].mo == NULL)
		{
			break;
		}
	}
	if(i >= snd_Channels)
	{
		return;
	}
	if(S_sfx[sound_id].lumpnum == 0)
	{
		S_sfx[sound_id].lumpnum = I_GetSfxLumpNum(&S_sfx[sound_id]);
	}
	if(S_sfx[sound_id].snd_ptr == NULL)
	{
		S_sfx[sound_id].snd_ptr = W_CacheLumpNum(S_sfx[sound_id].lumpnum,
			PU_SOUND);
		#ifdef __WATCOMC__
		_dpmi_lockregion(S_sfx[sound_id].snd_ptr,
			lumpinfo[S_sfx[sound_id].lumpnum].size);
		#endif
	}
	channel[i].pitch = (byte)(127-(M_Random()&3)+(M_Random()&3));
	if(origin)
	{
		I_SoundAt(0, origin->x, origin->y, origin->z);
	}
	channel[i].handle = I_StartSound(sound_id, MIXER_VOLUME(volume), 128, channel[i].pitch, 0);
	channel[i].mo = origin;
	channel[i].sound_id = sound_id;
	channel[i].priority = 1; //super low priority.
	if(S_sfx[sound_id].usefulness == -1)
	{
		S_sfx[sound_id].usefulness = 1;
	}
	else
	{
		S_sfx[sound_id].usefulness++;
	}
}

boolean S_StopSoundID(int sound_id, int priority)
{
	int i;
	int lp; //least priority
	int found;

	if(S_sfx[sound_id].numchannels == -1)
	{
		return(true);
	}
	lp = -1; //denote the argument sound_id
	found = 0;
	for(i=0; i<snd_Channels; i++)
	{
		if(channel[i].sound_id == sound_id && channel[i].mo)
		{
			found++; //found one.  Now, should we replace it??
			if(priority >= channel[i].priority)
			{ // if we're gonna kill one, then this'll be it
				lp = i;
				priority = channel[i].priority;
			}
		}
	}
	if(found < S_sfx[sound_id].numchannels)
	{
		return(true);
	}
	else if(lp == -1)
	{
		return(false); // don't replace any sounds
	}
	if(channel[lp].handle)
	{
		if(I_SoundIsPlaying(channel[lp].handle))
		{
			I_StopSound(channel[lp].handle);
		}
		if(S_sfx[channel[i].sound_id].usefulness > 0)
		{
			S_sfx[channel[i].sound_id].usefulness--;
		}
		channel[lp].mo = NULL;
	}
	return(true);
}

void S_StopSound(mobj_t *origin)
{
	int i;

	for(i=0;i<snd_Channels;i++)
	{
		if(channel[i].mo == origin)
		{
			I_StopSound(channel[i].handle);
			if(S_sfx[channel[i].sound_id].usefulness > 0)
			{
				S_sfx[channel[i].sound_id].usefulness--;
			}
			channel[i].handle = 0;
			channel[i].mo = NULL;
			if(AmbChan == i)
			{
				AmbChan = -1;
			}
		}
	}
}

void S_SoundLink(mobj_t *oldactor, mobj_t *newactor)
{
	int i;

	for(i=0;i<snd_Channels;i++)
	{
		if(channel[i].mo == oldactor)
			channel[i].mo = newactor;
	}
}

void S_PauseSound(void)
{
	I_PauseSong(rs);
}

void S_ResumeSound(void)
{
	I_ResumeSong(rs);
}

static int nextcleanup;

void S_UpdateSounds(mobj_t *listener)
{
	int i, dist, vol;
	int angle;
	int sep;
	int priority;
	int absx;
	int absy;

	listener = players[consoleplayer].mo;
	if(snd_MaxVolume == 0)
	{
		return;
	}
	// DERP: nothing to place sounds around until a level has a player.
	if(listener == NULL)
	{
		return;
	}
	// DERP: no purging of unused sounds; the mixer keeps every effect it
	// loaded at startup, so their lumps must stay in the zone.
	for(i=0;i<snd_Channels;i++)
	{
		if(!channel[i].handle || S_sfx[channel[i].sound_id].usefulness == -1)
		{
			continue;
		}
		if(!I_SoundIsPlaying(channel[i].handle))
		{
			if(S_sfx[channel[i].sound_id].usefulness > 0)
			{
				S_sfx[channel[i].sound_id].usefulness--;
			}
			channel[i].handle = 0;
			channel[i].mo = NULL;
			channel[i].sound_id = 0;
			if(AmbChan == i)
			{
				AmbChan = -1;
			}
		}
		if(channel[i].mo == NULL || channel[i].sound_id == 0
			|| channel[i].mo == players[consoleplayer].mo)
		{
			continue;
		}
		else
		{
			absx = abs(channel[i].mo->x-players[consoleplayer].mo->x);
			absy = abs(channel[i].mo->y-players[consoleplayer].mo->y);
			dist = absx+absy-(absx > absy ? absy>>1 : absx>>1);
			dist >>= FRACBITS;
//          dist = P_AproxDistance(channel[i].mo->x-listener->x, channel[i].mo->y-listener->y)>>FRACBITS;

			if(dist >= MAX_SND_DIST)
			{
				S_StopSound(channel[i].mo);
				continue;
			}
			if(dist < 0)
				dist = 0;

// calculate the volume based upon the distance from the sound origin.
//          vol = (*((byte *)W_CacheLumpName("SNDCURVE", PU_CACHE)+dist)*(snd_MaxVolume*8))>>7;
			vol = soundCurve[dist];

			angle = R_PointToAngle2(players[consoleplayer].mo->x,
				players[consoleplayer].mo->y, channel[i].mo->x, channel[i].mo->y);
			angle = (angle-viewangle)>>24;
			sep = angle*2-128;
			if(sep < 64)
				sep = -sep;
			if(sep > 192)
				sep = 512-sep;
			I_UpdateSoundParams(channel[i].handle, MIXER_VOLUME(vol), sep, channel[i].pitch);
			I_SoundAt(channel[i].handle, channel[i].mo->x, channel[i].mo->y, channel[i].mo->z);
			priority = S_sfx[channel[i].sound_id].priority;
			priority *= (10 - (dist>>8));
			channel[i].priority = priority;
		}
	}
}

void S_Init(void)
{
	soundCurve = Z_Malloc(MAX_SND_DIST, PU_STATIC, NULL);
	// DERP: I_Init (platform/i_portable_system.c) started the sound device.
	if(snd_Channels > 8)
	{
		snd_Channels = 8;
	}
	I_SetChannels();
	I_SetMusicVolume(snd_MusicVolume);
	S_SetMaxVolume(true);
}

void S_GetChannelInfo(SoundInfo_t *s)
{
	int i;
	ChanInfo_t *c;

	s->channelCount = snd_Channels;
	s->musicVolume = snd_MusicVolume;
	s->soundVolume = snd_MaxVolume;
	for(i = 0; i < snd_Channels; i++)
	{
		c = &s->chan[i];
		c->id = channel[i].sound_id;
		c->priority = channel[i].priority;
		c->name = S_sfx[c->id].name;
		c->mo = channel[i].mo;
		c->distance = P_AproxDistance(c->mo->x-viewx, c->mo->y-viewy)
			>>FRACBITS;
	}
}

void S_SetMaxVolume(boolean fullprocess)
{
	int i;

	if(!fullprocess)
	{
		soundCurve[0] = (*((byte *)W_CacheLumpName("SNDCURVE", PU_CACHE))*(snd_MaxVolume*8))>>7;
	}
	else
	{
		for(i = 0; i < MAX_SND_DIST; i++)
		{
			soundCurve[i] = (*((byte *)W_CacheLumpName("SNDCURVE", PU_CACHE)+i)*(snd_MaxVolume*8))>>7;
		}
	}
}

static boolean musicPaused;
void S_SetMusicVolume(void)
{
	I_SetMusicVolume(snd_MusicVolume);
	if(snd_MusicVolume == 0)
	{
		I_PauseSong(rs);
		musicPaused = true;
	}
	else if(musicPaused)
	{
		musicPaused = false;
		I_ResumeSong(rs);
	}
}

void S_ShutDown(void)
{
	// DERP: I_Quit and I_Error shut the sound device down.
	I_StopSong(rs);
	I_UnRegisterSong(rs);
}

