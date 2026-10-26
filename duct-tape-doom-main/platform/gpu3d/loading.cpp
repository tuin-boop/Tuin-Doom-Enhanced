/* The loading screen (scene3d/loading.cpp), presented on its own between
   the steps of a level's build and bakes: the 640x400 loading image placed
   like the settings panel, over black. */
#include "internal.h"

namespace gpu3d {
static SDL_GPUBuffer *quadBuffer; // The panel quad's six vertices.
static SDL_GPUTransferBuffer *transfer;
static std::vector<unsigned> image;

void showLoading(const char *title,const char *stage,float fraction) {
    // Window events wait in the queue for the game; pumping them keeps the
    // window moving and the system from calling the game unresponsive.
    SDL_PumpEvents();
    SDL_GPUCommandBuffer *command=SDL_AcquireGPUCommandBuffer(device);
    if(!command)return;
    SDL_GPUTexture *swapchain=nullptr;Uint32 drawableWidth=0,drawableHeight=0;
    if((SDL_GetWindowFlags(gameWindow)&SDL_WINDOW_HIDDEN)||!SDL_WaitAndAcquireGPUSwapchainTexture(command,gameWindow,&swapchain,&drawableWidth,&drawableHeight)||!swapchain) {
        SDL_SubmitGPUCommandBuffer(command);return;
    }
    image.assign((size_t)DOOM_SETTINGS_WIDTH*DOOM_SETTINGS_HEIGHT,0);
    I_Render3DLoadingDraw(image.data(),title,stage,fraction);
    float origin[2],scale;
    std::vector<Vertex> quad=panelQuad(drawableWidth,drawableHeight,origin,scale);
    Uint32 imageBytes=(Uint32)(image.size()*4),quadBytes=(Uint32)(quad.size()*sizeof(Vertex));
    if(!quadBuffer) {
        SDL_GPUBufferCreateInfo info={SDL_GPU_BUFFERUSAGE_VERTEX,quadBytes,0};
        quadBuffer=SDL_CreateGPUBuffer(device,&info);
    }
    if(!transfer) {
        SDL_GPUTransferBufferCreateInfo info={SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,imageBytes+quadBytes,0};
        transfer=SDL_CreateGPUTransferBuffer(device,&info);
    }
    if(!quadBuffer||!transfer) {SDL_SubmitGPUCommandBuffer(command);return;}
    byte *mapped=(byte*)SDL_MapGPUTransferBuffer(device,transfer,true);
    memcpy(mapped,image.data(),imageBytes);
    memcpy(mapped+imageBytes,quad.data(),quadBytes);
    SDL_UnmapGPUTransferBuffer(device,transfer);
    SDL_GPUCopyPass *copy=SDL_BeginGPUCopyPass(command);
    SDL_GPUTextureTransferInfo pixels={transfer,0,DOOM_SETTINGS_WIDTH,DOOM_SETTINGS_HEIGHT};
    SDL_GPUTextureRegion target={};
    target.texture=settingsTexture;target.w=DOOM_SETTINGS_WIDTH;target.h=DOOM_SETTINGS_HEIGHT;target.d=1;
    SDL_UploadToGPUTexture(copy,&pixels,&target,true);
    SDL_GPUTransferBufferLocation vertices={transfer,imageBytes};
    SDL_GPUBufferRegion buffer={quadBuffer,0,quadBytes};
    SDL_UploadToGPUBuffer(copy,&vertices,&buffer,true);
    SDL_EndGPUCopyPass(copy);
    SDL_GPUColorTargetInfo info={};
    info.texture=swapchain;info.load_op=SDL_GPU_LOADOP_CLEAR;info.store_op=SDL_GPU_STOREOP_STORE;info.clear_color={0,0,0,1};
    SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(command,&info,1,nullptr);
    SDL_BindGPUGraphicsPipeline(pass,settingsPipeline);
    Uniforms none={};
    SDL_PushGPUVertexUniformData(command,0,&none,sizeof(none));
    SDL_GPUTextureSamplerBinding panel={settingsTexture,nearestClamp};
    SDL_BindGPUFragmentSamplers(pass,0,&panel,1);
    SDL_GPUBufferBinding binding={quadBuffer,0};
    SDL_BindGPUVertexBuffers(pass,0,&binding,1);
    SDL_DrawGPUPrimitives(pass,(Uint32)quad.size(),1,0,0);
    SDL_EndGPURenderPass(pass);
    // Textures the bakes made so far go up now rather than piling up.
    flushUploads();
    if(!SDL_SubmitGPUCommandBuffer(command))fprintf(stderr,"GPU loading screen: %s\n",SDL_GetError());
}
void releaseLoading() {
    release(quadBuffer,SDL_ReleaseGPUBuffer);
    release(transfer,SDL_ReleaseGPUTransferBuffer);
    image.clear();image.shrink_to_fit();
}
} // namespace gpu3d
