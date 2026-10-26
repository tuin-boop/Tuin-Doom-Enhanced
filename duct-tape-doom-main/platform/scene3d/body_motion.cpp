/* Body motion: monsters jolt from the blow that hits or kills them. A
   drawing-only layer over the game's own frames; the game never sees it. */
#include "internal.h"

namespace doom3d {
// Every tic each monster is watched for lost health. The blow's direction
// and strength are the push the game itself gives (P_DamageMobj thrusts the
// victim away, scaled by its mass; the chainsaw doesn't push), its height
// comes from where hitscan shots land (bodyHit) or from the missile or
// blast that did the damage (bodyDamaged, every damage event).
// Each blow starts an impulse that leans the body with the push, the upper
// body lagging behind, and buckles the legs; high hits throw the body back,
// low hits knock the legs out. Deaths jolt hardest and fade out before the
// corpse frame, so the final body stays the game's. Gibbing hops the body,
// fliers sway as they fall. Variation comes from hashes, never P_Random.
struct Impulse { int start;float dx,dy,lean,whip,squash,peak,hop;int duration;bool death,flier; };
struct Body { int health;float momx,momy;bool alive,seen; };
// This tic's hits on one body: hitscan points (exact) and the attack that
// did the damage (its inflictor: the shooter for hitscan, else the missile
// or blast), which groups bodies struck by one blast.
struct Hit { int tic;float z,dx,dy,side;int count;const mobj_t *attack;float missileZ,missileSide;bool missile; };
static std::unordered_map<const mobj_t*,Body> watched;
static std::unordered_map<const mobj_t*,std::vector<Impulse>> impulses;
static std::unordered_map<const mobj_t*,Hit> hits;
static unsigned bodyLevelSerial;
// Vanilla friction on the ground, so the push of this tic is what the
// momentum gained beyond the last tic's slide.
constexpr float slide=(float)0xe800/FRACUNIT;

static bool monster(const mobj_t &thing) {return thing.info&&(thing.info->flags&MF_COUNTKILL);}
// Tics from a death state to its resting frame (or removal).
static int deathTics(int state) {
    int total=0;
    for(int n=0;n<64&&state>0;++n) {
        const state_t &s=states[state];
        if(s.tics<0)break;
        total+=s.tics;state=s.nextstate;
    }
    return std::max(total,4);
}
void bodyHit(mobj_t *target,fixed_t fx,fixed_t fy,fixed_t fz,fixed_t fdx,fixed_t fdy,int damage) {
    (void)damage;
    if((!settings.deathMotion&&!settings.hitFlinch&&!settings.deathDirector)||!target||!monster(*target))return;
    float dx=units(fdx),dy=units(fdy),length=std::hypot(dx,dy);
    if(length<1e-4f)return;
    dx/=length;dy/=length;
    Hit &hit=hits[target];
    if(hit.tic!=leveltime)hit={leveltime,0,0,0,0,0,nullptr,0,0,false};
    // A blast's pellets average into one hit. Side: how far off the body's
    // middle it struck, across the shot, in radii (positive: the shooter's left).
    float across=(units(fx)-units(target->x))*-dy+(units(fy)-units(target->y))*dx;
    hit.z+=units(fz)-units(target->z);hit.dx+=dx;hit.dy+=dy;hit.side+=across/std::max(1.0f,units(target->radius));++hit.count;
}
// A death: the game's xdeath (gib) frames rather than its plain death.
static bool gibbed(const mobj_t &thing) {
    int x=thing.info->xdeathstate;
    return x&&(thing.state==&states[x]||thing.state==&states[states[x].nextstate]);
}
// The push of this tic: its direction and speed (units per tic).
static void pushOf(const mobj_t &thing,const Body &before,float hitDx,float hitDy,float &dx,float &dy,float &speed) {
    float mx=units(thing.momx)-before.momx*slide,my=units(thing.momy)-before.momy*slide;
    speed=std::hypot(mx,my);dx=0;dy=0;
    if(speed>0.3f) {dx=mx/speed;dy=my/speed;}
    else if(hitDx!=0||hitDy!=0) {float l=std::hypot(hitDx,hitDy);dx=hitDx/l;dy=hitDy/l;}
    else if(thing.target) {
        float ax=units(thing.x-thing.target->x),ay=units(thing.y-thing.target->y),l=std::hypot(ax,ay);
        if(l>1e-3f){dx=ax/l;dy=ay/l;}
    }
    if(dx==0&&dy==0) {float a=hashUnit((int)(uintptr_t)&thing,leveltime,0x51u)*2*doomPi;dx=std::cos(a);dy=std::sin(a);}
}
void bodyDamaged(mobj_t *target,mobj_t *inflictor,mobj_t *source,int damage) {
    (void)damage;
    if((!settings.deathMotion&&!settings.hitFlinch&&!settings.deathDirector)||!target||!monster(*target))return;
    Hit &hit=hits[target];
    if(hit.tic!=leveltime)hit={leveltime,0,0,0,0,0,nullptr,0,0,false};
    if(!hit.attack)hit.attack=inflictor?inflictor:source;
    // A missile or blast: its height on the body, and for a direct hit
    // (still flying) how far off the middle, across its flight.
    if(inflictor&&inflictor!=source&&!hit.missile) {
        hit.missile=true;hit.missileZ=units(inflictor->z)-units(target->z);
        float mx=units(inflictor->momx),my=units(inflictor->momy),l=std::hypot(mx,my);
        if((inflictor->flags&MF_MISSILE)&&l>1e-3f)
            hit.missileSide=((units(inflictor->x)-units(target->x))*-my+(units(inflictor->y)-units(target->y))*mx)/l/std::max(1.0f,units(target->radius));
    }
}
static void addImpulse(const mobj_t &thing,const Body &before,float hitZ,bool hitKnown,float hitDx,float hitDy,bool death) {
    float dx,dy,speed;pushOf(thing,before,hitDx,hitDy,dx,dy,speed);
    uint32_t seed=hash32((uint32_t)(uintptr_t)&thing*2654435761u^(uint32_t)leveltime*40503u);
    auto vary=[&](int k,float spread){return 1+spread*(hashUnit(k,0,seed)*2-1);};
    float height=std::max(16.0f,units(thing.info->height));
    // Hit height: 1 well above the hip (thrown back), -1 at the legs (swept).
    // Level shots land a little above the middle, so most hits count as high.
    float high=hitKnown?std::clamp((hitZ/height-0.45f)/0.3f,-1.0f,1.0f):0.2f;
    // High hits throw the body back; low ones lean it the other way.
    float back=high>=0?0.7f+0.3f*high:0.7f+1.2f*high;
    float strength=settings.bodyMotionStrength;
    Impulse impulse={leveltime,dx,dy,0,0,0,2.0f*vary(1,0.3f),0,14,death,false};
    int hurt=std::max(1,before.health-std::max(0,thing.health));
    float share=std::min(1.0f,hurt/(float)std::max(1,thing.info->spawnhealth));
    if(death) {
        bool gib=gibbed(thing);
        impulse.duration=deathTics(gib?thing.info->xdeathstate:thing.info->deathstate);
        // No push (the chainsaw, a heavy body): the body crumples where it stands.
        bool crumple=speed<0.6f;
        float lean=crumple?0:std::clamp(0.12f+speed/12,0.12f,0.65f);
        impulse.lean=lean*back*vary(2,0.2f)*strength;
        impulse.whip=lean*(high>=-0.2f?0.7f+0.4f*std::max(high,0.0f):-0.5f)*vary(3,0.3f)*strength;
        impulse.squash=std::min(0.6f,(crumple?0.42f:0.18f+(high<0?0.15f*-high:0))*vary(4,0.2f)*strength);
        if(gib)impulse.hop=std::clamp(speed*0.5f,3.0f,7.0f)*strength;
        else if(speed>6)impulse.hop=std::min(4.0f,(speed-6)*0.4f)*strength;
        if(thing.info->flags&MF_FLOAT) {
            // Fliers drop and sway about their middle instead.
            impulse.flier=true;impulse.lean=(0.25f+lean*0.5f)*vary(2,0.2f)*strength;impulse.whip=0;impulse.squash=0;impulse.hop=0;
        }
    } else {
        float lean=std::clamp(0.08f+speed/10+share*0.4f,0.08f,0.35f);
        impulse.lean=lean*back*strength;impulse.whip=lean*0.5f*std::max(high,-0.5f)*strength;
        impulse.squash=high<0?0.1f*-high*strength:0;
    }
    auto &list=impulses[&thing];
    if(death)list.clear();
    if(list.size()>=4)list.erase(list.begin());
    list.push_back(impulse);
}
void watchBodies() {
    bool spray=settings.blood&&settings.bloodSpray;
    if((!settings.deathMotion&&!settings.hitFlinch&&!settings.deathDirector&&!settings.wounds&&!spray)||levelSerial!=r_levelserial) {bodyMotionShutdown();return;}
    if(bodyLevelSerial!=r_levelserial) {bodyMotionShutdown();bodyLevelSerial=r_levelserial;}
    for(auto &entry:watched)entry.second.seen=false;
    static std::vector<mobj_t*> bodies;bodies.clear();
    for(int i=0;i<numsectors;++i) for(mobj_t *thing=sectors[i].thinglist;thing;thing=thing->snext)
        if(monster(*thing))bodies.push_back(thing);
    for(mobj_t *thing:bodies) {
        bool alive=thing->health>0&&!(thing->flags&MF_CORPSE);
        auto found=watched.find(thing);
        if(found==watched.end()) {watched[thing]={thing->health,units(thing->momx),units(thing->momy),alive,true};continue;}
        Body &body=found->second;body.seen=true;
        bool died=body.alive&&!alive,hurt=alive&&thing->health<body.health;
        if(alive&&!body.alive) {impulses.erase(thing);forgetDeath(thing);forgetWounds(thing);} // Raised again.
        if(died||(hurt&&(settings.hitFlinch||spray))) {
            float z=0,hdx=0,hdy=0,side=0;bool known=false;const mobj_t *attack=nullptr;
            auto hit=hits.find(thing);
            bool missile=false;
            if(hit!=hits.end()&&leveltime-hit->second.tic<=1) {
                const Hit &h=hit->second;attack=h.attack;
                if(h.count>0) {z=h.z/h.count;hdx=h.dx;hdy=h.dy;side=h.side/h.count;known=true;}
                else if(h.missile) {z=h.missileZ;side=h.missileSide;known=true;missile=true;}
            }
            if(died)impulses.erase(thing);
            // Blood thrown by the blow (hitscan hits already sprayed their own).
            if(spray&&(died||missile)) {
                float dx,dy,speed;pushOf(*thing,body,hdx,hdy,dx,dy,speed);
                if(died)sprayKill(*thing,dx,dy,speed,known?z:-1,gibbed(*thing),std::max(0,-thing->health));
                else sprayHurt(*thing,dx,dy,z,body.health-thing->health);
            }
            if(died?settings.deathMotion:settings.hitFlinch)addImpulse(*thing,body,z,known,hdx,hdy,died);
            if(died&&settings.deathDirector) {
                float dx,dy,speed;pushOf(*thing,body,hdx,hdy,dx,dy,speed);
                // Hit height: 1 well above the hip, -1 at the legs (as addImpulse).
                float tall=std::max(16.0f,units(thing->info->height));
                float high=known?std::clamp((z/tall-0.45f)/0.3f,-1.0f,1.0f):0.2f;
                // Where the killing blow struck, for a shot through the head: hitscan only.
                bool shot=hit!=hits.end()&&leveltime-hit->second.tic<=1&&hit->second.count>0;
                float hitU=-side*units(thing->radius);    // side: in radii, the shooter's left
                startDeath(*thing,dx,dy,speed,side,high,gibbed(*thing),attack,shot?z:-1,hitU);
            }
        }
        body.health=thing->health;body.momx=units(thing->momx);body.momy=units(thing->momy);body.alive=alive;
    }
    for(auto it=watched.begin();it!=watched.end();) {
        if(it->second.seen) {++it;continue;}
        forgetDeath(it->first);forgetWounds(it->first);it=watched.erase(it);
    }
    for(auto it=impulses.begin();it!=impulses.end();) {
        auto &list=it->second;
        list.erase(std::remove_if(list.begin(),list.end(),[](const Impulse &p){return leveltime-p.start>p.duration+2;}),list.end());
        it=list.empty()||!watched.count(it->first)?impulses.erase(it):std::next(it);
    }
    for(auto it=hits.begin();it!=hits.end();) it=leveltime-it->second.tic>2?hits.erase(it):std::next(it);
}
void bodyMotionShutdown() {watched.clear();impulses.clear();hits.clear();deathsShutdown();woundsShutdown();}

// A rise to its peak at tic peak, then a slow fall.
static float pulse(float t,float peak) {return t<=0?0:t/peak*std::exp(1-t/peak);}
// A damped spring struck at rest: out to 1 at its peak, back past rest, settling
// (rate per tic, damping < 1).
static float kick(float t,float rate,float damping) {
    if(t<=0)return 0;
    float root=std::sqrt(1-damping*damping),ringing=rate*root,peak=std::atan2(root,damping)/ringing;
    auto at=[&](float s){return std::exp(-damping*rate*s)*std::sin(ringing*s);};
    return at(t)/at(peak);
}
bool bodyPose(const mobj_t &thing,float fraction,BodyPose &pose) {
    pose={};
    const bool dead=thing.health<=0||(thing.flags&MF_CORPSE);
    if(dead&&!settings.deathMotion)return false;
    if(thing.tics==-1&&(thing.flags&MF_CORPSE))return false;
    auto found=impulses.find(&thing);
    if(found==impulses.end())return false;
    bool any=false;
    for(const Impulse &p:found->second) {
        if((dead&&!p.death)||(p.death?!settings.deathMotion:!settings.hitFlinch))continue;
        float t=leveltime+fraction-1-p.start;
        if(t<0||t>p.duration)continue;
        float fade=1;
        if(p.death) {float a=p.duration*0.6f,s=std::clamp((t-a)/std::max(1.0f,p.duration-a),0.0f,1.0f);fade=1-s*s*(3-2*s);}
        else fade=std::clamp((p.duration-t)/6.0f,0.0f,1.0f);
        float lean,whip=0,squash=0;
        if(p.flier) {lean=p.lean*std::sin(t*2*doomPi/14)*std::exp(-t/24);pose.flier=true;}
        else if(p.death) {
            // A whiplash over the game's own fall: the body snaps back from
            // the blow and rebounds, the upper body a beat behind, the legs
            // giving way. The frames do the falling; holding a lean would
            // only tip the card over.
            lean=p.lean*kick(t,0.5f,0.35f);
            whip=p.whip*kick(t-1.5f,0.4f,0.3f);
            float s=std::clamp(t/(p.duration*0.4f),0.0f,1.0f);squash=p.squash*s*s*(3-2*s);
        } else {
            lean=p.lean*pulse(t,p.peak);
            whip=p.whip*(pulse(t,p.peak*1.8f)-0.3f*pulse(t,p.peak*4));
            squash=p.squash*pulse(t,p.peak*2.5f);
        }
        pose.lean[0]+=p.dx*lean*fade;pose.lean[1]+=p.dy*lean*fade;
        pose.whip[0]+=p.dx*whip*fade;pose.whip[1]+=p.dy*whip*fade;
        pose.squash=std::max(pose.squash,squash*fade);
        if(p.hop>0) {float up=p.hop*t-0.5f*t*t;if(up>0)pose.lift=std::max(pose.lift,up);}
        any=true;
    }
    for(float &v:pose.lean)v=std::clamp(v,-0.9f,0.9f);
    for(float &v:pose.whip)v=std::clamp(v,-0.6f,0.6f);
    pose.squash=std::min(pose.squash,0.6f);
    return any&&(std::abs(pose.lean[0])+std::abs(pose.lean[1])+std::abs(pose.whip[0])+std::abs(pose.whip[1])+pose.squash+pose.lift>1e-3f);
}

// The sprite in strips of whole texel rows, so the artwork's pixels stay
// square and unblurred. The lean across the view bends the body: each row
// shifts sideways by whole texels, the feet stay planted and the bend grows
// up the legs into the waist, the upper body whipping further. The lean
// toward or away from the eye only shortens the body by dropping rows, as a
// body bent that way looks from here; tipping the card itself would show it
// is flat, so it tips only a sliver. Buckling legs drop rows too.
void bodyQuads(std::vector<Vertex> &out,const BodyPose &pose,const Vertex &base,float x,float y,float feet,float rx,float ry,
               float left,float right,int imageTop,int rows,float u0,float u1,const std::vector<byte> *holes) {
    float fx=-ry,fy=rx;
    float side1=pose.lean[0]*rx+pose.lean[1]*ry,depth1=pose.lean[0]*fx+pose.lean[1]*fy;
    float side2=side1+pose.whip[0]*rx+pose.whip[1]*ry,depth2=depth1+pose.whip[0]*fx+pose.whip[1]*fy;
    // The upper body bends at most about 70 degrees from upright.
    side1=std::clamp(side1,-1.0f,1.0f);depth1=std::clamp(depth1,-1.0f,1.0f);
    side2=std::clamp(side2,-1.2f,1.2f);depth2=std::clamp(depth2,-1.2f,1.2f);
    constexpr float maxTilt=0.15f;
    float tilt1=std::clamp(depth1,-maxTilt,maxTilt),tilt2=std::clamp(depth2,-maxTilt,maxTilt);
    int pivot=pose.flier?imageTop/2:0;
    float hip=pose.flier?1e6f:std::max(4.0f,std::round(imageTop*0.45f));
    // Row scales below and above the hip: the share of the bend the sliver of tilt doesn't show.
    float lower=(1-pose.flatten)*(1-pose.squash)*std::cos(depth1)/std::cos(tilt1),upper=(1-pose.flatten)*std::cos(depth2)/std::cos(tilt2);
    float hipDest=std::floor(hip*lower);
    // Destination height (above the pivot) of a source row edge that far above it.
    auto dest=[&](int u)->float {
        if(u<=0)return pose.flier?-std::floor(-u*lower):(float)u;
        return u<=hip?std::floor(u*lower):hipDest+std::floor((u-hip)*upper);
    };
    // A destination height's place on the slightly tipped card: forward and up from the pivot.
    auto bend=[&](float d,float &ahead,float &up) {
        if(d<=hipDest) {ahead=d*std::sin(tilt1);up=d*std::cos(tilt1);}
        else {ahead=hipDest*std::sin(tilt1)+(d-hipDest)*std::sin(tilt2);up=hipDest*std::cos(tilt1)+(d-hipDest)*std::cos(tilt2);}
    };
    // Sideways offset at each whole destination height: the lean angle grows
    // from nothing at the feet to side1 at the hip, then on to side2 at the
    // top. Fliers turn whole about their middle.
    int topDest=(int)std::max(0.0f,dest(imageTop-pivot));
    std::vector<float> offset((size_t)topDest+2,0.0f);
    float topSpan=std::max(1.0f,topDest-hipDest);
    for(int d=0;d<=topDest;++d) {
        float mid=d+0.5f,angle=pose.flier?side1:mid<=hipDest?side1*mid/std::max(1.0f,hipDest):side1+(side2-side1)*(mid-hipDest)/topSpan;
        offset[(size_t)d+1]=offset[(size_t)d]+std::tan(angle);
    }
    auto shiftAt=[&](float d) {
        if(d<0)return pose.flier?std::round(d*std::tan(side1)):0.0f;
        float at=std::min(d,(float)topDest+1);int whole=(int)at;
        return std::round(offset[(size_t)whole]+(offset[std::min((size_t)whole+1,offset.size()-1)]-offset[(size_t)whole])*(at-whole));
    };
    auto corner=[&](float d,float across,float shift,float u,float v) {
        float ahead,up;bend(d,ahead,up);
        Vertex vertex=base;
        vertex.x=x+rx*(across+shift)+fx*ahead;vertex.y=y+ry*(across+shift)+fy*ahead;vertex.z=feet+pivot+up;
        vertex.u=u;vertex.v=v;
        return vertex;
    };
    // Runs of source rows that land on consecutive whole rows with one shift.
    int runStart=-1;float runTop=0,runShift=0;bool runUpper=false;
    // Rows with wound holes (wounds.cpp) go out one by one, cut into runs.
    int width=(int)std::lround(right-left);bool flip=u0>u1;
    auto wounded=[&](int r){if(!holes)return false;for(int c=0;c<width;++c)if((*holes)[(size_t)r*width+c])return true;return false;};
    auto flush=[&](int end,float bottom) {
        if(runStart<0)return;
        bool any=false;
        for(int r=runStart;r<end&&!any;++r)any=wounded(r);
        if(!any)
            quad(out,corner(runTop,left,runShift,u0,(float)runStart),corner(runTop,right,runShift,u1,(float)runStart),
                 corner(bottom,right,runShift,u1,(float)end),corner(bottom,left,runShift,u0,(float)end));
        else for(int r=runStart;r<end;++r) {
            float rowTop=runTop-(r-runStart);
            maskedRow(out,*holes,width,r,flip,[&](int edge,bool low) {
                return corner(low?rowTop-1:rowTop,flip?right-edge:left+edge,runShift,(float)edge,(float)(r+(low?1:0)));
            });
        }
        runStart=-1;
    };
    float lastBottom=0;
    for(int r=0;r<rows;++r) {
        int uTop=imageTop-r-pivot;
        float top=dest(uTop),bottom=dest(uTop-1);
        if(top<=bottom) {flush(r,lastBottom);continue;} // Dropped: buckled or bent toward or away.
        float shift=shiftAt((top+bottom)*0.5f);bool upperRow=bottom>=hipDest;
        if(runStart>=0&&(shift!=runShift||upperRow!=runUpper||top!=lastBottom))flush(r,lastBottom);
        if(runStart<0) {runStart=r;runTop=top;runShift=shift;runUpper=upperRow;}
        lastBottom=bottom;
    }
    flush(rows,lastBottom);
}
} // namespace doom3d
