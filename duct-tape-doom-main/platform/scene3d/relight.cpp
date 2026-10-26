/* Background re-bake of lamp light and sun around moving sectors
   (doors, lifts, lowering walls) and shot-out lights. */
#include "internal.h"
#include "frame_workers.h"

namespace doom3d {
// Moving sectors re-light (settings.movingRelight): bakeMap follows doors,
// lifts and lowering walls, at their exact heights at rest and on an 8-unit
// grid while they move, so things lit by traceStatic see through an open
// door at once. Each change re-bakes, on a background thread, the static
// light within reach of every baked light whose radius touches the sector,
// and the sun on floors and walls up to the distance the sector's opening
// can throw it. Bounce, sky light, flow and sunbeams keep the level's start.
// One re-bake runs at a time; changes meanwhile wait for the next.
struct Relight {
    std::thread worker;
    std::unique_ptr<FrameWorkers> pool;
    std::atomic<bool> done{false};
    bool running=false,lights=false,only=false,grid=false,sun=false;
    BakeMap map; // bakeMap as it was when the re-bake started.
    float sunDirection[3]={};
    // Per map cell, wall strip and grid cell: 1 re-light, 2 re-sun.
    std::vector<uint8_t> cells,strips,gridCells;
    int lightBounds[4],sunBounds[4]; // Marked cells: x0, y0, x1, y1 inclusive.
    std::vector<int16_t> sunCells; // Floor sun over sunBounds; -1 unchanged.
    std::vector<size_t> gridList;
    std::vector<StaticCell> gridResults;
    // Merged lights that lost shot-out tiles in this re-bake, as they shone
    // and as they shine now (strength 0 when all went), and per sector the
    // floor light they lost and the floor light left after (every other
    // cell each way).
    std::vector<BakeLight> gone,kept;
    std::vector<double> lost,left;
} relight;
static std::vector<int> relightPending;
static std::vector<SurfaceLightKey> brokenPending;
void relightBrokenLight(const SurfaceLightKey &cell) {brokenPending.push_back(cell);}
// The height a sector's floor or ceiling takes in bakeMap.
static float bakeHeight(int sector,bool ceiling) {
    float now=units(ceiling?sectors[sector].ceilingheight:sectors[sector].floorheight);
    if((size_t)sector>=oldHeights.size()||oldHeights[sector][ceiling]==now)return now;
    return std::round(now/8)*8;
}
static void markRelight(float x0,float y0,float x1,float y1,uint8_t bit) {
    int cx0=std::clamp((int)std::floor((x0-mapOrigin[0])/mapCell),0,mapWidth-1),cx1=std::clamp((int)std::floor((x1-mapOrigin[0])/mapCell),0,mapWidth-1);
    int cy0=std::clamp((int)std::floor((y0-mapOrigin[1])/mapCell),0,mapHeight-1),cy1=std::clamp((int)std::floor((y1-mapOrigin[1])/mapCell),0,mapHeight-1);
    for(int y=cy0;y<=cy1;++y)for(int x=cx0;x<=cx1;++x)relight.cells[(size_t)y*mapWidth+x]|=bit;
    int *b=bit==1?relight.lightBounds:relight.sunBounds;
    b[0]=std::min(b[0],cx0);b[1]=std::min(b[1],cy0);b[2]=std::max(b[2],cx1);b[3]=std::max(b[3],cy1);
    for(int index:bakeOrder) {
        const BakeStrip &strip=bakeStrips[index];
        float bx=strip.ax+strip.dx*strip.length,by=strip.ay+strip.dy*strip.length;
        if(std::max(strip.ax,bx)<x0||std::min(strip.ax,bx)>x1||std::max(strip.ay,by)<y0||std::min(strip.ay,by)>y1)continue;
        relight.strips[index]|=bit;
    }
    if(bit==1&&relight.grid) {
        int gx0=std::clamp((int)std::floor((x0-mapOrigin[0])/staticCell),0,staticWidth-1),gx1=std::clamp((int)std::floor((x1-mapOrigin[0])/staticCell),0,staticWidth-1);
        int gy0=std::clamp((int)std::floor((y0-mapOrigin[1])/staticCell),0,staticHeight-1),gy1=std::clamp((int)std::floor((y1-mapOrigin[1])/staticCell),0,staticHeight-1);
        for(int y=gy0;y<=gy1;++y)for(int x=gx0;x<=gx1;++x)relight.gridCells[(size_t)y*staticWidth+x]=1;
    }
}
// How much of each sector's floor light the shot-out tiles gave (their
// merged lights before, less what is left of them), against what the
// re-bake left it (every other cell each way, both).
static void measureLoss(Relight &job) {
    static const float up[3]={0,0,1};
    job.lost.assign(numsectors,0);job.left.assign(numsectors,0);
    const int *b=job.lightBounds;
    for(int y=b[1];y<=b[3];y+=2)for(int x=b[0];x<=b[2];x+=2) {
        size_t i=(size_t)y*mapWidth+x;int sector=seamCells[i].own;
        if(!(job.cells[i]&1)||sector==noSector||job.map.sectors[sector].sky)continue;
        float px=mapOrigin[0]+(x+0.5f)*mapCell,py=mapOrigin[1]+(y+0.5f)*mapCell,before[3]={},after[3]={};
        for(const BakeLight &light:job.gone)addBakedLight(job.map,light,sector,px,py,job.map.sectors[sector].floor+1,up,before);
        for(const BakeLight &light:job.kept)
            if(light.strength>0)addBakedLight(job.map,light,sector,px,py,job.map.sectors[sector].floor+1,up,after);
        job.lost[sector]+=luminance(before)-luminance(after);
    }
    for(double &lost:job.lost)lost=std::max(lost,0.0);
    for(int y=0;y<mapHeight;y+=2)for(int x=0;x<mapWidth;x+=2) {
        size_t i=(size_t)y*mapWidth+x;int sector=seamCells[i].own;
        if(sector==noSector||job.lost[sector]<=0)continue;
        const uint8_t *cell=flatCell(floorLight,i);
        float rgb[3]={cell[0]/255.0f*2,cell[1]/255.0f*2,cell[2]/255.0f*2};
        job.left[sector]+=luminance(rgb);
    }
}
static void startRelight() {
    Relight &job=relight;
    job.lights=settings.bakedLights&&lightBakeKey>=0&&lightBakeKey==lightBakeInputs()&&!bakeGrid.empty()&&wallBakeTexture&&flatLightTexture;
    job.only=job.lights&&wallDirectionTexture&&flatDirectionTexture; // Directions, as bakeLights made them.
    job.grid=job.lights&&staticSerial==lightBakeSerial&&!staticGrid.empty();
    job.sun=sunBaked&&sunLevel>0&&contactTexture&&wallBakeTexture;
    std::vector<int> pending;pending.swap(relightPending);
    // Shot-out fixtures (shot_lights.cpp) leave the bake. The merged lights
    // they were part of take the tiles left of them (remergeBakeSource), or
    // go dark, at once, as things read them live, and here, with no re-bake
    // reading them; the light they baked changes with this re-bake, where
    // they reached before and where they reach now. Bounce, like the rest
    // the re-bake keeps, stays as the level started.
    std::set<SurfaceLightKey> broken(brokenPending.begin(),brokenPending.end());brokenPending.clear();
    job.gone.clear();job.kept.clear();job.lost.clear();job.left.clear();
    for(size_t n=0;n<bakeSourceKeys.size()&&n<bakeSources.size();++n) {
        if(!broken.count(bakeSourceKeys[n])||bakeSources[n].strength<=0)continue;
        job.gone.push_back(bakeSources[n]);
        remergeBakeSource((int)n);
        job.kept.push_back(bakeSources[n]);
    }
    if(!job.lights&&!job.sun)return;
    job.cells.assign((size_t)mapWidth*mapHeight,0);job.strips.assign(bakeStrips.size(),0);
    job.gridCells.assign(job.grid?staticGrid.size():0,0);
    for(int *b:{job.lightBounds,job.sunBounds}) {b[0]=b[1]=INT_MAX;b[2]=b[3]=-1;}
    if(job.lights)for(const auto *changed:{&job.gone,&job.kept})for(const BakeLight &light:*changed)
        if(light.strength>0)markRelight(light.x-light.radius,light.y-light.radius,light.x+light.radius,light.y+light.radius,1);
    const float elevation=sunElevation;
    for(int sector:pending) {
        const BakeSector &s=bakeMap.sectors[sector];
        float x0=1e9f,y0=1e9f,x1=-1e9f,y1=-1e9f,low=s.floor,high=s.ceiling;
        for(int index:s.lines) {
            const BakeLine &line=bakeMap.lines[index];
            x0=std::min({x0,line.ax,line.bx});y0=std::min({y0,line.ay,line.by});
            x1=std::max({x1,line.ax,line.bx});y1=std::max({y1,line.ay,line.by});
            int other=line.front==sector?line.back:line.front;
            if(other>=0) {low=std::min(low,bakeMap.sectors[other].floor);high=std::max(high,bakeMap.sectors[other].ceiling);}
        }
        if(x1<x0)continue;
        if(job.lights) {
            for(const BakeLight &light:bakeSources) {
                float dx=std::max({x0-light.x,0.0f,light.x-x1}),dy=std::max({y0-light.y,0.0f,light.y-y1});
                if(dx*dx+dy*dy<light.radius*light.radius)
                    markRelight(light.x-light.radius,light.y-light.radius,light.x+light.radius,light.y+light.radius,1);
            }
            for(const BakeArea &area:bakeAreas) {
                if(area.high[0]+area.radius<x0||area.low[0]-area.radius>x1||area.high[1]+area.radius<y0||area.low[1]-area.radius>y1)continue;
                markRelight(area.low[0]-area.radius,area.low[1]-area.radius,area.high[0]+area.radius,area.high[1]+area.radius,1);
            }
        }
        if(job.sun) {
            // Sun through the sector reaches floors and walls this far away from the sun.
            float reach=std::min((high-low)/std::tan(elevation)+16,1024.0f);
            float sx=-sunAzimuth[0]*reach,sy=-sunAzimuth[1]*reach;
            markRelight(x0+std::min(sx,0.0f),y0+std::min(sy,0.0f),x1+std::max(sx,0.0f),y1+std::max(sy,0.0f),2);
        }
    }
    if(job.lightBounds[2]<0&&job.sunBounds[2]<0)return;
    job.map=bakeMap;
    for(int c=0;c<3;++c)job.sunDirection[c]=sunDirection[c];
    const int *sb=job.sunBounds;
    job.sunCells.assign(sb[2]>=0?(size_t)(sb[2]-sb[0]+1)*(sb[3]-sb[1]+1):0,-1);
    job.gridList.clear();
    for(size_t i=0;i<job.gridCells.size();++i)if(job.gridCells[i])job.gridList.push_back(i);
    job.gridResults.assign(job.gridList.size(),{});
    job.done=false;job.running=true;
    // Frame lighting already has its own workers. Reuse a smaller background
    // pool rather than creating half the logical CPUs' worth of threads for
    // every bake phase, competing with rendering while a door moves.
    if(!job.pool)job.pool=std::make_unique<FrameWorkers>(std::clamp(std::thread::hardware_concurrency()/4,1u,4u));
    job.worker=std::thread([&] {
        int y0=std::min(job.lightBounds[1],job.sunBounds[1]),y1=std::max(job.lightBounds[3],job.sunBounds[3]);
        const int *sb=job.sunBounds;
        job.pool->each(y1-y0+1,[&](int row) {
            int y=y0+row;
            for(int x=0;x<mapWidth;++x) {
                size_t i=(size_t)y*mapWidth+x;uint8_t mark=job.cells[i];
                if(mark&1)lightCell(job.map,i,job.only);
                if(mark&2)job.sunCells[(size_t)(y-sb[1])*(sb[2]-sb[0]+1)+(x-sb[0])]=(int16_t)sunCell(job.map,i,job.sunDirection);
            }
        });
        std::vector<int> strips;
        for(int index:bakeOrder)if(job.strips[index])strips.push_back(index);
        job.pool->each((int)strips.size(),[&](int n) {
            const BakeStrip &strip=bakeStrips[strips[n]];
            if(job.strips[strips[n]]&1)lightStrip(job.map,strip,job.only);
            if(job.strips[strips[n]]&2)sunStrip(job.map,strip,job.sunDirection);
        });
        job.pool->each((int)job.gridList.size(),[&](int n) {
            size_t i=job.gridList[n];
            job.gridResults[n]=staticCellAt(job.map,(int)(i%staticWidth),(int)(i/staticWidth));
        });
        if(job.lights&&!job.gone.empty())measureLoss(job);
        job.done=true;
    });
}
void finishRelight(bool apply) {
    Relight &job=relight;
    if(!job.running)return;
    job.worker.join();job.running=false;
    if(!apply)return;
    const int *lb=job.lightBounds,*sb=job.sunBounds;
    if(job.lights&&lb[2]>=0) {
        int w=lb[2]-lb[0]+1,h=lb[3]-lb[1]+1;
        gpuUpdateTexture(flatLightTexture,GpuFormat::RGBA8,{{lb[0],lb[1]+floorLight*mapHeight,w,h},{lb[0],lb[1]+ceilingLight*mapHeight,w,h}},
                         flatLightCells.data(),mapWidth*4);
        if(job.only)gpuUpdateTexture(flatDirectionTexture,GpuFormat::RGBA8,{{lb[0],lb[1],w,h},{lb[0],lb[1]+mapHeight,w,h}},flatDirectionCells.data(),mapWidth*4);
        for(size_t n=0;n<job.gridList.size();++n)staticGrid[job.gridList[n]]=job.gridResults[n];
        for(size_t s=0;s<job.lost.size();++s)
            if(job.lost[s]>0)dimShotSector((int)s,(float)(job.lost[s]/(job.lost[s]+job.left[s])));
        job.lost.clear();job.left.clear();
    }
    if(job.sun&&sb[2]>=0) {
        int w=sb[2]-sb[0]+1,h=sb[3]-sb[1]+1;
        for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
            int16_t visible=job.sunCells[(size_t)y*w+x];
            if(visible>=0)contactCells[4*((size_t)(sb[1]+y)*mapWidth+sb[0]+x)+2]=(uint8_t)visible;
        }
        gpuUpdateTexture(contactTexture,GpuFormat::RGBA8,{{sb[0],sb[1],w,h}},contactCells.data(),mapWidth*4);
    }
    std::vector<std::array<int,4>> rects,directions;
    for(size_t index=0;index<job.strips.size();++index) {
        if(!job.strips[index])continue;
        const BakeStrip &strip=bakeStrips[index];
        rects.push_back({strip.x,strip.y,strip.width,strip.height});
        if(job.only&&(job.strips[index]&1))directions.push_back(rects.back());
    }
    gpuUpdateTexture(wallBakeTexture,GpuFormat::RGBA8,rects,wallBakeCells.data(),atlasStride*4);
    gpuUpdateTexture(wallDirectionTexture,GpuFormat::RGBA8,directions,wallDirectionCells.data(),atlasWidth*4);
}
// Called each frame before geometry: follows sector heights into bakeMap,
// applies a finished re-bake and starts the next.
void relightMovingSectors() {
    if(relight.running&&relight.done)finishRelight();
    if(bakeMap.sectors.size()!=(size_t)numsectors)return;
    if(settings.movingRelight)for(int i=0;i<numsectors;++i) {
        BakeSector &s=bakeMap.sectors[i];
        float floor=bakeHeight(i,false),ceiling=bakeHeight(i,true);
        if(floor==s.floor&&ceiling==s.ceiling)continue;
        invalidateStaticSamples();s.floor=floor;s.ceiling=ceiling;
        if(std::find(relightPending.begin(),relightPending.end(),i)==relightPending.end())relightPending.push_back(i);
    }
    if(!relight.running&&(!relightPending.empty()||!brokenPending.empty()))startRelight();
}
// Waits for any re-bake in flight and drops what it would apply.
void stopRelight() {finishRelight(false);relightPending.clear();brokenPending.clear();}
} // namespace doom3d
