/* Accelerated 3D renderer: gpu3d/ draws the scene from scene3d/ with
   SDL_gpu (Metal on macOS, Vulkan elsewhere). */
#ifndef DOOM_RENDER3D_H
#define DOOM_RENDER3D_H
#ifdef __cplusplus
extern "C" {
#endif
struct SDL_Window;
int I_Render3DInit(struct SDL_Window *window);
void I_Render3DShutdown(void);
void I_Render3DStartFrame(void);
void I_Render3DPresent(const unsigned *pixels, const unsigned *palette);
void I_Render3DLook(float delta);
void I_Render3DSettings(void);
void I_Render3DFullscreenChanged(int enabled);
/* The in-game F4 panel (settings_menu.cpp), opened by I_Render3DSettings.
   It draws a 640x400 image that the renderer scales by whole pixels; clicks
   use that image's coordinates (button 0: hover, 1: left, 3: right).
   I_Render3DSettingsMouse takes window coordinates. */
#define DOOM_SETTINGS_WIDTH 640
#define DOOM_SETTINGS_HEIGHT 400
void I_Render3DSettingsMenu(void);
int I_Render3DSettingsOpen(void);
void I_Render3DSettingsKey(int scancode, int shift);
void I_Render3DSettingsClick(int x, int y, int button);
void I_Render3DSettingsDraw(unsigned *pixels);
void I_Render3DSettingsMouse(float x, float y, int button);
/* The loading screen, in the same 640x400 image and font: title, a bar
   fraction (0-1) full and the stage under it, on black. */
void I_Render3DLoadingDraw(unsigned *pixels, const char *title, const char *stage, float fraction);
void I_Render3DScreenshot(void);
void I_Render3DToggleFlashlight(void);
int I_Render3DIsAccelerated(void);
int I_Render3DModernHUD(void);
/* Frame profiling for the performance smoke test (tests/perf_smoke.c). While
   enabled, frames present without vsync and each waits for the GPU so its
   time can be read; the view stays level. skip leaves out lights of the kinds
   1 << I_RENDER3D_LIGHT_* and the I_RENDER3D_SKIP_* passes, so their cost
   shows as the difference. */
enum {
    I_RENDER3D_LIGHT_FLASHLIGHT, I_RENDER3D_LIGHT_SHOT, I_RENDER3D_LIGHT_BARREL,
    I_RENDER3D_LIGHT_DECORATION, I_RENDER3D_LIGHT_PROJECTILE, I_RENDER3D_LIGHT_DOOR,
    I_RENDER3D_LIGHT_SURFACE, I_RENDER3D_LIGHT_FOG, I_RENDER3D_LIGHT_KINDS
};
#define I_RENDER3D_SKIP_LIGHTS 0xffu
#define I_RENDER3D_SKIP_REFLECTIONS (1u << 8)
#define I_RENDER3D_SKIP_BLOOM (1u << 9)
#define I_RENDER3D_SKIP_FOG (1u << 10)
#define I_RENDER3D_SKIP_MIST (1u << 11)
typedef struct {
    float x, y, z, radius;
    int kind;      /* I_RENDER3D_LIGHT_* */
    int thing;     /* The source thing's mobjtype_t, or -1. */
    int blockers;  /* Wall segments each pixel it reaches tests. */
    int triangles; /* World triangles within its radius. */
    float perPixel; /* Blockers a pixel tests on average, with slices. */
} I_Render3DLightProfile;
typedef struct {
    unsigned serial;  /* Counts profiled frames. */
    int world;        /* The frame drew the 3D view. */
    double cpuMs;     /* Building, encoding and submitting the frame. */
    double gpuMs;     /* From submission until the GPU finished. */
    double prepareMs; /* Scene build, including level-load bakes. */
    double geometryMs, lightsMs, spritesMs, masksMs;
    int lights, blockers, triangles, litTriangles, lightTriangles, reflection;
    I_Render3DLightProfile light[64];
} I_Render3DFrameProfile;
void I_Render3DProfile(int enabled, unsigned skip);
const I_Render3DFrameProfile *I_Render3DLastFrame(void);
/* Drops lights still fading in or out from the previous view. */
void I_Render3DProfileTeleported(void);
/* The next screenshot goes to path instead of Screenshots/. */
void I_Render3DScreenshotTo(const char *path);
int I_Render3DWheelOpen(void);
void I_Render3DWheelKey(int down);
int I_Render3DPeekKey(int scancode,int down);
void I_Render3DPeekReset(void);
void I_Render3DWheelMove(float x,float y);
#ifdef __cplusplus
}
#endif
#endif
