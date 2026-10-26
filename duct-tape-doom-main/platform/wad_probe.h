#ifndef DOOM_WAD_PROBE_H
#define DOOM_WAD_PROBE_H

typedef struct {
    int is_iwad;
    int episode_maps;
    int numbered_maps;
    int full_game;
    int has_palette; /* PLAYPAL: missing from DOOM 64 and other non-DOOM games. */
    int game;        /* DOOM_WAD_*: which game program plays it */
} doom_wad_info;

/* Heretic and Hexen ship TINTTAB (translucency), DOOM does not; Hexen's maps
   carry BEHAVIOR (scripts). Add-ons without either count as DOOM's, and
   are paired with whatever base game is chosen. */
enum { DOOM_WAD_DOOM, DOOM_WAD_HERETIC, DOOM_WAD_HEXEN };

/* Check the directory before offering a file in the launcher. */
#ifdef __cplusplus
extern "C" {
#endif
int DOOM_ProbeWad(const char *path, doom_wad_info *info);
#ifdef __cplusplus
}
#endif
#endif
