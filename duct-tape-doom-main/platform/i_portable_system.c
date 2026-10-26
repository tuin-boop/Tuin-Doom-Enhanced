/* Common C11 system interface for the portable and legacy Linux targets. */
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
/* The engine supplies fixed-width limits with these historical names. */
#undef MAXCHAR
#undef MAXSHORT
#undef MAXINT
#undef MAXLONG
#undef MINCHAR
#undef MINSHORT
#undef MININT
#undef MINLONG
#else
#include <errno.h>
#include <time.h>
#endif

#include "doomdef.h"
#include "i_system.h"
#include "i_sound.h"
#include "i_video.h"
#include "m_misc.h"
#include "d_net.h"
#include "g_game.h"
#include "game_profile.h"
#ifdef DOOM_NATIVE
#include <SDL3/SDL.h>
#endif

int mb_used = 128;
static ticcmd_t emptycmd;

ticcmd_t *I_BaseTiccmd(void) { return &emptycmd; }
int I_GetHeapSize(void) { return mb_used * 1024 * 1024; }

byte *I_ZoneBase(int *size)
{
    byte *memory;
    if (mb_used < 1 || mb_used > INT_MAX / (1024 * 1024))
        I_Error("Invalid zone size");
    *size = I_GetHeapSize();
    memory = malloc((size_t)*size);
    if (!memory)
        I_Error("Could not allocate the DOOM memory zone");
    return memory;
}

int I_GetTime(void)
{
    uint64_t ticks;
#ifdef _WIN32
    static LARGE_INTEGER frequency, base;
    LARGE_INTEGER now;
    if (!frequency.QuadPart)
    {
        if (!QueryPerformanceFrequency(&frequency) || !QueryPerformanceCounter(&base))
            I_Error("Could not initialize the monotonic clock");
    }
    if (!QueryPerformanceCounter(&now))
        I_Error("Could not read the monotonic clock");
    uint64_t elapsed = (uint64_t)(now.QuadPart - base.QuadPart);
    ticks = (elapsed / frequency.QuadPart) * TICRATE
          + (elapsed % frequency.QuadPart) * TICRATE / frequency.QuadPart;
#else
    static struct timespec base;
    static boolean initialized;
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now))
        I_Error("Could not read the monotonic clock");
    if (!initialized)
    {
        base = now;
        initialized = true;
    }
    int64_t seconds = (int64_t)now.tv_sec - base.tv_sec;
    int64_t nanoseconds = (int64_t)now.tv_nsec - base.tv_nsec;
    if (nanoseconds < 0)
    {
        --seconds;
        nanoseconds += INT64_C(1000000000);
    }
    ticks = (uint64_t)seconds * TICRATE
          + (uint64_t)nanoseconds * TICRATE / UINT64_C(1000000000);
#endif
    return (int)(ticks & INT32_MAX);
}

void I_WaitVBL(int count)
{
    if (count <= 0)
        return;
#ifdef _WIN32
    Sleep((DWORD)((uint64_t)count * 1000 / 70));
#else
    struct timespec delay = {
        count / 70,
        (long)((int64_t)(count % 70) * INT64_C(1000000000) / 70)
    };
    while (nanosleep(&delay, &delay) && errno == EINTR) { }
#endif
}

void I_Init(void) { I_InitSound(); }
void I_BeginRead(void) { }
void I_EndRead(void) { }
void I_Tactile(int on, int off, int total)
{
    (void)on; (void)off; (void)total;
}

byte *I_AllocLow(int length)
{
    byte *memory;
    if (length < 0)
        I_Error("Invalid allocation size");
    memory = calloc((size_t)length, 1);
    if (!memory)
        I_Error("Could not allocate memory");
    return memory;
}

void I_Quit(void)
{
    D_QuitNetGame();
    I_ShutdownSound();
    I_ShutdownMusic();
    M_SaveDefaults();
    I_ShutdownGraphics();
    exit(EXIT_SUCCESS);
}

void I_Error(char *error, ...)
{
    va_list args;
    va_start(args, error);
#ifdef DOOM_NATIVE
    char message[1024];
    va_list display_args;
    va_copy(display_args, args);
    vsnprintf(message, sizeof(message), error, display_args);
    va_end(display_args);
#endif
    fputs("Error: ", stderr);
    vfprintf(stderr, error, args);
    fputc('\n', stderr);
    va_end(args);
    I_ShutdownSound();
    I_ShutdownMusic();
#ifdef DOOM_NATIVE
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, Game_Title(), message, NULL);
#endif
    I_ShutdownGraphics();
    exit(EXIT_FAILURE);
}
