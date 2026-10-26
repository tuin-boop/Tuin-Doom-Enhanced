/* The F4 Sound settings, handed to the mixer by the 3D scene's applySettings. */
#ifndef DOOM_AUDIO_SOUND_OPTIONS_H
#define DOOM_AUDIO_SOUND_OPTIONS_H

/* acoustics: room echoes and tail, at amount (0-2). walls: muffle sounds out
   of sight. distance: dull far sounds. falloff: far sounds fade as in air,
   not in DOOM's straight line (where the game allows it). */
void I_SetSoundOptions(int acoustics, float amount, int walls, int distance, int falloff);
/* How effects are read between their samples: 0 held as in the original,
   1 a cubic, 2 a windowed sinc (see audio/resample.h). Past held samples,
   treble (0-2) is how much made-up treble goes above them (audio/treble.h). */
void I_SetSfxPlayback(int mode, float treble);

/* The made-up sounds (audio/made_bank.h). impacts: sparks, dying lamps and
   screens, breaking glass; glass 0 the shatter, 1 the fracture; ambient: the
   sounds of lamps, machines, liquids, fire and wind, at amount (0-2); steps:
   the player's footsteps and splashes, and monsters splashing. */
void I_SetMadeSounds(int impacts, int glass, int ambient, float amount, int steps);
/* Computer ambience: machine hum, hard-drive seeks and relay chatter instead of bleeps. */
void I_SetAlternativeComputerSounds(int enabled);
/* A made-up effect (an IMPACT_ kind of audio/impact_synth.h) at a map point. */
void I_MadeSound(int kind, float x, float y, float z);
/* Fixtures shot out around a map point: their ambient sound stops. */
void I_AmbienceSilence(float x, float y, float z, float radius);
/* A window pane on a map line, whole or broken: through a whole one ambience is muffled. */
void I_AmbienceGlaze(int line, int whole);
/* Whether window glass is shown; without it the wind blows through every window. */
void I_AmbienceShowGlass(int shown);
/* How hard it rains (0-1, scene3d/weather.cpp): the rain is heard where the wind is,
   and on the window glass until it breaks. */
void I_AmbienceRain(float amount);

/* What plays the music; see platform/i_music.c. The system synthesizer
   is the Mac's own; elsewhere that choice plays the SoundFont. */
enum { MUSIC_SOUNDFONT, MUSIC_OPL, MUSIC_SYSTEM };
void I_SetMusicDevice(int device);
#endif
