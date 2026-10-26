// Bounded cosmetic reactions, independent of gameplay RNG and actor lists.
struct EnvDust {float x,y,z,vx,vy,vz,born,size;std::array<float,3> color;bool wave,settled=false;};
static std::vector<EnvDust> envDust;
static unsigned envLevel=~0u;
static int envTic=-1,envBurstTic=-1,envBurstCount=0;
void resetEnvironment(){envDust.clear();envLevel=r_levelserial;envTic=-1;envBurstTic=-1;envBurstCount=0;}
static void syncEnvironment(){if(envLevel!=r_levelserial||!settings.environmentEffects)resetEnvironment();}
void environmentBlast(float x,float y,float z,float strength){
    syncEnvironment();if(!settings.environmentEffects)return;
    if(envBurstTic!=leveltime){envBurstTic=leveltime;envBurstCount=0;}if(envBurstCount++>=6)return;
    discoverRunoff(x,y,z);
    const sector_t *room=R_PointInSubsector(fixed_t(x*FRACUNIT),fixed_t(y*FRACUNIT))->sector;
    float now=gameSeconds(),floor=units(room->floorheight);
    bool wave=settings.pressureWaves&&strength>=.8f;
    if(wave){
        int pushed=0;
        for(auto &d:debris){
            float age=std::max(0.f,now-d.born),px=d.x+d.vx*age,py=d.y+d.vy*age,pz=std::max(floor,d.z+d.vz*age-210*age*age);
            float dx=px-x,dy=py-y,dz=pz-z,len=std::sqrt(dx*dx+dy*dy+dz*dz);
            if(len<1||len>128||pushed>=48)continue;
            if(SV_Active()&&SV_Trace(x,y,z,dx,dy,dz,nullptr,nullptr)<.98f)continue;
            float kick=(1-len/128)*180;
            d.x=px;d.y=py;d.z=pz;d.vx+=dx/len*kick;d.vy+=dy/len*kick;d.vz=d.vz-420*age+40;d.life=std::max(.1f,d.life-age);d.born=now;++pushed;
        }
    }
    if(!settings.settlingDust&&!wave)return;
    auto tint=rubbleSurfaceColor(x,y,z);for(float &c:tint)c=std::clamp(c*.38f,.06f,.65f);
    int cap=std::clamp(settings.effectBudget,64,256),count=int(12+strength*20);
    if(envDust.size()+count>size_t(cap))envDust.erase(envDust.begin(),envDust.begin()+std::min(envDust.size(),envDust.size()+count-cap));
    bool liquid=sectorMist.size()==size_t(numsectors)&&sectorMist[room-sectors].liquid;
    for(int i=0;i<count;++i){
        bool ground=wave&&!liquid&&i%2==0; if(!ground&&!settings.settlingDust)continue;
        float a=randomRange(0,2*doomPi),v=ground?randomRange(100,220):randomRange(8,38);
        envDust.push_back({x,y,ground?floor+2:z,std::cos(a)*v,std::sin(a)*v,ground?randomRange(8,20):randomRange(8,35),now,ground?1.f:randomRange(2,5),tint,ground});
    }
}
void tickEnvironment(){
    syncEnvironment();if(envTic==leveltime)return;envTic=leveltime;float now=gameSeconds(),dt=1.f/TICRATE;
    envDust.erase(std::remove_if(envDust.begin(),envDust.end(),[&](const auto &d){return now<d.born||now-d.born>6||(d.wave?!settings.pressureWaves:!settings.settlingDust);}),envDust.end());
    if(envDust.size()>size_t(std::clamp(settings.effectBudget,64,256)))envDust.resize(std::clamp(settings.effectBudget,64,256));
    for(auto &d:envDust){
        if(d.settled)continue;
        float dx=d.vx*dt,dy=d.vy*dt,dz=d.vz*dt,point[6];int owner;
        float hit=SV_Active()?SV_Trace(d.x,d.y,d.z,dx,dy,dz,point,&owner):1;
        if(hit<1){d.vx*=.1f;d.vy*=.1f;d.vz=0;if(point[5]>.5f)d.settled=true;}
        d.x+=dx*hit;d.y+=dy*hit;d.z+=dz*hit;
        const sector_t *room=R_PointInSubsector(fixed_t(d.x*FRACUNIT),fixed_t(d.y*FRACUNIT))->sector;
        float floor=units(room->floorheight)+.3f;
        if(d.z<=floor){d.z=floor;d.settled=true;}
        d.vx*=.96f;d.vy*=.96f;d.vz-=12*dt;
    }
}
void buildEnvironment(const Uniforms &camera){
    syncEnvironment();for(const auto &d:envDust){
        float age=gameSeconds()-d.born,alpha=std::clamp((6-age)/2,0.f,1.f)*(d.settled?.16f:.23f);
        const sector_t *room=R_PointInSubsector(fixed_t(d.x*FRACUNIT),fixed_t(d.y*FRACUNIT))->sector;
        auto tint=d.color;for(float &c:tint)c*=lighting(room);
        pixelBlock(particleVertices,camera,d.x,d.y,d.z,d.settled?.8f:d.size+age*.5f,1,tint,d.settled,nullptr,false,nullptr,alpha);
    }
}

extern "C" void I_TestEnvironment(){
    auto old=settings;int oldTime=leveltime;auto oldDust=std::move(envDust);auto oldDebris=std::move(debris);
    settings.environmentEffects=settings.pressureWaves=settings.settlingDust=1;settings.effectBudget=64;
    auto *p=players[consoleplayer].mo;float x=units(p->x),y=units(p->y),z=units(p->z)+30;
    debris.push_back({x+8,y,z,0,0,0,gameSeconds(),2,1,{.4f,.4f,.4f}});
    environmentBlast(x,y,z,1);
    if(debris[0].vx<=0)I_Error("Pressure wave did not move loose debris");
    for(int i=0;i<30;++i)environmentBlast(x,y,z,1);
    if(envDust.empty()||envDust.size()>64)I_Error("Environmental dust missing or over budget");
    leveltime+=7*TICRATE;tickEnvironment();if(!envDust.empty())I_Error("Environmental dust did not expire");
    leveltime=oldTime;settings.environmentEffects=0;environmentBlast(x,y,z,1);
    if(!envDust.empty())I_Error("Environmental master toggle failed");
    settings=old;envDust=std::move(oldDust);debris=std::move(oldDebris);envTic=-1;
    puts("PASS environmental pressure impulse, dust budget, expiry and master toggle");
}
