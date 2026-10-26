#include "../platform/wad_probe.h"
#include <stdio.h>
#include <string.h>

#define CHECK(test) do { if (!(test)) { fprintf(stderr, "Failed at line %d\n", __LINE__); return 1; } } while (0)

int main(void)
{
    const char *path = "loader-test.wad";
    unsigned char wad[60] = { 'I','W','A','D',3,0,0,0,12,0,0,0 };
    memcpy(wad + 20, "E1M1", 4);
    memcpy(wad + 36, "E2M1", 4);
    memcpy(wad + 52, "PLAYPAL", 7);
    doom_wad_info info;
    FILE *file = fopen(path, "wb");
    CHECK(file); CHECK(fwrite(wad, 1, sizeof(wad), file) == sizeof(wad)); fclose(file);
    CHECK(DOOM_ProbeWad(path, &info) && info.is_iwad && info.episode_maps && info.full_game && !info.numbered_maps && info.has_palette);
    CHECK(info.game == DOOM_WAD_DOOM);
    memcpy(wad + 36, "TINTTAB\0", 8); /* Heretic: translucency table, episodes. */
    file = fopen(path, "wb"); CHECK(file); fwrite(wad, 1, sizeof(wad), file); fclose(file);
    CHECK(DOOM_ProbeWad(path, &info) && info.game == DOOM_WAD_HERETIC);
    memcpy(wad + 20, "BEHAVIOR", 8); /* Hexen: map scripts. */
    file = fopen(path, "wb"); CHECK(file); fwrite(wad, 1, sizeof(wad), file); fclose(file);
    CHECK(DOOM_ProbeWad(path, &info) && info.game == DOOM_WAD_HEXEN);
    memcpy(wad + 20, "E1M1\0\0\0\0", 8); memcpy(wad + 36, "E2M1\0\0\0\0", 8);
    memcpy(wad, "PWAD", 4);
    memcpy(wad + 20, "MAP07", 5); memset(wad + 36, 0, 8); memset(wad + 52, 0, 8); /* Like DOOM 64: no palette. */
    file = fopen(path, "wb"); CHECK(file); fwrite(wad, 1, sizeof(wad), file); fclose(file);
    CHECK(DOOM_ProbeWad(path, &info) && !info.is_iwad && info.numbered_maps && !info.episode_maps && !info.has_palette);
    wad[8] = 255; /* Directory beyond end of file. */
    file = fopen(path, "wb"); CHECK(file); fwrite(wad, 1, sizeof(wad), file); fclose(file);
    CHECK(!DOOM_ProbeWad(path, &info));
    wad[8] = 12; wad[16] = 255; /* Lump beyond end of file. */
    file = fopen(path, "wb"); CHECK(file); fwrite(wad, 1, sizeof(wad), file); fclose(file);
    CHECK(!DOOM_ProbeWad(path, &info));
    file = fopen(path, "wb"); CHECK(file); fwrite(wad, 1, 8, file); fclose(file);
    CHECK(!DOOM_ProbeWad(path, &info));
    remove(path);
    CHECK(!DOOM_ProbeWad(path, &info));
    puts("WAD loader checks passed");
    return 0;
}
