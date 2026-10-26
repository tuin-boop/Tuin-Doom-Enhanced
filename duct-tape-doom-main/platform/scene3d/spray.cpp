/* Blood spray: what hits and kills throw. Drops fly as Doom-pixel specks
   and land as splats on floors, walls and ceilings; wounds drip; bodies
   sliding across the floor smear it, bodies thrown into walls splash them.
   Drawing only: own random numbers, nothing the game sees. */
#include "internal.h"
extern "C" {
#include "../audio/impact_synth.h"
#include "../audio/sound_options.h"
}

namespace doom3d {
// A drop's flight is worked out once, when it is thrown: where it lands
// (the floor under it, a wall in its way, the ceiling it rises into) and
// when. Its splat is laid then, shown from that moment, and the speck flies
// until it gets there, moving at the tic rate like the debris.
constexpr float sprayGravity=1225; // Doom's: a unit a tic, each tic.
struct Drop { float x,y,z,vx,vy,vz,born,end,size;std::array<float,3> color; };
static std::vector<Drop> drops;
constexpr size_t maxDrops=768;
// Dying and dead bodies watched for sliding (smears) and stopping short (a wall).
struct Slide { float x,y,speed,dx,dy,since;bool slammed,seen; };
static std::unordered_map<const mobj_t*,Slide> slides;
static unsigned sprayLevelSerial;

static void checkSprayLevel() {
    if(sprayLevelSerial==r_levelserial)return;
    drops.clear();slides.clear();sprayLevelSerial=r_levelserial;
}
static const sector_t *sectorAt(float x,float y) {return R_PointInSubsector((fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT))->sector;}
static bool bleeds(const mobj_t &thing) {
    if(!thing.info||(thing.flags&MF_NOBLOOD))return false;
    return (thing.info->flags&MF_COUNTKILL)||Game_ThingRole(thing.type)==GAME_THING_UNCOUNTED_MONSTER;
}
static bool sprayOn() {return settings.bloodSpray&&bloodOn();}

// Throws a drop of size (its speck, units) from (x,y,z) at (vx,vy,vz) units
// a second; splat: the size of what it leaves (0 none).
static void throwDrop(float x,float y,float z,float vx,float vy,float vz,float size,float splat,int kind) {
    float now=gameSeconds();
    const sector_t *start=sectorAt(x,y);
    z=std::clamp(z,floorZ(start)+0.5f,ceilZ(start)-0.5f);
    auto fallTime=[&](float floor) {
        float d=z-floor;
        return d<0?-1.0f:(vz+std::sqrt(vz*vz+2*sprayGravity*d))/sprayGravity;
    };
    // The floor: the start's, then the landing point's (a step's face in
    // between is found as a wall below).
    float t=fallTime(floorZ(start));
    const sector_t *landing=start;
    for(int pass=0;pass<2;++pass) {
        const sector_t *s=sectorAt(x+vx*t,y+vy*t);
        if(s==landing)break;
        float again=fallTime(floorZ(s));
        if(again<0)break;
        landing=s;t=again;
    }
    t=std::max(t,0.03f);
    enum { FLOOR,WALL,CEILING } where=FLOOR;
    // The ceiling, when it rises that high first.
    if(vz>0&&start->ceilingpic!=skyflatnum) {
        float rise=ceilZ(start)-0.5f-z;
        if(rise>0&&vz*vz>2*sprayGravity*rise) {
            float up=(vz-std::sqrt(vz*vz-2*sprayGravity*rise))/sprayGravity;
            const sector_t *above=sectorAt(x+vx*up,y+vy*up);
            if(up<t&&std::abs(ceilZ(above)-ceilZ(start))<1) {t=up;where=CEILING;landing=above;}
        }
    }
    // A wall in the way, along the chord of its arc.
    float h=std::hypot(vx,vy);WallHit hit;
    if(h>1) {
        float reach=h*t,end=z+vz*t-0.5f*sprayGravity*t*t;
        if(firstSolidWall(x,y,z,vx/h,vy/h,(end-z)/reach,reach,hit)) {t=hit.t/h;where=WALL;}
    }
    if(t>2.5f)splat=0; // Down a pit.
    float lx=x+vx*t,ly=y+vy*t,lz=z+vz*t-0.5f*sprayGravity*t*t,fall=vz-sprayGravity*t;
    if(splat>0) {
        if(where==WALL) {
            // Glancing drops streak along the wall.
            const line_t &line=lines[hit.line];
            float ex=units(line.v2->x-line.v1->x),ey=units(line.v2->y-line.v1->y),el=std::max(1e-3f,std::hypot(ex,ey));
            float facing=std::abs((vx*ey-vy*ex)/(h*el));
            bloodWallSplat(hit,lx,ly,lz,splat,std::clamp(1/std::max(facing,0.35f),1.0f,2.4f),now+t,kind,kind==BLOOD_SPLAT&&splat>=18);
        } else {
            const sector_t *s=where==CEILING?landing:sectorAt(lx,ly);
            size_t index=s-sectors;
            bool liquid=index<sectorMist.size()&&sectorMist[index].liquid;
            if(where==CEILING||!liquid) {
                // Drops coming in low stretch along their way.
                float stretch=where==CEILING?1:1+std::clamp(h/std::max(60.0f,std::abs(fall))*0.6f,0.0f,1.2f);
                bloodFloorSplat(lx,ly,(int)index,splat,std::atan2(vy,vx),stretch,where==CEILING,now+t,kind);
            }
        }
    }
    if(drops.size()>=maxDrops)drops.erase(drops.begin(),drops.begin()+(drops.size()-maxDrops+1));
    drops.push_back({x,y,z,vx,vy,vz,now,now+t,size,bloodColor(randomRange(0.35f,0.7f))});
}
// A spray of count drops from (x,y,z) around the heading (dx,dy), spread
// that far across it, speeds and rises in those ranges; the first big ones
// carry splats of the big size range, the rest specks now and then.
struct Burst { int count,big;float spread,speed[2],rise[2],bigSize[2],speckChance; };
static void burst(float x,float y,float z,float dx,float dy,const Burst &b) {
    for(int i=0;i<b.count;++i) {
        float a=std::atan2(dy,dx)+randomRange(-b.spread,b.spread),speed=randomRange(b.speed[0],b.speed[1]);
        bool big=i<b.big;
        float splat=big?randomRange(b.bigSize[0],b.bigSize[1]):randomRange(0,1)<b.speckChance?randomRange(4.5f,7.5f):0;
        throwDrop(x,y,z,std::cos(a)*speed,std::sin(a)*speed,randomRange(b.rise[0],b.rise[1]),big?1.6f:randomRange(0.8f,1.3f),
                  splat,big?BLOOD_SPLAT:BLOOD_SPECK);
    }
}

void sprayHit(const mobj_t *target,float x,float y,float z,float dx,float dy,float slope,int damage) {
    if(!target||!bleeds(*target))return;
    checkSprayLevel();
    // The game puts the hit 10 units short of the body. The exit spray
    // leaves the far side of it, along the shot, the harder the faster.
    float r=units(target->radius),through=(units(target->x)-x)*dx+(units(target->y)-y)*dy+r*0.6f;
    float ex=x+dx*through,ey=y+dy*through,ez=z+slope*through,force=0.7f+std::min(damage,30)/30.0f;
    int count=std::clamp(1+damage/5,2,7),big=1+(randomRange(0,30)<damage);
    for(int i=0;i<count;++i) {
        float across=randomRange(-0.3f,0.3f),speed=randomRange(110,280)*force;
        float vx=(dx-dy*across)*speed,vy=(dy+dx*across)*speed,vz=randomRange(-30,110)+slope*speed;
        float splat=i<big?randomRange(14,24)*(damage>=15?1.25f:1.0f):randomRange(0,1)<0.6f?randomRange(4.5f,7.5f):0;
        throwDrop(ex,ey,ez,vx,vy,vz,i<big?1.6f:randomRange(0.8f,1.3f),splat,i<big?BLOOD_SPLAT:BLOOD_SPECK);
    }
    // Back spatter at the entry, falling short toward the shooter.
    if(randomRange(0,1)<0.5f) {
        float near=std::max(0.0f,through-2*r);
        burst(x+dx*near,y+dy*near,z,-dx,-dy,{1+(damage>=10),0,0.6f,{30,90},{0,80},{0,0},0.7f});
    }
}
void sprayKill(const mobj_t &thing,float dx,float dy,float speed,float hitZ,bool gib,int overkill) {
    if(!sprayOn()||!bleeds(thing))return;
    checkSprayLevel();
    float x=units(thing.x),y=units(thing.y),feet=units(thing.z),tall=std::max(16.0f,units(thing.info->height)),r=units(thing.radius);
    float z=feet+(hitZ>=0?std::clamp(hitZ,tall*0.2f,tall*0.9f):tall*0.6f);
    if(gib) {
        // Burst all around and high, up to a low ceiling.
        burst(x,y,z,1,0,{32,6,doomPi,{60,230},{40,330},{18,30},0.7f});
        return;
    }
    float force=std::clamp((speed+overkill/8.0f)/4,0.4f,2.0f);
    int count=std::clamp(6+overkill/3+(int)(speed*2),6,26),big=std::min(5,2+overkill/20);
    bool pushed=speed>=0.6f;
    float ox=x+(pushed?dx*r*0.5f:0),oy=y+(pushed?dy*r*0.5f:0);
    burst(ox,oy,z,dx,dy,{count,big,pushed?0.7f:doomPi,{60*force,200*force},{-20,140*std::max(0.6f,force)},{18,30},0.7f});
    if(count>=10)I_MadeSound(IMPACT_GORE_BURST,x,y,z);
}
void sprayHurt(const mobj_t &thing,float dx,float dy,float hitZ,int damage) {
    if(!sprayOn()||!bleeds(thing))return;
    checkSprayLevel();
    float tall=std::max(16.0f,units(thing.info->height)),z=units(thing.z)+(hitZ>=0?std::clamp(hitZ,tall*0.2f,tall*0.9f):tall*0.6f);
    float r=units(thing.radius),x=units(thing.x)+dx*r*0.5f,y=units(thing.y)+dy*r*0.5f;
    burst(x,y,z,dx,dy,{std::clamp(2+damage/8,3,8),1,0.8f,{60,180},{-10,120},{14,22},0.6f});
}
static bool onLiquid(float x,float y) {
    size_t index=sectorAt(x,y)-sectors;
    return index<sectorMist.size()&&sectorMist[index].liquid;
}
// The slap is heard with or without blood spray (it went quiet with the spray switch).
void sprayLanding(float x,float y,float z,float speed) {
    if(onLiquid(x,y))return;
    if(speed>1.5f)I_MadeSound(IMPACT_GORE_SLAP,x,y,z);
    if(!sprayOn())return;
    checkSprayLevel();
    size_t index=sectorAt(x,y)-sectors;
    bloodFloorSplat(x,y,(int)index,std::clamp(16+speed*4,18.0f,32.0f),randomRange(0,2*doomPi),1,false,gameSeconds(),BLOOD_SPLAT);
    burst(x,y,z+2,1,0,{std::clamp((int)(2+speed*1.2f),3,8),0,doomPi,{30,110},{60,150},{0,0},0.8f});
}
// A splash on the first wall within reach from (x,y,z) along (dx,dy), a burst back off it, the slap.
static bool wallSlam(float x,float y,float z,float dx,float dy,float reach,float size,int specks) {
    WallHit hit;
    if(!firstSolidWall(x,y,z,dx,dy,0,reach,hit))return false;
    I_MadeSound(IMPACT_GORE_SLAP,x,y,z);
    if(!sprayOn())return true;
    float now=gameSeconds();
    bloodWallSplat(hit,x+dx*hit.t,y+dy*hit.t,z,size,1.3f,now,BLOOD_SPLAT,true);
    burst(x,y,z,-dx,-dy,{specks,0,0.9f,{30,100},{0,90},{0,0},0.7f});
    return true;
}
void sprayWallHit(float x,float y,float z,float dx,float dy,float speed) {
    if(sprayOn())checkSprayLevel();
    wallSlam(x,y,z,dx,dy,16,std::clamp(18+speed*4,20.0f,34.0f),std::clamp((int)(2+speed*1.5f),3,8));
}
void spraySmear(float x,float y,float angle,float size) {
    if(!sprayOn()||onLiquid(x,y))return;
    checkSprayLevel();
    bloodFloorSplat(x,y,(int)(sectorAt(x,y)-sectors),size,angle,1,false,gameSeconds(),BLOOD_SMEAR);
}

// Wounds drip while they are fresh: about one drop a second from a big
// hole, thinning out over twenty seconds, falling with the monster's step.
static void drip(const mobj_t &thing) {
    WoundSpot spots[10];
    int count=woundSpots(&thing,spots,10);
    if(!count)return;
    float x=units(thing.x),y=units(thing.y),feet=units(thing.z),r=units(thing.radius);
    float facing=(float)(thing.angle*(2*doomPi/4294967296.0));
    for(int n=0;n<count;++n) {
        const WoundSpot &w=spots[n];
        float age=(leveltime-w.tic)/(float)TICRATE,fresh=std::clamp(1-age/20,0.0f,1.0f);
        if(randomRange(0,1)>=0.02f*w.radius*fresh)continue;
        // The wound faces the view it was shot from; across is to that view's right.
        float view=facing+w.yaw,vx=std::cos(view),vy=std::sin(view);
        float wx=x-vx*r*0.7f+vy*w.across,wy=y-vy*r*0.7f-vx*w.across;
        throwDrop(wx,wy,feet+w.height,units(thing.momx)*TICRATE+randomRange(-6,6),units(thing.momy)*TICRATE+randomRange(-6,6),0,
                  randomRange(0.7f,1.0f),randomRange(0,1)<0.5f?randomRange(4,6):0,BLOOD_SPECK);
    }
}
void watchSpray() {
    if(!sprayOn()||levelSerial!=r_levelserial) {drops.clear();slides.clear();return;}
    checkSprayLevel();
    for(auto &entry:slides)entry.second.seen=false;
    float now=gameSeconds();
    for(int i=0;i<numsectors;++i)for(mobj_t *thing=sectors[i].thinglist;thing;thing=thing->snext) {
        if(!bleeds(*thing))continue;
        if(thing->health>0) {drip(*thing);slides.erase(thing);continue;}
        float x=units(thing->x),y=units(thing->y),mx=units(thing->momx),my=units(thing->momy),speed=std::hypot(mx,my);
        auto found=slides.find(thing);
        if(found==slides.end()) {slides[thing]={x,y,speed,speed>0?mx/speed:0,speed>0?my/speed:0,0,false,true};continue;}
        Slide &s=found->second;s.seen=true;
        // A ragdoll's body is elsewhere: its own contacts splash and smear (ragdoll.cpp).
        bool doll=hasRagdoll(thing);
        // Thrown hard and stopped short (more than friction does): it met
        // a wall. The wall takes a splash at the body's middle, once.
        if(!doll&&!s.slammed&&s.speed>5&&speed<s.speed*0.5f) {
            float tall=std::max(16.0f,units(thing->info->height)),z=units(thing->z)+tall*0.5f;
            if(wallSlam(s.x,s.y,z,s.dx,s.dy,units(thing->radius)+12,randomRange(24,34),6))s.slammed=true;
        }
        // Down and sliding: a smear every so far, thick end at the body.
        float moved=std::hypot(x-s.x,y-s.y);
        if(!doll&&!(thing->flags&MF_SOLID)&&thing->z<=thing->floorz&&speed>1.2f) {
            s.since+=moved;
            float size=std::clamp(units(thing->radius)*0.6f,9.0f,15.0f);
            if(s.since>=size*1.4f) {
                s.since=0;
                size_t index=thing->subsector->sector-sectors;
                if(index>=sectorMist.size()||!sectorMist[index].liquid)
                    bloodFloorSplat(x-mx/speed*size*0.6f,y-my/speed*size*0.6f,(int)index,size,std::atan2(my,mx)+doomPi,1,false,now,BLOOD_SMEAR);
            }
        } else s.since=0;
        s.x=x;s.y=y;s.speed=speed;
        if(speed>0.01f) {s.dx=mx/speed;s.dy=my/speed;}
    }
    for(auto it=slides.begin();it!=slides.end();) it=it->second.seen?std::next(it):slides.erase(it);
}
void buildSpray(const Uniforms &camera) {
    checkSprayLevel();
    float now=gameSeconds();
    drops.erase(std::remove_if(drops.begin(),drops.end(),[&](const Drop &d){return now>=d.end||now<d.born-0.1f;}),drops.end());
    for(const Drop &d:drops) {
        float t=std::clamp(std::floor((now-d.born)*TICRATE)/TICRATE,0.0f,d.end-d.born);
        float x=d.x+d.vx*t,y=d.y+d.vy*t,z=d.z+d.vz*t-0.5f*sprayGravity*t*t;
        float light=lighting(sectorAt(x,y)),tall=std::abs(d.vz-sprayGravity*t)>250?2.0f:1.0f;
        std::array<float,3> lit;for(int c=0;c<3;++c)lit[c]=std::min(1.0f,d.color[c]*light);
        pixelBlock(particleVertices,camera,x,y,z,d.size,tall,lit,false,&d.color);
    }
}
void sprayShutdown() {drops.clear();slides.clear();}
} // namespace doom3d
