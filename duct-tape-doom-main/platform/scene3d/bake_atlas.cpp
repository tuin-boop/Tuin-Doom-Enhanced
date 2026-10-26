/* Layout of the light bakes: the wall strip atlas and the map-space floor
   and ceiling layers, and their upload. */
#include "internal.h"

namespace doom3d {
std::vector<BakeStrip> bakeStrips;
std::vector<int> bakeOrder; // Strips that have texels, tallest first.
int atlasHeight=0;
std::vector<uint8_t> wallBakeCells;
std::vector<uint8_t> flatLightCells;
BakeMap bakeMap;
// Shared by the sun and light bakes, built once per level: the sector/line
// map the rays walk, and a texel for every wall at the floor cell size.
// Sector heights start as the level's; relightMovingSectors keeps them
// following doors and lifts.
void layoutBake() {
    invalidateStaticSamples();
    bakeMap={};bakeMap.sectors.resize(numsectors);
    for(int i=0;i<numsectors;++i)
        bakeMap.sectors[i]={units(sectors[i].floorheight),units(sectors[i].ceilingheight),sectors[i].ceilingpic==skyflatnum,{}};
    for(int i=0;i<numlines;++i) {
        const line_t &line=lines[i];
        int front=line.frontsector?(int)(line.frontsector-sectors):-1,back=line.backsector?(int)(line.backsector-sectors):-1;
        bakeMap.lines.push_back({units(line.v1->x),units(line.v1->y),units(line.v2->x),units(line.v2->y),front,back});
        if(front>=0)bakeMap.sectors[front].lines.push_back(i);
        if(back>=0&&back!=front)bakeMap.sectors[back].lines.push_back(i);
    }
    buildBakeLineTrees(bakeMap);
    bakeMap.verifyRays=M_CheckParm((char*)"-verifybakerays")!=0;
    // Strip heights cover what doors, lifts and floors can reach: each
    // sector's own and its neighbors' floors and ceilings.
    std::vector<float> reachLow(numsectors),reachHigh(numsectors);
    for(int i=0;i<numsectors;++i) {reachLow[i]=bakeMap.sectors[i].floor;reachHigh[i]=bakeMap.sectors[i].ceiling;}
    for(const auto &line:bakeMap.lines) if(line.front>=0&&line.back>=0) for(int k=0;k<2;++k) {
        int a=k?line.back:line.front,b=k?line.front:line.back;
        reachLow[a]=std::min(reachLow[a],bakeMap.sectors[b].floor);reachHigh[a]=std::max(reachHigh[a],bakeMap.sectors[b].ceiling);
    }
    bakeStrips.assign((size_t)numlines*2,{});bakeOrder.clear();
    for(int i=0;i<numlines;++i) for(int side=0;side<2;++side) {
        const line_t &line=lines[i];if(line.sidenum[side]<0)continue;
        BakeStrip &strip=bakeStrips[(size_t)i*2+side];
        strip.sector=(int)(sides[line.sidenum[side]].sector-sectors);
        const vertex_t *from=side?line.v2:line.v1,*to=side?line.v1:line.v2;
        strip.ax=units(from->x);strip.ay=units(from->y);
        float dx=units(to->x)-strip.ax,dy=units(to->y)-strip.ay;
        strip.length=std::hypot(dx,dy);if(strip.length<1)continue;
        strip.dx=dx/strip.length;strip.dy=dy/strip.length;
        const sector_t *other=side?line.frontsector:line.backsector;
        strip.low=reachLow[strip.sector];strip.high=reachHigh[strip.sector];
        if(other) {int o=(int)(other-sectors);strip.low=std::min(strip.low,reachLow[o]);strip.high=std::max(strip.high,reachHigh[o]);}
        if(strip.high>strip.low)bakeOrder.push_back(i*2+side);
    }
    std::sort(bakeOrder.begin(),bakeOrder.end(),[](int a,int b) {
        return bakeStrips[a].high-bakeStrips[a].low>bakeStrips[b].high-bakeStrips[b].low;
    });
    // Shelf packing at the floor cell size, coarser if the atlas would not fit.
    constexpr int atlasLimit=8192;
    for(float cell=mapCell;;cell*=2) {
        int x=0,y=0,shelf=0;bool fits=true;
        for(int index:bakeOrder) {
            BakeStrip &strip=bakeStrips[index];
            strip.scale=std::min(1/cell,(atlasWidth-2)/strip.length);
            strip.width=(int)std::ceil(strip.length*strip.scale)+1;
            strip.height=(int)std::ceil((strip.high-strip.low)*strip.scale)+1;
            if(x+strip.width>atlasWidth) {x=0;y+=shelf;shelf=0;}
            strip.x=x;strip.y=y;x+=strip.width;shelf=std::max(shelf,strip.height);
            if(y+shelf>atlasLimit) {fits=false;break;}
        }
        if(fits) {atlasHeight=std::max(1,y+shelf);break;}
    }
    wallBakeCells.assign((size_t)atlasStride*atlasHeight*4,0);
    flatLightCells.assign((size_t)mapWidth*mapHeight*4*4,0);
    // Middle textures on two-sided lines block rays where they are opaque,
    // placed as geometry() draws them in the level's initial state.
    for(int i=0;i<numlines;++i) {
        const line_t &line=lines[i];
        if(!line.frontsector||!line.backsector)continue;
        for(int side=0;side<2;++side) {
            if(line.sidenum[side]<0||!sides[line.sidenum[side]].midtexture)continue;
            const side_t &s=sides[line.sidenum[side]];
            const Image &image=wallImage(texturetranslation[s.midtexture]);
            float low=std::max(units(line.frontsector->floorheight),units(line.backsector->floorheight));
            float high=std::min(units(line.frontsector->ceilingheight),units(line.backsector->ceilingheight));
            float anchor=((line.flags&ML_DONTPEGBOTTOM)?low+image.height:high)+units(s.rowoffset);
            float length=std::hypot(bakeMap.lines[i].bx-bakeMap.lines[i].ax,bakeMap.lines[i].by-bakeMap.lines[i].ay);
            // Side 1 runs from v2, so its texture u decreases from v1.
            float u=units(s.textureoffset)+(side?length:0);
            bakeMap.lines[i].mask={image.pixels.data(),image.width,image.height,u,side?-1.0f:1.0f,anchor,
                                   std::max(low,anchor-image.height),std::min(high,anchor)};
            break;
        }
    }
}
// Runs work(n) for n in [0,count) on all cores (at most threads); work must
// touch only its own texels. Under the loading screen the calling thread
// keeps the screen drawn with the progress and a worker takes its place.
void parallelFor(int count,const std::function<void(int)> &work,unsigned threads) {
    std::atomic<int> next{0},done{0};
    auto run=[&] {for(int n;(n=next++)<count;) {work(n);++done;}};
    bool loading=loadingOnThisThread();
    std::vector<std::thread> workers;
    for(unsigned n=loading?0:1;n<std::clamp(std::thread::hardware_concurrency(),1u,std::max(1u,threads));++n)workers.emplace_back(run);
    if(loading)while(done<count) {loadingTick((float)done/std::max(1,count));SDL_Delay(2);}
    else run();
    for(auto &worker:workers)worker.join();
}
void uploadWallBake() {
    wallBakeTexture=gpuCreateTexture(GpuFormat::RGBA8,atlasStride,atlasHeight,wallBakeCells.data(),atlasStride*4);
    if(!wallBakeTexture)I_Error((char*)"Could not allocate surface maps");
}
void uploadFlatLight() {
    bakeWetness(); // The ground's wetness under the rain rides in the floor light's alpha.
    flatLightTexture=gpuCreateTexture(GpuFormat::RGBA8,mapWidth,mapHeight*4,flatLightCells.data(),mapWidth*4);
    if(!flatLightTexture)I_Error((char*)"Could not allocate surface maps");
}
// Wall vertex coordinates in the bake atlas; -1 leaves the wall without one.
// Walls also carry their strip's origin and scale in the tint for the
// texel-aligned read (blood decals use the tint for themselves).
void bakeWallCoordinates(const line_t &line,int side,float along,Vertex &vertex,bool strip) {
    if(bakeStrips.empty())return;
    const BakeStrip &s=bakeStrips[(size_t)(&line-lines)*2+side];
    if(s.x<0)return;
    vertex.sunU=s.x+along*s.scale;
    vertex.sunV=s.y+(std::clamp(vertex.z,s.low,s.high)-s.low)*s.scale;
    if(strip) {vertex.red=(float)s.x;vertex.green=(float)s.y;vertex.blue=s.scale;}
}
void bakeAtlasNewLevel() {wallBakeTexture=nullptr;flatLightTexture=nullptr;}
void bakeAtlasShutdown() {
    bakeAtlasNewLevel();
    bakeStrips.clear();bakeOrder.clear();wallBakeCells.clear();flatLightCells.clear();bakeMap={};
}
} // namespace doom3d
