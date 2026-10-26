/* Level geometry built at load: BSP floor polygons, sector classification
   (liquids, pits, outdoor) and the map-space seam and contact maps. */
#include "internal.h"
#include "../audio/ambience_spots.h"

namespace doom3d {
std::vector<std::vector<Point>> floors;
std::vector<std::vector<int>> sectorFloors;
std::vector<std::array<float,4>> floorBounds;
std::vector<SectorMist> sectorMist;
float fogReferenceHeight=0;
constexpr float seamBand=16,contactRange=48;
std::vector<SeamCell> seamCells;

std::vector<uint8_t> contactCells;
float mapOrigin[2],mapCell=4;int mapWidth=0,mapHeight=0;
void clipInto(const std::vector<Point> &poly,Point origin,Point direction,bool right,std::vector<Point> &out) {
    out.clear(); if(poly.empty()) return;
    auto distance=[&](Point p) {double d=(double)direction.x*(p.y-origin.y)-(double)direction.y*(p.x-origin.x);return right?-d:d;};
    Point a=poly.back(); double da=distance(a);
    for(Point b:poly) {
        double db=distance(b);
        if((da>=-0.01)!=(db>=-0.01)) {
            double t=da/(da-db);
            out.push_back({(float)(a.x+(b.x-a.x)*t),(float)(a.y+(b.y-a.y)*t)});
        }
        if(db>=-0.01) out.push_back(b);
        a=b; da=db;
    }
}
std::vector<Point> clip(const std::vector<Point> &poly,Point origin,Point direction,bool right) {
    std::vector<Point> out;clipInto(poly,origin,direction,right,out);return out;
}
static void partition(unsigned node,const std::vector<Point> &poly,int depth=0) {
    if(poly.size()<3) return;
    if(depth>numnodes+1) I_Error((char*)"Invalid BSP tree");
    if(node&NF_SUBSECTOR) {
        unsigned leaf=node&~NF_SUBSECTOR; if(leaf>=(unsigned)numsubsectors) I_Error((char*)"Invalid BSP leaf");
        // Only solid walls cut the BSP cell down: they hold back the void. A
        // two-sided seg runs between split vertices rounded to whole units, so
        // its line strays from the partition the cell already ends on, and
        // cutting along it on both sides would open sliver cracks to the
        // neighbor. Solid walls cut along their whole linedef, where the
        // wall is drawn, not the seg: a seg of a split line strays the same
        // way and would leave a sliver of void between flat and wall.
        auto shape=poly; const subsector_t &sub=subsectors[leaf];
        for(int i=0;i<sub.numlines;++i) {
            const seg_t &seg=segs[sub.firstline+i];
            if(seg.backsector)continue;
            Point a={units(seg.v1->x),units(seg.v1->y)},b={units(seg.v2->x),units(seg.v2->y)};
            if(const line_t *line=seg.linedef) {
                Point la={units(line->v1->x),units(line->v1->y)},lb={units(line->v2->x),units(line->v2->y)};
                if((lb.x-la.x)*(b.x-a.x)+(lb.y-la.y)*(b.y-a.y)<0)std::swap(la,lb);
                a=la;b=lb;
            }
            shape=clip(shape,a,{b.x-a.x,b.y-a.y},true);
        }
        floors[leaf]=std::move(shape); return;
    }
    if(node>=(unsigned)numnodes) I_Error((char*)"Invalid BSP node");
    const node_t &n=nodes[node]; Point origin={units(n.x),units(n.y)},direction={units(n.dx),units(n.dy)};
    partition(n.children[0],clip(poly,origin,direction,true),depth+1);
    partition(n.children[1],clip(poly,origin,direction,false),depth+1);
}
// Neighboring leaves are clipped apart, so a shared edge can carry a corner on
// one side only (a T-junction) or two nearly equal corners: both rasterize as
// hairline cracks. Snap close corners together, then give every edge the
// corners lying along it, so neighbors share exact vertices.
static void weldFloors() {
    constexpr float snap=0.05f,cell=64;
    std::unordered_map<int64_t,std::vector<Point>> grid;
    auto key=[](int x,int y){return (int64_t)x<<32|(uint32_t)y;};
    auto cellOf=[&](float v){return (int)std::floor(v/cell);}; // MSVC wants cell captured.
    auto weld=[&](Point p) {
        int cx=cellOf(p.x),cy=cellOf(p.y);
        for(int y=cy-1;y<=cy+1;++y)for(int x=cx-1;x<=cx+1;++x) {
            auto found=grid.find(key(x,y));if(found==grid.end())continue;
            for(Point q:found->second) if(std::abs(q.x-p.x)<=snap&&std::abs(q.y-p.y)<=snap) return q;
        }
        grid[key(cx,cy)].push_back(p);return p;
    };
    for(auto &poly:floors) {
        std::vector<Point> out;
        for(Point p:poly) {
            p=weld(p);
            if(out.empty()||out.back().x!=p.x||out.back().y!=p.y)out.push_back(p);
        }
        while(out.size()>1&&out.front().x==out.back().x&&out.front().y==out.back().y)out.pop_back();
        poly=out.size()<3?std::vector<Point>{}:std::move(out);
    }
    for(auto &poly:floors) {
        if(poly.empty())continue;
        std::vector<Point> out;
        for(size_t n=0;n<poly.size();++n) {
            Point a=poly[n],b=poly[(n+1)%poly.size()];out.push_back(a);
            float ex=b.x-a.x,ey=b.y-a.y,length2=ex*ex+ey*ey;
            std::vector<std::pair<float,Point>> along;
            for(int y=cellOf(std::min(a.y,b.y)-snap);y<=cellOf(std::max(a.y,b.y)+snap);++y)
                for(int x=cellOf(std::min(a.x,b.x)-snap);x<=cellOf(std::max(a.x,b.x)+snap);++x) {
                    auto found=grid.find(key(x,y));if(found==grid.end())continue;
                    for(Point q:found->second) {
                        float t=((q.x-a.x)*ex+(q.y-a.y)*ey)/length2;
                        if(t*t*length2<=snap*snap||(1-t)*(1-t)*length2<=snap*snap||t<0||t>1)continue;
                        float cross=ex*(q.y-a.y)-ey*(q.x-a.x);
                        if(cross*cross<=snap*snap*length2)along.push_back({t,q});
                    }
                }
            std::sort(along.begin(),along.end(),[](const auto &l,const auto &r){return l.first<r.first;});
            for(const auto &[t,q]:along)out.push_back(q);
        }
        poly=std::move(out);
    }
}
// Liquids are the liquid flats (game_materials.h) the engine animates: Doom 2's
// SLIME13-16 share the name but are still metal floors (MAP04's courtyard).
static game_liquid liquidFlat(int pic) {
    char name[9];memcpy(name,lumpinfo[firstflat+pic].name,8);name[8]=0;int next;
    game_liquid kind=Game_LiquidFlat(name);
    return kind!=GAME_LIQUID_NONE&&P_PicAnimationNext(false,pic,&next)>0?kind:GAME_LIQUID_NONE;
}
// How a liquid flat meets the walls around it (LIQUID_LOOK_*).
int liquidLook(int pic) {
    static std::vector<signed char> looks;
    if(pic<0||pic>=numflats)return LIQUID_LOOK_NONE;
    if((int)looks.size()!=numflats)looks.assign(numflats,-1);
    if(looks[pic]<0) {
        char name[9];memcpy(name,lumpinfo[firstflat+pic].name,8);name[8]=0;
        game_liquid kind=liquidFlat(pic);
        looks[pic]=kind==GAME_LIQUID_NONE?LIQUID_LOOK_NONE:Game_HotFlat(name)?LIQUID_LOOK_HOT:
                   kind==GAME_LIQUID_WATER?LIQUID_LOOK_WATER:Game_AmbientFlat(name,0)==AMBIENT_BLOOD?LIQUID_LOOK_BLOOD:LIQUID_LOOK_THICK;
    }
    return looks[pic];
}
bool iceFlat(int pic) {
    static std::vector<signed char> ice;
    if(pic<0||pic>=numflats)return false;
    if((int)ice.size()!=numflats)ice.assign(numflats,-1);
    if(ice[pic]<0) {char name[9];memcpy(name,lumpinfo[firstflat+pic].name,8);name[8]=0;ice[pic]=Game_IceFlat(name)?1:0;}
    return ice[pic]!=0;
}
// Big pools fade their flat toward its average with distance, keeping this
// much of its contrast: water most freely, thick liquids less, lava least so
// its glowing cracks still read.
float liquidDetailFloor(int pic) {
    switch(liquidLook(pic)) {
    case LIQUID_LOOK_WATER: return 0.2f;
    case LIQUID_LOOK_THICK: case LIQUID_LOOK_BLOOD: return 0.35f;
    case LIQUID_LOOK_HOT: return 0.5f;
    default: return 0;
    }
}
static void classifySectorMist() {
    sectorMist.assign(numsectors,{});
    std::vector<float> heights;
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    for(int i=0;i<numsectors;++i) {
        const sector_t &sector=sectors[i];auto &mist=sectorMist[i];
        heights.push_back(units(sector.floorheight));
        mist.outdoor=sector.ceilingpic==skyflatnum;
        float lowestNeighbor=1e9;bool hasNeighbor=false;
        for(int j=0;j<sector.linecount;++j) {
            const line_t &line=*sector.lines[j];
            const sector_t *neighbor=line.frontsector==&sector?line.backsector:line.frontsector;
            if(!neighbor||neighbor==&sector)continue;
            hasNeighbor=true;lowestNeighbor=std::min(lowestNeighbor,units(neighbor->floorheight));
        }
        mist.pit=hasNeighbor&&lowestNeighbor-units(sector.floorheight)>=24;
        int lump=firstflat+sector.floorpic;
        game_liquid liquid=liquidFlat(sector.floorpic);
        mist.liquid=liquid!=GAME_LIQUID_NONE;
        // Only water mirrors the room: nukage, slime, blood and lava are too
        // murky for the toned-down reflection to read as more than a smudge.
        mist.reflective=liquid==GAME_LIQUID_WATER;
        // Every liquid but water is thick: heavy splashes, bubbles, and a
        // collar of goo around things standing in it.
        mist.thick=mist.liquid&&!mist.reflective;
        mist.toxic=Game_DamagingFloor(sector.special);
        if(mist.liquid&&W_LumpLength(lump)>=4096) {
            const byte *flat=(const byte*)W_CacheLumpNum(lump,PU_CACHE);
            mist.color={};
            for(int pixel=0;pixel<4096;++pixel)for(int c=0;c<3;++c)
                mist.color[c]+=palette[flat[pixel]*3+c]/(4096.0f*255.0f);
        } else if(mist.toxic) mist.color={0.18f,0.30f,0.12f};
        char flatName[9];memcpy(flatName,lumpinfo[lump].name,8);flatName[8]=0;
        bool lava=Game_HotFlat(flatName);
        if((lava||mist.toxic)&&W_LumpLength(lump)>=4096) {
            const byte *flat=(const byte*)W_CacheLumpNum(lump,PU_CACHE);
            float rgb[3]={};
            for(int pixel=0;pixel<4096;++pixel)for(int c=0;c<3;++c)rgb[c]+=palette[flat[pixel]*3+c];
            mist.hot=lava||(rgb[0]>rgb[1]*1.25f&&rgb[0]>rgb[2]*1.4f);
        }
    }
    if(!heights.empty()) {
        std::nth_element(heights.begin(),heights.begin()+heights.size()/2,heights.end());
        fogReferenceHeight=heights[heights.size()/2];
    }
    size_t count=std::count_if(sectorMist.begin(),sectorMist.end(),[](const auto &m){return m.pit||m.liquid||m.toxic;});
    fprintf(stderr,"3D mist: %zu candidate sectors; derived from floor heights, flats and specials.\n",count);
}
static void buildSurfaceMaps(float minx,float miny,float maxx,float maxy) {
    minx-=64;miny-=64;maxx+=64;maxy+=64;
    mapCell=std::max(4.0f,std::ceil(std::max(maxx-minx,maxy-miny)/1024.0f));
    mapOrigin[0]=minx;mapOrigin[1]=miny;
    mapWidth=(int)std::ceil((maxx-minx)/mapCell);mapHeight=(int)std::ceil((maxy-miny)/mapCell);
    size_t count=(size_t)mapWidth*mapHeight;
    seamCells.assign(count,{});
    std::vector<float> floorDistance(count,contactRange),ceilingDistance(count,contactRange),seamDistance(count,seamBand);
    auto center=[&](int x,int y){return Point{mapOrigin[0]+(x+0.5f)*mapCell,mapOrigin[1]+(y+0.5f)*mapCell};};
    // Cell sectors come from the convex BSP floor polygons; the rest is void.
    for(int leaf=0;leaf<numsubsectors;++leaf) {
        const auto &poly=floors[leaf];if(poly.size()<3)continue;
        const auto &b=floorBounds[leaf];
        int x0=std::max(0,(int)std::floor((b[0]-minx)/mapCell)),x1=std::min(mapWidth-1,(int)std::floor((b[2]-minx)/mapCell));
        int y0=std::max(0,(int)std::floor((b[1]-miny)/mapCell)),y1=std::min(mapHeight-1,(int)std::floor((b[3]-miny)/mapCell));
        uint16_t sector=(uint16_t)(subsectors[leaf].sector-sectors);
        for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x) {
            Point p=center(x,y);bool positive=false,negative=false;
            for(size_t n=0;n<poly.size();++n) {
                Point a=poly[n],c=poly[(n+1)%poly.size()];
                float cross=(c.x-a.x)*(p.y-a.y)-(c.y-a.y)*(p.x-a.x);
                positive|=cross>0.001f;negative|=cross<-0.001f;
            }
            if(!(positive&&negative))seamCells[(size_t)y*mapWidth+x].own=sector;
        }
    }
    for(int i=0;i<numlines;++i) {
        const line_t &line=lines[i];
        float ax=units(line.v1->x),ay=units(line.v1->y),bx=units(line.v2->x),by=units(line.v2->y);
        float ex=bx-ax,ey=by-ay,length2=ex*ex+ey*ey;if(length2<0.01f)continue;
        const sector_t *front=line.frontsector,*back=line.backsector;
        int f=front?(int)(front-sectors):-1,b=back?(int)(back-sectors):-1;
        if(f==b)continue;
        float ff=front?units(front->floorheight):0,fc=front?units(front->ceilingheight):0;
        float bf=back?units(back->floorheight):0,bc=back?units(back->ceilingheight):0;
        // Floors blend light only between walkable neighbors, so tall steps
        // stay crisp; ceilings wherever they run on nearly flush, as over a
        // raised lift whose sector is dimmer than the room.
        bool floorSeam=front&&back&&std::abs(ff-bf)<=24,ceilingSeam=front&&back&&std::abs(fc-bc)<=24;
        bool seam=floorSeam||ceilingSeam;
        int x0=std::max(0,(int)std::floor((std::min(ax,bx)-contactRange-minx)/mapCell));
        int x1=std::min(mapWidth-1,(int)std::floor((std::max(ax,bx)+contactRange-minx)/mapCell));
        int y0=std::max(0,(int)std::floor((std::min(ay,by)-contactRange-miny)/mapCell));
        int y1=std::min(mapHeight-1,(int)std::floor((std::max(ay,by)+contactRange-miny)/mapCell));
        for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x) {
            size_t index=(size_t)y*mapWidth+x;auto &cell=seamCells[index];
            if(cell.own==noSector)continue;
            Point p=center(x,y);
            float t=std::clamp(((p.x-ax)*ex+(p.y-ay)*ey)/length2,0.0f,1.0f);
            float d=std::hypot(p.x-ax-ex*t,p.y-ay-ey*t);
            if(d>=contactRange)continue;
            int s=cell.own;bool isFront=s==f,isBack=s==b;
            bool floorWall=!back||(isFront&&bf>=ff+24)||(isBack&&ff>=bf+24);
            bool ceilingWall=!back||(isFront&&bc<=fc-24)||(isBack&&fc<=bc-24);
            if(floorWall)floorDistance[index]=std::min(floorDistance[index],d);
            // Lower steps (stair risers, ledges) shade the floor at their foot
            // over three times their height, at least 24 units.
            float rise=!back?0:isFront?bf-ff:isBack?ff-bf:0;
            if(!floorWall&&rise>=4)floorDistance[index]=std::min(floorDistance[index],d*contactRange/std::clamp(3*rise,24.0f,contactRange));
            if(ceilingWall)ceilingDistance[index]=std::min(ceilingDistance[index],d);
            if(seam&&(isFront||isBack)&&d<seamDistance[index]) {
                seamDistance[index]=d;cell.neighbor=(uint16_t)(isFront?b:f);
                int weight=(int)std::lround((1-d/seamBand)*255);
                cell.weight=(uint16_t)((floorSeam?weight:0)|(ceilingSeam?weight<<8:0));
            }
        }
    }
    contactCells.assign(count*4,0);
    for(size_t i=0;i<count;++i) {
        bool solid=seamCells[i].own==noSector;
        contactCells[4*i]=solid?0:(uint8_t)std::lround(floorDistance[i]/contactRange*255);
        contactCells[4*i+1]=solid?0:(uint8_t)std::lround(ceilingDistance[i]/contactRange*255);
    }
    seamTexture=gpuCreateTexture(GpuFormat::RGBA16Uint,mapWidth,mapHeight,seamCells.data(),mapWidth*8);
    contactTexture=gpuCreateTexture(GpuFormat::RGBA8,mapWidth,mapHeight,contactCells.data(),mapWidth*4);
    if(!seamTexture||!contactTexture)I_Error((char*)"Could not allocate surface maps");
    fprintf(stderr,"3D surface maps: %dx%d cells of %.0f units.\n",mapWidth,mapHeight,mapCell);
}
void buildMap() {
    floors.assign(numsubsectors,{});
    float minx=1e9,miny=1e9,maxx=-1e9,maxy=-1e9;
    for(int i=0;i<numvertexes;++i) {
        float x=units(vertexes[i].x),y=units(vertexes[i].y);
        minx=std::min(minx,x);miny=std::min(miny,y);maxx=std::max(maxx,x);maxy=std::max(maxy,y);
    }
    std::vector<Point> bounds={{minx-128,miny-128},{maxx+128,miny-128},{maxx+128,maxy+128},{minx-128,maxy+128}};
    partition(numnodes?unsigned(numnodes-1):unsigned(NF_SUBSECTOR),bounds);
    weldFloors();
    sectorFloors.assign(numsectors,{});
    for(int i=0;i<numsubsectors;++i)
        sectorFloors[subsectors[i].sector-sectors].push_back(i);
    floorBounds.assign(numsubsectors,{1e9f,1e9f,-1e9f,-1e9f});
    for(int i=0;i<numsubsectors;++i) for(Point p:floors[i]) {
        auto &b=floorBounds[i];b[0]=std::min(b[0],p.x);b[1]=std::min(b[1],p.y);
        b[2]=std::max(b[2],p.x);b[3]=std::max(b[3],p.y);
    }
    classifySectorMist();
    buildSurfaceMaps(minx,miny,maxx,maxy);
    size_t triangles=0; for(auto &p:floors) if(p.size()>2) triangles+=p.size()-2;
    fprintf(stderr,"3D map: %d subsectors, %zu floor triangles, %d lines.\n",numsubsectors,triangles,numlines);
}
float sectorHeight(const sector_t *sector,bool ceiling) {
    float now=units(ceiling?sector->ceilingheight:sector->floorheight);
    size_t index=sector-sectors;
    if(index>=oldHeights.size())return now;
    float old=oldHeights[index][ceiling];
    return old+(now-old)*renderFraction;
}
const game_mood &levelMood() {
    static game_mood mood={1,1,0,0,-1,1,{1,1,1}};static unsigned serial=~0u;static int authored=-1;
    if(serial!=r_levelserial||authored!=settings.authored) {
        serial=r_levelserial;authored=settings.authored;
        mood={1,1,0,0,-1,1,{1,1,1}};
        if(authored)Game_Mood(gameepisode,gamemap,&mood);
    }
    return mood;
}
float lighting(const sector_t *sector,bool bright) {
    if(bright) return 1;
    float level=lampLightLevel(sector)/255.0f;
    if(bakedLightsActive&&lampDrop.size()==(size_t)numsectors)level-=lampDrop[sector-sectors];
    // The episode's mood (Game_Mood) can darken the outdoors.
    if(sector->ceilingpic==skyflatnum)level*=levelMood().outdoor;
    level=std::clamp(level,0.12f,1.0f);
    size_t index=sector-sectors;
    if(index<shotDim.size())level*=shotDim[index]; // Rooms whose lamps were shot out (shot_lights.cpp).
    return level;
}
void mapShutdown() {
    floors.clear();floorBounds.clear();sectorFloors.clear();seamCells.clear();contactCells.clear();sectorMist.clear();
    seamTexture=nullptr;contactTexture=nullptr;
}
} // namespace doom3d
