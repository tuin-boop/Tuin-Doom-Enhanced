#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "doomdef.h"
#include "doomstat.h"
#include "d_main.h"
#include "d_event.h"
#include "i_system.h"
#include "i_video.h"
#include "m_argv.h"
#include "v_video.h"
#include "game_profile.h"
#include <SDL3/SDL.h>
#ifdef DOOM_RENDER3D
#include "i_render3d.h"
static boolean accel;
#endif

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static uint32_t palette[256], pixels[SCREENWIDTH * SCREENHEIGHT];
static int buttons, keys[SDL_SCANCODE_COUNT];
static boolean captured;
extern boolean menuactive;
extern boolean paused;

static int translate(SDL_Scancode key)
{
    switch (key)
    {
        case SDL_SCANCODE_LEFT: return KEY_LEFTARROW;
        case SDL_SCANCODE_RIGHT: return KEY_RIGHTARROW;
        case SDL_SCANCODE_UP: return KEY_UPARROW;
        case SDL_SCANCODE_DOWN: return KEY_DOWNARROW;
        case SDL_SCANCODE_ESCAPE: return KEY_ESCAPE;
        case SDL_SCANCODE_RETURN: return KEY_ENTER;
        case SDL_SCANCODE_TAB: return KEY_TAB;
        case SDL_SCANCODE_BACKSPACE: return KEY_BACKSPACE;
        case SDL_SCANCODE_DELETE: return KEY_BACKSPACE;
        case SDL_SCANCODE_PAUSE: return KEY_PAUSE;
        case SDL_SCANCODE_LSHIFT: case SDL_SCANCODE_RSHIFT: return KEY_RSHIFT;
        case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL: return KEY_RCTRL;
        case SDL_SCANCODE_LALT: case SDL_SCANCODE_RALT: return KEY_RALT;
        case SDL_SCANCODE_EQUALS: return KEY_EQUALS;
        case SDL_SCANCODE_MINUS: return KEY_MINUS;
        case SDL_SCANCODE_SPACE: return ' ';
        default:
            if (key >= SDL_SCANCODE_A && key <= SDL_SCANCODE_Z)
                return 'a' + key - SDL_SCANCODE_A;
            if (key >= SDL_SCANCODE_1 && key <= SDL_SCANCODE_9)
                return '1' + key - SDL_SCANCODE_1;
            if (key == SDL_SCANCODE_0) return '0';
            if (key >= SDL_SCANCODE_F1 && key <= SDL_SCANCODE_F10)
                return KEY_F1 + key - SDL_SCANCODE_F1;
            if (key == SDL_SCANCODE_F11) return KEY_F11;
            if (key == SDL_SCANCODE_F12) return KEY_F12;
            if (key == SDL_SCANCODE_COMMA) return ',';
            if (key == SDL_SCANCODE_PERIOD) return '.';
            if (key == SDL_SCANCODE_LEFTBRACKET) return '[';
            if (key == SDL_SCANCODE_RIGHTBRACKET) return ']';
            return 0;
    }
}

static void post(evtype_t type, int a, int b, int c)
{
    event_t event = { type, a, b, c };
    D_PostEvent(&event);
}

static void capture_mouse(boolean value)
{
    if (value != captured)
    {
        SDL_SetWindowRelativeMouseMode(window, value);
        captured = value;
    }
}

void I_InitGraphics(void)
{
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        I_Error("Could not initialize graphics: %s", SDL_GetError());
    int width = 1280, height = 800;
    int arg = M_CheckParm("-width");
    if (arg && arg + 1 < myargc) width = atoi(myargv[arg + 1]);
    arg = M_CheckParm("-height");
    if (arg && arg + 1 < myargc) height = atoi(myargv[arg + 1]);
    if (width < 320 || width > 8192 || height < 240 || height > 8192)
        I_Error("Window dimensions must be between 320x240 and 8192x8192");
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    // Render checks draw off screen: no window comes up over the user's work
    if (M_CheckParm("-rendercheck")) flags |= SDL_WINDOW_HIDDEN;
    char title[128];
    snprintf(title, sizeof(title), "%s — Esc: menu · F11: fullscreen · Cmd-Q: quit", Game_Title());
    window = SDL_CreateWindow(title, width, height, flags);
    if (!window)
        I_Error("Could not open the game window: %s", SDL_GetError());
    SDL_SetWindowMinimumSize(window, 320, 240);
#ifdef DOOM_RENDER3D
    accel = I_Render3DInit(window);
#endif
    if (
#ifdef DOOM_RENDER3D
        !accel
#else
        true
#endif
    )
    {
        renderer = SDL_CreateRenderer(window, NULL);
        if (!renderer) I_Error("Could not create renderer: %s", SDL_GetError());
        SDL_SetRenderLogicalPresentation(renderer, 320, 240, SDL_LOGICAL_PRESENTATION_LETTERBOX);
        SDL_SetRenderVSync(renderer, 1);
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                   SDL_TEXTUREACCESS_STREAMING, SCREENWIDTH, SCREENHEIGHT);
        if (!texture) I_Error("Could not create the game image: %s", SDL_GetError());
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    }
    if (M_CheckParm("-fullscreen"))
        SDL_SetWindowFullscreen(window, true);
}

void I_ShutdownGraphics(void)
{
#ifdef DOOM_RENDER3D
    if (accel) { I_Render3DShutdown(); accel = false; }
#endif
    SDL_DestroyTexture(texture); texture = NULL;
    SDL_DestroyRenderer(renderer); renderer = NULL;
    SDL_DestroyWindow(window); window = NULL;
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
void I_StartFrame(void)
{
#ifdef DOOM_RENDER3D
    if (accel) { I_Render3DStartFrame(); I_StartTic(); }
#endif
}
void I_UpdateNoBlit(void) { }

void I_StartTic(void)
{
    if (!window)
        return;
    boolean focused = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
#ifdef DOOM_TEST_DRIVER
    focused = false; /* Automated input must not capture the user's mouse. */
#endif
    capture_mouse(focused && gamestate == GS_LEVEL && !menuactive && !paused);
    int dx = 0;
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
            I_Quit();
#ifdef DOOM_TEST_DRIVER
        /* Keyboard, mouse and focus events must not overwrite D_PostEvent
         * fixtures or move/fire while the user works in another window. */
        continue;
#endif
        if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP)
        {
#ifdef DOOM_TEST_DRIVER
            /* Test input comes from D_PostEvent, independent of desktop keys. */
            continue;
#endif
            SDL_Scancode scan = event.key.scancode;
            if (scan < 0 || scan >= SDL_SCANCODE_COUNT)
                continue;
#ifdef DOOM_RENDER3D
            /* The in-game F4 panel takes the keyboard, key repeat included. */
            if (accel && I_Render3DSettingsOpen() && scan != SDL_SCANCODE_F7)
            {
                if (event.type == SDL_EVENT_KEY_DOWN)
                    I_Render3DSettingsKey(scan, (event.key.mod & SDL_KMOD_SHIFT) != 0);
                continue;
            }
#endif
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.repeat)
                continue;
            if (event.type == SDL_EVENT_KEY_DOWN &&
                scan == SDL_SCANCODE_Q && (event.key.mod & SDL_KMOD_GUI))
                I_Quit();
#ifdef DOOM_RENDER3D
            if(accel&&scan==SDL_SCANCODE_Q){
                I_Render3DWheelKey(event.type==SDL_EVENT_KEY_DOWN);
                for(int i=0;i<SDL_SCANCODE_COUNT;++i)if(keys[i]==KEY_RCTRL||keys[i]=='r'){
                    post(ev_keyup,keys[i],0,0);keys[i]=0;
                }
                buttons=0;post(ev_mouse,0,0,0);dx=0;continue;
            }
            if(accel&&I_Render3DWheelOpen()&&event.type==SDL_EVENT_KEY_DOWN&&
                (scan==SDL_SCANCODE_ESCAPE||scan==SDL_SCANCODE_F4||scan==SDL_SCANCODE_PAUSE))I_Render3DWheelKey(-1);
#endif
            if (scan == SDL_SCANCODE_F11)
            {
                if (event.type == SDL_EVENT_KEY_DOWN)
                {
                    int fullscreen=!(SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN);
                    if(SDL_SetWindowFullscreen(window,fullscreen)){
#ifdef DOOM_RENDER3D
                        if(accel)I_Render3DFullscreenChanged(fullscreen);
#endif
                    }
                }
                continue;
            }
#ifdef DOOM_RENDER3D
            if (accel && I_Render3DIsAccelerated() && scan == SDL_SCANCODE_F &&
                gamestate == GS_LEVEL && !menuactive && !paused)
            {
                /* Also passed on, so cheats such as "idfly" can be typed. */
                if (event.type == SDL_EVENT_KEY_DOWN) I_Render3DToggleFlashlight();
            }
            else if (accel && (scan == SDL_SCANCODE_F4 || scan == SDL_SCANCODE_F7))
            {
                if (event.type == SDL_EVENT_KEY_DOWN)
                {
                    if (scan == SDL_SCANCODE_F7) I_Render3DScreenshot();
                    else
                    {
                        capture_mouse(false);
                        for (int i = 0; i < SDL_SCANCODE_COUNT; ++i)
                            if (keys[i]) { post(ev_keyup, keys[i], 0, 0); keys[i] = 0; }
                        buttons = 0; post(ev_mouse, 0, 0, 0);
                        I_Render3DSettings();
                    }
                }
                continue;
            }
#endif
            int key = translate(scan);
            if (key)
            {
                keys[scan] = event.type == SDL_EVENT_KEY_DOWN ? key : 0;
                post(event.type == SDL_EVENT_KEY_DOWN ? ev_keydown : ev_keyup, key, 0, 0);
            }
        }
        else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
        {
#ifdef DOOM_RENDER3D
            if(accel)I_Render3DWheelKey(-1);
#endif
            for (int i = 0; i < SDL_SCANCODE_COUNT; ++i)
                if (keys[i]) { post(ev_keyup, keys[i], 0, 0); keys[i] = 0; }
            buttons = 0;
            capture_mouse(false);
            post(ev_mouse, 0, 0, 0);
        }
        else if (event.type == SDL_EVENT_MOUSE_MOTION && captured)
        {
#ifdef DOOM_RENDER3D
            if(accel&&I_Render3DWheelOpen()){I_Render3DWheelMove(event.motion.xrel,event.motion.yrel);continue;}
#endif
            dx += (int)event.motion.xrel;
#ifdef DOOM_RENDER3D
            if (accel) I_Render3DLook(event.motion.yrel);
#endif
        }
#ifdef DOOM_RENDER3D
        else if (accel && I_Render3DSettingsOpen() && event.type == SDL_EVENT_MOUSE_MOTION)
            I_Render3DSettingsMouse(event.motion.x, event.motion.y, 0);
        else if (accel && I_Render3DSettingsOpen() &&
                 (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP))
        {
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
                I_Render3DSettingsMouse(event.button.x, event.button.y,
                                        event.button.button == SDL_BUTTON_RIGHT ? 3 : 1);
        }
        else if (accel && I_Render3DSettingsOpen() && event.type == SDL_EVENT_MOUSE_WHEEL)
        {
            if (event.wheel.y != 0)
                I_Render3DSettingsKey(event.wheel.y > 0 ? SDL_SCANCODE_UP : SDL_SCANCODE_DOWN, 0);
        }
#endif
        else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP)
        {
#ifdef DOOM_RENDER3D
            if(accel&&I_Render3DWheelOpen()){buttons=0;post(ev_mouse,0,0,0);continue;}
#endif
            int mask = event.button.button == SDL_BUTTON_LEFT ? 1
                     : event.button.button == SDL_BUTTON_RIGHT ? 2
                     : event.button.button == SDL_BUTTON_MIDDLE ? 4 : 0;
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) buttons |= mask;
            else buttons &= ~mask;
            post(ev_mouse, buttons, 0, 0);
        }
    }
    if (dx)
        post(ev_mouse, buttons, dx * 4, 0);
}

void I_SetPalette(byte *data)
{
    for (int i = 0; i < 256; ++i)
        palette[i] = UINT32_C(0xff000000)
                   | (uint32_t)gammatable[usegamma][data[3*i]] << 16
                   | (uint32_t)gammatable[usegamma][data[3*i+1]] << 8
                   | gammatable[usegamma][data[3*i+2]];
}
void I_FinishUpdate(void)
{
#ifdef DOOM_TEST_DRIVER
    extern void I_TestFrame(void);
    I_TestFrame();
#endif
    for (int i = 0; i < SCREENWIDTH * SCREENHEIGHT; ++i)
        pixels[i] = palette[screens[0][i]];
#ifdef DOOM_RENDER3D
    if (accel) { I_Render3DPresent(pixels, palette); return; }
#endif
    SDL_UpdateTexture(texture, NULL, pixels, SCREENWIDTH * sizeof(uint32_t));
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}
void I_ReadScreen(byte *screen)
{
    memcpy(screen, screens[0], SCREENWIDTH * SCREENHEIGHT);
}
