/* Static lights baked at level load: lamps, glowing pools and window
   light, and the grid of lights that reach each map cell. */
#include "internal.h"

namespace doom3d {
int lightBakeKey=-1;
unsigned lightBakeSerial=0;
bool bakedLightsActive=false; // This frame's lights rely on the bake.

std::vector<float> lampDrop;
bool bakeOnlyActive=false; // ...and static lights come only from it.

std::vector<BakeLight> bakeSources;
std::vector<SurfaceLightKey> bakeSourceKeys;
int firstWindowSource=0; // Window lights (addWindowLights) end bakeSources; they cast no thing shadows.
// Window light reaching one point eases toward about the outdoor level, so
// a room with windows on several sides isn't lit past it by their sum. Scales
// rgb (by its luminance) and returns the factor.
float capWindowLight(float rgb[3]) {
    constexpr float cap=1.0f;
    float l=0.299f*rgb[0]+0.587f*rgb[1]+0.114f*rgb[2];
    if(l<=0)return 1;
    float scale=cap*(1-std::exp(-l/cap))/l;
    for(int c=0;c<3;++c)rgb[c]*=scale;
    return scale;
}
constexpr float bakeGridCell=128;
static int bakeGridWidth=0,bakeGridHeight=0;
std::vector<std::vector<int>> bakeGrid;

std::vector<BakeArea> bakeAreas;
std::vector<uint8_t> sourceGroups;
std::vector<uint8_t> wallDirectionCells,flatDirectionCells;
const std::vector<int> &bakeGridLights(float x,float y) {
    int gx=std::clamp((int)std::floor((x-mapOrigin[0])/bakeGridCell),0,bakeGridWidth-1);
    int gy=std::clamp((int)std::floor((y-mapOrigin[1])/bakeGridCell),0,bakeGridHeight-1);
    return bakeGrid[(size_t)gy*bakeGridWidth+gx];
}

// The light bake's inputs: 1 emissive textures, 2 bake-only lights.
int lightBakeInputs() {return (settings.emissive?1:0)|(settings.bakeOnlyLights?2:0)|(settings.windowLight?4:0)|(settings.dimLitRooms?8:0)|
    (settings.windowLight?(int)std::lround(settings.windowStrength*4)<<4:0)|
    (settings.dimLitRooms?(int)std::lround(settings.unlitDarkening*20)<<8:0)|(settings.windowLight&&settings.sun?1<<14:0);}
// Bake-only lights: glowing liquid floors and ceilings (lava, nukage, slime,
// magma, or flats that mostly glow) light as pools; lamps and panels stay
// point lights.
static bool poolSurface(int sector,bool ceiling) {
    int pic=ceiling?sectors[sector].ceilingpic:sectors[sector].floorpic;
    if(pic==skyflatnum)return false;
    int lump=firstflat+flattranslation[pic];
    const Image &image=lumpImage(lump,true);
    if(image.emissionWeight<=0)return false;
    char name[9];memcpy(name,lumpinfo[lump].name,8);name[8]=0;
    int kind=Game_GlowKind(name,1);
    return kind==DOOM_EMISSIVE_LAVA||kind==DOOM_EMISSIVE_SLIME||kind==DOOM_EMISSIVE_MAGMA||image.emissionCoverage>=0.4f;
}
// One area light per pool: neighboring sectors whose floor (or ceiling)
// glows in the same flat at the same height join. Strength and reach grow
// gently with the pool's size, like merged tile lights did. The 0.68 keeps
// the total near what the tile lights gave (E1M1: Doom +12%, Freedoom -10%),
// as the glow now spills further over rims.
static void buildPoolAreas() {
    bakeAreas.clear();
    std::vector<float> surface,weight; // Per area: size, and emission weight times size.
    for(int ceiling=0;ceiling<2;++ceiling) {
        std::vector<int> parent(numsectors);
        for(int i=0;i<numsectors;++i)parent[i]=i;
        std::function<int(int)> root=[&](int i) {return parent[i]==i?i:parent[i]=root(parent[i]);};
        std::vector<char> pool(numsectors);
        for(int i=0;i<numsectors;++i)pool[i]=poolSurface(i,ceiling);
        for(int i=0;i<numlines;++i) {
            const line_t &line=lines[i];
            if(!line.frontsector||!line.backsector)continue;
            int a=(int)(line.frontsector-sectors),b=(int)(line.backsector-sectors);
            if(!pool[a]||!pool[b])continue;
            const sector_t &sa=sectors[a],&sb=sectors[b];
            bool same=ceiling?sa.ceilingpic==sb.ceilingpic&&sa.ceilingheight==sb.ceilingheight
                             :sa.floorpic==sb.floorpic&&sa.floorheight==sb.floorheight;
            if(same)parent[root(a)]=root(b);
        }
        std::map<int,size_t> areaOf;
        for(int i=0;i<numsectors;++i) {
            if(!pool[i])continue;
            auto found=areaOf.find(root(i));
            if(found==areaOf.end()) {
                found=areaOf.emplace(root(i),bakeAreas.size()).first;
                BakeArea area;area.ceiling=ceiling;
                area.z=ceiling?bakeMap.sectors[i].ceiling:bakeMap.sectors[i].floor;
                area.clearance=1e9f;
                area.color[0]=area.color[1]=area.color[2]=0;
                bakeAreas.push_back(area);surface.push_back(0);weight.push_back(0);
            }
            size_t index=found->second;BakeArea &area=bakeAreas[index];
            // Rays test against a point up to 48 units off the surface, inside the room.
            area.clearance=std::min(area.clearance,std::clamp(bakeMap.sectors[i].ceiling-bakeMap.sectors[i].floor-2,0.0f,48.0f));
            const Image &image=lumpImage(firstflat+flattranslation[ceiling?sectors[i].ceilingpic:sectors[i].floorpic],true);
            for(int leaf:sectorFloors[i]) {
                const auto &poly=floors[leaf];if(poly.size()<3)continue;
                std::vector<std::array<float,2>> piece;float twice=0;
                for(size_t n=0;n<poly.size();++n) {
                    const Point &p=poly[n],&q=poly[(n+1)%poly.size()];
                    piece.push_back({p.x,p.y});twice+=p.x*q.y-q.x*p.y;
                    area.low[0]=std::min(area.low[0],p.x);area.low[1]=std::min(area.low[1],p.y);
                    area.high[0]=std::max(area.high[0],p.x);area.high[1]=std::max(area.high[1],p.y);
                }
                float size=std::fabs(twice)*0.5f;
                for(int c=0;c<3;++c)area.color[c]+=image.emissionColor[c]*size;
                surface[index]+=size;weight[index]+=image.emissionWeight*materialEmissionGain(image)*size;
                area.pieces.push_back(std::move(piece));
            }
        }
        for(size_t n=0;n<bakeAreas.size();++n) {
            BakeArea &area=bakeAreas[n];
            if(area.ceiling!=(bool)ceiling||surface[n]<=0)continue;
            for(float &c:area.color)c/=surface[n];
            float tiles=surface[n]/4096;
            area.strength=0.68f*1.65f*weight[n]/surface[n]*std::clamp(0.6f+0.2f*std::sqrt(tiles),0.75f,1.5f);
            area.radius=256+std::clamp(std::sqrt(surface[n])*0.125f,0.0f,96.0f);
        }
    }
    bakeAreas.erase(std::remove_if(bakeAreas.begin(),bakeAreas.end(),[](const BakeArea &a){return a.pieces.empty();}),bakeAreas.end());
}
// Direction texel (bake-only lights, or lamps that switch): where the static light comes from in the
// surface's tangent frame (rg; walls: along the side and up, floors and
// ceilings: map x and y), how much of it comes from that one direction (b),
// and the strongest flicker group with its share in 1/15 steps
// (a: group*16+share). Without a flicker group the low nibble holds the
// share of light from a pool the surface lies in (its sheen; see world.frag).
// Keep the frame equivalent to world.frag.
static void encodeDirection(const float normal[3],const float towards[3],float total,const float groups[],float sheen,uint8_t *out) {
    out[0]=out[1]=128;out[2]=out[3]=0;
    float length=std::sqrt(towards[0]*towards[0]+towards[1]*towards[1]+towards[2]*towards[2]);
    if(total<=0||length<=1e-6f)return;
    bool flat=std::fabs(normal[2])>0.5f;
    float t[3]={flat?1.0f:-normal[1],flat?0.0f:normal[0],0},b[3]={0,flat?1.0f:0.0f,flat?0.0f:1.0f};
    float x=(towards[0]*t[0]+towards[1]*t[1]+towards[2]*t[2])/length,y=(towards[0]*b[0]+towards[1]*b[1]+towards[2]*b[2])/length;
    out[0]=(uint8_t)std::lround(std::clamp(x*0.5f+0.5f,0.0f,1.0f)*255);
    out[1]=(uint8_t)std::lround(std::clamp(y*0.5f+0.5f,0.0f,1.0f)*255);
    out[2]=(uint8_t)std::lround(std::clamp(length/total,0.0f,1.0f)*255);
    int best=0;
    for(int g=1;g<flickerGroups;++g)if(groups[g]>0&&groups[g]>(best?groups[best]:0))best=g;
    if(best)out[3]=(uint8_t)(best*16+std::lround(std::clamp(groups[best]/total,0.0f,1.0f)*15));
    else out[3]=(uint8_t)std::lround(std::clamp(sheen/total,0.0f,1.0f)*15);
}
// The static light at a surface point, unflickered, into out (rgb at half
// scale, in 1/32 steps) and, if given, its bake-only direction texel.
static void gatherStatic(const BakeMap &map,int sector,float x,float y,float z,const float normal[3],uint8_t *out,uint8_t *direction) {
    int points=(int)bakeSources.size();
    float sum[3]={},towards[3]={},total=0,groups[flickerGroups]={},sheen=0;
    float window[3]={},windowTowards[3]={},windowTotal=0;
    for(int n:bakeGridLights(x,y)) {
        float to[3];bool inside=false;
        const float *color=n<points?bakeSources[n].color:bakeAreas[n-points].color;
        bool isWindow=n>=firstWindowSource&&n<points;
        float amount=n<points?addBakedLight(map,bakeSources[n],sector,x,y,z,normal,isWindow?window:sum,to)
                             :addBakedArea(map,bakeAreas[n-points],sector,x,y,z,normal,sum,to,&inside);
        if(!direction||amount<=0)continue;
        float weight=amount*luminance(color);
        if(isWindow) {for(int c=0;c<3;++c)windowTowards[c]+=to[c]*weight;windowTotal+=weight;continue;}
        for(int c=0;c<3;++c)towards[c]+=to[c]*weight;
        total+=weight;
        if(n<points)groups[sourceGroups[n]]+=weight;
        // The pool's own surface (facing the way the pool glows).
        else if(inside&&normal[2]*(bakeAreas[n-points].ceiling?-1:1)>0.5f)sheen+=weight;
    }
    addGlassLight(map,sector,x,y,z,normal,sum);
    float scale=capWindowLight(window);
    for(int c=0;c<3;++c) {sum[c]+=window[c];towards[c]+=windowTowards[c]*scale;}
    total+=windowTotal*scale;
    for(int c=0;c<3;++c)out[c]=(uint8_t)std::min(255L,std::lround(std::round(sum[c]*32)/32*0.5f*255));
    if(direction)encodeDirection(normal,towards,total,groups,sheen,direction);
}
// Static light for a floor/ceiling map cell (only: with directions and flicker groups).
void lightCell(const BakeMap &map,size_t i,bool only) {
    static const float up[3]={0,0,1},down[3]={0,0,-1};
    int sector=seamCells[i].own;
    if(sector==noSector)return;
    float px=mapOrigin[0]+(i%mapWidth+0.5f)*mapCell,py=mapOrigin[1]+(i/mapWidth+0.5f)*mapCell;
    const BakeSector &s=map.sectors[sector];
    size_t layer=(size_t)mapWidth*mapHeight;
    if(sectors[sector].floorpic!=skyflatnum)
        gatherStatic(map,sector,px,py,s.floor+1,up,flatCell(floorLight,i),only?&flatDirectionCells[4*i]:nullptr);
    if(sectors[sector].ceilingpic!=skyflatnum)
        gatherStatic(map,sector,px,py,s.ceiling-1,down,flatCell(ceilingLight,i),only?&flatDirectionCells[4*(layer+i)]:nullptr);
}
// Static light for a wall strip's atlas texels.
void lightStrip(const BakeMap &map,const BakeStrip &strip,bool only) {
    const float normal[3]={strip.dy,-strip.dx,0};
    forStripTexels(strip,[&](size_t i,float x,float y,float z) {
        size_t texel=i/atlasStride*atlasWidth+i%atlasStride;
        gatherStatic(map,strip.sector,x,y,z,normal,&wallBakeCells[4*i],only?&wallDirectionCells[4*texel]:nullptr);
    });
}
// A window frame at night (Game_Mood shineOut): a thin indoor sector (at
// most 64 units across) beside the sky, as bright as it and brighter than
// every indoor sector beside it, has the outdoor level from id (E1M1's
// FLAT14 room has its frames at 255 in a 224 room). Its room, the brightest
// of those, is what shines through it; any other sector is its own room.
static const sector_t *nightRoom(const sector_t *sector) {
    if(!levelMood().shineOut||sector->ceilingpic==skyflatnum)return sector;
    int sky=-1;const sector_t *room=nullptr;
    float x0=INFINITY,y0=INFINITY,x1=-INFINITY,y1=-INFINITY;
    for(int n=0;n<sector->linecount;++n) {
        const line_t *side=sector->lines[n];
        for(const vertex_t *v:{side->v1,side->v2}) {
            x0=std::min(x0,units(v->x));x1=std::max(x1,units(v->x));
            y0=std::min(y0,units(v->y));y1=std::max(y1,units(v->y));
        }
        const sector_t *other=side->frontsector==sector?side->backsector:side->frontsector;
        if(!other||other==sector)continue;
        if(other->ceilingpic==skyflatnum)sky=std::max(sky,(int)other->lightlevel);
        else if(!room||other->lightlevel>room->lightlevel)room=other;
    }
    bool frame=sky>=0&&room&&sector->lightlevel>=sky&&sector->lightlevel>room->lightlevel&&std::min(x1-x0,y1-y0)<=64;
    return frame?room:sector;
}
// A sector's light level (given as level) at night: a window frame's is its
// room's, with the outdoors dark.
static int nightFrameLevel(const sector_t *sector,int level) {
    const sector_t *room=nightRoom(sector);
    return room==sector?level:room->lightlevel;
}
// Lamp-lit sectors: Doom maps fake a lamp's glow with a brighter sector under
// and around it (E1M1's monitor boxes stand at 255 in a 128 room). With
// baked lights that glow is drawn by the lamps themselves, falling off
// smoothly, so the sector's own extra brightness would count it twice and
// show as a hard-edged bright patch. Touching sectors at the same level are
// one region and drop together: beside a dimmer sector, without sky, by the
// baked light every cell of its floors gets (its dimmest cell), but not below
// its brightest dimmer neighbor; dropped regions pass the level on inward. A
// bright room its lamps do not fully light keeps its level, glowing pools in
// it included (E1M1's nukage room), or they would leave the rest of the room
// as a bright island.
// Darker rooms around lamps and windows (settings.dimLitRooms): a region also
// drops by the static light its floors get on average (in full: a bright
// room shows little of it until it drops), at most 60% of its level, so the
// light visibly comes from its lamps, screens and windows and far corners
// fall darker, but no further than leaves its least-lit floor point 60% of
// the level (E1M1 and E1M2 had 6% of their indoor floors under half their
// level). Every region drops by at least settings.unlitDarkening of its
// level, or a room nothing visibly lights would keep its full even light and
// look brighter and flatter than the lit rooms around it (E1M3's sector 98).
// This needs no dimmer neighbor; only regions mostly open to the sky keep
// their level. Sectors whose lamps flicker count at their low, and only
// steady light dims a region: with the lamps out, a room is as Doom has it.
// When the episode's mood turns window light round (Game_Mood shineOut),
// rooms opening onto the sky dim like any other: the night outside no
// longer lights them, so only their own lamps do. Their window frames,
// which id gave the outdoor level, take their room's (nightFrameLevel).
static void classifyLampSectors() {
    lampDrop.assign(numsectors,0.0f);
    if(flatLightCells.empty()||seamCells.empty())return;
    std::vector<float> least(numsectors,INFINITY),shown(numsectors,0.0f);
    std::vector<int> cells(numsectors,0);
    std::vector<float> level(numsectors);
    for(int i=0;i<numsectors;++i)level[i]=std::clamp(nightFrameLevel(&sectors[i],lampLightLevel(&sectors[i]))/255.0f,0.12f,1.0f);
    for(size_t i=0;i<seamCells.size();++i) {
        int s=seamCells[i].own;if(s==noSector)continue;
        const uint8_t *cell=flatCell(floorLight,i);
        // Only steady light counts: flickering lamps' share (their flicker
        // group, assignLampGroups) leaves when they go out.
        float steady=1;
        if(lampGroupCount>0&&!flatDirectionCells.empty()) {
            unsigned packed=flatDirectionCells[4*i+3],group=packed>>4;
            if(group>0&&(int)group<=lampGroupCount)steady=1-(packed&15)/15.0f;
        }
        least[s]=std::min(least[s],std::max({cell[0],cell[1],cell[2]})/255.0f*2*steady);
        float rgb[3]={cell[0]/255.0f*2*steady,cell[1]/255.0f*2*steady,cell[2]/255.0f*2*steady};
        shown[s]+=bakeLuminance(rgb);++cells[s];
    }
    bool dim=settings.dimLitRooms;
    auto neighbors=[&](int i,auto &&visit) {
        for(int n=0;n<sectors[i].linecount;++n) {
            const line_t *line=sectors[i].lines[n];
            const sector_t *other=line->frontsector==&sectors[i]?line->backsector:line->frontsector;
            if(other&&other!=&sectors[i])visit(int(other-sectors));
        }
    };
    // Regions: their members, how far their lamps drop them, and whether sky keeps them.
    std::vector<int> regionOf(numsectors,-1);
    std::vector<std::vector<int>> members;std::vector<float> regionLeast;std::vector<bool> fixed;
    for(int i=0;i<numsectors;++i) {
        if(regionOf[i]>=0)continue;
        int r=(int)members.size();members.push_back({i});regionOf[i]=r;
        float dimmest=INFINITY,sum=0;bool sky=false;int count=0,open=0;
        for(size_t k=0;k<members[r].size();++k) {
            int s=members[r][k];
            dimmest=std::min(dimmest,least[s]);sky|=sectors[s].ceilingpic==skyflatnum;
            sum+=shown[s];count+=cells[s];if(sectors[s].ceilingpic==skyflatnum)open+=cells[s];
            // Dimming keeps indoor sectors out of sky regions: a window
            // frame at the outdoor level would stay at it with the outdoors.
            neighbors(s,[&](int o){if(regionOf[o]<0&&std::abs(level[o]-level[s])<0.01f&&
                (!dim||(sectors[o].ceilingpic==skyflatnum)==(sectors[s].ceilingpic==skyflatnum))){regionOf[o]=r;members[r].push_back(o);}});
        }
        if(dim&&count>0) {
            // Its least-lit floor point keeps at least 60% of the level: the
            // drop goes no further than the static light that point gets.
            float least=dimmest==INFINITY?0.0f:dimmest;
            float lit=std::min({sum/count,0.6f*level[i],least+0.4f*level[i]});
            lit=std::max(lit,settings.unlitDarkening*level[i]);
            regionLeast.push_back(std::max(least,lit));fixed.push_back(open*2>count);
        } else {regionLeast.push_back(dimmest);fixed.push_back(sky||dimmest==INFINITY);}
    }
    std::vector<float> base=level;
    for(int pass=0;pass<16;++pass) {
        bool changed=false;
        for(size_t r=0;r<members.size();++r) {
            if(fixed[r])continue;
            float now=base[members[r][0]],to=-1;
            for(int s:members[r])neighbors(s,[&](int o){if(base[o]<now-0.01f)to=std::max(to,base[o]);});
            float lowered=std::max(to,level[members[r][0]]-regionLeast[r]);
            if((to>=0||dim)&&lowered<now-0.01f) {for(int s:members[r])base[s]=lowered;changed=true;}
        }
        if(!changed)break;
    }
    for(int i=0;i<numsectors;++i)lampDrop[i]=std::clamp(lampLightLevel(&sectors[i])/255.0f,0.12f,1.0f)-base[i];
}
// The sky's color: the upper rows of the sky texture, above any horizon art,
// halfway to white.
std::array<float,3> skyColor() {
    const Image &sky=wallImage(skytexture);
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    double sum[3]={};
    for(int y=0;y<std::max(1,sky.height*2/5);++y)for(int x=0;x<sky.width;++x) {
        size_t i=(size_t)y*sky.width+x;if(!sky.pixels[2*i+1])continue;
        for(int c=0;c<3;++c)sum[c]+=palette[sky.pixels[2*i]*3+c];
    }
    double peak=std::max({sum[0],sum[1],sum[2],1e-9});
    std::array<float,3> tint;
    for(int c=0;c<3;++c)tint[c]=(float)(0.5+0.5*sum[c]/peak);
    return tint;
}
// The sky's color at the horizon, as sky.frag draws its low rows: what
// distant outdoor surfaces fade into (Game_Sun haze).
std::array<float,3> skyHorizon() {
    const Image &sky=wallImage(skytexture);
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    double sum[3]={},count=0;
    for(int y=sky.height*88/100;y<std::max(sky.height*88/100+1,sky.height*96/100);++y)for(int x=0;x<sky.width;++x) {
        size_t i=(size_t)y*sky.width+x;if(!sky.pixels[2*i+1])continue;
        for(int c=0;c<3;++c)sum[c]+=palette[sky.pixels[2*i]*3+c]/255.0;
        ++count;
    }
    return {(float)(count?sum[0]/count:0.5),(float)(count?sum[1]/count:0.5),(float)(count?sum[2]/count:0.5)};
}
// Window light: every opening from an indoor sector into a sky sector
// (windows, doorways, the rim of a hole in the ceiling) is a soft lamp facing
// inward, built as glowing panels 2 units inside it, about 32 units wide and
// at most 64 tall. They shine at the sky sector's light level in the sky's
// color paled further, so the outdoors lights the rooms beside it as Doom
// shows it, not as its dark textures would bounce it. A panel's strength
// follows its area (and settings.windowStrength): a long window lights the
// floor beside it well past the outdoor level before the headroom eases it,
// a slot barely; light fades out over 448 units. With sun shadows, each
// 32-unit column is weighted by the sun on the outdoor floor 16-80 units
// outside it against its sunniest neighbors (below), so the sunny side of a
// room leads; capWindowLight keeps several windows from adding past the
// outdoor level.
// An episode whose mood turns window light round (Game_Mood shineOut) has
// the panels 2 units outside instead, facing out, shining by how much
// brighter the room is than the dark outdoors (three times that, as the
// night makes the eye see more of it), in the room's lamp color made
// bolder (nightRoomColors), reaching half as far and thrown straight out
// (beam 4: half as strong 33 degrees aside): the room's light falls in
// lanes out of its windows and doors onto the ground outside.
// A room's color at night, per sector: its lamps' color (the lights already
// in sources sitting in it, by strength; white with none), saturated 2.5
// times as much and scaled to a peak of 1. Not its flats: E1M1's blue
// carpet threw blue light out of the FLAT14 room.
static std::vector<std::array<float,3>> nightRoomColors(const std::vector<BakeLight> &sources) {
    std::vector<std::array<float,3>> lamp(numsectors,{0,0,0});
    for(const BakeLight &light:sources) {
        int s=int(R_PointInSubsector((fixed_t)(light.x*FRACUNIT),(fixed_t)(light.y*FRACUNIT))->sector-sectors);
        for(int c=0;c<3;++c)lamp[s][c]+=light.color[c]*light.strength;
    }
    std::vector<std::array<float,3>> colors(numsectors);
    for(int i=0;i<numsectors;++i) {
        float lit=std::max({lamp[i][0],lamp[i][1],lamp[i][2]});
        std::array<float,3> color;
        for(int c=0;c<3;++c)color[c]=lit>0?lamp[i][c]/lit:1.0f;
        float grey=0.299f*color[0]+0.587f*color[1]+0.114f*color[2];
        for(float &c:color)c=std::max(0.0f,grey+(c-grey)*2.5f);
        float peak=std::max({color[0],color[1],color[2]});
        for(float &c:color)c=peak>0?c/peak:1.0f;
        colors[i]=color;
    }
    return colors;
}
static void addWindowLights(std::vector<BakeLight> &sources) {
    bool sunny=settings.sun&&sunBaked&&sunLevel>0&&!contactCells.empty()&&!seamCells.empty();
    const game_mood &mood=levelMood();
    std::array<float,3> tint=skyColor();
    for(float &c:tint)c=0.5f+0.5f*c;
    float peak=std::max({tint[0],tint[1],tint[2]});
    for(float &c:tint)c/=peak;
    std::vector<std::array<float,3>> roomColors;
    if(mood.shineOut)roomColors=nightRoomColors(sources);
    struct Window { float x,y,nx,ny,low,height,strength,sunlit;int rows;std::array<float,3> color; };
    std::vector<Window> windows;
    for(int i=0;i<numlines;++i) {
        const line_t &line=lines[i];
        if(!line.frontsector||!line.backsector)continue;
        bool frontSky=line.frontsector->ceilingpic==skyflatnum,backSky=line.backsector->ceilingpic==skyflatnum;
        if(frontSky==backSky)continue;
        const sector_t *in=frontSky?line.backsector:line.frontsector,*out=frontSky?line.frontsector:line.backsector;
        float low=std::max(floorZ(in),floorZ(out)),high=std::min(ceilZ(in),ceilZ(out));
        if(high-low<8)continue;
        float ax=units(line.v1->x),ay=units(line.v1->y),dx=units(line.v2->x)-ax,dy=units(line.v2->y)-ay;
        float length=std::hypot(dx,dy);if(length<8)continue;
        dx/=length;dy/=length;
        // The front side lies to the right of v1->v2.
        float nx=in==line.frontsector?dy:-dy,ny=in==line.frontsector?-dx:dx;
        int columns=std::max(1,(int)std::lround(length/32)),rows=std::max(1,(int)std::ceil((high-low)/64));
        float width=length/columns,height=(high-low)/rows;
        float outdoor=std::clamp(out->lightlevel/255.0f*mood.outdoor,0.12f,1.0f),level=outdoor;
        std::array<float,3> color=tint;
        if(mood.shineOut) {
            // A window frame often has the outdoor level; it shines as its room.
            float room=nightFrameLevel(in,in->lightlevel)/255.0f;
            color=roomColors[nightRoom(in)-sectors];
            level=(std::clamp(room,0.12f,1.0f)-outdoor)*3;
            if(level<0.08f)continue; // A room no brighter than the night outside.
            nx=-nx;ny=-ny;
        }
        float strength=1.6f*settings.windowStrength*level*width/128*height/96;
        int outside=int(out-sectors);
        for(int column=0;column<columns;++column) {
            float along=(column+0.5f)*width,px=ax+dx*along,py=ay+dy*along;
            // How sunlit the ground just outside is (0-1; -1 unknown).
            float seen=0,count=0;
            if(sunny&&!mood.shineOut)for(float reach:{16.0f,40.0f,80.0f}) {
                int cx=(int)std::floor((px-nx*reach-mapOrigin[0])/mapCell),cy=(int)std::floor((py-ny*reach-mapOrigin[1])/mapCell);
                if(cx<0||cy<0||cx>=mapWidth||cy>=mapHeight)continue;
                size_t cell=(size_t)cy*mapWidth+cx;
                if(seamCells[cell].own!=outside)continue;
                seen+=contactCells[4*cell+2]/255.0f;++count;
            }
            windows.push_back({px,py,nx,ny,low,height,strength,count>0?seen/count:-1,rows,color});
        }
    }
    // A window onto a sunny courtyard takes over the room from those onto
    // shade: each column against the sunniest within its reach, 0.35 for
    // full shade beside full sun. Where all are alike (all shaded, or no sun)
    // they keep their strength, so a room's light still leans on its windows.
    int shining=0;
    for(const Window &w:windows) {
        float sunlit=1;
        if(w.sunlit>=0) {
            float best=w.sunlit;
            for(const Window &o:windows)
                if(o.sunlit>best&&(o.x-w.x)*(o.x-w.x)+(o.y-w.y)*(o.y-w.y)<448.0f*448)best=o.sunlit;
            if(best>0.05f)sunlit=0.35f+0.65f*w.sunlit/best;
        }
        if(mood.shineOut)++shining;
        for(int row=0;row<w.rows;++row)
            sources.push_back({w.x+w.nx*2,w.y+w.ny*2,w.low+(row+0.5f)*w.height,mood.shineOut?224.0f:448.0f,w.strength*sunlit,0.5f,
                               {w.color[0],w.color[1],w.color[2]},w.height*0.5f,w.nx,w.ny,mood.shineOut?4.0f:0.0f});
    }
    if(mood.shineOut)fprintf(stderr,"3D bake: %d window columns shine out into the night.\n",shining);
}
// A merged surface light as the bake keeps it.
static BakeLight surfaceSource(const SurfaceLight &light) {
    auto n=panelNormal(light);
    return {light.x,light.y,light.z,light.radius,light.strength,0.35f,{light.color[0],light.color[1],light.color[2]},
            std::min(light.extent,light.radius),n[0],n[1]};
}
// Whether the bake takes a glowing tile into a point light; with bake-only
// lights, pools light as areas instead.
static bool bakedPoint(const SurfaceLight &tile,bool only) {
    return !only||tile.key[0]!=0||!poolSurface(tile.key[1],tile.key[2]);
}
// Calls visit with each bake grid cell's list over x0..x1, y0..y1.
template<class Visit> static void forGridCells(float x0,float y0,float x1,float y1,Visit visit) {
    auto gridX=[&](float x){return std::clamp((int)std::floor((x-mapOrigin[0])/bakeGridCell),0,bakeGridWidth-1);};
    auto gridY=[&](float y){return std::clamp((int)std::floor((y-mapOrigin[1])/bakeGridCell),0,bakeGridHeight-1);};
    for(int y=gridY(y0);y<=gridY(y1);++y)for(int x=gridX(x0);x<=gridX(x1);++x)visit(bakeGrid[(size_t)y*bakeGridWidth+x]);
}
void remergeBakeSource(int n) {
    invalidateStaticSamples();
    BakeLight &source=bakeSources[n];
    const SurfaceLightKey &cell=bakeSourceKeys[n];
    bool only=lightBakeKey&2;
    std::vector<SurfaceLight> left;
    for(const auto &tile:surfaceLights)
        if(!tileShotOut(tile.key)&&bakedPoint(tile,only)&&surfaceCellOf(tile)==cell)left.push_back(tile);
    std::vector<SurfaceLight> merged=mergedSurfaceLights(left);
    if(merged.size()!=1) {source.strength=0;return;}
    source=surfaceSource(merged[0]);
    // Listed in order, as bakeLights lists them.
    forGridCells(source.x-source.radius,source.y-source.radius,source.x+source.radius,source.y+source.radius,[&](std::vector<int> &list) {
        auto at=std::lower_bound(list.begin(),list.end(),n);
        if(at==list.end()||*at!=n)list.insert(at,n);
    });
}
// Flicker groups for lamps in flickering sectors (lampOut): each such
// sector's merged lights take one of groups 1..lampGroupCount, which the
// bake records like a flickering decoration's (encodeDirection), so their
// light leaves with its baked shadows and steps when they switch off, in
// every mode, dimming as they fade (lampShine). Sectors share a group only
// 1024 units or more apart, where the one nearest the eye sets it
// (updateLampGroups). Decorations hash into the groups left (flickerGroup).
int lampGroupCount=0;
float lampGroupFlicker[flickerGroups];
struct LampGroupSector { int sector; float x,y; };
static std::vector<std::vector<LampGroupSector>> lampGroupSectors;
static std::vector<int> lampGroupOf; // Per sector; 0 for none.
static void assignLampGroups(const std::vector<SurfaceLight> &merged) {
    lampGroupCount=0;lampGroupSectors.assign(flickerGroups,{});lampGroupOf.assign(numsectors,0);
    std::map<int,std::array<float,3>> centers; // Per sector: x and y sums, count.
    for(const auto &light:merged)if(light.flickerSector>=0&&light.strength>0) {
        auto &c=centers[light.flickerSector];c[0]+=light.x;c[1]+=light.y;c[2]+=1;
    }
    for(const auto &[sector,c]:centers) {
        float x=c[0]/c[2],y=c[1]/c[2],farthest=-1;int pick=1;
        for(int g=1;g<flickerGroups;++g) {
            float nearest=INFINITY;
            for(const auto &o:lampGroupSectors[g])nearest=std::min(nearest,std::hypot(o.x-x,o.y-y));
            if(nearest>=1024) {pick=g;break;}
            if(nearest>farthest) {farthest=nearest;pick=g;}
        }
        lampGroupSectors[pick].push_back({sector,x,y});lampGroupOf[sector]=pick;
        lampGroupCount=std::max(lampGroupCount,pick);
    }
}
void updateLampGroups(const float eye[3]) {
    for(int g=0;g<flickerGroups;++g)lampGroupFlicker[g]=1;
    for(int g=1;g<=lampGroupCount&&g<(int)lampGroupSectors.size();++g) {
        const LampGroupSector *nearest=nullptr;float best=INFINITY;
        for(const auto &o:lampGroupSectors[g]) {
            float d=std::hypot(o.x-eye[0],o.y-eye[1]);
            if(d<best) {best=d;nearest=&o;}
        }
        if(nearest)lampGroupFlicker[g]=lampShine(nearest->sector);
    }
}
bool lampGroupSector(int sector) {return sector>=0&&sector<(int)lampGroupOf.size()&&lampGroupOf[sector]>0;}
// Static lights, baked into the floor/ceiling map and the wall atlas rgb:
// fullbright decorations and (with emissive textures) merged surface lights,
// unflickered, with the sector heights in bakeMap. Values step by 1/32, like the
// 32 light rows of COLORMAP. Bake-only lights also light liquid pools as
// areas and record each texel's light direction and flicker group.
void bakeLights() {
    invalidateStaticSamples();
    loadingStage(LoadStage::Lights);
    finishRelight();
    lightBakeKey=lightBakeInputs();++lightBakeSerial;
    bool only=lightBakeKey&2;
    if(flatLightCells.empty())return;
    uint64_t start=SDL_GetTicksNS();
    std::vector<BakeLight> &sources=bakeSources;sources.clear();sourceGroups.clear();bakeAreas.clear();bakeSourceKeys.clear();
    std::vector<SurfaceLight> merged;
    if(settings.emissive) {
        if(only)buildPoolAreas();
        std::vector<SurfaceLight> points;
        for(const auto &light:surfaceLights)
            if(bakedPoint(light,only))points.push_back(light);
        merged=mergedSurfaceLights(points);
    }
    assignLampGroups(merged); // Before decorations take the groups left.
    findGlassWindows();
    for(int i=0;i<numsectors;++i)for(mobj_t *thing=sectors[i].thinglist;thing;thing=thing->snext)
        if(decorationLight(*thing)) {sources.push_back(decorationSource(*thing));sourceGroups.push_back((uint8_t)flickerGroup(*thing));}
    for(const auto &light:merged) {
        sources.push_back(surfaceSource(light));
        sourceGroups.push_back((uint8_t)(light.flickerSector>=0?lampGroupOf[light.flickerSector]:0));
        bakeSourceKeys.resize(sources.size()-1);bakeSourceKeys.push_back(light.key);
    }
    // Directions (and flicker groups) with bake-only lights, or for lamps that switch.
    bool directed=only||lampGroupCount>0;
    firstWindowSource=(int)sources.size();
    if(settings.windowLight) {addWindowLights(sources);sourceGroups.resize(sources.size(),0);}
    bakeSourceKeys.resize(sources.size());
    int points=(int)sources.size();
    bakeGridWidth=(int)std::ceil(mapWidth*mapCell/bakeGridCell);bakeGridHeight=(int)std::ceil(mapHeight*mapCell/bakeGridCell);
    bakeGrid.assign((size_t)bakeGridWidth*bakeGridHeight,{});
    auto list=[&](int n,float x0,float y0,float x1,float y1) {
        forGridCells(x0,y0,x1,y1,[&](std::vector<int> &cell){cell.push_back(n);});
    };
    for(int n=0;n<points;++n) {
        const BakeLight &light=sources[n];
        list(n,light.x-light.radius,light.y-light.radius,light.x+light.radius,light.y+light.radius);
    }
    for(size_t n=0;n<bakeAreas.size();++n) {
        const BakeArea &area=bakeAreas[n];
        list(points+(int)n,area.low[0]-area.radius,area.low[1]-area.radius,area.high[0]+area.radius,area.high[1]+area.radius);
    }
    if(directed) {
        wallDirectionCells.assign((size_t)atlasWidth*atlasHeight*4,0);
        flatDirectionCells.assign((size_t)mapWidth*mapHeight*2*4,0);
    } else {wallDirectionCells.clear();flatDirectionCells.clear();}
    parallelFor(mapHeight,[&](int y) {for(int x=0;x<mapWidth;++x)lightCell(bakeMap,(size_t)y*mapWidth+x,directed);});
    parallelFor((int)bakeOrder.size(),[&](int n) {lightStrip(bakeMap,bakeStrips[bakeOrder[n]],directed);});
    uploadFlatLight();
    uploadWallBake();
    wallDirectionTexture=nullptr;flatDirectionTexture=nullptr;
    if(directed) {
        wallDirectionTexture=gpuCreateTexture(GpuFormat::RGBA8,atlasWidth,atlasHeight,wallDirectionCells.data(),atlasWidth*4);
        flatDirectionTexture=gpuCreateTexture(GpuFormat::RGBA8,mapWidth,mapHeight*2,flatDirectionCells.data(),mapWidth*4);
        if(!wallDirectionTexture||!flatDirectionTexture)I_Error((char*)"Could not allocate surface maps");
    }
    classifyLampSectors();
    fprintf(stderr,"3D lights: %zu static lights (%zu window panels)%s baked in %.0f ms.\n",sources.size(),sources.size()-firstWindowSource,
        only?(", "+std::to_string(bakeAreas.size())+" pools, bake-only").c_str():"",(SDL_GetTicksNS()-start)/1e6);
}
void lightBakeNewLevel() {
    lightBakeKey=-1;
    bakeSources.clear();bakeSourceKeys.clear();bakeGrid.clear();bakeAreas.clear();sourceGroups.clear();lampDrop.clear();
    lampGroupCount=0;lampGroupSectors.clear();lampGroupOf.clear();
    wallDirectionCells.clear();flatDirectionCells.clear();wallDirectionTexture=nullptr;flatDirectionTexture=nullptr;
}
void lightBakeShutdown() {lightBakeNewLevel();bakedLightsActive=false;bakeOnlyActive=false;}
} // namespace doom3d
