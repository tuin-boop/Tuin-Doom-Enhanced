/* Drawing helpers: the per-frame upload of vertices and light and sector
   data, resource binding, and the world and overlay draws the passes share. */
#include "internal.h"

namespace gpu3d {
struct Arena { SDL_GPUBuffer *buffer=nullptr; Uint32 capacity=0; };
static Arena vertexArena,blockerArena,sectorArena;
struct ResidentVertices {Arena arena;std::vector<Vertex> snapshot;size_t count=0;unsigned revision=~0u,seen=0;uint64_t maskStamp=0;};
static std::unordered_map<uint64_t,ResidentVertices> residentVertices;
struct DrawSection {SDL_GPUBuffer* buffer;Uint32 offset,count;};
static std::unordered_map<const std::vector<Vertex>*,std::vector<DrawSection>> residentBindings;
static unsigned uploadSerial=0;
static SDL_GPUTransferBuffer *frameTransfer;
static Uint32 frameTransferCapacity;

// One copy pass uploads dynamic vertices, changed resident sections and light/sector data.
static void reserve(Arena &arena,Uint32 bytes,SDL_GPUBufferUsageFlags usage) {
    if(arena.buffer&&arena.capacity>=bytes)return;
    if(arena.buffer)SDL_ReleaseGPUBuffer(device,arena.buffer);
    arena.capacity=std::max<Uint32>(65536,bytes+bytes/2);
    SDL_GPUBufferCreateInfo info={usage,arena.capacity,0};
    arena.buffer=SDL_CreateGPUBuffer(device,&info);
    if(!arena.buffer)I_Error((char*)"Could not allocate GPU buffer: %s",SDL_GetError());
}
void uploadFrame(SDL_GPUCommandBuffer *command,const std::vector<const std::vector<Vertex>*> &transient,
                 const std::vector<std::array<float,4>> &sectors,const unsigned *palette,const std::vector<unsigned> &hud,
                 const std::vector<unsigned> &panel) {
    residentBindings.clear();++uploadSerial;Uint32 bytes=0;
    struct Pending {const Vertex* data;Uint32 size;SDL_GPUBuffer* buffer;Uint32 offset;};
    std::vector<Pending> pending,dynamic;
    Uint32 residentBytes=0;
    const bool reuse=!M_CheckParm((char*)"-nostaticuploads");
    auto place=[&](const std::vector<Vertex>& vertices){
        if(vertices.empty()||residentBindings.count(&vertices))return;
        auto& draws=residentBindings[&vertices];
        auto stream=[&](size_t first,size_t count){
            if(!count)return;Uint32 size=(Uint32)(count*sizeof(Vertex));
            dynamic.push_back({vertices.data()+first,size,nullptr,bytes});draws.push_back({nullptr,bytes,(Uint32)count});
            bytes=(bytes+size+255)&~Uint32(255);
        };
        auto sections=vertexSections.find(&vertices);size_t cursor=0;
        if(reuse&&sections!=vertexSections.end())for(const auto& section:sections->second){
            if(section.first<cursor||section.first+section.count>vertices.size())continue;
            stream(cursor,section.first-cursor);
            const Vertex* data=vertices.data()+section.first;Uint32 size=(Uint32)(section.count*sizeof(Vertex));
            auto& cached=residentVertices[section.id];cached.seen=uploadSerial;
            if(!cached.arena.buffer||cached.count!=section.count||cached.revision!=section.revision||cached.maskStamp!=section.maskStamp){
                reserve(cached.arena,size,SDL_GPU_BUFFERUSAGE_VERTEX);
                pending.push_back({data,size,cached.arena.buffer,residentBytes});
                residentBytes=(residentBytes+size+255)&~Uint32(255);
                cached.count=section.count;cached.revision=section.revision;cached.maskStamp=section.maskStamp;
                if(M_CheckParm((char*)"-verifyresidentuploads"))cached.snapshot.assign(data,data+section.count);
            }
            if(M_CheckParm((char*)"-verifyresidentuploads")&&(cached.snapshot.size()!=section.count||memcmp(cached.snapshot.data(),data,size)))I_Error((char*)"Resident geometry differs from current CPU geometry");
            draws.push_back({cached.arena.buffer,0,(Uint32)section.count});cursor=section.first+section.count;
        }
        stream(cursor,vertices.size()-cursor);
    };
    for(const auto *groups:{&batches,&shadowBatches,&decalBatches,&effectBatches,&effectAddBatches,&mirrorBatches})for(const auto &batch:*groups)place(batch.second);
    place(rubbleEmissionVertices);place(mistVertices);place(shaftVertices);place(particleVertices);place(crestVertices);place(heatVertices);place(skyVertices);place(glassVertices);
    for(const auto *vertices:transient)place(*vertices);
    for(auto it=residentVertices.begin();it!=residentVertices.end();){
        if(it->second.seen!=uploadSerial){SDL_ReleaseGPUBuffer(device,it->second.arena.buffer);it=residentVertices.erase(it);}else ++it;
    }
    Uint32 blockerBytes=(Uint32)(lightWords.size()*sizeof(lightWords[0]));
    Uint32 sectorBytes=(Uint32)(sectors.size()*sizeof(sectors[0]));
    Uint32 vertexBytes=std::max<Uint32>(bytes,256);
    reserve(vertexArena,vertexBytes,SDL_GPU_BUFFERUSAGE_VERTEX);
    reserve(blockerArena,blockerBytes,SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);
    reserve(sectorArena,sectorBytes,SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);
    Uint32 residentOffset=vertexBytes+blockerBytes+sectorBytes;
    Uint32 imageOffset=residentOffset+residentBytes,hudBytes=(Uint32)(hud.size()*4);
    Uint32 panelBytes=(Uint32)(panel.size()*4),total=imageOffset+1024+hudBytes+panelBytes;
    if(!frameTransfer||frameTransferCapacity<total) {
        if(frameTransfer)SDL_ReleaseGPUTransferBuffer(device,frameTransfer);
        frameTransferCapacity=std::max<Uint32>(262144,total+total/2);
        SDL_GPUTransferBufferCreateInfo info={SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,frameTransferCapacity,0};
        frameTransfer=SDL_CreateGPUTransferBuffer(device,&info);
        if(!frameTransfer)I_Error((char*)"Could not allocate GPU frame upload: %s",SDL_GetError());
    }
    byte *mapped=(byte*)SDL_MapGPUTransferBuffer(device,frameTransfer,true);
    for(const auto& item:dynamic)memcpy(mapped+item.offset,item.data,item.size);
    memcpy(mapped+vertexBytes,lightWords.data(),blockerBytes);
    memcpy(mapped+vertexBytes+blockerBytes,sectors.data(),sectorBytes);
    for(const auto& item:pending)memcpy(mapped+residentOffset+item.offset,item.data,item.size);
    memcpy(mapped+imageOffset,palette,1024);
    memcpy(mapped+imageOffset+1024,hud.data(),hudBytes);
    if(panelBytes)memcpy(mapped+imageOffset+1024+hudBytes,panel.data(),panelBytes);
    SDL_UnmapGPUTransferBuffer(device,frameTransfer);
    SDL_GPUCopyPass *copy=SDL_BeginGPUCopyPass(command);
    auto send=[&](Uint32 offset,SDL_GPUBuffer *buffer,Uint32 size) {
        SDL_GPUTransferBufferLocation source={frameTransfer,offset};
        SDL_GPUBufferRegion target={buffer,0,size};
        SDL_UploadToGPUBuffer(copy,&source,&target,true);
    };
    if(bytes)send(0,vertexArena.buffer,bytes);
    for(const auto& item:pending)send(residentOffset+item.offset,item.buffer,item.size);
    send(vertexBytes,blockerArena.buffer,blockerBytes);
    send(vertexBytes+blockerBytes,sectorArena.buffer,sectorBytes);
    auto image=[&](Uint32 offset,SDL_GPUTexture *texture,Uint32 w,Uint32 h) {
        SDL_GPUTextureTransferInfo source={frameTransfer,offset,w,h};
        SDL_GPUTextureRegion target={};target.texture=texture;target.w=w;target.h=h;target.d=1;
        SDL_UploadToGPUTexture(copy,&source,&target,true);
    };
    image(imageOffset,paletteTexture,256,1);
    image(imageOffset+1024,hudTexture,SCREENWIDTH,SCREENHEIGHT*2);
    if(panelBytes)image(imageOffset+1024+hudBytes,settingsTexture,DOOM_SETTINGS_WIDTH,DOOM_SETTINGS_HEIGHT);
    SDL_EndGPUCopyPass(copy);
    if(M_CheckParm((char*)"-profileuploads")&&gametic%10==0){size_t kept=0;for(const auto& entry:residentVertices)kept+=entry.second.count*sizeof(Vertex);printf("UPLOAD tic %d dynamic %u changed %u resident %zu batches %zu\n",gametic,bytes,residentBytes,kept,pending.size());}
}
void drawVertices(SDL_GPURenderPass *pass,const std::vector<Vertex> &vertices) {
    if(vertices.empty())return;
    auto found=residentBindings.find(&vertices);if(found==residentBindings.end())return;
    for(const auto& section:found->second){
        SDL_GPUBufferBinding binding={section.buffer?section.buffer:vertexArena.buffer,section.offset};
        SDL_BindGPUVertexBuffers(pass,0,&binding,1);SDL_DrawGPUPrimitives(pass,section.count,1,0,0);
    }
}
void setView(SDL_GPURenderPass *pass,float x,float y,float w,float h,const SDL_Rect *scissor) {
    SDL_GPUViewport viewport={x,y,w,h,0,1};SDL_SetGPUViewport(pass,&viewport);
    SDL_Rect clip=scissor?*scissor:SDL_Rect{(int)x,(int)y,(int)w,(int)h};
    SDL_SetGPUScissor(pass,&clip);
}
// World fragment resources: image, palette, next frame, seams, contact,
// reflection, palette lookup, wall bake, flat light, caustic map and pattern,
// wall and flat light directions, the surface's screen glass frame, detail
// texture layers, the shore map; blockers and sector data;
// four uniform blocks.
// Bindings reset with every render pass, so each pass binds them again.
void bindWorld(SDL_GPURenderPass *pass,SDL_GPUTexture *reflection) {
    SDL_GPUTextureSamplerBinding samplers[]={
        {paletteTexture,nearestClamp},{seamTexture?gpu(seamTexture):dummySeam,nearestClamp},
        {contactTexture?gpu(contactTexture):dummyContact,linearClamp},{reflection?reflection:paletteTexture,linearClamp},
        {paletteLUT,nearestClamp},{wallBakeTexture?gpu(wallBakeTexture):dummyBake,nearestClamp},
        {flatLightTexture?gpu(flatLightTexture):dummyBake,nearestClamp},
        {causticTexture?gpu(causticTexture):dummyContact,linearClamp},{causticPattern?gpu(causticPattern):dummyContact,nearestClamp},
        {wallDirectionTexture?gpu(wallDirectionTexture):dummyBake,nearestClamp},{flatDirectionTexture?gpu(flatDirectionTexture):dummyBake,nearestClamp},
        {dummyBake,nearestClamp},{shoreTexture?gpu(shoreTexture):dummyContact,linearClamp},{dummyRelief,nearestClamp}};
    SDL_BindGPUFragmentSamplers(pass,1,&samplers[0],1);
    SDL_BindGPUFragmentSamplers(pass,3,&samplers[1],13);
    SDL_GPUBuffer *storage[]={blockerArena.buffer,sectorArena.buffer};
    SDL_BindGPUFragmentStorageBuffers(pass,0,storage,2);
}
// The bloom pass's glowing liquid edges: shore map, caustic pattern (holding
// the edge strip), sector heights and the camera.
void bindEmissionEdges(SDL_GPUCommandBuffer *command,SDL_GPURenderPass *pass,const Uniforms &camera) {
    SDL_GPUTextureSamplerBinding samplers[]={{shoreTexture?gpu(shoreTexture):dummyContact,linearClamp},
        {causticPattern?gpu(causticPattern):dummyContact,nearestClamp}};
    SDL_BindGPUFragmentSamplers(pass,3,samplers,2);
    SDL_BindGPUFragmentStorageBuffers(pass,0,&sectorArena.buffer,1);
    SDL_PushGPUFragmentUniformData(command,1,&camera,sizeof(camera));
}
void bindLightBlockers(SDL_GPURenderPass *pass) {SDL_BindGPUFragmentStorageBuffers(pass,0,&blockerArena.buffer,1);}
// Binds a batch texture with its next animation frame and crossfade amount;
// world passes also take its screen glass frame and bump relief.
void bindSurface(SDL_GPUCommandBuffer *command,SDL_GPURenderPass *pass,int key,Uint32 blendSlot,bool world) {
    SurfaceBinding binding=surfaceBinding(key);
    SDL_GPUTextureSamplerBinding image={gpu(binding.image->texture),linearRepeat},next={gpu(binding.next->texture),linearRepeat};
    SDL_BindGPUFragmentSamplers(pass,0,&image,1);
    SDL_BindGPUFragmentSamplers(pass,2,&next,1);
    if(world) {
        SDL_GPUTextureSamplerBinding frame={binding.image->glassFrame?gpu(binding.image->glassFrame):dummyBake,nearestClamp};
        SDL_BindGPUFragmentSamplers(pass,13,&frame,1);
        SDL_GPUTextureSamplerBinding relief={binding.image->relief?gpu(binding.image->relief):dummyRelief,nearestClamp};
        SDL_BindGPUFragmentSamplers(pass,15,&relief,1);
    }
    // The crossfade, the weapon's surroundings (unused here), then the
    // liquid's frame and cycle averages.
    float blend[36]={binding.blend};
    std::copy(binding.frameMean.begin(),binding.frameMean.end(),blend+28);
    std::copy(binding.cycleMean.begin(),binding.cycleMean.end(),blend+32);
    SDL_PushGPUFragmentUniformData(command,blendSlot,blend,world?sizeof(blend):4*sizeof(float));
}
void bindImage(SDL_GPURenderPass *pass,const GpuTextureRef &texture,SDL_GPUSampler *filter) {
    SDL_GPUTextureSamplerBinding binding={gpu(texture),filter};
    SDL_BindGPUFragmentSamplers(pass,0,&binding,1);
}
void pushWorldUniforms(SDL_GPUCommandBuffer *command,const Uniforms &view) {
    float noBlend[36]={};
    SDL_PushGPUVertexUniformData(command,0,&view,sizeof(view));
    SDL_PushGPUFragmentUniformData(command,0,&view,sizeof(view));
    SDL_PushGPUFragmentUniformData(command,1,&gpuFlashes,sizeof(gpuFlashes));
    SDL_PushGPUFragmentUniformData(command,2,&fogLights,sizeof(fogLights));
    SDL_PushGPUFragmentUniformData(command,3,noBlend,sizeof(noBlend));
}
bool opaqueBatch(const std::pair<const int,std::vector<Vertex>> &batch) {
    return !batch.second.empty()&&!(batch.second.front().mode&MODE_SPRITE)&&images.at(batch.first).opaque;
}
// The world drawn opaque-first, then sky surfaces into depth and the sky into
// uncovered pixels, then cutouts and sprites. Shared by the main view and the
// liquid reflection. With coverage and cutout pipelines, masked walls first
// write their depth, after the sky so their soft edges still blend over it,
// and then shade only where they are frontmost. Without sprites (glass
// probes), only the map itself.
void drawWorld(SDL_GPUCommandBuffer *command,SDL_GPURenderPass *pass,SDL_GPUGraphicsPipeline *opaque,SDL_GPUGraphicsPipeline *skySurface,
               SDL_GPUGraphicsPipeline *sky,SDL_GPUGraphicsPipeline *world,SDL_GPUTexture *reflection,
               SDL_GPUGraphicsPipeline *coverage,SDL_GPUGraphicsPipeline *cutout,bool sprites) {
    SDL_BindGPUGraphicsPipeline(pass,opaque);bindWorld(pass,reflection);
    for(const auto &batch:batches) if(opaqueBatch(batch)) {bindSurface(command,pass,batch.first,3);drawVertices(pass,batch.second);}
    SDL_GPUTextureSamplerBinding skyImage[]={{gpu(wallImage(skytexture).texture),linearRepeat},{paletteTexture,nearestClamp}};
    if(!skyVertices.empty()) {
        SDL_BindGPUGraphicsPipeline(pass,skySurface);SDL_BindGPUFragmentSamplers(pass,0,skyImage,2);
        drawVertices(pass,skyVertices);
    }
    SDL_BindGPUGraphicsPipeline(pass,sky);
    SDL_BindGPUFragmentSamplers(pass,0,skyImage,2);
    SDL_DrawGPUPrimitives(pass,3,1,0,0);
    auto cutouts=[&](SDL_GPUGraphicsPipeline *pipeline) {
        SDL_BindGPUGraphicsPipeline(pass,pipeline);bindWorld(pass,reflection);
        for(const auto &batch:batches) {
            if(batch.second.empty()||(batch.second.front().mode&MODE_SPRITE)||images.at(batch.first).opaque)continue;
            bindSurface(command,pass,batch.first,3);drawVertices(pass,batch.second);
        }
    };
    if(coverage&&cutout) {cutouts(coverage);cutouts(cutout);}
    else cutouts(world);
    if(sprites) {SDL_BindGPUGraphicsPipeline(pass,world);bindWorld(pass,reflection);}
    for(const auto &batch:batches) {
        if(!sprites||batch.second.empty()||!(batch.second.front().mode&MODE_SPRITE))continue;
        bindSurface(command,pass,batch.first,3);drawVertices(pass,batch.second);
    }
    float noBlend[36]={};SDL_PushGPUFragmentUniformData(command,3,noBlend,sizeof(noBlend));
}
void drawOverlays(SDL_GPUCommandBuffer *command,SDL_GPURenderPass *pass,const FrameView &view,
                  const std::vector<SpriteDraw> &weapons,const std::vector<Vertex> &hud,const std::vector<Vertex> &cross,bool depth) {
    SDL_Rect full={0,0,width,height};
    setView(pass,0,view.uiY,width,view.uiHeight,&full);
    if(worldPending&&!weapons.empty()) {
        SDL_BindGPUGraphicsPipeline(pass,depth?weaponPipeline:overlayWeaponPipeline);bindWorld(pass,nullptr);
        // No crossfade, then what the weapon sees around it.
        float blend[36]={};std::copy(weaponProbe,weaponProbe+24,blend+4);
        SDL_PushGPUFragmentUniformData(command,3,blend,sizeof(blend));
        for(const auto &draw:weapons) {
            SDL_BindGPUGraphicsPipeline(pass,draw.additive?(depth?weaponAddPipeline:overlayWeaponAddPipeline):(depth?weaponPipeline:overlayWeaponPipeline));
            SDL_GPUTextureSamplerBinding image[]={{gpu(draw.image->texture),linearRepeat},{paletteTexture,nearestClamp},{gpu(draw.image->texture),linearRepeat}};
            SDL_BindGPUFragmentSamplers(pass,0,image,3);drawVertices(pass,draw.vertices);
        }
    }
    setView(pass,0,view.uiY,width,view.uiHeight,&full);
    SDL_BindGPUGraphicsPipeline(pass,depth?hudPipeline:overlayHudPipeline);
    SDL_GPUTextureSamplerBinding hudImage={hudTexture,nearestClamp};
    SDL_BindGPUFragmentSamplers(pass,0,&hudImage,1);
    drawVertices(pass,hud);
    if(!cross.empty()) {
        setView(pass,view.worldX,view.worldY,view.worldW,view.worldH,&full);
        SDL_BindGPUGraphicsPipeline(pass,depth?solidPipeline:overlaySolidPipeline);drawVertices(pass,cross);
    }
}
void releaseFrameUploads() {
    for(auto& entry:residentVertices)release(entry.second.arena.buffer,SDL_ReleaseGPUBuffer);
    residentVertices.clear();residentBindings.clear();uploadSerial=0;
    for(Arena *a:{&vertexArena,&blockerArena,&sectorArena}) {release(a->buffer,SDL_ReleaseGPUBuffer);a->capacity=0;}
    release(frameTransfer,SDL_ReleaseGPUTransferBuffer);frameTransferCapacity=0;
}
} // namespace gpu3d
