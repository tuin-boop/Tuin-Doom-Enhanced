/* Ragdoll falls: a rigged monster's body thrown down by the killing blow.
   The doll is eleven points on the drawing's own skeleton (head, neck,
   shoulders, the spine at the chest joint, hips, knees and ankles),
   Verlet-integrated in game tics and held together by bone lengths (the
   torso two stiff blocks hinged at the spine), joint limits that keep the
   bones' lengths and add no speed, the floors under it (the sectors round
   the kill on a grid: steps, ledges, the lower floor a corpse at a stair edge
   lies on) that stop bodies dead, and the walls and steps around it. Muscles, extra lengths across the joints, hold
   the standing pose at first and let go on a schedule the hit sets. It keeps
   the way it faced and falls in the world from where it died, with the
   corpse's slide; the hit tips it over the way it was struck, and while it
   falls it is twisted so it lands with a drawn side (front or back) up or
   toward the player; nothing turns it once it lies. Its relief
   (ragdoll_body.cpp) is skinned to it, and a virtual camera draws it back
   into a sprite (ragdollCard): poses quickly through the hit, then at the
   pace of the monster's own death frames, and at the top of a throw and
   the moments it touches down.
   Sometimes a chest blast plays the shot death on strings instead (guided
   falls, below). Drawing only. From the lab: tests/death_ragdoll.py. */
#include "ragdoll.h"

namespace doom3d {
constexpr float dollGravity=0.45f;  // units per tic per tic: 1.5x earth's at a body's scale, so throws read snappy (at earth's 0.3 a hard throw flew flat at chest height for half a second); Doom's 1 drops a body in 8 tics
constexpr int dollSubsteps=4,dollPasses=6;
constexpr float kneeTwist=0.35f;    // of a shin's length, how far it may stray sideways from the knee's hinge plane
constexpr int lyingGrace=3;         // tics the torso is down before the whole body is damped (Ragdoll::damp)
constexpr int earlyTics=8,earlyPose=2,airPose=5;   // the hit's first tics drawn every earlyPose, in the air at least every airPose (ragdollView)
constexpr float cardSink=3;         // units a lying body's card is pressed into the floor (ragdollCard)
constexpr float overKeep=0.7f;      // of a tumble past flat in the air, the share kept a substep (Ragdoll::damp)
constexpr float tiltMost=30;        // degrees the virtual camera is tipped down at most, lying (ragdollCard)
constexpr float carryMost=4.5f;
// With F4 "Flying enemies", an explosion's kill (rocket, barrel, BFG) sends the body flying: its slide kept up to blastCarry
// units a tic, and on top a throw along the blast (blastThrow), up (blastLift) and a back somersault
// (blastSpin radians a tic). At the shotgun's caps a rocket's 15 a tic only hopped it 6 units up.
constexpr float blastCarry=8,blastThrow=2.5f,blastLift=4,blastSpin=0.1f;
// And until it lands it comes back off a wall with blastRebound of the speed it hit it at, so it can
// glance off one column into the next (walls are dead otherwise: see the walls in Ragdoll::tic). No
// slam for it: pinned to the first wall it hit, it never reached a second.
constexpr float blastRebound=0.5f;
// The same switch gives shots more punch: a hitscan kill pushes hitPunch times as hard, its slide kept
// up to punchCarry, and a chest or side kill launches graded by the push (Ragdoll::launch from
// startRagdoll): a chaingun's last bullet hops it, a shotgun throws it, a super shotgun harder, never
// past punchLift up (a blast's own); at one cap for all, the shotgun flew as high as a rocket.
constexpr float hitPunch=2,punchCarry=6.5f,punchLift=3.5f;
constexpr float slideTurnLeast=2,slideTurnMost=7;   // degrees the corpse's slide is turned off the shot's line, either way (startRagdoll)
constexpr float slamSpeed=1.5f;     // units a tic into a wall a torso point must hit at for a slam (Ragdoll::pin)
constexpr float slamBuckle=1.2f;    // units a tic a slam kicks the knees out from the wall
constexpr float sweepFrom=2.0f;     // units a tic a leg kill must push for the legs to be swept (a single bullet, 1.25, buckles them)
constexpr float sweepTurnTics=10;   // tics a sweep's turn about the vertical (its slant off the shot) is spread over
constexpr float sweepSpin=0.2f,sweepLift=1.0f;    // a sweep's turn over (radians a tic) and lift (units a tic) at a shotgun's kill; then only its weight
constexpr int slamTics=40,slamStick=3;   // tics a slammed body is held to the wall at most while it sinks to sitting; of them, hanging     // units per tic: the most of the corpse's own slide the doll flies with
static const float radius[DOLL_POINTS]={4,2.5f,2.5f,2.5f,4,3,3,2,2,1.5f,1.5f};
struct Link { int a,b; };
static const Link bones[]={{P_HEAD,P_NECK},{P_NECK,P_SH_L},{P_NECK,P_SH_R},{P_SH_L,P_SH_R},{P_SH_L,P_SPINE},{P_SH_R,P_SPINE},
    {P_NECK,P_SPINE},{P_SPINE,P_HIP_L},{P_SPINE,P_HIP_R},{P_HIP_L,P_HIP_R},
    {P_HIP_L,P_KNEE_L},{P_KNEE_L,P_ANK_L},{P_HIP_R,P_KNEE_R},{P_KNEE_R,P_ANK_R}};
static const Link soft[]={{P_HEAD,P_SH_L},{P_HEAD,P_SH_R},{P_SH_L,P_HIP_L},{P_SH_R,P_HIP_R},{P_SH_L,P_HIP_R},{P_SH_R,P_HIP_L}};
enum { M_LEGS,M_TRUNK,M_NECK,MUSCLES };
static const std::vector<Link> muscles[MUSCLES]={
    {{P_HIP_L,P_ANK_L},{P_HIP_R,P_ANK_R},{P_SPINE,P_KNEE_L},{P_SPINE,P_KNEE_R},{P_KNEE_L,P_KNEE_R},{P_ANK_L,P_ANK_R}},
    {{P_NECK,P_HIP_L},{P_NECK,P_HIP_R},{P_SH_L,P_HIP_R},{P_SH_R,P_HIP_L}},
    {{P_HEAD,P_SPINE}}};

// A wall or step near the body, in the world: a point below top (units above the body's floor) can't pass it.
struct Wall { float ax,ay,bx,by,top; };

// The floors round the kill (tests/death_floors.py): a grid of floorCell-wide cells floorReach each
// way, each holding its sector, whose floor is read live (a lift under a body carries it). Doom holds
// a thing up at the highest floor anywhere under its box, so a corpse killed at a stair edge floated
// over the lower step its body lay on, sank into a step behind it and lay in the air over a drop.
constexpr float floorCell=4,floorReach=160;   // the doll flies in the world: room for a throw
constexpr float blastReach=224;                // and for a blast's (twice the cells to sample: why it's a switch)
constexpr float dropEdge=2;                    // how round a step's edge over a drop is (Ragdoll::ground)
constexpr float wallSlide=0.5f;                // of a point's skid along a wall it touches, the share lost a substep (Ragdoll::rub)
constexpr float faceDepth=6;                   // units under a floor cell's top past which a point is against its face (Ragdoll::ground)
constexpr float floorStick=0.6f,floorSlide=0.5f;   // contact friction, static and sliding (Ragdoll::rub)
struct FloorGrid { float x0=0,y0=0;int size=0;std::vector<const sector_t*> cells; };
// A body's weight per point once it has touched a wall (Ragdoll::shareOf): head, neck, shoulders with
// the arms, spine, hips, knees with the thighs, ankles with the shins and feet, shares of the whole.
static const float weight[DOLL_POINTS]={0.07f,0.08f,0.10f,0.10f,0.15f,0.10f,0.10f,0.10f,0.10f,0.05f,0.05f};

static V3 turn(V3 v,V3 axis,float angle) {
    float c=std::cos(angle),s=std::sin(angle);
    return v*c+cross(axis,v)*s+axis*(dot(axis,v)*(1-c));
}

// Guided falls: the shot death on strings, aimed at the zombieman's own death drawings (tests/
// death_ragdoll.py --guided, beside POSSH-L). Film's recipe: a real body, a fake force. A ratchet
// (the stunt wire on a jerk vest) yanks the chest along the shot for a few tics, the one force
// from outside, its drift taken out like the hit's. All else acts inside the body: the legs give
// way, the knees kicked forward, and key strings pull the body's shape (never its place or
// heading) toward the drawings' poses by shape matching (Mueller et al. 2005): each substep the
// key is fitted rigidly onto the body and every point pulled part of the way to its fitted spot.
// The strings are slack while the ratchet pulls, take hold after it (Zordan and Hodgins 2002: go
// loose at the impact, then track again) and let go as the chest nears the floor, so the landing
// is the doll's own; the muscles let go in the order the landing asks.
// Key angles (degrees) at tics after the hit: the spine arched back at the chest joint, the head
// thrown back on the neck, the thighs swung forward of the hips, the knees bent.
struct GuideKey { float tic,arch,head,hip,knee; };
static const GuideKey guideKeys[]={{0,0,0,0,0},
    {2,15,30,0,5},          // POSSH: the hit, head snapped back
    {7,35,55,15,25},        // POSSI: arched, the head behind the shoulders
    {12,25,50,60,80},       // POSSJ: the knees buckle, it sits back
    {17,10,40,75,95}};      // POSSK: on its back, knees up
static const GuideKey guideFlinch={0,-12,-25,10,20};   // the startle: trunk and head forward, knees soft
// The landing order. Butt: the hips land, then the back, the head last (the trunk held until the
// hips are down, the neck until the back is). Flat: all lets go when the legs give. Topple: the
// legs stay locked until the chest is low (a plank falling).
enum { LAND_BUTT,LAND_FLAT,LAND_TOPPLE };
// The knobs, as the lab names them: ratchet (units per tic added to the chest each ratchet tic, the
// head 60% of it; scaled by the blast), its tics, the tic the legs give, how far a tic the key
// strings pull, a startle before the hit (it held the body up 4 tics longer and barely showed at
// the death frames' pace: off), and the landing order.
struct Guide { float ratchet=0.9f;int ratchetTics=3,give=4;float key=0.35f;bool flinch=false;int landing=LAND_BUTT; };
constexpr int flinchTics=2;
constexpr float keySpring=0.05f;   // of each key pull, the share kept as speed (0.3 still rocked the chest 10 degrees)
constexpr float keyFade=6;         // tics the strings take to let go after the last key (hung over a ledge they held its jackknife)

static V3 rotX(V3 v,V3 pivot,float a) {   // about the side axis through pivot; positive turns up toward the front (-y)
    float y=v.y-pivot.y,z=v.z-pivot.z,c=std::cos(a),s=std::sin(a);
    return {v.x,pivot.y+y*c-z*s,pivot.z+y*s+z*c};
}
// The rest skeleton bent by a key: arch and head back, the thighs forward, the shins back (the hinge's way).
static DollPose keyShape(const DollPose &rest,const GuideKey &k) {
    const float rad=doomPi/180;
    DollPose p=rest;
    for(int n:{P_NECK,P_SH_L,P_SH_R,P_HEAD})p[n]=rotX(p[n],rest[P_SPINE],-k.arch*rad);
    p[P_HEAD]=rotX(p[P_HEAD],p[P_NECK],-k.head*rad);
    for(int s=0;s<2;++s) {
        int hip=s?P_HIP_R:P_HIP_L,knee=s?P_KNEE_R:P_KNEE_L,ankle=s?P_ANK_R:P_ANK_L;
        p[knee]=rotX(p[knee],rest[hip],-k.hip*rad);p[ankle]=rotX(p[ankle],rest[hip],-k.hip*rad);
        p[ankle]=rotX(p[ankle],p[knee],k.knee*rad);
    }
    return p;
}
// The key u tics after the hit: eased from rest through the keys, held at the last.
static GuideKey keyAt(float u) {
    const int count=(int)(sizeof guideKeys/sizeof guideKeys[0]);
    for(int i=1;i<count;++i) {
        const GuideKey &a=guideKeys[i-1],&b=guideKeys[i];
        if(u>=b.tic)continue;
        float f=std::clamp((u-a.tic)/(b.tic-a.tic),0.0f,1.0f);f=f*f*(3-2*f);
        auto mix=[&](float x,float y){return x+(y-x)*f;};
        return {u,mix(a.arch,b.arch),mix(a.head,b.head),mix(a.hip,b.hip),mix(a.knee,b.knee)};
    }
    return guideKeys[count-1];
}
// The rotation nearest m (its polar part: averaged with its inverse transpose until it settles);
// false when m is degenerate or a reflection.
static bool rotationOf(const float m[3][3],float r[3][3]) {
    float a[3][3];memcpy(a,m,sizeof a);
    for(int pass=0;pass<40;++pass) {
        V3 r0{a[0][0],a[0][1],a[0][2]},r1{a[1][0],a[1][1],a[1][2]},r2{a[2][0],a[2][1],a[2][2]};
        V3 cof[3]={cross(r1,r2),cross(r2,r0),cross(r0,r1)};
        float det=dot(r0,cof[0]);
        if(det<=1e-9f)return false;
        float change=0;
        for(int i=0;i<3;++i) {
            float c[3]={cof[i].x,cof[i].y,cof[i].z};
            for(int j=0;j<3;++j) {float v=0.5f*(a[i][j]+c[j]/det);change=std::max(change,std::abs(v-a[i][j]));a[i][j]=v;}
        }
        if(change<1e-6f)break;
    }
    memcpy(r,a,sizeof a);
    return true;
}

struct Ragdoll {
    const Relief *body=nullptr;
    int state=0,start=0,tics=0,still=0;
    std::vector<int> steps;
    float ax[2]={1,0},ay[2]={0,1};     // the body's x and y in the world, fixed at the kill
    float ox=0,oy=0;                   // where it died: the doll falls in the world from there
    bool mirror=false,landed=false,resting=false,heavy=false;
    DollPose p{},prev{},shown{};
    int shownAt=0,lastPose=0,poseTics=5;   // a pose drawn at tic lastPose, held poseTics
    int lyingAt=-1;                        // the tic the torso came down
    V3 tipWay{0,1,0},toEye{0,-1,0};        // the hit's way over the floor and the eye's, in body space
    float centre[2]={},centreWas[2]={},centreShown[2]={};   // the body's middle over the floor: this tic, the last, the pose shown
    float rise=0;                          // its middle's climb the last tic (the top of a throw is a pose)
    const sector_t *restSector=nullptr;float restFloor=0;   // the floor it came to rest on (a lift carries the corpse)
    std::vector<Wall> nearWalls;           // the walls round it, in body space
    int slamAt=-1;float slamAge=-1;V3 slamWay{};   // a wall slam (Ragdoll::pin): its tic, age, the wall's way out
    float hitWall=0;V3 hitWay{};int hitPoint=-1;    // the hardest a torso point went into a wall this tic, and which
    bool splashed=false;float drop=0,smearing=0;    // the wall splash made; the spine's fall last tic; slid since a smear
    float hold[MUSCLES]={},over[MUSCLES]={},late=0;
    float spin=0;int way=0;
    V3 launchV{};float launchSpin=0;int launchAt=-1;   // a chest blast's launch (launch), at tic launchAt
    float launchTwist=0;               // and its turn about the vertical (a pirouette), radians a tic
    bool swept=false;                  // legs swept by a heavy blast: the weight brings it down, no steering hold
    bool blasted=false;                // thrown by an explosion: flies, comes back off walls in the air, no slam
    float reach=floorReach;            // how far round the kill its walls and floors are known
    float hipHold=0;                   // a swept body's hips held (spine to knee) while its knees are gone (tic)
    bool guided=false;Guide guide;     // a guided fall (above): its knobs, the ratchet's line,
    V3 yank{};float yaw=0,kicks[2]={},legLate=0;   // its turn, the knees' kicks and the late leg
    int hipsDownAt=-1,chestDownAt=-1,lowAt=-1;     // tics the hips, then the chest touched down, and the chest got low
    std::vector<Wall> walls;
    FloorGrid floors;float gx=0,gy=0,gz=0;          // the floors, and where the thing is over them now (its feet gz)
    float pressed[DOLL_POINTS]={};                  // how deep the floor pushed each point back up this substep
    int wallTouches=0;                              // point-wall contacts this tic (the test log)
    V3 walled[DOLL_POINTS]={};                      // the way out of the wall each point touched this substep (rub)
    int wallAt=-1;                                  // the tic it first touched a wall (shareOf)
    V3 shouldersWas{};                              // the shoulder line last tic (the test log's twist)
    mutable std::vector<V3> posed;mutable int posedAt=-1;
    mutable int cardKey=0,cardBottom=0,cardLeft=0,cardRight=0,cardFirst=0;mutable uint64_t cardSignature=~0ull;   // the card's picture (ragdollCard)
    mutable float cardMiddle[2]={};                     // where the card stands, in the body's drawn space
    mutable int lastStep=-1;                            // the camera's 45-degree step last drawn
    // Once it lies still the body is baked into a plain corpse sprite: the last picture, the camera
    // angle and tilt it was seen from, shown the same from every angle (rotation 0), its 3D data freed.
    mutable bool baked=false;mutable int bakedAngle=0,bakedTilt=0;

    float rest(int a,int b) const {return length(body->rest[a]-body->rest[b]);}
    void push(int n,V3 v) {prev[n]=prev[n]-v*(1.0f/dollSubsteps);}   // add a velocity (units per tic)
    void noDrift() {          // the points' mean sideways speed taken out: the game's own momentum carries the thing
        V3 mean{};
        for(int n=0;n<DOLL_POINTS;++n)mean=mean+(p[n]-prev[n]);
        mean=mean*(1.0f/DOLL_POINTS);mean.z=0;
        for(int n=0;n<DOLL_POINTS;++n)prev[n]=prev[n]+mean;
    }
    // A cell's floor in the body's heights (the thing's feet 0); off the grid, the nearest edge cell's.
    float cellFloor(int i,int j) const {
        if(floors.cells.empty())return 0;
        i=std::clamp(i,0,floors.size-1);j=std::clamp(j,0,floors.size-1);
        return units(floors.cells[(size_t)j*floors.size+i]->floorheight)-gz;
    }
    // A body point over the grid (its world-aligned units); the doll falls unmirrored, like the walls.
    void overGrid(V3 q,float &wx,float &wy) const {
        float side=mirror?-1.0f:1.0f;
        wx=gx+ax[0]*side*q.x+ay[0]*q.y;wy=gy+ax[1]*side*q.x+ay[1]*q.y;
    }
    float floorAt(V3 q) const {
        float wx,wy;overGrid(q,wx,wy);
        return cellFloor((int)std::floor(wx/floorCell),(int)std::floor(wy/floorCell));
    }
    float up(int n) const {return p[n].z-floorAt(p[n]);}   // how high a point stands over the floor under it
    // Its walls and floors are known reach round where it died: near that edge it goes no
    // further out (a throw that far would pass walls it doesn't know).
    void keepNear() {
        V3 c{};
        for(int n=0;n<DOLL_POINTS;++n)c=c+p[n];
        c=c*(1.0f/DOLL_POINTS);
        float out=std::hypot(c.x,c.y);
        if(out<reach-24)return;
        V3 way{c.x/out,c.y/out,0},v{};
        for(int n=0;n<DOLL_POINTS;++n)v=v+(p[n]-prev[n]);
        float going=dot(v,way)/DOLL_POINTS;
        if(going>0)for(int n=0;n<DOLL_POINTS;++n)prev[n]=prev[n]+way*going;
    }
    // The sector of the floor cell under a body point (off the grid, the nearest edge cell's).
    const sector_t *sectorUnder(V3 q) const {
        if(floors.cells.empty())return nullptr;
        float wx,wy;overGrid(q,wx,wy);
        int i=std::clamp((int)std::floor(wx/floorCell),0,floors.size-1),j=std::clamp((int)std::floor(wy/floorCell),0,floors.size-1);
        return floors.cells[(size_t)j*floors.size+i];
    }
    // Down with its torso, not just touched down: the damping that stops a body soon and the legs
    // slackening out wait for this (keyed to touching down, the head too, an upside-down body froze
    // standing on its head against a wall).
    bool lying() const {return landed&&(up(P_SPINE)<8||up(P_NECK)<7);}
    // Sitting: both hips on the floor. Slumped against a wall after a slam the spine stays up, and a
    // body counted as down only by its spine was never landed, never damped down, and steered on.
    // (Sitting in the open is only on the way over: a legs-swept fall sits, then goes onto its back.)
    bool seated() const {return std::max(up(P_HIP_L)-radius[P_HIP_L],up(P_HIP_R)-radius[P_HIP_R])<1.5f;}
    bool down() const {return lying()||(slamAt>=0&&landed&&seated());}
    // Pushes that turn and fold the body without carrying it: their net sideways drift taken out of
    // every point (taking out all of it, as noDrift does, cancelled the launch).
    void pushInside(std::initializer_list<std::pair<int,V3>> pushes) {
        V3 mean{};
        for(const auto &q:pushes) {push(q.first,q.second);mean=mean+q.second;}
        mean=mean*(1.0f/DOLL_POINTS);mean.z=0;
        for(int n=0;n<DOLL_POINTS;++n)push(n,mean*-1);
    }
    // The launch (tests/death_ragdoll.py GUIDED launch, lift, spin): the whole body thrown back and up,
    // kept (the game's slide comes on top), and a back somersault about its middle, so the chest leads
    // and the feet sweep up and it flies laid back. Without it a chest blast only turned the body about
    // its hips and it sat down: lights out, never sent flying.
    void launch() {
        V3 c{};
        for(int n=0;n<DOLL_POINTS;++n)c=c+p[n];
        c=c*(1.0f/DOLL_POINTS);
        V3 d=unit(V3{launchV.x,launchV.y,0});
        for(int n=0;n<DOLL_POINTS;++n) {V3 r=p[n]-c;push(n,launchV+V3{d.x*r.z,d.y*r.z,-dot(r,d)}*launchSpin+V3{-r.y,r.x,0}*launchTwist);}
    }
    V3 hips() const {return middle(p[P_HIP_L],p[P_HIP_R]);}
    V3 front() const {return unit(cross(p[P_SH_R]-p[P_SH_L],p[P_NECK]-hips()));}
    V3 pelvisFront() const {return unit(cross(p[P_HIP_R]-p[P_HIP_L],p[P_SPINE]-hips()));}
    // Point a's share of a link's correction. Even until the body touches a wall, then by weight: with
    // every point as heavy as the spine, a foot stopped against a wall dragged the trunk round it as hard
    // as the trunk dragged the foot, and bodies twisted and rolled there (arena: shotgunner leg sweeps
    // into a wall 22 behind, shoulders turned 279 degrees in all, 91 weighted; open floor 164). Weighted
    // from the start the open-floor falls would change; after the touch the play test read more real.
    float shareOf(int a,int b) const {
        if(wallAt<0)return 0.5f;
        float wa=1/weight[a],wb=1/weight[b];return wa/(wa+wb);
    }
    void link(int a,int b,float length_,float k) {
        V3 d=p[b]-p[a];float dist=std::max(1e-6f,length(d));
        V3 corr=d*(k*(dist-length_)/dist);float sa=shareOf(a,b);
        p[a]=p[a]+corr*sa;p[b]=p[b]-corr*(1-sa);
    }
    // As link, but never lifting either point: the lying body's own easing (legs slackening out,
    // hips opening) assumes a body flat on the floor; slumped against a wall, the only way left for
    // them was up it, and bodies climbed walls (up to 27 units in the arena) and pillars (48).
    void linkDown(int a,int b,float length_,float k) {
        V3 d=p[b]-p[a];float dist=std::max(1e-6f,length(d));
        float sa=shareOf(a,b);V3 corr=d*(k*(dist-length_)/dist),ca=corr*sa,cb=corr*-(1-sa);
        ca.z=std::min(ca.z,0.0f);cb.z=std::min(cb.z,0.0f);
        p[a]=p[a]+ca;p[b]=p[b]+cb;
    }
    // A whole leg turned about its hip part of the way from d to want, where it was too: a limit
    // that keeps the bones' lengths and adds no speed.
    void swing(int s,V3 d,V3 want) {
        V3 axis=cross(d,want);float size=length(axis);
        if(size<1e-6f)return;
        float angle=0.5f*std::atan2(size,dot(d,want));axis=axis*(1/size);
        V3 hip=p[s?P_HIP_R:P_HIP_L];
        for(int n:{s?P_KNEE_R:P_KNEE_L,s?P_ANK_R:P_ANK_L}) {p[n]=hip+turn(p[n]-hip,axis,angle);prev[n]=hip+turn(prev[n]-hip,axis,angle);}
    }
    // The spine bends at most about 45 degrees at the chest joint, a thigh swings no further back
    // than 30 degrees behind the hips nor flexes past 110 toward the chest, and a knee is a hinge:
    // the shin only swings back from the thigh's line (a knee bent the wrong way by over ~9 degrees
    // is mirrored across the hip-ankle line, which keeps both lengths, where it was too; less
    // doesn't show, and a straight leg on the floor flipped every substep and twitched), never
    // folding the leg tighter than 55% of its length (tighter curled bodies into balls). Lying,
    // a leg slackens out toward 85% of its length, and the hips ease open, the thigh out toward the
    // trunk's line (neck to knee 95% of standing): sat down by legs swept or a side hit and rolled
    // drawn side up, a body lay curled with its knees to its chest (7 and 6 in 12 of those falls).
    void limits() {
        for(int s=0;s<2;++s) {
            int hip=s?P_HIP_R:P_HIP_L;float least=0.9f*rest(P_NECK,hip);
            if(length(p[P_NECK]-p[hip])<least)link(P_NECK,hip,least,0.5f);
        }
        V3 n=pelvisFront(),up=unit(p[P_SPINE]-hips());
        for(int s=0;s<2;++s) {
            V3 hip=p[s?P_HIP_R:P_HIP_L],d=unit(p[s?P_KNEE_R:P_KNEE_L]-hip),want=d;
            if(dot(want,n)<-0.5f)want=unit(want+n*(-0.5f-dot(want,n)));
            if(dot(want,up)>0.34f)want=unit(want-up*(dot(want,up)-0.34f));
            if(length(want-d)>1e-6f)swing(s,d,want);
        }
        V3 side=unit(p[P_HIP_R]-p[P_HIP_L]);
        for(int s=0;s<2;++s) {
            int h=s?P_HIP_R:P_HIP_L,k=s?P_KNEE_R:P_KNEE_L,a=s?P_ANK_R:P_ANK_L;
            V3 hip=p[h];
            // The hinge's plane: the knee turns about the hips' side axis (square to the thigh), give
            // or take kneeTwist of the shin sideways for the thigh's own turn. The sign test alone let
            // a knee fold sideways (101 of 104 bends past 45 degrees in the settling lab, mostly knee
            // on the floor); the ankle is turned back toward the plane, its speed kept.
            {
                V3 thigh=unit(p[k]-hip),hinge=unit(side-thigh*dot(side,thigh)),shin=p[a]-p[k];
                float len=length(shin),off=dot(shin,hinge);
                if(len>1e-3f&&std::abs(off)>kneeTwist*len) {
                    V3 want=unit(shin-hinge*(off-std::copysign(kneeTwist*len,off)))*len;
                    V3 move=(p[k]+want-p[a])*0.5f;
                    p[a]=p[a]+move;prev[a]=prev[a]+move;
                }
            }
            if(dot(cross(unit(p[k]-hip),unit(p[a]-p[k])),side)<-0.15f) {
                V3 axis=unit(p[a]-hip);
                auto mirrored=[&](V3 q){V3 r=q-hip;return hip+axis*(2*dot(r,axis))-r;};
                p[k]=mirrored(p[k]);prev[k]=mirrored(prev[k]);
            }
            float full=rest(h,k)+rest(k,a),reach=length(p[a]-p[h]);
            if(reach<0.55f*full)link(h,a,0.55f*full,0.5f);
            else if(lying()&&reach<0.85f*full)linkDown(h,a,0.85f*full,0.03f);
            // Only a knee down on the floor: one in the air pushed off the neck held the legs
            // straight up off a body on its back (ragdoll_arena.py, chest blasts); gravity takes those.
            float open=0.95f*rest(P_NECK,k);
            if(lying()&&p[k].z-floorAt(p[k])<radius[k]+4&&length(p[k]-p[P_NECK])<open)linkDown(P_NECK,k,open,0.03f);
        }
    }
    // Pull the body k of the way toward a key shape fitted rigidly onto it: no net push, no net
    // turn. Only keySpring of each pull is kept as speed (kept whole, the strings rang: the chest
    // thrown back, sprung forward, the gun rocking); a point on the floor takes only the pull's
    // height (moved outright, a planted foot skated and the body inched along).
    void shapeMatch(const DollPose &shape,float k) {
        V3 c{},q0{};
        for(int n=0;n<DOLL_POINTS;++n) {c=c+p[n];q0=q0+shape[n];}
        c=c*(1.0f/DOLL_POINTS);q0=q0*(1.0f/DOLL_POINTS);
        float a[3][3]={},r[3][3];
        for(int n=0;n<DOLL_POINTS;++n) {
            V3 d=p[n]-c,q=shape[n]-q0;
            float dv[3]={d.x,d.y,d.z},qv[3]={q.x,q.y,q.z};
            for(int i=0;i<3;++i)for(int j=0;j<3;++j)a[i][j]+=dv[i]*qv[j];
        }
        if(!rotationOf(a,r))return;
        for(int n=0;n<DOLL_POINTS;++n) {
            V3 q=shape[n]-q0;
            V3 goal=c+V3{r[0][0]*q.x+r[0][1]*q.y+r[0][2]*q.z,r[1][0]*q.x+r[1][1]*q.y+r[1][2]*q.z,r[2][0]*q.x+r[2][1]*q.y+r[2][2]*q.z};
            V3 pull=(goal-p[n])*k;
            if(up(n)<=radius[n]+0.05f)pull={0,0,pull.z};
            p[n]=p[n]+pull;prev[n]=prev[n]+pull*(1-keySpring);
        }
    }
    // The floor: bodies land dead, no bounce. Each point is a ball against the grid's cells (solid
    // below their floor): a top pushes it up, a step's face sideways, an edge out from the edge, and
    // edges over a drop are rounded (dropEdge) as a thigh resting on the corner would be (only points
    // collide), so a body hanging over a drop hangs clear of the face and its weight drags the rest
    // over (by centres alone it lay in the face and hung by its knees). A point that came at a step's
    // face from the side, below the top, stops there first: a ball's push would lift it onto the step.
    // How deep a floor's top pushed back is kept for the friction (rub); an edge is slick.
    void ground(int n) {
        float r=radius[n],side=mirror?-1.0f:1.0f,depth=0;V3 sideways{};
        V3 q=p[n];const V3 was=prev[n];
        float g=floorAt(q);
        if(q.z<g&&g-floorAt(was)>r&&was.z<g+r) {q.x=was.x;q.y=was.y;}
        for(int round=0;round<3;++round) {       // the deepest cell each round
            float wx,wy;overGrid(q,wx,wy);
            float best=0;V3 way{};
            int i0=(int)std::floor((wx-r)/floorCell),i1=(int)std::floor((wx+r)/floorCell);
            int j0=(int)std::floor((wy-r)/floorCell),j1=(int)std::floor((wy+r)/floorCell);
            for(int j=j0;j<=j1;++j)for(int i=i0;i<=i1;++i) {
                float top=cellFloor(i,j);
                if(q.z-r>=top)continue;
                auto lower=[&](int di,int dj){return cellFloor(i+di,j+dj)<top-0.5f?dropEdge:0.0f;};
                float cut[4]={lower(-1,0),lower(1,0),lower(0,-1),lower(0,1)};
                float e=cut[0]+cut[1]+cut[2]+cut[3]>0?dropEdge:0;
                float cx=std::clamp(wx,i*floorCell+cut[0],(i+1)*floorCell-cut[1]),cy=std::clamp(wy,j*floorCell+cut[2],(j+1)*floorCell-cut[3]);
                V3 d{wx-cx,wy-cy,q.z-std::min(q.z,top-e)};
                float dist=length(d),amount;V3 dir;
                if(dist<1e-6f) {                                       // inside the cell: up onto it,
                    dir={0,0,1};amount=top+r-q.z;
                    // or, deep under its top, out through its nearest edge over lower floor: it is
                    // against a step's face (lifted onto the step, a body pushed into one climbed it,
                    // 25 units up a pillar's side in the arena, and flew)
                    if(amount>r+faceDepth) {
                        float exits[4]={wx-i*floorCell,(i+1)*floorCell-wx,wy-j*floorCell,(j+1)*floorCell-wy};
                        static const float ways[4][2]={{-1,0},{1,0},{0,-1},{0,1}};
                        int pick=-1;
                        for(int k=0;k<4;++k)if(cut[k]>0&&(pick<0||exits[k]<exits[pick]))pick=k;
                        if(pick>=0) {dir={ways[pick][0],ways[pick][1],0};amount=exits[pick]+r;}
                    }
                }
                else if(dist<r+e) {dir=d*(1/dist);amount=r+e-dist;}
                else continue;
                if(amount>best) {best=amount;way=dir;}
            }
            if(best<=0)break;
            V3 push=V3{side*(way.x*ax[0]+way.y*ax[1]),way.x*ay[0]+way.y*ay[1],way.z}*best;   // back into the body's frame
            q=q+push;sideways=sideways+V3{push.x,push.y,0};
            if(way.z>0.999f)depth+=way.z*best;
        }
        p[n]=q;
        if(depth>0) {pressed[n]+=depth;prev[n].z=std::max(prev[n].z,q.z);}
        // Out of a step's face or off its edge: no speed from the push (moved alone, a point pushed a
        // sphere's width out of a face in a substep left at up to 16 units a tic: bodies flung off
        // steps, 1.8 a tic gained in a tic against the arena's), and none left going into it.
        if(length(sideways)>1e-6f) {
            V3 out=unit(sideways);
            prev[n]=prev[n]+sideways;
            float into=dot(p[n]-prev[n],out);
            if(into<0)prev[n]=prev[n]+out*into;
        }
    }
    // Contact friction, once a substep (Mueller et al. 2007): a point the floor pushed back up by
    // depth in all keeps none of its sideways motion this substep if that is under floorStick*depth,
    // else loses floorSlide*depth of it. A hard landing stops it dead; a leg resting under its own
    // weight slides when the body hanging off it pulls (a fixed share of speed lost per substep,
    // however lightly a point rested, held bodies hanging off ledges).
    // Walls rub too: a point against one loses wallSlide of its skid along it a substep (frictionless,
    // a body hitting a wall at a slant skidded along it and barrel-rolled, 91 in 144 over half a turn).
    void rub() {
        for(int n=0;n<DOLL_POINTS;++n) {
            V3 out=walled[n];
            if(out.x!=0||out.y!=0) {
                V3 v=p[n]-prev[n],along=v-out*dot(v,out);along.z=0;
                p[n]=p[n]-along*wallSlide;
            }
        }
        for(int n=0;n<DOLL_POINTS;++n) {
            float depth=pressed[n];
            if(depth<=0)continue;
            float dx=p[n].x-prev[n].x,dy=p[n].y-prev[n].y,slide=std::hypot(dx,dy);
            if(slide<=floorStick*depth) {p[n].x=prev[n].x;p[n].y=prev[n].y;}
            else if(slide>0) {float keep=1-floorSlide*depth/slide;p[n].x=prev[n].x+dx*keep;p[n].y=prev[n].y+dy*keep;}
        }
    }
    // Damping (Mueller et al. 2006, 3.5): until the torso has been down lyingGrace tics only the
    // body's motion inside itself, its flail, is damped; its flight and its turn as one thing are
    // kept, so a throw carries until something stops it (damped whole it lost two thirds of its speed
    // in half a second of flight) and the slap of a landing reaches the head. Down a while: every
    // point on the floor damped hard, so it stops soon; a limb still in the air falls (damped too,
    // legs left up sank for most of a second).
    // The body moving as one rigid thing: its middle c, the middle's speed vc and its spin (radians
    // a substep about c), from the points' speeds v.
    void rigid(const V3 v[DOLL_POINTS],V3 &c,V3 &vc,V3 &spin) const {
        c=vc=spin=V3{};
        for(int n=0;n<DOLL_POINTS;++n) {c=c+p[n];vc=vc+v[n];}
        c=c*(1.0f/DOLL_POINTS);vc=vc*(1.0f/DOLL_POINTS);
        V3 turning{},inertia[3]={};             // its angular momentum, and its inertia's columns
        for(int n=0;n<DOLL_POINTS;++n) {
            V3 r=p[n]-c;float rr=dot(r,r);
            turning=turning+cross(r,v[n]);
            inertia[0]=inertia[0]+V3{rr-r.x*r.x,-r.y*r.x,-r.z*r.x};
            inertia[1]=inertia[1]+V3{-r.x*r.y,rr-r.y*r.y,-r.z*r.y};
            inertia[2]=inertia[2]+V3{-r.x*r.z,-r.y*r.z,rr-r.z*r.z};
        }
        auto det=[](V3 a,V3 b,V3 c){return dot(a,cross(b,c));};
        float d=det(inertia[0],inertia[1],inertia[2]);
        if(d>1e-3f)spin=V3{det(turning,inertia[1],inertia[2]),det(inertia[0],turning,inertia[2]),det(inertia[0],inertia[1],turning)}*(1/d);
    }
    void damp(V3 v[DOLL_POINTS]) const {
        V3 c,vc,spin;rigid(v,c,vc,spin);
        // Past flat and still going over: that tumble eased out fast. Its spin kept whole, a body
        // tipped forward (shot from behind) dived on past flat and came down head first, upside down
        // (an imp: tipped -0.86 at the landing); and sliding in head first, its shoulders caught on
        // the floor and its legs flipped up over its head (a barrel roll, hips 29 up). Turning toward
        // flat is left alone.
        V3 axis=unit(p[P_NECK]-hips());
        if(axis.z<0.1f&&cross(spin,axis).z<0) {
            V3 over=V3{spin.x,spin.y,0}*(1-overKeep);
            spin=spin-over;
            for(int n=0;n<DOLL_POINTS;++n)v[n]=v[n]-cross(over,p[n]-c);
        }
        if(heavy) {
            for(int n=0;n<DOLL_POINTS;++n)v[n]=v[n]*(p[n].z-floorAt(p[n])<radius[n]+1.5f?0.9f:0.985f);
            return;
        }
        for(int n=0;n<DOLL_POINTS;++n) {V3 whole=vc+cross(spin,p[n]-c);v[n]=whole+(v[n]-whole)*0.985f;}
    }
    void tic(const float muscle[MUSCLES],const std::vector<Wall> &near,const DollPose *shape=nullptr,float k=0) {
        const float dt=1.0f/dollSubsteps;
        float kSub=1-std::pow(1-std::min(k,0.99f),1.0f/dollSubsteps);
        for(int sub=0;sub<dollSubsteps;++sub) {
            V3 v[DOLL_POINTS];
            for(int n=0;n<DOLL_POINTS;++n)v[n]=p[n]-prev[n];
            damp(v);
            if(slamAge>=0)pin(v);
            const DollPose begin=p;
            for(int n=0;n<DOLL_POINTS;++n) {prev[n]=p[n];p[n]=p[n]+v[n]+V3{0,0,-dollGravity*dt*dt};}
            if(shape&&kSub>0)shapeMatch(*shape,kSub);
            std::fill(std::begin(pressed),std::end(pressed),0.0f);
            std::fill(std::begin(walled),std::end(walled),V3{});
            for(int pass=0;pass<dollPasses;++pass) {
                for(const Link &l:bones)link(l.a,l.b,rest(l.a,l.b),1);
                for(const Link &l:soft)link(l.a,l.b,rest(l.a,l.b),0.15f);
                for(int g=0;g<MUSCLES;++g)if(muscle[g]>0)for(const Link &l:muscles[g])link(l.a,l.b,rest(l.a,l.b),muscle[g]);
                if(hipHold>0)for(int knee:{P_KNEE_L,P_KNEE_R})link(P_SPINE,knee,rest(P_SPINE,knee),hipHold);
                limits();
                for(int n=0;n<DOLL_POINTS;++n) {
                    ground(n);
                    for(const Wall &w:near) {
                        if(p[n].z>=w.top)continue;
                        float ex=w.bx-w.ax,ey=w.by-w.ay,len2=std::max(1e-6f,ex*ex+ey*ey);
                        float along=((p[n].x-w.ax)*ex+(p[n].y-w.ay)*ey)/len2,t=std::clamp(along,0.0f,1.0f);
                        // Back to the side it was on when the substep began: pushed out to the nearer
                        // side, a point carried through the line came out beyond it, the body lay
                        // across the wall and its bones flung it. The side from where the point was
                        // (begin), not prev: the limits turn a leg's prev with it, and a knee or ankle
                        // they carried through the line passed for one that had always been beyond,
                        // was held there and tore the body (12% of 189 arena wall falls).
                        float was=ex*(begin[n].y-w.ay)-ey*(begin[n].x-w.ax),now=ex*(p[n].y-w.ay)-ey*(p[n].x-w.ax);
                        if(begin[n].z<w.top&&along>0&&along<1&&std::abs(was)>1e-4f&&(was>0)!=(now>0)) {
                            float l=std::sqrt(len2);
                            V3 normal=V3{-ey/l,ex/l,0}*(was>0?1.0f:-1.0f);
                            V3 push=normal*(radius[n]-((p[n].x-w.ax)*normal.x+(p[n].y-w.ay)*normal.y));
                            p[n]=p[n]+push;prev[n]=prev[n]+push;walled[n]=normal;if(wallAt<0)wallAt=tics;
                            float into=dot(p[n]-prev[n],normal);++wallTouches;
                            if(into<0) {prev[n]=prev[n]+normal*(into*(1+rebound()));struck(n,-into,normal);}
                            prev[n].z=std::max(prev[n].z,p[n].z);   // rough: nothing slides up it (below)
                            continue;
                        }
                        float qx=p[n].x-(w.ax+ex*t),qy=p[n].y-(w.ay+ey*t),dist=std::sqrt(qx*qx+qy*qy);
                        if(dist>=radius[n]||dist<1e-4f)continue;
                        V3 out={qx/dist,qy/dist,0};
                        // Dead, like the floor: no speed from the push (it threw a body back off a wall it
                        // died against as fast as it came, 83 in 144 arena falls gaining over a unit a tic).
                        p[n]=p[n]+out*(radius[n]-dist);prev[n]=prev[n]+out*(radius[n]-dist);
                        walled[n]=out;if(wallAt<0)wallAt=tics;
                        float into=dot(p[n]-prev[n],out);++wallTouches;
                        if(into<0) {prev[n]=prev[n]+out*(into*(1+rebound()));struck(n,-into,out);}   // and stops going in
                        // Walls are rough: a point pressed to one does not slide up it. A body slamming
                        // in upright, still stiff, ran up the wall like a ladder whose foot slides in,
                        // and was thrown 40 units up (the arena's wall and pillar).
                        prev[n].z=std::max(prev[n].z,p[n].z);
                    }
                }
            }
            rub();
        }
    }
    float rebound() const {return blasted&&!landed?blastRebound:0;}
    // A torso point stopped going into a wall at speed (units a substep), along out.
    void struck(int n,float speed,V3 out) {
        if((n==P_HEAD||n==P_NECK||n==P_SH_L||n==P_SH_R||n==P_SPINE)&&speed>hitWall) {hitWall=speed;hitWay=out;hitPoint=n;}
    }
    // The wall slam, film's way: a body thrown into a wall back or chest first, still up, sticks
    // and sinks down it into a slump instead of bouncing off. While pinned (slamAge tics in, up to
    // slamTics) its torso keeps no speed away from the wall, little along it, none up; it hangs
    // slamStick tics, then slides down ever faster (a slow slide held it up most of a second and
    // it read as stuck); its legs go limp the moment it hits (ragdollView).
    void pin(V3 v[DOLL_POINTS]) const {
        float slide=(slamAge<slamStick?0.1f:std::min(3.0f,0.1f+0.4f*(slamAge-slamStick)))/dollSubsteps;
        for(int n:{P_HEAD,P_NECK,P_SH_L,P_SH_R,P_SPINE}) {
            V3 &w=v[n];
            float away=dot(w,slamWay);
            if(away>0)w=w-slamWay*away;
            V3 flat{w.x,w.y,0};flat=flat-slamWay*dot(flat,slamWay);
            w=w-flat*0.5f;
            w.z=std::clamp(w.z,-slide,0.0f);
        }
    }
    // The hit tips the whole body over about its feet the way it was struck (tip, at the hit); while
    // it falls a light hold keeps it going that way (harder the more it leans back against it), so
    // a body shot from behind goes down on its face. Once it leans past 25 degrees it is twisted
    // about its length toward a drawn side (front or back) up or toward the player, whichever is
    // the shorter turn, spread over the time left before the chest lands. Down: nothing more. A swept
    // body gets neither: twisted while it turned over, it landed on a shoulder and rolled up to sitting.
    void tip(float amount) {
        float feet=std::min(p[P_ANK_L].z,p[P_ANK_R].z);
        for(int n=0;n<DOLL_POINTS;++n)push(n,tipWay*(amount*std::max(0.0f,p[n].z-feet)/40));
    }
    void steer() {
        if(up(P_SPINE)<6||up(P_NECK)<5||up(P_HEAD)<radius[P_HEAD]+0.5f||(slamAt>=0&&seated()))landed=true;
        // Slammed into a wall: no more steering, ever (once the pin let go, the hold leaned the slumped
        // body back into the wall every tic until it sprang off it rolling, up to 4 units a tic).
        if(landed||slamAt>=0)return;
        V3 mid=hips(),axis=unit(p[P_NECK]-mid);
        if(up(P_SPINE)>=7&&axis.z>0.5f&&!swept) {
            V3 lean=p[P_NECK]-mid,flat=unit(V3{lean.x,lean.y,0});
            float against=std::abs(lean.x)+std::abs(lean.y)>1e-3f?std::max(0.0f,-dot(flat,tipWay)):0.0f;
            tip(0.03f+0.2f*against);
        }
        if(up(P_SPINE)<14||axis.z>0.906f||swept||launchTwist!=0)return;   // a pirouette is left to spin
        auto flat=[&](V3 v){return unit(v-axis*dot(v,axis));};
        V3 n=flat(front());
        float best=0;bool any=false;
        for(V3 want:{V3{0,0,1},V3{0,0,-1},toEye,toEye*-1}) {
            if(length(want-axis*dot(want,axis))<0.3f)continue;   // along the body: no side to turn toward
            V3 w=flat(want);
            float angle=std::atan2(dot(axis,cross(n,w)),dot(n,w));
            if(!any||std::abs(angle)<std::abs(best)) {best=angle;any=true;}
        }
        if(std::abs(best)>2.6f&&way)best=std::abs(best)*way;     // nearly opposite: keep the way chosen
        way=best>=0?1:-1;
        float left=std::sqrt(2*std::max(1.0f,up(P_SPINE)-4)/dollGravity);
        float want=std::clamp(best/std::max(3.0f,left),-0.15f,0.15f),delta=(want-spin)*0.5f;
        spin+=delta;
        for(int k=0;k<DOLL_POINTS;++k)push(k,cross(axis,p[k]-mid)*delta);
    }
    // A guided fall's tic t, before the doll moves: the ratchet and the knees' kick, the muscles
    // in the landing's order, and the key the strings pull toward and how hard.
    void guideBefore(int t,float muscle[MUSCLES],DollPose &shape,float &k) {
        const Guide &g=guide;
        int r0=g.flinch?flinchTics:0,give=r0+g.give;
        if(t>=r0&&t<r0+g.ratchetTics) {
            pushInside({{P_SPINE,yank},{P_NECK,yank},{P_SH_L,yank},{P_SH_R,yank},{P_HEAD,yank*0.6f}});
            if(t==r0)for(int n=0;n<DOLL_POINTS;++n) {V3 r=p[n]-p[P_SPINE];push(n,V3{-r.y,r.x,0}*yaw);}
        }
        if(t==give&&g.landing!=LAND_TOPPLE) {
            V3 f=pelvisFront();
            pushInside({{P_KNEE_L,V3{f.x,f.y,0}*kicks[0]},{P_KNEE_R,V3{f.x,f.y,0}*kicks[1]}});
        }
        auto after=[&](int at,float over){return at<0?1.0f:std::clamp(1-(t-at)/over,0.0f,1.0f);};
        auto fadeAt=[&](float from,float over){return t>from?std::clamp(1-(t-from)/over,0.0f,1.0f):1.0f;};
        if(g.landing==LAND_TOPPLE) {muscle[M_LEGS]=muscle[M_TRUNK]=after(lowAt,3);muscle[M_NECK]=0.5f*after(chestDownAt,1);}
        else if(g.landing==LAND_FLAT) {muscle[M_LEGS]=muscle[M_TRUNK]=fadeAt((float)give,2);muscle[M_NECK]=0.5f*muscle[M_LEGS];}
        else {muscle[M_LEGS]=fadeAt(give+legLate,2);muscle[M_TRUNK]=0.8f*after(hipsDownAt,3);muscle[M_NECK]=0.5f*after(chestDownAt,1);}
        if(t<r0) {shape=keyShape(body->rest,guideFlinch);k=0.5f;return;}
        int u=t-r0;
        float ramp=u>=g.ratchetTics+2?1.0f:u<g.ratchetTics?0.0f:(u-g.ratchetTics+1)/3.0f;
        const float lastKey=guideKeys[sizeof guideKeys/sizeof guideKeys[0]-1].tic;
        float height=std::clamp((up(P_SPINE)-10)/8,0.0f,1.0f)*std::clamp(1-(u-lastKey)/keyFade,0.0f,1.0f);   // and after the last key, wherever it is
        shape=keyShape(body->rest,keyAt((float)u));k=landed?0.0f:g.key*ramp*height;
    }
    void guideAfter(int t) {
        if(hipsDownAt<0&&std::min(up(P_HIP_L),up(P_HIP_R))<radius[P_HIP_L]+1)hipsDownAt=t;
        if(chestDownAt<0&&up(P_SPINE)<radius[P_SPINE]+1.5f)chestDownAt=t;
        if(lowAt<0&&up(P_SPINE)<20)lowAt=t;
    }
};

static std::unordered_map<const mobj_t*,Ragdoll> dolls;
static void dropCard(const Ragdoll &doll);
bool hasRagdoll(const mobj_t *thing) {return thing->health<=0&&dolls.count(thing);}
bool ragdollSettled(const mobj_t *thing) {auto found=dolls.find(thing);return found==dolls.end()||found->second.resting;}
void forgetRagdoll(const mobj_t *thing) {
    auto found=dolls.find(thing);
    if(found==dolls.end())return;
    dropCard(found->second);dolls.erase(found);
}
void ragdollsShutdown() {
    for(auto &entry:dolls)dropCard(entry.second);
    dolls.clear();reliefsShutdown();
}

static int frameLump(int state,int rotation,bool *flip=nullptr) {
    if(state<=0)return -1;
    const state_t &s=states[state];
    if(s.sprite<0||s.sprite>=numsprites)return -1;
    int frame=s.frame&FF_FRAMEMASK;
    if(frame>=sprites[s.sprite].numframes)return -1;
    const spriteframe_t &sf=sprites[s.sprite].spriteframes[frame];
    if(!sf.rotate&&rotation)return -1;
    if(flip)*flip=sf.flip[rotation];
    return firstspritelump+sf.lump[rotation];
}
static const Relief *reliefOfState(int state) {
    bool flip=false,backFlip=false;
    int side=frameLump(state,2,&flip),back=frameLump(state,4,&backFlip);
    return reliefFor(frameLump(state,0),side,flip,back,backFlip);
}
void prepareRagdolls() {
    if(!settings.deathRagdoll)return;
    for(int type=0;type<NUMMOBJTYPES;++type)
        if(mobjinfo[type].flags&MF_COUNTKILL)reliefOfState(mobjinfo[type].painstate);
}

// Walls and steps within reach of the body: one-sided lines, and two-sided
// ones whose far floor stands over 16 units above the body's (a step it
// can't lie across) or whose opening is too low to lie through.
static std::vector<Wall> wallsNear(const mobj_t &thing,float reach) {
    std::vector<Wall> out;
    float x=units(thing.x),y=units(thing.y),floor=units(thing.floorz);
    for(int i=0;i<numlines;++i) {
        const line_t &line=lines[i];
        if(units(line.bbox[BOXLEFT])>x+reach||units(line.bbox[BOXRIGHT])<x-reach||
           units(line.bbox[BOXBOTTOM])>y+reach||units(line.bbox[BOXTOP])<y-reach)continue;
        float top=1e9f;
        if(line.backsector) {
            float lo=std::max(units(line.frontsector->floorheight),units(line.backsector->floorheight));
            float hi=std::min(units(line.frontsector->ceilingheight),units(line.backsector->ceilingheight));
            if(hi-lo>=16&&lo-floor<=16)continue;
            top=hi-lo<16?1e9f:lo-floor;
        }
        out.push_back({units(line.v1->x),units(line.v1->y),units(line.v2->x),units(line.v2->y),top});
    }
    return out;
}
// Whether a to b crosses a wall's line.
static bool crosses(float ax,float ay,float bx,float by,const Wall &w) {
    auto turn=[](float px,float py,float qx,float qy,float rx,float ry){return (qx-px)*(ry-py)-(qy-py)*(rx-px);};
    float d1=turn(w.ax,w.ay,w.bx,w.by,ax,ay),d2=turn(w.ax,w.ay,w.bx,w.by,bx,by);
    float d3=turn(ax,ay,bx,by,w.ax,w.ay),d4=turn(ax,ay,bx,by,w.bx,w.by);
    return ((d1>0)!=(d2>0))&&((d3>0)!=(d4>0));
}
// The floors round the kill: each cell's sector, sampled once (FloorGrid). A cell whose middle lies
// behind a solid wall (one-sided, or an opening too low to lie through) from the body samples whatever
// room is on the far side, another floor: it takes the body's own sector instead, or a ledge or pit
// up to a cell wide hugged the wall and snagged or swallowed limbs falling against it.
static FloorGrid floorsNear(const mobj_t &thing,const std::vector<Wall> &walls,float reach) {
    FloorGrid g;
    float tx=units(thing.x),ty=units(thing.y);
    g.size=(int)(2*reach/floorCell);g.x0=tx-reach;g.y0=ty-reach;
    g.cells.resize((size_t)g.size*g.size);
    const sector_t *own=R_PointInSubsector(thing.x,thing.y)->sector;
    std::vector<const Wall*> solid;
    for(const Wall &w:walls)if(w.top>=1e9f)solid.push_back(&w);
    for(int j=0;j<g.size;++j)for(int i=0;i<g.size;++i) {
        float x=g.x0+(i+0.5f)*floorCell,y=g.y0+(j+0.5f)*floorCell;
        const sector_t *sector=R_PointInSubsector((fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT))->sector;
        if(sector!=own)
            for(const Wall *w:solid)if(crosses(tx,ty,x,y,*w)) {sector=own;break;}
        g.cells[(size_t)j*g.size+i]=sector;
    }
    return g;
}
static std::vector<int> deathStepsOf(int first) {
    std::vector<int> steps;
    for(int state=first,n=0;n<64&&state>0;++n) {
        if(std::find(steps.begin(),steps.end(),state)!=steps.end())break;
        steps.push_back(state);
        if(states[state].tics<0)break;
        state=states[state].nextstate;
    }
    return steps;
}

// The walls in body space round where it died (mirrored with the body: the doll falls unmirrored
// and is drawn mirrored).
static std::vector<Wall> wallsHere(const Ragdoll &doll) {
    std::vector<Wall> out;
    float side=doll.mirror?-1.0f:1.0f;
    for(const Wall &w:doll.walls) {
        float a0=w.ax-doll.ox,a1=w.ay-doll.oy,b0=w.bx-doll.ox,b1=w.by-doll.oy;
        out.push_back({side*(a0*doll.ax[0]+a1*doll.ax[1]),a0*doll.ay[0]+a1*doll.ay[1],
                       side*(b0*doll.ax[0]+b1*doll.ax[1]),b0*doll.ay[0]+b1*doll.ay[1],w.top});
    }
    return out;
}

// The fall the hit makes (tests/death_ragdoll.py FALLS): which points it strikes, the push
// (units per tic: across the body, away from the shooter, up) and each muscle group's hold
// and let-go (tics).
enum { FALL_CHEST,FALL_LEGS,FALL_SIDE,FALL_HEAD,FALL_SWEEP };
bool startRagdoll(const mobj_t &thing,float dx,float dy,float speed,float side,float high,float hitZ,float hitU,bool blast) {
    const bool punch=settings.deathBlast&&!blast;    // F4 Flying enemies: shots punch harder, blasts fly
    blast=blast&&settings.deathBlast;
    if(punch)speed*=hitPunch;
    const mobjinfo_t &info=*thing.info;
    const Relief *body=reliefOfState(info.painstate);
    if(!body)return false;
    Ragdoll doll;
    doll.body=body;doll.state=info.painstate;doll.start=leveltime;doll.steps=deathStepsOf(info.deathstate);
    // Gibbed by a blast (Flying enemies only, see startDeath): drawn whole and thrown while the game plays
    // its gib frames, so the doll lasts as long as those do.
    if(std::find(doll.steps.begin(),doll.steps.end(),(int)(thing.state-states))==doll.steps.end()&&info.xdeathstate>0)
        doll.steps=deathStepsOf(info.xdeathstate);
    doll.poseTics=std::clamp((int)states[info.deathstate].tics,2,10);   // the pace of its own death frames
    if(doll.steps.size()<2)return false;
    // Its draw: the thing and the tic. Test runs (ragdoll_test.cpp) key it by where the thing was placed
    // instead of where it sits in memory, so a run replays the same falls.
    uint32_t who=(uint32_t)(uintptr_t)&thing;
    if(M_CheckParm((char*)"-ragdolltest"))who=(uint32_t)thing.spawnpoint.x*73856093u^(uint32_t)thing.spawnpoint.y*19349663u^(uint32_t)thing.type;
    uint32_t seed=hash32(who*2654435761u^(uint32_t)leveltime*40503u^0x7a9du);
    float chance=(float)(hash32(seed^0x6a09e667u)>>8)/16777216.0f;   // its own draw: the falls' stay as they were
    auto rnd=[&] {seed=hash32(seed+0x9e3779b9u);return (float)(seed>>8)/16777216.0f;};
    // The body's axes: the way it faced when it died (a monster shot from behind falls from behind,
    // its back painted from the drawing seen from behind). Turned to face the player, every death
    // swung round at the kill, and a shot in the back read as a blast to the chest.
    float facing=(float)(thing.angle*(2*doomPi/4294967296.0)),fx=-std::cos(facing),fy=-std::sin(facing);
    doll.ax[0]=fy;doll.ax[1]=-fx;doll.ay[0]=fx;doll.ay[1]=fy;
    doll.ox=units(thing.x);doll.oy=units(thing.y);
    // Two of a kind dropped in the same tic never lie alike: the later one mirrored the other way.
    doll.mirror=rnd()<0.5f;
    {
        struct Taken { int type;bool mirror; };
        static std::vector<Taken> taken;static int takenTic=-1;
        if(takenTic!=leveltime) {taken.clear();takenTic=leveltime;}
        for(const Taken &t:taken)if(t.type==thing.type)doll.mirror=!t.mirror;
        taken.push_back({thing.type,doll.mirror});
    }
    // World to body space: x mirrored with the drawing (the doll falls unmirrored, like the walls).
    float sgn=doll.mirror?-1.0f:1.0f;
    auto bodyWay=[&](float wx,float wy){return V3{sgn*(wx*doll.ax[0]+wy*doll.ax[1]),wx*doll.ay[0]+wy*doll.ay[1],0};};
    V3 struckWay=bodyWay(dx,dy);
    float tx=struckWay.x,ty=struckWay.y;
    if(std::abs(tx)+std::abs(ty)<1e-3f)ty=1;
    if(const mobj_t *eye=players[displayplayer].mo) {
        V3 e=bodyWay(units(eye->x-thing.x),units(eye->y-thing.y));
        if(length(e)>1e-3f)doll.toEye=unit(e);
    }
    // Side and hitU come across the shot (positive: the shooter's left); in body space that is the
    // body's -x only when it faces the shooter.
    float acrossShot=sgn*(dy*doll.ax[0]-dx*doll.ax[1]);
    side*=acrossShot;hitU*=acrossShot;
    bool headshot=body->hasHead&&hitZ>=body->neck&&std::abs(hitU-body->headMiddle)<=body->headWidth/2+1;
    // Weak hits buckle the legs. A bullet pushes a 100-mass monster at most 1.9 units a tic: under
    // the old cut at 2 every pistol and chaingun kill buckled, never thrown.
    // A heavy blast low on the legs sweeps them out from under it instead: the feet kicked away along the
    // shot, and the body, its support gone, falls by its own weight (buckled, it sank in place and
    // toppled over 10 tics, a collapse).
    int kind=headshot?FALL_HEAD:high<-0.2f&&speed>=sweepFrom?FALL_SWEEP:high<-0.2f||speed<1.0f?FALL_LEGS:std::abs(side)>0.5f?FALL_SIDE:FALL_CHEST;
    float force=std::clamp(speed/6,0.6f,1.4f),blow=std::clamp(speed/3.5f,0.8f,1.6f);   // blow: 1 at a shotgun kill
    if(M_CheckParm((char*)"-ragdolllog"))fprintf(stderr,"ragdollstart tic %d speed %.2f kind %d punch %d blast %d slide %.2f\n",leveltime,speed,kind,punch,blast,std::hypot(units(thing.momx),units(thing.momy)));
    bool leftHit=side>0;                         // side: positive on the shooter's left, the body's -x
    int struck=leftHit?0:1;float across=leftHit?1.0f:-1.0f;
    std::vector<int> hit;V3 push;
    // Swept: legs and trunk stiff a few tics, so the whole body turns as one (folded at the hips, the kick
    // only sat it down), the neck loose so the head lags and comes down last and hardest.
    static const float holds[5][MUSCLES][2]={{{9,6},{5,10},{2,6}},{{0,5},{10,10},{6,10}},{{4,8},{5,10},{0,4}},{{0,2},{0,2},{0,2}},{{4,4},{8,6},{0,3}}};
    switch(kind) {
    case FALL_CHEST:hit={P_SPINE,P_SH_L,P_SH_R,P_NECK};push=V3{tx,ty,0}*(2.2f*force)+V3{0,0,0.4f};break;
    case FALL_LEGS:hit={struck?P_HIP_R:P_HIP_L,struck?P_KNEE_R:P_KNEE_L,P_SPINE};push=V3{1.1f*across,0.5f*ty,0}*blow;break;
    case FALL_SIDE:hit={struck?P_SH_R:P_SH_L,P_NECK,P_SPINE};push=V3{1.6f*across,0.7f*ty,0}*blow;break;
    case FALL_SWEEP:break;
    default:hit={P_HEAD};push=V3{tx,ty,0}*(1.2f*blow);break;
    }
    for(int g=0;g<MUSCLES;++g) {doll.hold[g]=holds[kind][g][0]+(rnd()*4-2);doll.over[g]=holds[kind][g][1];}
    doll.late=rnd()*6;                           // one leg's muscles let go later than the other's
    doll.p=doll.prev=body->rest;
    // Two chest blasts in three from the front (pushed away from the eye) play the shot death on
    // strings, where the hand rig fits the keys. The harder the blast, the harder the wire: a
    // shotgun's kill pushes 3-4 units a tic; point blank it lands flat on id's own timing.
    // Every chest blast launches the body, as hard as the game pushed it: a far shot hops it back, point
    // blank throws it (2 up, 1 more along the shot and a 0.05 somersault at a shotgun kill's 3.5 units
    // a tic, on top of the corpse's own slide, which the doll now flies with; heavy monsters, pushed
    // less, barely leave the floor; a bullet only hops it).
    float hard=kind==FALL_CHEST?std::clamp((speed-0.8f)/2.7f,0.0f,1.5f):0.0f;
    float sideways=rnd()*0.5f-0.25f;
    if(punch&&(kind==FALL_CHEST||kind==FALL_SIDE)) {
        // Graded by the (doubled) push: up 2 at a chaingun's kill, 2.4 a shotgun's, 3.4 a super shotgun's
        // point blank. And it spins as struck: a hit off the middle turns it about the vertical (a pirouette,
        // the struck side going back), a high one tips it over backward (a somersault); the harder, the
        // faster, each varied 30% either way.
        float lift=std::clamp(1.2f*std::log(std::max(speed,1.0f)),0.0f,punchLift),along=std::min(0.12f*speed,2.0f);
        doll.launchV=V3{tx+2*sideways,ty,0}*along+V3{0,0,lift};
        doll.launchSpin=std::min(0.012f*speed*(0.5f+std::max(high,0.0f)),0.16f)*(0.7f+0.6f*rnd());
        doll.launchTwist=std::clamp(-side*ty*0.035f*speed,-0.35f,0.35f)*(0.7f+0.6f*rnd());
        doll.launchAt=0;
    }
    else if(hard>0) {
        float most=std::min(hard,1.2f);              // a super shotgun's 9 a tic threw it past the walls it knows
        doll.launchV=V3{tx+2*sideways,ty,0}*std::min(hard,1.0f)+V3{0,0,2.0f*most};doll.launchSpin=0.05f*std::min(hard,1.0f);doll.launchAt=0;
    }
    doll.blasted=blast;
    if(blast)doll.reach=blastReach;
    if(blast) {doll.launchV=V3{tx+2*sideways,ty,0}*blastThrow+V3{0,0,blastLift};doll.launchSpin=blastSpin;doll.launchAt=0;}
    doll.tipWay=unit(V3{tx,ty,0});
    if(kind==FALL_SWEEP) {
        // Hollywood, like the chest launch: the blast to the shins throws the legs up and out along the
        // shot and turns the body over about its middle, the trunk going back toward the shooter (on its
        // face, from the front), and lifts it a little; the knees are gone (no muscle holds the legs).
        // From there only its weight: the trunk, still held, comes down hard, the loose head last. Its own
        // weight alone was too slow: the feet slid out and the trunk toppled like a felled tree, 19 tics
        // to the floor (a collapse), or, the feet lifted, the whole body dropped upright (nothing turns a
        // falling body). The lower the hit and the harder the blast, the harder the throw; one leg
        // kicked a little harder: no two alike. And it turns 30-45 degrees about the vertical toward the
        // struck side on the way down: straight along the shot the body fell along the line of sight, its
        // flying legs hidden behind a foreshortened heap (and turned over about a slanted axis instead, it
        // went down diagonally onto a shoulder and rolled back up to sitting).
        float low=std::clamp((-high-0.2f)/0.6f,0.0f,1.0f),strength=std::clamp(speed/3.5f,0.7f,1.4f)*(0.6f+0.4f*low);
        float slant=(0.52f+0.27f*rnd())*(std::abs(side)>0.05f?across:rnd()<0.5f?1.0f:-1.0f);
        V3 d=doll.tipWay,c{};
        doll.tipWay=d*-1;doll.swept=true;
        for(int n=0;n<DOLL_POINTS;++n)c=c+doll.p[n];
        c=c*(1.0f/DOLL_POINTS);
        for(int n=0;n<DOLL_POINTS;++n) {V3 r=doll.p[n]-c;doll.push(n,V3{d.x*r.z,d.y*r.z,-dot(r,d)}*(-sweepSpin*strength)+V3{0,0,sweepLift*strength});}
        int lead=rnd()<0.5f?P_ANK_L:P_ANK_R;
        doll.push(lead,d*(0.8f*strength));
        float yaw=slant/sweepTurnTics;
        for(int n=0;n<DOLL_POINTS;++n) {V3 r=doll.p[n]-c;doll.push(n,V3{-r.y,r.x,0}*yaw);}
        doll.noDrift();
    } else if(settings.deathGuided&&body->guided&&kind==FALL_CHEST&&ty>0.7f&&chance<0.67f&&!blast) {
        doll.guided=true;
        if(doll.guide.flinch&&doll.launchAt>=0)doll.launchAt=flinchTics;
        doll.yank=V3{tx+sideways,ty,0.15f}*(doll.guide.ratchet*std::clamp(speed/4,0.7f,1.8f));
        doll.yaw=rnd()*0.12f-0.06f;
        for(float &kick:doll.kicks)kick=0.2f+0.6f*rnd();
        doll.legLate=rnd()*2;
    } else {
        // The push, varied a little in strength and across; a little turn about the vertical;
        // a small, uneven kick forward and out to each knee. Then the push's net drift is taken
        // out: the game's own momentum carries the body, the doll only turns and folds it.
        float drift=rnd()*0.8f-0.4f;
        for(int n:hit)doll.push(n,push*(0.8f+0.4f*rnd())+V3{drift,0,0});
        float yaw=rnd()*0.2f-0.1f;
        for(int n=0;n<DOLL_POINTS;++n) {V3 r=doll.p[n]-doll.p[P_SPINE];doll.push(n,V3{-r.y,r.x,0}*yaw);}
        V3 f=doll.front();
        for(int s=0;s<2;++s)doll.push(s?P_KNEE_R:P_KNEE_L,f*(0.2f+0.9f*rnd())+V3{(s?1.0f:-1.0f)*0.4f*rnd(),0,0});
        doll.noDrift();
        // And the hit tips it over its feet the way it was struck, at once (steered on through the
        // fall instead, the turn came late and eased, a dive rather than a blow).
        doll.tip(kind==FALL_CHEST?0.15f*blow:0.3f*blow);
    }
    // The corpse's own slide at the kill is the doll's: it flies on in the world from where it died.
    // Carried by the thing it was braked by the floor's friction in mid-air and slid on after it landed.
    {
        float mx=units(thing.momx),my=units(thing.momy),ms=std::hypot(mx,my);
        if(punch) {mx*=hitPunch;my*=hitPunch;ms*=hitPunch;}
        float most=blast?blastCarry:punch?punchCarry:carryMost;
        if(ms>most) {mx*=most/ms;my*=most/ms;}
        // Turned a few degrees off the shot's line, one way or the other: straight along it, every body
        // of a kind flew the same line and landed alike. Drawn last, so the rest of the fall is unchanged.
        float turn=(slideTurnLeast+(slideTurnMost-slideTurnLeast)*rnd())*(rnd()<0.5f?-1.0f:1.0f)*doomPi/180;
        float tc=std::cos(turn),ts=std::sin(turn),turned=mx*tc-my*ts;my=mx*ts+my*tc;mx=turned;
        V3 carried=bodyWay(mx,my);
        for(int n=0;n<DOLL_POINTS;++n)doll.push(n,carried);
    }
    doll.shown=doll.p;
    doll.walls=wallsNear(thing,doll.reach);
    doll.floors=floorsNear(thing,doll.walls,doll.reach);
    doll.gx=doll.ox-doll.floors.x0;doll.gy=doll.oy-doll.floors.y0;doll.gz=units(thing.z);
    doll.nearWalls=wallsHere(doll);doll.walls.clear();doll.walls.shrink_to_fit();
    for(int n=0;n<DOLL_POINTS;++n)doll.centre[0]+=doll.p[n].x/DOLL_POINTS,doll.centre[1]+=doll.p[n].y/DOLL_POINTS;
    std::copy(doll.centre,doll.centre+2,doll.centreWas);std::copy(doll.centre,doll.centre+2,doll.centreShown);
    forgetRagdoll(&thing);
    dolls[&thing]=std::move(doll);
    return true;
}

static float fade(float t,float hold,float over) {return t>hold?std::clamp(1-(t-hold)/over,0.0f,1.0f):1.0f;}
bool ragdollView(const mobj_t &thing,DeathView &view) {
    auto found=dolls.find(&thing);
    if(found==dolls.end())return false;
    Ragdoll &doll=found->second;
    // Crushed to gibs, raised, or otherwise moved on: the game's own frames again.
    if(thing.health>0||std::find(doll.steps.begin(),doll.steps.end(),(int)(thing.state-states))==doll.steps.end())return false;
    // The fall runs in game tics, in the world from where it died. A pose is drawn every earlyPose
    // tics through the hit (at the death frames' pace the recoil fell between the first two poses),
    // then as often as the monster's own death frames change (the zombieman's every 5 tics, the
    // imp's every 8), and always at the top of a throw, the tic it first touches down and the tic
    // its torso does: sampled evenly that slowly, the impact often fell between two poses and the
    // body jumped from mid-air to lying.
    int target=leveltime-doll.start;
    while(doll.tics<target&&!doll.resting) {
        float t=(float)doll.tics,muscle[MUSCLES],k=0;
        DollPose shape;
        if(doll.tics==doll.launchAt)doll.launch();
        if(doll.guided)doll.guideBefore(doll.tics,muscle,shape,k);
        else if(doll.swept) {
            // The knees gone, the trunk and hips held until the chest is low, the neck loose. With the hips
            // loose too, a body going over onto its back folded its legs up to its chest, landed on its
            // butt and the legs levered the trunk back up: it sat 15 tics before going over.
            muscle[M_LEGS]=0;muscle[M_NECK]=0;
            muscle[M_TRUNK]=doll.hipHold=doll.lowAt<0?1.0f:std::clamp(1-(t-doll.lowAt)/3,0.0f,1.0f);
        }
        else for(int g=0;g<MUSCLES;++g)muscle[g]=fade(t,doll.hold[g]+(g==M_LEGS?doll.late:0),doll.over[g]);
        // Held until its hips reach the floor (let go by the clock, half slumped, the legs folded under
        // the pinned torso sprang it off the wall at 6 units a tic), slamTics at most.
        doll.slamAge=doll.slamAt>=0&&doll.tics-doll.slamAt<slamTics&&doll.up(P_SPINE)>10&&!doll.seated()?(float)(doll.tics-doll.slamAt):-1;
        if(doll.slamAge>=0) {             // slammed: the legs give at once, the trunk half held
            muscle[M_LEGS]=0;muscle[M_TRUNK]=std::min(muscle[M_TRUNK],0.5f);
            k=0;
        }
        doll.heavy=doll.lyingAt>=0&&doll.tics-doll.lyingAt>=lyingGrace;
        doll.hitWall=0;doll.hitPoint=-1;doll.wallTouches=0;
        float was=0,spineWas=doll.p[P_SPINE].z;
        for(int n=0;n<DOLL_POINTS;++n)was+=doll.p[n].z;
        doll.tic(muscle,doll.nearWalls,doll.guided||k>0?&shape:nullptr,k);
        doll.keepNear();
        bool before=doll.landed;
        if(doll.tics>=2)doll.steer();
        if(doll.guided||doll.swept)doll.guideAfter(doll.tics);
        ++doll.tics;
        float z=0,cx=0,cy=0,energy=0;
        for(int n=0;n<DOLL_POINTS;++n) {
            z+=doll.p[n].z;cx+=doll.p[n].x;cy+=doll.p[n].y;
            V3 v=doll.p[n]-doll.prev[n];energy+=dot(v,v);
        }
        float rise=(z-was)/DOLL_POINTS;
        bool top=!doll.landed&&doll.rise>0.3f&&rise<=0;
        doll.rise=rise;
        std::copy(doll.centre,doll.centre+2,doll.centreWas);
        doll.centre[0]=cx/DOLL_POINTS;doll.centre[1]=cy/DOLL_POINTS;
        doll.still=doll.landed&&energy<0.002f?doll.still+1:0;
        bool landing=doll.landed&&!before,torso=false,slam=false;
        // A slam when the body hits a wall with its torso, hard, still up: not when it slumps into one.
        if(doll.slamAt<0&&!doll.landed&&!doll.blasted&&doll.hitWall*dollSubsteps>slamSpeed&&doll.up(P_SPINE)>14&&
           unit(doll.p[P_NECK]-doll.hips()).z>0.4f) {
            doll.slamAt=doll.tics;doll.slamWay=doll.hitWay;slam=true;
            // The knees buckle out from the wall (on straight legs a slammed body stood pinned upright).
            for(int n:{P_KNEE_L,P_KNEE_R})doll.push(n,doll.slamWay*slamBuckle+V3{0,0,-0.3f});
            doll.push(P_HIP_L,doll.slamWay*(0.3f*slamBuckle));doll.push(P_HIP_R,doll.slamWay*(0.3f*slamBuckle));
        }
        // In the world: a body point, and a way in body space.
        auto world=[&](V3 q,float &wx,float &wy){doll.overGrid(q,wx,wy);wx+=doll.floors.x0;wy+=doll.floors.y0;};
        float sideSign=doll.mirror?-1.0f:1.0f;
        auto worldWay=[&](V3 v,float &wx,float &wy){wx=doll.ax[0]*sideSign*v.x+doll.ay[0]*v.y;wy=doll.ax[1]*sideSign*v.x+doll.ay[1]*v.y;};
        float drop=std::max(0.0f,spineWas-doll.p[P_SPINE].z);
        float slid=std::hypot(doll.centre[0]-doll.centreWas[0],doll.centre[1]-doll.centreWas[1]);
        if(doll.lyingAt<0&&doll.down()) {
            doll.lyingAt=doll.tics;torso=true;
            // The torso coming down splashes the floor under it (the head's touch is not the landing),
            // as hard as it came down: its fall (this tic's or the last whole one) and some of its slide.
            // In a liquid it plunges in instead, by its weight too (liquids.cpp); sprayLanding leaves liquids be.
            float wx,wy,hit=std::hypot(std::max(drop,doll.drop),0.5f*slid);world(doll.p[P_SPINE],wx,wy);
            bodySplash(wx,wy,hit,(float)thing.info->mass,units(thing.radius));
            if(!(thing.flags&MF_NOBLOOD))sprayLanding(wx,wy,doll.gz+doll.floorAt(doll.p[P_SPINE]),hit);
        }
        doll.drop=drop;
        // A torso slamming into a wall splashes it where it hit, once (the corpse's own slam watch in
        // spray.cpp followed the thing, not the body).
        if(!doll.splashed&&doll.hitPoint>=0&&doll.hitWall*dollSubsteps>slamSpeed&&!(thing.flags&MF_NOBLOOD)) {
            doll.splashed=true;
            float wx,wy,dx,dy;world(doll.p[doll.hitPoint],wx,wy);worldWay(doll.hitWay*-1,dx,dy);
            sprayWallHit(wx,wy,doll.gz+doll.p[doll.hitPoint].z,dx,dy,doll.hitWall*dollSubsteps);
        }
        // Down and sliding: a smear every so far behind its middle, thick end forward.
        if(doll.lyingAt>=0&&slid>1.2f&&!(thing.flags&MF_NOBLOOD)) {
            float size=std::clamp(units(thing.radius)*0.6f,9.0f,15.0f);
            if((doll.smearing+=slid)>=size*1.4f) {
                doll.smearing=0;
                float wx,wy,dx,dy;world(doll.p[P_SPINE],wx,wy);
                worldWay(V3{doll.centre[0]-doll.centreWas[0],doll.centre[1]-doll.centreWas[1],0}*(1/slid),dx,dy);
                spraySmear(wx-dx*size*0.6f,wy-dy*size*0.6f,std::atan2(dy,dx)+doomPi,size);
            }
        } else doll.smearing=0;
        // Still for 8 tics, or 40 after the torso came down if it no longer slides, 60 at the latest:
        // from then on a corpse sprite. (At 40 flat, 5 bodies in a review's 48 froze still sliding,
        // up to half a unit a tic.)
        int down=doll.lyingAt>=0?doll.tics-doll.lyingAt:-1;
        const char *stop=doll.still>=8?"quiet":down>=40&&slid<0.2f?"deadline":down>=60?"late":doll.tics>=175?"cap":nullptr;
        if(stop) {
            if(M_CheckParm((char*)"-ragdolllog"))fprintf(stderr,"ragdollrest %u tic %d down %d slide %.2f %s\n",(unsigned)((uintptr_t)&thing%100000),doll.tics,down,slid,stop);
            doll.resting=true;
            if((doll.restSector=doll.sectorUnder(doll.p[P_SPINE])))doll.restFloor=units(doll.restSector->floorheight);
            std::copy(doll.centre,doll.centre+2,doll.centreWas);
            doll.nearWalls.clear();doll.nearWalls.shrink_to_fit();doll.floors.cells.clear();doll.floors.cells.shrink_to_fit();
        }
        if(M_CheckParm((char*)"-ragdolllog")) {
            // Test runs (ragdoll_test.cpp): the turn per tic about the vertical (yaw), along the body
            // (roll) and all but the roll (tumble), radians; its middle's height over the floor it died on
            // and its speed over the floor.
            V3 v[DOLL_POINTS],c,vc,spin;
            for(int n=0;n<DOLL_POINTS;++n)v[n]=doll.p[n]-doll.prev[n];
            doll.rigid(v,c,vc,spin);spin=spin*(float)dollSubsteps;vc=vc*(float)dollSubsteps;
            V3 axis=unit(doll.p[P_NECK]-doll.hips());
            float yaw=spin.z,roll=dot(spin,axis),tumble=std::sqrt(std::max(0.0f,dot(spin,spin)-roll*roll));
            // hip: the angle (degrees) between the trunk and the legs, 180 standing straight
            V3 legs=middle(doll.p[P_ANK_L],doll.p[P_ANK_R])-doll.hips();
            float hip=std::acos(std::clamp(dot(axis,unit(legs)),-1.0f,1.0f))*180/doomPi;
            // twist: the shoulder line's turn about the trunk this tic, degrees (the rigid fit's roll
            // counts a leg folding against a wall as the body turning)
            V3 sh=doll.p[P_SH_R]-doll.p[P_SH_L];sh=sh-axis*dot(sh,axis);
            V3 sw=doll.shouldersWas-axis*dot(doll.shouldersWas,axis);
            float twist=doll.tics>1&&length(sw)>1e-3f&&length(sh)>1e-3f?std::atan2(dot(axis,cross(sw,sh)),dot(sw,sh))*180/doomPi:0;
            doll.shouldersWas=doll.p[P_SH_R]-doll.p[P_SH_L];
            fprintf(stderr,"ragdoll %u tic %d landed %d lying %d heavy %d slam %d wall %d mid %.1f speed %.2f yaw %.3f roll %.3f tumble %.3f up %.2f hips %.1f neck %.1f head %.1f hip %.0f twist %.1f\n",
                (unsigned)((uintptr_t)&thing%100000),doll.tics,doll.landed,doll.lyingAt>=0?doll.tics-doll.lyingAt:-1,doll.heavy,doll.slamAt,doll.wallTouches,
                c.z,std::hypot(vc.x,vc.y),yaw,roll,tumble,axis.z,doll.up(P_HIP_L),doll.up(P_NECK),doll.up(P_HEAD),hip,twist);
        }
        // In the air at most the zombieman's pace: at the imp's 8 tics a throw was one held pose sliding
        // through the air, a statue, then lying.
        int every=doll.tics<=earlyTics?earlyPose:doll.landed?doll.poseTics:std::min(doll.poseTics,airPose);
        if(doll.tics==1||doll.tics-doll.lastPose>=every||top||landing||torso||slam||doll.resting) {
            doll.shown=doll.p;++doll.shownAt;doll.lastPose=doll.tics;
            std::copy(doll.centre,doll.centre+2,doll.centreShown);
        }
    }
    view.sprite=states[doll.state].sprite;view.frame=states[doll.state].frame;view.mirror=doll.mirror;
    view.facing=thing.angle;view.bend=0;view.settle=0;view.fall=FallPose{};view.fall.active=true;
    view.ragdoll=&doll;
    return true;
}

// The card (see internal.h). The posed relief is drawn into a square picture by an orthographic
// camera, one pixel per unit, nearest texel, the nearest face winning (turned-away ones too: see below): each
// pixel a texel of the drawing, darkened through COLORMAP where the relief shows an edge (so it stays
// in the palette); shot-through texels left out, wound rims painted in blood colours. Its normals are
// the sprite normals of the picture itself, so it lights like any sprite. It is drawn again only when
// the pose, the 45-degree step, the pitch or the wounds change.
constexpr int cardKeyBase=-0x50000000;     // image keys clear of walls, lumps and blood
static int cardSerial=0;
static void dropCard(const Ragdoll &doll) {
    if(!doll.cardKey)return;
    images.erase(doll.cardKey);
    for(auto *group:{&batches,&mirrorBatches,&shadowBatches,&decalBatches})group->erase(doll.cardKey);
    // Shadow batches use the inverse image key, just as they do for WAD sprites.
    shadowBatches.erase(-1-doll.cardKey);decalBatches.erase(-1-doll.cardKey);
}
static const std::vector<V3> &posedCorners(const Ragdoll &doll) {
    const Relief &body=*doll.body;
    if(doll.posedAt!=doll.shownAt) {
        std::array<PartMotion,DOLL_PARTS> motion;partMotions(body.rest,doll.shown,motion);
        doll.posed.resize(body.corners.size());
        for(size_t i=0;i<body.corners.size();++i) {
            const ReliefCorner &c=body.corners[i];V3 q{};
            for(int k=0;k<4;++k)if(c.w[k]>0)q=q+carry(motion[c.part[k]],c.p)*c.w[k];
            doll.posed[i]=q;
        }
        doll.posedAt=doll.shownAt;
    }
    return doll.posed;
}
static byte nearestColour(const byte *palette,int r,int g,int b) {
    int best=0,bestDistance=INT_MAX;
    for(int i=0;i<256;++i) {
        int dr=palette[i*3]-r,dg=palette[i*3+1]-g,db=palette[i*3+2]-b,d=dr*dr+dg*dg+db*db;
        if(d<bestDistance) {bestDistance=d;best=i;}
    }
    return (byte)best;
}
// Where it is drawn: where it died, moved on by how far its middle has gone since the pose shown,
// between the last two tics: the pose steps on Doom's tics, its flight runs smooth, as a walking
// monster's does. Resting, the floor it lies on carries it (a lift). feet: the floor it died on.
static void dollPlace(const Ragdoll &doll,float fraction,float &x,float &y,float &feet) {
    float side=doll.mirror?-1.0f:1.0f;
    float mx=doll.centreWas[0]+(doll.centre[0]-doll.centreWas[0])*fraction-doll.centreShown[0];
    float my=doll.centreWas[1]+(doll.centre[1]-doll.centreWas[1])*fraction-doll.centreShown[1];
    x=doll.ox+doll.ax[0]*side*mx+doll.ay[0]*my;y=doll.oy+doll.ax[1]*side*mx+doll.ay[1]*my;
    feet=doll.gz+(doll.restSector?units(doll.restSector->floorheight)-doll.restFloor:0);
}
bool ragdollWhere(const Ragdoll &doll,float fraction,float &x,float &y,float &z) {
    dollPlace(doll,fraction,x,y,z);
    x+=doll.ax[0]*doll.cardMiddle[0]+doll.ay[0]*doll.cardMiddle[1];y+=doll.ax[1]*doll.cardMiddle[0]+doll.ay[1]*doll.cardMiddle[1];
    return doll.landed;
}
bool ragdollCard(const Ragdoll &doll,float fraction,float sink,const float *eye,const std::vector<byte> *holes,RagdollCard &card) {
    const Relief &body=*doll.body;
    if(body.cardSize<=0)return false;
    int size=body.cardSize;
    // Drawn space: the body's own, mirrored when it is drawn mirrored (the world is x + ax*m.x + ay*m.y).
    float side=doll.mirror?-1.0f:1.0f;
    float x,y,feet;dollPlace(doll,fraction,x,y,feet);feet-=sink;
    V3 upright=unit(doll.shown[P_NECK]-middle(doll.shown[P_HIP_L],doll.shown[P_HIP_R]));
    float lying=std::clamp(1-upright.z,0.0f,1.0f);
    auto found=images.find(doll.cardKey);
    auto place=[&] {
        card.key=doll.cardKey;card.size=size;card.half=size/2.0f;
        card.u0=doll.cardLeft;card.v0=doll.cardFirst;card.u1=doll.cardRight;card.v1=doll.cardBottom+1;
        card.x=x+doll.ax[0]*doll.cardMiddle[0]+doll.ay[0]*doll.cardMiddle[1];card.y=y+doll.ax[1]*doll.cardMiddle[0]+doll.ay[1]*doll.cardMiddle[1];
        // Its lowest solid row on the body's lowest surface (the floor it lies on, which may be below
        // the thing's: Doom holds a corpse up at the highest floor under its box), not the thing's feet.
        float lowest=1e9f;
        for(int n=0;n<DOLL_POINTS;++n)lowest=std::min(lowest,doll.shown[n].z-radius[n]);
        // Lying, pressed cardSink into the floor: the picture's lowest rows are the body's near
        // edge seen from above, and a row or two of floor-coloured fringe read as a gap under it.
        card.top=feet+lowest+doll.cardBottom+1-cardSink*lying;
        return true;
    };
    if(doll.baked&&doll.cardKey&&found!=images.end())return place();   // a corpse sprite now: the same from every angle
    const std::vector<V3> &posed=posedCorners(doll);
    if(posed.empty())return false;
    // The camera aims at the body's middle, in height too: aimed at its feet, the top of a body
    // still standing reached past the picture (the canvas only spans the body's own size).
    V3 lo{1e9f,1e9f,1e9f},hi{-1e9f,-1e9f,-1e9f};
    for(const V3 &q:posed) {V3 m{side*q.x,q.y,q.z};lo={std::min(lo.x,m.x),std::min(lo.y,m.y),std::min(lo.z,m.z)};hi={std::max(hi.x,m.x),std::max(hi.y,m.y),std::max(hi.z,m.z)};}
    V3 o=middle(lo,hi);
    // The virtual camera: from the eye's side of the body, stepped to 45 degrees from its front, as
    // the eye sees every sprite, all through the fall: what the shot did is what is drawn. (It used to
    // turn side-on as the body went flat, which hid the far half of a lying body hanging over the
    // floor, but read as the body rolling onto its side after it landed.)
    float ex=eye[0]-x,ey=eye[1]-y;
    V3 seen{ex*doll.ax[0]+ey*doll.ax[1],ex*doll.ay[0]+ey*doll.ay[1],0};
    float azimuth=std::atan2(o.x-seen.x,o.y-seen.y);
    int step=((int)std::lround(azimuth/(doomPi/4))%8+8)%8;
    // A step is kept until the view is well past the next one's middle: flipping between two on the
    // line between them looked like the body turning.
    if(doll.lastStep>=0&&std::abs(std::remainder(azimuth-doll.lastStep*doomPi/4,2*doomPi))<doomPi/8+doomPi/18)step=doll.lastStep;
    doll.lastStep=step;
    int angle=doll.baked?doll.bakedAngle:step*45;   // whole degrees
    float a=angle*doomPi/180;
    V3 along=doll.shown[P_HEAD]-middle(doll.shown[P_ANK_L],doll.shown[P_ANK_R]);
    float lx=side*along.x,ly=along.y;
    // Tipped down as the body goes over (straight on standing, as the sprite it was), lying as far as
    // the eye looks down at it, at most tiltMost. The card stands upright, so whatever of the body runs
    // away from the eye is drawn as height; tipped further than the eye looks (30 degrees, where the
    // eye from 128 units looks down 18 and from 256 units 9) the far half of a lying body hung over
    // the floor.
    float cx=x+doll.ax[0]*o.x+doll.ay[0]*o.y,cy=y+doll.ax[1]*o.x+doll.ay[1]*o.y;
    float looks=std::atan2(eye[2]-feet,std::max(std::hypot(eye[0]-cx,eye[1]-cy),1.0f))*180/doomPi;
    int tilt=doll.baked?doll.bakedTilt:(int)std::lround(std::clamp(looks,0.0f,tiltMost)*lying/5);   // pitch in 5-degree steps
    float pitch=tilt*5*doomPi/180;
    V3 fh{std::sin(a),std::cos(a),0},f{fh.x*std::cos(pitch),fh.y*std::cos(pitch),-std::sin(pitch)},r{fh.y,-fh.x,0},u=cross(r,f);
    uint32_t wounds=0;
    if(holes)for(size_t i=0;i<holes->size();++i)wounds=wounds*31+(*holes)[i];
    uint64_t signature=((uint64_t)(uint32_t)doll.shownAt<<32)^((uint64_t)angle<<20)^((uint64_t)tilt<<14)^((uint64_t)doll.mirror<<8)^((uint64_t)wounds*0x9e3779b97f4a7c15ull);
    if((!doll.cardKey||found==images.end()||signature!=doll.cardSignature)&&M_CheckParm((char*)"-ragdolllog")) {
        // Test runs: each new picture's camera angle and the body's own heading in it (degrees).
        float heading=std::atan2(ly,lx)*(180/doomPi);
        fprintf(stderr,"ragdollcard %u pose %d angle %d heading %.1f tilt %d looks %.1f\n",(unsigned)((uintptr_t)&doll%100000),
            doll.shownAt,angle,heading,tilt,looks);
    }
    if(!doll.cardKey||found==images.end()||signature!=doll.cardSignature) {
        static const byte *palette=nullptr,*colormap=nullptr;
        static byte rims[4];
        if(!palette) {
            palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_STATIC);colormap=(const byte*)W_CacheLumpName((char*)"COLORMAP",PU_STATIC);
            rims[0]=nearestColour(palette,30,4,4);rims[1]=nearestColour(palette,140,8,8);rims[2]=nearestColour(palette,196,56,48);rims[3]=nearestColour(palette,84,10,8);
        }
        std::vector<float> depth((size_t)size*size,1e9f);
        std::vector<byte> pixels((size_t)size*size*2,0);
        float half=size/2.0f;
        for(const ReliefFace &face:body.faces) {
            size_t texel=(size_t)face.r*body.width+face.c;
            byte wound=holes&&texel<holes->size()?(*holes)[texel]:0;
            if(wound==1)continue;                    // shot through
            V3 q[4];
            for(int k=0;k<4;++k) {V3 b=posed[face.corner[k]];q[k]={side*b.x-o.x,b.y-o.y,b.z-o.z};}
            // Faces turned away from the camera are drawn too, darker and a unit behind (a face toward it
            // at the same depth wins): where the body bends, faces fold over or twist half away, and with
            // them left out 1-5% of a falling body's picture was holes onto the floor behind it (arena,
            // three monsters, five shots, front and back), at the chest and hips most.
            bool away=dot(cross(q[3]-q[1],q[2]-q[0])*side,f)>=0;
            byte colour=face.kind==FACE_BACK?body.backColour[texel]:body.colour[texel];
            if(wound>=2&&face.kind==FACE_FRONT)colour=rims[std::min(3,wound-2)];
            int level=std::clamp((int)std::lround((1-face.dim)*24)+(away?6:0),0,31);
            colour=colormap[level*256+colour];
            float px[4],py[4],pd[4];
            for(int k=0;k<4;++k) {px[k]=dot(q[k],r)+half;py[k]=half-dot(q[k],u);pd[k]=dot(q[k],f)+(away?1.0f:0.0f);}
            for(int t=0;t<2;++t) {
                int i0=0,i1=t?2:1,i2=t?3:2;
                float area=(px[i1]-px[i0])*(py[i2]-py[i0])-(px[i2]-px[i0])*(py[i1]-py[i0]);
                if(std::abs(area)<1e-9f)continue;
                int x0=std::max(0,(int)std::floor(std::min({px[i0],px[i1],px[i2]}))),x1=std::min(size-1,(int)std::ceil(std::max({px[i0],px[i1],px[i2]})));
                int y0=std::max(0,(int)std::floor(std::min({py[i0],py[i1],py[i2]}))),y1=std::min(size-1,(int)std::ceil(std::max({py[i0],py[i1],py[i2]})));
                for(int j=y0;j<=y1;++j)for(int i=x0;i<=x1;++i) {
                    float cx=i+0.5f,cy=j+0.5f;
                    float w0=((px[i1]-cx)*(py[i2]-cy)-(px[i2]-cx)*(py[i1]-cy))/area;
                    float w1=((px[i2]-cx)*(py[i0]-cy)-(px[i0]-cx)*(py[i2]-cy))/area,w2=1-w0-w1;
                    if(w0<0||w1<0||w2<0)continue;
                    float d=w0*pd[i0]+w1*pd[i1]+w2*pd[i2];
                    size_t at=(size_t)j*size+i;
                    if(d<depth[at]) {depth[at]=d;pixels[at*2]=colour;pixels[at*2+1]=255;}
                }
            }
        }
        int left=size,right=0,first=size,bottom=-1;
        for(int j=0;j<size;++j)for(int i=0;i<size;++i)if(pixels[((size_t)j*size+i)*2+1]) {
            left=std::min(left,i);right=std::max(right,i+1);first=std::min(first,j);bottom=j;
        }
        doll.cardBottom=bottom<0?size-1:bottom;
        doll.cardLeft=left;doll.cardRight=right;doll.cardFirst=first;
        std::vector<byte> normals=spriteNormals(size,size,pixels.data());
        if(!doll.cardKey)doll.cardKey=cardKeyBase+(++cardSerial&0x0fffffff);
        found=images.find(doll.cardKey);
        if(found==images.end())found=images.emplace(doll.cardKey,upload(size,size,pixels.data(),0,0,nullptr,normals.data())).first;
        else {
            std::vector<byte> packed((size_t)size*size*4);
            for(size_t i=0;i<(size_t)size*size;++i) {packed[4*i]=pixels[2*i];packed[4*i+1]=pixels[2*i+1];packed[4*i+2]=normals[2*i];packed[4*i+3]=normals[2*i+1];}
            gpuUpdateTexture(found->second.texture,GpuFormat::RGBA8,{{0,0,size,size}},packed.data(),size*4);
            found->second.pixels=std::move(pixels);
        }
        doll.cardSignature=signature;
    }
    doll.cardMiddle[0]=o.x;doll.cardMiddle[1]=o.y;
    if(doll.resting&&!doll.baked&&doll.cardSignature==signature) {
        // At rest and drawn in its last pose: from now on a sprite, the doll's posed body freed.
        doll.baked=true;doll.bakedAngle=angle;doll.bakedTilt=tilt;
        doll.posed.clear();doll.posed.shrink_to_fit();doll.posedAt=-1;
    }
    return place();
}
}
