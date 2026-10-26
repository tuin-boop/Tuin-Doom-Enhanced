/* Bullet impacts and explosions: wall and floor decals, scorches and
   debris. */
#include "internal.h"
#include "p_digwall.h"
extern "C" {
#include "p_solid.h"
}
#include "../../../map_world.h"
extern const lab::MapWorld *SV_MapWorld();

namespace doom3d {
// Hitscan wall impacts leave fading bullet marks and a short burst of debris
// tinted by the wall texture's average color. Render-only: no game state,
// own random numbers, cleared on level changes and loads.
struct WallDecal { int line,side,section; float along,dz,born,size; bool flip; };
struct Scorch { float x,y,born,size;int sector; };
static std::vector<WallDecal> wallDecals;
std::vector<FlatDecal> flatDecals;
std::vector<Debris> debris;
static std::vector<Scorch> scorches;
static unsigned decalLevelSerial;
std::minstd_rand impactRandom(1993);
// Impacts from a previous level go once the engine has moved on.
void dropOldLevelImpacts() {
    if(decalLevelSerial!=r_levelserial){wallDecals.clear();flatDecals.clear();debris.clear();scorches.clear();decalLevelSerial=r_levelserial;}
}
// A block size map units across (tall times as high), but never under one
// Doom pixel, set on the Doom-pixel grid at its depth: a pixel of an old
// sprite rather than a soft speck. The color is lit, then put in the palette
// (unless palette is false: liquid crests match the smoothly shaded water).
// With taken, a block whose Doom-pixel corner is already in it is skipped.
// A resting block stands on the point instead of centering on it, so a floor
// does not swallow half of it. Given its unlit color, the flashlight beam
// lights it too.
std::unordered_set<uint64_t> crestCells;
void pixelBlock(std::vector<Vertex> &out,const Uniforms &camera,float x,float y,float z,float size,float tall,const std::array<float,3> &color,bool resting,
                const std::array<float,3> *albedo,bool palette,std::unordered_set<uint64_t> *taken,float alpha) {
    float d[3]={x-camera.eye[0],y-camera.eye[1],z-camera.eye[2]};
    float depth=d[0]*camera.forward[0]+d[1]*camera.forward[1]+d[2]*camera.forward[2];
    if(depth<4)return;
    float unit=depth/(100*camera.projection[1]),across=100*camera.projection[1]/camera.projection[0]; // Map units per Doom pixel; half the view in Doom pixels.
    float px=(d[0]*camera.right[0]+d[1]*camera.right[1]+d[2]*camera.right[2])/unit;
    float py=(d[0]*camera.up[0]+d[1]*camera.up[1]+d[2]*camera.up[2])/unit;
    float w=std::max(1.0f,std::round(size/unit)),h=std::max(1.0f,std::round(size*tall/unit));
    float left=std::round(px+across-w*0.5f)-across,bottom=std::round(py+100-(resting?0:h*0.5f))-100;
    if(taken&&!taken->insert((uint64_t)(uint32_t)(int32_t)std::lround(left)<<32|(uint32_t)(int32_t)std::lround(bottom)).second)return;
    std::array<float,3> shade=color;
    if(albedo&&flashlightOn&&flashes.count&&flashes.lights[0].direction[3]>0&&camera.effects[2]==0) {
        const Flash &beam=flashes.lights[0];
        float amount=flashAt(x,y,z,beam);
        if(amount>0)for(int c=0;c<3;++c)shade[c]=std::min(1.0f,shade[c]+(*albedo)[c]*amount*beam.color[c]);
    }
    auto lit=palette?paletteColor(shade):shade;
    auto vertex=[&](float sx,float sz) {
        float r=(sx-px)*unit,u=(sz-py)*unit;
        Vertex v={x+camera.right[0]*r+camera.up[0]*u,y+camera.right[1]*r+camera.up[1]*u,z+camera.right[2]*r+camera.up[2]*u,0,0,alpha,0};
        v.red=lit[0];v.green=lit[1];v.blue=lit[2];return v;
    };
    quad(out,vertex(left,bottom+h),vertex(left+w,bottom+h),vertex(left+w,bottom),vertex(left,bottom));
}
// Section 0 is a one-sided wall (anchored to its floor), 1 an upper wall
// (anchored to the back ceiling, e.g. a door face), 2 a lower wall.
bool wallSpan(const line_t &line,int side,int section,WallSpan &span) {
    if(line.sidenum[side]<0)return false;
    const side_t &s=sides[line.sidenum[side]];
    const sector_t *front=s.sector,*back=side?line.frontsector:line.backsector;
    span.a={units((side?line.v2:line.v1)->x),units((side?line.v2:line.v1)->y)};
    span.b={units((side?line.v1:line.v2)->x),units((side?line.v1:line.v2)->y)};
    if(section==0) {if(back)return false;span.bottom=floorZ(front);span.top=ceilZ(front);span.anchor=span.bottom;span.texture=s.midtexture;}
    else if(!back) return false;
    else if(section==1) {span.bottom=ceilZ(back);span.top=ceilZ(front);span.anchor=span.bottom;span.texture=s.toptexture;}
    else {span.bottom=floorZ(front);span.top=floorZ(back);span.anchor=span.top;span.texture=s.bottomtexture;}
    return span.top>span.bottom;
}
#include "impact_particles.inc"
#include "surface_glow.inc"
#include "fluid_effects.inc"
#include "environment_reactions.inc"

void wallImpact(line_t *line,int side,fixed_t fx,fixed_t fy,fixed_t fz) {
    if(!line||gamestate!=GS_LEVEL)return;
    dropOldLevelImpacts();
    float x=units(fx),y=units(fy),z=units(fz);
    const sector_t *back=side?line->frontsector:line->backsector;
    int section=!back?0:z>=ceilZ(back)?1:z<=floorZ(back)?2:-1;
    WallSpan span;if(section<0||!wallSpan(*line,side,section,span))return;
    float ex=span.b.x-span.a.x,ey=span.b.y-span.a.y,len=std::hypot(ex,ey);if(len<1)return;
    float along=std::clamp(((x-span.a.x)*ex+(y-span.a.y)*ey)/len,0.0f,len);
    float now=leveltime/(float)TICRATE;
    shotWall(*line,side,section,along,x,y,z);
    fluidBulletMark(int(line-lines),side,section,along,z-span.anchor);
    if(!settings.gunImpacts)return;
    if(wallDecals.size()>=192)wallDecals.erase(wallDecals.begin());
    wallDecals.push_back({int(line-lines),side,section,along,z-span.anchor,now,randomRange(5.0f,7.5f),randomRange(0,1)<0.5f});
    if(span.texture<=0)return;
    Image &image=wallImage(texturetranslation[span.texture]);
    float light=lighting(sides[line->sidenum[side]].sector);
    float nx=ey/len,ny=-ex/len,tx=ex/len,ty=ey/len;
    float wx=span.a.x+tx*along+nx*1.5f,wy=span.a.y+ty*along+ny*1.5f;
    uziImpact(wx+nx*2.5f,wy+ny*2.5f,z,nx,ny,0);
}
void flatImpactParticles(float x,float y,float z,bool ceiling,Image &image,float light) {
    uziImpact(x,y,z+(ceiling?-4.f:4.f),0,0,ceiling?-1.f:1.f);
}
// Called only after a door actually moves upward. Emit at the two jambs,
// never across the face; distance, cadence and a global budget bound the work.
void doorMotion(sector_t *sector) {
    if(!settings.doorSparks||gamestate!=GS_LEVEL||!players[displayplayer].mo)return;
    int index=int(sector-sectors);
    if((leveltime+index)%3)return;
    dropOldLevelImpacts();
    const mobj_t *eye=players[displayplayer].mo;
    float px=units(eye->x),py=units(eye->y),now=leveltime/(float)TICRATE;
    static int budgetTic=-1,budget=0;
    if(budgetTic!=leveltime){budgetTic=leveltime;budget=0;}
    for(int i=0;i<sector->linecount&&budget<24;++i) {
        line_t &line=*sector->lines[i];
        int side=line.backsector==sector?0:1;
        sector_t *front=side?line.backsector:line.frontsector;
        if(!front||line.sidenum[side]<0||sides[line.sidenum[side]].toptexture<=0||
           front->ceilingheight<=sector->ceilingheight)continue;
        float ax=units((side?line.v2:line.v1)->x),ay=units((side?line.v2:line.v1)->y);
        float bx=units((side?line.v1:line.v2)->x),by=units((side?line.v1:line.v2)->y);
        float dx=bx-ax,dy=by-ay,len=std::hypot(dx,dy);if(len<8)continue;
        if(dx*(py-ay)-dy*(px-ax)>0)continue;
        float nx=dy/len,ny=-dx/len;
        for(int end=0;end<2&&budget<24;++end) {
            float f=end?1-2/len:2/len;
            float x=ax+dx*f+nx*2,y=ay+dy*f+ny*2;
            if(std::hypot(x-px,y-py)>640)continue;
            for(int n=0;n<2&&debris.size()<512;++n) {
                float out=randomRange(15,45),slide=randomRange(-24,24);
                debris.push_back({x,y,units(sector->ceilingheight)+1,
                    nx*out+dx/len*slide,ny*out+dy/len*slide,randomRange(-12,38),
                    now,randomRange(.16f,.3f),randomRange(.7f,1.2f),{1,.65f,.18f},1});
                ++budget;
            }
        }
    }
}
void doorDamage(line_t *line,int side,fixed_t fx,fixed_t fy,fixed_t fz,int broken) {
    if(!line)return;
    dropOldLevelImpacts();
    WallSpan span;if(!wallSpan(*line,side,1,span))return;
    float dx=span.b.x-span.a.x,dy=span.b.y-span.a.y,len=std::hypot(dx,dy);if(len<1)return;
    float along=std::clamp(((units(fx)-span.a.x)*dx+(units(fy)-span.a.y)*dy)/len,0.0f,len);
    float nx=dy/len,ny=-dx/len,x=span.a.x+dx/len*along+nx*2,y=span.a.y+dy/len*along+ny*2;
    float z=std::clamp(units(fz),span.bottom,span.top),now=leveltime/(float)TICRATE;
    if(wallDecals.size()>=192)wallDecals.erase(wallDecals.begin());
    wallDecals.push_back({int(line-lines),side,1,along,z-span.anchor,now,broken?64.f:36.f,false});
    int count=broken?32:12;
    if(debris.size()+count>512)debris.erase(debris.begin(),debris.begin()+(debris.size()+count-512));
    for(int i=0;i<count;++i) {
        float out=randomRange(60,220),slide=randomRange(-140,140);
        std::array<float,3> color={1,.65f,.18f};
        if(broken&&span.texture>0)color=texelColor(wallImage(texturetranslation[span.texture]));
        float u=broken?randomRange(2,std::max(2.f,len-2)):along;
        float sx=span.a.x+dx/len*u+nx*2,sy=span.a.y+dy/len*u+ny*2;
        float sz=broken?randomRange(span.bottom,span.top):z;
        debris.push_back({sx,sy,sz,nx*out+dx/len*slide,ny*out+dy/len*slide,randomRange(40,160),now,
            broken?1.2f:.4f,broken?randomRange(1.5f,3.f):1.f,color,broken?lighting(sides[line->sidenum[side]].sector):1.f});
    }
}
void solidBulletImpact(float x,float y,float z,float nx,float ny,float nz){
    if(!settings.gunImpacts)return;
    resetImpactLevel();
    // The original wall callback may already have emitted this shot's burst.
    for(const auto& p:gunPuffs)if(p.tic==leveltime){
        float dx=x-p.x,dy=y-p.y,dz=z-p.z;
        if(dx*dx+dy*dy+dz*dz<64)return;
    }
    uziImpact(x+nx*2,y+ny*2,z+nz*2,nx,ny,nz);
}
void surfaceChip(float x,float y,float z,float nx,float ny,float nz){
    dropOldLevelImpacts();
    auto tint=rubbleSurfaceColor(x,y,z);
    float now=leveltime/(float)TICRATE;
    const sector_t *room=R_PointInSubsector(fixed_t(x*FRACUNIT),fixed_t(y*FRACUNIT))->sector;
    for(int i=0;i<3&&debris.size()<512;++i){
        float shade=randomRange(.35f,.6f),speed=randomRange(28,55);
        debris.push_back({x,y,z,nx*speed+randomRange(-10,10),ny*speed+randomRange(-10,10),nz*speed+randomRange(12,28),now,
            .65f,randomRange(.4f,1.f),{shade*tint[0],shade*tint[1],shade*tint[2]},lighting(room)});
    }
}
void craterCrumble(float x,float y,float z){
    environmentBlast(x,y,z,.35f);
    if(settings.chainCollapses&&debris.size()<512){
        auto color=rubbleSurfaceColor(x,y,z);for(float &c:color)c*=.4f;
        debris.push_back({x,y,z,randomRange(-4,4),randomRange(-4,4),-8,gameSeconds(),3.5f,3.8f,color});
    }
    dropOldLevelImpacts();
    auto tint=rubbleSurfaceColor(x,y,z);
    float now=leveltime/(float)TICRATE;
    const sector_t *room=R_PointInSubsector(fixed_t(x*FRACUNIT),fixed_t(y*FRACUNIT))->sector;
    for(int i=0;i<3&&debris.size()<512;++i){
        float shade=randomRange(.35f,.6f);
        debris.push_back({x,y,z,randomRange(-18,18),randomRange(-18,18),randomRange(-25,5),now,
            1.3f,randomRange(1.5f,3.f),{shade*tint[0],shade*tint[1],shade*tint[2]},lighting(room)});
    }
}
void barrelBlast(const mobj_t &thing,float x,float y,float z) {
    dropOldLevelImpacts();
    environmentBlast(x,y,z+24,1);
    glassBlast(x,y,z+24,128);
    blastLights(x,y,z+24,128);
    float now=gameSeconds();
    if(scorches.size()>=48)scorches.erase(scorches.begin());
    scorches.push_back({x,y,now,randomRange(60,76),int(thing.subsector->sector-sectors)});
    if(debris.size()>480)debris.erase(debris.begin(),debris.begin()+(debris.size()-480));
    for(int i=0;i<32;++i) {
        float angle=randomRange(0,2*doomPi),speed=randomRange(60,220),heat=randomRange(0,1);
        debris.push_back({x,y,z+randomRange(8,32),std::cos(angle)*speed,std::sin(angle)*speed,randomRange(80,260),now,
            randomRange(0.6f,1.3f),randomRange(1.0f,2.2f),{1.0f,0.45f+0.5f*heat,0.08f+0.3f*heat*heat}});
    }
}
// A square decal on a sector's floor or ceiling, clipped to its leaves.
static void flatDecal(int sectorIndex,float x,float y,float h,float z,int lump,const Image &image,float fade,bool flip) {
    std::vector<Point> square={{x-h,y-h},{x+h,y-h},{x+h,y+h},{x-h,y+h}};
    for(int leaf:sectorFloors[sectorIndex]) {
        const auto &b=floorBounds[leaf];
        if(b[2]<x-h||b[0]>x+h||b[3]<y-h||b[1]>y+h)continue;
        auto polygon=floors[leaf];
        for(size_t n=0;n<square.size()&&!polygon.empty();++n) {
            Point a=square[n],c=square[(n+1)%square.size()];
            polygon=clip(polygon,a,{c.x-a.x,c.y-a.y},false);
        }
        auto vertex=[&](Point p){float u=(p.x-x+h)/(2*h);return Vertex{p.x,p.y,z,(flip?1-u:u)*image.width,(y+h-p.y)/(2*h)*image.height,fade,0};};
        for(size_t n=1;n+1<polygon.size();++n)
            for(Point p:{polygon[0],polygon[n],polygon[n+1]})decalBatches[lump].push_back(vertex(p));
    }
}
// The game's sprite for an effect role; no frames when the game has none.
static const spritedef_t &effectSprite(game_sprite_role role) {
    static const spritedef_t none={0,nullptr};
    int sprite=Game_Sprite(role);
    return sprite>=0&&sprite<numsprites?sprites[sprite]:none;
}
void buildImpacts(const Uniforms &camera) {
    for(auto &batch:decalBatches)batch.second.clear();
    particleVertices.clear();crestVertices.clear();crestCells.clear();
    dropOldLevelImpacts();
    float now=gameSeconds();
    scorches.erase(std::remove_if(scorches.begin(),scorches.end(),[&](const Scorch &d){return now-d.born>60||now<d.born;}),scorches.end());
    const auto &blast=effectSprite(GAME_SPRITE_EXPLOSION);
    if(blast.numframes>2&&!scorches.empty()) {
        // Scorches reuse a fireball frame's coverage, clipped to the sector floor.
        int lump=firstspritelump+blast.spriteframes[2].lump[0];Image &image=lumpImage(lump,false);
        for(const auto &d:scorches) {
            const sector_t &sector=sectors[d.sector];if(sector.floorpic==skyflatnum)continue;
            float fade=std::clamp((60-(now-d.born))/8,0.0f,1.0f)*0.7f;
            flatDecal(d.sector,d.x,d.y,d.size*0.5f,floorZ(&sector)+0.08f,lump,image,fade,false);
        }
    }
    wallDecals.erase(std::remove_if(wallDecals.begin(),wallDecals.end(),[&](const WallDecal &d){return now-d.born>40||now<d.born;}),wallDecals.end());
    flatDecals.erase(std::remove_if(flatDecals.begin(),flatDecals.end(),[&](const FlatDecal &d){return now-d.born>40||now<d.born;}),flatDecals.end());
    // Do not reuse Doom's grey PUFF sprite as a persistent impact decal.
    // The imported Uzi flash and voxel dust are the complete surface effect.
    debris.erase(std::remove_if(debris.begin(),debris.end(),[&](const Debris &p){return now-p.born>p.life||now<p.born;}),debris.end());
    drawImpactParticles(camera);
    drawFluidEffects(camera);
    drawSurfaceGlows();
    buildEnvironment(camera);
    buildElectricalArcs(camera);
    // Debris moves at the tic rate, like the rings, stretches upright while
    // it falls fast and shrinks once before it is gone.
    for(auto &p:debris) {
        float t=std::floor((now-p.born)*TICRATE)/TICRATE,x=p.x+p.vx*t,y=p.y+p.vy*t,z=p.z+p.vz*t-210*t*t;
        const sector_t *sector=R_PointInSubsector((fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT))->sector;
        float floor=floorZ(sector)+0.2f;
        size_t index=sector-sectors;
        if(z<floor&&index<sectorMist.size()&&sectorMist[index].liquid){
            if(settings.environmentEffects&&settings.debrisLiquids){
                if(!p.liquidHit){debrisLiquidSplash(x,y,floor,int(index),p.size);p.liquidHit=true;}
                if(p.size>1.2f)continue; // Heavy rubble sinks; small flecks linger.
            }else if(!sectorMist[index].thick)continue;
        }
        z=std::min(z,ceilZ(sector)-p.size*0.5f);
        float size=t>p.life*0.75f?p.size*0.5f:p.size,tall=std::abs(p.vz-420*t)>220?2.0f:1.0f;
        std::array<float,3> lit;for(int c=0;c<3;++c)lit[c]=std::min(1.0f,p.color[c]*p.light);
        pixelBlock(particleVertices,camera,x,y,std::max(z,floor),size,tall,lit,z<=floor+p.size*0.5f,&p.color);
    }
}
// The first wall a ray from (x,y,z) along the unit horizontal (dx,dy) meets
// within length, climbing dz per unit: one-sided lines, and two-sided ones
// where the ray passes above the far ceiling or below the far floor, at the
// current heights. side faces the ray's origin; section is as in wallSpan.
bool firstSolidWall(float x,float y,float z,float dx,float dy,float dz,float length,WallHit &hit) {
    std::vector<std::pair<float,int>> crossings;
    for(int i=0;i<numlines;++i) {
        const line_t &line=lines[i];
        float ax=units(line.v1->x),ay=units(line.v1->y),ex=units(line.v2->x)-ax,ey=units(line.v2->y)-ay;
        float det=dx*ey-dy*ex;if(std::abs(det)<1e-6f)continue;
        float ox=ax-x,oy=ay-y,t=(ox*ey-oy*ex)/det,u=(ox*dy-oy*dx)/det;
        if(t>0.01f&&t<length&&u>=0&&u<=1)crossings.push_back({t,i});
    }
    std::sort(crossings.begin(),crossings.end());
    for(auto [t,i]:crossings) {
        const line_t &line=lines[i];
        float ax=units(line.v1->x),ay=units(line.v1->y),ex=units(line.v2->x)-ax,ey=units(line.v2->y)-ay;
        int side=ex*(y-ay)-ey*(x-ax)>=0?1:0;
        const sector_t *front=side?line.backsector:line.frontsector,*back=side?line.frontsector:line.backsector;
        if(!front)continue;
        float at=z+dz*t;int section=-1;
        if(!back)section=0;
        else if(at>=ceilZ(back)&&!(front->ceilingpic==skyflatnum&&back->ceilingpic==skyflatnum))section=1;
        else if(at<=floorZ(back))section=2;
        if(section<0)continue;
        hit={i,side,section,t};return true;
    }
    return false;
}
float valueNoise(float x,float y,float cell,uint32_t seed) {
    x/=cell;y/=cell;int ix=(int)std::floor(x),iy=(int)std::floor(y);
    float fx=x-ix,fy=y-iy;fx=fx*fx*(3-2*fx);fy=fy*fy*(3-2*fy);
    float a=hashUnit(ix,iy,seed),b=hashUnit(ix+1,iy,seed),c=hashUnit(ix,iy+1,seed),d=hashUnit(ix+1,iy+1,seed);
    float top=a+(b-a)*fx,bottom=c+(d-c)*fx;
    return top+(bottom-top)*fy;
}
void impactsShutdown() {resetEnvironment();fluidParticles.clear();fluidBursts.clear();fluidMarks.clear();wallDecals.clear();flatDecals.clear();debris.clear();scorches.clear();impactParticles.clear();gunPuffs.clear();}
} // namespace doom3d

#ifdef DOOM_TEST_DRIVER
#include "../settings_panel.h"
extern "C" void I_TestImpactOptions() {
    using namespace doom3d;
    // A solid boundary must reflect debris; an open doorway must admit it.
    {sector_t fixture[2]={};sector_t *original=sectors;
     vertex_t a={},b={};a.x=b.x=FRACUNIT;a.y=-10*FRACUNIT;b.y=10*FRACUNIT;
     line_t edge={};edge.v1=&a;edge.v2=&b;edge.dy=20*FRACUNIT;edge.frontsector=&fixture[0];
     line_t *boundary=&edge;for(auto &sec:fixture){sec.ceilingheight=64*FRACUNIT;sec.linecount=1;sec.lines=&boundary;}
     sectors=fixture;
     ImpactParticle p={};p.z=p.oz=32;p.vx=100;p.life=1;
     moveImpactParticle(p,.02f);
     if(p.x>=1||p.vx>=0)I_Error("Impact fragment crossed solid wall");
     p={};p.z=32;p.vx=100;p.life=1;edge.backsector=&fixture[1];
     moveImpactParticle(p,.02f);
     if(p.sector!=1||p.x<1.9f)I_Error("Impact fragment did not cross open portal");
     p={};p.z=32;p.vx=100;p.life=1;fixture[1].ceilingheight=16*FRACUNIT;
     moveImpactParticle(p,.02f);
     if(p.x>=1||p.vx>=0)I_Error("Impact fragment crossed closed door");
     sectors=original;}
    for(const auto &mask:fluidMasks){bool clear=false,covered=false;for(int i=0;i<mask.w*mask.h;++i){clear|=mask.alpha[i]==0;covered|=mask.alpha[i]>0;}if(!clear||!covered)I_Error("Fluid mask lost transparency");}
    resetFluidEffects();
    auto saveFluidParticles=fluidParticles;auto saveFluidMarks=fluidMarks;auto saveFluidBursts=fluidBursts;
    int enabled=settings.fluidWeapons,rocketFire=settings.fluidRocketFire;
    fluidParticles.clear();fluidMarks.clear();fluidBursts.clear();settings.fluidWeapons=1;
    for(int style=0;style<2;++style)for(auto type:{MT_ROCKET,MT_PLASMA,MT_BFG}){
        settings.fluidRocketFire=style;
        mobj_t missile=*players[displayplayer].mo;missile.type=type;missile.target=players[displayplayer].mo;
        missile.flags=MF_MISSILE;missile.momx=20*FRACUNIT;missile.momy=0;missile.momz=8*FRACUNIT;
        missile.z=missile.floorz;missile.info=&mobjinfo[type];
        size_t count=fluidParticles.size();fluidProjectileFlight(missile);
        if(fluidParticles.size()<=count)I_Error("Missing Fluid projectile trail");
        if(type==MT_ROCKET){
            int fire=0;for(size_t i=count;i<fluidParticles.size();++i){
                const auto &p=fluidParticles[i];
                if(p.mask==13)I_Error("Original default disables flight smoke");
                if(std::abs(p.halfSize-2.5f/7.f)>.0001f||p.fadeTics!=p.life-1)I_Error("Original rocket particle scale/fade mismatch");
                if(p.mask==(style?14:15))++fire;
            }
            // Soft glow sparks share mask 12; the flare and four trail particles come first.
            if(fire<5||fire>6)I_Error("Rocket flare/trail style missing");
            if(missile.z!=missile.floorz||missile.momz!=8*FRACUNIT)I_Error("Rocket fire style changed projectile height or velocity");
            const auto &trail=fluidParticles[count+1];
            for(size_t i=count+1;i<count+5;++i){const auto &p=fluidParticles[i];
                if(p.life!=5||p.size!=25||p.growth!=-5||p.color!=trail.color)I_Error("Rocket differs from original Fluid trail timing/color");}

        }
        size_t marks=fluidMarks.size();fluidProjectileImpact(&missile);
        if(fluidBursts.empty())I_Error("Missing Fluid projectile impact");
        if(missile.subsector->sector->floorpic!=skyflatnum&&fluidMarks.size()<=marks)I_Error("Missing Fluid projectile scorch");
    }
    for(int i=0;i<80;++i)tickFluidEffects();
    if(!fluidParticles.empty()||!fluidBursts.empty())I_Error("Fluid projectile effects failed to expire");
    fluidParticles.clear();fluidBursts.clear();
    fluidRocketLaunch(players[displayplayer].mo);
    if(!fluidParticles.empty())I_Error("Non-original muzzle trail returned");
    fluidBursts.push_back({0,0,0,MT_ROCKET,0});
    tickFluidEffects();tickFluidEffects();
    int smoke=0,sparks=0;
    for(const auto &p:fluidParticles){
        if(!p.add){++smoke;if(p.color[0]!=30/255.f||p.life<20||p.life>30||p.growth<4.4f||p.growth>5)I_Error("Original impact smoke parameters differ");}
        if(p.fadeTics<0){++sparks;if(!p.add||p.growth>=0||p.life<36||p.life>39)I_Error("Original shrinking impact sparks differ");}
    }
    if(smoke!=2||sparks<30||sparks>40)I_Error("Original impact emission cadence differs");
    fluidParticles.clear();fluidBursts.clear();
    puts("PASS original rocket sizing, fade, shrinking sparks, smoke cadence and no extra muzzle trail");
    // Exercise a real map corner: the next face must share the same UV origin.
    bool wrapped=false;
    for(int i=0;i<numlines&&!wrapped;++i)for(int side=0;side<2&&!wrapped;++side)for(int part=0;part<3&&!wrapped;++part){
        WallSpan span;if(!wallSpan(lines[i],side,part,span)||span.texture<=0)continue;
        fluidMarks.clear();float len=std::hypot(span.b.x-span.a.x,span.b.y-span.a.y);
        addFluidMark(i,side,part,-1,false,len-1,(span.top+span.bottom)*.5f-span.anchor,5,.5f);
        if(fluidMarks.size()>1){wrapped=true;const auto first=fluidMarks.front();
            for(const auto &m:fluidMarks)if(m.flip!=first.flip||m.born!=first.born||m.mask!=first.mask)I_Error("Corner decal continuity lost");
        }
    }
    if(!wrapped)I_Error("Fluid decals failed to wrap map corners");
    fluidParticles.clear();fluidBursts.clear();fluidBursts.push_back({0,0,0,MT_PLASMA,0});
    tickFluidEffects();tickFluidEffects();
    if(fluidParticles.size()>10)I_Error("Plasma impact emitted lightning instead of sparks");
    settings.fluidWeapons=0;tickFluidEffects();if(!fluidMarks.empty())I_Error("Disabled Fluid effects left decals");
    settings.fluidWeapons=enabled;settings.fluidRocketFire=rocketFire;
    fluidParticles=std::move(saveFluidParticles);fluidMarks=std::move(saveFluidMarks);fluidBursts=std::move(saveFluidBursts);
    puts("Fluid rocket/plasma/BFG trails, impacts, scorches, expiry and toggle passed.");
    int optionCount=0;
    Settings config;
      for(const char *key:{"material_rubble","progressive_glass","hidden_machinery","adjacent_reverb","blood_spray","live_readouts","worn_plates","window_streaks"}) {
          int count=0;
          for(auto &tab:optionTabs(config))for(auto &section:tab.sections)for(auto &option:section.options)
              if(!strcmp(option.key,key))++count;
          if(count!=1)I_Error("Missing or duplicate integrated option: %s",key);
      }
      readOption(config,"material_rubble",0);readOption(config,"progressive_glass",0);
      readOption(config,"hidden_machinery",0);readOption(config,"adjacent_reverb",0);
      if(config.materialRubble||config.progressiveGlass||config.hiddenMachinery||config.adjacentReverb)
          I_Error("Integrated feature toggles did not turn off");
      readOption(config,"material_rubble",1);readOption(config,"progressive_glass",1);
      readOption(config,"hidden_machinery",1);readOption(config,"adjacent_reverb",1);
      if(!config.materialRubble||!config.progressiveGlass||!config.hiddenMachinery||!config.adjacentReverb)
          I_Error("Integrated feature toggles did not turn on");
      int colorMode=settings.materialRubble;bool colored=false;
      for(int i=0;i<numlines&&!colored;++i)for(int part=0;part<3&&!colored;++part) {
          WallSpan span;if(!wallSpan(lines[i],0,part,span)||span.texture<=0)continue;
          float x=(span.a.x+span.b.x)*.5f,y=(span.a.y+span.b.y)*.5f,z=(span.bottom+span.top)*.5f;
          settings.materialRubble=1;auto tint=rubbleSurfaceColor(x,y,z);
          if(tint[0]-tint[2]<.15f)continue;
          settings.materialRubble=0;auto grey=rubbleSurfaceColor(x,y,z);
          if(grey!=std::array<float,3>{1,1,1})I_Error("Rubble color toggle did not restore grey");
          settings.materialRubble=1;
          if(tint!=rubbleSurfaceColor(x,y,z))I_Error("Rubble color changed when re-enabled");
          printf("PASS warm wall rubble tint %.3f %.3f %.3f and grey fallback\n",tint[0],tint[1],tint[2]);colored=true;
      }
      settings.materialRubble=colorMode;
      if(!colored)I_Error("Warm wall material not sampled in test map");
    readOption(config,"fluid_rocket_fire",0);
    if(config.fluidRocketFire!=0)I_Error("Soft rocket fire option not loaded");
    readOption(config,"fluid_rocket_fire",1);
    if(config.fluidRocketFire!=1)I_Error("Grainy rocket fire option not loaded");
    for(auto &tab:optionTabs(config))for(auto &section:tab.sections)for(auto &option:section.options)
        if(!strcmp(option.key,"door_sparks")||
           !strcmp(option.key,"gun_impacts")) {
            if(strcmp(tab.title,!strcmp(option.key,"gun_impacts")?"Combat":"Destruction"))I_Error("Impact/door option is in the wrong category");
            ++optionCount;
        }
    if(optionCount!=2)I_Error("Missing/duplicate Destruction options");
    readOption(config,"gun_impacts",0);
    if(config.gunImpacts)I_Error("Impact config keys not loaded");
    dropOldLevelImpacts();
    auto savedGunPuffs=gunPuffs;gunPuffs.clear();
    auto savedParticles=impactParticles;impactParticles.clear();
    auto savedDebris=debris;auto savedWalls=wallDecals;auto savedScorches=scorches;
    int gun=settings.gunImpacts,lights=settings.shootableLights;
    auto random=impactRandom;settings.shootableLights=0;debris.clear();wallDecals.clear();scorches.clear();
    bool tested=false;
    for(int i=0;i<numlines&&!tested;++i) {
        line_t &l=lines[i];WallSpan span;
        if(!wallSpan(l,0,0,span)||span.texture<=0)continue;
        fixed_t x=(fixed_t)((span.a.x+span.b.x)*.5f*FRACUNIT);
        fixed_t y=(fixed_t)((span.a.y+span.b.y)*.5f*FRACUNIT);
        fixed_t z=(fixed_t)((span.bottom+span.top)*.5f*FRACUNIT);
        settings.gunImpacts=0;wallImpact(&l,0,x,y,z);
        if(!impactParticles.empty()||!wallDecals.empty())I_Error("Disabled gun impacts emitted effects");
        settings.gunImpacts=1;wallImpact(&l,0,x,y,z);
        if(impactParticles.size()!=6||wallDecals.size()!=1)I_Error("Enabled gun impact burst missing");
        mobj_t puff={};puff.type=MT_PUFF;puff.x=x;puff.y=y;puff.z=z;
        if(!customGunPuff(&puff))I_Error("Enhanced hit left the original puff visible");
        puff.z+=24*FRACUNIT; // Stock puff rises after spawning; keep it replaced.
        if(!customGunPuff(&puff))I_Error("Rising original puff escaped replacement");
        puff.x+=100*FRACUNIT;
        if(customGunPuff(&puff))I_Error("Unrelated actor puff was hidden");
        tested=true;
    }
    if(!tested)I_Error("No wall for gun impact regression");
    {
        gunPuffs.clear();impactParticles.clear();
        mobj_t puff={};puff.type=MT_PUFF;
        puff.x=players[displayplayer].mo->x;puff.y=players[displayplayer].mo->y;puff.z=players[displayplayer].mo->z+32*FRACUNIT;
        settings.gunImpacts=0;solidBulletImpact(units(puff.x),units(puff.y),units(puff.z),1,0,0);
        if(!impactParticles.empty()||customGunPuff(&puff))I_Error("Disabled solid impact replaced classic puff");
        settings.gunImpacts=1;solidBulletImpact(units(puff.x),units(puff.y),units(puff.z),1,0,0);
        if(impactParticles.size()!=6||!customGunPuff(&puff))I_Error("Solid surface hit did not replace classic puff");
        solidBulletImpact(units(puff.x),units(puff.y),units(puff.z),1,0,0);
        if(impactParticles.size()!=6)I_Error("Solid callback duplicated existing wall effect");
        puff.z+=24*FRACUNIT;if(!customGunPuff(&puff))I_Error("Rising solid puff escaped replacement");
        puts("PASS solid hits replace legacy puffs, honor toggle and avoid duplicate wall bursts");
    }

    impactParticles.clear();debris.clear();wallDecals.clear();scorches.clear();
    for(int i=0;i<100;++i)uziImpact(units(players[displayplayer].mo->x),units(players[displayplayer].mo->y),32,0,0,1);
    if(impactParticles.size()>512)I_Error("Uzi effects exceeded particle budget");
    gunPuffs=std::move(savedGunPuffs);
    impactParticles=std::move(savedParticles);
    debris=std::move(savedDebris);wallDecals=std::move(savedWalls);scorches=std::move(savedScorches);
    settings.gunImpacts=gun;settings.shootableLights=lights;impactRandom=random;
    {
        auto saved=std::move(rubbleParticles);auto savedChunks=std::move(debris);auto savedVoxels=std::move(impactParticles);int chunks=settings.rocketChunks;settings.rocketChunks=1;
        for(int type:{MT_ROCKET,MT_BFG,MT_PLASMA,MT_TROOPSHOT,MT_HEADSHOT,MT_BRUISERSHOT,MT_FATSHOT,MT_ARACHPLAZ,MT_TRACER}){
            rubbleParticles.clear();rubbleImpact(type,0,0,0,0,0,1);
            if(rubbleParticles.empty())I_Error("Missing rubble projectile profile");
            if((type==MT_BFG||type==MT_BRUISERSHOT)&&rubbleParticles[0].color[1]!=1)I_Error("Missing green rubble tint");
            for(int t=0;t<60;++t)tickRubbleEffects();
            if(!rubbleParticles.empty())I_Error("Rubble effect did not expire");
        }
        for(int i=0;i<100;++i)rubbleImpact(MT_BFG,0,0,0,0,0,1);
        if(rubbleParticles.size()!=192)I_Error("Rubble particle cap failed");
        impactParticles.clear();
        auto *player=players[displayplayer].mo;float cx=units(player->x),cy=units(player->y),cz=units(player->z)+24;
        auto expected=rubbleSurfaceColor(cx,cy,cz);
        for(int i=0;i<12;++i)rubbleImpact(MT_ROCKET,cx,cy,cz,0,0,1);
        if(impactParticles.size()!=48)I_Error("Rocket chunk budget failed");
        for(const auto &p:impactParticles)if(p.kind!=4||p.color!=expected||p.size<3.5f||p.size>7)I_Error("Rocket chunk material or size failed");
        auto before=impactParticles.front();moveImpactParticle(impactParticles.front(),1.f/TICRATE);
        if(impactParticles.front().z==before.z)I_Error("Rocket chunk did not move");
        settings.rocketChunks=0;tickImpactParticles();
        if(!impactParticles.empty())I_Error("Rocket chunks toggle did not clear particles");
        impactParticles=std::move(savedVoxels);settings.rocketChunks=chunks;
        rubbleParticles=std::move(saved);debris=std::move(savedChunks);
        puts("PASS rocket voxel chunk material, cap, movement and toggle");
        puts("PASS rubble projectile profiles, expiration and shared 192 particle cap");
    }
    {
        auto saved=std::move(surfaceGlows);
        SurfaceGlow g{{0,0,0},{1,0,0},{0,1,1},gameSeconds(),3,24,units(sectors[0].ceilingheight),0,1};
        fixed_t height=sectors[0].ceilingheight;
        sectors[0].ceilingheight+=16*FRACUNIT;
        if(std::abs(surfaceGlowCenter(g).z-16)>0.01f)I_Error("Surface glow did not follow door height");
        sectors[0].ceilingheight=height;
        int rock=W_CheckNumForName((char*)"DIGROCK"),material=rock>=0?-1-rock:R_TextureNumForName((char*)"GRAY1");
        auto &mesh=batches[material];auto oldMesh=std::move(mesh);auto oldSections=std::move(vertexSections[&mesh]);
        mesh.resize(3);mesh[0].y=-4;mesh[0].z=-4;mesh[1].y=4;mesh[1].z=-4;mesh[2].z=4;
        mesh.resize(6);mesh[3]=mesh[1];mesh[4]=mesh[0];mesh[5].x=3;mesh[5].y=0;mesh[5].z=-8;
        vertexSections[&mesh]={{~uint64_t(0),0,6,1}};
        auto folded=glowFacets(mesh,0,6);
        if(folded[0].fold[0]<=0)I_Error("Concave edge missed by heat mask");
        mesh[5].x=0;auto flat=glowFacets(mesh,0,6);
        if(flat[0].fold[0]!=0)I_Error("Flat triangulation edge glows");
        mesh[5].x=-3;auto convex=glowFacets(mesh,0,6);
        if(convex[0].fold[0]!=0)I_Error("Convex edge treated as a hot crevice");
        mesh[5].x=3;
        surfaceGlows={g};auto &overlay=effectAddBatches[surfaceGlowImage()];overlay.clear();drawSurfaceGlows();
        if(overlay.size()<=6)I_Error("Surface glow did not follow concave concrete edges");
        if(rubbleEmissionVertices.size()!=overlay.size()||rubbleEmissionVertices[0].green<=1)I_Error("Rubble missing HDR bloom source");
        g.color={.025f,.4f,1.f};surfaceGlows={g};overlay.clear();drawSurfaceGlows();
        if(overlay.size()!=mesh.size())I_Error("Plasma field did not cover surviving facets");
        for(const auto& v:overlay)if(v.mode!=2048u||v.sunV!=g.radius)I_Error("Plasma field lost local coordinates");
        for(int type:{MT_BFG,MT_ROCKET,MT_TRACER,MT_TROOPSHOT,MT_HEADSHOT,MT_FATSHOT,MT_BRUISERSHOT,MT_ARACHPLAZ}){
            bool thermal=type==MT_ROCKET||type==MT_TRACER||type==MT_TROOPSHOT||type==MT_HEADSHOT||type==MT_FATSHOT;
            g.weapon=type;g.color=type==MT_BFG?std::array<float,3>{.25f,1,.12f}:std::array<float,3>{1,.4f,.02f};
            if(type==MT_ARACHPLAZ)g.color={.025f,.4f,1.f};
            surfaceGlows={g};overlay.clear();drawSurfaceGlows();
            if(overlay.size()!=mesh.size()||effectBatches[surfaceGlowImage()].empty())I_Error("Monster impact fell back to legacy facets");
            if(type==MT_BFG&&overlay.size()!=mesh.size())I_Error("Weapon heat field lost concrete coverage");
            if(type==MT_ROCKET&&(overlay.empty()||effectBatches[surfaceGlowImage()].empty()))I_Error("Rocket missing molten edges or scorch");
            for(const auto& v:overlay)if(v.mode!=(2048u|(thermal?1024u:0u)))I_Error("Wrong projectile heat field profile");
        }
        overlay.clear();mesh.clear();vertexSections[&mesh].clear();drawSurfaceGlows();
        if(!overlay.empty()||!rubbleEmissionVertices.empty()||!effectBatches[surfaceGlowImage()].empty())I_Error("Surface glow remained over removed concrete");
        mesh=std::move(oldMesh);vertexSections[&mesh]=std::move(oldSections);
        surfaceGlows=std::move(saved);
        puts("PASS surface glow follows door height and surviving concrete only");
    }
    puts("Uzi impact toggles and particle budget passed.");
}

#endif
