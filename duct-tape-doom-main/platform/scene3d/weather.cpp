/* Weather: rain under the sky (settings.weather). Whether a map rains is
   authored, not derived: the episode's mood says so (levelMood, Game_Mood),
   or the player asks for rain everywhere. It pours from the start to the
   end of the map, driven slant by a wind of its own, apart from the sun:
   the way the episode's mood says (rainAngle), or the level picks it.
   Drawn as streaks of whole Doom pixels stepping at the tic rate, like the
   debris, falling from the sky ceiling and splashing where they land; on
   liquids they ring. Through a broken window the wind blows it in, and the
   floor and wall under the window soak (wear.cpp draws them wet). The ground
   under the sky lies wet in steps baked once (bakeWetness), the same all
   map long. The glass draws its drops itself (glass.frag), the ground its
   wetness (wear.glsl), the ambience its sound. Render-only, with its own
   random numbers. */
#include "internal.h"
extern "C" {
#include "../audio/sound_options.h"
}

namespace doom3d {
float rainNow=0,wetNow=0,glassWetNow=0;
float rainWind[2]={0,0};
namespace {
unsigned weatherLevelSerial=~0u,soakPaneSerial=~0u;
uint32_t weatherSeed=0;
float windSpeed=0;
int weatherTic=-1;
std::vector<float> soak;
bool rainy() {return settings.weather==2||(settings.weather==1&&levelMood().rain);}
uint32_t levelWeatherSeed() {
    return hash32((uint32_t)gameepisode*7919u+(uint32_t)gamemap*104729u+(uint32_t)skytexture*31u+17u);
}
// How hard it rains: a downpour, or less as the mood says (rainAmount), the
// gusts varying it a little.
float downpour(float t) {return (0.8f+0.2f*valueNoise(t,0,6,weatherSeed^0x5bd1e995u))*levelMood().rainAmount;}
void newLevel() {
    weatherLevelSerial=r_levelserial;weatherTic=-1;
    weatherSeed=levelWeatherSeed();
    windSpeed=90+50*hashUnit(3,4,weatherSeed);
    rainNow=rainy()?downpour(0):0;wetNow=rainNow>0?1:0;glassWetNow=rainNow;
    soak.clear();soakPaneSerial=~0u;
    fprintf(stderr,"3D weather: %s.\n",rainy()?levelMood().rain?"authored rain":"rain everywhere":"dry");
}
// A broken pane's share of the wind blowing in through it: none when the
// rain slants away from it or past it, so only windows facing the rain let
// it in.
float inward(const PaneView &p) {
    float speed=std::hypot(rainWind[0],rainWind[1]);
    if(speed<1) return 0;
    return std::max(0.0f,(rainWind[0]*p.inX+rainWind[1]*p.inY)/speed);
}
bool letsRainIn(const PaneView &p) {return p.broken&&p.sky&&rainNow>0.02f&&inward(p)>0.05f;}
}

float paneSoak(int pane) {return pane>=0&&(size_t)pane<soak.size()?soak[pane]:0.0f;}

// Where the rain blows (unit): the episode mood's rain angle, or else the
// level's own, picked by its hash. Never the sun's: the two are apart.
void rainHeading(float &x,float &y) {
    float authored=levelMood().rainAngle;
    float angle=authored>=0?authored*(float)doomPi/180:hashUnit(1,2,levelWeatherSeed())*2*(float)doomPi;
    x=std::cos(angle);y=std::sin(angle);
}

namespace {
// The ground's wetness per map cell under the sky (1 + wetness * 254; 0:
// not open to the sky), for the level and rain heading it was baked for.
std::vector<uint8_t> wetCells;
unsigned wetLevelSerial=~0u;
float wetHeading[2]={0,0};
}
// Bakes the wetness of the ground under the sky into the floor light's
// spare alpha (bake_atlas.cpp's floorLight layer; wear.glsl reads it). The
// rain comes in slanted on the wind, falling about four and a half units for
// one across: the foot of a wall facing it runs soaked, the lee of a wall,
// roof or ledge stays nearly dry; elsewhere the ground lies in patches damp,
// wet and soaked with puddles between. Steps of wetness, not a gradient, and
// the same all map long: the rain has been falling a while.
void bakeWetness() {
    if(flatLightCells.size()<(size_t)mapWidth*mapHeight*4||seamCells.size()<(size_t)mapWidth*mapHeight)return;
    float hx,hy;rainHeading(hx,hy);
    if(wetLevelSerial!=r_levelserial||wetCells.size()!=seamCells.size()||hx!=wetHeading[0]||hy!=wetHeading[1]) {
        wetLevelSerial=r_levelserial;wetHeading[0]=hx;wetHeading[1]=hy;
        wetCells.assign(seamCells.size(),0);
        uint32_t seed=levelWeatherSeed()^0x27d4eb2fu;
        constexpr float fall=4.5f,shelter=96;
        auto sectorIn=[&](float x,float y)->int {
            int cx=(int)std::floor((x-mapOrigin[0])/mapCell),cy=(int)std::floor((y-mapOrigin[1])/mapCell);
            if(cx<0||cy<0||cx>=mapWidth||cy>=mapHeight)return -1;
            int own=seamCells[(size_t)cy*mapWidth+cx].own;
            return own==noSector?-1:own;
        };
        // Whether rain at height z over (x, y) is stopped there: by the
        // void, a room (its roof runs up to the sky), or higher ground.
        auto stops=[&](float x,float y,float z) {
            int s=sectorIn(x,y);
            return s<0||sectors[s].ceilingpic!=skyflatnum||units(sectors[s].floorheight)>z;
        };
        for(int cy=0;cy<mapHeight;++cy)for(int cx=0;cx<mapWidth;++cx) {
            size_t i=(size_t)cy*mapWidth+cx;
            int own=seamCells[i].own;
            if(own==noSector||sectors[own].ceilingpic!=skyflatnum)continue;
            if((size_t)own<sectorMist.size()&&sectorMist[own].liquid) {wetCells[i]=1;continue;}
            float x=mapOrigin[0]+(cx+0.5f)*mapCell,y=mapOrigin[1]+(cy+0.5f)*mapCell,z=units(sectors[own].floorheight);
            bool lee=false,windward=false;
            for(float d=mapCell;d<=shelter&&!lee;d+=mapCell)lee=stops(x-hx*d,y-hy*d,z+fall*d);
            for(float d=mapCell;d<=std::max(mapCell,8.0f)&&!windward;d+=mapCell)windward=stops(x+hx*d,y+hy*d,z+24);
            float n=valueNoise(x,y,72,seed),wet=n<0.4f?0.35f:n<0.68f?0.6f:0.85f;
            if(valueNoise(x,y,40,seed^0x165667b1u)>0.7f)wet=1;
            if(windward)wet=std::max(wet,0.9f);
            if(lee)wet=0.12f;
            wetCells[i]=(uint8_t)(1+std::lround(wet*254));
        }
    }
    for(size_t i=0;i<wetCells.size();++i)flatCell(floorLight,i)[3]=wetCells[i];
}

// Each tic: the rain, the wind's gusts, whether the ground is wet (it is
// while it rains), how wet the glass gets (soaking in about 6 seconds,
// drying over one minute), and how much rain each broken window has let in:
// quick, the floor under it soaked in about 8 seconds with the wind blowing
// straight in, the wet spreading out from the sill as it does; four minutes
// to dry. Only a window facing the rain lets it in. One already broken when
// the level starts has been letting it in all along.
void watchWeather() {
    if(gamestate!=GS_LEVEL||levelSerial!=r_levelserial) return;
    if(weatherLevelSerial!=r_levelserial) newLevel();
    if(leveltime==weatherTic) return;
    weatherTic=leveltime;
    float t=leveltime/(float)TICRATE,gust=valueNoise(t,0,4,weatherSeed^0x68e31da4u);
    rainNow=rainy()?downpour(t):0;
    // The rain blows along its heading; gusts vary how hard.
    float wx,wy;rainHeading(wx,wy);
    rainWind[0]=wx*windSpeed*(0.6f+0.8f*gust);rainWind[1]=wy*windSpeed*(0.6f+0.8f*gust);
    // The heading turned (the authored layer switched on or off): the ground's wetness anew.
    if(flatLightTexture&&wetLevelSerial==r_levelserial&&(wx!=wetHeading[0]||wy!=wetHeading[1])) {
        bakeWetness();
        gpuUpdateTexture(flatLightTexture,GpuFormat::RGBA8,{{0,floorLight*mapHeight,mapWidth,mapHeight}},flatLightCells.data(),mapWidth*4);
    }
    constexpr float tic=1.0f/TICRATE;
    wetNow=rainNow>0?1:0;
    glassWetNow=rainNow>glassWetNow?glassWetNow+(rainNow-glassWetNow)*tic/6:std::max(rainNow,glassWetNow-tic/60);
    if(soakPaneSerial!=windowPaneSerial()) {
        soakPaneSerial=windowPaneSerial();soak.assign(windowPaneCount(),0);
        for(size_t i=0;i<soak.size();++i) {PaneView p;soak[i]=windowPane((int)i,p)&&letsRainIn(p)?1.0f:0.0f;}
    }
    for(size_t i=0;i<soak.size();++i) {
        PaneView p;
        if(!windowPane((int)i,p)) continue;
        if(letsRainIn(p)) soak[i]=std::min(1.0f,soak[i]+rainNow*(0.4f+0.6f*inward(p))*tic/8);
        else soak[i]=std::max(0.0f,soak[i]-tic/240);
    }
    I_AmbienceRain(settings.accelerated?rainNow:0);
}

namespace {
// Rain is pale grey-blue catching the sky's tint, unlit; the flashlight
// beam lights it from this too.
std::array<float,3> rainAlbedo() {return {0.5f+0.12f*skyTint[0],0.5f+0.12f*skyTint[1],0.53f+0.12f*skyTint[2]};}
// What a streak of rain at (x, y, z) looks like: in its sector's light, a
// little brighter than the walls behind it so it reads against them, and
// the baked lamp light of the floor under it, which it catches more the
// nearer the ground: a glowing pool or a lamp's pool of light below takes
// the rain falling into it over to its own color, by its share of the
// light there, and brightens it a little in that color. One lookup a drop.
std::array<float,3> rainColor(const sector_t &sector,float x,float y,float z) {
    float light=lighting(&sector),glow[3]={0,0,0};
    if(bakedLightsActive&&!flatLightCells.empty()) {
        int cx=std::clamp((int)std::floor((x-mapOrigin[0])/mapCell),0,mapWidth-1);
        int cy=std::clamp((int)std::floor((y-mapOrigin[1])/mapCell),0,mapHeight-1);
        const uint8_t *baked=flatCell(floorLight,(size_t)cy*mapWidth+cx);
        float near=1-0.85f*std::clamp((z-floorZ(&sector))/128,0.0f,1.0f);
        for(int c=0;c<3;++c)glow[c]=baked[c]/255.0f*2*near;
    }
    auto albedo=rainAlbedo();
    float peak=std::max({glow[0],glow[1],glow[2]}),share=peak>0.01f?std::min(1.0f,peak/(peak+light*0.5f)):0;
    std::array<float,3> color;
    for(int c=0;c<3;++c) {
        float tint=peak>0.01f?glow[c]/peak:1;
        color[c]=std::min(1.0f,albedo[c]*light*1.15f*(1+(tint-1)*share)+glow[c]*0.15f+0.04f);
    }
    return color;
}
const sector_t *sectorAt(float x,float y) {
    int cx=(int)std::floor((x-mapOrigin[0])/mapCell),cy=(int)std::floor((y-mapOrigin[1])/mapCell);
    if(cx<0||cy<0||cx>=mapWidth||cy>=mapHeight) return nullptr;
    int own=seamCells[(size_t)cy*mapWidth+cx].own;
    return own==noSector?nullptr:&sectors[own];
}
// A streak of rain whose head is at (x, y, z), moving along (vx, vy, vz):
// length units of it trail behind the head, drawn in pieces of whole Doom
// pixels, so a slant shows as a stair the way an old sprite would draw it.
void streak(const Uniforms &camera,float x,float y,float z,float vx,float vy,float vz,float length,int pieces,
            const std::array<float,3> &color,float alpha) {
    float speed=std::sqrt(vx*vx+vy*vy+vz*vz);
    if(speed<1) return;
    float step=length/pieces,dx=vx/speed,dy=vy/speed,dz=vz/speed;
    for(int k=0;k<pieces;++k) {
        float back=step*(k+0.5f);
        static std::array<float,3> albedo;albedo=rainAlbedo();
        pixelBlock(particleVertices,camera,x-dx*back,y-dy*back,z-dz*back,0.55f,step/0.55f,color,false,&albedo,true,nullptr,alpha);
    }
}
// A drop landing at (x, y, z): on a floor two specks jump aside over three
// tics; on a liquid (or a puddle) a ring of pixels grows round it.
void splash(const Uniforms &camera,float x,float y,float z,float tics,const sector_t &sector,uint32_t seed,bool puddle=false) {
    size_t index=&sector-sectors;
    bool liquid=puddle||(index<sectorMist.size()&&sectorMist[index].liquid);
    auto color=rainColor(sector,x,y,z);
    int tic=(int)tics;
    if(liquid) {
        if(tic>3) return;
        float radius=1.5f+1.5f*tic;
        int count=4+2*tic;
        for(int n=0;n<count;++n) {
            float a=(n+hashUnit(n,tic,seed)*0.5f)*2*(float)doomPi/count;
            pixelBlock(particleVertices,camera,x+std::cos(a)*radius,y+std::sin(a)*radius,z+0.3f,0.5f,1,color,true,nullptr,true,nullptr,0.55f-0.1f*tic);
        }
        return;
    }
    if(tic>2) return;
    float angle=hashUnit(5,6,seed)*2*(float)doomPi,out=1.0f+1.2f*tic,up=tic==1?1.5f:0.5f;
    for(int side=-1;side<=1;side+=2)
        pixelBlock(particleVertices,camera,x+std::cos(angle)*out*side,y+std::sin(angle)*out*side,z+up,0.5f,1,color,true,nullptr,true,nullptr,0.6f);
}
}

// The rain around the eye: one drop per 16-unit cell in a share of the
// cells as heavy as the rain (the same cells first as it thickens, so it
// grows rather than shuffles), out to 640 units, in sky sectors only, each
// falling from the sky down its sector's height and splashing. Then the
// broken windows the wind blows it in by.
// World-anchored short gusts: no persistent actors or accumulating particles.
// Check the full drift path so specks fade instead of passing through shelter.
void buildOutdoorWind(const Uniforms &camera) {
    if(!settings.outdoorWind||camera.effects[2]!=0||seamCells.empty()||levelSerial!=r_levelserial)return;
    constexpr float cell=64,reach=448,life=2.4f;
    float now=gameSeconds(),wx,wy;rainHeading(wx,wy);
    float density=std::clamp(settings.outdoorWindDensity,.25f,2.f)*.38f*(1-.85f*std::clamp(rainNow,0.f,1.f));
    int cx=int(std::floor(camera.eye[0]/cell)),cy=int(std::floor(camera.eye[1]/cell)),count=0;
    for(int gy=cy-8;gy<=cy+8&&count<128;++gy)for(int gx=cx-8;gx<=cx+8&&count<128;++gx){
        uint32_t seed=levelWeatherSeed()^0x871ab9u;
        if(hashUnit(gx,gy,seed)>density)continue;
        float clock=now+hashUnit(gx,gy,seed^1)*life;
        int cycle=int(std::floor(clock/life));float age=clock-cycle*life;
        seed^=uint32_t(cycle)*2654435761u;
        float x0=(gx+hashUnit(gx,gy,seed^2))*cell,y0=(gy+hashUnit(gx,gy,seed^3))*cell;
        const sector_t *room=sectorAt(x0,y0);
        if(!room||room->ceilingpic!=skyflatnum)continue;
        int own=int(room-sectors);bool ground=hashUnit(gx,gy,seed^4)<.35f;
        if(ground&&sectorMist[own].liquid)continue;
        float speed=(ground?105.f:85.f)+70*hashUnit(gx,gy,seed^5),phase=hashUnit(gx,gy,seed^6)*2*doomPi;
        // Integrate shared gusts so nearby specks surge together without
        // jumping when the wind changes speed. Small eddies add crosswind.
        float born=now-age;
        auto drift=[&](float t,float &dx,float &dy){
            float travel=speed*(t+.32f*(std::sin((born+t)*2)-std::sin(born*2)));
            float swirl=(ground?7.f:13.f)*(std::sin(t*4+phase)-std::sin(phase));
            dx=wx*travel-wy*swirl;dy=wy*travel+wx*swirl;
        };
        float driftX,driftY;drift(age,driftX,driftY);
        float x=x0+driftX,y=y0+driftY;
        bool sheltered=false;
        int steps=std::clamp(int(std::ceil(speed*1.7f*age/8)),1,96);
        for(int k=1;k<=steps;++k){float sx,sy;drift(age*k/steps,sx,sy);
            if(sectorAt(x0+sx,y0+sy)!=room){sheltered=true;break;}}
        if(sheltered)continue;
        float floor=floorZ(room),z=floor+(ground?2.f:12+100*hashUnit(gx,gy,seed^7));
        z+=ground?4*std::abs(std::sin(age*7+phase)):9*std::sin(age*4+phase);
        if(z>=ceilZ(room)-2)continue;
        float dx=x-camera.eye[0],dy=y-camera.eye[1],dist=std::hypot(dx,dy);
        if(dist>reach||dist<16)continue;
        float fade=std::min({1.f,age*2,(life-age)*2,(reach-dist)/96,(dist-16)/24});
        Image &flat=lumpImage(firstflat+flattranslation[room->floorpic],true);
        auto tint=averageColor(flat);
        float light=lighting(room);
        for(float &c:tint)c=(.32f+c*.45f)*light;
        pixelBlock(particleVertices,camera,x,y,z,ground?1.2f:.55f,ground?.65f:1.f,tint,false,nullptr,false,nullptr,fade*(ground?.65f:.38f));
        ++count;
    }
}

extern "C" void I_TestOutdoorWind() {
    auto saved=settings;float oldRain=rainNow;auto savedVertices=std::move(particleVertices);
    settings.outdoorWind=1;settings.outdoorWindDensity=2;rainNow=0;
    Uniforms camera={};camera.forward[0]=1;camera.right[1]=1;camera.up[2]=1;camera.projection[0]=camera.projection[1]=1;
    size_t dry=0;
    for(int y=0;y<mapHeight&&!dry;y+=4)for(int x=0;x<mapWidth&&!dry;x+=4){
        float px=mapOrigin[0]+(x+.5f)*mapCell,py=mapOrigin[1]+(y+.5f)*mapCell;
        const sector_t *s=sectorAt(px,py);if(!s||s->ceilingpic!=skyflatnum)continue;
        camera.eye[0]=px;camera.eye[1]=py;camera.eye[2]=floorZ(s)+40;
        particleVertices.clear();buildOutdoorWind(camera);dry=particleVertices.size();
    }
    if(!dry||dry>128*6)I_Error("Outdoor wind missing or over particle budget");
    particleVertices.clear();rainNow=1;buildOutdoorWind(camera);
    if(particleVertices.size()>dry)I_Error("Rain increased wind dust");
    particleVertices.clear();settings.outdoorWind=0;buildOutdoorWind(camera);
    if(!particleVertices.empty())I_Error("Wind toggle failed");
    settings=saved;rainNow=oldRain;particleVertices=std::move(savedVertices);
    puts("PASS outdoor wind renders under sky, respects particle cap, rain reduction and toggle");
}

void buildRain(const Uniforms &camera) {
    if(rainNow<=0.01f||camera.effects[2]!=0||seamCells.empty()||levelSerial!=r_levelserial||!settings.weather) return;
    constexpr float cell=16,reach=640;
    float now=std::floor(gameSeconds()*TICRATE)/TICRATE;
    float density=0.7f*rainNow;
    int gx0=(int)std::floor((camera.eye[0]-reach)/cell),gx1=(int)std::floor((camera.eye[0]+reach)/cell);
    int gy0=(int)std::floor((camera.eye[1]-reach)/cell),gy1=(int)std::floor((camera.eye[1]+reach)/cell);
    float flat=std::hypot(camera.forward[0],camera.forward[1]);
    float fx=flat>1e-3f?camera.forward[0]/flat:0,fy=flat>1e-3f?camera.forward[1]/flat:0;
    for(int gy=gy0;gy<=gy1;++gy)for(int gx=gx0;gx<=gx1;++gx) {
        if(hashUnit(gx,gy,weatherSeed)>=density) continue;
        float x0=(gx+hashUnit(gx,gy,weatherSeed^1u))*cell,y0=(gy+hashUnit(gx,gy,weatherSeed^2u))*cell;
        float dx=x0-camera.eye[0],dy=y0-camera.eye[1],distance=std::hypot(dx,dy);
        // Behind the eye only near it, where a glance down still shows them.
        if(distance>reach||(distance>96&&(dx*fx+dy*fy)<-0.3f*distance&&flat>0.5f)) continue;
        const sector_t *sector=sectorAt(x0,y0);
        if(!sector||sector->ceilingpic!=skyflatnum) continue;
        float floor=floorZ(sector),top=std::max(ceilZ(sector),camera.eye[2]+160),height=top-floor;
        if(height<8) continue;
        uint32_t seed=hash32((uint32_t)gx*73856093u^(uint32_t)gy*19349663u^weatherSeed);
        float speed=520*(0.85f+0.3f*hashUnit(gx,gy,weatherSeed^3u)),fall=height/speed,period=fall+0.15f;
        float clock=now+hashUnit(gx,gy,weatherSeed^4u)*period*7;
        int cycle=(int)std::floor(clock/period);
        float age=clock-cycle*period;
        uint32_t own=seed^(uint32_t)cycle*2654435761u;
        // It lands in its cell, coming in on the wind from upwind of it; on
        // the way it may cross other open sky, or pass over a roof, never
        // through a room.
        float lx=x0+(hashUnit(cycle,1,own)-0.5f)*cell*0.5f,ly=y0+(hashUnit(cycle,2,own)-0.5f)*cell*0.5f;
        float early=std::min(age,fall)-fall;
        float x=lx+rainWind[0]*early,y=ly+rainWind[1]*early;
        const sector_t *landing=sectorAt(lx,ly);
        if(landing!=sector) continue;
        const sector_t *at=sectorAt(x,y);
        float zHere=top-speed*std::min(age,fall);
        if(!at||(at!=sector&&at->ceilingpic!=skyflatnum&&zHere<ceilZ(at))) continue;
        if(age<fall) {
            float z=zHere;
            if(z<floor+1) continue;
            float alpha=0.32f+0.25f*hashUnit(cycle,3,own);
            streak(camera,x,y,z,rainWind[0],rainWind[1],-speed,10,distance<320?3:distance<520?2:1,rainColor(*sector,x,y,z),alpha*std::min(1.0f,rainNow*2));
        }
        else if(distance<384) splash(camera,lx,ly,floor,(age-fall)*TICRATE,*sector,own);
    }
    // Through broken windows: drops blown in across the opening, slow at
    // first and falling ever faster, to splash on the sill or the floor
    // inside. As many as the window is wide, the rain heavy and the wind
    // blowing in; none through a window the rain slants away from.
    for(int i=0;i<windowPaneCount();++i) {
        PaneView p;
        if(!windowPane(i,p)||!letsRainIn(p)||p.top-p.bottom<4) continue;
        float ex=p.bx-p.ax,ey=p.by-p.ay,len=std::hypot(ex,ey);
        float mx=(p.ax+p.bx)/2,my=(p.ay+p.by)/2;
        if(len<4||std::hypot(mx-camera.eye[0],my-camera.eye[1])>reach) continue;
        float in=inward(p),gust=std::hypot(rainWind[0],rainWind[1]);
        int count=std::min(32,(int)std::lround(len/5*rainNow*in));
        for(int n=0;n<count;++n) {
            uint32_t seed=p.seed^(uint32_t)n*0x9e3779b9u;
            float period=0.55f+0.3f*hashUnit(n,0,seed),clock=now+hashUnit(n,1,seed)*period*5;
            int cycle=(int)std::floor(clock/period);
            float age=clock-cycle*period;
            uint32_t own=seed^(uint32_t)cycle*2654435761u;
            float u=hashUnit(cycle,1,own)*len,z0=p.bottom+(p.top-p.bottom)*(0.2f+0.8f*hashUnit(cycle,2,own));
            float speed=(30+gust*1.6f)*in*(0.7f+0.6f*hashUnit(cycle,3,own));
            float x=p.ax+ex/len*u+p.inX*speed*age,y=p.ay+ey/len*u+p.inY*speed*age,z=z0-450*age*age-60*age;
            const sector_t *at=sectorAt(x,y);
            if(!at||(at!=&sectors[p.frame]&&at!=&sectors[p.inner>=0?p.inner:p.frame]&&at!=&sectors[p.outer])) continue;
            float floor=floorZ(at);
            if(z>floor+1) {
                streak(camera,x,y,z,p.inX*speed,p.inY*speed,-(900*age+60),6,2,rainColor(*at,x,y,z),0.45f);
                continue;
            }
            // Landed: when it reached the floor, then the splash from there.
            float landed=(-60+std::sqrt(3600+1800*std::max(0.0f,z0-floor)))/900;
            float lx=p.ax+ex/len*u+p.inX*speed*landed,ly=p.ay+ey/len*u+p.inY*speed*landed;
            const sector_t *under=sectorAt(lx,ly);
            if(under)splash(camera,lx,ly,floorZ(under),(age-landed)*TICRATE,*under,own);
        }
    }
    // Leaks (wear.cpp): a drop swells at the crack, lets go, falls and
    // rings the puddle below, one about every second.
    for(const CeilingLeak &l:ceilingLeaks()) {
        if(std::hypot(l.x-camera.eye[0],l.y-camera.eye[1])>reach)continue;
        const sector_t &sector=sectors[l.sector];
        float top=ceilZ(&sector),floor=floorZ(&sector),fall=std::sqrt(2*std::max(0.0f,top-floor)/800);
        float period=0.8f+0.5f*hashUnit(3,0,l.seed)+fall,clock=now+hashUnit(4,0,l.seed)*period,age=clock-std::floor(clock/period)*period;
        float hang=period-fall-0.15f;
        auto color=rainColor(sector,l.x,l.y,top);
        if(age<hang) {
            if(age>hang*0.4f)pixelBlock(particleVertices,camera,l.x,l.y,top-0.6f,0.5f,1,color,false,nullptr,true,nullptr,0.6f);
        }
        else if(age<hang+fall) {
            float t=age-hang,z=top-400*t*t;
            streak(camera,l.x,l.y,z,0,0,-800*t-1,3,1,rainColor(sector,l.x,l.y,z),0.6f);
        }
        else splash(camera,l.x,l.y,floor,(age-hang-fall)*TICRATE,sector,l.seed,true);
    }
}

void weatherShutdown() {rainNow=wetNow=glassWetNow=0;soak.clear();weatherLevelSerial=~0u;soakPaneSerial=~0u;I_AmbienceRain(0);}
} // namespace doom3d
