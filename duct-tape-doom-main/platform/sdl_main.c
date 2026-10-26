#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "doomdef.h"
#include "d_main.h"
#include "m_argv.h"
#include "i_compat.h"
#include "game_profile.h"
#include "wad_probe.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

static char *selected_wad;
static boolean selection_done;
#ifdef __APPLE__
#include <limits.h>
#include <mach-o/dyld.h>
extern int DOOM_ChooseWads(const char *bundled, char **base, char **addon);
#endif
#ifdef _WIN32
#include <process.h>
#endif
static void selected(void *userdata, const char * const *files, int filter)
{
    (void)userdata; (void)filter;
    if (files && files[0]) selected_wad = SDL_strdup(files[0]);
    selection_done = true;
}

/* Many releases ship their DEHACKED patch beside the WAD: map.wad + map.deh. */
static char *companion_patch(const char *wad)
{
    static const char *extensions[] = { ".deh", ".DEH", ".bex", ".BEX" };
    size_t length = strlen(wad);
    const char *dot = strrchr(wad, '.');
    const char *slash = strrchr(wad, '/');
    if (dot && (!slash || dot > slash)) length = (size_t)(dot - wad);
    char *path = malloc(length + 5);
    if (!path) return NULL;
    for (int i = 0; i < 4; ++i)
    {
        memcpy(path, wad, length);
        strcpy(path + length, extensions[i]);
        FILE *file = fopen(path, "rb");
        if (file) { fclose(file); return path; }
    }
    free(path);
    return NULL;
}

static boolean is_absolute(const char *path)
{
    if (path[0] == '/' || path[0] == '\\') return true;
#ifdef _WIN32
    if (SDL_isalpha((unsigned char)path[0]) && path[1] == ':') return true;
#endif
    return false;
}

/* Launchers such as Doom Runner pass paths relative to the folder they start
 * the game in, but the game then runs from its save folder: make them absolute
 * while they still resolve. -file and -deh take every path up to the next option. */
static void absolute_path_args(int argc, char **argv)
{
    static const char *single[] = { "-iwad", "-config", "-record", "-soundfont" };
    static const char *lists[] = { "-file", "-deh", "-bex" };
    char *folder = SDL_GetCurrentDirectory();
    if (!folder) return;
    for (int i = 1; i < argc; ++i)
    {
        boolean path = false, list = false;
        for (size_t k = 0; k < SDL_arraysize(single); ++k)
            if (!SDL_strcasecmp(argv[i], single[k])) path = true;
        for (size_t k = 0; k < SDL_arraysize(lists); ++k)
            if (!SDL_strcasecmp(argv[i], lists[k])) path = list = true;
        while (path && i + 1 < argc && argv[i + 1][0] != '-')
        {
            char *full;
            ++i;
            if (!is_absolute(argv[i]) && SDL_asprintf(&full, "%s%s", folder, argv[i]) > 0) argv[i] = full;
            path = list;
        }
    }
    SDL_free(folder);
}

/* Each game has its own program, beside this one: DOOM's and Heretic's.
 * Hexen and the rest have none yet. */
static const char *program_for(int game)
{
#if defined(__APPLE__)
    if (game == DOOM_WAD_DOOM) return "Play DOOM";
#elif defined(_WIN32)
    if (game == DOOM_WAD_DOOM) return "doom.exe";
    if (game == DOOM_WAD_HERETIC) return "derp-heretic.exe";
#else
    if (game == DOOM_WAD_DOOM) return "doom";
#endif
#ifndef _WIN32
    if (game == DOOM_WAD_HERETIC) return "derp-heretic";
#endif
    return NULL;
}

/* The folder this program runs from (in the Mac app: Contents/MacOS). */
static char *program_folder(void)
{
#ifdef __APPLE__
    char path[PATH_MAX], real[PATH_MAX];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) || !realpath(path, real)) return NULL;
    char *slash = strrchr(real, '/');
    if (!slash) return NULL;
    slash[1] = 0;
    return SDL_strdup(real);
#else
    const char *base = SDL_GetBasePath();
    return base ? SDL_strdup(base) : NULL;
#endif
}

#ifdef _WIN32
/* _execv joins the arguments with spaces; quote the ones that hold any. */
static char *quoted(const char *arg)
{
    if (!strpbrk(arg, " \t\"")) return SDL_strdup(arg);
    size_t length = strlen(arg);
    char *out = SDL_malloc(length * 2 + 3), *o = out;
    if (!out) return NULL;
    *o++ = '"';
    for (const char *a = arg; *a; ++a) { if (*a == '"') *o++ = '\\'; *o++ = *a; }
    *o++ = '"'; *o = 0;
    return out;
}
#endif

/* A WAD for another game starts that game's program with the same
 * arguments plus the chosen WADs. Returns only when that fails. */
static void hand_off(int game, int argc, char **argv, const char *wad, const char *addon)
{
    const char *program = program_for(game);
    char *folder = program_folder();
    char *path = NULL;
    if (!program)
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, Game_Title(),
            game == DOOM_WAD_HEXEN ? "Hexen cannot be played yet." : "This game cannot be played yet.", NULL);
        SDL_free(folder);
        return;
    }
    if (folder && SDL_asprintf(&path, "%s%s", folder, program) > 0)
    {
        FILE *probe = fopen(path, "rb");
#ifdef __APPLE__
        /* Next to the build's Play DOOM.app rather than inside it. */
        if (!probe && game == DOOM_WAD_DOOM)
        {
            SDL_free(path); path = NULL;
            if (SDL_asprintf(&path, "%sPlay DOOM.app/Contents/MacOS/Play DOOM", folder) > 0) probe = fopen(path, "rb");
        }
#endif
        if (probe) fclose(probe);
        else { SDL_free(path); path = NULL; }
    }
    SDL_free(folder);
    if (!path)
    {
        char message[256];
        snprintf(message, sizeof(message), "The program for this game (%s) is missing beside this one.", program);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, Game_Title(), message, NULL);
        return;
    }
    char **args = calloc((size_t)argc + 6, sizeof(char*));
    if (!args) return;
    int count = 0;
    args[count++] = path;
    for (int i = 1; i < argc; ++i) args[count++] = argv[i];
    if (wad) { args[count++] = "-iwad"; args[count++] = (char *)wad; }
    if (addon)
    {
        char *patch = companion_patch(addon);
        args[count++] = "-file"; args[count++] = (char *)addon;
        if (patch) { args[count++] = "-deh"; args[count++] = patch; }
    }
    SDL_Quit();
#ifdef _WIN32
    for (int i = 0; i < count; ++i) args[i] = quoted(args[i]);
    _execv(path, (const char * const *)args);
#else
    execv(path, args);
#endif
    fprintf(stderr, "Could not start %s\n", path);
}

#ifndef __APPLE__
#define MAX_CHOICES 8

static int compare_paths(const void *a, const void *b)
{
    return SDL_strcasecmp(*(char * const *)a, *(char * const *)b);
}

static const char *file_name(const char *path)
{
    const char *name = path;
    for (const char *p = path; *p; ++p)
        if (*p == '/' || *p == '\\') name = p + 1;
    return name;
}

static void notice(const char *text)
{
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, Game_Title(), text, NULL);
}

/* Returns an index into paths, -1 for the extra choice, or -2 to quit. */
static int choose(const char *message, char **paths, int count, const char *extra)
{
    SDL_MessageBoxButtonData buttons[MAX_CHOICES + 2];
    int shown = 0, result = -2;
    for (int i = 0; i < count; ++i)
        buttons[shown++] = (SDL_MessageBoxButtonData){ i ? 0 : SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, i, file_name(paths[i]) };
    if (extra) buttons[shown++] = (SDL_MessageBoxButtonData){ 0, -1, extra };
    buttons[shown++] = (SDL_MessageBoxButtonData){ SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, -2, "Quit" };
    SDL_MessageBoxData box = { SDL_MESSAGEBOX_INFORMATION | SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT,
                               NULL, Game_Title(), message, shown, buttons, NULL };
    if (!SDL_ShowMessageBox(&box, &result)) return -2;
    return result;
}

/* Windows and Linux: play the WADs dropped into the "wads" folder beside the
 * program, following the same rules as the Mac launcher. Returns 1 when a game
 * was chosen, 0 when the folder holds no WADs, and -1 to quit. */
static int choose_from_folder(const char *base, const char *bundled, char **game, char **addon)
{
    char folder[4096];
    snprintf(folder, sizeof(folder), "%swads", base);
    int count = 0;
    char **names = SDL_GlobDirectory(folder, "*.wad", SDL_GLOB_CASEINSENSITIVE, &count);
    if (!names) return 0;
    char *bases[MAX_CHOICES], *addons[MAX_CHOICES];
    int base_count = 0, addon_count = 0, invalid = 0, other_game = 0;
    for (int i = 0; i < count; ++i)
    {
        char *path = NULL;
        doom_wad_info info;
        if (!SDL_asprintf(&path, "%s/%s", folder, names[i])) continue;
        if (!DOOM_ProbeWad(path, &info)) invalid = 1;
        else if (info.is_iwad && (!info.has_palette || info.game == DOOM_WAD_HEXEN)) other_game = 1;
        else if (info.is_iwad && (info.episode_maps || info.numbered_maps) && base_count < MAX_CHOICES)
        { bases[base_count++] = path; continue; }
        else if (!info.is_iwad && info.game != DOOM_WAD_HEXEN && addon_count < MAX_CHOICES)
        { addons[addon_count++] = path; continue; }
        SDL_free(path);
    }
    SDL_free(names);
    qsort(bases, (size_t)base_count, sizeof(*bases), compare_paths);
    qsort(addons, (size_t)addon_count, sizeof(*addons), compare_paths);

    int result = -1;
    const char *chosen_game = NULL, *chosen_addon = NULL;
    FILE *file = fopen(bundled, "rb");
    int has_bundled = file != NULL;
    if (file) fclose(file);
    if (!base_count && !addon_count)
    {
        if (other_game) notice("The wads folder holds a game WAD that cannot be played yet, such as Hexen or DOOM 64. DOOM, DOOM II, Final DOOM, Heretic and Freedoom game WADs can be played.");
        else if (invalid) notice("The WAD files in the wads folder are damaged or unsupported.");
        result = invalid || other_game ? -1 : 0;
        goto done;
    }
    if (!base_count)
    {
        if (!has_bundled)
        {
            notice("Custom maps need a base game. Put your DOOM or DOOM II game WAD into the wads folder too.");
            goto done;
        }
        chosen_game = bundled;
    }
    else if (base_count == 1 && !has_bundled) chosen_game = bases[0];
    else
    {
        int pick = choose("Choose the game to play.", bases, base_count, has_bundled ? "Freedoom" : NULL);
        if (pick == -2) goto done;
        chosen_game = pick == -1 ? bundled : bases[pick];
    }
    if (addon_count == 1) chosen_addon = addons[0];
    else if (addon_count > 1)
    {
        int pick = choose("Choose a custom WAD.", addons, addon_count, "Base game only");
        if (pick == -2) goto done;
        if (pick >= 0) chosen_addon = addons[pick];
    }
    if (chosen_addon)
    {
        doom_wad_info game_info, addon_info;
        DOOM_ProbeWad(chosen_game, &game_info);
        DOOM_ProbeWad(chosen_addon, &addon_info);
        if ((addon_info.numbered_maps && !game_info.numbered_maps) ||
            (addon_info.episode_maps && !game_info.episode_maps))
        {
            notice(addon_info.numbered_maps
                ? "This map uses DOOM II levels. Put your DOOM II WAD (or Freedoom Phase 2) into the wads folder."
                : "This map uses DOOM episodes. Put your DOOM WAD (or Freedoom Phase 1) into the wads folder.");
            goto done;
        }
        if (!game_info.numbered_maps && !game_info.full_game)
        {
            notice("The shareware base game cannot load custom WADs. Use the full game or Freedoom instead.");
            goto done;
        }
        *addon = SDL_strdup(chosen_addon);
    }
    *game = SDL_strdup(chosen_game);
    result = 1;
done:
    for (int i = 0; i < base_count; ++i) SDL_free(bases[i]);
    for (int i = 0; i < addon_count; ++i) SDL_free(addons[i]);
    return result;
}
#endif

int main(int argc, char **argv)
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        fprintf(stderr, "Could not initialize SDL: %s\n", SDL_GetError());
        return 1;
    }
    absolute_path_args(argc, argv);
    const char *base = SDL_GetBasePath();
    char bundled[4096];
    snprintf(bundled, sizeof(bundled), "%sdefault.wad", base ? base : "./");
    char materials[4096],graphics[4096],remembered[4096];
    snprintf(materials,sizeof(materials),"%sDiggingMaterials.wad",base?base:"./");
    snprintf(graphics,sizeof(graphics),"%sglobal-play/graphics.cfg",base?base:"./");
    snprintf(remembered,sizeof(remembered),"%sglobal-play/iwad.txt",base?base:"./");
    int has_iwad = 0;
    char *selected_addon = NULL;
    for (int i = 1; i + 1 < argc; ++i)
        if (!strcmp(argv[i], "-iwad")) has_iwad = 1;
    if(!has_iwad){FILE *stored=fopen(remembered,"r");
        if(stored){char path[4096];if(fgets(path,sizeof(path),stored)){path[strcspn(path,"\r\n")]=0;FILE *check=fopen(path,"rb");if(check){fclose(check);selected_wad=SDL_strdup(path);}}fclose(stored);}
    }
#ifdef __APPLE__
    if (argc == 1&&!selected_wad)
    {
        if (!DOOM_ChooseWads(bundled, &selected_wad, &selected_addon))
        { SDL_Quit(); return 0; }
    }
#else
    if (!has_iwad && !selected_wad && choose_from_folder(base ? base : "./", bundled, &selected_wad, &selected_addon) < 0)
    { SDL_Quit(); return 0; }
#endif
    if (!has_iwad && !selected_wad)
    {
        FILE *file = fopen(bundled, "rb");
        if (file) { fclose(file); selected_wad = SDL_strdup(bundled); }
        else
        {
            SDL_DialogFileFilter filter = { "DOOM game data", "wad" };
            SDL_ShowOpenFileDialog(selected, NULL, NULL, &filter, 1, NULL, false);
            while (!selection_done) { SDL_PumpEvents(); SDL_Delay(10); }
            if (!selected_wad) { SDL_Quit(); return 0; }
        }
    }
    /* The WAD may belong to another game's program. */
    const char *wad = selected_wad;
    for (int i = 1; i + 1 < argc; ++i)
        if (!strcmp(argv[i], "-iwad")) wad = argv[i + 1];
    doom_wad_info wad_info;
    if (wad && DOOM_ProbeWad(wad, &wad_info) && wad_info.game != Game_WadKind())
    {
        hand_off(wad_info.game, argc, argv, has_iwad ? NULL : selected_wad, selected_addon);
        SDL_Quit(); return 1;
    }
    int solid_maps=0;
    for(int i=1;i<argc;++i)if(!strcmp(argv[i],"-solidmaps"))solid_maps=1;
    char *preferences = SDL_GetPrefPath("Local Games",
#ifdef DOOM_TEST_REAL_STARTUP
        "DERP Solid Maps Validation"
#else
        solid_maps?"DERP Solid Maps":"DERP Destruction Prototype"
#endif
    );
    if (!preferences)
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, Game_Title(), "Could not create the game save folder.", NULL);
        SDL_Quit(); return 1;
    }
#ifdef _WIN32
    if (_chdir(preferences))
#else
    if (chdir(preferences))
#endif
    { SDL_free(preferences); SDL_Quit(); return 1; }
    SDL_free(preferences);
    if (argc == 1)
    {
        FILE *log = fopen("game.log", "w");
        if (log) fclose(log);
        freopen("game.log", "a", stdout);
        freopen("game.log", "a", stderr);
    }
    FILE *file = fopen(Game_ConfigFile(), "r");
    if (file) fclose(file);
    else
    {
        file = fopen(Game_ConfigFile(), "w");
        if (file)
        {
            fputs("key_up 119\nkey_down 115\nkey_strafeleft 97\nkey_straferight 100\n"
                  "key_use 101\nkey_fire 157\nmouse_sensitivity 5\n"
                  "sfx_volume 10\nmusic_volume 7\nsnd_channels 16\n", file);
            fclose(file);
        }
    }
    char **args = calloc((size_t)argc + 24, sizeof(char*));
    if (!args) { SDL_Quit(); return 1; }
    int count = 0;
    int files=0,has_graphics=0,has_mode=0;
    for (int i = 0; i < argc; ++i){
        args[count++] = argv[i];
        if(!strcmp(argv[i],"-file")){args[count++]=materials;files=1;}
        if(!strcmp(argv[i],"-graphics"))has_graphics=1;
        if(!strcmp(argv[i],"-digglobal")||!strcmp(argv[i],"-digging")||!strcmp(argv[i],"-nodigging")||
           !strcmp(argv[i],"-solidmaps")||!strcmp(argv[i],"-solidlab"))has_mode=1;
    }
    if(!has_mode)args[count++]="-digglobal";
    if(!has_graphics){args[count++]="-graphics";args[count++]=graphics;}
    args[count++]="-fullscreen";
    if (!has_iwad) { args[count++] = "-iwad"; args[count++] = selected_wad; }
    if (selected_addon)
    {
        char *patch = companion_patch(selected_addon);
        args[count++] = "-file"; args[count++] = materials; args[count++] = selected_addon; files=1;
        if (patch) { args[count++] = "-deh"; args[count++] = patch; }
    }
    if(!files){args[count++]="-file";args[count++]=materials;}
    int has_config = 0;
    for (int i = 1; i + 1 < argc; ++i)
        if (!strcmp(argv[i], "-config")) has_config = 1;
    if (!has_config) { args[count++] = "-config"; args[count++] = (char *)Game_ConfigFile(); }
    myargc = count;
    myargv = args;
    D_DoomMain();
    return 0;
}
