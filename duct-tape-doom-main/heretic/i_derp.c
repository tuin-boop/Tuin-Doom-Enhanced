/* Heretic's side of the platform layer: the parts of Raven's DOS I_IBM.C
   that DERP's shared SDL code (platform/) does not already provide under
   DOOM's names. */
#include "doomdef.h"
#include "i_video.h"

// The DOS build blitted only the parts of the screen that changed. The
// screen buffer always holds the whole frame, so it all goes out each time.
int UpdateState;
int DisplayTicker = 0;

void I_Update(void)
{
	I_FinishUpdate();
	UpdateState = I_NOUPDATE;
}

// The CyberMan controller: never present.
boolean isCyberPresent = false;
void I_ReadCyberCmd(ticcmd_t *cmd)
{
	(void)cmd;
}

// Sound settings Raven's I_SOUND.C kept. Only the volumes and channel count
// mean anything here; the card settings stay in the config file unused.
int snd_Channels = 8;
int snd_DesiredMusicDevice, snd_DesiredSfxDevice;
int snd_MaxVolume = 10;
int snd_MusicVolume = 10;
int snd_SBport, snd_SBirq, snd_SBdma, snd_Mport;
