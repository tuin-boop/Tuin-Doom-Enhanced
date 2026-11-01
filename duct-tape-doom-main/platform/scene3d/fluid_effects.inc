extern "C" int P_RocketDoorImpact(mobj_t *,line_t *);
// FluidWeapons Player.zsc particle behavior, adapted to render-only bounded
// batches. No actors, gameplay RNG, or damage are introduced by these effects.
#include "fluid_effect_assets.inc"
struct FluidParticle {
    float x,y,z,vx,vy,vz,az,size,growth,alpha,age,life;
    int mask; bool add; std::array<float,3> color;
    float ax=0,ay=0,halfSize=.5f,fadeTics=0,roll=0,rollSpeed=0;
    bool fullBright=false;bool ownerRelative=false;float ownerX=0,ownerY=0,ownerZ=0;
};
struct FluidBurst {float x,y,z;int type,tic;};
struct FluidMark {int line,side,section,sector,mask;bool ceiling,flip;float u,z,size,born,fadeStart,life;std::array<float,3> color;unsigned supportRevision=~0u;};
static std::vector<FluidParticle> fluidParticles;
static std::vector<FluidParticle> rubbleParticles;
static std::vector<FluidBurst> fluidBursts;
static std::vector<FluidMark> fluidMarks;
static unsigned fluidSerial;
static std::vector<FluidParticle> indoorSmoke;
static int fluidImage(int mask) {
    // Private image keys cannot collide with WAD lumps or wall textures.
    int lump=100000000+mask,key=-1-lump;
    if(!images.count(key)) {
        const auto &m=fluidMasks[mask];std::vector<byte> data(m.w*m.h*2);
        for(int i=0;i<m.w*m.h;++i){data[i*2]=0;data[i*2+1]=m.alpha[i];}
        images.emplace(key,upload(m.w,m.h,data.data()));
    }
    return lump;
}
static void resetFluidEffects() {
    if(fluidSerial!=r_levelserial){indoorSmoke.clear();rubbleParticles.clear();fluidParticles.clear();fluidBursts.clear();fluidMarks.clear();fluidSerial=r_levelserial;}
}
static void fluidParticle(float x,float y,float z,int mask,bool add,std::array<float,3> color,float life,float size,
                          float vx=0,float vy=0,float vz=0,float az=0,float alpha=1,float growth=0) {
    if(life<=0||size<=0)return;
    // Each sprite costs one quad; dense BFG bursts cannot grow without bound.
    if(fluidParticles.size()>=4096)return;
    fluidParticles.push_back({x,y,z,vx,vy,vz,az,size,growth,alpha,0,life,mask,add,color});
}
// GZDoom textured particles: half extent = size * (2.5 / 7).
// Fluid's helper fades over Life-1; a zero fade is used for shrinking sparks.
static void originalRocketParticle(float x,float y,float z,int mask,bool add,std::array<float,3> color,
    float life,float size,float vx=0,float vy=0,float vz=0,float az=0,float alpha=1,float growth=0,
    float ax=0,float ay=0,bool fade=true,float roll=0,float rollSpeed=0) {
    size_t count=fluidParticles.size();
    fluidParticle(x,y,z,mask,add,color,life,size,vx,vy,vz,az,alpha,growth);
    if(fluidParticles.size()==count)return;
    auto &p=fluidParticles.back();p.ax=ax;p.ay=ay;
    // HWSprite::ProcessParticle: square = 1/7; smooth/custom = 2.5/7.
    // Using the smooth extent for square fumes makes a broad wall of smoke.
    p.halfSize=mask==18?1.f/7.f:2.5f/7.f;
    p.fadeTics=fade?std::max(1.f,life-1):-1;p.roll=roll;p.rollSpeed=rollSpeed;
}
static int rocketRandom(int low,int high){return std::min(high,low+int(randomRange(0,float(high-low+1))));}
// Effects.zsc MRIntWeaps_SmokeParticle: a moving visual thinker, not a HUD ribbon.
void fluidFiringSmoke(player_t *player,int weapon,float ox,float oy,float power,float rotation,float offset){
    if(!settings.fluidFiringSmoke||!player||!player->mo)return;
    resetFluidEffects();auto *mo=player->mo;
    float yaw=float(mo->angle*(2.0*doomPi/4294967296.0)),c=std::cos(yaw),s=std::sin(yaw),cp=std::cos(tourpitch),sp=std::sin(tourpitch);
    int life=weapon==1?15:weapon==2?18:weapon==3?16:weapon==4?23:20;
    float alpha=weapon==4?randomRange(.8f,.9f):(weapon==1?.15f:weapon==3?.2f:.1f)+power*(weapon==8?.2f:.1f);
    float vertical=weapon==1?-1+offset:weapon==3?-1.6f:weapon==4?-1.6f-oy/.032f*.05f:-1.5f;
    for(int barrel=0;barrel<(weapon==8?2:1);++barrel){
        float side=ox+(weapon==8?(barrel?.25f:-.25f):weapon==3?(std::fmod(std::fmod(rotation,120.f)+120.f,120.f)-60)*.02f:0);
        float forward=5*cp+oy*2*sp-(weapon==4?-3.2f-oy/.032f*.05f:vertical*2)*sp;
        if(weapon==3)forward+=std::abs(side-ox)*sp;
        float x=units(mo->x)+c*forward+s*side,y=units(mo->y)+s*forward-c*side;
        float z=units(player->viewz)+5*sp-oy*cp+vertical*cp;
        if(weapon==3)z-=std::abs(side-ox)*.5f*cp;
        float scale=weapon==4?randomRange(.9f,1):weapon==8?.2f+power*.1f:randomRange(.2f,.3f);
        float grow=weapon==4?randomRange(2.1f,2.8f):weapon==3?randomRange(1.4f,3):weapon==8?randomRange(1.4f,2.4f):randomRange(weapon==1?.9f:1,2.4f);
        float gray=randomRange(weapon==4?40:weapon==2||weapon==8?140:170,weapon==4?50:weapon==2||weapon==8?150:180)/255.f;
        float blue=weapon==4?gray:std::min(1.f,gray*randomRange(weapon==2?1.1f:1,1.4f)),sideSpeed=randomRange(-.01f,.01f);
        size_t count=fluidParticles.size();
        // NSHSA0 is normally alpha blended: grey RGB modulates SColor, not
        // coverage. Folding grey into alpha made these puffs too transparent.
        fluidParticle(x,y,z,16,false,{gray*fluidSmokeTint,gray*fluidSmokeTint,blue*fluidSmokeTint},float(life),1.28f*scale,c*.2f*cp+s*sideSpeed,s*.2f*cp-c*sideSpeed,.2f*sp+(weapon==4?-.05f:weapon==3?.03f:weapon==1?.005f*cp:.005f),0,alpha,grow*.1f);
        if(fluidParticles.size()>count){auto &p=fluidParticles.back();p.fadeTics=float(life-(weapon==1?2:rocketRandom(1,3)));p.ownerRelative=true;p.ownerX=units(mo->x);p.ownerY=units(mo->y);p.ownerZ=units(player->viewz);}
    }
}
// Source flowing smoke uses world particles 10 units from the view, not
// screen-space ribbons. Movement leaves the old particles behind naturally.
void fluidBarrelSmoke(player_t *player,int stream,float ox,float oy,float scale,float alpha,float speed,float fire){
    struct Trail {float x=0,y=0,z=0;int tic=-100,weapon=-1;unsigned serial=~0u;};
    static Trail trails[6];static float windX=0,windY=0;static int windTic=-1;
    static float floor=0,bob=0,oldBob=0;static int bobTic=-1;static unsigned bobSerial=~0u;
    static float lastRoll[6]={};
    if(stream<0||stream>=6||!player||!player->mo||!settings.fluidHotSmoke)return;
    resetFluidEffects();
    auto &prev=trails[stream];auto *mo=player->mo;
    if(bobSerial!=r_levelserial){floor=units(mo->floorz);bob=oldBob=0;bobSerial=r_levelserial;bobTic=-1;}
    if(bobTic!=gametic){oldBob=bob;float delta=units(mo->floorz)-floor;floor+=delta*(std::abs(delta)>25?.5f:.05f);bob=units(player->viewz)-floor-units(player->viewheight);bobTic=gametic;}
    if(windTic!=gametic&&gametic%2==0){
        windTic=gametic;float limit=mo->subsector->sector->ceilingpic==skyflatnum?.4f:.1f;
        windX=std::clamp(windX+randomRange(-.0025f,.0025f),-limit,limit);
        windY=std::clamp(windY+randomRange(-.0025f,.0025f),-limit,limit);
    }
    float yaw=float(mo->angle*(2.0*doomPi/4294967296.0)),pitch=tourpitch;
    float c=std::cos(yaw),s=std::sin(yaw),cp=std::cos(pitch),sp=std::sin(pitch);
    float x=units(mo->x)+c*10*cp+s*ox*2+c*oy*2.7f*sp;
    float y=units(mo->y)+s*10*cp-c*ox*2+s*oy*2.7f*sp;
    float z=units(player->viewz)+oldBob-bob+sp*10-cp*oy*1.9f;
    float vx=windX*speed,vy=windY*speed,vz=.15f*speed;
    if(prev.serial!=r_levelserial||gametic-prev.tic>2||prev.weapon!=int(player->readyweapon)||std::hypot(x-prev.x,y-prev.y)>100)
        {prev.x=x;prev.y=y;prev.z=z;}
    float dx=prev.x-x,dy=prev.y-y,dz=prev.z-z,d=std::sqrt(dx*dx+dy*dy+dz*dz);
    float step=.25f*scale,connect=d<=step*1.5f&&std::abs(dz)<1?step:d;
    float attenuation=std::max(1.f,d*.15f/.6f);
    float actRoll=0,rollDelta=0;
    if(settings.fluidSmokeBio){float side=dx*s-dy*c,forward=dx*c+dy*s;
        actRoll=-std::atan2(-dz*cp+forward*sp,side*std::cos(pitch*2)*(1-std::min(1.f,forward*.05f/std::max(.1f,side))));
        rollDelta=std::remainder(actRoll-lastRoll[stream],float(2*doomPi));}
    int count=std::min(64,std::max(1,int(std::ceil(connect/step))));
    for(int i=0;i<count&&fluidParticles.size()<3072;++i){float t=d>0?std::min(d,i*step)/d:0;
        originalRocketParticle(x+dx*t,y+dy*t,z+dz*t,settings.fluidSmokeBio?17:18,false,{(130-fire)/255.f,(130-fire)/255.f,(130-fire)/255.f},25/attenuation,1.3f*scale,vx,vy,vz,0,alpha/attenuation,0,0,0,true,lastRoll[stream]+rollDelta*t);
    }
    // Short-lived connector follows the muzzle while the older trail rises.
    if(fire>60||mo->momx||mo->momy){
        auto color=fire>100?std::array<float,3>{1,(fire-100)*2/255.f,0}:std::array<float,3>{(130+fire)/255.f,(130-fire)/255.f,(130-fire)/255.f};
        int n=std::min(64,std::max(1,int(std::ceil(connect*.5f/step))));
        for(int i=0;i<n&&fluidParticles.size()<3072;++i){float distance=i*step,t=d>0?distance/d:0,curve=distance/(connect*.5f+step),velocity=1-distance/(connect*.5f);
            originalRocketParticle(x+dx*t*curve,y+dy*t*curve,z+dz*t*curve,18,false,color,1,scale,vx+units(mo->momx)*velocity,vy+units(mo->momy)*velocity,vz+(bob-oldBob)*velocity,0,alpha/attenuation,0,0,0,false,lastRoll[stream]+rollDelta*t);
            fluidParticles.back().fullBright=fire>60;
        }
    }
    lastRoll[stream]=actRoll;
    prev={x+vx,y+vy,z+vz,gametic,int(player->readyweapon),r_levelserial};
}
// Cosmetic surface bursts use a separate, small budget and no gameplay RNG.
// They remain available when the optional Fluid weapon replacement is disabled.
void rubbleImpact(int type,float x,float y,float z,float nx,float ny,float nz) {
    environmentBlast(x+nx*3,y+ny*3,z+nz*3,type==MT_ROCKET||type==MT_BFG?1.f:.25f);
    resetFluidEffects();
    if(std::getenv("DOOM_TEST_RUBBLE"))printf("RUBBLE hit type %d tic %d at %.1f %.1f %.1f normal %.2f %.2f %.2f\n",type,gametic,x,y,z,nx,ny,nz);
    bool rocket=type==MT_ROCKET||type==MT_TRACER;
    bool bfg=type==MT_BFG, plasma=type==MT_PLASMA||type==MT_ARACHPLAZ;
    bool green=type==MT_BRUISERSHOT, fat=type==MT_FATSHOT;
    if(!rocket&&!bfg&&!plasma&&!green&&!fat&&type!=MT_TROOPSHOT&&type!=MT_HEADSHOT)return;
    std::array<float,3> color=bfg||green?std::array<float,3>{.25f,1,.12f}:
        plasma?std::array<float,3>{.3f,.55f,1}:std::array<float,3>{1,.32f,.055f};
    addSurfaceGlow(type,x,y,z,nx,ny,nz,color);
    if(rocket||bfg){surfaceChip(x+nx*2,y+ny*2,z+nz*2,nx,ny,nz);}
    auto material=rubbleSurfaceColor(x,y,z);
    if(type==MT_ROCKET&&settings.rocketChunks){
        resetImpactLevel();
        int active=int(std::count_if(impactParticles.begin(),impactParticles.end(),[](const auto &p){return p.kind==4;}));
        for(int i=0;i<6&&active+i<48;++i){
            float size=randomRange(3.5f,7.f),speed=randomRange(70,150);
            float vx=randomRange(-65,65),vy=randomRange(-65,65),vz=randomRange(25,115);
            float dot=vx*nx+vy*ny+vz*nz;if(dot<0){vx-=dot*nx;vy-=dot*ny;vz-=dot*nz;}
            impactParticle(x+nx*(size+1),y+ny*(size+1),z+nz*(size+1),vx+nx*speed,vy+ny*speed,vz+nz*speed,randomRange(1.7f,2.8f),size,4,material);
        }
    }
    int count=bfg?12:rocket?12:fat?8:plasma?4:6;
    float speed=bfg?2.8f:rocket?2.2f:plasma?.9f:1.4f;
    auto emit=[&](int mask,bool add,std::array<float,3> c,float life,float size,float vx,float vy,float vz,float gravity,float growth){
        if(rubbleParticles.size()>=192)return;
        rubbleParticles.push_back({x+nx*2,y+ny*2,z+nz*2,vx,vy,vz,gravity,size,growth,add?.85f:.5f,0,life,mask,add,c});
    };
    for(int i=0;i<count;++i){
        float vx=randomRange(-1,1),vy=randomRange(-1,1),vz=randomRange(-1,1);
        // Reflect inward velocity: fragments initially leave the hit surface.
        float dot=vx*nx+vy*ny+vz*nz;
        if(dot<0){vx-=2*dot*nx;vy-=2*dot*ny;vz-=2*dot*nz;}
        // Crisp chips remain readable after the stock explosion has faded.
        bool stone=rocket;
        emit(i%5,!stone,stone?std::array<float,3>{.6f*material[0],.6f*material[1],.6f*material[2]}:color,
             plasma?24:42,plasma?randomRange(.8f,1.8f):randomRange(3,6),
             (vx+nx)*speed,(vy+ny)*speed,(vz+nz)*speed,-.08f,-.025f);
        if((bfg||green||fat)&&i%3==0)
            emit(12,true,color,40,9,(vx+nx)*speed,(vy+ny)*speed,(vz+nz)*speed,-.04f,-.1f);
    }
    emit(12,true,color,bfg?35:fat?35:plasma?18:12,bfg?38:fat?26:plasma?7:18,nx*.1f,ny*.1f,nz*.1f,0,-.35f);
    if(!plasma){
        for(int i=0;i<(fat?3:2);++i)
            emit(13,false,{.3f*material[0],.3f*material[1],.3f*material[2]},fat?48:38,rocket?24:16,nx*.35f+randomRange(-.3f,.3f),ny*.35f+randomRange(-.3f,.3f),nz*.35f+.4f,0,.5f);
    }
}
static void tickRubbleEffects(){
    for(auto &p:rubbleParticles){++p.age;p.x+=p.vx;p.y+=p.vy;p.z+=p.vz;p.vz+=p.az;p.vx*=.96f;p.vy*=.96f;p.size+=p.growth;}
    rubbleParticles.erase(std::remove_if(rubbleParticles.begin(),rubbleParticles.end(),[](const auto &p){return p.age>=p.life||p.size<=0;}),rubbleParticles.end());
}

static std::array<float,3> fluidColor(int type,float low=100,float high=250) {
    float c=randomRange(low,high)/255.f;
    return type==MT_ROCKET?std::array<float,3>{1,c,c*.5f}:type==MT_BFG?std::array<float,3>{c,1,c}:std::array<float,3>{c,c,1};
}
static void addFluidMark(int line,int side,int section,int sector,bool ceiling,float u,float z,int mask,float scale,
                         std::array<float,3> color={0,0,0},float start=56,float life=60) {
    FluidMark first={line,side,section,sector,mask,ceiling,randomRange(0,1)<.5f,u,z,scale,gameSeconds(),start,life,color};
    std::vector<FluidMark> pieces={first};
    // Unfold the texture along connected wall faces. Each piece retains the
    // same UV origin and is clipped to its own wall, never stretched over air.
    for(size_t n=0;n<pieces.size()&&pieces.size()<32;++n) {
        auto m=pieces[n];if(m.line<0)continue;
        WallSpan span;if(!wallSpan(lines[m.line],m.side,m.section,span))continue;
        float len=std::hypot(span.b.x-span.a.x,span.b.y-span.a.y);
        float hw=fluidMasks[mask].w*scale*.5f,hh=fluidMasks[mask].h*scale*.5f,zc=span.anchor+m.z;
        auto *front=sides[lines[m.line].sidenum[m.side]].sector;
        for(int end=0;end<2;++end) {
            if(end?m.u+hw<=len:m.u-hw>=0)continue;
            Point joint=end?span.b:span.a;
            for(int j=0;j<front->linecount&&pieces.size()<32;++j) {
                auto *next=front->lines[j];int index=int(next-lines);
                if(index==m.line)continue;
                for(int face=0;face<2;++face) {
                    if(next->sidenum[face]<0||sides[next->sidenum[face]].sector!=front)continue;
                    for(int part=0;part<3&&pieces.size()<32;++part) {
                        WallSpan target;if(!wallSpan(*next,face,part,target)||target.texture<=0)continue;
                        Point edge=end?target.a:target.b;
                        if(std::hypot(edge.x-joint.x,edge.y-joint.y)>.01f)continue;
                        if(zc+hh<=target.bottom||zc-hh>=target.top)continue;
                        bool seen=false;for(const auto &p:pieces)if(p.line==index&&p.side==face&&p.section==part)seen=true;
                        if(seen)continue;
                        float length=std::hypot(target.b.x-target.a.x,target.b.y-target.a.y);
                        if(length<.01f)continue;
                        auto copy=m;copy.line=index;copy.side=face;copy.section=part;
                        copy.u=end?m.u-len:m.u+length;copy.z=zc-target.anchor;
                        pieces.push_back(copy);
                    }
                }
            }
        }
    }
    for(const auto &piece:pieces) {
        if(fluidMarks.size()>=512)fluidMarks.erase(fluidMarks.begin());
        fluidMarks.push_back(piece);
    }
}
static void fluidBulletMark(int line,int side,int section,float along,float relativeZ) {
    if(!settings.fluidWeapons)return;
    resetFluidEffects();addFluidMark(line,side,section,-1,false,along,relativeZ,int(randomRange(0,4.99f)),.5f);
}
void fluidBulletFlat(int sector,bool ceiling,float x,float y) {
    if(!settings.fluidWeapons)return;
    resetFluidEffects();addFluidMark(-1,0,0,sector,ceiling,x,y,int(randomRange(0,4.99f)),.5f);
}
void fluidBFGHit(mobj_t *ball,mobj_t *victim) {
    if(!settings.fluidWeapons||!ball||!victim)return;resetFluidEffects();
    float x=units(victim->x),y=units(victim->y),z=units(victim->z),h=units(victim->height),r=units(victim->radius);
    fluidParticle(x,y,z+h*.5f,13,false,{1,1,1},25,r,0,0,1,0,.6f,1.4f);
    for(int i=0;i<4;++i)fluidParticle(x+randomRange(-r,r),y+randomRange(-r,r),z+randomRange(.1f,.4f)*h,12,true,fluidColor(MT_BFG),randomRange(47,55),randomRange(2,3),0,0,2,.005f,.7f,.05f);
    float bx=units(ball->x),by=units(ball->y),bz=units(ball->z)+5,dx=x-bx,dy=y-by,dz=z+h*.5f-bz,len=std::sqrt(dx*dx+dy*dy+dz*dz);
    for(float d=3;d<len;d+=3){float f=d/len;fluidParticle(bx+dx*f+randomRange(-1,1),by+dy*f+randomRange(-1,1),bz+dz*f,12,true,{.39f,1,.39f},20,2,0,0,0,0,.3f);}
}
extern "C" int p_diglastchanged,p_missile_hit_actor;
int rocketDoorImpact(mobj_t *mo) {
    // Shared solid collision bypasses Doom's ceilingline; trace the impacted face.
    if(settings.doorBlowout&&mo&&mo->type==MT_ROCKET&&mo->target&&mo->target->player){

        float vx=units(mo->momx),vy=units(mo->momy),vz=units(mo->momz),len=std::hypot(vx,vy);
        WallHit hit;
        if(!p_missile_hit_actor&&len>.01f&&firstSolidWall(units(mo->x),units(mo->y),units(mo->z)+units(mo->height)*.5f,vx/len,vy/len,vz/len,len+units(mo->radius)+8,hit))
            return P_RocketDoorImpact(mo,&lines[hit.line]);
    }
    return 0;
}
// Room haze is render-only and bounded; never spawn gameplay actors.
static void rocketRoomSmoke(mobj_t *mo){
    if(!settings.fluidIndoorSmoke||!settings.fluidWeapons||!mo||mo->type!=MT_ROCKET)return;
    auto *room=mo->subsector->sector;if(room->ceilingpic==skyflatnum)return;
    resetFluidEffects();
    float x=units(mo->x),y=units(mo->y),floor=units(room->floorheight),ceiling=units(room->ceilingheight);
    if(ceiling-floor<16)return;
    for(int i=0;i<9;++i){
        float cx=x+randomRange(-100,100),cy=y+randomRange(-100,100);
        if(R_PointInSubsector(fixed_t(cx*FRACUNIT),fixed_t(cy*FRACUNIT))->sector!=room)continue;
        float z=std::clamp(units(mo->z)+randomRange(16,64),floor+8,ceiling-8);
        bool merged=false;
        for(auto &p:indoorSmoke)if(std::hypot(p.x-cx,p.y-cy)<56&&std::abs(p.z-z)<40){
            p.alpha=std::min(.48f,p.alpha*std::max(0.f,1-p.age/p.life)+.10f);
            p.age=0;p.life=525;p.size=std::min(220.f,p.size+20);merged=true;break;
        }
        if(!merged&&indoorSmoke.size()<48)
            indoorSmoke.push_back({cx,cy,z,0,0,0,0,140,0,.12f,0,525,13,false,{.42f,.42f,.44f}});
    }
}
void fluidProjectileImpact(mobj_t *mo) {
    if(liquidProjectileImpact(mo))return;
    rocketRoomSmoke(mo);
    if(mo&&mo->type==MT_ROCKET&&p_diglastchanged>0){
        dropOldLevelImpacts();
        float dx=units(mo->momx),dy=units(mo->momy),len=std::max(.01f,std::hypot(dx,dy));
        float x=units(mo->x)-dx/len*3,y=units(mo->y)-dy/len*3,z=units(mo->z),now=gameSeconds();
        if(debris.size()>480)debris.erase(debris.begin(),debris.begin()+(debris.size()-480));
        for(int i=0;i<12;++i){float gray=randomRange(.28f,.63f);
            debris.push_back({x,y,z+randomRange(-8,12),-dx/len*randomRange(30,110)+randomRange(-45,45),
                -dy/len*randomRange(30,110)+randomRange(-45,45),randomRange(30,100),now,
                randomRange(.7f,1.6f),randomRange(1,3.5f),{gray,gray*.98f,gray*.94f}});
        }
    }
    if(!settings.fluidWeapons||!mo||!mo->target||!mo->target->player)return;
    if(mo->type!=MT_ROCKET&&mo->type!=MT_PLASMA&&mo->type!=MT_BFG)return;
    resetFluidEffects();
    float x=units(mo->x),y=units(mo->y),z=units(mo->z);
    if(fluidBursts.size()<128)fluidBursts.push_back({x,y,z,mo->type,0});
    int sector=int(mo->subsector->sector-sectors),line=-1,side=0,section=0;
    bool ceiling=z+units(mo->height)>=units(mo->ceilingz)-1;
    bool plane=ceiling||z<=units(mo->floorz)+1;
    float u=x,anchor=y;
    if(plane) {if((ceiling?sectors[sector].ceilingpic:sectors[sector].floorpic)==skyflatnum)return;}
    else {
        // Do not paint the wall behind a struck monster.
        for(auto *other=mo->subsector->sector->thinglist;other;other=other->snext)
            if(other!=mo&&other!=mo->target&&(other->flags&MF_SHOOTABLE)&&
               std::hypot(units(other->x)-x,units(other->y)-y)<units(other->radius+mo->radius)+4&&
               z<units(other->z+other->height)&&z+units(mo->height)>units(other->z))return;
        float vx=units(mo->momx),vy=units(mo->momy),vz=units(mo->momz),len=std::hypot(vx,vy);
        WallHit hit;
        if(len<.01f||!firstSolidWall(x,y,z+units(mo->height)*.5f,vx/len,vy/len,vz/len,len+units(mo->radius)+8,hit))return;
        WallSpan span;if(!wallSpan(lines[hit.line],hit.side,hit.section,span))return;
        line=hit.line;side=hit.side;section=hit.section;
        float dx=span.b.x-span.a.x,dy=span.b.y-span.a.y,l=std::hypot(dx,dy);
        u=std::clamp(((x+vx/len*hit.t-span.a.x)*dx+(y+vy/len*hit.t-span.a.y)*dy)/l,0.f,l);anchor=z+units(mo->height)*.5f+vz/len*hit.t-span.anchor;
    }
    auto mark=[&](int mask,float scale,std::array<float,3> color=std::array<float,3>{0,0,0},float start=56,float life=60){addFluidMark(line,side,section,sector,ceiling,u,anchor,mask,scale,color,start,life);};
    if(mo->type==MT_ROCKET)mark(5,.5f);
    else if(mo->type==MT_PLASMA){int mask=6+int(randomRange(0,1.99f));mark(mask,.55f);mark(mask,.55f,{208/255.f,196/255.f,249/255.f},.5f,1.5f);}
    else {mark(8+int(randomRange(0,1.99f)),1);mark(10+int(randomRange(0,1.99f)),1,{.5f,1,.5f},1,4);}
}
void fluidProjectileFlight(const mobj_t &mo) {
    if(!settings.fluidWeapons||!(mo.flags&MF_MISSILE)||!mo.target||!mo.target->player)return;
    int type=mo.type;if(type!=MT_ROCKET&&type!=MT_PLASMA&&type!=MT_BFG)return;
    resetFluidEffects();
    float x=units(mo.x),y=units(mo.y),z=units(mo.z),vx=units(mo.momx),vy=units(mo.momy),vz=units(mo.momz);
    float len=std::sqrt(vx*vx+vy*vy+vz*vz);if(len<.01f)return;
    if(type==MT_ROCKET) {
        // Original default flags disable flight smoke, but enable impact smoke.
        int mask=settings.fluidRocketFire?14:15;
        originalRocketParticle(x-vx/len*9,y-vy/len*9,z+5-vz/len*9,mask,false,{190/255.f,40/255.f,0},2,float(rocketRandom(24,35)),vx,vy,vz,0,.7f);
        float col=25*rocketRandom(1,6)/255.f;
        for(float i=0;i<1;i+=.25f)originalRocketParticle(x-vx*i,y-vy*i,z+5-vz*i,mask,true,{1,col,std::floor(col*255*.5f)/255},5,25,0,0,0,0,.6f,-5);
        if(leveltime%2==0){
            float ang=std::atan2(vy,vx),pit=std::atan2(vz,std::hypot(vx,vy));
            float power=float(rocketRandom(1,2)),rp=rocketRandom(-70,70)*doomPi/180;
            float side=ang+(rocketRandom(0,1)?1:-1)*doomPi*.5f;
            float sx=std::cos(side)*power+std::cos(ang+rp*4)*power*std::sin(pit);
            float sy=std::sin(side)*power+std::sin(ang+rp*4)*power*std::sin(pit);
            int c=25*rocketRandom(4,8);
            originalRocketParticle(x,y,z+5,mask,true,{1,c/255.f,(c/2)/255.f},float(rocketRandom(6,10)),randomRange(2,3),
                sx+vx*.5f,sy+vy*.5f,power*std::sin(rp)*std::cos(pit)+vz*.5f,-.2f,1,0,randomRange(-.1f,.1f),randomRange(-.1f,.1f));
        }
    }else if(type==MT_PLASMA) {
        for(float i=0;i<2;i+=.25f)fluidParticle(x-vx*i,y-vy*i,z+2-vz*i,12,true,fluidColor(type,25,150),3,24-i*8,vx,vy,vz,0,.4f,-8);
    }else {
        fluidParticle(x,y,z+15,12,true,fluidColor(type,25,100),10,90,0,0,0,0,.7f,-8);
        for(int i=0;i<5;++i){float a=leveltime*.018f+i*1.256637f,p=leveltime*.24f+i*2.1f;
            fluidParticle(x+std::cos(a)*std::cos(p)*30,y+std::sin(a)*std::cos(p)*30,z+12+std::sin(p)*30,12,true,{.47f,1,.43f},2,3,vx,vy,vz,0,.7f);}
    }
    if(type!=MT_ROCKET){float a=randomRange(0,2*doomPi),speed=type==MT_BFG?3.5f:2.5f;
        fluidParticle(x+randomRange(-4,4),y+randomRange(-4,4),z+(type==MT_BFG?15:5),12,true,fluidColor(type,50,250),randomRange(6,12),randomRange(2,5),vx+std::cos(a)*speed,vy+std::sin(a)*speed,vz+randomRange(-2,2),-.2f);}
}
// The original launcher supplies its own GunFlash overlays. Do not invent a
// second world-space trail at the muzzle (it produces an oversized fire disc).
void fluidRocketLaunch(mobj_t *) {}
void fluidIndoorSmoke(mobj_t*source,int weapon){
    if(!settings.fluidIndoorSmoke||!source||weapon==wp_fist||weapon==wp_chainsaw)return;
    resetFluidEffects();auto *sec=source->subsector->sector;if(sec->ceilingpic==skyflatnum)return;
    float x=units(source->x),y=units(source->y),z=std::max(units(sec->floorheight)+8,std::min(units(sec->ceilingheight)-30,units(source->z)+100));
    for(auto &p:indoorSmoke)if(std::hypot(p.x-x,p.y-y)<60&&std::abs(p.z-z)<24){p.age=0;p.alpha=std::min(.25f,p.alpha+.015f);return;}
    if(indoorSmoke.size()>=24)return;
    indoorSmoke.push_back({x,y,z,0,0,0,0,100,0,.04f,0,350,13,false,{.4f,.4f,.43f}});
}
void tickFluidEffects() {
    resetFluidEffects();tickRubbleEffects();
    if(!settings.fluidIndoorSmoke||!settings.fluidWeapons)indoorSmoke.clear();
    for(auto &p:indoorSmoke){
        ++p.age;
        auto *room=R_PointInSubsector(fixed_t(p.x*FRACUNIT),fixed_t(p.y*FRACUNIT))->sector;
        p.z=std::min(p.z+.06f,units(room->ceilingheight)-8);
        p.size=std::min(240.f,p.size+.10f);
    }
    indoorSmoke.erase(std::remove_if(indoorSmoke.begin(),indoorSmoke.end(),[](const auto&p){return p.age>=p.life;}),indoorSmoke.end());
    if(!settings.fluidWeapons){fluidParticles.clear();fluidBursts.clear();fluidMarks.clear();return;}
    for(auto &p:fluidParticles){
        if(p.ownerRelative&&players[displayplayer].mo){auto &owner=players[displayplayer];float x=units(owner.mo->x),y=units(owner.mo->y),z=units(owner.viewz);p.x+=(x-p.ownerX)*.9f;p.y+=(y-p.ownerY)*.9f;p.z+=z-p.ownerZ;p.ownerX=x;p.ownerY=y;p.ownerZ=z;}
        p.age++;p.x+=p.vx;p.y+=p.vy;p.z+=p.vz;p.vx+=p.ax;p.vy+=p.ay;p.vz+=p.az;p.size+=p.growth;p.roll+=p.rollSpeed;}
    fluidParticles.erase(std::remove_if(fluidParticles.begin(),fluidParticles.end(),[](const auto &p){return p.age>=p.life||p.size<=0;}),fluidParticles.end());
    for(auto &b:fluidBursts){int t=++b.tic;float x=b.x,y=b.y,z=b.z;
        if(b.type==MT_ROCKET){
            int mask=settings.fluidRocketFire?14:15;
            auto *viewer=players[displayplayer].mo;
            float dx=viewer?units(viewer->x)-x:0,dy=viewer?units(viewer->y)-y:0,dz=viewer?units(viewer->z)-z:0;
            float dist=std::max(1.f,1+(std::sqrt(dx*dx+dy*dy+dz*dz)-80)*.0016f);
            // Player.zsc default enables one rotating smoke particle on tics 1..6.
            if(t<7){float angle=float(rocketRandom(-30,30)),a=std::atan2(dy,dx)+angle*2*doomPi/180;
                originalRocketParticle(x,y,z,13,false,{30/255.f,30/255.f,30/255.f},float(rocketRandom(20,30)),float(rocketRandom(32,35)),
                    std::cos(a)*3,std::sin(a)*3,randomRange(-.5f,2),0,randomRange(.6f,.7f),randomRange(4.4f,5),0,0,true,float(rocketRandom(0,360))*doomPi/180,-angle*.1f*doomPi/180);}
            if(t<7&&t%2==0){for(int i=rocketRandom(0,10);i<40;++i){
                float v=randomRange(1,4),a=rocketRandom(0,360)*doomPi/180,size=randomRange(1,3)*dist,life=float(int(40-v));
                int c=25*rocketRandom(4,10);
                originalRocketParticle(x,y,z,mask,true,{1,c/255.f,(c/2)/255.f},life,size,std::cos(a)*v*dist,std::sin(a)*v*dist,
                    std::sin(rocketRandom(-20,40)*doomPi/180)*v*2*dist,-randomRange(.005f,.7f)*dist,1,-size/life,0,0,false);}
                if(t==2){int g=rocketRandom(30,70);originalRocketParticle(x,y,z+5,mask,true,{1,g/255.f,rocketRandom(10,g/2)/255.f},8,20,0,0,0,0,1,float(rocketRandom(25,30)));}}
        }else {
            if((b.type==MT_PLASMA&&t==1)||(b.type==MT_BFG&&t<10)){
                int count=b.type==MT_PLASMA?6:std::max(0,40-(t-4)*6);
                for(int i=0;i<count;++i){float a=randomRange(0,2*doomPi),v=b.type==MT_PLASMA?2:6.4f-(t-4),pit=randomRange(-1.57f,1.57f);
                    float life=b.type==MT_PLASMA?randomRange(13,19):std::max(1,(t-2)*6),size=b.type==MT_PLASMA?randomRange(1.5f,3.5f):randomRange(8,10)-t*.8f;
                    fluidParticle(x,y,z+5,12,true,fluidColor(b.type),life,size,std::cos(a)*std::cos(pit)*v,std::sin(a)*std::cos(pit)*v,std::sin(pit)*v,b.type==MT_PLASMA?-.4f:0,1,b.type==MT_PLASMA?-size/life:0);}
            }
            if(b.type==MT_PLASMA&&t<6&&t%2){for(int i=0;i<2;++i)fluidParticle(x+randomRange(-5,5),y+randomRange(-5,5),z+randomRange(-5,0),12,true,fluidColor(b.type),randomRange(17,24),randomRange(3.5f,4.5f),0,0,0,.05f);}
            if(b.type==MT_BFG&&t<21){
                float a=randomRange(0,2*doomPi),pit=randomRange(-1.57f,1.57f),px=x,py=y,pz=z;
                for(int i=0;i<(b.type==MT_BFG?30:20);++i){px+=std::cos(a);py+=std::sin(a);pz+=std::sin(pit);
                    auto c=b.type==MT_BFG?std::array<float,3>{1,1,randomRange(.2f,1)}:fluidColor(b.type);
                    fluidParticle(px,py,pz,12,true,c,8+t,3);a+=randomRange(-.7f,.7f);pit+=randomRange(-.4f,.4f);}
            }
            if(t==10)fluidParticle(x,y,z,13,false,{1,1,1},randomRange(16,22),randomRange(27,32),0,0,.5f,0,.45f,1.8f);
        }
    }
    fluidBursts.erase(std::remove_if(fluidBursts.begin(),fluidBursts.end(),[](const auto &b){return b.tic>=(b.type==MT_ROCKET?7:21);}),fluidBursts.end());
}
static void drawFluidEffects(const Uniforms &camera) {
    resetFluidEffects();for(auto &b:effectBatches)b.second.clear();for(auto &b:effectAddBatches)b.second.clear();
    float now=gameSeconds();
    fluidMarks.erase(std::remove_if(fluidMarks.begin(),fluidMarks.end(),[&](const auto &m){return now-m.born>=m.life||now<m.born;}),fluidMarks.end());
    for(auto &m:fluidMarks){int lump=fluidImage(m.mask);const auto &im=fluidMasks[m.mask];
        float alpha=std::clamp((m.life-(now-m.born))/(m.life-m.fadeStart),0.f,1.f)*(m.mask<5?.85f:1.f);
        auto &out=decalBatches[lump];float hw=im.w*m.size*.5f,hh=im.h*m.size*.5f;
        if(m.line>=0){float excavation[4];if(P_GlobalDigEnabled()&&P_GlobalDigBounds(m.line,m.side,excavation))continue;WallSpan span;if(!wallSpan(lines[m.line],m.side,m.section,span))continue;
            float dx=span.b.x-span.a.x,dy=span.b.y-span.a.y,len=std::hypot(dx,dy),zc=span.anchor+m.z;
            float u0=std::max(0.f,m.u-hw),u1=std::min(len,m.u+hw),z0=std::max(span.bottom,zc-hh),z1=std::min(span.top,zc+hh);
            if(u0>=u1||z0>=z1)continue;
            // Retire the flat scorch wherever its supporting skin has gone.
            // Testing the footprint also catches a hole beside its centre.
            if(const auto* world=SV_MapWorld();world&&m.supportRevision!=world->revision()){
                m.supportRevision=world->revision();
                bool removed=false;
                for(float u=u0;u<=u1&&!removed;u+=std::max(1.f,(u1-u0)/8))
                    for(float z=z0;z<=z1;z+=std::max(1.f,(z1-z0)/8))
                        if(world->removal({span.a.x+dx/len*u,span.a.y+dy/len*u,z})>0){removed=true;break;}
                if(removed){m.life=0;continue;}
            }
            auto v=[&](float u,float z){float tu=(u-m.u+hw)/m.size;Vertex a={span.a.x+dx/len*u+dy/len*.1f,span.a.y+dy/len*u-dx/len*.1f,z,m.flip?im.w-tu:tu,(zc+hh-z)/m.size,alpha,0};a.red=m.color[0];a.green=m.color[1];a.blue=m.color[2];return a;};
            quad(out,v(u0,z1),v(u1,z1),v(u1,z0),v(u0,z0));
        }else{float excavation[4];if(P_GlobalDigFlatBounds(m.sector,m.ceiling,excavation)&&m.u+m.size*64>=excavation[0]&&m.u-m.size*64<=excavation[1]&&m.z+m.size*64>=excavation[2]&&m.z-m.size*64<=excavation[3])continue;const auto &sec=sectors[m.sector];float z=m.ceiling?ceilZ(&sec)-.1f:floorZ(&sec)+.1f;
            if((m.ceiling?sec.ceilingpic:sec.floorpic)==skyflatnum)continue;
            if(const auto* world=SV_MapWorld();world&&m.supportRevision!=world->revision()){
                m.supportRevision=world->revision();
                bool removed=false;
                float plane=m.ceiling?ceilZ(&sec):floorZ(&sec);
                for(float x=m.u-hw;x<=m.u+hw&&!removed;x+=std::max(1.f,hw/4))
                    for(float y=m.z-hh;y<=m.z+hh;y+=std::max(1.f,hh/4))
                        if(world->removal({x,y,plane})>0){removed=true;break;}
                if(removed){m.life=0;continue;}
            }
            std::vector<Point> square={{m.u-hw,m.z-hh},{m.u+hw,m.z-hh},{m.u+hw,m.z+hh},{m.u-hw,m.z+hh}};
            for(int leaf:sectorFloors[m.sector]){auto polygon=floors[leaf];for(int k=0;k<4&&!polygon.empty();++k){auto a=square[k],b=square[(k+1)%4];polygon=clip(polygon,a,{b.x-a.x,b.y-a.y},false);}
                auto v=[&](Point p){float u=(p.x-m.u+hw)/m.size;Vertex a={p.x,p.y,z,m.flip?im.w-u:u,(m.z+hh-p.y)/m.size,alpha,0};a.red=m.color[0];a.green=m.color[1];a.blue=m.color[2];return a;};
                for(size_t k=1;k+1<polygon.size();++k)for(Point p:{polygon[0],polygon[k],polygon[k+1]})out.push_back(v(p));}
        }
    }
    std::vector<const FluidParticle*> sorted;for(const auto&p:indoorSmoke)sorted.push_back(&p);for(const auto &p:fluidParticles)sorted.push_back(&p);for(const auto &p:rubbleParticles)sorted.push_back(&p);
    auto depth=[&](const auto *p){return (p->x-camera.eye[0])*camera.forward[0]+(p->y-camera.eye[1])*camera.forward[1]+(p->z-camera.eye[2])*camera.forward[2];};
    std::sort(sorted.begin(),sorted.end(),[&](auto a,auto b){return depth(a)>depth(b);});
    for(auto ptr:sorted){const auto &p=*ptr;if(depth(ptr)<1)continue;int lump=fluidImage(p.mask);const auto &im=fluidMasks[p.mask];
        float f=renderFraction-1,x=p.x+p.vx*f,y=p.y+p.vy*f,z=p.z+p.vz*f,h=std::max(.1f,p.size+p.growth*f)*p.halfSize,alpha=p.alpha*(p.fadeTics<0?1:std::clamp(1-(p.age+f)/(p.fadeTics>0?p.fadeTics:p.life),0.f,1.f));
        auto v=[&](float u,float a,float tu,float tv){float r=p.roll+p.rollSpeed*f,c=std::cos(r),s=std::sin(r),old=u;u=c*u-s*a;a=s*old+c*a;Vertex b={x+camera.right[0]*u+camera.up[0]*a,y+camera.right[1]*u+camera.up[1]*a,z+camera.right[2]*u+camera.up[2]*a,tu,tv,alpha,(p.mask==14||p.fullBright)?4096u:0u};b.red=p.color[0];b.green=p.color[1];b.blue=p.color[2];return b;};
        auto &out=p.add?effectAddBatches[lump]:effectBatches[lump];quad(out,v(-h,h,0,0),v(h,h,im.w,0),v(h,-h,im.w,im.h),v(-h,-h,0,im.h));
    }
}
