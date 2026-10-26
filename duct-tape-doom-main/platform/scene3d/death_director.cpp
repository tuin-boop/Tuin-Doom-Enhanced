/* Varied deaths: a dying monster's frames retimed afresh for each kill, so
   two bodies dropped by one blast never fall alike. Drawing only. */
#include "internal.h"

namespace doom3d {
// The game's death frames are drawn for one angle and play alike every
// time. From the killing blow (startDeath, from body_motion.cpp) the
// drawing takes them over, all of it settled at the kill so what the
// player sees doesn't depend on which view draws the body first:
// - the death frames keep their order but not their timing: the time is
//   shared out afresh between the reaction (first frame), the collapse and
//   the settle (from the frame the body lands on). One body snaps down,
//   another hesitates and then drops, another slumps. Bodies struck by
//   one attack (one blast, one rocket) take contrasting timings on purpose;
// - now and then an accent before the fall, from the monster's own rotated
//   frames: a hit well off its middle turns it (the struck side driven
//   back), a hard push steps it back on its walk frames, a kill with no
//   push (the chainsaw) holds its pain frame a moment;
// - optionally (F4) a body killed facing away turns toward the eye before
//   death frames drawn facing it, instead of snapping round;
// - the frames are mirrored so the body falls away from the blow as seen by
//   the player when the art clearly falls to one side, else either way;
// - the frame it lands on flattens briefly (the settle), and the death
//   jolt of body_motion.cpp follows the phases: full until the collapse,
//   gone once the body lies.
// A dying body stays solid until its death frames clear that (A_Fall), so
// while it does the drawing never runs ahead of the game's own frame.
// The game's states and timing are untouched. Variation comes from
// hashes, never P_Random.
struct DeathArt { std::vector<int> steps;float drift=0;int landing=-1; };
static std::unordered_map<int,DeathArt> arts;
enum { STAGGER_NONE,STAGGER_HOLD,STAGGER_SPIN,STAGGER_STEP };
// Phase time scales (reaction, collapse, settle): above 1 slower. Listed
// in contrast order: each pace differs most from the one before it.
enum { PACE_SNAP,PACE_HESITATE,PACE_SLUMP,PACE_PLAIN,PACES };
constexpr float paceScales[PACES][3]={{0.6f,0.75f,1},{1.8f,0.65f,1},{1,1.4f,1.1f},{1,1,1}};
struct Death { int start,first;int stagger;float staggerTics,spin;int pace;float overall;angle_t facing;bool mirror; };
static std::unordered_map<const mobj_t*,Death> deaths;
// This tic's attacks (what did the damage) and the pace of the last body each dropped.
static std::unordered_map<const mobj_t*,int> attackPace;
static int attackTic=-1;

static angle_t toAngle(float radians) {return (angle_t)(int64_t)std::llround(radians/(2*doomPi)*4294967296.0);}
// A frame's opaque texels as drawn (rotation 0): their middle in units right
// of the thing's middle, and their top and bottom in units above its feet.
struct Extent { float middle=0,top=0,bottom=0;bool any=false; };
static Extent frameExtent(int spriteNum,int frame) {
    Extent e;
    if(spriteNum<0||spriteNum>=numsprites)return e;
    const auto &sprite=sprites[spriteNum];frame&=FF_FRAMEMASK;
    if(frame>=sprite.numframes)return e;
    const auto &sf=sprite.spriteframes[frame];
    const Image &image=lumpImage(firstspritelump+sf.lump[0],false);
    double sum=0;long count=0;int first=-1,last=-1;
    for(int y=0;y<image.height;++y)for(int x=0;x<image.width;++x)
        if(image.pixels[((size_t)y*image.width+x)*2+1]) {sum+=x+0.5;++count;if(first<0)first=y;last=y;}
    if(!count)return e;
    float mean=(float)(sum/count);
    e.middle=sf.flip[0]?image.width-mean-image.left:mean-image.left;
    e.top=(float)(image.top-first);e.bottom=(float)(image.top-last-1);e.any=true;
    return e;
}
// A death sequence's states up to its resting frame, how far its body
// drifts sideways (positive: falls to the right as drawn), and the step it
// lands on: the first frame under 45% of the standing height whose bottom
// touches the floor, after a taller one. Fliers, art drawn off its origin
// and odd anatomy fail those tests, and then there is no landing.
static const DeathArt &deathArt(int first,const mobjinfo_t &info) {
    auto found=arts.find(first);
    if(found!=arts.end())return found->second;
    DeathArt &art=arts[first];
    for(int state=first,n=0;n<64&&state>0;++n) {
        if(std::find(art.steps.begin(),art.steps.end(),state)!=art.steps.end())break;
        art.steps.push_back(state);
        if(states[state].tics<0)break;
        state=states[state].nextstate;
    }
    std::vector<Extent> extents;
    for(int state:art.steps)extents.push_back(frameExtent(states[state].sprite,states[state].frame));
    if(extents.size()>=2)art.drift=extents.back().middle-extents.front().middle;
    Extent standing=info.spawnstate>0?frameExtent(states[info.spawnstate].sprite,states[info.spawnstate].frame):Extent{};
    if(standing.any&&standing.top>8&&!(info.flags&MF_FLOAT))
        for(size_t i=1;i<extents.size();++i) {
            const Extent &e=extents[i];
            if(e.any&&e.top<standing.top*0.45f&&std::abs(e.bottom)<=6&&extents[i-1].top>e.top) {art.landing=(int)i;break;}
        }
    return art;
}
void startDeath(const mobj_t &thing,float dx,float dy,float speed,float side,float high,bool gib,const mobj_t *attack,float hitZ,float hitU) {
    if(!settings.deathDirector)return;
    // A rigged monster falls procedurally instead (death_falls.cpp).
    if(!gib&&startFall(thing,speed,high,side,hitZ,hitU))return;
    const mobjinfo_t &info=*thing.info;
    int first=gib?info.xdeathstate:info.deathstate;
    if(first<=0)return;
    const DeathArt &art=deathArt(first,info);
    uint32_t seed=hash32((uint32_t)(uintptr_t)&thing*2654435761u^(uint32_t)leveltime*40503u);
    auto unit=[&](int k){return hashUnit(k,7,seed);};
    Death d={leveltime,first,STAGGER_NONE,0,0,PACE_PLAIN,1,thing.angle,false};
    // Accents, kept rare so they don't become a pattern of their own.
    bool pain=info.painstate>0,walk=info.seestate>0;
    if(!gib) {
        if(pain&&std::abs(side)>0.5f&&unit(1)<0.35f) {
            // Struck off its middle: the struck side is driven back. Side is
            // positive on the shooter's left, which turns the body clockwise.
            float turn=std::clamp(0.2f+std::abs(side)*0.2f+speed/40,0.25f,0.5f)*doomPi*(0.8f+0.4f*unit(2));
            d.stagger=STAGGER_SPIN;d.spin=side>0?-turn:turn;d.staggerTics=4+2*unit(3);
        } else if(speed<0.6f&&pain) {d.stagger=STAGGER_HOLD;d.staggerTics=3+unit(3);}
        else if(speed>6&&walk&&unit(4)<0.4f) {d.stagger=STAGGER_STEP;d.staggerTics=3+2*unit(3);}
    }
    // Timing: a lone kill's pace leans on its push; each further body
    // dropped by the same attack this tic takes the next pace in contrast order.
    if(attackTic!=leveltime) {attackPace.clear();attackTic=leveltime;}
    auto before=attack?attackPace.find(attack):attackPace.end();
    if(before!=attackPace.end())d.pace=(before->second+1)%PACES;
    else {
        float pick=unit(5);
        if(speed>6)d.pace=pick<0.5f?PACE_SNAP:pick<0.8f?PACE_PLAIN:PACE_HESITATE;
        else if(speed<1.5f)d.pace=pick<0.4f?PACE_SLUMP:pick<0.7f?PACE_HESITATE:PACE_PLAIN;
        else d.pace=std::min((int)(pick*PACES),PACES-1);
    }
    if(gib)d.pace=PACE_PLAIN;
    if(attack)attackPace[attack]=d.pace;
    d.overall=gib?0.9f+0.2f*unit(7):std::clamp(1-(speed-3)/20,0.8f,1.15f)*(0.92f+0.16f*unit(7));
    // Seen from the player at the kill: which side the blow pushes, and where the eye is.
    const mobj_t *viewer=players[displayplayer].mo;
    float rx=1,ry=0,ex=units(thing.x),ey=units(thing.y);
    if(viewer) {
        float a=(float)(viewer->angle*(2*doomPi/4294967296.0));
        rx=std::sin(a);ry=-std::cos(a);ex=units(viewer->x);ey=units(viewer->y);
    }
    float across=dx*rx+dy*ry;
    // Shot along the line of sight (the usual case: the player shoots what it
    // sees), or art that falls straight down: either way, for variety.
    if(std::abs(across)<0.25f||std::abs(art.drift)<2)d.mirror=unit(8)<0.5f;
    else d.mirror=(across>0)!=(art.drift>0);
    if(settings.deathFaceViewer&&viewer) {
        float toEye=std::atan2(ey-units(thing.y),ex-units(thing.x));
        float delta=std::remainder(toEye-(float)(thing.angle*(2*doomPi/4294967296.0)),(float)(2*doomPi));
        if(d.stagger==STAGGER_SPIN) {
            // End facing the eye: the blow's way round unless that's past a half turn, then the short way.
            float needed=d.spin>0?(delta<0?delta+2*doomPi:delta):(delta>0?delta-2*doomPi:delta);
            d.spin=std::abs(needed)<=doomPi?needed:delta;
            d.staggerTics=std::max(d.staggerTics,3+2.5f*std::abs(d.spin)/doomPi);
        } else if(!gib&&std::abs(delta)>doomPi*0.5f&&pain) {
            d.stagger=STAGGER_SPIN;d.spin=delta;d.staggerTics=3+2.5f*std::abs(delta)/doomPi;
        }
    }
    // Two of a kind killed in the same tic never die alike: a later one takes a pace
    // none of them took this tic, and the mirroring the last of them didn't.
    {
        struct Taken { int type,pace;bool mirror; };
        static std::vector<Taken> taken;static int takenTic=-1;
        if(takenTic!=leveltime) {taken.clear();takenTic=leveltime;}
        const Taken *last=nullptr;
        for(const Taken &t:taken)if(t.type==thing.type)last=&t;
        if(last) {
            for(int n=0;n<PACES;++n) {
                int pace=(d.pace+n)%PACES;bool free=true;
                for(const Taken &t:taken)if(t.type==thing.type&&t.pace==pace)free=false;
                if(free) {d.pace=pace;break;}
            }
            d.mirror=!last->mirror;
        }
        taken.push_back({thing.type,d.pace,d.mirror});
    }
    deaths[&thing]=d;
}
void forgetDeath(const mobj_t *thing) {deaths.erase(thing);forgetFall(thing);forgetRagdoll(thing);}
void deathsShutdown() {deaths.clear();arts.clear();attackPace.clear();attackTic=-1;fallsShutdown();ragdollsShutdown();}

bool deathView(const mobj_t &thing,float fraction,DeathView &view) {
    if(!settings.deathDirector)return false;
    if(settings.deathFalls&&fallView(thing,fraction,view))return true;
    auto found=deaths.find(&thing);
    if(found==deaths.end()||thing.health>0||!(thing.flags&MF_CORPSE)||!thing.state)return false;
    const Death &d=found->second;
    const mobjinfo_t &info=*thing.info;
    const DeathArt &art=deathArt(d.first,info);
    // Crushed to gibs or otherwise moved on: the game's own frames again.
    auto at=std::find(art.steps.begin(),art.steps.end(),(int)(thing.state-states));
    if(at==art.steps.end())return false;
    int gameStep=(int)(at-art.steps.begin());
    float t=std::max(0.0f,leveltime+fraction-1-d.start);
    auto show=[&](int state){view.sprite=states[state].sprite;view.frame=states[state].frame;};
    view.bend=1;view.settle=0;
    if(d.stagger!=STAGGER_NONE&&t<d.staggerTics) {
        view.mirror=false;view.facing=d.facing;
        if(d.stagger==STAGGER_STEP) {
            // Walk frames backward, two tics each, as the push carries it back.
            std::vector<int> walk;
            for(int state=info.seestate,n=0;n<4&&state>0;++n,state=states[state].nextstate) {
                if(std::find(walk.begin(),walk.end(),state)!=walk.end())break;
                walk.push_back(state);
            }
            if(!walk.empty()) {show(walk.at(walk.size()-1-((int)(t/2)%walk.size())));return true;}
        }
        show(info.painstate);
        if(d.stagger==STAGGER_SPIN) {float k=1-t/d.staggerTics;view.facing=d.facing+toAngle(d.spin*(1-k*k));}
        return true;
    }
    // The death frames in order, each held its tics scaled by its phase.
    int steps=(int)art.steps.size(),landing=art.landing>0?art.landing:steps;
    float u=t-(d.stagger!=STAGGER_NONE?d.staggerTics:0),clock=0,into=0,collapseStart=0,collapseEnd=-1;
    int pick=-1;
    for(int i=0;i<steps;++i) {
        int tics=states[art.steps.at(i)].tics;
        if(i==1)collapseStart=clock;
        if(i==landing)collapseEnd=clock;
        float length=tics*paceScales[d.pace][i==0?0:i<landing?1:2]*d.overall;
        if(pick<0&&(tics<0||i==steps-1||u<clock+length)) {pick=i;into=u-clock;}
        if(tics>=0)clock+=length;
    }
    // Still solid: never drawn past the game's own frame.
    if((thing.flags&MF_SOLID)&&pick>gameStep) {pick=gameStep;into=0;}
    show(art.steps.at(pick));view.facing=d.facing+toAngle(d.spin);view.mirror=d.mirror;
    // The jolt fades over the collapse; the landing frame flattens for 4 tics.
    float end=collapseEnd>=0?collapseEnd:clock;
    if(u>collapseStart)view.bend=std::clamp(1-(u-collapseStart)/std::max(1.0f,end-collapseStart),0.0f,1.0f);
    if(pick==art.landing&&into<4)view.settle=0.15f*std::sin(doomPi*into/4);
    return true;
}
} // namespace doom3d
