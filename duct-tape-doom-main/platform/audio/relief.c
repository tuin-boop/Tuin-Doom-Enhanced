/* Seam share of a texture's artwork; see relief.h. Brightness is taken as
 * log luminance, blurred over 3x3 texels so speckle washes out; a texel is in
 * a seam when it is darker than both texels two across from it, along any of
 * four directions, by a clear step. The texture wraps, as it does on walls. */
#include <math.h>
#include <stdlib.h>
#include "relief.h"

#define SEAM_STEP 0.08f  /* log luminance: how much darker a seam's floor is than both its sides */

float Relief_Seams(const unsigned char *index_alpha, int w, int h, const unsigned char *palette)
{
    if (w < 8 || h < 8) return -1;
    size_t count = (size_t)w * h;
    float level[256];
    for (int i = 0; i < 256; ++i)
        level[i] = logf(1 + 0.299f * palette[3 * i] + 0.587f * palette[3 * i + 1] + 0.114f * palette[3 * i + 2]);
    float *bright = malloc(count * sizeof(float)), *blurred = malloc(count * sizeof(float));
    if (!bright || !blurred) { free(bright); free(blurred); return -1; }

    /* Holes take the solid texels' mean, so their edges aren't seams. */
    double sum = 0;
    size_t solid = 0;
    for (size_t i = 0; i < count; ++i)
        if (index_alpha[2 * i + 1]) { sum += level[index_alpha[2 * i]]; ++solid; }
    if (solid * 2 < count) { free(bright); free(blurred); return -1; }
    float mean = (float)(sum / solid);
    for (size_t i = 0; i < count; ++i) bright[i] = index_alpha[2 * i + 1] ? level[index_alpha[2 * i]] : mean;

#define AT(a, x, y) a[(size_t)((((y) % h) + h) % h) * w + ((((x) % w) + w) % w)]
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            float s = 0;
            for (int j = -1; j <= 1; ++j)
                for (int i = -1; i <= 1; ++i) s += AT(bright, x + i, y + j);
            blurred[(size_t)y * w + x] = s / 9;
        }
    static const int across[4][2] = {{2, 0}, {0, 2}, {2, 2}, {2, -2}};
    size_t seams = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            float c = blurred[(size_t)y * w + x];
            for (int d = 0; d < 4; ++d)
            {
                float a = AT(blurred, x + across[d][0], y + across[d][1]);
                float b = AT(blurred, x - across[d][0], y - across[d][1]);
                if (c < fminf(a, b) - SEAM_STEP) { ++seams; break; }
            }
        }
#undef AT
    free(bright);
    free(blurred);
    return (float)seams / (float)count;
}
