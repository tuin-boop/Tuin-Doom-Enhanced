// Cosmetic liquid bursts. Physics runs once per tic, not per rendered frame.
struct LiquidDrop {float x,y,z,vx,vy,vz,size,born;int sector;std::array<float,3> color;bool projectile=true;};
struct LiquidRun {int line,side,part,sector;float along,dz,born,width,length;std::array<float,3> color;bool projectile=true;};
struct LiquidBlast {const mobj_t *actor;int type;float x,y,z,born;};
static std::vector<LiquidDrop> liquidDrops;
static std::vector<LiquidRun> liquidRuns;
static std::vector<LiquidBlast> liquidBlasts;
struct Runoff {float x,y,z,vx,vy,born;int sector;float sx=0,sy=0;std::vector<std::array<float,3>> path;bool landing=false;};
static std::vector<Runoff> runoffs;
static unsigned liquidBlastLevel=~0u;
static int liquidBlastTic=-1;
static void resetLiquidBursts() {
    if(liquidBlastLevel!=r_levelserial||!settings.splashes||(!settings.liquidProjectileImpacts&&!(settings.environmentEffects&&(settings.debrisLiquids||settings.liquidRunoff)))) {
        liquidDrops.clear();liquidRuns.clear();liquidBlasts.clear();runoffs.clear();
        spouts.erase(std::remove_if(spouts.begin(),spouts.end(),[](const Spout &s){return s.power!=1;}),spouts.end());
        liquidBlastLevel=r_levelserial;liquidBlastTic=-1;
    }
}
bool liquidProjectileHidden(const mobj_t *mo) {
    if(!mo||!mo->info||!(mo->info->flags&MF_MISSILE)||!settings.splashes||!settings.liquidProjectileImpacts||liquidBlastLevel!=r_levelserial||(mo->flags&MF_MISSILE))return false;
    for(const auto &b:liquidBlasts)if(b.actor==mo&&b.type==mo->type&&gameSeconds()-b.born<2.5f&&
        std::abs(units(mo->x)-b.x)<1&&std::abs(units(mo->y)-b.y)<1&&std::abs(units(mo->z)-b.z)<1)return true;
    return false;
}
bool liquidProjectileImpact(mobj_t *mo) {
    resetLiquidBursts();
    if(!settings.splashes||!settings.liquidProjectileImpacts||!mo||!(mo->flags&MF_MISSILE)||!mo->subsector||sectorMist.size()!=(size_t)numsectors)return false;
    const sector_t *sector=mo->subsector->sector;int index=int(sector-sectors);
    float floor=units(sector->floorheight),x=units(mo->x),y=units(mo->y),z=units(mo->z);
    if(!sectorMist[index].liquid||sector->floorpic==skyflatnum||z>floor+2||z<floor-4||mo->momz>0)return false;
    if(splashLevelSerial!=r_levelserial){splashWatch.clear();rings.clear();spouts.clear();plunges.clear();splashLevelSerial=r_levelserial;}
    float power=mo->type==MT_ROCKET?1.f:mo->type==MT_BFG?.75f:mo->type==MT_PLASMA||mo->type==MT_ARACHPLAZ?.28f:.42f;
    float now=gameSeconds();
    if(liquidBlasts.size()>=64)liquidBlasts.erase(liquidBlasts.begin());
    liquidBlasts.push_back({mo,(int)mo->type,x,y,z,now});
    ripple(x,y,index,24+power*80,true);spout(x,y,index,0.6f+power*2.2f);
    Image &flat=lumpImage(firstflat+flattranslation[sector->floorpic],true);
    const auto color=averageColor(flat);
    int count=int(12+60*power);
    while(liquidDrops.size()+count>384)liquidDrops.erase(liquidDrops.begin());
    for(int i=0;i<count;++i) {
        float a=randomRange(0,2*doomPi),v=randomRange(25,140)*(.45f+power*.75f);
        float up=randomRange(80,260)*(.45f+power*.75f);
        if(i%3==0){v*=.35f;up*=1.15f;} // Center of the rising plume.
        auto tint=color;float tintScale=randomRange(.85f,1.25f);for(float &c:tint)c=std::min(1.f,c*tintScale);
        liquidDrops.push_back({x,y,floor+2,std::cos(a)*v,std::sin(a)*v,up,randomRange(.6f,2.f)*(.6f+power),now,index,tint});
    }
    I_MadeSound(sectorMist[index].thick?IMPACT_SPLASH_THICK:IMPACT_SPLASH_WATER,x,y,floor);
    return true;
}
void debrisLiquidSplash(float x,float y,float z,int sector,float size){
    if(!settings.environmentEffects||!settings.debrisLiquids||!settings.splashes)return;
    resetLiquidBursts();static int last=-1,count=0;if(last!=leveltime){last=leveltime;count=0;}if(count++>=8)return;
    ripple(x,y,sector,std::clamp(size*8,6.f,24.f),true);
    auto color=averageColor(lumpImage(firstflat+flattranslation[sectors[sector].floorpic],true));
    for(int i=0;i<4&&liquidDrops.size()<384;++i)liquidDrops.push_back({x,y,z+1,randomRange(-24,24),randomRange(-24,24),randomRange(20,55),.6f,gameSeconds(),sector,color,false});
}
// Cosmetic flow paths only. No pool plane, map geometry or collision changes.
// Build the short gravity arc once per update, clipping against real rubble.
static void runoffSurface(Runoff &r){
    r.path.clear();r.landing=false;
    float x=r.x,y=r.y,z=r.z;r.path.push_back({x,y,z});
    for(int n=1;n<=20;++n){
        float t=n*.035f,nx=r.x+r.vx*t,ny=r.y+r.vy*t,nz=r.z-8*t-120*t*t;
        float hit[6];int owner;
        float fraction=SV_Trace(x,y,z,nx-x,ny-y,nz-z,hit,&owner);
        if(fraction<1){
            if(fraction>.01f)r.path.push_back({hit[0],hit[1],hit[2]+.12f});
            r.landing=hit[5]>.5f;break;
        }
        r.path.push_back({nx,ny,nz});x=nx;y=ny;z=nz;
    }
}
// Particle-only mode bits: smoothly shaded flowing liquid and landing rings.
static constexpr unsigned liquidFlowMode=8192,liquidRingMode=16384;
static Vertex liquidEffectVertex(float x,float y,float z,float u,float v,float alpha,
                                  unsigned mode,const std::array<float,3> &color,float phase){
    Vertex out={x,y,z,u,v,alpha,mode};out.red=color[0];out.green=color[1];out.blue=color[2];out.sunU=phase;return out;
}
static void buildRunoffEffects(const Uniforms &camera){
    if(!settings.environmentEffects||!settings.liquidRunoff||!settings.splashes)return;
    for(const auto &r:runoffs){
        if(r.path.size()<2)continue;
        Image &flat=lumpImage(firstflat+flattranslation[sectors[r.sector].floorpic],true);
        auto color=averageColor(flat);float light=liquidLight(r.sector,flat,r.x,r.y,r.z,camera);
        for(auto &c:color)c*=light;
        float phase=gameSeconds()*2.2f+r.x*.037f+r.y*.023f,along=0;
        for(size_t i=1;i<r.path.size();++i){
            const auto &a=r.path[i-1],&b=r.path[i];
            float dx=b[0]-a[0],dy=b[1]-a[1],dz=b[2]-a[2],length=std::sqrt(dx*dx+dy*dy+dz*dz);
            // Cross the flow with the eye vector: a narrow ribbon from every angle.
            float ex=camera.eye[0]-a[0],ey=camera.eye[1]-a[1],ez=camera.eye[2]-a[2];
            float rx=dy*ez-dz*ey,ry=dz*ex-dx*ez,rz=dx*ey-dy*ex,n=std::sqrt(rx*rx+ry*ry+rz*rz);
            if(n<.001f)continue;
            float width=.65f*(1-.45f*float(i)/r.path.size());rx*=width/n;ry*=width/n;rz*=width/n;
            float fade=std::min(1.f,float(r.path.size()-i)/3.f);
            auto v=[&](const auto &p,float side,float uv,float distance){return liquidEffectVertex(p[0]+rx*side,p[1]+ry*side,p[2]+rz*side,uv,distance*.2f,.78f*fade,liquidFlowMode,color,phase);};
            quad(particleVertices,v(a,-1,-1,along),v(a,1,1,along),v(b,1,1,along+length),v(b,-1,-1,along+length));
            along+=length;
        }
        if(r.landing){
            auto p=r.path.back();float size=2.8f;
            auto v=[&](float x,float y){return liquidEffectVertex(p[0]+x*size,p[1]+y*size,p[2]+.12f,x,y,.38f,liquidRingMode,color,phase);};
            quad(particleVertices,v(-1,-1),v(1,-1),v(1,1),v(-1,1));
        }
    }
}
static bool runoffOutlet(int pool,float sx,float sy,float ex,float ey){
    if(runoffs.size()>=12)return false;
    float floor=units(sectors[pool].floorheight),dx=ex-sx,dy=ey-sy,len=std::hypot(dx,dy);
    if(len<1||!SV_Edited(ex,ey,floor-4))return false;
    float point[6];int owner;
    // The source must still contain liquid on an intact floor. The receiver
    // is actual excavated geometry, which may lie below its sector floor.
    if(SV_Trace(sx,sy,floor+1,0,0,-4,point,&owner)>=1||std::abs(point[2]-floor)>2)return false;
    if(SV_Trace(sx,sy,floor+1,dx,dy,0,nullptr,nullptr)<1)return false;
    float drop=SV_Trace(ex,ey,floor+1,0,0,-128,point,&owner);
    if(drop>=1||point[2]>floor-8)return false;
    if(std::any_of(runoffs.begin(),runoffs.end(),[&](const auto &r){return std::hypot(r.x-ex,r.y-ey)<16;}))return false;
    runoffs.push_back({ex,ey,floor+1,dx/len*32,dy/len*32,gameSeconds(),pool,sx,sy});runoffSurface(runoffs.back());return true;
}
void discoverRunoff(float x,float y,float z){
    if(!settings.environmentEffects||!settings.liquidRunoff||!settings.splashes||!SV_MapMode()||sectorMist.size()!=size_t(numsectors))return;
    resetLiquidBursts();const sector_t *at=R_PointInSubsector(fixed_t(x*FRACUNIT),fixed_t(y*FRACUNIT))->sector;
    for(int i=0;i<at->linecount&&runoffs.size()<12;++i){
        const line_t &l=*at->lines[i];if(!l.frontsector||!l.backsector)continue;
        const sector_t *pool=sectorMist[l.frontsector-sectors].liquid?l.frontsector:sectorMist[l.backsector-sectors].liquid?l.backsector:nullptr;
        if(!pool)continue;
        float floor=units(pool->floorheight);if(std::abs(z-floor)>80)continue;
        float ax=units(l.v1->x),ay=units(l.v1->y),dx=units(l.v2->x-l.v1->x),dy=units(l.v2->y-l.v1->y),len=std::hypot(dx,dy);
        if(len<1)continue;float u=std::clamp(((x-ax)*dx+(y-ay)*dy)/(len*len),0.f,1.f),px=ax+dx*u,py=ay+dy*u;
        if(std::hypot(px-x,py-y)>80)continue;
        float nx=(pool==l.frontsector?-dy:dy)/len,ny=(pool==l.frontsector?dx:-dx)/len;
        // Search several depths into the receiving crater, rather than
        // requiring an authored floor-height drop at exactly four units.
        for(float reach:{4.f,8.f,16.f,24.f})if(runoffOutlet(int(pool-sectors),px-nx*2,py-ny*2,px+nx*reach,py+ny*reach))break;
    }
}
static void rediscoverRunoff(){
    struct Probe {int pool;float sx,sy,ex,ey,distance;};
    static std::vector<Probe> probes;static unsigned serial=~0u;static int cursor=0,last=-99999;
    if(!settings.environmentEffects||!settings.liquidRunoff||!settings.splashes||!players[consoleplayer].mo||!SV_MapMode()||levelSerial!=r_levelserial||sectorMist.size()!=size_t(numsectors))return;
    auto *player=players[consoleplayer].mo;float px=units(player->x),py=units(player->y);
    if(serial!=r_levelserial||leveltime<last||leveltime-last>3*TICRATE){
        probes.clear();cursor=0;serial=r_levelserial;last=leveltime;
        for(int i=0;i<numlines;++i){const auto &l=lines[i];
            for(int side=0;side<2;++side){const sector_t *pool=side?l.backsector:l.frontsector;
                if(!pool||!sectorMist[pool-sectors].liquid)continue;
                float ax=units(l.v1->x),ay=units(l.v1->y),dx=units(l.v2->x-l.v1->x),dy=units(l.v2->y-l.v1->y),len=std::hypot(dx,dy);if(len<1)continue;
                float nx=(side?dy:-dy)/len,ny=(side?-dx:dx)/len;
                int steps=std::max(1,int(std::ceil(len/16)));
                for(int n=0;n<=steps;++n){float x=ax+dx*n/steps,y=ay+dy*n/steps,d=std::hypot(x-px,y-py);if(d>384)continue;
                    for(float reach:{4.f,12.f,24.f})probes.push_back({int(pool-sectors),x-nx*2,y-ny*2,x+nx*reach,y+ny*reach,d});
                }
            }
        }
        // Breaches can be inside a pool floor, not only across a linedef.
        for(int gy=-12;gy<=12;++gy)for(int gx=-12;gx<=12;++gx){
            float x=std::floor(px/16)*16+gx*16,y=std::floor(py/16)*16+gy*16;
            const auto *pool=R_PointInSubsector(fixed_t(x*FRACUNIT),fixed_t(y*FRACUNIT))->sector;
            int index=int(pool-sectors);float floor=units(pool->floorheight);
            if(!sectorMist[index].liquid||!SV_Edited(x,y,floor-4))continue;
            for(auto offset:{Point{16,0},Point{-16,0},Point{0,16},Point{0,-16}})
                probes.push_back({index,x+offset.x,y+offset.y,x,y,std::hypot(x-px,y-py)});
        }
        std::stable_sort(probes.begin(),probes.end(),[](const auto &a,const auto &b){return a.distance<b.distance;});
        if(probes.size()>768)probes.resize(768);
    }
    for(int n=0;n<4&&cursor<int(probes.size())&&runoffs.size()<12;++n){auto &p=probes[cursor++];runoffOutlet(p.pool,p.sx,p.sy,p.ex,p.ey);}
}
static void tickLiquidBursts() {
    resetLiquidBursts();if(liquidBlastTic==leveltime)return;liquidBlastTic=leveltime;
    rediscoverRunoff();
    float now=gameSeconds(),dt=1.f/TICRATE;
    if(!settings.liquidProjectileImpacts){liquidBlasts.clear();liquidDrops.erase(std::remove_if(liquidDrops.begin(),liquidDrops.end(),[](const auto &d){return d.projectile;}),liquidDrops.end());}
    if(!settings.environmentEffects){liquidDrops.erase(std::remove_if(liquidDrops.begin(),liquidDrops.end(),[](const auto &d){return !d.projectile;}),liquidDrops.end());}
    liquidRuns.erase(std::remove_if(liquidRuns.begin(),liquidRuns.end(),[](const auto &d){return d.projectile?!settings.liquidProjectileImpacts:!settings.environmentEffects;}),liquidRuns.end());
    if(!settings.environmentEffects||!settings.liquidRunoff||!settings.splashes)runoffs.clear();
    runoffs.erase(std::remove_if(runoffs.begin(),runoffs.end(),[&](const auto &r){return now<r.born||std::abs(units(sectors[r.sector].floorheight)+1-r.z)>1;}),runoffs.end());
    if(leveltime%3==0&&!runoffs.empty())runoffSurface(runoffs[(leveltime/3)%runoffs.size()]);
    if(leveltime%3==0)for(const auto &r:runoffs){
        if(liquidDrops.size()>=384)break;
        if(SV_Trace(r.x,r.y,r.z,r.vx*.1f,r.vy*.1f,-2,nullptr,nullptr)<1)continue;
        auto color=averageColor(lumpImage(firstflat+flattranslation[sectors[r.sector].floorpic],true));
        liquidDrops.push_back({r.x+randomRange(-1,1),r.y+randomRange(-1,1),r.z,r.vx,r.vy,-8,.5f,now,r.sector,color,false});
    }
    liquidBlasts.erase(std::remove_if(liquidBlasts.begin(),liquidBlasts.end(),[&](const auto &b){return now<b.born||now-b.born>2.5f;}),liquidBlasts.end());
    liquidRuns.erase(std::remove_if(liquidRuns.begin(),liquidRuns.end(),[&](const auto &s){return now<s.born||now-s.born>8;}),liquidRuns.end());
    for(auto &d:liquidDrops) {
        if(now-d.born>3){d.size=0;continue;}
        float dx=d.vx*dt,dy=d.vy*dt,dz=d.vz*dt,best=1;
        WallHit hit;WallSpan wall;
        const sector_t *at=R_PointInSubsector(fixed_t(d.x*FRACUNIT),fixed_t(d.y*FRACUNIT))->sector;
        // Only nearby sector boundaries, rather than scanning the entire map
        // for every flying drop. Open portals let the drop continue next tic.
        for(int n=0;n<at->linecount;++n) {
            const line_t &l=*at->lines[n];float ax=units(l.v1->x),ay=units(l.v1->y),ex=units(l.v2->x)-ax,ey=units(l.v2->y)-ay;
            float det=dx*ey-dy*ex;if(std::abs(det)<1e-6f)continue;
            float ox=ax-d.x,oy=ay-d.y,t=(ox*ey-oy*ex)/det,u=(ox*dy-oy*dx)/det;
            if(t<0||t>=best||u<0||u>1)continue;
            int side=ex*(d.y-ay)-ey*(d.x-ax)>=0?1:0;
            for(int part=0;part<3;++part) {
                WallSpan span;if(!wallSpan(l,side,part,span)||d.z+dz*t<span.bottom||d.z+dz*t>span.top)continue;
                float point[6];int owner;
                if(SV_MapMode()&&SV_Trace(d.x,d.y,d.z,dx,dy,dz,point,&owner)>=1)continue;
                best=t;hit={int(&l-lines),side,part,t};wall=span;break;
            }
        }
        d.x+=dx*best;d.y+=dy*best;d.z+=dz*best;d.vz-=240*dt;
        if(hit.line>=0) {
            float ex=wall.b.x-wall.a.x,ey=wall.b.y-wall.a.y,len=std::hypot(ex,ey);
            if(liquidRuns.size()>=96)liquidRuns.erase(liquidRuns.begin());
            liquidRuns.push_back({hit.line,hit.side,hit.section,d.sector,((d.x-wall.a.x)*ex+(d.y-wall.a.y)*ey)/std::max(.01f,len),d.z-wall.anchor,now,d.size,randomRange(10,35),d.color,d.projectile});
            d.size=0;continue;
        }
        at=R_PointInSubsector(fixed_t(d.x*FRACUNIT),fixed_t(d.y*FRACUNIT))->sector;
        if(d.z<=units(at->floorheight)+.3f){
            if(!SV_Active())d.size=0;
            else {float landing[6];int owner;
                if(SV_Trace(d.x,d.y,d.z+2,0,0,-3,landing,&owner)<1)d.size=0;}
        }
        if(at->ceilingpic!=skyflatnum&&d.z>=units(at->ceilingheight)-1){d.z=units(at->ceilingheight)-1;d.vz=-std::abs(d.vz)*.2f;}
    }
    liquidDrops.erase(std::remove_if(liquidDrops.begin(),liquidDrops.end(),[](const auto &d){return d.size<=0;}),liquidDrops.end());
}
static void buildLiquidBursts(const Uniforms &camera) {
    resetLiquidBursts();float now=gameSeconds();
    buildRunoffEffects(camera);
    for(const auto &d:liquidDrops) {
        Image &flat=lumpImage(firstflat+flattranslation[sectors[d.sector].floorpic],true);
        float light=liquidLight(d.sector,flat,d.x,d.y,d.z,camera),fade=std::min(1.f,(3-(now-d.born))*2);
        auto color=d.color;for(float &c:color)c*=light;
        if(!d.projectile){
            float w=d.size,h=w*std::clamp(std::abs(d.vz)/70,1.f,3.f);
            auto v=[&](float x,float y){return liquidEffectVertex(d.x+camera.right[0]*x*w+camera.up[0]*y*h,d.y+camera.right[1]*x*w+camera.up[1]*y*h,d.z+camera.right[2]*x*w+camera.up[2]*y*h,x,y,.75f*fade,liquidRingMode,color,-1);};
            quad(particleVertices,v(-1,-1),v(1,-1),v(1,1),v(-1,1));
        }else
        pixelBlock(particleVertices,camera,d.x,d.y,d.z,d.size,std::clamp(std::abs(d.vz)/90,1.f,3.f),color,false,&d.color,false,nullptr,fade);
    }
    for(const auto &d:liquidRuns) {
        WallSpan span;if(!wallSpan(lines[d.line],d.side,d.part,span))continue;
        float ex=span.b.x-span.a.x,ey=span.b.y-span.a.y,len=std::hypot(ex,ey);if(len<1)continue;
        float tx=ex/len,ty=ey/len;
        if(SV_MapMode()){float point[6];int owner;float x=span.a.x+tx*d.along,y=span.a.y+ty*d.along;
            if(SV_Trace(x+ty*2,y-tx*2,span.anchor+d.dz,-ty*4,tx*4,0,point,&owner)>=1)continue;}
        float age=now-d.born,top=std::min(span.top,span.anchor+d.dz+d.width);
        float bottom=std::max(span.bottom,top-d.width*2-d.length*std::min(1.f,age/3));
        float u0=std::max(0.f,d.along-d.width*.5f),u1=std::min(len,d.along+d.width*.5f);
        if(top<=bottom||u0>=u1)continue;
        float x=span.a.x+tx*d.along,y=span.a.y+ty*d.along;
        Image &flat=lumpImage(firstflat+flattranslation[sectors[d.sector].floorpic],true);
        float light=liquidLight(d.sector,flat,x,y,top,camera),fade=std::min(1.f,(8-age)/2)*.8f;
        float lift=decalLift(x,y,camera)+.18f;
        auto vertex=[&](float u,float z){Vertex v={span.a.x+tx*u+ty*lift,span.a.y+ty*u-tx*lift,z,0,0,fade,0};v.red=d.color[0]*light;v.green=d.color[1]*light;v.blue=d.color[2]*light;return v;};
        quad(particleVertices,vertex(u0,top),vertex(u1,top),vertex(u1,bottom),vertex(u0,bottom));
    }
}

extern "C" void P_LineAttack(mobj_t*,angle_t,fixed_t,fixed_t,int);
extern "C" void I_TestLiquidProjectiles() {
    auto saved=settings;int oldTime=leveltime;
    settings.splashes=settings.liquidProjectileImpacts=1;
    mobj_t mo={};mo.subsector=players[consoleplayer].mo->subsector;
    mo.x=players[consoleplayer].mo->x;mo.y=players[consoleplayer].mo->y;
    int sector=int(mo.subsector->sector-sectors);
    bool oldLiquid=sectorMist[sector].liquid;sectorMist[sector].liquid=true;
    rings.clear();
    P_LineAttack(players[consoleplayer].mo,0,64*FRACUNIT,-4*FRACUNIT,5);
    if(rings.empty())I_Error("Solid-world bullet missed liquid splash callback");
    puts("PASS solid-world bullet produces liquid splash");
    mo.z=sectors[sector].floorheight;mo.momz=-FRACUNIT;
    mo.flags=MF_MISSILE;mo.type=MT_ROCKET;mo.info=&mobjinfo[MT_ROCKET];
    liquidDrops.clear();
    if(!liquidProjectileImpact(&mo)||liquidDrops.size()!=72)I_Error("Rocket liquid burst missing");
    mo.flags=0;if(!liquidProjectileHidden(&mo))I_Error("Liquid fireball was not hidden");
    mo.flags=MF_MISSILE;mo.type=MT_PLASMA;mo.info=&mobjinfo[MT_PLASMA];
    liquidDrops.clear();if(!liquidProjectileImpact(&mo)||liquidDrops.size()>=72)I_Error("Plasma splash was not smaller");
    mo.type=MT_TROOPSHOT;mo.info=&mobjinfo[MT_TROOPSHOT];
    if(!liquidProjectileImpact(&mo))I_Error("Monster projectile did not splash");
    mo.z+=40*FRACUNIT;if(liquidProjectileImpact(&mo))I_Error("Air impact treated as water");mo.z-=40*FRACUNIT;
    sectorMist[sector].liquid=false;if(liquidProjectileImpact(&mo))I_Error("Dry floor treated as water");sectorMist[sector].liquid=true;
    for(int i=0;i<24;++i)liquidProjectileImpact(&mo);
    if(liquidDrops.size()>384||liquidBlasts.size()>64)I_Error("Liquid effect budget exceeded");
    leveltime+=4*TICRATE;tickLiquidBursts();
    if(!liquidDrops.empty()||!liquidBlasts.empty())I_Error("Liquid effects did not expire");
    // Shoot a cosmetic drop at an intact real-map wall and verify it turns
    // into a wall-aligned run, then expires instead of accumulating forever.
    leveltime=oldTime;bool testedWall=false;
    for(int i=0;i<numlines&&!testedWall;++i)for(int side=0;side<2&&!testedWall;++side){
        WallSpan span;if(!wallSpan(lines[i],side,0,span)||span.top-span.bottom<16)continue;
        float ex=span.b.x-span.a.x,ey=span.b.y-span.a.y,len=std::hypot(ex,ey);if(len<32)continue;
        float nx=ey/len,ny=-ex/len,x=(span.a.x+span.b.x)*.5f,y=(span.a.y+span.b.y)*.5f;
        liquidDrops.clear();liquidRuns.clear();
        liquidDrops.push_back({x+nx*2,y+ny*2,(span.bottom+span.top)*.5f,-nx*140,-ny*140,0,1,gameSeconds(),sector,{.1f,.3f,.8f}});
        liquidBlastTic=-1;tickLiquidBursts();
        if(liquidRuns.empty())continue;
        testedWall=true;size_t before=particleVertices.size();Uniforms camera={};buildLiquidBursts(camera);
        if(particleVertices.size()<=before)I_Error("Liquid wall run was not drawn");particleVertices.resize(before);
        leveltime+=9*TICRATE;tickLiquidBursts();if(!liquidRuns.empty())I_Error("Liquid wall drips did not expire");
    }
    if(!testedWall)I_Error("Liquid drops did not stick to an intact wall");
    leveltime=oldTime;settings.liquidProjectileImpacts=0;
    if(liquidProjectileImpact(&mo)||liquidProjectileHidden(&mo)||!liquidDrops.empty())I_Error("Liquid toggle did not restore normal effects");
    settings.environmentEffects=settings.debrisLiquids=settings.splashes=1;
    liquidDrops.clear();debrisLiquidSplash(units(mo.x),units(mo.y),units(mo.z),sector,2);
    if(liquidDrops.empty())I_Error("Rubble did not splash in liquid");
    if(Settings{}.liquidRunoff!=0)I_Error("Experimental runoff should be disabled");
    puts("PASS experimental runoff disabled by default");
    sectorMist[sector].liquid=oldLiquid;settings=saved;splashesShutdown();
    puts("PASS liquid impacts: rockets, smaller plasma and monster shots, surface-only, wall drips, toggle, limits and expiry");
}

extern "C" void I_DebugSavedLiquid(){
    auto *m=players[consoleplayer].mo;auto *at=m->subsector->sector;
    printf("LIQUID options %d %d %d streams %zu sector-liquid %d\n",settings.environmentEffects,settings.liquidRunoff,settings.splashes,runoffs.size(),int(sectorMist[at-sectors].liquid));
    size_t faces=0;for(const auto &r:runoffs)faces+=r.path.size();printf("LIQUID flow path points %zu\n",faces);
    for(int i=0;i<at->linecount;++i){auto &l=*at->lines[i];if(!l.frontsector||!l.backsector)continue;
        printf("EDGE %d sectors %d %d floor %.1f %.1f liquid %d %d ends %.1f %.1f %.1f %.1f\n",int(&l-lines),int(l.frontsector-sectors),int(l.backsector-sectors),units(l.frontsector->floorheight),units(l.backsector->floorheight),int(sectorMist[l.frontsector-sectors].liquid),int(sectorMist[l.backsector-sectors].liquid),units(l.v1->x),units(l.v1->y),units(l.v2->x),units(l.v2->y));
    }
}
