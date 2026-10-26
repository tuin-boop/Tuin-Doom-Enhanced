/* Internal to the SDL_gpu backend in platform/gpu3d/: the device, pipelines,
   targets and helpers its files share. The scene it draws is scene3d.h. */
#ifndef DOOM_GPU3D_INTERNAL_H
#define DOOM_GPU3D_INTERNAL_H
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>
extern "C" {
#include "doomdef.h"
#include "doomstat.h"
#include "r_sky.h"
#include "m_argv.h"
#include "i_system.h"
extern boolean paused;
}
#include <SDL3/SDL.h>
#include "../i_render3d.h"
#include "../scene3d.h"

struct GpuTexture {
    SDL_GPUTexture *texture;
    ~GpuTexture();
};

namespace gpu3d {
using namespace doom3d;

// device.cpp
extern SDL_GPUDevice *device;
extern SDL_GPUShaderFormat shaderFormat;
extern SDL_GPUTextureFormat depthFormat,swapchainFormat;
extern int maxSamples;extern bool forceSingleSample;
extern SDL_GPUSampler *linearRepeat,*linearClamp,*nearestClamp;
// Fixed textures: the HUD and settings panel images, the palette and its
// 32³ lookup, 1x1 stand-ins for missing scene maps, a covered solid texel.
extern SDL_GPUTexture *settingsTexture,*hudTexture,*paletteTexture,*paletteLUT,*dummySeam,*dummyContact,*dummyBake,*dummyRelief,*solidTexel;
SDL_GPUTexture *createTexture(SDL_GPUTextureFormat format,int w,int h,SDL_GPUTextureUsageFlags usage,
                              SDL_GPUSampleCount samples=SDL_GPU_SAMPLECOUNT_1,SDL_GPUTextureType type=SDL_GPU_TEXTURETYPE_2D,int depth=1);
void flushUploads();
void updatePaletteLUT(const unsigned *palette);

// pipelines.cpp
extern SDL_GPUSampleCount sampleCount;
extern SDL_GPUGraphicsPipeline *opaquePipeline,*skyPipeline,*skySurfacePipeline,*reflectSkySurfacePipeline,*worldPipeline,*shadowPipeline,*decalPipeline,*effectAddPipeline,*particlePipeline,*crestPipeline,*heatPipeline,
    *weaponPipeline,*weaponAddPipeline,*hudPipeline,*solidPipeline,*mistPipeline,*shaftPipeline,*overlayWeaponPipeline,*overlayWeaponAddPipeline,*overlayHudPipeline,*overlaySolidPipeline,
    *reflectOpaquePipeline,*reflectSkyPipeline,*reflectWorldPipeline,*emissionPipeline,*rubbleEmissionPipeline,*blurPipeline,*presentPipeline,*depthPipeline,*prepassPipeline,*coveragePipeline,*cutoutPipeline,
    *settingsPipeline,*glassPipeline,*reflectPrepassPipeline;
// Render targets at the render resolution (width x height); bloom at half.
extern SDL_GPUTexture *multisampleTexture,*colorTexture,*depthTexture,*sceneDepth,*emissionTexture,*emissionDepth,*bloomScratch,*bloomTexture;
extern int width,height;
bool createPipelines();
void releasePipelines();
void releaseTargets();
void allocateTargets(int w,int h);
void applySampleCount();

// draw.cpp
void uploadFrame(SDL_GPUCommandBuffer *command,const std::vector<const std::vector<Vertex>*> &transient,
                 const std::vector<std::array<float,4>> &sectors,const unsigned *palette,const std::vector<unsigned> &hud,
                 const std::vector<unsigned> &panel);
void releaseFrameUploads();
void drawVertices(SDL_GPURenderPass *pass,const std::vector<Vertex> &vertices);
void setView(SDL_GPURenderPass *pass,float x,float y,float w,float h,const SDL_Rect *scissor=nullptr);
void bindWorld(SDL_GPURenderPass *pass,SDL_GPUTexture *reflection);
void bindEmissionEdges(SDL_GPUCommandBuffer *command,SDL_GPURenderPass *pass,const Uniforms &camera);
// Just the lights' blocker buffer, for passes that only light highlights (glass.frag).
void bindLightBlockers(SDL_GPURenderPass *pass);
void bindSurface(SDL_GPUCommandBuffer *command,SDL_GPURenderPass *pass,int key,Uint32 blendSlot,bool world=true);
void bindImage(SDL_GPURenderPass *pass,const GpuTextureRef &texture,SDL_GPUSampler *filter);
void pushWorldUniforms(SDL_GPUCommandBuffer *command,const Uniforms &view);
bool opaqueBatch(const std::pair<const int,std::vector<Vertex>> &batch);
void drawWorld(SDL_GPUCommandBuffer *command,SDL_GPURenderPass *pass,SDL_GPUGraphicsPipeline *opaque,SDL_GPUGraphicsPipeline *skySurface,
               SDL_GPUGraphicsPipeline *sky,SDL_GPUGraphicsPipeline *world,SDL_GPUTexture *reflection,
               SDL_GPUGraphicsPipeline *coverage=nullptr,SDL_GPUGraphicsPipeline *cutout=nullptr,bool sprites=true);
void drawOverlays(SDL_GPUCommandBuffer *command,SDL_GPURenderPass *pass,const FrameView &view,
                  const std::vector<SpriteDraw> &weapons,const std::vector<Vertex> &hud,const std::vector<Vertex> &cross,bool depth);

// frame.cpp
void releaseReflection();
// A 640x400 panel image (settings, loading) at a whole-pixel scale in the
// middle of the drawable, as a quad over all of it; origin and scale in drawable pixels.
std::vector<Vertex> panelQuad(Uint32 drawableWidth,Uint32 drawableHeight,float origin[2],float &scale);

// loading.cpp
void showLoading(const char *title,const char *stage,float fraction);
void releaseLoading();

inline SDL_GPUTexture *gpu(const GpuTextureRef &texture) {return texture?texture->texture:nullptr;}
template<class T> inline void release(T *&object,void (*destroy)(SDL_GPUDevice*,T*)) {if(object&&device)destroy(device,object);object=nullptr;}
inline void release(SDL_GPUTexture *&texture) {release(texture,SDL_ReleaseGPUTexture);}
inline void release(SDL_GPUGraphicsPipeline *&pipeline) {release(pipeline,SDL_ReleaseGPUGraphicsPipeline);}
} // namespace gpu3d
#endif
