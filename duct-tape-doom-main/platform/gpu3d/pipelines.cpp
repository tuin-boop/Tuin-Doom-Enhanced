/* Shaders and graphics pipelines from the embedded shader code, the render
   targets, and the MSAA sample count that both depend on. */
#include "internal.h"
#include "../gpu_shaders.h"

namespace gpu3d {
SDL_GPUSampleCount sampleCount=SDL_GPU_SAMPLECOUNT_1;
static SDL_GPUShader *worldVertexShader,*reflectVertexShader,*screenVertexShader,*skyVertexShader;
SDL_GPUGraphicsPipeline *opaquePipeline,*skyPipeline,*skySurfacePipeline,*reflectSkySurfacePipeline,*worldPipeline,*shadowPipeline,*decalPipeline,*effectAddPipeline,*particlePipeline,*crestPipeline,*heatPipeline,
    *weaponPipeline,*weaponAddPipeline,*hudPipeline,*solidPipeline,*mistPipeline,*shaftPipeline,*overlayWeaponPipeline,*overlayWeaponAddPipeline,*overlayHudPipeline,*overlaySolidPipeline,
    *reflectOpaquePipeline,*reflectSkyPipeline,*reflectWorldPipeline,*emissionPipeline,*rubbleEmissionPipeline,*blurPipeline,*presentPipeline,*depthPipeline,*prepassPipeline,*coveragePipeline,*cutoutPipeline,
    *settingsPipeline,*glassPipeline,*reflectPrepassPipeline;
SDL_GPUTexture *multisampleTexture,*colorTexture,*depthTexture,*sceneDepth,*emissionTexture,*emissionDepth,*bloomScratch,*bloomTexture;
int width,height;
struct ShaderCode { const unsigned char *spirv; size_t spirvSize; const unsigned char *msl; size_t mslSize; };
static SDL_GPUShader *shader(const ShaderCode &code,SDL_GPUShaderStage stage,Uint32 samplers,Uint32 storage,Uint32 uniforms) {
    SDL_GPUShaderCreateInfo info={};
    bool msl=shaderFormat==SDL_GPU_SHADERFORMAT_MSL;
    info.code=msl?code.msl:code.spirv;info.code_size=msl?code.mslSize:code.spirvSize;
    // SPIRV-Cross renames the entry point because main is reserved in MSL.
    info.entrypoint=msl?"main0":"main";info.format=shaderFormat;
    info.stage=stage;info.num_samplers=samplers;info.num_storage_buffers=storage;info.num_uniform_buffers=uniforms;
    SDL_GPUShader *result=SDL_CreateGPUShader(device,&info);
    if(!result)fprintf(stderr,"GPU shader: %s\n",SDL_GetError());
    return result;
}
#define SHADER_CODE(name) ShaderCode{doom_spv_##name,sizeof(doom_spv_##name),doom_msl_##name,sizeof(doom_msl_##name)}
#define VERTEX_SHADER(name,uniforms) shader(SHADER_CODE(name),SDL_GPU_SHADERSTAGE_VERTEX,0,0,uniforms)
#define FRAGMENT_SHADER(name,samplers,storage,uniforms) shader(SHADER_CODE(name),SDL_GPU_SHADERSTAGE_FRAGMENT,samplers,storage,uniforms)
// heatBlend writes only alpha, keeping the lowest (hottest) value.
enum Blend { opaqueBlend, alphaBlend, heatBlend, addBlend, mirrorBlend, depthBlend, brightenBlend };
enum Depth { noDepth, depthWrite, depthTest, depthAlways, depthOnly };
// Geometry pipelines read the 64-byte Vertex; screen and sky passes have no vertex input.
static SDL_GPUGraphicsPipeline *pipeline(SDL_GPUShader *vertex,SDL_GPUShader *fragment,Blend blend,Depth depth,
                                  SDL_GPUSampleCount samples,SDL_GPUTextureFormat color) {
    if(!vertex||!fragment)return nullptr;
    static const SDL_GPUVertexBufferDescription buffer={0,sizeof(Vertex),SDL_GPU_VERTEXINPUTRATE_VERTEX,0};
    static const SDL_GPUVertexAttribute attributes[]={
        {0,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,offsetof(Vertex,x)},{1,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,offsetof(Vertex,u)},
        {2,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT,offsetof(Vertex,light)},{3,0,SDL_GPU_VERTEXELEMENTFORMAT_UINT,offsetof(Vertex,mode)},
        {4,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,offsetof(Vertex,red)},{5,0,SDL_GPU_VERTEXELEMENTFORMAT_UINT2,offsetof(Vertex,lightMask)},
        {6,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,offsetof(Vertex,sunU)},{7,0,SDL_GPU_VERTEXELEMENTFORMAT_UINT,offsetof(Vertex,statics)},
        {8,0,SDL_GPU_VERTEXELEMENTFORMAT_UINT,offsetof(Vertex,lampTint)}};
    SDL_GPUGraphicsPipelineCreateInfo info={};
    info.vertex_shader=vertex;info.fragment_shader=fragment;
    if(vertex==worldVertexShader||vertex==reflectVertexShader) {
        info.vertex_input_state.vertex_buffer_descriptions=&buffer;info.vertex_input_state.num_vertex_buffers=1;
        info.vertex_input_state.vertex_attributes=attributes;info.vertex_input_state.num_vertex_attributes=9;
    }
    info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;info.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
    info.rasterizer_state.enable_depth_clip=true;
    info.multisample_state.sample_count=samples;
    SDL_GPUColorTargetDescription target={};
    target.format=color;
    if(blend==alphaBlend) {
        auto &b=target.blend_state;b.enable_blend=true;
        b.src_color_blendfactor=SDL_GPU_BLENDFACTOR_SRC_ALPHA;b.dst_color_blendfactor=SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        b.src_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE;b.dst_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        b.color_blend_op=b.alpha_blend_op=SDL_GPU_BLENDOP_ADD;
    } else if(blend==mirrorBlend) {
        // Blended like alphaBlend, but the target's alpha (the reflection's
        // distance from the surface, written by opaque geometry) is kept.
        auto &b=target.blend_state;b.enable_blend=true;
        b.src_color_blendfactor=SDL_GPU_BLENDFACTOR_SRC_ALPHA;b.dst_color_blendfactor=SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        b.src_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ZERO;b.dst_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE;
        b.color_blend_op=b.alpha_blend_op=SDL_GPU_BLENDOP_ADD;
    } else if(blend==addBlend) {
        // Light added by alpha; the target's alpha (heat shimmer marks) is kept.
        auto &b=target.blend_state;b.enable_blend=true;
        b.src_color_blendfactor=SDL_GPU_BLENDFACTOR_SRC_ALPHA;b.dst_color_blendfactor=SDL_GPU_BLENDFACTOR_ONE;
        b.src_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ZERO;b.dst_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE;
        b.color_blend_op=b.alpha_blend_op=SDL_GPU_BLENDOP_ADD;
    } else if(blend==brightenBlend) {
        // What is drawn already, times one plus the source color: liquid
        // crests brighten the shaded surface under them by a step; the
        // target's alpha is kept.
        auto &b=target.blend_state;b.enable_blend=true;
        b.src_color_blendfactor=SDL_GPU_BLENDFACTOR_DST_COLOR;b.dst_color_blendfactor=SDL_GPU_BLENDFACTOR_ONE;
        b.src_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ZERO;b.dst_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE;
        b.color_blend_op=b.alpha_blend_op=SDL_GPU_BLENDOP_ADD;
    } else if(blend==depthBlend) {
        // Depth prepass: the color target stays untouched.
        target.blend_state.enable_color_write_mask=true;target.blend_state.color_write_mask=0;
    } else if(blend==heatBlend) {
        auto &b=target.blend_state;b.enable_blend=true;b.enable_color_write_mask=true;b.color_write_mask=SDL_GPU_COLORCOMPONENT_A;
        b.src_color_blendfactor=b.src_alpha_blendfactor=b.dst_color_blendfactor=b.dst_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE;
        b.color_blend_op=SDL_GPU_BLENDOP_ADD;b.alpha_blend_op=SDL_GPU_BLENDOP_MIN;
    }
    if(depth!=depthOnly) {info.target_info.color_target_descriptions=&target;info.target_info.num_color_targets=1;}
    if(depth!=noDepth) {
        info.target_info.has_depth_stencil_target=true;info.target_info.depth_stencil_format=depthFormat;
        info.depth_stencil_state.enable_depth_test=depth!=depthAlways;
        info.depth_stencil_state.enable_depth_write=depth==depthWrite||depth==depthOnly;
        info.depth_stencil_state.compare_op=SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    }
    SDL_GPUGraphicsPipeline *result=SDL_CreateGPUGraphicsPipeline(device,&info);
    if(!result)fprintf(stderr,"GPU pipeline: %s\n",SDL_GetError());
    return result;
}
bool createPipelines() {
    worldVertexShader=VERTEX_SHADER(world_vert,1);reflectVertexShader=VERTEX_SHADER(reflect_vert,2);
    screenVertexShader=VERTEX_SHADER(screen_vert,0);skyVertexShader=VERTEX_SHADER(sky_vert,0);
    SDL_GPUShader *world=FRAGMENT_SHADER(world_frag,16,2,4),*opaque=FRAGMENT_SHADER(opaque_frag,16,2,4);
    SDL_GPUShader *cutout=FRAGMENT_SHADER(cutout_frag,16,2,4),*coverage=FRAGMENT_SHADER(coverage_frag,16,2,4);
    SDL_GPUShader *sky=FRAGMENT_SHADER(sky_frag,2,0,3),*skySurface=FRAGMENT_SHADER(sky_surface_frag,2,0,3),*shadow=FRAGMENT_SHADER(shadow_frag,1,0,0),*decal=FRAGMENT_SHADER(decal_frag,1,0,0);
    SDL_GPUShader *particle=FRAGMENT_SHADER(particle_frag,0,0,0),*solid=FRAGMENT_SHADER(solid_frag,0,0,0),*hud=FRAGMENT_SHADER(hud_frag,1,0,0);
    SDL_GPUShader *heat=FRAGMENT_SHADER(heat_frag,0,0,0),*glass=FRAGMENT_SHADER(glass_frag,4,1,4);
    SDL_GPUShader *emission=FRAGMENT_SHADER(emission_frag,5,1,2),*blur=FRAGMENT_SHADER(blur_frag,1,0,1);
    SDL_GPUShader *mist=FRAGMENT_SHADER(mist_frag,2,0,1),*present=FRAGMENT_SHADER(present_frag,4,0,2),*depth=FRAGMENT_SHADER(depth_frag,1,0,0);
    const auto color=SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,glow=SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    const auto one=SDL_GPU_SAMPLECOUNT_1,msaa=sampleCount;
    SDL_GPUShader *wv=worldVertexShader,*rv=reflectVertexShader,*sv=screenVertexShader,*kv=skyVertexShader;
    opaquePipeline=pipeline(wv,opaque,opaqueBlend,depthWrite,msaa,color);
    prepassPipeline=pipeline(wv,solid,depthBlend,depthWrite,msaa,color);
    coveragePipeline=pipeline(wv,coverage,depthBlend,depthWrite,msaa,color);
    cutoutPipeline=pipeline(wv,cutout,alphaBlend,depthTest,msaa,color);
    skyPipeline=pipeline(kv,sky,opaqueBlend,depthTest,msaa,color);
    skySurfacePipeline=pipeline(wv,skySurface,opaqueBlend,depthWrite,msaa,color);
    worldPipeline=pipeline(wv,world,alphaBlend,depthWrite,msaa,color);
    shadowPipeline=pipeline(wv,shadow,alphaBlend,depthTest,msaa,color);
    decalPipeline=pipeline(wv,decal,alphaBlend,depthTest,msaa,color);
    effectAddPipeline=pipeline(wv,decal,addBlend,depthTest,msaa,color);
    particlePipeline=pipeline(wv,particle,alphaBlend,depthTest,msaa,color);
    crestPipeline=pipeline(wv,particle,brightenBlend,depthTest,msaa,color);
    heatPipeline=pipeline(wv,heat,heatBlend,depthTest,msaa,color);
    glassPipeline=pipeline(wv,glass,alphaBlend,depthTest,msaa,color);
    weaponPipeline=pipeline(wv,world,alphaBlend,depthAlways,msaa,color);
    weaponAddPipeline=pipeline(wv,world,addBlend,depthAlways,msaa,color);
    hudPipeline=pipeline(wv,hud,alphaBlend,depthAlways,msaa,color);
    solidPipeline=pipeline(wv,solid,alphaBlend,depthAlways,msaa,color);
    // After the mist, overlays continue in a pass without a depth target.
    mistPipeline=pipeline(wv,mist,alphaBlend,noDepth,msaa,color);
    shaftPipeline=pipeline(wv,mist,addBlend,noDepth,msaa,color);
    overlayWeaponPipeline=pipeline(wv,world,alphaBlend,noDepth,msaa,color);
    overlayWeaponAddPipeline=pipeline(wv,world,addBlend,noDepth,msaa,color);
    overlayHudPipeline=pipeline(wv,hud,alphaBlend,noDepth,msaa,color);
    overlaySolidPipeline=pipeline(wv,solid,alphaBlend,noDepth,msaa,color);
    reflectOpaquePipeline=pipeline(rv,opaque,opaqueBlend,depthWrite,one,color);
    reflectPrepassPipeline=pipeline(rv,solid,depthBlend,depthWrite,one,color);
    reflectSkyPipeline=pipeline(kv,sky,opaqueBlend,depthTest,one,color);
    reflectSkySurfacePipeline=pipeline(rv,skySurface,opaqueBlend,depthWrite,one,color);
    reflectWorldPipeline=pipeline(rv,world,mirrorBlend,depthWrite,one,color);
    emissionPipeline=pipeline(wv,emission,opaqueBlend,depthWrite,one,glow);
    rubbleEmissionPipeline=pipeline(wv,particle,addBlend,depthTest,one,glow);
    blurPipeline=pipeline(sv,blur,opaqueBlend,noDepth,one,glow);
    presentPipeline=pipeline(sv,present,opaqueBlend,noDepth,one,swapchainFormat);
    settingsPipeline=pipeline(wv,hud,alphaBlend,noDepth,one,swapchainFormat);
    depthPipeline=pipeline(wv,depth,opaqueBlend,depthOnly,one,color);
    for(SDL_GPUShader *s:{world,opaque,cutout,coverage,sky,skySurface,shadow,decal,particle,heat,glass,solid,hud,emission,blur,mist,present,depth})
        if(s)SDL_ReleaseGPUShader(device,s);
    for(SDL_GPUShader **s:{&worldVertexShader,&reflectVertexShader,&screenVertexShader,&skyVertexShader})
        if(*s) {SDL_ReleaseGPUShader(device,*s);*s=nullptr;}
    for(auto *p:{opaquePipeline,skyPipeline,skySurfacePipeline,reflectSkySurfacePipeline,worldPipeline,shadowPipeline,decalPipeline,effectAddPipeline,particlePipeline,crestPipeline,heatPipeline,weaponPipeline,weaponAddPipeline,overlayWeaponAddPipeline,hudPipeline,
                 solidPipeline,mistPipeline,shaftPipeline,overlayWeaponPipeline,overlayHudPipeline,overlaySolidPipeline,reflectOpaquePipeline,
                 reflectSkyPipeline,reflectWorldPipeline,emissionPipeline,rubbleEmissionPipeline,blurPipeline,presentPipeline,depthPipeline,prepassPipeline,coveragePipeline,cutoutPipeline,settingsPipeline,glassPipeline,reflectPrepassPipeline})
        if(!p)return false;
    return true;
}
void releasePipelines() {
    for(SDL_GPUGraphicsPipeline **p:{&opaquePipeline,&skyPipeline,&skySurfacePipeline,&reflectSkySurfacePipeline,&worldPipeline,&shadowPipeline,&decalPipeline,&effectAddPipeline,&particlePipeline,&crestPipeline,&heatPipeline,
            &weaponPipeline,&hudPipeline,&solidPipeline,&mistPipeline,&shaftPipeline,&overlayWeaponPipeline,&overlayHudPipeline,&overlaySolidPipeline,
            &reflectOpaquePipeline,&reflectSkyPipeline,&reflectWorldPipeline,&emissionPipeline,&rubbleEmissionPipeline,&blurPipeline,&presentPipeline,&depthPipeline,&prepassPipeline,&coveragePipeline,&cutoutPipeline,
            &settingsPipeline,&glassPipeline,&reflectPrepassPipeline})
        release(*p);
}
void releaseTargets() {
    for(SDL_GPUTexture **t:{&multisampleTexture,&colorTexture,&depthTexture,&sceneDepth,&emissionTexture,&emissionDepth,&bloomScratch,&bloomTexture})
        release(*t);
}
void allocateTargets(int w,int h) {
    releaseTargets();
    const auto color=SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,glow=SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    const auto target=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER;
    bool msaa=sampleCount!=SDL_GPU_SAMPLECOUNT_1;
    if(msaa)multisampleTexture=createTexture(color,w,h,SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,sampleCount);
    colorTexture=createTexture(color,w,h,target);
    // Single-sample depth is read by the mist directly; MSAA depth uses a prepass copy.
    depthTexture=createTexture(depthFormat,w,h,SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET|(msaa?0:SDL_GPU_TEXTUREUSAGE_SAMPLER),sampleCount);
    if(msaa)sceneDepth=createTexture(depthFormat,w,h,SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER);
    if(!colorTexture||!depthTexture||(msaa&&(!multisampleTexture||!sceneDepth)))
        I_Error((char*)"Could not allocate GPU render targets at %dx%d: %s",w,h,SDL_GetError());
    int bw=std::max(1,w/2),bh=std::max(1,h/2);
    emissionTexture=createTexture(glow,bw,bh,target);
    bloomScratch=createTexture(glow,bw,bh,target);
    bloomTexture=createTexture(glow,bw,bh,target);
    emissionDepth=createTexture(depthFormat,bw,bh,SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET);
    if(!emissionTexture||!bloomScratch||!bloomTexture||!emissionDepth) I_Error((char*)"Could not allocate bloom targets");
}
// The world targets' sample count from the MSAA setting, capped by the GPU;
// changing it rebuilds the pipelines and the targets.
void applySampleCount() {
    int wanted=forceSingleSample?1:std::min(maxSamples,settings.msaa>0?1<<(int)std::lround(settings.msaa):1);
    SDL_GPUSampleCount count=wanted>=8?SDL_GPU_SAMPLECOUNT_8:wanted>=4?SDL_GPU_SAMPLECOUNT_4:wanted>=2?SDL_GPU_SAMPLECOUNT_2:SDL_GPU_SAMPLECOUNT_1;
    if(count==sampleCount&&opaquePipeline)return;
    SDL_WaitForGPUIdle(device);
    releasePipelines();sampleCount=count;
    if(!createPipelines())I_Error((char*)"Could not create the GPU pipelines for %dx MSAA",wanted);
    releaseTargets();width=height=0;
    fprintf(stderr,"GPU anti-aliasing: %dx MSAA.\n",wanted);
}
} // namespace gpu3d
