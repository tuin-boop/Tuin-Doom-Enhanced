#include "wad_probe.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t little32(const unsigned char *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

int DOOM_ProbeWad(const char *path, doom_wad_info *info)
{
    unsigned char header[12], entry[16];
    FILE *file = fopen(path, "rb");
    memset(info, 0, sizeof(*info));
    if (!file) return 0;
    int valid = 0;
    if (fread(header, 1, 12, file) != 12 ||
        (memcmp(header, "IWAD", 4) && memcmp(header, "PWAD", 4))) goto done;
    if (fseek(file, 0, SEEK_END)) goto done;
    long length = ftell(file);
    uint32_t count = little32(header + 4), offset = little32(header + 8);
    if (length < 12 || !count || count > 100000 || offset < 12 ||
        (uint64_t)offset + (uint64_t)count * 16 > (uint64_t)length ||
        fseek(file, offset, SEEK_SET)) goto done;
    info->is_iwad = !memcmp(header, "IWAD", 4);
    int has_tinttab = 0, has_behavior = 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        if (fread(entry, 1, 16, file) != 16 ||
            (uint64_t)little32(entry) + little32(entry + 4) > (uint64_t)length) goto done;
        const unsigned char *name = entry + 8;
        if (!memcmp(name, "PLAYPAL\0", 8)) info->has_palette = 1;
        if (!memcmp(name, "TINTTAB\0", 8)) has_tinttab = 1;
        if (!memcmp(name, "BEHAVIOR", 8)) has_behavior = 1;
        if (name[0] == 'E' && name[1] >= '1' && name[1] <= '9' &&
            name[2] == 'M' && name[3] >= '1' && name[3] <= '9' && !name[4])
        {
            info->episode_maps = 1;
            if (name[1] >= '2') info->full_game = 1;
        }
        if (!memcmp(name, "MAP", 3) && name[3] >= '0' && name[3] <= '9' &&
            name[4] >= '0' && name[4] <= '9' && !name[5]) info->numbered_maps = 1;
    }
    info->game = has_behavior ? DOOM_WAD_HEXEN : has_tinttab ? DOOM_WAD_HERETIC : DOOM_WAD_DOOM;
    valid = 1;
done:
    fclose(file);
    return valid;
}
