extern "C" int Menu_BrandOpen(void);
/* One frame: the scene prepared by prepareFrame drawn in passes (liquid and
   glass reflections, depth for the mist, the main view with mist and overlays,
   bloom), then presented with the HUD and the settings panel. */
#include "internal.h"
extern "C" int Arcade_PodiumRect(float*);
extern "C" int Arcade_MenuOpen(void);
extern "C" int director_view_active;
namespace doom3d {extern GpuTextureRef arcadeRankTexture;}

namespace gpu3d {
// The in-game settings panel: a quad over the whole drawable whose texture
// coordinates place the 640x400 image at a whole-pixel scale in the middle;
// beyond it the clamped border texels dim the view.
static std::vector<Vertex> settingsQuad;
static std::vector<Vertex> toastyQuad,romeroQuad,arcadePodiumQuad;
static std::vector<Vertex> arcadeRankQuad;
static float settingsOrigin[2]={},settingsScale=1;
static void present(SDL_GPUCommandBuffer *command,SDL_GPUTexture *target,const FrameView &view) {
    SDL_GPUColorTargetInfo info={};
    info.texture=target;info.load_op=SDL_GPU_LOADOP_DONT_CARE;info.store_op=SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(command,&info,1,nullptr);
    SDL_BindGPUGraphicsPipeline(pass,presentPipeline);
    float hudRect[8]={view.uiX/width,view.uiY/height,view.uiWidth/width,view.uiHeight/height,settings.hudLayout==1?0.f:1.f,0,0,0};
    SDL_PushGPUFragmentUniformData(command,0,hudRect,sizeof(hudRect));
    SDL_PushGPUFragmentUniformData(command,1,&view.camera,sizeof(view.camera));
    SDL_GPUTextureSamplerBinding samplers[]={{colorTexture,linearClamp},{bloomTexture,linearClamp},{hudTexture,linearClamp},{paletteLUT,nearestClamp}};
    SDL_BindGPUFragmentSamplers(pass,0,samplers,4);
    SDL_DrawGPUPrimitives(pass,3,1,0,0);
    if(!toastyQuad.empty() && toastyTexture) {
        SDL_BindGPUGraphicsPipeline(pass,settingsPipeline);
        SDL_GPUTextureSamplerBinding portrait={gpu(toastyTexture),linearClamp};
        SDL_BindGPUFragmentSamplers(pass,0,&portrait,1);
        drawVertices(pass,toastyQuad);
    }
    if(!romeroQuad.empty() && romeroTexture) {
        SDL_BindGPUGraphicsPipeline(pass,settingsPipeline);
        SDL_GPUTextureSamplerBinding portrait={gpu(romeroTexture),linearClamp};
        SDL_BindGPUFragmentSamplers(pass,0,&portrait,1);drawVertices(pass,romeroQuad);
    }
    if(!arcadePodiumQuad.empty()&&arcadePodiumTexture){
        SDL_BindGPUGraphicsPipeline(pass,settingsPipeline);
        SDL_GPUTextureSamplerBinding art={gpu(arcadePodiumTexture),linearClamp};
        SDL_BindGPUFragmentSamplers(pass,0,&art,1);drawVertices(pass,arcadePodiumQuad);
    }
    if(!arcadeRankQuad.empty()&&arcadeRankTexture){
        SDL_BindGPUGraphicsPipeline(pass,settingsPipeline);
        SDL_GPUTextureSamplerBinding lettering={gpu(arcadeRankTexture),nearestClamp};
        SDL_BindGPUFragmentSamplers(pass,0,&lettering,1);drawVertices(pass,arcadeRankQuad);
    }
    if(!settingsQuad.empty()) {
        SDL_BindGPUGraphicsPipeline(pass,settingsPipeline);
        SDL_GPUTextureSamplerBinding panel={settingsTexture,nearestClamp};
        SDL_BindGPUFragmentSamplers(pass,0,&panel,1);
        drawVertices(pass,settingsQuad);
    }
    SDL_EndGPURenderPass(pass);
}
static void capture(SDL_GPUCommandBuffer *command,const FrameView &view,int w,int h) {
    SDL_GPUTexture *image=createTexture(swapchainFormat,w,h,SDL_GPU_TEXTUREUSAGE_COLOR_TARGET);
    SDL_GPUTransferBufferCreateInfo info={SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,(Uint32)(w*h*4),0};
    SDL_GPUTransferBuffer *download=image?SDL_CreateGPUTransferBuffer(device,&info):nullptr;
    if(!download) {
        release(image);SDL_SubmitGPUCommandBuffer(command);
        saveScreenshot(nullptr,0,0,0);return;
    }
    present(command,image,view);
    SDL_GPUCopyPass *copy=SDL_BeginGPUCopyPass(command);
    SDL_GPUTextureRegion source={};source.texture=image;source.w=(Uint32)w;source.h=(Uint32)h;source.d=1;
    SDL_GPUTextureTransferInfo target={download,0,(Uint32)w,(Uint32)h};
    SDL_DownloadFromGPUTexture(copy,&source,&target);
    SDL_EndGPUCopyPass(copy);
    SDL_GPUFence *fence=SDL_SubmitGPUCommandBufferAndAcquireFence(command);
    if(fence) {SDL_WaitForGPUFences(device,true,&fence,1);SDL_ReleaseGPUFence(device,fence);}
    std::vector<unsigned> pixels((size_t)w*h);
    memcpy(pixels.data(),SDL_MapGPUTransferBuffer(device,download,false),pixels.size()*4);
    SDL_UnmapGPUTransferBuffer(device,download);
    SDL_ReleaseGPUTransferBuffer(device,download);release(image);
    if(swapchainFormat==SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM)
        for(unsigned &p:pixels)p=(p&0xff00ff00u)|((p&0xffu)<<16)|((p>>16)&0xffu);
    saveScreenshot(fence?pixels.data():nullptr,w,h,w*4);
}
std::vector<Vertex> panelQuad(Uint32 drawableWidth,Uint32 drawableHeight,float origin[2],float &scale) {
    float dw=(float)drawableWidth,dh=(float)drawableHeight;
    scale=std::max(1.0f,std::floor(std::min(dw/DOOM_SETTINGS_WIDTH,dh/DOOM_SETTINGS_HEIGHT)));
    float pw=DOOM_SETTINGS_WIDTH*scale,ph=DOOM_SETTINGS_HEIGHT*scale;
    origin[0]=std::floor((dw-pw)/2);origin[1]=std::floor((dh-ph)/2);
    std::vector<Vertex> quad;
    screenQuad(quad,-1,1,2,2,-origin[0]/pw,-origin[1]/ph,(dw-origin[0])/pw,(dh-origin[1])/ph);
    return quad;
}
// Draws the settings panel into image and places it over the drawable.
static void layoutSettingsPanel(Uint32 drawableWidth,Uint32 drawableHeight,std::vector<unsigned> &image) {
    image.assign((size_t)DOOM_SETTINGS_WIDTH*DOOM_SETTINGS_HEIGHT,0);
    I_Render3DSettingsDraw(image.data());
    settingsQuad=panelQuad(drawableWidth,drawableHeight,settingsOrigin,settingsScale);
}
// Mirrors and probes draw the whole map from elsewhere; a depth prepass of
// the opaque batches first, as in the main view, lets each pixel shade once.
static void mirrorPrepass(SDL_GPURenderPass *pass) {
    SDL_BindGPUGraphicsPipeline(pass,reflectPrepassPipeline);
    for(const auto &batch:batches) if(opaqueBatch(batch)) drawVertices(pass,batch.second);
}
// A mirror's render target at the reflection resolution, reallocated when the view changes size.
struct MirrorTarget { SDL_GPUTexture *color=nullptr,*depth=nullptr; int width=0,height=0; };
static MirrorTarget liquidMirror,glassMirror;
static bool glassReflected; // This frame drew a glass reflection.
// Glass probes: per probe a row of six glassProbeSize faces (+x, -x, +y,
// -y, +z, -z; glass.frag reads them the same way), for glassProbeSerial.
static SDL_GPUTexture *probeAtlas,*probeDepth;
static unsigned probeAtlasSerial=~0u;
// The world drawn again at reduced resolution from the mirrored camera,
// clipped to the eye's side of plane (unit normal toward the eye, offset).
// Its sprites: the main view's, or extra in their place.
static void drawMirror(SDL_GPUCommandBuffer *render,const FrameView &view,const Uniforms &mirror,const float plane[4],MirrorTarget &target,
                       const std::map<int,std::vector<Vertex>> *extra=nullptr) {
    int divisor=settings.retroReflections?8:2;
    int rw=std::max(1,(int)(view.worldW/divisor)),rh=std::max(1,(int)(view.worldH/divisor));
    if(!target.color||rw!=target.width||rh!=target.height) {
        release(target.color);release(target.depth);
        target.color=createTexture(SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,rw,rh,SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER);
        target.depth=createTexture(depthFormat,rw,rh,SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET);
        if(!target.color||!target.depth)I_Error((char*)"Could not allocate reflection targets");
        target.width=rw;target.height=rh;
    }
    SDL_GPUColorTargetInfo color={};
    color.texture=target.color;color.load_op=SDL_GPU_LOADOP_CLEAR;color.store_op=SDL_GPU_STOREOP_STORE;
    color.clear_color={0.025f,0.025f,0.03f,1};
    SDL_GPUDepthStencilTargetInfo depth={};
    depth.texture=target.depth;depth.clear_depth=1;depth.load_op=SDL_GPU_LOADOP_CLEAR;depth.store_op=SDL_GPU_STOREOP_DONT_CARE;
    depth.stencil_load_op=SDL_GPU_LOADOP_DONT_CARE;depth.stencil_store_op=SDL_GPU_STOREOP_DONT_CARE;
    SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(render,&color,1,&depth);
    pushWorldUniforms(render,mirror);
    SDL_PushGPUVertexUniformData(render,1,plane,4*sizeof(float));
    mirrorPrepass(pass);
    drawWorld(render,pass,reflectOpaquePipeline,reflectSkySurfacePipeline,reflectSkyPipeline,reflectWorldPipeline,nullptr,nullptr,nullptr,!extra);
    if(extra) {
        SDL_BindGPUGraphicsPipeline(pass,reflectWorldPipeline);bindWorld(pass,nullptr);
        for(const auto &batch:*extra) if(!batch.second.empty()) {bindSurface(render,pass,batch.first,3);drawVertices(pass,batch.second);}
    }
    SDL_EndGPURenderPass(pass);
}
// Liquid reflections: the camera mirrored at the liquid height, clipped to
// above the surface. Its alpha holds how near each point is to the surface
// along the mirrored ray; the mirror pass is flagged by water = (plane, 0, 0, 1).
static void drawReflection(SDL_GPUCommandBuffer *render,const FrameView &view,Uniforms &camera) {
    camera.water[2]=camera.water[3]=0;
    if(!worldPending||!reflectionActive)return;
    Uniforms mirror=camera;
    mirror.eye[2]=2*reflectionPlane-camera.eye[2];mirror.forward[2]=-camera.forward[2];mirror.up[2]=-camera.up[2];
    mirror.water[0]=reflectionPlane;mirror.water[3]=1;
    float plane[4]={0,0,1,reflectionPlane};
    drawMirror(render,view,mirror,plane,liquidMirror);
    camera.water[0]=view.worldX;camera.water[1]=view.worldY;camera.water[2]=1/view.worldW;camera.water[3]=1/view.worldH;
}
// Glass probes: each probe in glassCaptures drawn six times into its row of
// the atlas, a 90-degree view per cube face from the probe. Only the map,
// as its bakes light it: no sprites, no dynamic lights (they would freeze
// in the glass), no fog and no eye adaptation.
static void captureGlassProbes(SDL_GPUCommandBuffer *render,const Uniforms &camera) {
    if(!worldPending)return;
    if(probeAtlasSerial!=glassProbeSerial) {
        release(probeAtlas);release(probeDepth);probeAtlasSerial=glassProbeSerial;
        if(!glassProbes.empty()) {
            int w=6*glassProbeSize,h=(int)glassProbes.size()*glassProbeSize;
            probeAtlas=createTexture(SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,w,h,SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER);
            probeDepth=createTexture(depthFormat,w,h,SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET);
            if(!probeAtlas||!probeDepth)I_Error((char*)"Could not allocate the glass probe atlas");
        }
    }
    if(glassCaptures.empty()||!probeAtlas)return;
    static const float faces[6][2][3]={{{1,0,0},{0,0,1}},{{-1,0,0},{0,0,1}},{{0,1,0},{0,0,1}},{{0,-1,0},{0,0,1}},{{0,0,1},{0,1,0}},{{0,0,-1},{0,1,0}}};
    SDL_GPUColorTargetInfo color={};
    color.texture=probeAtlas;color.load_op=SDL_GPU_LOADOP_LOAD;color.store_op=SDL_GPU_STOREOP_STORE;
    SDL_GPUDepthStencilTargetInfo depth={};
    depth.texture=probeDepth;depth.clear_depth=1;depth.load_op=SDL_GPU_LOADOP_CLEAR;depth.store_op=SDL_GPU_STOREOP_DONT_CARE;
    depth.stencil_load_op=SDL_GPU_LOADOP_DONT_CARE;depth.stencil_store_op=SDL_GPU_STOREOP_DONT_CARE;
    SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(render,&color,1,&depth);
    static const FlashSet noFlashes;static const FogLights noFog;
    const float noClip[4]={0,0,0,-1e9f};
    for(int index:glassCaptures) {
        const GlassProbe &probe=glassProbes[index];
        for(int face=0;face<6;++face) {
            Uniforms view=camera;
            const float *f=faces[face][0],*u=faces[face][1];
            for(int i=0;i<3;++i) {view.eye[i]=probe.at[i];view.forward[i]=f[i];view.up[i]=u[i];}
            view.right[0]=f[1]*u[2]-f[2]*u[1];view.right[1]=f[2]*u[0]-f[0]*u[2];view.right[2]=f[0]*u[1]-f[1]*u[0];
            view.projection[0]=view.projection[1]=1;
            for(float &w:view.water)w=0;
            view.materials[2]=0;view.fx[0]=1;view.fx2[0]=glassProbeSize/200.0f;
            setView(pass,(float)(face*glassProbeSize),(float)(index*glassProbeSize),(float)glassProbeSize,(float)glassProbeSize);
            pushWorldUniforms(render,view);
            SDL_PushGPUFragmentUniformData(render,1,&noFlashes,sizeof(noFlashes));
            SDL_PushGPUFragmentUniformData(render,2,&noFog,sizeof(noFog));
            SDL_PushGPUVertexUniformData(render,1,noClip,sizeof(noClip));
            mirrorPrepass(pass);
            drawWorld(render,pass,reflectOpaquePipeline,reflectSkySurfacePipeline,reflectSkyPipeline,reflectWorldPipeline,nullptr,nullptr,nullptr,false);
        }
    }
    SDL_EndGPURenderPass(pass);
}
// Glass reflections: the camera and its axes mirrored across the chosen
// window's upright plane (glassMirrorPlane), so each point of the mirrored
// view lands where its reflection shows on the pane and glass.frag reads it
// at its own screen position. water stays 0: nothing in it is a liquid
// mirror. Its sprites are its own (mirrorBatches), the player's included.
static bool drawGlassReflection(SDL_GPUCommandBuffer *render,const FrameView &view,const Uniforms &camera) {
    if(!worldPending||!glassMirrorActive)return false;
    const float *n=glassMirrorPlane;
    Uniforms mirror=camera;
    float side=n[0]*camera.eye[0]+n[1]*camera.eye[1]-n[3];
    mirror.eye[0]-=2*side*n[0];mirror.eye[1]-=2*side*n[1];
    for(float *axis:{mirror.forward,mirror.right,mirror.up}) {
        float along=n[0]*axis[0]+n[1]*axis[1];
        axis[0]-=2*along*n[0];axis[1]-=2*along*n[1];
    }
    for(float &w:mirror.water)w=0;
    drawMirror(render,view,mirror,n,glassMirror,&mirrorBatches);
    return true;
}
// Soft mist needs single-sample scene depth; under MSAA a depth-only prepass supplies it.
static void drawMistDepth(SDL_GPUCommandBuffer *render,const FrameView &view,const Uniforms &camera) {
    SDL_GPUDepthStencilTargetInfo depth={};
    depth.texture=sceneDepth;depth.clear_depth=1;depth.load_op=SDL_GPU_LOADOP_CLEAR;depth.store_op=SDL_GPU_STOREOP_STORE;
    depth.stencil_load_op=SDL_GPU_LOADOP_DONT_CARE;depth.stencil_store_op=SDL_GPU_STOREOP_DONT_CARE;
    SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(render,nullptr,0,&depth);
    setView(pass,view.worldX,view.worldY,view.worldW,view.worldH);
    SDL_BindGPUGraphicsPipeline(pass,depthPipeline);
    SDL_PushGPUVertexUniformData(render,0,&camera,sizeof(camera));
    for(const auto &batch:batches) if(!batch.second.empty()) {bindImage(pass,images.at(batch.first).texture,nearestClamp);drawVertices(pass,batch.second);}
    SDL_GPUTextureSamplerBinding solid={solidTexel,nearestClamp};
    SDL_BindGPUFragmentSamplers(pass,0,&solid,1);drawVertices(pass,skyVertices);
    SDL_EndGPURenderPass(pass);
}
// The world, its shadows, decals, particles and heat haze.
static void drawScene(SDL_GPUCommandBuffer *render,SDL_GPURenderPass *pass,const FrameView &view,const Uniforms &camera) {
    setView(pass,view.worldX,view.worldY,view.worldW,view.worldH);
    // Only fully covered textures use early depth writes. Cutouts and
    // sprites retain discard-aware shading after the sky background.
    // The whole map is drawn each frame in texture order, so without a
    // depth prepass hidden walls and floors would run the full lighting
    // before nearer ones cover them; with it each pixel shades once.
    SDL_BindGPUGraphicsPipeline(pass,prepassPipeline);
    for(const auto &batch:batches) if(opaqueBatch(batch)) drawVertices(pass,batch.second);
    drawWorld(render,pass,opaquePipeline,skySurfacePipeline,skyPipeline,worldPipeline,camera.water[2]>0?liquidMirror.color:nullptr,
              coveragePipeline,cutoutPipeline);
    // The completed world depth hides shadows behind walls and enemies.
    SDL_BindGPUGraphicsPipeline(pass,shadowPipeline);
    for(const auto &batch:shadowBatches) {bindImage(pass,images.at(-1-batch.first).texture,linearClamp);drawVertices(pass,batch.second);}
    SDL_BindGPUGraphicsPipeline(pass,decalPipeline);
    for(const auto &batch:decalBatches) {bindImage(pass,images.at(-1-batch.first).texture,linearClamp);drawVertices(pass,batch.second);}
    SDL_BindGPUGraphicsPipeline(pass,particlePipeline);drawVertices(pass,particleVertices);
    SDL_BindGPUGraphicsPipeline(pass,decalPipeline);
    for(const auto &batch:effectBatches) {bindImage(pass,images.at(-1-batch.first).texture,linearClamp);drawVertices(pass,batch.second);}
    SDL_BindGPUGraphicsPipeline(pass,effectAddPipeline);
    for(const auto &batch:effectAddBatches) {bindImage(pass,images.at(-1-batch.first).texture,linearClamp);drawVertices(pass,batch.second);}
    SDL_BindGPUGraphicsPipeline(pass,crestPipeline);drawVertices(pass,crestVertices);
    // Window glass over everything behind it, shards included; it writes no depth.
    if(!glassVertices.empty()) {
        SDL_BindGPUGraphicsPipeline(pass,glassPipeline);
        SDL_GPUTextureSamplerBinding images[]={{gpu(wallImage(skytexture).texture),linearRepeat},{paletteTexture,nearestClamp},
                                              {glassReflected?glassMirror.color:paletteTexture,nearestClamp},
                                              {probeAtlas?probeAtlas:paletteTexture,nearestClamp}};
        SDL_BindGPUFragmentSamplers(pass,0,images,4);bindLightBlockers(pass);
        // Where the mirrored view sits on screen (z is 0 without a
        // reflection), the probe face size and the mirror's fade-in, then each
        // probe's point and box.
        static std::vector<std::array<float,4>> mirror(2+3*maxGlassProbes);
        mirror[0]={view.worldX,view.worldY,glassReflected?1/view.worldW:0,1/view.worldH};
        mirror[1]={(float)glassProbeSize,glassMirrorFade,0,0};
        for(size_t i=0;i<glassProbes.size();++i) {
            const GlassProbe &p=glassProbes[i];
            mirror[2+3*i]={p.at[0],p.at[1],p.at[2],0};mirror[3+3*i]={p.low[0],p.low[1],p.low[2],0};mirror[4+3*i]={p.high[0],p.high[1],p.high[2],0};
        }
        SDL_PushGPUFragmentUniformData(render,3,mirror.data(),(Uint32)(mirror.size()*sizeof(mirror[0])));
        drawVertices(pass,glassVertices);
    }
    // Hot air marks the color target's alpha for the shimmer in present.
    if(!heatVertices.empty()) {SDL_BindGPUGraphicsPipeline(pass,heatPipeline);drawVertices(pass,heatVertices);}
}
// Soft mist and sunbeams over the finished scene, against its depth.
static void drawMistPass(SDL_GPUCommandBuffer *render,SDL_GPURenderPass *pass,const Uniforms &camera,bool msaa) {
    SDL_BindGPUGraphicsPipeline(pass,mistPipeline);
    SDL_PushGPUVertexUniformData(render,0,&camera,sizeof(camera));
    SDL_PushGPUFragmentUniformData(render,0,&camera,sizeof(camera));
    SDL_GPUTextureSamplerBinding mist[]={{gpu(cloudTexture),linearRepeat},{msaa?sceneDepth:depthTexture,nearestClamp}};
    SDL_BindGPUFragmentSamplers(pass,0,mist,2);
    drawVertices(pass,mistVertices);
    if(!shaftVertices.empty()) {
        SDL_BindGPUGraphicsPipeline(pass,shaftPipeline);SDL_BindGPUFragmentSamplers(pass,0,mist,2);
        drawVertices(pass,shaftVertices);
    }
}
// The scene with mist and the overlays on top, into colorTexture.
static void drawMainView(SDL_GPUCommandBuffer *render,const FrameView &view,const Uniforms &camera,bool drawMist,bool msaa,
                  const std::vector<SpriteDraw> &weapons,const std::vector<Vertex> &hud,const std::vector<Vertex> &cross) {
    SDL_GPUColorTargetInfo color={};
    color.texture=msaa?multisampleTexture:colorTexture;color.clear_color={0.025f,0.025f,0.03f,1};
    color.load_op=SDL_GPU_LOADOP_CLEAR;
    color.store_op=msaa&&!drawMist?SDL_GPU_STOREOP_RESOLVE:SDL_GPU_STOREOP_STORE;
    color.resolve_texture=msaa&&!drawMist?colorTexture:nullptr;
    SDL_GPUDepthStencilTargetInfo depth={};
    depth.texture=depthTexture;depth.clear_depth=1;depth.load_op=SDL_GPU_LOADOP_CLEAR;
    depth.store_op=drawMist&&!msaa?SDL_GPU_STOREOP_STORE:SDL_GPU_STOREOP_DONT_CARE;
    depth.stencil_load_op=SDL_GPU_LOADOP_DONT_CARE;depth.stencil_store_op=SDL_GPU_STOREOP_DONT_CARE;
    SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(render,&color,1,&depth);
    pushWorldUniforms(render,camera);
    if(worldPending)drawScene(render,pass,view,camera);
    if(drawMist) {
        SDL_EndGPURenderPass(pass);
        color.load_op=SDL_GPU_LOADOP_LOAD;
        color.store_op=msaa?SDL_GPU_STOREOP_RESOLVE:SDL_GPU_STOREOP_STORE;
        color.resolve_texture=msaa?colorTexture:nullptr;
        pass=SDL_BeginGPURenderPass(render,&color,1,nullptr);
        setView(pass,view.worldX,view.worldY,view.worldW,view.worldH);
        drawMistPass(render,pass,camera,msaa);
        pushWorldUniforms(render,camera);
    }
    drawOverlays(render,pass,view,weapons,hud,cross,!drawMist);
    SDL_EndGPURenderPass(pass);
}
// Bloom: emissive texels drawn at half resolution, then blurred.
static void drawBloom(SDL_GPUCommandBuffer *render,const FrameView &view,const Uniforms &camera,const std::vector<SpriteDraw> &weapons) {
    int bw=std::max(1,width/2),bh=std::max(1,height/2);
    float sx=(float)bw/width,sy=(float)bh/height;
    SDL_GPUColorTargetInfo glow={};
    glow.texture=emissionTexture;glow.load_op=SDL_GPU_LOADOP_CLEAR;glow.store_op=SDL_GPU_STOREOP_STORE;glow.clear_color={0,0,0,1};
    SDL_GPUDepthStencilTargetInfo glowDepth={};
    glowDepth.texture=emissionDepth;glowDepth.clear_depth=1;glowDepth.load_op=SDL_GPU_LOADOP_CLEAR;
    glowDepth.store_op=SDL_GPU_STOREOP_DONT_CARE;glowDepth.stencil_load_op=SDL_GPU_LOADOP_DONT_CARE;glowDepth.stencil_store_op=SDL_GPU_STOREOP_DONT_CARE;
    SDL_GPURenderPass *glowPass=SDL_BeginGPURenderPass(render,&glow,1,&glowDepth);
    SDL_Rect area={0,0,bw,bh};
    setView(glowPass,view.worldX*sx,view.worldY*sy,view.worldW*sx,view.worldH*sy,&area);
    SDL_BindGPUGraphicsPipeline(glowPass,emissionPipeline);
    SDL_PushGPUVertexUniformData(render,0,&camera,sizeof(camera));
    SDL_GPUTextureSamplerBinding palette={paletteTexture,nearestClamp};
    SDL_BindGPUFragmentSamplers(glowPass,1,&palette,1);
    bindEmissionEdges(render,glowPass,camera);
    for(const auto &batch:batches) if(!batch.second.empty()) {bindSurface(render,glowPass,batch.first,0,false);drawVertices(glowPass,batch.second);}
    // The sky hides glow behind it: a covered texel with no emission.
    SDL_GPUTextureSamplerBinding solid={solidTexel,nearestClamp};
    SDL_BindGPUFragmentSamplers(glowPass,0,&solid,1);SDL_BindGPUFragmentSamplers(glowPass,2,&solid,1);
    float noBlend[4]={};SDL_PushGPUFragmentUniformData(render,0,noBlend,sizeof(noBlend));
    drawVertices(glowPass,skyVertices);
    // Use the completed world depth to keep rubble emission behind intact
    // walls. Reuse the existing HDR blur, with no extra full-screen pass.
    if(!rubbleEmissionVertices.empty()){
        SDL_BindGPUGraphicsPipeline(glowPass,rubbleEmissionPipeline);
        drawVertices(glowPass,rubbleEmissionVertices);
        SDL_BindGPUGraphicsPipeline(glowPass,emissionPipeline);
    }
    // The weapon hides glow behind it too; it is drawn over the view in
    // the HUD's frame, so the glow from lamps behind it would otherwise
    // show through it.
    if(!weapons.empty()) {
        setView(glowPass,0,view.uiY*sy,width*sx,view.uiHeight*sy,&area);
        for(const auto &draw:weapons) {
            SDL_GPUTextureSamplerBinding image[]={{gpu(draw.image->texture),nearestClamp},{paletteTexture,nearestClamp},{gpu(draw.image->texture),nearestClamp}};
            SDL_BindGPUFragmentSamplers(glowPass,0,image,3);drawVertices(glowPass,draw.vertices);
        }
    }
    SDL_EndGPURenderPass(glowPass);
    // Fine emissive creases need a continuous prefilter before the wide halo.
    // Only pay for it while rubble heat is visible.
    const int blurPasses=rubbleEmissionVertices.empty()?2:6;
    for(int pass=0;pass<blurPasses;++pass) {
        int axis=pass&1;
        SDL_GPUColorTargetInfo blur={};
        blur.texture=axis?bloomTexture:bloomScratch;blur.load_op=SDL_GPU_LOADOP_DONT_CARE;blur.store_op=SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass *blurPass=SDL_BeginGPURenderPass(render,&blur,1,nullptr);
        SDL_BindGPUGraphicsPipeline(blurPass,blurPipeline);
        SDL_GPUTextureSamplerBinding source={axis?bloomScratch:(pass?bloomTexture:emissionTexture),linearClamp};
        SDL_BindGPUFragmentSamplers(blurPass,0,&source,1);
        // Radius scales with resolution, retaining a similar halo at 50–100%.
        float dx=0.0035f,dy=0.0056f;
        if(blurPasses>2 && pass<4){
            float texels=pass<2?1.0f:2.0f;
            dx=texels/bw;dy=texels/bh;
        }
        float direction[4]={axis?0:dx,axis?dy:0,0,0};
        SDL_PushGPUFragmentUniformData(render,0,direction,sizeof(direction));
        SDL_DrawGPUPrimitives(blurPass,3,1,0,0);
        SDL_EndGPURenderPass(blurPass);
    }
}
// Profiling: submits the scene's own command buffer and times it.
static void submitProfiled(SDL_GPUCommandBuffer *render,uint64_t frameStart) {
    flushUploads();
    uint64_t submit=SDL_GetTicksNS();
    SDL_GPUFence *fence=SDL_SubmitGPUCommandBufferAndAcquireFence(render);
    if(!fence) I_Error((char*)"GPU rendering failed: %s",SDL_GetError());
    SDL_WaitForGPUFences(device,true,&fence,1);SDL_ReleaseGPUFence(device,fence);
    uint64_t done=SDL_GetTicksNS();
    profile.frame.cpuMs=(submit-frameStart)/1e6;profile.frame.gpuMs=(done-submit)/1e6;
}
void releaseReflection() {
    for(MirrorTarget *t:{&liquidMirror,&glassMirror}) {release(t->color);release(t->depth);t->width=t->height=0;}
    release(probeAtlas);release(probeDepth);probeAtlasSerial=~0u;
}
} // namespace gpu3d

using namespace gpu3d;
void I_Render3DPresent(const unsigned *pixels,const unsigned *palette) {
    const bool allFrames=M_CheckParm((char*)"-profileallframes")!=0;
    const bool trace=allFrames||M_CheckParm((char*)"-profilespikes")!=0;
    static uint64_t previousEnd=0;
    uint64_t begin=trace?SDL_GetTicksNS():0,mark=begin;
    double stages[5]={};
    auto stage=[&](int n){if(trace){auto now=SDL_GetTicksNS();stages[n]=(now-mark)/1e6;mark=now;}};
    applySampleCount();
    // A new level's build and bakes run first, with the loading screen up.
    if(loadPending()) {
        int w=width,h=height;
        if(!colorTexture) {
            int pw=1,ph=1;SDL_GetWindowSizeInPixels(gameWindow,&pw,&ph);
            w=std::max(1,pw*settings.scale/100);h=std::max(1,ph*settings.scale/100);
        }
        runLoading(w,h,showLoading);
    }
    SDL_GPUCommandBuffer *command=SDL_AcquireGPUCommandBuffer(device);
    if(!command) {fprintf(stderr,"GPU command buffer: %s\n",SDL_GetError());return;}
    SDL_GPUTexture *swapchain=nullptr;Uint32 drawableWidth=0,drawableHeight=0;
    // A hidden window (render checks) has no swapchain: the frame is drawn for capture() alone.
    if(SDL_GetWindowFlags(gameWindow)&SDL_WINDOW_HIDDEN) {
        int pw=1,ph=1;SDL_GetWindowSizeInPixels(gameWindow,&pw,&ph);drawableWidth=(Uint32)pw;drawableHeight=(Uint32)ph;
    }
    else if(!SDL_WaitAndAcquireGPUSwapchainTexture(command,gameWindow,&swapchain,&drawableWidth,&drawableHeight)||!swapchain) {
        flushUploads();SDL_SubmitGPUCommandBuffer(command);return;
    }
    uint64_t frameStart=SDL_GetTicksNS();
    stage(0);
    int w=std::max(1,(int)drawableWidth*settings.scale/100),h=std::max(1,(int)drawableHeight*settings.scale/100);
    if(w!=width||h!=height||!colorTexture) {
        width=w;height=h;allocateTargets(w,h);
        fprintf(stderr,"GPU resolution: %dx%d (display %ux%u, %d%%).\n",w,h,drawableWidth,drawableHeight,settings.scale);
    }
    if(!paletteLUT||((settings.palette||(settings.reflections&&settings.retroReflections))&&worldPending))updatePaletteLUT(palette);
    std::vector<unsigned> overlay=hudPixels(pixels);
    FrameView view=prepareFrame(w,h);Uniforms &camera=view.camera;
    stage(1);
    bool drawMist=!mistVertices.empty()||!shaftVertices.empty();
    bool msaa=sampleCount!=SDL_GPU_SAMPLECOUNT_1;
    // Transient quads join the frame upload; textures created while drawing
    // go through the upload command buffer, which is submitted first.
    std::vector<SpriteDraw> weapons;if(worldPending&&!director_view_active)weapons=weaponDraws(camera);
    // Keep the original weapon scale, but allow its artwork outside the 4:3
    // UI rectangle. A full scissor alone cannot undo clip-space clipping.
    for(auto &draw:weapons)for(auto &v:draw.vertices)
        v.x=(v.x*view.uiWidth+2*view.uiX+view.uiWidth-width)/width;
    std::vector<Vertex> hud;screenQuad(hud,-1,1,2,2,0,0,1,.5f);
    // Separate HUD atlas layer: dock side panels to widescreen edges without
    // stretching lettering or shifting menus, notices, or the weapon wheel.
    const float edges[]={0,136,202,320};
    for(int group=0;group<3;++group) {
        float shift=settings.hudLayout==1?0.f:(group==0?-view.uiX:group==2?width-view.uiX-view.uiWidth:0.f);
        screenQuad(hud,-1+2*edges[group]/320+2*shift/view.uiWidth,1,
            2*(edges[group+1]-edges[group])/320,2,edges[group]/320,.5f,edges[group+1]/320,1);
    }
    for(auto &v:hud)v.x=(v.x*view.uiWidth+2*view.uiX+view.uiWidth-width)/width;
    std::vector<Vertex> cross;if(!director_view_active)cross=crosshairVertices(view.worldW,view.worldH);
    std::vector<const std::vector<Vertex>*> transient={&hud,&cross};
    arcadeRankQuad.clear();
    if((Arcade_MenuOpen()||Menu_BrandOpen())&&arcadeRankTexture){
        // Composite after palette/post-processing so purple stays purple.
        screenQuad(arcadeRankQuad,2*view.uiX/width-1,1-2*view.uiY/height,
            2*view.uiWidth/width,2*view.uiHeight/height,0,0,1,1);
        transient.push_back(&arcadeRankQuad);
    }
    arcadePodiumQuad.clear();float artRect[4];
    if(arcadePodiumTexture&&Arcade_PodiumRect(artRect)){
        // Logical menu coordinates locate the box; fit the original aspect
        // in physical pixels, then composite after the low-resolution HUD.
        float x=(view.uiX+artRect[0]*view.uiWidth/320)/width;
        float y=(view.uiY+artRect[1]*view.uiHeight/200)/height;
        float bw=artRect[2]*view.uiWidth/320/width*drawableWidth;
        float bh=artRect[3]*view.uiHeight/200/height*drawableHeight;
        float aw=std::min(bw,bh*1.5f),ah=aw/1.5f;
        x+=(bw-aw)*.5f/drawableWidth;y+=(bh-ah)*.5f/drawableHeight;
        screenQuad(arcadePodiumQuad,x*2-1,1-y*2,aw*2/drawableWidth,ah*2/drawableHeight,0,0,1,1);
        transient.push_back(&arcadePodiumQuad);
    }
    romeroQuad.clear();
    if(romeroAge>=0 && romeroTexture){
        float t=romeroAge,rise=std::clamp(t/.15f,0.f,1.f),fly=std::clamp((t-.65f)/.6f,0.f,1.f);
        float h=1.0f*(1-.65f*fly),w=h*romeroAspect*drawableHeight/drawableWidth;
        float shake=(t>.15f&&t<.65f)?1.f:0.f;
        float cx=romeroCorner*1.5f*fly*fly+.055f*std::sin(t*173)*shake;
        float top=-1+(1-std::pow(1-rise,3.f))*1.3f+1.8f*fly*fly+.07f*std::sin(t*219)*shake;
        screenQuad(romeroQuad,cx-w*.5f,top,w,h,0,0,1,1);transient.push_back(&romeroQuad);
    }
    toastyQuad.clear();
    if(toastyVisible>0 && toastyTexture) {
        float h=.88f,w=h*toastyAspect*drawableHeight/drawableWidth;
        screenQuad(toastyQuad,1-w,-1+h*toastyVisible,w,h,0,0,1,1);
        transient.push_back(&toastyQuad);
    }
    for(const auto &draw:weapons)transient.push_back(&draw.vertices);
    std::vector<unsigned> settingsImage;settingsQuad.clear();
    if(I_Render3DSettingsOpen()) {layoutSettingsPanel(drawableWidth,drawableHeight,settingsImage);transient.push_back(&settingsQuad);}
    // Profiling: the scene goes in a command buffer of its own, timed by a
    // fence. The swapchain's may wait for the compositor, which in a window
    // keeps the display's rate even without vsync.
    SDL_GPUCommandBuffer *render=profile.enabled?SDL_AcquireGPUCommandBuffer(device):command;
    if(!render) I_Error((char*)"GPU command buffer: %s",SDL_GetError());
    uploadFrame(render,transient,sectorInfo(),palette,overlay,settingsImage);
    stage(2);
    drawReflection(render,view,camera);
    captureGlassProbes(render,camera);
    glassReflected=drawGlassReflection(render,view,camera);
    if(drawMist&&msaa)drawMistDepth(render,view,camera);
    drawMainView(render,view,camera,drawMist,msaa,weapons,hud,cross);
    if(camera.materials[1]>0)drawBloom(render,view,camera,weapons);
    stage(3);
    if(render!=command)submitProfiled(render,frameStart);
    if(swapchain)present(command,swapchain,view);
    flushUploads();
    if(screenshotPending||renderCheckDue()) {screenshotPending=false;capture(command,view,(int)drawableWidth,(int)drawableHeight);}
    else if(!SDL_SubmitGPUCommandBuffer(command)) I_Error((char*)"GPU rendering failed: %s",SDL_GetError());
    char renderer[64];snprintf(renderer,sizeof(renderer),"%s 3D",SDL_GetGPUDeviceDriver(device));
    if(renderer[0])renderer[0]=(char)toupper((unsigned char)renderer[0]);
    updateTitle(w,h,renderer);
    if(trace) {
        stage(4);auto end=SDL_GetTicksNS();
        double outside=previousEnd?(begin-previousEnd)/1e6:0,elapsed=(end-begin)/1e6;
        if(previousEnd&&(allFrames||elapsed+outside>16.67))
            fprintf(stderr,"Frame spike tic %d: total %.3f outside %.3f acquire %.3f prepare %.3f upload %.3f draw %.3f submit %.3f ms\n",gametic,elapsed+outside,outside,stages[0],stages[1],stages[2],stages[3],stages[4]);
        previousEnd=SDL_GetTicksNS();
    }
}

void I_Render3DSettingsMouse(float x,float y,int button) {
    float density=gameWindow?SDL_GetWindowPixelDensity(gameWindow):1;
    if(density<=0)density=1;
    float px=(x*density-settingsOrigin[0])/settingsScale,py=(y*density-settingsOrigin[1])/settingsScale;
    I_Render3DSettingsClick((int)std::floor(px),(int)std::floor(py),button);
}
