/* DERP: DOOM's i_video.h, implemented by platform/i_sdl_video.c. Heretic's
   I_Update (heretic/i_derp.c) calls these. */
#ifndef DERP_HERETIC_I_VIDEO_H
#define DERP_HERETIC_I_VIDEO_H
#include "doomdef.h"
void I_ShutdownGraphics(void);
void I_UpdateNoBlit(void);
void I_FinishUpdate(void);
void I_ReadScreen(byte *scr);
#endif
