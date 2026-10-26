/* Shootable lights (settings.shootableLights): sparks off machinery, and
   lamps and screens shot out. Render-only, like the window glass: monsters
   see as before, and a new level or a loaded game brings the lights back. */
#include "internal.h"
extern "C" {
#include "p_solid.h"
#include "../audio/impact_synth.h"
#include "../audio/sound_options.h"
}

namespace doom3d {
// A fixture breaks as one: every glowing tile of the surface hit (its
// joined planes, or its wall piece), and nothing else; the merged lights of
// the bake that it shared with other fixtures keep the tiles still lit
// (remergeBakeSource). Tiles are SurfaceLight keys; each remembers when it
// broke (game seconds) and where it stood. Pieces too small to hold a tile
// (wholePiece, wholePlane) break as a whole.
struct BrokenTile { float born,x,y,z; };
static std::map<SurfaceLightKey,BrokenTile> brokenTiles;
// Pieces holding broken tiles: {0, line*2+side, span} walls, {1, sector, ceiling} flats.
static std::set<std::array<int,3>> brokenPieces;
constexpr float sputterSeconds=0.6f;
static std::array<int,3> pieceOf(const SurfaceLightKey &key) {
    if(key[0]==3)return {0,key[1]*2+key[2],key[3]};
    return key[0]==1?std::array<int,3>{0,key[1]*2+key[2],key[5]}:std::array<int,3>{1,key[1],key[2]};
}
// A whole wall piece going dark, for pieces with no tile of their own: the
// narrow end of E1M1's hanging computer boxes is shorter than its texture.
static SurfaceLightKey wholePiece(int line,int side,int span) {return {3,line,side,span,0,0};}
// For a moment after the shot the tile sputters, flickering back on for a
// tic or two, less often as it dies; every tile of a fixture alike.
bool tileDark(const SurfaceLightKey &key) {
    if(brokenTiles.empty())return false;
    auto found=brokenTiles.find(key);
    if(found==brokenTiles.end())return false;
    float age=gameSeconds()-found->second.born;
    float duration=settings.environmentEffects&&settings.electricalDamage?3.f:sputterSeconds;
    if(age<0||age>=duration)return true;
    int tic=(int)(age*TICRATE);
    if(tic<1)return true;
    uint32_t seed=(uint32_t)std::lround(found->second.born*TICRATE);
    return hashUnit(tic/2,0,seed)>=0.6f*(1-age/duration);
}
bool tileShotOut(const SurfaceLightKey &key) {return brokenTiles.count(key)>0;}
// The texture row offset a wall section is drawn from, as wallGeometry sets it.
static float wallAnchor(const line_t &line,int side,int section) {
    const side_t &s=sides[line.sidenum[side]];
    const sector_t *front=s.sector,*back=side?line.frontsector:line.backsector;
    float floor=floorZ(front),ceil=ceilZ(front),row=units(s.rowoffset);
    if(section==0)return ((s.midtexture&&(line.flags&ML_DONTPEGBOTTOM))?floor+units(textureheight[s.midtexture]):ceil)+row;
    if(section==1)return ((line.flags&ML_DONTPEGTOP)?ceil:ceilZ(back)+(s.toptexture?units(textureheight[s.toptexture]):0))+row;
    return ((line.flags&ML_DONTPEGBOTTOM)?ceil:floorZ(back))+row;
}
// Whether the shot found the glow: a glowing texel within reach of (u, v)
// (bullets land a pixel or so off, and a thin tube should still break;
// fireballs are wider), or, on a panel, anywhere on a screen's glass, as
// most of a screen's picture is too dim to glow.
static bool shotGlow(const Image &image,float u,float v,bool panel,int reach) {
    if(image.emission.empty())return false;
    int w=image.width,h=image.height,x=(int)std::floor(u),y=(int)std::floor(v);
    auto at=[&](int dx,int dy){return (size_t)(((y+dy)%h+h)%h)*w+((x+dx)%w+w)%w;};
    if(panel&&!image.screens.empty()&&image.screens[at(0,0)]!=DOOM_GLASS_NONE)return true;
    for(int dy=-reach;dy<=reach;++dy)for(int dx=-reach;dx<=reach;++dx)if(image.emission[at(dx,dy)]>=64)return true;
    return false;
}
// Sparks fly off the surface along normal, up a little and falling:
// fullbright pixel blocks, pale glass and white from a lamp, electric blue
// from machinery.
static void sparks(float x,float y,float z,const float n[3],int count,bool glass) {
    float now=gameSeconds();
    if(debris.size()>480)debris.erase(debris.begin(),debris.begin()+(debris.size()-480));
    bool flat=std::abs(n[2])>0.5f;
    float t1[3]={flat?1.0f:-n[1],flat?0.0f:n[0],0},t2[3]={0,flat?1.0f:0.0f,flat?0.0f:1.0f};
    for(int i=0;i<count;++i) {
        float out=randomRange(70,220),across=randomRange(-110,110),up=flat?randomRange(-110,110):randomRange(-40,150);
        std::array<float,3> color=glass?(i%3?std::array<float,3>{0.85f,0.93f,1.0f}:std::array<float,3>{1.0f,1.0f,0.85f})
                                       :(i%3?std::array<float,3>{0.3f,0.55f,1.0f}:std::array<float,3>{0.8f,0.92f,1.0f});
        float v[3];for(int c=0;c<3;++c)v[c]=n[c]*out+t1[c]*across+t2[c]*up;
        debris.push_back({x+n[0]*1.5f,y+n[1]*1.5f,z+n[2]*1.5f,v[0],v[1],v[2],now,randomRange(0.12f,0.35f),randomRange(0.7f,1.2f),color,1.0f});
    }
}
struct ElectricalFault {float x,y,z,n[3],born,next,lastFlash=-100;};
static std::vector<ElectricalFault> faults;
static unsigned faultLevel=~0u;
static void addFault(float x,float y,float z,const float n[3]){
    if(faultLevel!=r_levelserial){faults.clear();faultLevel=r_levelserial;}
    if(!settings.environmentEffects||!settings.electricalDamage)return;
    if(std::any_of(faults.begin(),faults.end(),[&](const auto &f){return std::hypot(x-f.x,y-f.y)<24&&std::abs(z-f.z)<24;}))return;
    if(faults.size()>=16)faults.erase(faults.begin());
    faults.push_back({x,y,z,{n[0],n[1],n[2]},gameSeconds(),gameSeconds()+.25f});
}
static void tickFaults(){
    if(faultLevel!=r_levelserial||!settings.environmentEffects||!settings.electricalDamage||!settings.shootableLights||!settings.emissive){faults.clear();faultLevel=r_levelserial;return;}
    float now=gameSeconds();int flashes=0;
    faults.erase(std::remove_if(faults.begin(),faults.end(),[&](const auto &f){return now<f.born||now-f.born>5;}),faults.end());
    for(auto &f:faults){
        if(now<f.next||flashes>=2)continue;f.next=now+randomRange(.25f,.8f);
        if(SV_Active()&&SV_Trace(f.x+f.n[0]*3,f.y+f.n[1]*3,f.z+f.n[2]*3,-f.n[0]*6,-f.n[1]*6,-f.n[2]*6,nullptr,nullptr)>=1)continue;
        sparks(f.x,f.y,f.z,f.n,3,false);f.lastFlash=now;++flashes;
        sparkFlash(f.x+f.n[0]*4,f.y+f.n[1]*4,f.z+f.n[2]*4,64,.6f,{.3f,.6f,1.f});
    }
}
void buildElectricalArcs(const Uniforms &camera){
    if(!settings.environmentEffects||!settings.electricalDamage||!settings.shootableLights||!settings.emissive||faultLevel!=r_levelserial)return;
    float now=gameSeconds();
    for(const auto &f:faults){
        float age=now-f.lastFlash;if(age<0||age>.12f)continue;
        float tx=-f.n[1],ty=f.n[0],len=std::hypot(tx,ty);if(len<.1f){tx=1;ty=0;}else{tx/=len;ty/=len;}
        uint32_t seed=uint32_t(f.lastFlash*TICRATE)*2654435761u;
        float px=f.x+f.n[0]*3-tx*7,py=f.y+f.n[1]*3-ty*7,pz=f.z+f.n[2]*3;
        for(int i=1;i<=5;++i){
            float x=f.x+f.n[0]*3+tx*(i*2.8f-7),y=f.y+f.n[1]*3+ty*(i*2.8f-7),z=f.z+f.n[2]*3+(hashUnit(i,0,seed)-.5f)*7;
            auto vertex=[&](float vx,float vy,float vz,float side){Vertex v={vx+camera.up[0]*side,vy+camera.up[1]*side,vz+camera.up[2]*side,0,0,1-age/.12f,0};v.red=.45f;v.green=.8f;v.blue=1;return v;};
            quad(particleVertices,vertex(px,py,pz,.25f),vertex(x,y,z,.25f),vertex(x,y,z,-.25f),vertex(px,py,pz,-.25f));px=x;py=y;pz=z;
        }
    }
}
// The floors or ceilings joined to a sector's (through two-sided lines,
// with the same flat at the same height): a strip of lamps is often several
// sectors, some too narrow to hold a tile's center and so a light of their
// own (E1M1's sector 59 between 20 and 60).
static std::vector<int> joinedPlanes(int sector,bool ceiling) {
    std::vector<int> found={sector};
    const sector_t &first=sectors[sector];
    for(size_t k=0;k<found.size();++k) {
        const sector_t &s=sectors[found[k]];
        for(int n=0;n<s.linecount;++n) {
            const line_t *line=s.lines[n];
            const sector_t *other=line->frontsector==&s?line->backsector:line->frontsector;
            if(!other)continue;
            bool same=ceiling?other->ceilingpic==first.ceilingpic&&other->ceilingheight==first.ceilingheight
                             :other->floorpic==first.floorpic&&other->floorheight==first.floorheight;
            int index=int(other-sectors);
            if(same&&std::find(found.begin(),found.end(),index)==found.end())found.push_back(index);
        }
    }
    return found;
}
// A whole floor or ceiling going dark, for planes with no tile of their own.
static SurfaceLightKey wholePlane(int sector,bool ceiling) {return {0,sector,(int)ceiling,0,0,1};}
// Planes without tiles among those joined to the given ones go dark with them.
static void darkenLampless(const std::set<std::pair<int,int>> &planes,float now,float x,float y,float z) {
    std::set<std::pair<int,int>> lit;
    for(const auto &tile:surfaceLights)if(tile.key[0]==0)lit.insert({tile.key[1],tile.key[2]});
    for(const auto &entry:brokenTiles)if(entry.first[0]==0&&entry.first[5]==0)lit.insert({entry.first[1],entry.first[2]});
    for(const auto &[sector,ceiling]:planes)for(int joined:joinedPlanes(sector,ceiling)) {
        if(lit.count({joined,ceiling})||brokenTiles.count(wholePlane(joined,ceiling)))continue;
        brokenTiles[wholePlane(joined,ceiling)]={now,x,y,z};
        brokenPieces.insert({1,joined,ceiling});
    }
}
// The tiles of the fixture a tile belongs to: the glowing tiles of its
// surface (its joined planes, or its wall piece), as a strip of lamps is
// often several sectors (E1M1's sector 59 between 20 and 60). A surface wider
// than a fixture (a long lit ceiling or wall, E1M1's hanging computer boxes)
// breaks by the 256-unit merge cell the tile lies in.
static std::vector<const SurfaceLight*> fixtureTiles(const SurfaceLight &hit) {
    constexpr float widest=448;
    std::vector<int> planes=hit.key[0]==0?joinedPlanes(hit.key[1],hit.key[2]):std::vector<int>{};
    auto same=[&](const SurfaceLightKey &k) {
        if(hit.key[0]==0)return k[0]==0&&k[2]==hit.key[2]&&k[5]==0&&std::find(planes.begin(),planes.end(),k[1])!=planes.end();
        return hit.key[0]==1&&k[0]==1&&k[1]==hit.key[1]&&k[2]==hit.key[2]&&k[5]==hit.key[5];
    };
    std::vector<const SurfaceLight*> tiles;
    float low[3]={hit.x,hit.y,hit.z},high[3]={hit.x,hit.y,hit.z};
    for(const auto &tile:surfaceLights) {
        if(!same(tile.key))continue;
        tiles.push_back(&tile);
        const float p[3]={tile.x,tile.y,tile.z};
        for(int c=0;c<3;++c) {low[c]=std::min(low[c],p[c]);high[c]=std::max(high[c],p[c]);}
    }
    if(high[0]-low[0]>widest||high[1]-low[1]>widest||high[2]-low[2]>widest) {
        SurfaceLightKey cell=surfaceCellOf(hit);
        tiles.erase(std::remove_if(tiles.begin(),tiles.end(),[&](const SurfaceLight *tile){return surfaceCellOf(*tile)!=cell;}),tiles.end());
    }
    if(tiles.empty())tiles.push_back(&hit);
    return tiles;
}
// Breaks the fixture a tile belongs to, with the lampless planes joined to
// its floors and ceilings; the merged lights it was part of re-bake from
// the tiles left.
static void breakFixture(const SurfaceLight &hit) {
    float now=gameSeconds();
    std::set<std::pair<int,int>> planes;
    std::set<SurfaceLightKey> cells;
    for(const SurfaceLight *tile:fixtureTiles(hit)) {
        brokenTiles[tile->key]={now,tile->x,tile->y,tile->z};
        brokenPieces.insert(pieceOf(tile->key));
        if(tile->key[0]==0)planes.insert({tile->key[1],tile->key[2]});
        cells.insert(surfaceCellOf(*tile));
    }
    darkenLampless(planes,now,hit.x,hit.y,hit.z);
    for(const SurfaceLightKey &cell:cells)relightBrokenLight(cell);
    I_AmbienceSilence(hit.x,hit.y,hit.z,96); // Its hum or chatter stops with it.
}
// The sound of a fixture dying: a lamp's buzz sputtering out, a screen's static.
static void dyingSound(int kind,float x,float y,float z) {
    I_MadeSound(kind==DOOM_EMISSIVE_PANEL?IMPACT_SCREEN_DIES:IMPACT_LAMP_DIES,x,y,z);
}
// Breaks the fixture of the glowing tile nearest (x, y, z) among those
// matches takes; false when there is none, or the nearest is already out.
// light receives the fixture's color.
static bool breakNearest(const std::function<bool(const SurfaceLightKey&)> &matches,float x,float y,float z,std::array<float,3> &light) {
    auto distance=[&](float px,float py,float pz){return (px-x)*(px-x)+(py-y)*(py-y)+(pz-z)*(pz-z);};
    const SurfaceLight *hit=nullptr;float nearest=INFINITY;
    for(const auto &tile:surfaceLights)
        if(matches(tile.key)&&distance(tile.x,tile.y,tile.z)<nearest) {nearest=distance(tile.x,tile.y,tile.z);hit=&tile;}
    if(!hit)return false;
    for(const auto &[key,tile]:brokenTiles)if(matches(key)&&distance(tile.x,tile.y,tile.z)<=nearest)return false;
    light=hit->color;
    breakFixture(*hit);
    return true;
}
// The emissive kind of the texture a tile lies on.
static int tileKind(const SurfaceLightKey &key) {
    char name[9];
    if(key[0]==0) {
        int pic=key[2]?sectors[key[1]].ceilingpic:sectors[key[1]].floorpic;
        if(pic==skyflatnum)return DOOM_EMISSIVE_NONE;
        memcpy(name,lumpinfo[firstflat+flattranslation[pic]].name,8);name[8]=0;
        return Game_GlowKind(name,1);
    }
    const line_t &line=lines[key[1]];
    if(line.sidenum[key[2]]<0)return DOOM_EMISSIVE_NONE;
    const side_t &s=sides[line.sidenum[key[2]]];
    int texture=key[5]==1?s.toptexture:key[5]==2?s.bottomtexture:s.midtexture;
    if(texture<=0)return DOOM_EMISSIVE_NONE;
    R_TextureName(texturetranslation[texture],name);
    return Game_GlowKind(name,0);
}
// A near miss: the unbroken lamp whose light point (the middle of its
// glow) lies within 16 units of the hit, on whatever surface the hit is. A
// lamp's point sits on its bulbs, so this forgives shots into the dark
// middle of a ring light or the ceiling beside a strip, while a wall with a
// small lamp painted in (BRICKLIT) still needs the shot near the lamp.
// Screens keep needing the glass. Flat tiles stand 8 units off their plane;
// distance is taken from the plane.
static const SurfaceLight *nearLamp(float x,float y,float z) {
    constexpr float reach=16;
    const SurfaceLight *best=nullptr;float nearest=reach*reach;
    for(const auto &tile:surfaceLights) {
        float tz=tile.key[0]==0?tile.z+(tile.key[2]?8.0f:-8.0f):tile.z;
        float d=(tile.x-x)*(tile.x-x)+(tile.y-y)*(tile.y-y)+(tz-z)*(tz-z);
        if(d>=nearest||brokenTiles.count(tile.key)||tileKind(tile.key)!=DOOM_EMISSIVE_LAMP)continue;
        nearest=d;best=&tile;
    }
    return best;
}
// After the shot: sparks, a flash, and the fixture broken if the shot
// found its glow or came near a lamp. matches picks the tiles the shot may break; where the
// piece has none at all, lampless breaks it (or what it joins) whole.
static void shotSurface(const char *name,bool flat,const Image &image,float u,float v,const std::function<bool(const SurfaceLightKey&)> &matches,
                        const std::function<bool()> &lampless,float x,float y,float z,const float normal[3],int reach) {
    int kind=Game_GlowKind(name,flat);
    bool fixture=settings.emissive&&(kind==DOOM_EMISSIVE_LAMP||kind==DOOM_EMISSIVE_PANEL)&&shotGlow(image,u,v,kind==DOOM_EMISSIVE_PANEL,reach);
    std::array<float,3> color=image.emissionColor;
    auto anyTile=[&] {
        return std::any_of(surfaceLights.begin(),surfaceLights.end(),[&](const SurfaceLight &tile){return matches(tile.key);})||
               std::any_of(brokenTiles.begin(),brokenTiles.end(),[&](const auto &entry){return matches(entry.first);});
    };
    if(fixture&&(breakNearest(matches,x,y,z,color)||(!anyTile()&&lampless()))) {
        addFault(x,y,z,normal);
        dyingSound(kind,x,y,z);
        sparks(x,y,z,normal,16,kind==DOOM_EMISSIVE_LAMP);
        for(float &channel:color)channel=0.5f+0.5f*channel;
        sparkFlash(x+normal[0]*8,y+normal[1]*8,z+normal[2]*8,256,2.0f,color);
        return;
    }
    if(settings.emissive)if(const SurfaceLight *lamp=nearLamp(x,y,z)) {
        color=lamp->color;
        breakFixture(*lamp);
        addFault(x,y,z,normal);
        dyingSound(DOOM_EMISSIVE_LAMP,x,y,z);
        sparks(x,y,z,normal,16,true);
        for(float &channel:color)channel=0.5f+0.5f*channel;
        sparkFlash(x+normal[0]*8,y+normal[1]*8,z+normal[2]*8,256,2.0f,color);
        return;
    }
    if(!Game_SparkTexture(name,flat))return;
    float pick=randomRange(0,1);
    I_MadeSound(pick<0.5f?IMPACT_SPARK_CRACKLE:pick<0.8f?IMPACT_SPARK_FIZZ:IMPACT_SPARK_ZAP,x,y,z);
    sparks(x,y,z,normal,6,false);
    sparkFlash(x+normal[0]*8,y+normal[1]*8,z+normal[2]*8,128,0.9f,{0.45f,0.65f,1.0f});
}
// A hitscan shot stopping on a wall section (wallImpact) at along units
// from the side's first vertex; reach: how many texels from the hit its glow
// test looks (a projectile's width).
void shotWall(const line_t &line,int side,int section,float along,float x,float y,float z,int reach) {
    if(!settings.shootableLights||line.sidenum[side]<0)return;
    const side_t &s=sides[line.sidenum[side]];
    int texture=section==0?s.midtexture:section==1?s.toptexture:s.bottomtexture;
    if(texture<=0)return;
    texture=texturetranslation[texture];
    char name[9];R_TextureName(texture,name);
    const Image &image=wallImage(texture);
    const vertex_t *v1=side?line.v2:line.v1,*v2=side?line.v1:line.v2;
    float ex=units(v2->x)-units(v1->x),ey=units(v2->y)-units(v1->y),len=std::hypot(ex,ey);
    if(len<1)return;
    const float normal[3]={ey/len,-ex/len,0};
    int index=(int)(&line-lines);
    shotSurface(name,false,image,units(s.textureoffset)+along,wallAnchor(line,side,section)-z,
                [&](const SurfaceLightKey &k){return k[0]==1&&k[1]==index&&k[2]==side&&k[5]==section;},[&] {
        SurfaceLightKey whole=wholePiece(index,side,section);
        if(brokenTiles.count(whole))return false;
        brokenTiles[whole]={gameSeconds(),x,y,z};brokenPieces.insert(pieceOf(whole));
        return true;
    },x,y,z,normal,reach);
}
// A free-aimed hitscan shot stopping on a floor or ceiling (planeImpact):
// it may break tiles of the planes joined to the one it hit.
void shotFlat(int sector,bool ceiling,float x,float y,float z,int reach) {
    if(!settings.shootableLights)return;
    int pic=ceiling?sectors[sector].ceilingpic:sectors[sector].floorpic;
    if(pic==skyflatnum)return;
    int lump=firstflat+flattranslation[pic];
    char name[9];memcpy(name,lumpinfo[lump].name,8);name[8]=0;
    const float normal[3]={0,0,ceiling?-1.0f:1.0f};
    std::vector<int> planes=joinedPlanes(sector,ceiling);
    shotSurface(name,true,lumpImage(lump,true),x,-y,[&](const SurfaceLightKey &k) {
        return k[0]==0&&k[2]==(int)ceiling&&k[5]==0&&std::find(planes.begin(),planes.end(),k[1])!=planes.end();
    },[&] {
        size_t before=brokenTiles.size();
        darkenLampless({{sector,(int)ceiling}},gameSeconds(),x,y,z);
        return brokenTiles.size()>before;
    },x,y,z,normal,reach);
}
// Where a shot carried on (shotThrough) ends: the first wall it meets, or
// the floor or ceiling it reaches first, at the current heights, as
// P_LineAttack would stop it; other things do not stop it. Sparks and lights
// there as for a miss; sky shows nothing.
static void carryShot(float x,float y,float z,float dx,float dy,float slope,float length,int reach=1) {
    std::vector<std::pair<float,int>> crossings;
    for(int i=0;i<numlines;++i) {
        const line_t &line=lines[i];
        float ax=units(line.v1->x),ay=units(line.v1->y),ex=units(line.v2->x)-ax,ey=units(line.v2->y)-ay;
        float det=dx*ey-dy*ex;if(std::abs(det)<1e-6f)continue;
        float ox=ax-x,oy=ay-y,t=(ox*ey-oy*ex)/det,u=(ox*dy-oy*dx)/det;
        if(t>0.01f&&t<length&&u>=0&&u<=1)crossings.push_back({t,i});
    }
    std::sort(crossings.begin(),crossings.end());
    const sector_t *sector=R_PointInSubsector((fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT))->sector;
    // Whether the shot ends in sector's floor or ceiling before reaching end.
    auto plane=[&](const sector_t *s,float end) {
        float at=z+slope*end;bool ceiling=slope>0&&at>ceilZ(s);
        if(!ceiling&&!(slope<0&&at<floorZ(s)))return false;
        float h=ceiling?ceilZ(s):floorZ(s),t=(h-z)/slope;
        if((ceiling?s->ceilingpic:s->floorpic)!=skyflatnum)shotFlat(int(s-sectors),ceiling,x+dx*t,y+dy*t,h,reach);
        return true;
    };
    for(auto [t,i]:crossings) {
        if(plane(sector,t))return;
        const line_t &line=lines[i];
        float ax=units(line.v1->x),ay=units(line.v1->y),ex=units(line.v2->x)-ax,ey=units(line.v2->y)-ay;
        int side=ex*(y-ay)-ey*(x-ax)>=0?1:0;
        const sector_t *front=side?line.backsector:line.frontsector,*back=side?line.frontsector:line.backsector;
        if(!front)return;
        float at=z+slope*t;int section=-1;
        if(!back)section=0;
        else if(at>=ceilZ(back)) {
            if(front->ceilingpic==skyflatnum&&back->ceilingpic==skyflatnum)return; // Into the sky, as P_ShootTraverse.
            section=1;
        }
        else if(at<=floorZ(back))section=2;
        if(section<0) {sector=back;continue;}
        WallSpan span;
        if(!wallSpan(line,side,section,span))return;
        float hx=x+dx*t,hy=y+dy*t,sx=span.b.x-span.a.x,sy=span.b.y-span.a.y,len=std::hypot(sx,sy);
        if(len<1)return;
        shotWall(line,side,section,std::clamp(((hx-span.a.x)*sx+(hy-span.a.y)*sy)/len,0.0f,len),hx,hy,at,reach);
        return;
    }
    plane(sector,length);
}
// A hitscan shot whose damage is at least its target's health (bloodHit,
// called before the damage) carries on past the body in the render only
// (settings.killShotsCarry): a kill, not every hit, so lights go out from
// the fight rather than from every stray pellet. The game sees it stop.
// Deflected off bone, it leaves anywhere within 20 degrees to either side
// and 8 down to 16 up of its line, evenly spread so it mostly leaves the
// line, and leaning up as rounds sent down only meet the floor.
void shotThrough(const mobj_t *target,fixed_t fx,fixed_t fy,fixed_t fz,fixed_t fdx,fixed_t fdy,int damage) {
    if(!settings.shootableLights||!settings.killShotsCarry||gamestate!=GS_LEVEL||!target||target->player||
       !(target->flags&MF_SHOOTABLE)||target->health<=0||damage<target->health)return;
    float dx=units(fdx),dy=units(fdy),length=std::hypot(dx,dy);
    if(length<1)return;
    const float degree=(float)doomPi/180;
    float yaw=std::atan2(dy,dx)+randomRange(-20,20)*degree;
    float pitch=std::atan(aimslope/(float)FRACUNIT)+randomRange(-8,16)*degree;
    carryShot(units(fx),units(fy),units(fz),std::cos(yaw),std::sin(yaw),std::tan(std::clamp(pitch,-1.3f,1.3f)),length);
}
// A blast (a rocket, BFG ball or barrel bursting) breaks every lamp or
// screen with a tile within radius that it can see, and lampless wall pieces
// of lamps and screens it faces within radius. Liquids glow on.
void blastLights(float x,float y,float z,float radius) {
    if(!settings.shootableLights||!settings.emissive||gamestate!=GS_LEVEL)return;
    int sector=int(R_PointInSubsector((fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT))->sector-sectors);
    bool rays=bakeMap.sectors.size()==(size_t)numsectors;
    std::vector<std::pair<float,SurfaceLight>> reached; // Nearest first.
    for(const auto &tile:surfaceLights) {
        if(brokenTiles.count(tile.key))continue;
        float d[3]={tile.x-x,tile.y-y,tile.z-z},length=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
        if(length>=radius)continue;
        int kind=tileKind(tile.key);
        if(kind!=DOOM_EMISSIVE_LAMP&&kind!=DOOM_EMISSIVE_PANEL)continue;
        if(rays&&length>4) {
            for(float &c:d)c/=length;
            if(!traceRay(bakeMap,sector,x,y,z,d,length-4,SkyCeiling::open))continue;
        }
        reached.push_back({length,tile});
    }
    std::sort(reached.begin(),reached.end(),[](const auto &a,const auto &b){return a.first<b.first;});
    int sounding=0; // The nearest two fixtures a blast kills are heard dying; more would only blur.
    for(const auto &[length,tile]:reached) {
        if(brokenTiles.count(tile.key))continue; // Gone with a nearer tile of its fixture.
        breakFixture(tile);
        if(sounding++<2)dyingSound(tileKind(tile.key),tile.x,tile.y,tile.z);
        float n[3]={x-tile.x,y-tile.y,z-tile.z},span=std::max(1.0f,length);
        for(float &c:n)c/=span;
        float hi=std::max({tile.color[0],tile.color[1],tile.color[2]}),lo=std::min({tile.color[0],tile.color[1],tile.color[2]});
        addFault(tile.x,tile.y,tile.z,n);
        sparks(tile.x,tile.y,tile.z,n,10,hi-lo<0.3f);
    }
    float now=gameSeconds();
    for(int i=0;i<numlines;++i) {
        const line_t &line=lines[i];
        float ax=units(line.v1->x),ay=units(line.v1->y),ex=units(line.v2->x)-ax,ey=units(line.v2->y)-ay,len2=ex*ex+ey*ey;
        if(len2<1)continue;
        float t=std::clamp(((x-ax)*ex+(y-ay)*ey)/len2,0.0f,1.0f),px=ax+ex*t,py=ay+ey*t;
        if(std::hypot(px-x,py-y)>=radius)continue;
        int side=ex*(y-ay)-ey*(x-ax)>=0?1:0; // The side the blast is on.
        for(int section=0;section<3;++section) {
            WallSpan span;
            if(!wallSpan(line,side,section,span)||span.texture<=0)continue;
            float pz=std::clamp(z,span.bottom,span.top);
            if(std::hypot(std::hypot(px-x,py-y),pz-z)>=radius)continue;
            char name[9];R_TextureName(texturetranslation[span.texture],name);
            int kind=Game_GlowKind(name,0);
            if(kind!=DOOM_EMISSIVE_LAMP&&kind!=DOOM_EMISSIVE_PANEL)continue;
            if(wallImage(texturetranslation[span.texture]).emissionWeight<=0)continue;
            SurfaceLightKey whole=wholePiece(i,side,section);
            auto ours=[&](const SurfaceLightKey &k){return k[0]==1&&k[1]==i&&k[2]==side&&k[5]==section;};
            if(brokenTiles.count(whole)||std::any_of(surfaceLights.begin(),surfaceLights.end(),[&](const SurfaceLight &l){return ours(l.key);})||
               std::any_of(brokenTiles.begin(),brokenTiles.end(),[&](const auto &e){return ours(e.first);}))continue;
            brokenTiles[whole]={now,px,py,pz};brokenPieces.insert(pieceOf(whole));
        }
    }
}
// Projectiles in flight, watched each tic (watchShotLights): where they
// were, their climb per unit along the ground, and their type.
struct Flight { float x,y,z,slope; int type; bool seen; };
static std::unordered_map<const mobj_t*,Flight> flights;
// A projectile burst since the last tic. One that struck a wall, floor or
// ceiling hits its glow as a shot would, a little wider; rockets and BFG
// balls also break what their blast reaches. Its momentum is gone, so it
// struck what lies ahead along its angle; one that struck a thing there
// breaks no wall.
static void projectileBurst(const mobj_t &thing,const Flight &flight) {
    float x=units(thing.x),y=units(thing.y),z=units(thing.z)+units(thing.height)/2;
    game_thing_role role=Game_ThingRole(thing.type);
    if(role==GAME_THING_ROCKET||role==GAME_THING_BIG_SHOT)blastLights(x,y,z,128);
    int reach=std::max(1,(int)std::lround(units(thing.radius)*0.5f));
    int sector=int(thing.subsector->sector-sectors);
    if(thing.z<=thing.floorz+FRACUNIT) {if(sectors[sector].floorpic!=skyflatnum)shotFlat(sector,false,x,y,units(thing.floorz),reach);return;}
    if(thing.z+thing.height>=thing.ceilingz-FRACUNIT) {if(sectors[sector].ceilingpic!=skyflatnum)shotFlat(sector,true,x,y,units(thing.ceilingz),reach);return;}
    float angle=(float)(thing.angle*(2.0*doomPi/4294967296.0)),dx=std::cos(angle),dy=std::sin(angle);
    float ahead=units(thing.info->speed)+units(thing.radius)+8;
    for(int i=0;i<numsectors;++i)for(const mobj_t *other=sectors[i].thinglist;other;other=other->snext) {
        if(other==&thing||!(other->flags&MF_SHOOTABLE))continue;
        float ox=units(other->x)-x,oy=units(other->y)-y,along=ox*dx+oy*dy,room=units(other->radius)+units(thing.radius)+2;
        if(along>-room&&along<ahead+room&&std::abs(ox*dy-oy*dx)<room)return;
    }
    carryShot(x,y,z,dx,dy,flight.slope,ahead,reach);
}
void watchShotLights() {
    tickFaults();
    if(!settings.shootableLights||!settings.emissive||gamestate!=GS_LEVEL) {flights.clear();return;}
    for(auto &entry:flights)entry.second.seen=false;
    for(int i=0;i<numsectors;++i)for(mobj_t *thing=sectors[i].thinglist;thing;thing=thing->snext) {
        bool missile=thing->flags&MF_MISSILE;
        auto found=flights.find(thing);
        if(!missile&&found==flights.end())continue;
        float x=units(thing->x),y=units(thing->y),z=units(thing->z)+units(thing->height)/2;
        if(missile) {
            float ground=std::hypot(units(thing->momx),units(thing->momy));
            float slope=ground>0.01f?units(thing->momz)/ground:0;
            if(found!=flights.end()&&found->second.type==thing->type)found->second={x,y,z,slope,thing->type,true};
            else flights[thing]={x,y,z,slope,thing->type,true};
            continue;
        }
        if(found->second.type==thing->type&&std::hypot(x-found->second.x,y-found->second.y)<units(thing->info->speed)*2+16)
            projectileBurst(*thing,found->second);
        flights.erase(found);
    }
    for(auto it=flights.begin();it!=flights.end();)it=it->second.seen?std::next(it):flights.erase(it);
}
// The wall piece's tiles as wallLights samples them; a dark one covers the
// texture repeats its sample stands for, and the outer ones reach the
// piece's ends. Split along those edges, the piece's quads inside them get
// MODE_DEAD_GLOW. A flickering sector's lamps go dark whole while they are
// out (lampOut), marked MODE_LAMP_OFF too: switched off, not shot, so a
// screen among them keeps its glass whole; its shot-out tiles stay broken.
bool addBrokenWall(std::vector<Vertex> &out,const line_t &line,int side,int span,const Image &image,Point a,Point b,
                   float bottom,float top,float u,float anchor,float light,unsigned mode,unsigned seed) {
    int index=(int)(&line-lines);
    if(image.emissionWeight<=0)return false;
    bool off=lampOut(int(sides[line.sidenum[side]].sector-sectors),image); // A flickering sector's lamps, switched off.
    bool shot=!brokenPieces.empty()&&brokenPieces.count({0,index*2+side,span});
    if(!off&&!shot)return false;
    unsigned offMode=off?MODE_DEAD_GLOW|MODE_LAMP_OFF:0u;
    float len=std::hypot(b.x-a.x,b.y-a.y),w=(float)image.width,h=(float)image.height;
    if(len<=0)return false;
    auto vertex=[&](float along,float z,unsigned m) {
        float t=along/len;
        Vertex v={a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,z,u+along,anchor-z,light,m};
        bakeWallCoordinates(line,side,along,v,true);v.statics=seed;
        return v;
    };
    if(!shot||tileDark(wholePiece(index,side,span))) {
        unsigned m=mode|(shot?MODE_DEAD_GLOW:offMode);
        quad(out,vertex(0,top,m),vertex(len,top,m),vertex(len,bottom,m),vertex(0,bottom,m));
        return true;
    }
    int firstU=(int)std::ceil((u-image.emissionU)/w),lastU=(int)std::floor((u+len-image.emissionU)/w);
    int firstV=(int)std::ceil((anchor-top-image.emissionV)/h),lastV=(int)std::floor((anchor-bottom-image.emissionV)/h);
    int stepU=std::max(1,64/image.width),stepV=std::max(1,64/image.height);
    std::vector<std::array<float,4>> dark; // along0, along1, z0, z1
    for(int tileU=firstU;tileU<=lastU;tileU+=stepU)for(int tileV=firstV;tileV<=lastV;tileV+=stepV) {
        if(!tileShotOut({1,index,side,tileU,tileV,span}))continue;
        float a0=tileU==firstU?0:tileU*w-u,a1=tileU+stepU>lastU?len:(tileU+stepU)*w-u;
        float z1=tileV==firstV?top:anchor-tileV*h,z0=tileV+stepV>lastV?bottom:anchor-(tileV+stepV)*h;
        a0=std::max(a0,0.0f);a1=std::min(a1,len);z0=std::max(z0,bottom);z1=std::min(z1,top);
        if(a1>a0&&z1>z0)dark.push_back({a0,a1,z0,z1});
    }
    if(dark.empty()) {
        if(!off)return false;
        unsigned m=mode|offMode;
        quad(out,vertex(0,top,m),vertex(len,top,m),vertex(len,bottom,m),vertex(0,bottom,m));
        return true;
    }
    std::vector<float> alongs={0,len},heights={bottom,top};
    for(const auto &r:dark) {alongs.push_back(r[0]);alongs.push_back(r[1]);heights.push_back(r[2]);heights.push_back(r[3]);}
    for(auto *cuts:{&alongs,&heights}) {std::sort(cuts->begin(),cuts->end());cuts->erase(std::unique(cuts->begin(),cuts->end()),cuts->end());}
    for(size_t i=0;i+1<alongs.size();++i)for(size_t j=0;j+1<heights.size();++j) {
        float a0=alongs[i],a1=alongs[i+1],z0=heights[j],z1=heights[j+1];
        float ma=(a0+a1)*0.5f,mz=(z0+z1)*0.5f;
        bool dead=std::any_of(dark.begin(),dark.end(),[&](const std::array<float,4> &r){return ma>r[0]&&ma<r[1]&&mz>r[2]&&mz<r[3];});
        unsigned m=mode|(dead?MODE_DEAD_GLOW:offMode);
        quad(out,vertex(a0,z1,m),vertex(a1,z1,m),vertex(a1,z0,m),vertex(a0,z0,m));
    }
    return true;
}
// The same for a floor or ceiling leaf: each dark tile is its texture
// repeat, with the partial repeats beside it at the sector's edge that have
// no tile of their own. A plane with no tiles at all (darkenLampless) goes
// dark whole.
bool addBrokenFlat(std::vector<Vertex> &out,int leaf,int sector,bool ceiling,const Image &image,float z,float light,unsigned mode) {
    if(image.emissionWeight<=0)return false;
    bool off=lampOut(sector,image);
    bool shot=!brokenPieces.empty()&&brokenPieces.count({1,sector,(int)ceiling});
    if(!off&&!shot)return false;
    unsigned offMode=off?MODE_DEAD_GLOW|MODE_LAMP_OFF:0u;
    auto whole=[&](unsigned m) {
        const auto &poly=floors[leaf];
        for(size_t n=1;n+1<poly.size();++n)for(Point p:{poly[0],poly[n],poly[n+1]})out.push_back({p.x,p.y,z,p.x,-p.y,light,mode|m});
        return true;
    };
    if(!shot||tileDark(wholePlane(sector,ceiling)))return whole(shot?MODE_DEAD_GLOW:offMode);
    auto ours=[&](const SurfaceLightKey &k){return k[0]==0&&k[1]==sector&&k[2]==(int)ceiling;};
    std::set<std::pair<int,int>> tiles,dark;
    for(const auto &tile:surfaceLights)if(ours(tile.key))tiles.insert({tile.key[3],tile.key[4]});
    for(const auto &entry:brokenTiles)if(ours(entry.first)&&entry.first[5]==0) {
        tiles.insert({entry.first[3],entry.first[4]});
        if(tileShotOut(entry.first))dark.insert({entry.first[3],entry.first[4]});
    }
    std::set<std::pair<int,int>> repeats=dark;
    for(const auto &[u,v]:dark)for(int dv=-1;dv<=1;++dv)for(int du=-1;du<=1;++du)
        if(!tiles.count({u+du,v+dv}))repeats.insert({u+du,v+dv});
    float w=(float)image.width,h=(float)image.height;
    const auto &bounds=floorBounds[leaf];
    std::vector<std::array<float,4>> squares; // x0, y0, x1, y1
    for(const auto &[u,v]:repeats) {
        std::array<float,4> s={u*w,-(v+1)*h,(u+1)*w,-v*h};
        if(s[0]<bounds[2]&&s[2]>bounds[0]&&s[1]<bounds[3]&&s[3]>bounds[1])squares.push_back(s);
    }
    if(squares.empty())return off&&whole(offMode);
    std::vector<std::vector<Point>> pieces={floors[leaf]};
    auto split=[&](bool vertical,float at) {
        std::vector<std::vector<Point>> next;
        Point origin=vertical?Point{at,0}:Point{0,at},direction=vertical?Point{0,1}:Point{1,0};
        for(auto &piece:pieces) {
            float lo=INFINITY,hi=-INFINITY;
            for(const Point &p:piece) {float c=vertical?p.x:p.y;lo=std::min(lo,c);hi=std::max(hi,c);}
            if(lo<at-0.01f&&hi>at+0.01f) {
                for(bool right:{false,true}) {auto half=clip(piece,origin,direction,right);if(half.size()>=3)next.push_back(std::move(half));}
            } else next.push_back(std::move(piece));
        }
        pieces.swap(next);
    };
    for(const auto &s:squares) {split(true,s[0]);split(true,s[2]);split(false,s[1]);split(false,s[3]);}
    for(const auto &piece:pieces) {
        Point mid={0,0};
        for(const Point &p:piece) {mid.x+=p.x/piece.size();mid.y+=p.y/piece.size();}
        bool dead=std::any_of(squares.begin(),squares.end(),[&](const std::array<float,4> &s){return mid.x>s[0]&&mid.x<s[2]&&mid.y>s[1]&&mid.y<s[3];});
        unsigned m=mode|(dead?MODE_DEAD_GLOW:offMode);
        for(size_t n=1;n+1<piece.size();++n)for(Point p:{piece[0],piece[n],piece[n+1]})out.push_back({p.x,p.y,z,p.x,-p.y,light,m});
    }
    return true;
}
#ifdef DOOM_TEST_DRIVER
extern "C" void I_TestBrokenFlicker(void) {
    int index=-1,sector=-1;Image lamp;lamp.emissionWeight=1;
    for(int i=0;i<numlines;++i)if(lines[i].sidenum[0]>=0){
        int candidate=int(sides[lines[i].sidenum[0]].sector-sectors);
        int saved=sectors[candidate].lightlevel;
        sectors[candidate].lightlevel=0;bool low=lampOut(candidate,lamp);
        sectors[candidate].lightlevel=255;bool high=lampOut(candidate,lamp);
        sectors[candidate].lightlevel=saved;
        if(low&&!high){index=i;sector=candidate;break;}
    }
    if(index<0)I_Error((char*)"Flicker fixture needs a blinking sector");
    auto savedTiles=brokenTiles;auto savedPieces=brokenPieces;
    int savedLight=sectors[sector].lightlevel;
    Image image;image.width=image.height=128;image.emissionWeight=1;image.emissionU=image.emissionV=64;
    for(int kind=0;kind<3;++kind)for(float age:{0.f,.1f,.35f,1.f,20.f})for(int off=0;off<2;++off){
        brokenTiles.clear();brokenPieces.clear();
        if(kind){auto key=kind==1?wholePiece(index,0,0):SurfaceLightKey{1,index,0,0,0,0};
            brokenTiles[key]={gameSeconds()-age,0,0,0};brokenPieces.insert({0,index*2,0});}
        sectors[sector].lightlevel=off?0:255;
        std::vector<Vertex> out;unsigned mode=unsigned(sector+1)<<MODE_SECTOR_SHIFT;
        bool handled=addBrokenWall(out,lines[index],0,0,image,{0,0},{128,0},0,128,0,128,1,mode,42);
        if(handled!=bool(kind||off))I_Error((char*)"Flicker wall handling mismatch");
        if(handled&&out.empty())I_Error((char*)"Flicker wall missing geometry");
        for(const auto &v:out)if(bool(v.mode&MODE_DEAD_GLOW)!=bool(kind)||bool(v.mode&MODE_LAMP_OFF)!=bool(off)
            ||(v.mode>>MODE_SECTOR_SHIFT)!=unsigned(sector+1))I_Error((char*)"Flicker changed physical screen damage");
    }
    sectors[sector].lightlevel=savedLight;brokenTiles=std::move(savedTiles);brokenPieces=std::move(savedPieces);
    puts("Intact and shot-out screens: on/off cycles, partial/whole damage and sputter ages passed.");
}
#endif
// Doom lights a lamp-lit room by its sector level as much as by the lamps,
// so a room that lost them falls dark by the share of its baked floor light
// they gave, below Doom's darkest level if they gave it nearly all; each
// further lamp takes its share of what is left.
std::vector<float> shotDim;
void dimShotSector(int sector,float share) {
    if(sector<0||sector>=numsectors)return;
    if(shotDim.size()!=(size_t)numsectors)shotDim.assign(numsectors,1.0f);
    shotDim[sector]=std::max(0.1f,shotDim[sector]*(1-0.85f*std::clamp(share,0.0f,1.0f)));
}
void shotLightsShutdown() {faults.clear();faultLevel=~0u;brokenTiles.clear();brokenPieces.clear();shotDim.clear();flights.clear();}
} // namespace doom3d

extern "C" void I_TestElectricalFaults(){
    using namespace doom3d;
    auto old=settings;auto oldFaults=std::move(faults);auto oldDebris=debris;int oldTime=leveltime;
    settings.environmentEffects=settings.electricalDamage=settings.shootableLights=settings.emissive=1;
    float n[3]={1,0,0};for(int i=0;i<40;++i)addFault(float(i*40),0,32,n);
    if(faults.size()!=16)I_Error("Electrical faults exceeded cap");
    leveltime+=6*TICRATE;tickFaults();if(!faults.empty())I_Error("Electrical faults did not expire");
    leveltime=oldTime;addFault(0,0,32,n);settings.electricalDamage=0;tickFaults();
    if(!faults.empty())I_Error("Electrical toggle did not clear faults");
    settings=old;faults=std::move(oldFaults);debris=std::move(oldDebris);
    puts("PASS electrical fault cap, expiry and toggle");
}
