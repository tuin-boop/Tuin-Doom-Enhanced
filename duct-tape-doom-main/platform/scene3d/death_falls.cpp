/* Procedural falls: a monster rigged by hand (death_rigs_stock.h) or, where
   its art has no hand rig, by the joints found in its drawings
   (death_joints.h) falls a way its drawings never show, driven by the hit.
   Drawing only. */
#include "internal.h"
#include <memory>
#include "../death_joints.h"

namespace doom3d {
// Two falls, from the death lab (tests/death_pose.py), on the rigged
// drawings' own texels:
// - Legs swept: on the pain drawing, simulated per kill from the hit (see
//   simulateLegs): the leg on the hit's side gives, the pelvis drops, the
//   torso balances then goes over, toward the weapon or away from it. Once
//   support is gone the pelvis drops to the floor, the legs straighten and
//   swing out about it. Mid-fall the lying drawing takes over (where the
//   silhouettes overlap most, measured per kill), at the body's height, and
//   finishes the fall under gravity; it lands with a brief settle, then the
//   corpse.
// - Chest blast: the death drawings already are one, so it keeps them and
//   the force varies them: the shoulders recoil on the hit drawing's rig
//   while the legs hold, a harder hit moves through the drawings sooner,
//   and the body thrown off its feet flies a ballistic arc before it lands.
//   The game's own momentum carries it back.
// Each texel is drawn as its own quad, moved by the rig: the bend is
// continuous over each part and blended over a few rows at ankle, knee and
// hip, so the body stays joined and texels stay sharp. Lying drawings are
// set down on the floor by their lowest solid row (the art floats a few
// units). A low hit or a weak push buckles the legs; a hard push throws.
struct FallRig {
    const doom_stock_rig *data=nullptr;   // the hand rig, or found below
    doom_stock_rig found;
    bool hasHead=false;doom_joints::Head head{};   // where a shot through the head lands, and the neck it turns about
    int lump=-1,width=0,height=0,left=0,top=0;
    std::vector<byte> carried;      // per texel: moves with the torso whatever its height
    std::vector<byte> opaque;
};
struct Pt { float x,y; };
static Pt lerpPt(Pt a,Pt b,float w) {return {a.x+(b.x-a.x)*w,a.y+(b.y-a.y)*w};}
static Pt turn(Pt p,Pt pivot,float a) {
    float c=std::cos(a),s=std::sin(a),dx=p.x-pivot.x,dy=p.y-pivot.y;
    return {pivot.x+dx*c-dy*s,pivot.y+dx*s+dy*c};
}
static float ease(float x) {x=std::clamp(x,0.0f,1.0f);return x*x*(3-2*x);}
static float pulse(float t,float peak) {return t<=0?0:t/peak*std::exp(1-t/peak);}

// Rigs depend on the artwork alone, so they're kept from level to level.
static std::unordered_map<int,std::unique_ptr<FallRig>> rigs;   // by sprite lump, null where none
static const FallRig *rigFor(int lump) {
    if(lump<0)return nullptr;
    auto found=rigs.find(lump);
    if(found!=rigs.end())return found->second.get();
    auto &slot=rigs[lump];
    const Image &image=lumpImage(lump,false);
    char name[9];memcpy(name,lumpinfo[lump].name,8);name[8]=0;
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    auto rig=std::make_unique<FallRig>();
    rig->data=doom_stock_rig_for(name,image.width,image.height,image.pixels.data());
    rig->carried.assign((size_t)image.width*image.height,0);
    if(!rig->data) {
        if(!doom_joints::find(image.pixels.data(),image.width,image.height,image.left,image.top,palette,rig->found,rig->carried))return nullptr;
        rig->data=&rig->found;
    }
    rig->hasHead=doom_joints::findHead(image.pixels.data(),image.width,image.height,image.left,image.top,rig->head);
    rig->lump=lump;rig->width=image.width;rig->height=image.height;rig->left=image.left;rig->top=image.top;
    rig->opaque.assign((size_t)image.width*image.height,0);
    for(int r=0;r<image.height;++r)for(int c=0;c<image.width;++c) {
        size_t i=(size_t)r*image.width+c;
        if(!image.pixels[i*2+1])continue;
        rig->opaque[i]=1;
        float u=c-image.left+0.5f,h=image.top-r-0.5f;
        for(const doom_rig_box &box:rig->data->carried)
            if(u>=box.u0&&u<=box.u1&&h>=box.h0&&h<=box.h1&&doom_rig_colour(box.colour,palette+image.pixels[i*2]*3))rig->carried[i]=1;
    }
    slot=std::move(rig);
    return slot.get();
}

// The rig's bend for one pose: each leg's thigh and shin as rigid bones
// (lengths kept, the knee by two-bone IK, out to the side), the torso riding
// the pelvis and turning about it, and once support is gone the legs
// swinging about the pelvis as it sinks.
struct Bone { Pt from,to;float c=1,s=0; Pt apply(Pt p) const {float dx=p.x-from.x,dy=p.y-from.y;return {to.x+dx*c-dy*s,to.y+dx*s+dy*c};} };
static Bone bone(Pt p0,Pt q0,Pt p1,Pt q1) {
    float a=std::atan2(q1.y-p1.y,q1.x-p1.x)-std::atan2(q0.y-p0.y,q0.x-p0.x);
    return {p0,p1,std::cos(a),std::sin(a)};
}
struct Field {
    const doom_stock_rig *d;Bone thigh[2],shin[2];Pt mid0,mid1,pivot{};float tilt,legs,sink,head=0,neck=0;
    Field(const FallRig &rig,const FallPose &pose):d(rig.data) {
        for(int leg=0;leg<2;++leg) {
            Pt hip0={d->hip[leg][0],d->hip[leg][1]},knee0={d->knee[leg][0],d->knee[leg][1]},ankle={d->ankle[leg][0],d->ankle[leg][1]};
            Pt hip1={hip0.x+pose.shift,hip0.y-pose.drops[leg]};
            float lt=std::hypot(knee0.x-hip0.x,knee0.y-hip0.y),ls=std::hypot(ankle.x-knee0.x,ankle.y-knee0.y);
            float dx=hip1.x-ankle.x,dy=hip1.y-ankle.y,dist=std::clamp(std::hypot(dx,dy),std::abs(lt-ls)+0.01f,lt+ls-0.01f);
            float a=(ls*ls-lt*lt+dist*dist)/(2*dist),h=std::sqrt(std::max(0.0f,ls*ls-a*a));
            Pt m={ankle.x+a*dx/dist,ankle.y+a*dy/dist},k1={m.x-h*dy/dist,m.y+h*dx/dist},k2={m.x+h*dy/dist,m.y-h*dx/dist};
            float outward=leg==0?-1:1;
            Pt knee1=(outward*k1.x>outward*k2.x)?k1:k2;
            if(pose.drops[leg]<0.5f)    // a leg barely bent keeps its knee where it was drawn
                knee1=std::hypot(k1.x-knee0.x,k1.y-knee0.y)<std::hypot(k2.x-knee0.x,k2.y-knee0.y)?k1:k2;
            thigh[leg]=bone(hip0,knee0,hip1,knee1);shin[leg]=bone(knee0,ankle,knee1,ankle);
        }
        float lx=d->hip[0][0],rx=d->hip[1][0];
        mid0={(lx+rx)/2,d->hip[0][1]};
        // The torso keeps itself mostly upright over a tilted pelvis (a quarter of the tilt shows).
        tilt=0.25f*std::atan2(pose.drops[0]-pose.drops[1],rx-lx)+pose.torso;
        mid1={mid0.x+pose.shift,mid0.y-(pose.drops[0]+pose.drops[1])/2};
        legs=pose.legs;sink=pose.sink;
        // The head turns about the neck, where the torso has taken it.
        if(rig.hasHead&&pose.head!=0) {
            head=pose.head;neck=rig.head.neck;
            pivot=turn({mid1.x+rig.head.middle-mid0.x,mid1.y+neck-mid0.y},mid1,tilt);
        }
    }
    Pt at(Pt p,int leg,bool carried) const {
        float wShin=ease((p.y-(d->ankle[leg][1]-1.5f))/3),wThigh=ease((p.y-(d->knee[leg][1]-1))/2);
        float wTorso=carried?1:ease((p.y-(d->hip[leg][1]-2))/4);
        Pt q=p;
        q=lerpPt(q,shin[leg].apply(p),wShin);
        q=lerpPt(q,thigh[leg].apply(p),wThigh);
        q=lerpPt(q,turn({mid1.x+p.x-mid0.x,mid1.y+p.y-mid0.y},mid1,tilt),wTorso);
        if(head!=0)q=lerpPt(q,turn(q,pivot,head),ease((p.y-(neck-1))/2));
        if(legs!=0)q=lerpPt(q,turn(q,mid1,legs),1-wTorso);
        q.y-=sink;
        return q;
    }
};

// Every opaque texel's four corners through the field, lifted if the bend
// takes the body lower than the drawing itself reaches (its feet may sit a
// little below its origin, as drawn).
template<class Visit> static void posedTexels(const FallRig &rig,const FallPose &pose,Visit visit) {
    Field field(rig,pose);
    std::vector<std::array<Pt,4>> cells;std::vector<std::pair<int,int>> where;
    float low=1e9f,drawnLow=1e9f;
    for(int r=0;r<rig.height;++r)for(int c=0;c<rig.width;++c) {
        size_t i=(size_t)r*rig.width+c;
        if(!rig.opaque[i])continue;
        float u0=(float)(c-rig.left),h0=(float)(rig.top-r);
        int leg=u0+0.5f<rig.data->split?0:1;bool carried=rig.carried[i];
        std::array<Pt,4> q={field.at({u0,h0},leg,carried),field.at({u0+1,h0},leg,carried),
                            field.at({u0+1,h0-1},leg,carried),field.at({u0,h0-1},leg,carried)};
        for(const Pt &p:q)low=std::min(low,p.y);
        drawnLow=std::min(drawnLow,h0-1);
        cells.push_back(q);where.push_back({c,r});
    }
    float lift=std::max(0.0f,std::min(0.0f,drawnLow)-low);
    for(size_t n=0;n<cells.size();++n) {
        for(Pt &p:cells[n])p.y+=lift;
        visit(where[n].first,where[n].second,cells[n]);
    }
}
void fallQuads(std::vector<Vertex> &out,const Vertex &base,const FallPose &pose,float x,float y,float feet,float rx,float ry,bool flip,
               const std::vector<byte> *holes) {
    float side=flip?-1.0f:1.0f;
    int width=pose.rig->width;
    posedTexels(*pose.rig,pose,[&](int c,int r,const std::array<Pt,4> &q) {
        byte wound=holes?(*holes)[(size_t)r*width+c]:0;    // shot through (wounds.cpp): skipped, or blood paint
        if(wound==1)return;
        auto corner=[&](const Pt &p,float u,float v) {
            Vertex vertex=base;vertex.x=x+rx*p.x*side;vertex.y=y+ry*p.x*side;vertex.z=feet+p.y;vertex.u=u;vertex.v=v;
            if(wound>=2)woundVertex(vertex,wound);
            return vertex;
        };
        quad(out,corner(q[0],(float)c,(float)r),corner(q[1],(float)c+1,(float)r),corner(q[2],(float)c+1,(float)r+1),corner(q[3],(float)c,(float)r+1));
    });
}

// A drawing set down on the floor: how far its lowest solid row (at least
// four texels, not a drop of blood below the body) floats above its origin.
// Drawings reaching below it are left as vanilla draws them.
static std::unordered_map<int,float> grounds;
static float groundOf(int lump) {
    auto found=grounds.find(lump);
    if(found!=grounds.end())return found->second;
    const Image &image=lumpImage(lump,false);
    float ground=0;
    for(int r=image.height-1;r>=0;--r) {
        int solid=0;
        for(int c=0;c<image.width;++c)solid+=image.pixels[((size_t)r*image.width+c)*2+1]!=0;
        if(solid>=4) {ground=std::max(0.0f,(float)(image.top-r-1));break;}
    }
    return grounds[lump]=ground;
}
static int lumpOf(int state) {
    if(state<=0)return -1;
    const state_t &s=states[state];
    if(s.sprite<0||s.sprite>=numsprites)return -1;
    int frame=s.frame&FF_FRAMEMASK;
    if(frame>=sprites[s.sprite].numframes)return -1;
    return firstspritelump+sprites[s.sprite].spriteframes[frame].lump[0];
}

enum { FALL_LEGS,FALL_CHEST,FALL_HEAD };
constexpr float fallStep=3;    // tics a pose holds
struct FallTimes { float land=17,height=0,speed=2.5f; };
struct Fall {
    int kind,start;std::vector<int> steps;int pain,lying;float force;
    int rigState=-1;                // simulated: the state whose drawing the rig moves (-1: vanilla's drawings)
    bool mirror=false;              // the art mirrored left to right: a coin flip, for variety
    bool lyingMirror=false;         // the lying drawings mirrored (they lie to their right as drawn)
    std::vector<FallPose> poses;    // legs swept: the simulated pose of each tic until the hand-off
    FallTimes times;
};
static std::unordered_map<const mobj_t*,Fall> falls;

// Legs swept, simulated per kill (tests/death_sim.py): a small body model in
// the drawing's plane, in units and tics, giving the controls the rig draws.
// - Knees: each hip is held up by its leg's muscle and pulled down by
//   gravity, damped. The hit leg lets go first (kicked out at once by a low
//   hit, a tic or two after a body or shoulder hit, faster the harder), the
//   other a few tics later (or holding a while, the body hanging on it).
//   The pelvis is rigid: a leg still holding stops the other hip; one that
//   has let go comes along.
// - Pelvis: the push slides it toward the failing side, braked while the
//   legs hold; it drifts toward the lower hip.
// - Torso: an inverted pendulum on the pelvis, balancing while the legs hold,
//   thrown toward the side the pelvis drops; a hit above the hips first
//   snaps the shoulders back.
// - Support gone (the pelvis half down, both legs let go, or the torso past
//   the feet): the pelvis falls freely to the floor, the legs straighten and
//   swing out about it, the torso keeps going over.
// Worked out for a fall to the drawing's left and turned over for one to its
// right (legs swapped, sideways motion and turns reversed). The seed varies
// when each leg goes, how fast, and how hard the body fights to stay up.
constexpr float fallGravity=0.5f;   // units per tic per tic: brisk, like Doom's own
// Two more modes run on the same body:
// - A shot through the head ("strings cut"): both knees go at once, nothing
//   balances, the pelvis drops nearly in place, the head snaps toward the
//   fall's side and the body follows it down.
// - A blast to the chest, on the hit drawing: the knees lock a few tics while
//   the shoulders are kicked over, then give.
enum { SIM_LEGS,SIM_HEAD,SIM_CHEST };
static std::vector<FallPose> simulateLegs(const FallRig &rig,float force,float high,uint32_t seed,int side,int mode,std::vector<float> &progress) {
    bool head=mode==SIM_HEAD;
    uint32_t state=seed;
    auto rnd=[&] {state=hash32(state+0x9e3779b9u);return (float)(state>>8)/16777216.0f;};
    auto muscle=[](float t,float start,float pace) {return t<=start?1.0f:std::exp(-(t-start)/pace);};
    const doom_stock_rig &d=*rig.data;
    float leg=d.hip[0][1]-d.ankle[0][1],k=leg/23,width=d.hip[1][0]-d.hip[0][0],hipH=d.hip[0][1];
    bool low=high<-0.2f,shoulders=high>0.3f;
    float hitStart=low?0:(shoulders?1.5f:0.5f)+1.5f*rnd();
    float hitPace=(low?0.8f:2.0f)*(1.4f-0.5f*force)*(0.8f+0.4f*rnd());
    float r=rnd(),otherStart=hitStart+0.5f+11*r*r,otherPace=2+5*rnd();
    float balance=0.01f+0.25f*rnd(),give=0.08f+0.12f*rnd(),torsoLen=0.8f*leg,g=fallGravity;
    float drops[2]={0,0},speeds[2]={low?(0.6f+1.2f*force)*k:0.0f,0};
    float x=0,vx=-(0.15f+0.35f*force)*k,th=0,w=shoulders?-0.10f*force*high:(rnd()-0.5f)*0.04f;
    float snap=0.35f+0.25f*rnd();   // the head's snap (radians)
    if(head) {
        hitStart=0;hitPace=0.7f+0.5f*rnd();otherStart=0.6f*rnd();otherPace=0.8f+0.7f*rnd();
        balance=0;give=0.10f+0.08f*rnd();
        speeds[0]=(0.3f+0.3f*rnd())*k;speeds[1]=(0.3f+0.3f*rnd())*k;   // the knees fold at once
        vx*=0.3f;w=0.07f+0.05f*rnd();                                  // nothing holds the torso
    }
    if(mode==SIM_CHEST) {
        hitStart=2.5f+2*rnd();hitPace=1.5f+rnd();otherStart=hitStart+0.5f+3*rnd();otherPace=2+2*rnd();
        balance=0.02f+0.06f*rnd();
        w=(0.12f+0.12f*force)*(0.8f+0.4f*rnd());vx=-(0.3f+0.5f*force)*k;
    }
    auto neckTurn=[&](float t) {return head?snap*std::min(1.0f,t)*(0.6f+0.4f*std::exp(-std::max(0.0f,t-1)/2)):0.0f;};
    bool gone=false;float goneDrops[2]={0,0},sink=0,vsink=0;
    std::vector<FallPose> out;progress.clear();
    const float h=0.25f;
    for(int step=0;step<=48*4;++step) {
        float t=step*h;
        if(step%4==0) {
            FallPose p;p.rig=&rig;p.active=true;p.shift=x;p.torso=th;p.head=neckTurn(t);
            float u=0;
            if(!gone) {p.drops[0]=drops[0];p.drops[1]=drops[1];}
            else {
                float room=std::max(1.0f,hipH-(goneDrops[0]+goneDrops[1])/2-6*k);
                u=std::min(1.0f,sink/room);
                p.drops[0]=goneDrops[0]*(1-u);p.drops[1]=goneDrops[1]*(1-u);p.legs=1.3f*u;p.sink=sink;
            }
            if(side>0) {std::swap(p.drops[0],p.drops[1]);p.shift=-p.shift;p.torso=-p.torso;p.legs=-p.legs;p.head=-p.head;}
            out.push_back(p);progress.push_back(u);
        }
        float held[2]={muscle(t,hitStart,hitPace),muscle(t,otherStart,otherPace)},m=(held[0]+held[1])/2;
        if(!gone) {
            for(int i=0;i<2;++i) {
                speeds[i]+=(g*(1-held[i])-give*speeds[i])*h;
                drops[i]=std::min(0.85f*leg,drops[i]+speeds[i]*h);
            }
            float spread=drops[0]-drops[1];
            if(std::abs(spread)>0.8f*width) {
                int lo=spread>0?0:1,hi=1-lo;float excess=std::abs(spread)-0.8f*width;
                if(held[hi]>0.5f) {drops[lo]-=excess;speeds[lo]=std::min(speeds[lo],speeds[hi]);}
                else {drops[hi]+=excess;speeds[hi]=std::max(speeds[hi],speeds[lo]*0.7f);}
            }
            vx+=(-(0.12f+0.3f*m)*vx-0.01f*spread*k)*h;
            x+=vx*h;
            float alpha=(g/torsoLen)*std::sin(th)-balance*m*th-0.2f*w+0.03f*(speeds[0]-speeds[1])/k;
            w+=alpha*h;th+=w*h;
            if((drops[0]+drops[1])/2>0.45f*leg||std::max(held[0],held[1])<0.12f||th>0.9f) {gone=true;goneDrops[0]=drops[0];goneDrops[1]=drops[1];}
        } else {
            vsink+=g*h;
            float room=std::max(1.0f,hipH-(goneDrops[0]+goneDrops[1])/2-6*k);
            sink=std::min(room,sink+vsink*h);
            x+=vx*h;vx*=0.95f;
            w+=((g/torsoLen)*std::sin(std::max(0.25f,th))-0.1f*w)*h;
            th=std::min(1.45f,th+w*h);
        }
    }
    return out;
}
// Where a legs-swept fall hands over to the lying drawing: mid-fall (progress
// 0.4-0.8), the tic where the rig's texels and the lying drawing's (set down,
// mirrored for a fall to the left) overlap most; then how high the body still
// is and how fast it comes down. Measured per kill, the fall being its own.
static FallTimes legsHandoff(const FallRig &rig,const std::vector<FallPose> &poses,const std::vector<float> &progress,int lying,bool lyingLeft) {
    FallTimes times;
    const Image &image=lumpImage(lying,false);
    float ground=groundOf(lying);
    auto cellsOf=[&](const FallPose &pose,float &middle,float &top) {
        std::set<std::pair<int,int>> cells;double sum=0;int n=0;top=-1e9f;
        posedTexels(rig,pose,[&](int,int,const std::array<Pt,4> &q) {
            float cx=(q[0].x+q[2].x)/2,cy=(q[0].y+q[2].y)/2;
            cells.insert({(int)std::floor(cx),(int)std::ceil(cy)});sum+=cy;++n;top=std::max(top,std::max(q[0].y,q[1].y));
        });
        middle=n?(float)(sum/n):0;return cells;
    };
    std::set<std::pair<int,int>> target;double lyingSum=0;int lyingCount=0;
    for(int r=0;r<image.height;++r)for(int c=0;c<image.width;++c) if(image.pixels[((size_t)r*image.width+c)*2+1]) {
        float u=c-image.left+0.5f,cx=lyingLeft?-u:u,cy=image.top-r-0.5f-ground;
        target.insert({(int)std::floor(cx),(int)std::ceil(cy)});lyingSum+=cy;++lyingCount;
    }
    int land=-1;float best=-1;
    for(size_t t=1;t<poses.size();++t) {
        if(progress[t]<0.4f||progress[t]>0.8f)continue;
        float middle,top;auto mine=cellsOf(poses[t],middle,top);
        for(int dx=-10;dx<=10;++dx) {
            size_t both=0;
            for(const auto &cell:target)both+=mine.count({cell.first+dx,cell.second});
            float iou=(float)both/(float)(mine.size()+target.size()-both);
            if(iou>best) {best=iou;land=(int)t;}
        }
    }
    if(land<0) {    // the fall never reached mid-way: hand over where it got furthest
        land=1;
        for(size_t t=1;t<poses.size();++t)if(progress[t]>progress[land])land=(int)t;
    }
    float middle,top,middleBefore,topBefore;
    cellsOf(poses[land],middle,top);cellsOf(poses[land-1],middleBefore,topBefore);
    times.land=(float)land;
    times.height=std::max(0.0f,middle-(lyingCount?(float)(lyingSum/lyingCount):0));
    times.speed=std::max(2.5f,topBefore-top);
    return times;
}

static std::vector<int> deathSteps(int first) {
    std::vector<int> steps;
    for(int state=first,n=0;n<64&&state>0;++n) {
        if(std::find(steps.begin(),steps.end(),state)!=steps.end())break;
        steps.push_back(state);
        if(states[state].tics<0)break;
        state=states[state].nextstate;
    }
    return steps;
}
bool startFall(const mobj_t &thing,float speed,float high,float side,float hitZ,float hitU) {
    if(!settings.deathFalls)return false;
    const mobjinfo_t &info=*thing.info;
    Fall fall;fall.start=leveltime;fall.steps=deathSteps(info.deathstate);
    if(fall.steps.size()<4)return false;
    fall.pain=lumpOf(info.painstate);
    // The first drawing under 60% of the hit drawing's height lies; the last is the corpse.
    int standing=lumpOf(fall.steps[0]);
    if(standing<0)return false;
    float tall=(float)lumpImage(standing,false).top;
    fall.lying=-1;
    for(size_t i=1;i<fall.steps.size();++i) {
        int lump=lumpOf(fall.steps[i]);
        if(lump>=0&&lumpImage(lump,false).top-groundOf(lump)<0.6f*tall) {fall.lying=(int)i;break;}
    }
    if(fall.lying<0)return false;
    bool legs=fall.pain>=0&&rigFor(fall.pain),chest=rigFor(standing)&&fall.lying>=3;
    // A shot through the head: the killing blow struck the pain drawing's head (above its
    // neck, within its width). Drawing only; the game's damage is untouched.
    const FallRig *painRig=legs?rigFor(fall.pain):nullptr;
    bool headshot=painRig&&painRig->hasHead&&hitZ>=painRig->head.neck&&
        std::abs(hitU-painRig->head.middle)<=painRig->head.width/2+1;
    // -deathfalls 2|3|4 forces legs swept, chest blast or head shot for every fall, for judging each
    // (once F4 choices; F4's Deaths only turns the falls on by the hit).
    int choice=settings.deathFalls,forced=M_CheckParm((char*)"-deathfalls");
    if(forced&&forced+1<myargc)choice=atoi(myargv[forced+1]);
    if(choice==2)fall.kind=FALL_LEGS;
    else if(choice==3)fall.kind=FALL_CHEST;
    else if(choice==4)fall.kind=FALL_HEAD;
    else fall.kind=headshot?FALL_HEAD:high<-0.2f||speed<2.0f?FALL_LEGS:FALL_CHEST;   // strings cut, support knocked away, or thrown
    if(((fall.kind==FALL_LEGS||fall.kind==FALL_HEAD)&&!legs)||(fall.kind==FALL_CHEST&&!chest))fall.kind=legs?FALL_LEGS:chest?FALL_CHEST:-1;
    if(fall.kind<0)return false;
    fall.force=std::clamp(speed/10,0.4f,1.0f);
    uint32_t seed=hash32((uint32_t)(uintptr_t)&thing*2654435761u^(uint32_t)leveltime*40503u);
    // The body goes over on the side the hit came from (as seen by the shooter): for legs
    // swept the leg there gives, for a chest blast that shoulder is driven back and down,
    // and a shot through the head snaps it away from the side it struck. So bodies go over
    // toward their weapon or away from it; mirroring the art is a coin flip on top.
    bool right=std::abs(side)>0.2f?side<0:(seed&1);   // on screen
    if(fall.kind==FALL_HEAD&&painRig&&painRig->hasHead) {
        float off=hitU-painRig->head.middle;
        right=std::abs(off)>1.5f?off<0:(seed&1);
    }
    fall.mirror=(seed>>1)&1;
    // Two of a kind killed in the same tic never fall alike, whether it makes sense or not:
    // a later one takes the other direction, the other mirroring, both, then the other fall.
    {
        struct Taken { int type,kind;bool right,mirror; };
        static std::vector<Taken> taken;static int takenTic=-1;
        if(takenTic!=leveltime) {taken.clear();takenTic=leveltime;}
        auto used=[&](int kind,bool r,bool m) {
            for(const Taken &t:taken)if(t.type==thing.type&&t.kind==kind&&t.right==r&&t.mirror==m)return true;
            return false;
        };
        if(used(fall.kind,right,fall.mirror)) {
            int other=fall.kind==FALL_CHEST?(legs?FALL_LEGS:-1):(chest?FALL_CHEST:-1);
            bool found=false;
            for(int kind:{fall.kind,other}) {
                if(kind<0)continue;
                for(int flips=0;flips<4&&!found;++flips) {
                    bool r=right!=((flips&1)!=0),m=fall.mirror!=((flips&2)!=0);
                    if(!used(kind,r,m)) {fall.kind=kind;right=r;fall.mirror=m;found=true;}
                }
                if(found)break;
            }
        }
        taken.push_back({thing.type,fall.kind,right,fall.mirror});
    }
    int toward=(right!=fall.mirror)?1:-1;              // in the drawing
    fall.lyingMirror=(toward<0)!=fall.mirror;
    // A chest blast going the way vanilla's drawings fall (to their right) keeps them;
    // going the other way, the simulation takes the hit drawing down instead.
    bool simulated=fall.kind!=FALL_CHEST||toward<0||fall.lying<3;
    if(simulated) {
        fall.rigState=fall.kind==FALL_CHEST?fall.steps[0]:info.painstate;
        const FallRig &rig=*rigFor(lumpOf(fall.rigState));
        std::vector<float> progress;
        int mode=fall.kind==FALL_HEAD?SIM_HEAD:fall.kind==FALL_CHEST?SIM_CHEST:SIM_LEGS;
        fall.poses=simulateLegs(rig,fall.force,high,hash32(seed^0x51ed27u),toward,mode,progress);
        fall.times=legsHandoff(rig,fall.poses,progress,lumpOf(fall.steps[fall.lying]),toward<0);
    }
    falls[&thing]=fall;
    return true;
}
void forgetFall(const mobj_t *thing) {falls.erase(thing);}
void fallsShutdown() {falls.clear();}
bool rigJoints(int lump,RigJoints &out) {
    const FallRig *rig=rigFor(lump);
    if(!rig)return false;
    const doom_stock_rig &d=*rig->data;
    memcpy(out.hip,d.hip,sizeof out.hip);memcpy(out.knee,d.knee,sizeof out.knee);memcpy(out.ankle,d.ankle,sizeof out.ankle);
    out.split=d.split;out.hasHead=rig->hasHead;out.guided=d.guided;
    out.neck=rig->head.neck;out.headMiddle=rig->head.middle;out.headWidth=rig->head.width;
    out.carried=&rig->carried;out.width=rig->width;out.height=rig->height;out.left=rig->left;out.top=rig->top;
    return true;
}
// At level load: rig every killable monster's pain and first death drawing,
// so no death waits on finding joints.
void prepareFalls() {
    if(!settings.deathFalls&&!settings.deathRagdoll)return;
    for(int type=0;type<NUMMOBJTYPES;++type) {
        const mobjinfo_t &info=mobjinfo[type];
        if(!(info.flags&MF_COUNTKILL))continue;
        rigFor(lumpOf(info.painstate));rigFor(lumpOf(info.deathstate));
    }
}

bool fallView(const mobj_t &thing,float fraction,DeathView &view) {
    auto found=falls.find(&thing);
    if(found==falls.end())return false;
    const Fall &f=found->second;
    // Crushed to gibs or otherwise moved on: the game's own frames again.
    if(std::find(f.steps.begin(),f.steps.end(),(int)(thing.state-states))==f.steps.end())return false;
    // Stepped like a Doom sprite: a new pose on whole tics, every few, never
    // in between (a pose every rendered frame read smoother than any death
    // the game draws). The drawings themselves hold 5 tics; the falls step
    // at 3 so their extra poses still show.
    (void)fraction;
    float t=std::max(0.0f,std::floor((leveltime-1-f.start)/fallStep)*fallStep);
    int tics=std::max(1,(int)states[f.steps[0]].tics);
    view.facing=thing.angle;view.bend=0;view.settle=0;view.fall=FallPose{};view.fall.active=true;
    auto show=[&](int state,bool mirror,float lift,float flatten) {
        view.sprite=states[state].sprite;view.frame=states[state].frame;view.mirror=mirror;
        int lump=lumpOf(state);
        view.fall.ground=lump>=0?groundOf(lump):0;view.fall.lift=lift;view.fall.flatten=flatten;
    };
    int lying=f.steps[f.lying],corpse=f.steps.back();
    // Down: any drawings between the lying one and the corpse, as the game times them, then the corpse.
    auto after=[&](float from,bool mirror) {
        for(size_t i=f.lying+1;i+1<f.steps.size();++i) {
            from+=std::max(1,(int)states[f.steps[i-1]].tics);
            if(t<from+std::max(1,(int)states[f.steps[i]].tics)) {show(f.steps[i],mirror,0,0);return;}
        }
        show(corpse,mirror,0,0);
    };
    if(f.rigState>=0) {
        const FallTimes &times=f.times;
        bool mirror=f.lyingMirror;
        if(t<times.land) {
            view.sprite=states[f.rigState].sprite;view.frame=states[f.rigState].frame;
            view.mirror=f.mirror;view.fall=f.poses[std::min(f.poses.size()-1,(size_t)t)];
            return true;
        }
        float g=1,drop=(-times.speed+std::sqrt(times.speed*times.speed+2*g*times.height))/g,floorAt=times.land+drop;
        if(t<floorAt) {float into=t-times.land;show(lying,mirror,std::max(0.0f,times.height-times.speed*into-0.5f*g*into*into),0);}
        else if(t<floorAt+tics) show(lying,mirror,0,0.12f*std::sin(doomPi*std::min(1.0f,(t-floorAt)/4)));
        else after(floorAt,mirror);
        return true;
    }
    // Chest blast: the death drawings, varied by force: the recoil on the hit
    // drawing, then on through the drawings to the lying one, faster for a
    // harder hit. The last of them is thrown in an arc when its art floats
    // off the floor (the zombie's); one standing on its feet (the
    // chaingunner toppling) stays down.
    float force=f.force,hit=tics*(1.1f-0.5f*force),back=tics*(1.0f-0.4f*force);
    if(t<hit) {
        const FallRig &rig=*rigFor(lumpOf(f.steps[0]));
        view.sprite=states[f.steps[0]].sprite;view.frame=states[f.steps[0]].frame;view.mirror=f.mirror;
        FallPose pose;pose.rig=&rig;pose.active=true;
        pose.torso=-(0.35f*force*(1-std::exp(-t/1.2f))+0.1f*force*pulse(t,2));   // the shoulders snap back
        pose.shift=force*ease(t/3);                                                 // the hips pushed the other way
        view.fall=pose;
        return true;
    }
    float land=hit;
    for(int i=1;i<f.lying;++i) {
        int lump=lumpOf(f.steps[i]);
        if(i<2||i<f.lying-1||lump<0||groundOf(lump)<4) {
            if(t<land+back) {show(f.steps[i],f.mirror,0,0);return true;}
            land+=back;continue;
        }
        float lift0=4,up=1+3*force,g=1,flight=(up+std::sqrt(up*up+2*g*lift0))/g;
        if(t<land+flight) {float into=t-land;show(f.steps[i],f.mirror,lift0+up*into-0.5f*g*into*into,0);return true;}
        land+=flight;
    }
    if(t<land+tics) show(lying,f.mirror,0,(0.08f+0.08f*force)*std::sin(doomPi*std::min(1.0f,(t-land)/4)));
    else after(land,f.mirror);
    return true;
}
} // namespace doom3d
