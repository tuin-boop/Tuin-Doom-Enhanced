/* The ambient loops in the mixer: AMBIENCE_LAYERS voices per kind, run with
 * the audio lock held. Each glides to the gains and muffling ambience.c gives it every
 * tic, so nothing steps or clicks as the listener walks. */
#ifndef DOOM_AUDIO_AMBIENCE_VOICES_H
#define DOOM_AUDIO_AMBIENCE_VOICES_H
#include "ambience.h"

/* The targets for this tic, every layer of every kind. */
void AmbienceVoices_Set(const ambience_voice_t voices[AMBIENT_KINDS][AMBIENCE_LAYERS]);
/* The alternative computer body follows the combined audible computer field. */
void AmbienceVoices_SetComputerBody(const ambience_voice_t *body);
/* Crossfades the computer family; other ambience and its positions are untouched. */
void AmbienceVoices_AlternativeComputer(int enabled);
/* Adds frames of every sounding voice to interleaved stereo and to the
   reverb send, read between samples by resampling (audio/resample.h), all
   scaled by duck (1 none). gain is the mixer's effect gain. */
void AmbienceVoices_Mix(float *stereo, float *send, int frames, int rate, int resampling, float gain, float duck);
/* Silences them all at once (a level ending, ambience switched off). */
void AmbienceVoices_Clear(void);
#endif
