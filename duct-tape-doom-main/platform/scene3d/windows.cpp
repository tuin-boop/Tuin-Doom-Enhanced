/* Window glass: windows found in the map's geometry get a pane of glass
   (glass.frag). Two bullets chip and crack it; the third starts a fracture
   spreading across its plane from the impact. Projectiles and blasts break
   it outright. Render-only: the engine never sees
   the glass, shots and sight pass as in vanilla, and every level starts
   with whole panes, but for a window an authored map has broken already
   (Game_BrokenWindows). */
#include "internal.h"
#include "../glass_fracture.h"
#include "../../../map_world.h"
extern const lab::MapWorld *SV_MapWorld();
extern "C" {
#include "../audio/impact_synth.h"
#include "../audio/sound_options.h"
}

namespace doom3d {
std::vector<Vertex> glassVertices;
bool glassMirrorActive=false;
float glassMirrorPlane[4]={},glassMirrorFade=1;
std::vector<GlassProbe> glassProbes;
std::vector<int> glassCaptures;
unsigned glassProbeSerial=0,settingsSerial=0;
// A pane fills the opening of one line between a window's frame sector and
// the sector it looks out on (outer); inner is the room on the other side.
// The panes of one frame break together. probe: the reflection probe on
// the outer and inner side (-1: none). The pane stands halfway through the
// frame, in the middle of the sill: (ax,ay)-(bx,by) is the line moved inward
// by shift.
struct GlassPane {
    int line,frame,outer,inner; float ax,ay,bx,by; uint32_t seed;
    bool broken=false,detached=false; int probe[2]={-1,-1}; float shift=0;
    int hits=0,fractureAt=0,fractureEnd=-1;
    std::array<doom_glass::Point,2> holes={};
    doom_glass::Point impact={};
};
// Actual pieces of the pane, in local coordinates about their center. Their
// yaw, tilt and in-plane roll tumble independently, interpolated between tics.
struct Shard {
    float x,y,z,px,py,pz,vx,vy,vz;
    doom_glass::Raster pixels;
    std::vector<doom_glass::Point> outline;
    doom_glass::Point center;
    std::array<float,3> angle,previous,spin;
    int pane,release,die;
    uint32_t seed;
    bool landed=false,impactPiece=false;
};
struct ShardAxes { std::array<float,3> u,v,normal; };
static ShardAxes shardAxes(const std::array<float,3> &a) {
    float cy=std::cos(a[0]),sy=std::sin(a[0]),ct=std::cos(a[1]),st=std::sin(a[1]);
    float cr=std::cos(a[2]),sr=std::sin(a[2]);
    std::array<float,3> u={cy,sy,0},v={-sy*st,cy*st,ct};
    ShardAxes axes;
    for(int i=0;i<3;++i) {axes.u[i]=u[i]*cr+v[i]*sr;axes.v[i]=v[i]*cr-u[i]*sr;}
    axes.normal={sy*ct,-cy*ct,st};
    return axes;
}
static void shardHeights(const Shard &s,const ShardAxes &axes,float &low,float &high) {
    low=high=0;
    for(const auto &band:s.pixels.bands) {
        float a=axes.u[2]*band.left,b=axes.u[2]*band.right;
        float c=axes.v[2]*band.bottom,d=axes.v[2]*band.top;
        low=std::min(low,std::min(a,b)+std::min(c,d));
        high=std::max(high,std::max(a,b)+std::max(c,d));
    }
}
struct MissileWatch { float x,y,z; bool missile,seen; int type; };
static std::vector<GlassPane> panes;
static std::vector<int> paneOfLine;
// Per frame sector with panes, the lines near its window that stop shards.
static std::unordered_map<int,std::vector<int>> nearLines;
static std::vector<Shard> shards;
static std::unordered_map<const mobj_t*,MissileWatch> missileWatch;
static unsigned glassLevelSerial=~0u;
// Map units per tic along the glass, deliberately visible over several frames.
static constexpr float fractureSpeed=12;

static float rawFloor(const sector_t *s) {return units(s->floorheight);}
static float rawCeiling(const sector_t *s) {return units(s->ceilingheight);}
static bool skySector(const sector_t *s) {return s->ceilingpic==skyflatnum;}
// Doors that open the sector behind them without a tag.
static bool manualDoor(int special) {
    switch(special) {case 1:case 26:case 27:case 28:case 31:case 32:case 33:case 34:case 117:case 118:return true;}
    return false;
}
static std::array<float,4> sectorBox(const sector_t &s) {
    std::array<float,4> box={1e9f,1e9f,-1e9f,-1e9f};
    for(int n=0;n<s.linecount;++n)for(const vertex_t *v:{s.lines[n]->v1,s.lines[n]->v2}) {
        box[0]=std::min(box[0],units(v->x));box[1]=std::min(box[1],units(v->y));
        box[2]=std::max(box[2],units(v->x));box[3]=std::max(box[3],units(v->y));
    }
    return box;
}
// Reflection probes: per window (frame sector) and side, a point in front of
// the glass at its middle height, 32 units out on the outer side and past
// the frame on the inner one, and the box of the room there for parallax
// (the side's sector and the frame, floor to ceiling). Captured as cube
// maps (planGlassCaptures) for panes with no live mirror.
static void findProbes() {
    glassProbes.clear();++glassProbeSerial;
    std::map<std::pair<int,int>,int> made;
    for(GlassPane &p:panes) {
        float ex=p.bx-p.ax,ey=p.by-p.ay,len=std::hypot(ex,ey);
        if(len<1)continue;
        float nx=ey/len,ny=-ex/len; // Toward the line's front side.
        if(lines[p.line].frontsector-sectors!=p.outer) {nx=-nx;ny=-ny;}
        auto frameBox=sectorBox(sectors[p.frame]);
        float depth=std::min(frameBox[2]-frameBox[0],frameBox[3]-frameBox[1]);
        float bottom=std::max(rawFloor(&sectors[p.frame]),rawFloor(&sectors[p.outer])),top=std::min(rawCeiling(&sectors[p.frame]),rawCeiling(&sectors[p.outer]));
        for(int side=0;side<2;++side) {
            int room=side==0?p.outer:p.inner;
            if(room<0)continue;
            auto key=std::make_pair(p.frame,side);
            auto found=made.find(key);
            if(found!=made.end()) {p.probe[side]=found->second;continue;}
            if(glassProbes.size()>=maxGlassProbes)continue;
            float out=side==0?32+p.shift:-(depth-p.shift+24),cx=(p.ax+p.bx)/2+nx*out,cy=(p.ay+p.by)/2+ny*out;
            const sector_t &s=sectors[room];
            auto box=sectorBox(s);
            GlassProbe probe={{cx,cy,(bottom+top)/2},
                {std::min({box[0],frameBox[0],cx-8}),std::min({box[1],frameBox[1],cy-8}),rawFloor(&s)},
                {std::max({box[2],frameBox[2],cx+8}),std::max({box[3],frameBox[3],cy+8}),rawCeiling(&s)+(skySector(&s)?128.0f:0.0f)}};
            probe.low[2]=std::min(probe.low[2],probe.at[2]-8);probe.high[2]=std::max(probe.high[2],probe.at[2]+8);
            made[key]=p.probe[side]=(int)glassProbes.size();
            glassProbes.push_back(probe);
        }
    }
}
// A window is a thin frame sector between two others, with a sill and a
// lintel: its floor above both neighbors and its ceiling below them (or
// under the sky beside it), too high a step to walk in or behind an
// impassable line, at least 32 units wide and tall and at most 96 deep (deep
// recesses like E1M2's side windows, sectors 50 and 57, are 72; deeper ones
// are alcoves). Sectors that move or
// that line specials act on are left alone: a lift is not a window. The
// glass goes on the line facing the sky, or else the larger room, moved
// halfway across the frame (its farthest corner from that line).
static void findWindows() {
    panes.clear();paneOfLine.assign(numlines,-1);nearLines.clear();
    std::set<int> tags;
    for(int i=0;i<numlines;++i)if(lines[i].special&&lines[i].tag)tags.insert(lines[i].tag);
    std::vector<char> excluded(numsectors,0);
    for(int i=0;i<numsectors;++i)if(sectors[i].tag&&tags.count(sectors[i].tag))excluded[i]=1;
    for(int i=0;i<numlines;++i) {
        const line_t &line=lines[i];
        if(!line.special||!line.frontsector||!line.backsector)continue;
        excluded[line.frontsector-sectors]=excluded[line.backsector-sectors]=1;
        if(manualDoor(line.special))excluded[line.backsector-sectors]=1;
    }
    std::vector<float> area(numsectors);
    for(int i=0;i<numsectors;++i) {auto b=sectorBox(sectors[i]);area[i]=(b[2]-b[0])*(b[3]-b[1]);}
    for(int s=0;s<numsectors;++s) {
        const sector_t &frame=sectors[s];
        if(excluded[s]||skySector(&frame)||rawCeiling(&frame)-rawFloor(&frame)<32)continue;
        bool window=true;std::vector<int> sides;
        for(int n=0;n<frame.linecount&&window;++n) {
            const line_t *line=frame.lines[n];
            if(!line->frontsector||!line->backsector)continue;
            const sector_t *other=line->frontsector==&frame?line->backsector:line->frontsector;
            if(other==&frame)continue;
            float bottom=std::max(rawFloor(&frame),rawFloor(other)),top=std::min(rawCeiling(&frame),rawCeiling(other));
            if(top-bottom<16)continue;
            bool walkable=rawFloor(&frame)-rawFloor(other)<=24&&top-bottom>=56&&!(line->flags&ML_BLOCKING);
            bool lintel=rawCeiling(&frame)<rawCeiling(other)||skySector(other);
            if(walkable||!lintel||rawFloor(&frame)<=rawFloor(other)) {window=false;break;}
            int o=int(other-sectors);
            if(std::find(sides.begin(),sides.end(),o)==sides.end())sides.push_back(o);
        }
        if(!window||sides.size()<2)continue;
        auto box=sectorBox(frame);
        float thin=std::min(box[2]-box[0],box[3]-box[1]),wide=std::max(box[2]-box[0],box[3]-box[1]);
        if(thin>96||wide<32)continue;
        auto rank=[&](int o){return std::make_pair(skySector(&sectors[o]),area[o]);};
        int outer=*std::max_element(sides.begin(),sides.end(),[&](int a,int b){return rank(a)<rank(b);}),inner=-1;
        for(int o:sides)if(o!=outer&&(inner<0||area[o]>area[inner]))inner=o;
        for(int n=0;n<frame.linecount;++n) {
            const line_t *line=frame.lines[n];
            if(!line->frontsector||!line->backsector)continue;
            const sector_t *other=line->frontsector==&frame?line->backsector:line->frontsector;
            if(other-sectors!=outer||paneOfLine[line-lines]>=0)continue;
            int index=int(line-lines);
            paneOfLine[index]=(int)panes.size();
            GlassPane pane={index,s,outer,inner,units(line->v1->x),units(line->v1->y),units(line->v2->x),units(line->v2->y),hash32((uint32_t)index*2654435761u+7)};
            float ex=pane.bx-pane.ax,ey=pane.by-pane.ay,len=std::hypot(ex,ey);
            if(len>=1) {
                float nx=ey/len,ny=-ex/len; // Toward the line's front side...
                if(line->frontsector==&sectors[outer]) {nx=-nx;ny=-ny;} // ...turned into the frame.
                float across=0;
                for(int k=0;k<frame.linecount;++k)for(const vertex_t *v:{frame.lines[k]->v1,frame.lines[k]->v2})
                    across=std::max(across,(units(v->x)-pane.ax)*nx+(units(v->y)-pane.ay)*ny);
                pane.shift=std::min(across,thin)/2;
                pane.ax+=nx*pane.shift;pane.ay+=ny*pane.shift;pane.bx+=nx*pane.shift;pane.by+=ny*pane.shift;
            }
            panes.push_back(pane);
        }
    }
    findProbes();
    // Lines within reach of each window, for the shards that fly from it.
    for(const GlassPane &p:panes) {
        auto &list=nearLines[p.frame];
        if(!list.empty())continue;
        float cx=(p.ax+p.bx)/2,cy=(p.ay+p.by)/2;
        for(int i=0;i<numlines;++i) {
            float ax=units(lines[i].v1->x),ay=units(lines[i].v1->y),ex=units(lines[i].v2->x)-ax,ey=units(lines[i].v2->y)-ay;
            float t=std::clamp(((cx-ax)*ex+(cy-ay)*ey)/std::max(ex*ex+ey*ey,1e-6f),0.0f,1.0f);
            if(std::hypot(ax+ex*t-cx,ay+ey*t-cy)<256)list.push_back(i);
        }
    }
    // The windows the authored map broke before the player came: the
    // window nearest each spot, within 128 units, every pane of it.
    int broken=0;
    if(settings.authored&&!panes.empty()) {
        game_spot spots[16];
        int count=Game_BrokenWindows(gameepisode,gamemap,spots,16);
        for(int i=0;i<count;++i) {
            int frame=-1;float nearest=128;
            for(const GlassPane &p:panes) {
                float d=std::hypot((p.ax+p.bx)/2-spots[i].x,(p.ay+p.by)/2-spots[i].y);
                if(d<nearest) {nearest=d;frame=p.frame;}
            }
            for(GlassPane &p:panes)if(p.frame==frame&&!p.broken) {p.broken=true;++broken;}
        }
    }
    fprintf(stderr,"3D glass: %zu window panes%s.\n",panes.size(),broken?", some broken already":"");
}
// Inside the frame the opening runs from its sill to its lintel.
static void paneHeights(const GlassPane &p,float &bottom,float &top) {
    const sector_t *frame=&sectors[p.frame];
    bottom=floorZ(frame);top=ceilZ(frame);
}
// Breaks every pane of a window. Shards scatter from all over the glass:
// fast along the shot near where it went through, slower further away, a few
// back toward the shooter; they fall, bounce once and lie glinting a while.
static void shatter(int frame,float hx,float hy,float hz,float dx,float dy,bool spreading=false,bool falling=false) {
    float length=std::hypot(dx,dy),area=0;
    if(length>1e-4f) {dx/=length;dy/=length;}
    for(GlassPane &p:panes) {
        if(p.frame!=frame||p.detached||(p.broken&&!falling))continue;
        bool rimOnly=p.broken;
        p.broken=true;p.detached=falling;
        float bottom,top;paneHeights(p,bottom,top);
        float ex=p.bx-p.ax,ey=p.by-p.ay,len=std::hypot(ex,ey);
        if(len<1||top<=bottom)continue;
        area+=len*(top-bottom);
        I_AmbienceGlaze(p.line,0); // Sound gets through: the wind blows in.
        ex/=len;ey/=len;
        // A blast in the glass's own plane pushes straight through it.
        if(length<=1e-4f) {dx=ey;dy=-ex;}
        float hitAlong=(hx-p.ax)*ex+(hy-p.ay)*ey;
        p.impact={hitAlong,hz-bottom};p.fractureAt=leveltime;
        auto fragments=doom_glass::fracture(len,top-bottom,hitAlong,hz-bottom,p.seed);
        size_t count=fragments.size();
        if(shards.size()+count>600)shards.erase(shards.begin(),shards.begin()+std::min(shards.size(),shards.size()+count-600));
        for(auto &piece:fragments) {
            // Already shattered panes retain only narrow edge remnants.
            if(rimOnly&&piece.center.x>12&&piece.center.x<len-12&&piece.center.y>12&&piece.center.y<top-bottom-12)continue;
            Shard s={};
            s.x=p.ax+ex*piece.center.x;s.y=p.ay+ey*piece.center.x;s.z=bottom+piece.center.y;
            float rx=piece.center.x-hitAlong,rz=s.z-hz,apart=std::hypot(rx,rz);
            float near=std::exp(-apart/90),mass=std::clamp(18/std::sqrt(piece.area),0.65f,1.6f);
            float push=randomRange(2.5f,5.5f)*(0.35f+0.65f*near)*mass;
            if(randomRange(0,1)<0.12f)push*=-0.35f;
            float slide=randomRange(-0.45f,0.45f)+rx/std::max(apart,12.0f)*near*1.3f;
            s.vx=dx*push+ex*slide;s.vy=dy*push+ey*slide;
            s.vz=randomRange(-0.3f,1.1f)+rz/std::max(apart,12.0f)*near;
            if(falling){s.vx*=0.25f;s.vy*=0.25f;s.vz=randomRange(-2.0f,-0.5f);}
            s.px=s.x;s.py=s.y;s.pz=s.z;
            s.angle={std::atan2(ey,ex),0,0};s.previous=s.angle;
            s.spin={randomRange(-0.10f,0.10f),randomRange(-0.22f,0.22f),randomRange(-0.14f,0.14f)};
            s.pixels=doom_glass::raster(piece.points);
            s.center=piece.center;s.outline=std::move(piece.points);
            // A piece stays in the pane until the crack has reached all its
            // corners. Pieces near the final hit release before distant ones.
            float reach=0;
            for(const auto &v:s.outline)reach=std::max(reach,std::hypot(s.center.x+v.x-hitAlong,s.center.y+v.y-p.impact.y));
            s.impactPiece=true;
            for(size_t i=0;i<s.outline.size();++i) {
                auto a=s.outline[i],b=s.outline[(i+1)%s.outline.size()];
                if((b.x-a.x)*(p.impact.y-s.center.y-a.y)-(b.y-a.y)*(hitAlong-s.center.x-a.x)<-1e-4f)s.impactPiece=false;
            }
            s.release=leveltime+(spreading?(int)std::ceil(reach/fractureSpeed)+2:0);
            p.fractureEnd=std::max(p.fractureEnd,s.release);
            s.pane=int(&p-panes.data());s.die=s.release+(int)randomRange(14*TICRATE,24*TICRATE);s.seed=(uint32_t)impactRandom();
            shards.push_back(std::move(s));
        }
    }
    // One sound for the window: the big one past about 96 by 64 units of glass.
    if(area>0)I_MadeSound(area>6144?IMPACT_GLASS_BIG:IMPACT_GLASS_BREAK,hx,hy,hz);
}
// Only actual excavation counts: map openings and a shot below intact glass
// must never cause a false break. Two adjacent lost sill samples release it.
static bool lostGlassSill(const GlassPane &p,const lab::MapWorld &world) {
    float bottom,top;paneHeights(p,bottom,top);
    int run=0;
    for(int i=1;i<8;++i) {
        float t=i/8.0f,x=p.ax+(p.bx-p.ax)*t,y=p.ay+(p.by-p.ay)*t;
        bool removed=world.removal({x,y,bottom-4})>0&&world.removal({x,y,bottom-12})>0;
        run=removed?run+1:0;
        if(run>=2)return true;
    }
    return false;
}
static void checkGlassSupports() {
    const auto *world=SV_MapWorld();if(!world)return;
    static unsigned serial=~0u,revision=~0u;
    if(serial==r_levelserial&&revision==world->revision())return;
    serial=r_levelserial;revision=world->revision();
    for(const auto &p:panes)if(!p.detached&&lostGlassSill(p,*world)) {
        float bottom,top;paneHeights(p,bottom,top);
        shatter(p.frame,(p.ax+p.bx)*0.5f,(p.ay+p.by)*0.5f,bottom,0,0,false,true);
    }
}
static bool glassReady() {return settings.windowGlass&&glassLevelSerial==r_levelserial&&gamestate==GS_LEVEL;}
void glassShot(line_t *line,fixed_t x,fixed_t y,fixed_t z,fixed_t dx,fixed_t dy) {
    if(!line||!glassReady())return;
    int pane=paneOfLine[line-lines];
    if(pane<0||panes[pane].broken)return;
    GlassPane &p=panes[pane];
    float ex=p.bx-p.ax,ey=p.by-p.ay,len=std::hypot(ex,ey),length=std::hypot(units(dx),units(dy));
    if(len<1||length<1e-4f)return;
    ex/=len;ey/=len;
    float rx=units(dx)/length,ry=units(dy)/length,hx=units(x),hy=units(y),hz=units(z);
    // The hook is on the map line; the visible glass is halfway through
    // the sill. Follow the shot to that plane, including its vertical slope.
    float across=rx*ey-ry*ex;
    if(std::abs(across)<1e-4f)return;
    float travel=((p.ax-hx)*ey-(p.ay-hy)*ex)/across;
    hx+=rx*travel;hy+=ry*travel;hz+=units(aimslope)*travel;
    float bottom,top;paneHeights(p,bottom,top);
    float along=(hx-p.ax)*ex+(hy-p.ay)*ey;
    if(along<0||along>len||hz<bottom||hz>top)return;
    if(!settings.progressiveGlass){shatter(p.frame,hx,hy,hz,rx,ry,false);return;}
    int hits=0;
    for(const GlassPane &other:panes)if(other.frame==p.frame)hits+=other.hits;
    if(hits>=2)shatter(p.frame,hx,hy,hz,rx,ry,true);
    else {
        p.holes[p.hits++]={along,hz-bottom};
        I_MadeSound(IMPACT_GLASS_TINKLE,hx,hy,hz);
    }
}
// Explosions break windows within radius of the blast.
void glassBlast(float x,float y,float z,float radius) {
    if(!glassReady())return;
    for(const GlassPane &p:panes) {
        if(p.broken)continue;
        float bottom,top;paneHeights(p,bottom,top);
        float ex=p.bx-p.ax,ey=p.by-p.ay;
        float t=std::clamp(((x-p.ax)*ex+(y-p.ay)*ey)/std::max(ex*ex+ey*ey,1e-6f),0.0f,1.0f);
        float px=p.ax+ex*t,py=p.ay+ey*t,pz=std::clamp(z,bottom,top);
        if(std::hypot(std::hypot(px-x,py-y),pz-z)<radius)shatter(p.frame,px,py,pz,px-x,py-y);
    }
}
// Whether a line stops a shard crossing it at height z.
static bool stopsShard(const line_t &line,float z) {
    if(!line.frontsector||!line.backsector)return true;
    return z<std::max(rawFloor(line.frontsector),rawFloor(line.backsector))||z>std::min(rawCeiling(line.frontsector),rawCeiling(line.backsector));
}
static bool crosses(float ax,float ay,float bx,float by,const line_t &line,float &t) {
    float lx=units(line.v1->x),ly=units(line.v1->y),ex=units(line.v2->x)-lx,ey=units(line.v2->y)-ly;
    float dx=bx-ax,dy=by-ay,det=dx*ey-dy*ex;
    if(std::abs(det)<1e-6f)return false;
    float ox=lx-ax,oy=ly-ay;t=(ox*ey-oy*ex)/det;
    float u=(ox*dy-oy*dx)/det;
    return t>=0&&t<=1&&u>=0&&u<=1;
}
static void stepShards() {
    for(Shard &s:shards) {
        s.px=s.x;s.py=s.y;s.pz=s.z;s.previous=s.angle;
        if(leveltime<=s.release)continue;
        if(s.landed)continue;
        for(int i=0;i<3;++i) {s.angle[i]+=s.spin[i];s.spin[i]*=0.985f;}
        float nx=s.x+s.vx,ny=s.y+s.vy,nz=s.z+s.vz;
        s.vz-=0.8f;
        float t;
        auto near=nearLines.find(panes[s.pane].frame);
        if(near!=nearLines.end())for(int i:near->second)
            if(crosses(s.x,s.y,nx,ny,lines[i],t)&&stopsShard(lines[i],s.z+(nz-s.z)*t)) {nx=s.x;ny=s.y;s.vx*=-0.18f;s.vy*=-0.18f;break;}
        const sector_t *sector=R_PointInSubsector((fixed_t)(nx*FRACUNIT),(fixed_t)(ny*FRACUNIT))->sector;
        float floor=rawFloor(sector),ceiling=rawCeiling(sector);
        ShardAxes axes=shardAxes(s.angle);
        float low,high;shardHeights(s,axes,low,high);
        // A broad piece can turn taller than a shallow window opening.
        // Lay it through the gap instead of applying conflicting contacts.
        if(high-low>ceiling-floor-0.4f) {
            s.angle[1]=doomPi/2+std::round((s.angle[1]-doomPi/2)/doomPi)*doomPi;
            s.spin[1]*=0.5f;low=high=0;
        }
        if(nz+high>ceiling-0.2f) {nz=ceiling-high-0.2f;s.vz=std::min(s.vz,0.0f);}
        if(nz+low<=floor+0.2f) {
            size_t index=sector-sectors;
            if(index<sectorMist.size()&&sectorMist[index].liquid) {s.die=leveltime;continue;} // Sinks.
            nz=floor-low+0.2f;
            if(s.vz<-2.5f) {
                s.vz=-s.vz*0.25f;s.vx*=0.6f;s.vy*=0.6f;
                for(float &spin:s.spin)spin*=0.55f;
            }
            else {
                s.vz=0;s.vx=s.vy=0;s.landed=true;
                s.angle[1]=doomPi/2+std::round((s.angle[1]-doomPi/2)/doomPi)*doomPi;
                nz=floor+0.2f;
            }
        }
        s.x=nx;s.y=ny;s.z=nz;
    }
    shards.erase(std::remove_if(shards.begin(),shards.end(),[](const Shard &s){return leveltime>=s.die;}),shards.end());
}
// Before each tic: projectiles that went through a pane since the last one
// break it, rockets that burst near one too, and the shards move.
void watchGlass() {
    if(!settings.windowGlass||gamestate!=GS_LEVEL) return;
    if(glassLevelSerial!=r_levelserial) {
        glassLevelSerial=r_levelserial;shards.clear();missileWatch.clear();
        findWindows();
        // Through the glass ambience comes muffled until it breaks.
        for(const GlassPane &p:panes)I_AmbienceGlaze(p.line,p.broken?0:1);
    }
    if(getenv("DOOM_TEST_GLASS_SUPPORT")) {
        static unsigned tested=~0u;
        if(tested!=r_levelserial) {
            tested=r_levelserial;
            auto *world=SV_MapWorld();if(!world||panes.empty())I_Error("Glass support test needs solid map panes");
            auto found=std::find_if(panes.begin(),panes.end(),[&](const GlassPane &p){return !lostGlassSill(p,*world);});
            if(found==panes.end())I_Error("No intact sill");
            auto testWorld=*world;
            float bottom,top;paneHeights(*found,bottom,top);
            float x=(found->ax+found->bx)*.5f,y=(found->ay+found->by)*.5f;
            if(lostGlassSill(*found,testWorld))I_Error("Intact sill collapsed");
            // Material outside the sill cannot detach this pane.
            testWorld.blast({x,y,bottom-100},16);
            if(lostGlassSill(*found,testWorld))I_Error("Distant cut collapsed glass");
            float radius=std::max(40.0f,std::hypot(found->bx-found->ax,found->by-found->ay)*.3f);
            testWorld.blast({x,y,bottom-12},radius);
            // Populate the edited field samples used by the support query.
            for(int i=1;i<8;++i)for(int z: {4,12}) {
                lab::Vec q{found->ax+(found->bx-found->ax)*i/8.f,found->ay+(found->by-found->ay)*i/8.f,bottom-float(z)};
                testWorld.tile({int(std::floor(q.x/128)),int(std::floor(q.y/128)),int(std::floor(q.z/128))});
            }
            if(!lostGlassSill(*found,testWorld))I_Error("Destroyed sill retained glass");
            size_t index=found-panes.begin();int frame=found->frame;
            shatter(frame,x,y,bottom,0,0,false,true);
            if(!panes[index].detached||shards.empty())I_Error("Unsupported pane did not fall");
            size_t count=shards.size();shatter(frame,x,y,bottom,0,0,false,true);
            if(shards.size()!=count)I_Error("Unsupported pane shattered twice");
            puts("PASS glass support: intact and distant cuts retained; lost sill detaches, falls and shatters once");
        }
    }
    if(getenv("DOOM_TEST_GLASS_HEIGHT")){
        static unsigned tested=~0u;
        if(tested!=r_levelserial){
            tested=r_levelserial;
            for(const auto &p:panes)if(p.broken||p.hits)I_Error("Fresh map contains pre-damaged glass");
            puts("PASS fresh-map windows start intact before gunfire");
            auto found=std::find_if(panes.begin(),panes.end(),[](const GlassPane& p){return !p.broken;});
            if(found==panes.end())I_Error("No intact pane for glass height test");
            const GlassPane pane=*found;float bottom,top;paneHeights(pane,bottom,top);
            auto savedPanes=panes;int progressive=settings.progressiveGlass;
            settings.progressiveGlass=1;
            fixed_t x=fixed_t((pane.ax+pane.bx)*.5f*FRACUNIT),y=fixed_t((pane.ay+pane.by)*.5f*FRACUNIT);
            glassShot(&lines[pane.line],x,y,fixed_t((bottom-16)*FRACUNIT),FRACUNIT,0);
            glassShot(&lines[pane.line],x,y,fixed_t((top+16)*FRACUNIT),FRACUNIT,0);
            if(found->broken)I_Error("Shot outside pane height broke glass");
            glassShot(&lines[pane.line],x,y,fixed_t((bottom+top)*.5f*FRACUNIT),FRACUNIT,0);
            if(found->broken||found->hits!=1)I_Error("First glass hit failed to chip pane");
            glassShot(&lines[pane.line],x,y,fixed_t((bottom+top)*.5f*FRACUNIT),FRACUNIT,0);
            if(found->broken||found->hits!=2)I_Error("Second glass hit failed to retain pane");
            glassShot(&lines[pane.line],x,y,fixed_t((bottom+top)*.5f*FRACUNIT),FRACUNIT,0);
            if(!found->broken)I_Error("Third glass hit failed to fracture pane");
            size_t index=found-panes.begin();panes=savedPanes;settings.progressiveGlass=0;
            glassShot(&lines[pane.line],x,y,fixed_t((bottom+top)*.5f*FRACUNIT),FRACUNIT,0);
            if(!panes[index].broken)I_Error("Instant glass toggle did not break pane");
            settings.progressiveGlass=progressive;
            puts("PASS glass height, two chip hits, third-hit fracture and instant-break toggle");
        }
    }
    checkGlassSupports();
    stepShards();
    if(panes.empty())return;
    for(auto &entry:missileWatch)entry.second.seen=false;
    for(int i=0;i<numsectors;++i)for(mobj_t *thing=sectors[i].thinglist;thing;thing=thing->snext) {
        bool missile=thing->flags&MF_MISSILE;
        auto found=missileWatch.find(thing);
        if(!missile&&found==missileWatch.end())continue;
        float x=units(thing->x),y=units(thing->y),z=units(thing->z)+units(thing->height)/2;
        if(found==missileWatch.end()||found->second.type!=thing->type||
           std::hypot(x-found->second.x,y-found->second.y)>units(thing->info->speed)*2+16) {
            if(missile)missileWatch[thing]={x,y,z,true,true,thing->type};
            else missileWatch.erase(found);
            continue;
        }
        MissileWatch &watch=found->second;watch.seen=true;
        if(watch.missile)for(const GlassPane &p:panes) {
            if(p.broken)continue;
            float t;
            if(!crosses(watch.x,watch.y,x,y,lines[p.line],t))continue;
            float bottom,top;paneHeights(p,bottom,top);
            float at=watch.z+(z-watch.z)*t;
            if(at<bottom-2||at>top+2)continue;
            shatter(p.frame,watch.x+(x-watch.x)*t,watch.y+(y-watch.y)*t,at,x-watch.x,y-watch.y);
        }
        if(watch.missile&&!missile&&Game_ThingRole(thing->type)==GAME_THING_ROCKET)glassBlast(x,y,z,96);
        watch.x=x;watch.y=y;watch.z=z;watch.missile=missile;
        if(!missile)watch.seen=false; // Burst: no longer watched.
    }
    for(auto it=missileWatch.begin();it!=missileWatch.end();) it=it->second.seen?std::next(it):missileWatch.erase(it);
}
// The panes as the camera sees them, and the shards. Each pane draws only
// its face toward the eye, carrying that side's light: the outer face shows
// the outdoors' light and reflects the sky, the inner one the room's.
// Vertex: uv the texel on the pane, (sunU, sunV) its size, (red, green) the
// face normal, blue 1 on a face out under the sky; statics the pane's seed,
// top bit set once broken; lightMask[0] 1 on panes in the mirror's plane;
// lightMask[1] 1 on panes out under the sky (rain on them), plus 2 where u
// runs from the pane's far end; both words carry one bullet hole each in
// bits 2..29 (14 bits per coordinate), with bit 30 marking a valid hole;
// lampTint the captured probe on the eye's side plus one (0: none).
// Glass reflections (settings.glassReflections): the pane the eye looks at
// becomes the mirror the world is drawn again through (glassMirrorPlane:
// its normal toward the eye and offset; panes in the same plane share it).
// That is the pane the view's center ray meets, or else the one whose
// nearest point to that ray lies closest to it in angle, within 1024 units,
// whole, facing the eye, in view and not behind a wall. So a window is live
// already while the eye walks up to it. The mirror only moves to another
// plane once that one is clearly nearer the center (hysteresis), and
// glassMirrorFade then brings it in from its probe over a quarter second.
static float lastPlane[4];
static bool lastActive=false;
static uint64_t mirrorSince=0;
static void chooseMirror(const Uniforms &camera) {
    glassMirrorActive=false;
    if(!settings.glassReflections||camera.effects[2]!=0||(profile.skip&I_RENDER3D_SKIP_REFLECTIONS)) {lastActive=false;return;}
    const float *e=camera.eye,*f=camera.forward;
    float best=1e9f,kept=1e9f,bestPlane[4]={};
    for(const GlassPane &p:panes) {
        if(p.detached||(p.broken&&leveltime>p.fractureEnd))continue;
        float bottom,top;paneHeights(p,bottom,top);
        float ex=p.bx-p.ax,ey=p.by-p.ay,len=std::hypot(ex,ey);
        if(top-bottom<4||len<4)continue;
        float nx=ey/len,ny=-ex/len,facing=(e[0]-p.ax)*nx+(e[1]-p.ay)*ny;
        if(facing<0) {nx=-nx;ny=-ny;facing=-facing;}
        if(facing<1)continue;
        // Where the center ray meets the pane's plane, clamped onto the
        // pane, 2 units inside its frame.
        float toward=-(f[0]*nx+f[1]*ny),hit[3];
        if(toward>1e-3f) {float t=facing/toward;for(int i=0;i<3;++i)hit[i]=e[i]+f[i]*t;}
        else {hit[0]=p.ax+ex*0.5f;hit[1]=p.ay+ey*0.5f;hit[2]=(bottom+top)/2;}
        float along=std::clamp(((hit[0]-p.ax)*ex+(hit[1]-p.ay)*ey)/len,2.0f,len-2);
        float px=p.ax+ex/len*along,py=p.ay+ey/len*along,pz=std::clamp(hit[2],bottom+2,top-2);
        float dx=px-e[0],dy=py-e[1],dz=pz-e[2],distance=std::sqrt(dx*dx+dy*dy+dz*dz);
        if(distance>1024||distance<1)continue;
        float angle=std::acos(std::clamp((dx*f[0]+dy*f[1]+dz*f[2])/distance,-1.0f,1.0f));
        if(angle>0.9f)continue; // Off screen, about.
        float flat=std::hypot(dx,dy);WallHit wall;
        if(flat>1&&firstSolidWall(e[0],e[1],e[2],dx/flat,dy/flat,dz/flat,flat-1,wall))continue;
        float plane[4]={nx,ny,0,nx*p.ax+ny*p.ay};
        bool same=lastActive&&nx*lastPlane[0]+ny*lastPlane[1]>0.999f&&std::abs(plane[3]-lastPlane[3])<1;
        if(same)kept=std::min(kept,angle);
        if(angle<best) {best=angle;std::copy(plane,plane+4,bestPlane);}
    }
    if(best>=1e9f) {lastActive=false;return;}
    glassMirrorActive=true;
    if(kept<1e9f&&kept<best+0.1f) std::copy(lastPlane,lastPlane+4,glassMirrorPlane);
    else {
        std::copy(bestPlane,bestPlane+4,glassMirrorPlane);
        std::copy(bestPlane,bestPlane+4,lastPlane);mirrorSince=SDL_GetTicksNS();
    }
    if(!lastActive)mirrorSince=SDL_GetTicksNS();
    lastActive=true;
    glassMirrorFade=std::clamp((SDL_GetTicksNS()-mirrorSince)/0.25e9f,0.0f,1.0f);
}
// Which probes to capture this frame: those not yet captured since the
// level's bakes or the settings last changed, four at a time so a level's
// worth spreads over a few frames, and only once the bakes switched on have
// all finished, so a level is captured once rather than per bake. The
// capture key covers what changes how the world looks lit.
static int captureKey=-1;
static std::vector<char> captured;
bool planGlassCaptures(const Uniforms &camera) {
    glassCaptures.clear();
    if(!settings.windowGlass||glassLevelSerial!=r_levelserial||glassProbes.empty()||camera.effects[2]!=0)return false;
    // The loading pass's frame is not drawn, so it captures nothing.
    if(bakesPending()||loadingOnThisThread())return false;
    int key=(int)hash32(lightBakeSerial*31u+(unsigned)bounceKey*7u+(unsigned)ambientKey*13u+(unsigned)flowKey*17u+(sunBaked?1u:0u)+
                        (unsigned)skytexture*19u+settingsSerial*23u+glassProbeSerial*29u);
    if(key!=captureKey||captured.size()!=glassProbes.size()) {captureKey=key;captured.assign(glassProbes.size(),0);}
    for(size_t i=0;i<glassProbes.size()&&glassCaptures.size()<4;++i)
        if(!captured[i]) {captured[i]=1;glassCaptures.push_back((int)i);}
    return !glassCaptures.empty();
}
void buildGlass(const Uniforms &camera) {
    glassVertices.clear();glassMirrorActive=false;
    if(!settings.windowGlass||glassLevelSerial!=r_levelserial)return;
    struct Draw {size_t first,count;float depth;};
    std::vector<Draw> draws;
    auto record=[&](size_t first,float x,float y,float z) {
        draws.push_back({first,glassVertices.size()-first,(x-camera.eye[0])*camera.forward[0]+
            (y-camera.eye[1])*camera.forward[1]+(z-camera.eye[2])*camera.forward[2]});
    };
    chooseMirror(camera);
    for(const GlassPane &p:panes) {
        if(p.detached)continue;
        float bottom,top;paneHeights(p,bottom,top);
        float ex=p.bx-p.ax,ey=p.by-p.ay,len=std::hypot(ex,ey);
        if(top-bottom<1||len<1)continue;
        float nx=ey/len,ny=-ex/len; // Toward the line's front side.
        float facing=(camera.eye[0]-p.ax)*nx+(camera.eye[1]-p.ay)*ny;
        if(std::abs(facing)<0.01f)continue;
        if(facing<0) {nx=-nx;ny=-ny;}
        bool frontEye=facing>0;
        const sector_t *eyeSide=frontEye?lines[p.line].frontsector:lines[p.line].backsector;
        bool outside=eyeSide-sectors==p.outer;
        const sector_t *lit=outside?&sectors[p.outer]:p.inner>=0?&sectors[p.inner]:&sectors[p.frame];
        float light=camera.effects[2]!=0?1:lighting(lit);
        // u runs along (-ny, nx), so the shader knows the pane's direction.
        Point a={p.ax,p.ay},b={p.bx,p.by};
        bool flipped=(b.x-a.x)*-ny+(b.y-a.y)*nx<0;
        if(flipped)std::swap(a,b);
        // Rain gathers on panes looking out under the sky, drawn the same
        // from both sides: u from the pane's first end (flipped: from b).
        unsigned rain=(skySector(&sectors[p.outer])?1u:0u)|(flipped?2u:0u);
        unsigned statics=(p.seed&0x7fffffffu)|(p.broken?0x80000000u:0u);
        bool mirror=glassMirrorActive&&nx*glassMirrorPlane[0]+ny*glassMirrorPlane[1]>0.999f&&std::abs(nx*p.ax+ny*p.ay-glassMirrorPlane[3])<1;
        int probe=p.probe[outside?0:1];
        unsigned probeId=probe>=0&&(size_t)probe<captured.size()&&captured[probe]?(unsigned)probe+1:0;
        auto hole=[&](int index) {
            if(index>=p.hits)return 0u;
            unsigned u=(unsigned)std::clamp((int)std::floor(p.holes[index].x),0,16383);
            unsigned v=(unsigned)std::clamp((int)std::floor(p.holes[index].y),0,16383);
            return (1u<<30)|(u<<2)|(v<<16);
        };
        auto vertex=[&](Point at,float u,float z) {
            Vertex v={at.x,at.y,z,u,z-bottom,light,0};v.lightMask[0]=(mirror?1u:0u)|hole(0);v.lightMask[1]=rain|hole(1);v.lampTint=probeId;
            v.red=nx;v.green=ny;v.blue=outside&&skySector(&sectors[p.outer])?1.0f:0.0f;
            v.sunU=len;v.sunV=top-bottom;v.statics=statics;
            // Suppress the rim's fresh broken edge until the fracture has
            // crossed the pane, so the whole perimeter cannot flash at once.
            if(p.broken&&leveltime<=p.fractureEnd)v.mode=MODE_GLASS_HELD;
            return v;
        };
        size_t first=glassVertices.size();
        quad(glassVertices,vertex(a,0,top),vertex(b,len,top),vertex(b,len,bottom),vertex(a,0,bottom));
        // While the fracture travels through the pane, exact, stationary
        // polygons retain its surface. They become tumbling pixel shards
        // only after the front reaches them, leaving the original rim.
        auto heldVertex=[&](doom_glass::Point at,unsigned mode) {
            Point world={p.ax+(p.bx-p.ax)*at.x/len,p.ay+(p.by-p.ay)*at.x/len};
            Vertex v=vertex(world,flipped?len-at.x:at.x,bottom+at.y);
            v.mode=mode;v.statics=p.seed&0x7fffffffu;
            return v;
        };
        for(const Shard &s:shards)if(s.pane==(int)(&p-panes.data())&&leveltime<=s.release) {
            auto at=[&](const doom_glass::Point &v) {return doom_glass::Point{s.center.x+v.x,s.center.y+v.y};};
            for(size_t i=1;i+1<s.outline.size();++i) {
                glassVertices.push_back(heldVertex(at(s.outline[0]),MODE_GLASS_HELD));
                glassVertices.push_back(heldVertex(at(s.outline[i]),MODE_GLASS_HELD));
                glassVertices.push_back(heldVertex(at(s.outline[i+1]),MODE_GLASS_HELD));
            }
        }
        // Reveal each polygon edge only as the front from the LAST hit
        // reaches it. Clipping the segment, rather than dissolving a circle,
        // makes cracks grow through the flat glass before pieces let go.
        float radius=std::max(0.0f,leveltime-p.fractureAt-1+renderFraction)*fractureSpeed;
        auto crackEdge=[&](doom_glass::Point c,doom_glass::Point d) {
            float dx=d.x-c.x,dy=d.y-c.y,aa=dx*dx+dy*dy;
            float ox=c.x-p.impact.x,oy=c.y-p.impact.y,bb=ox*dx+oy*dy;
            float disc=bb*bb-aa*(ox*ox+oy*oy-radius*radius);
            if(aa<1e-6f||disc<=0)return;
            float start=std::max(0.0f,(-bb-std::sqrt(disc))/aa),stop=std::min(1.0f,(-bb+std::sqrt(disc))/aa);
            if(stop<=start)return;
            d={c.x+dx*stop,c.y+dy*stop};c={c.x+dx*start,c.y+dy*start};
            // Whole texels, merging horizontal runs: the fracture's edges
            // have the same pixel steps as the first two bullet cracks.
            int steps=std::max(1,(int)std::ceil(std::max(std::abs(d.x-c.x),std::abs(d.y-c.y))));
            int row=INT_MIN,left=0,right=0;
            auto run=[&]() {
                if(row==INT_MIN)return;
                auto strip=[&](int x,int y,unsigned mode) {
                    quad(glassVertices,heldVertex({(float)(left+x),(float)(row+y)},mode),heldVertex({(float)(right+1+x),(float)(row+y)},mode),
                        heldVertex({(float)(right+1+x),(float)(row+1+y)},mode),heldVertex({(float)(left+x),(float)(row+1+y)},mode));
                };
                strip(1,-1,MODE_GLASS_CRACK|MODE_GLASS_DARK);strip(0,0,MODE_GLASS_CRACK);
            };
            for(int i=0;i<=steps;++i) {
                float t=i/(float)steps;
                int x=(int)std::floor(c.x+(d.x-c.x)*t),y=(int)std::floor(c.y+(d.y-c.y)*t);
                if(y!=row) {run();row=y;left=right=x;}
                else {left=std::min(left,x);right=std::max(right,x);}
            }
            run();
        };
        for(const Shard &s:shards)if(s.pane==(int)(&p-panes.data())&&leveltime<=s.release) {
            for(size_t i=0;i<s.outline.size();++i) {
                doom_glass::Point c={s.center.x+s.outline[i].x,s.center.y+s.outline[i].y};
                auto end=s.outline[(i+1)%s.outline.size()];
                crackEdge(c,{s.center.x+end.x,s.center.y+end.y});
                // Spokes start at the actual puncture and join the fracture
                // network, so the break visibly begins at the last bullet.
                if(s.impactPiece)crackEdge(p.impact,c);
            }
        }
        record(first,(a.x+b.x)/2,(a.y+b.y)/2,(bottom+top)/2);
    }
    // Large pieces keep their shape and tumble, but their silhouettes are
    // cut from whole texels like the broken glass left in the window frame.
    for(const Shard &s:shards) {
        if(leveltime<=s.release)continue;
        float f=renderFraction,x=s.px+(s.x-s.px)*f,y=s.py+(s.y-s.py)*f,z=s.pz+(s.z-s.pz)*f;
        const sector_t *sector=R_PointInSubsector((fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT))->sector;
        float light=camera.effects[2]!=0?1:lighting(sector);
        std::array<float,3> angle;
        for(int i=0;i<3;++i)angle[i]=s.previous[i]+(s.angle[i]-s.previous[i])*f;
        ShardAxes axes=shardAxes(angle);
        float low,high;shardHeights(s,axes,low,high);
        if(high-low>rawCeiling(sector)-rawFloor(sector)-0.4f) {
            angle[1]=doomPi/2+std::round((angle[1]-doomPi/2)/doomPi)*doomPi;
            axes=shardAxes(angle);low=high=0;
        }
        // Rotation changes the lowest corner nonlinearly. Keep interpolated
        // faces above the floor while a last bounce settles them flat.
        z=std::max(z,rawFloor(sector)-low+0.2f);
        z=std::min(z,rawCeiling(sector)-high-0.2f);
        const GlassPane &p=panes[s.pane];
        float side=(camera.eye[0]-p.ax)*(p.by-p.ay)-(camera.eye[1]-p.ay)*(p.bx-p.ax);
        bool outside=(side>0?lines[p.line].frontsector:lines[p.line].backsector)==&sectors[p.outer];
        int probe=p.probe[outside?0:1];
        unsigned probeId=!s.landed&&probe>=0&&(size_t)probe<captured.size()&&captured[probe]?(unsigned)probe+1:0;
        float fade=std::clamp((s.die-leveltime-f)/(float)TICRATE,0.0f,1.0f);
        float cell=s.pixels.cell;
        // Neighbour row limits let the shader paint the exposed pixel edge
        // without drawing a fine straight outline around the original mesh.
        auto encoded=[&](float value) {return (unsigned)std::clamp((int)std::lround(value/cell)+128,0,255);};
        unsigned neighbours=0,row=0;
        auto vertex=[&](float u,float v) {
            Vertex result={x+axes.u[0]*u+axes.v[0]*v,y+axes.u[1]*u+axes.v[1]*v,
                           z+axes.u[2]*u+axes.v[2]*v,u,v,light,MODE_GLASS_SHARD|(s.landed?MODE_GROUNDED:0u)};
            result.red=axes.normal[0];result.green=axes.normal[1];result.blue=axes.normal[2];
            result.lightMask[0]=neighbours;result.lightMask[1]=row|(sector->ceilingpic==skyflatnum?1u:0u);
            result.sunU=cell;result.sunV=fade;result.statics=s.seed;result.lampTint=probeId;return result;
        };
        size_t first=glassVertices.size();
        const auto &bands=s.pixels.bands;
        for(size_t i=0;i<bands.size();++i) {
            const auto &band=bands[i];
            unsigned belowLeft=128,belowRight=128,aboveLeft=128,aboveRight=128;
            if(i>0&&std::abs(bands[i-1].top-band.bottom)<0.01f) {
                belowLeft=encoded(bands[i-1].left);belowRight=encoded(bands[i-1].right);
            }
            if(i+1<bands.size()&&std::abs(bands[i+1].bottom-band.top)<0.01f) {
                aboveLeft=encoded(bands[i+1].left);aboveRight=encoded(bands[i+1].right);
            }
            neighbours=belowLeft|(belowRight<<8)|(aboveLeft<<16)|(aboveRight<<24);
            row=(encoded(band.left)<<8)|(encoded(band.right)<<16);
            quad(glassVertices,vertex(band.left,band.top),vertex(band.right,band.top),
                 vertex(band.right,band.bottom),vertex(band.left,band.bottom));
        }
        record(first,x,y,z);
    }
    // Transparent faces must blend from far to near, including unbroken
    // windows behind or in front of the flying pieces.
    std::sort(draws.begin(),draws.end(),[](const Draw &a,const Draw &b){return a.depth>b.depth;});
    static std::vector<Vertex> ordered;
    ordered.clear();ordered.reserve(glassVertices.size());
    for(const Draw &draw:draws)ordered.insert(ordered.end(),glassVertices.begin()+draw.first,glassVertices.begin()+draw.first+draw.count);
    glassVertices.swap(ordered);
}
unsigned windowPaneSerial() {return glassLevelSerial==r_levelserial?glassLevelSerial:~0u;}
int windowPaneCount() {return glassLevelSerial==r_levelserial?(int)panes.size():0;}
bool windowPane(int index,PaneView &out) {
    if(index<0||index>=windowPaneCount())return false;
    const GlassPane &p=panes[index];
    const line_t &line=lines[p.line];
    float ex=p.bx-p.ax,ey=p.by-p.ay,len=std::hypot(ex,ey);
    if(len<1)return false;
    float nx=ey/len,ny=-ex/len; // Toward the line's front side...
    if(line.frontsector==&sectors[p.outer]) {nx=-nx;ny=-ny;} // ...turned to point in from the outer side.
    out={p.line,p.frame,p.outer,p.inner,p.ax,p.ay,p.bx,p.by,0,0,nx,ny,p.broken,skySector(&sectors[p.outer]),p.seed};
    paneHeights(p,out.bottom,out.top);
    return true;
}
void glassShutdown() {glassProbes.clear();glassCaptures.clear();captured.clear();captureKey=-1;panes.clear();paneOfLine.clear();nearLines.clear();shards.clear();missileWatch.clear();glassVertices.clear();glassLevelSerial=~0u;}
} // namespace doom3d
