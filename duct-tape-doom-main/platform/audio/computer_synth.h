/* The optional computer palette: one continuous machine body and independently
 * seeded activity takes. Both add dry mono synthesis to the supplied buffer;
 * no recordings or replacement assets are used. */
#ifndef DOOM_AUDIO_COMPUTER_SYNTH_H
#define DOOM_AUDIO_COMPUTER_SYNTH_H

/* The body is periodic over frames, including its slow modulation and narrow
 * granular bands. Keep its full period; do not apply a noise-tail crossfade. */
void Computer_BodyRender(unsigned seed, int rate, float *out, int frames);
/* Fine head seeks/read chatter, occasional relay clacks and electrical detail.
 * Three different takes are intended to play together, sharing one body. */
void Computer_ActivityRender(unsigned seed, int rate, float *out, int frames);

#endif
