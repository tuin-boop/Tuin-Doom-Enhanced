/* How rough a wall texture or flat is to sound, read from its artwork:
 * the share of its texels lying in painted seams - joints, ribs, cracks, the
 * dark valleys the eye takes for relief. Smooth panels and plain plaster have
 * few, brickwork and rubble many. Ranked against a hand-judged catalog of
 * DOOM's surfaces it agrees only moderately (rank correlation 0.5, within
 * stone and metal alike), so room.c only lets it
 * roughen a material's scatter a little, never decide the material. */
#ifndef DOOM_AUDIO_RELIEF_H
#define DOOM_AUDIO_RELIEF_H

/* index_alpha: w*h pairs of palette index and coverage (R_CopyIndexedTexture);
   palette: 768 bytes of RGB. Returns the seam share, 0..1, or -1 when less
   than half the texture is solid (bars, fences: their gaps are not relief). */
float Relief_Seams(const unsigned char *index_alpha, int w, int h, const unsigned char *palette);

#endif
