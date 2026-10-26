/* DERP: DOOM's v_video.h for the shared platform code (heretic/v_video.c). */
#ifndef DERP_HERETIC_V_VIDEO_H
#define DERP_HERETIC_V_VIDEO_H
#include "doomdef.h"
// screens[0] is Heretic's screen buffer.
extern byte *screens[1];
extern byte v_overlaymask[SCREENWIDTH * SCREENHEIGHT];
extern boolean v_overlayactive;
extern int usegamma;
extern byte gammatable[5][256];
#endif
