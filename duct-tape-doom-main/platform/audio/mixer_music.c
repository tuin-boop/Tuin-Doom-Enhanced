/* Music in the SDL mixer. A score plays through one of two synths:
   - OPL FM: an emulated YMF262 with the WAD's GENMIDI instruments, the way
     DOS DOOM sounded on Sound Blaster cards. Built from the chip manual and
     the published lump formats; see opl_music_spec.md.
   - SoundFont: General MIDI instruments from a .sf2 bank, closer to the
     Sound Canvas the scores were written on. */
#include <stdio.h>
#include <string.h>
#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "../../third_party/dr_libs/dr_mp3.h"
#include "doomdef.h"
#include "m_argv.h"
#include "w_wad.h"
#include "z_zone.h"
#include "../i_mixer_music.h"
#include "music_player.h"
#include "opl_chip.h"
#include "opl_genmidi.h"
#include "opl_voices.h"
#include "sf2_synth.h"

/* Full-volume gains. The loudest OPL score, MAP01, peaks at 0.83 on the
   chip. Through GeneralUser GS no score clips at 0.3, which sits a little
   under the OPL. */
#define OPL_GAIN	1.0f
#define SF2_GAIN	0.3f

static genmidi_instrument_t	instruments[GENMIDI_COUNT];
static boolean		opl_ready, opl_failed;
static sf2_bank_t*	bank;
static score_t		score;
static music_player_t	player;
static const music_synth_t*	synth;
static boolean		song_loaded, music_paused;
static float		volume = 8 / 15.0f;
static drmp3 mp3;
static boolean mp3_loaded,mp3_playing,mp3_loop;
static double mp3_fraction,mp3_step;
static float mp3_current[2],mp3_next[2];
static boolean mp3_sample(float sample[2]){
    float decoded[2];
    if(!drmp3_read_pcm_frames_f32(&mp3,1,decoded)){
        if(!mp3_loop||!drmp3_seek_to_pcm_frame(&mp3,0)||!drmp3_read_pcm_frames_f32(&mp3,1,decoded)){mp3_playing=false;return false;}
    }
    sample[0]=decoded[0];sample[1]=decoded[mp3.channels==1?0:1];return true;
}

static boolean start_opl (int rate)
{
    int lump;

    if (opl_ready || opl_failed)
	return opl_ready;
    lump = W_CheckNumForName ("GENMIDI");
    /* Copied out at once, so the cached lump may be purged later. */
    if (lump < 0 || !GENMIDI_Parse (W_CacheLumpNum (lump, PU_CACHE), W_LumpLength (lump), instruments))
    {
	fprintf (stderr, "OPL music: no GENMIDI instruments in the WADs.\n");
	opl_failed = true;
	return false;
    }
    OPLChip_Start (rate);
    OPLVoices_Init (instruments);
    opl_ready = true;
    printf ("OPL music ready (GENMIDI instruments, %d voices).\n", OPL_VOICES);
    return true;
}

boolean MixerMusic_Register (const void* data, int size, int rate, boolean soundfont)
{
    MixerMusic_UnRegister ();
    if (M_CheckParm ("-nomusic"))
	return false;
    if(size>=3&&(!memcmp(data,"ID3",3)||(((const unsigned char*)data)[0]==255&&(((const unsigned char*)data)[1]&224)==224))){
        if(size>64*1024*1024||!drmp3_init_memory(&mp3,data,(size_t)size,NULL))return false;
        if(mp3.channels<1||mp3.channels>2||mp3.sampleRate<8000||mp3.sampleRate>192000){drmp3_uninit(&mp3);return false;}
        mp3_loaded=true;song_loaded=true;mp3_step=(double)mp3.sampleRate/rate;
        fprintf(stderr,"MP3 music ready (%u Hz, %u channels).\n",mp3.sampleRate,mp3.channels);return true;
    }
    if (soundfont && bank)
	synth = &SF2_Synth;
    else if (start_opl (rate))
	synth = &OPL_Synth;
    else
	return false;
    if (!Score_Load (&score, data, size, rate))
    {
	fprintf (stderr, "Could not load the music score.\n");
	return false;
    }
    song_loaded = true;
    return true;
}

void MixerMusic_UnRegister (void)
{
    MixerMusic_Stop ();
    song_loaded = false;
    if(mp3_loaded){drmp3_uninit(&mp3);mp3_loaded=false;}
    Score_Clear (&score);
}

void MixerMusic_Play (boolean loop)
{
    if (!song_loaded)
	return;
    if(mp3_loaded){mp3_loop=loop;mp3_fraction=0;mp3_playing=true;music_paused=false;drmp3_seek_to_pcm_frame(&mp3,0);mp3_sample(mp3_current);mp3_sample(mp3_next);return;}
    Player_Start (&player, &score, synth, loop);
    music_paused = false;
}

void MixerMusic_Stop (void)
{
    mp3_playing=false;
    Player_Stop (&player);
}

void MixerMusic_Pause (void) { music_paused = true; }
void MixerMusic_Resume (void) { music_paused = false; }

void MixerMusic_SetVolume (int level)
{
    volume = level < 0 ? 0 : level >= 15 ? 1.0f : level / 15.0f;
}

struct tsf* MixerMusic_UseBank (struct tsf* using)
{
    if (synth == &SF2_Synth)
	MixerMusic_UnRegister ();
    bank = using;
    return SF2_Use (using);
}

void MixerMusic_Render (float* stereo, int frames)
{
    if(mp3_loaded){
        if(music_paused||!mp3_playing)return;
        for(int n=0;n<frames&&mp3_playing;++n){
            for(int c=0;c<2;++c)stereo[2*n+c]+=(mp3_current[c]+(mp3_next[c]-mp3_current[c])*(float)mp3_fraction)*volume;
            mp3_fraction+=mp3_step;
            while(mp3_fraction>=1&&mp3_playing){mp3_fraction-=1;memcpy(mp3_current,mp3_next,sizeof(mp3_current));mp3_sample(mp3_next);}
        }
        return;
    }
    if (music_paused || !player.playing)
	return;
    Player_Mix (&player, stereo, frames, volume * (synth == &SF2_Synth ? SF2_GAIN : OPL_GAIN));
}
