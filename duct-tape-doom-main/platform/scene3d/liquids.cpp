/* Ripple rings, spouts and bubbles where things enter or move in or are
   shot into liquids. */
#include "internal.h"
extern "C" {
#include "p_solid.h"
#include "../audio/impact_synth.h"
#include "../audio/sound_options.h"
}

namespace doom3d {
static void tickLiquidBursts();
// Splashes: per tic, things in liquid sectors are watched for landing,
// projectiles exploding at the surface, wading, and bodies coming to rest
// in it (a corpse's last death frame). Each event spreads rings; shots and
// projectiles also raise a spout. The reflection stays as it is.
struct SplashWatch { float z;bool missile,seen;int ripple;bool down;float x,y; };
struct Ring { float x,y,born,speed,life,strength;int sector; };
struct Spout { float x,y,born;int sector;uint32_t seed;float power=1; };
struct Plunge { float x,y,born,heft,radius;int sector;uint32_t seed; };
static std::unordered_map<const mobj_t*,SplashWatch> splashWatch;
static std::vector<Ring> rings;
static std::vector<Spout> spouts;
static std::vector<Plunge> plunges;
static unsigned splashLevelSerial;
// Rings and spouts hold each frame this many tics, like the bullet puff,
// so they step like a Doom sprite rather than moving every tic.
constexpr float frameTics=4;
// Water rings race out; thick liquids' rings spread slowly and not far.
static void ripple(float x,float y,int sector,float size,bool shot=false) {
    float now=leveltime/(float)TICRATE;
    bool thick=sectorMist[sector].thick;
    if(rings.size()>=24)rings.erase(rings.begin());
    rings.push_back({x,y,now,thick?10+size*0.2f:30+size*0.6f,thick?1.3f+size*0.015f:0.8f+size*0.01f,shot?1.0f:std::min(1.0f,size/40),sector});
}
// One spout per blast: pellets landing close together in the same tic share it.
static void spout(float x,float y,int sector,float power=1) {
    float now=leveltime/(float)TICRATE;
    for(Spout &s:spouts)if(s.born==now&&std::hypot(s.x-x,s.y-y)<32){s.power=std::max(s.power,power);return;}
    if(spouts.size()>=16)spouts.erase(spouts.begin());
    spouts.push_back({x,y,now,sector,(uint32_t)leveltime*2654435761u^(uint32_t)(randomRange(0,1)*16777216),power});
}
// A body coming down in a liquid: a plunge (buildPlunges) and a ring wider
// than the body. How hard is its fall (units a tic) times its weight (mass
// 100, a zombieman's, counts as 1): a body slumping in throws about 0.6, a
// zombieman falling hard about 1.5, a baron dropping in up to 3.5. Any body
// going in throws some; a whole one is heavy, not a pebble (at a tenth of the
// fall alone a ragdoll's torso threw a cute little splash).
static void plunge(float x,float y,float z,int sector,float fall,float mass,float radius,bool ring=true) {
    float heft=std::min(3.5f,std::clamp(0.5f+fall/8,0.5f,2.5f)*std::clamp(std::sqrt(mass/100),0.8f,2.2f));
    if(ring)ripple(x,y,sector,std::clamp(radius*2+heft*24,32.0f,110.0f));
    float now=leveltime/(float)TICRATE;
    if(plunges.size()>=8)plunges.erase(plunges.begin());
    plunges.push_back({x,y,now,heft,radius,sector,(uint32_t)leveltime*2654435761u^(uint32_t)(randomRange(0,1)*16777216)});
    I_MadeSound(sectorMist[sector].thick?IMPACT_SPLASH_THICK:IMPACT_SPLASH_WATER,x,y,z);
}
bool bodySplash(float x,float y,float fall,float mass,float radius) {
    if(!settings.splashes||levelSerial!=r_levelserial||splashLevelSerial!=r_levelserial||sectorMist.size()!=(size_t)numsectors)return false;
    const sector_t *sector=R_PointInSubsector((fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT))->sector;
    int index=int(sector-sectors);
    if(!sectorMist[index].liquid||sector->floorpic==skyflatnum)return false;
    plunge(x,y,floorZ(sector),index,fall,mass,radius);
    return true;
}
// Free-aimed hitscan shots stopping on a floor or ceiling (R_PlaneImpact):
// liquids splash, the pellets of one blast sharing a ring where they land
// close together; other flats take a bullet mark and chips in their color.
boolean planeImpact(sector_t *sector,boolean ceiling,fixed_t fx,fixed_t fy,fixed_t fz) {
    if(gamestate!=GS_LEVEL||levelSerial!=r_levelserial||sectorMist.size()!=(size_t)numsectors)return false;
    dropOldLevelImpacts();
    int index=int(sector-sectors);float x=units(fx),y=units(fy),z=units(fz),now=leveltime/(float)TICRATE;
    if(!ceiling&&sectorMist[index].liquid) {
        if(!settings.splashes)return false;
        float tic=leveltime/(float)TICRATE; // As splash() stamps its rings.
        bool shared=std::any_of(rings.begin(),rings.end(),[&](const Ring &r){return r.born==tic&&std::hypot(r.x-x,r.y-y)<32;});
        if(!shared)ripple(x,y,index,16,true);
        spout(x,y,index);
        return true;
    }
    shotFlat(index,ceiling,x,y,z);
    fluidBulletFlat(index,ceiling,x,y);
    if(!settings.gunImpacts)return false;
    if(flatDecals.size()>=96)flatDecals.erase(flatDecals.begin());
    flatDecals.push_back({x,y,now,randomRange(5.0f,7.5f),index,(bool)ceiling,randomRange(0,1)<0.5f});
    int pic=ceiling?sector->ceilingpic:sector->floorpic;
    Image &image=lumpImage(firstflat+flattranslation[pic],true);
    float light=lighting(sector);
    flatImpactParticles(x,y,z,ceiling,image,light);
    return false;
}
void watchSplashes() {
    tickLiquidBursts();
    if(!settings.splashes||levelSerial!=r_levelserial||sectorMist.size()!=(size_t)numsectors)return;
    if(splashLevelSerial!=r_levelserial) {splashWatch.clear();rings.clear();spouts.clear();plunges.clear();splashLevelSerial=r_levelserial;}
    for(auto &entry:splashWatch)entry.second.seen=false;
    for(int i=0;i<numsectors;++i) {
        if(!sectorMist[i].liquid||sectors[i].floorpic==skyflatnum)continue;
        float floor=units(sectors[i].floorheight);
        for(mobj_t *thing=sectors[i].thinglist;thing;thing=thing->snext) {
            bool missile=thing->flags&MF_MISSILE;
            if((thing->flags&MF_NOBLOCKMAP)&&!missile)continue;
            float x=units(thing->x),y=units(thing->y),z=units(thing->z);
            bool surface=z<=floor+1;
            bool down=(thing->flags&MF_CORPSE)&&thing->tics==-1;
            auto found=splashWatch.find(thing);
            if(found==splashWatch.end()) {splashWatch[thing]={z,missile,true,leveltime,down,x,y};continue;}
            SplashWatch &watch=found->second;watch.seen=true;
            float size=units(thing->radius)*2;
            if(down&&!watch.down&&surface) {
                ripple(x,y,i,std::clamp(size*0.8f+16,24.0f,56.0f));
                watch.down=true;watch.ripple=leveltime;watch.z=z;watch.x=x;watch.y=y;continue;
            }
            watch.down=down;
            if(watch.missile&&!missile&&z<=floor+8&&!liquidProjectileHidden(thing)) {ripple(x,y,i,48);spout(x,y,i);}
            else if(surface&&watch.z>floor+3) {
                ripple(x,y,i,std::clamp(size*0.5f+(watch.z-z)*0.8f,12.0f,64.0f));
                // A monster or corpse dropping in hard plunges; a ragdoll's body
                // is elsewhere and plunges where its torso comes down (ragdoll.cpp).
                if(!thing->player&&!missile&&watch.z-z>5&&(thing->flags&(MF_COUNTKILL|MF_CORPSE))&&!hasRagdoll(thing))
                    plunge(x,y,floor,i,watch.z-z,(float)thing->info->mass,size*0.5f,false);
                watch.ripple=leveltime;
            } else if(surface&&!down&&std::hypot(x-watch.x,y-watch.y)>1&&leveltime-watch.ripple>=12) {
                // Monsters step without momentum, so movement is the change
                // in position since the last tic.
                ripple(x,y,i,size*0.4f);watch.ripple=leveltime;
            }
            watch.z=z;watch.missile=missile;watch.x=x;watch.y=y;
        }
    }
    for(auto it=splashWatch.begin();it!=splashWatch.end();) it=it->second.seen?std::next(it):splashWatch.erase(it);
}
// The light a ring, spout or bubble takes at a point: its sector's, faded with
// distance like the floor it stands on (world.frag), so it reads as lit
// liquid, not a glow, in dark rooms. Glowing liquids stay at full light:
// any that glows at all (nukage glows on only some texels), since the pool
// shines by its own glow and the spray thrown from it is the same stuff.
float liquidLight(int sector,Image &flat,float x,float y,float z,const Uniforms &camera) {
    if(flat.emissionCoverage>0||sectorMist[sector].hot||camera.effects[2]!=0)return 1;
    float depth=(x-camera.eye[0])*camera.forward[0]+(y-camera.eye[1])*camera.forward[1]+(z-camera.eye[2])*camera.forward[2];
    return lighting(&sectors[sector])*std::clamp(1-depth/3200,0.35f,1.0f);
}
// Water's foam: its flat's colors lifted toward white, a paler blue that
// keeps the hue. Only as pale as the light it stands in: dimly lit, a pale
// blue falls on the palette's greys, so in the dark it stays the water's blue.
static std::array<float,3> foam(std::array<float,3> color,float lift,float light) {
    lift*=std::clamp(light,0.0f,1.0f);
    for(float &c:color)c+=(1-c)*lift;
    return color;
}
#include "liquid_projectiles.inc"

// Ripple crests (rings, shore ripples) are the liquid as it is drawn, one
// light step brighter: crestVertices brighten what is under them, so a crest
// keeps the shaded water's own color, reflection and lamps included, and
// stays dark where the water is dark. A crest is a slope, so it catches
// light from directions the still surface cannot: a sharp glint, much
// brighter while fresh and falling off quickly (squared) as it ages, and
// more the light's color than the liquid's. fresh (0-1) sets the step in
// quarters: a new crest doubles the surface, an old one fades out.
static std::array<float,3> crestOf(int sector,float fresh) {
    const auto &color=sectorMist[sector].color;
    float peak=std::max({color[0],color[1],color[2],0.01f});
    float step=std::round(std::clamp(fresh,0.0f,1.0f)*4)/4,lift=1.0f*step*step;
    std::array<float,3> crest;
    for(int c=0;c<3;++c)crest[c]=lift*(0.88f+0.12f*color[c]/peak);
    return crest;
}
// Spouts: a drawn effect, not a simulation; nothing in it falls. A trunk
// fat at the surface and slimming as it shoots up forks halfway into a few
// branches, among loose specks that spread wider the higher they are. It
// grows in five tics on a foam base, then dissolves from the top while the
// trunk thins and the specks blink out, the high ones first. Thick liquids
// raise a lower, fatter, sparser spout that takes longer. Laid out across
// the view like a sprite and rebuilt each frame from the spout's seed;
// stepped by frameTics, colored from the liquid's own to its brightest texel.
static void buildSpouts(const Uniforms &camera) {
    float now=gameSeconds();
    spouts.erase(std::remove_if(spouts.begin(),spouts.end(),[&](const Spout &s){
        return now<s.born-0.1f||(now-s.born)*TICRATE>(sectorMist[s.sector].thick?36:22);}),spouts.end());
    float rx=camera.right[0],ry=camera.right[1],rl=std::hypot(rx,ry);
    if(rl<1e-3f){rx=1;ry=0;}else{rx/=rl;ry/=rl;}
    auto ease=[](float u){u=std::clamp(u,0.0f,1.0f);return 1-(1-u)*(1-u);};
    for(const Spout &s:spouts) {
        const sector_t &sector=sectors[s.sector];
        bool thick=sectorMist[s.sector].thick;
        float t=std::floor(std::max(0.0f,now-s.born)*TICRATE/(thick?1.6f:1)/frameTics)*frameTics,floor=floorZ(&sector)+0.2f,ceiling=ceilZ(&sector);
        float height=(thick?24:40)*s.power,base=(thick?20:17)*std::sqrt(s.power),block=thick?3:2;
        Image &flat=lumpImage(firstflat+flattranslation[sector.floorpic],true);
        float light=liquidLight(s.sector,flat,s.x,s.y,floor+height*0.5f,camera);
        const auto &brightest=brightestColor(flat);const auto &color=sectorMist[s.sector].color;
        std::array<float,3> albedo[3]; // Dim edge, body, crest.
        for(int k=0;k<3;++k)for(int c=0;c<3;++c)albedo[k][c]=color[c]+(brightest[c]-color[c])*(k==0?0.25f:k==1?0.6f:1.0f);
        // Water foams paler toward the top.
        auto put=[&](float side,float depth,float z,float size,int shade) {
            z=std::min(z,ceiling-floor-size);
            std::array<float,3> shown=thick?albedo[shade]:foam(albedo[shade],0.3f+0.3f*std::clamp(z/height,0.0f,1.0f),light),lit;
            for(int c=0;c<3;++c)lit[c]=std::min(1.0f,shown[c]*light);
            pixelBlock(particleVertices,camera,s.x+rx*side+ry*depth,s.y+ry*side-rx*depth,floor+z,size,1,lit,true,&shown);
        };
        int draw=0;auto random=[&]{return hashUnit(draw++,0x5b0u,s.seed);};
        auto gauss=[&]{return (random()+random()+random()-1.5f)/1.5f;};
        float front=height*1.05f*ease((t+1)/5),collapse=height*1.1f*(1-std::max(0.0f,(t-8)/8)),slim=1-std::clamp((t-9)/10,0.0f,0.7f);
        // The base lies on the surface: a crest there, the liquid as drawn
        // made brighter (crestOf), not a color of its own.
        if(t<12) {
            int n=std::max(2,(int)std::round((base+3)*(t<7?1:0.7f)/2));
            auto tint=crestOf(s.sector,t<6?1.0f:0.6f);
            for(int i=0;i<n;++i) {
                float side=(i-(n-1)*0.5f)*2;
                pixelBlock(crestVertices,camera,s.x+rx*side,s.y+ry*side,floor,2,1,tint,true,nullptr,false,&crestCells);
            }
        }
        float x=0,split=height*(0.45f+random()*0.15f);
        for(float z=0;z<split;z+=block) {
            x+=(random()-0.5f)*0.8f;
            if(z>front||z>=collapse)continue;
            float width=std::max(block,(block+(base-block)*std::pow(1-z/split,1.6f))*slim);
            int n=std::max(1,(int)std::round(width/block));
            for(int j=0;j<n;++j) {
                bool edge=n>2&&(j==0||j==n-1);
                put(x+(j-(n-1)*0.5f)*block*0.9f,edge?0.1f:0,z,block,edge?0:z<4?2:1);
            }
        }
        int branches=thick?2:3;
        for(int b=0;b<branches;++b) {
            float bx=x,slope=(b-(branches-1)*0.5f)*0.5f+(random()-0.5f)*0.3f,top=height*(0.8f+random()*0.25f);
            for(float z=split;z<top;z+=block) {
                bx+=slope*block*0.5f+(random()-0.5f)*0.9f;
                int shade=random()<0.5f?1:2;
                if(z<=front&&z<collapse)put(bx,0,z,z>top-block*2?std::max(1.0f,block-1):block,shade);
            }
        }
        int specks=thick?20:40;
        for(int i=0;i<specks;++i) {
            float f=std::pow(random(),0.9f),z=f*height*1.05f,side=gauss()*(base*0.35f*(1-f)+1.5f+f*height*0.28f),depth=gauss()*2;
            float out=(side<0?-1:1)*(0.1f+random()*0.15f),up=0.1f+random()*0.2f*f;
            float size=thick?(random()<0.5f?2:3):(random()<0.25f?2:1),die=6+random()*9+(1-f)*5;
            int shade=std::min(2,(int)(random()*3));
            if(z>front||t>die)continue;
            float k=std::max(0.0f,t-std::max(1.0f,z/height*4));
            put(side+out*k,depth,z+up*k,t>die-2?1:size,shade);
        }
    }
}
// Plunges: a body come down in a liquid throws it up round itself. The
// surface heaves where it went in, and a crown of chunky globs is flung up
// and out from its edge, arcs and drops back, each leaving a crest where it
// falls in; from nearer its middle a few fatter ones shoot up twice as high
// and hardly out, a column of the stuff. A harder fall and a heavier body
// throw more, higher and wider: a baron's goes up most of a man's height.
// Thick liquids throw fewer, fatter globs that hang longer; water throws a
// spray of foam. Drawn like the spouts: rebuilt each frame
// from the plunge's seed, stepped every two tics, colored from the liquid's
// own to its brightest texel.
static void buildPlunges(const Uniforms &camera) {
    float now=gameSeconds();
    plunges.erase(std::remove_if(plunges.begin(),plunges.end(),[&](const Plunge &p){return now<p.born-0.1f||now-p.born>2.2f;}),plunges.end());
    for(const Plunge &p:plunges) {
        const sector_t &sector=sectors[p.sector];
        bool thick=sectorMist[p.sector].thick;
        float t=std::floor(std::max(0.0f,now-p.born)*TICRATE/2)*2/TICRATE,floor=floorZ(&sector)+0.2f,ceiling=ceilZ(&sector);
        Image &flat=lumpImage(firstflat+flattranslation[sector.floorpic],true);
        float light=liquidLight(p.sector,flat,p.x,p.y,floor+8,camera);
        const auto &brightest=brightestColor(flat);const auto &color=sectorMist[p.sector].color;
        std::array<float,3> albedo[3]; // Dim edge, body, crest.
        for(int k=0;k<3;++k)for(int c=0;c<3;++c)albedo[k][c]=color[c]+(brightest[c]-color[c])*(k==0?0.25f:k==1?0.6f:1.0f);
        // The heave: crest-bright over the body's middle for its first tics.
        if(t<0.35f) {
            float r=std::clamp(p.radius,8.0f,28.0f);
            auto tint=crestOf(p.sector,t<0.17f?1.0f:0.6f);
            for(float gy=-r;gy<=r;gy+=3)for(float gx=-r;gx<=r;gx+=3)
                if(gx*gx+gy*gy<=r*r)pixelBlock(crestVertices,camera,p.x+gx,p.y+gy,floor,3,1,tint,true,nullptr,false,&crestCells);
        }
        float lift=std::sqrt(p.heft),gravity=thick?700:1225;
        int crown=(int)std::round((thick?14:24)+(thick?10:20)*std::min(p.heft,3.0f));
        int globs=crown+(int)std::round(6+4*std::min(p.heft,3.0f));
        for(int i=0;i<globs;++i) {
            auto random=[&](int k){return hashUnit(i,k,p.seed);};
            bool column=i>=crown;
            float angle=(i+random(0)*0.8f)*2*doomPi/(column?globs-crown:crown);
            float start=column?p.radius*0.3f*random(1):p.radius*(0.5f+0.5f*random(1));
            float high=(thick?30:44)*lift*(0.5f+0.5f*random(2))*(column?1.7f:1),out=(thick?26:40)*lift*(0.5f+0.5f*random(3))*(column?0.25f:1);
            float s=t-(column?0.05f+random(4)*0.1f:random(4)*0.08f),up=std::sqrt(2*gravity*high),air=2*up/gravity;
            float size=(thick?3+(random(5)<0.4f?1:0):(random(5)<0.3f?2:1))+(p.heft>1.5f?1:0)+(column?1:0);
            int shade=std::min(2,(int)(random(6)*3));
            float r=start+out*std::min(1.0f,std::max(0.0f,s)/air),x=p.x+std::cos(angle)*r,y=p.y+std::sin(angle)*r;
            if(s<0)continue;
            if(s<air) {
                float z=std::min(up*s-0.5f*gravity*s*s,ceiling-floor-size);
                std::array<float,3> shown=thick?albedo[shade]:foam(albedo[shade],0.3f+0.3f*std::clamp(z/high,0.0f,1.0f),light),lit;
                for(int c=0;c<3;++c)lit[c]=std::min(1.0f,shown[c]*light);
                pixelBlock(particleVertices,camera,x,y,floor+z,size,1,lit,true,&shown);
            } else if(s<air+0.2f)
                pixelBlock(crestVertices,camera,x,y,floor,size+2,1,crestOf(p.sector,0.75f),true,nullptr,false,&crestCells);
        }
    }
}
// Shore ripples (settings.shoreRipples): liquid held by a wall ripples
// against it, drawn like the splash rings: unbroken lines of Doom-pixel
// blocks. Each crest is a line of equal distance from the shore, as a wave
// sent back off the walls would be: straight along walls, meeting in a
// point in inside corners, curving round outside corners and pillars. The
// crests sit a wavelength apart and travel out at a steady pace, stepping
// down from one light step above the liquid as drawn (crestOf) to nothing
// as they go, so the wall shows a short train of fading lines. Water
// ripples quick and close, thick liquids slow and broad, lava slower still
// and glowing; blood only shows a still dark line along the wall. Quiet
// liquid, so no foam. Phases drift across the map, so the walls of one pool
// do not pulse in lockstep.
static void buildShoreRipples(const Uniforms &camera) {
    if(!settings.shoreRipples||shoreEdges.empty()||camera.effects[2]!=0)return;
    float now=std::floor(gameSeconds()*TICRATE/frameTics)*frameTics/TICRATE;
    auto depthOf=[&](float x,float y){return (x-camera.eye[0])*camera.forward[0]+(y-camera.eye[1])*camera.forward[1];};
    for(const ShoreEdge &edge:shoreEdges) {
        // Near enough to show, and not wholly behind the eye.
        float ex=edge.bx-edge.ax,ey=edge.by-edge.ay,length=std::hypot(ex,ey),tx=ex/length,ty=ey/length;
        float t=std::clamp(((camera.eye[0]-edge.ax)*tx+(camera.eye[1]-edge.ay)*ty)/length,0.0f,1.0f);
        float nearX=edge.ax+ex*t,nearY=edge.ay+ey*t;
        if(std::hypot(nearX-camera.eye[0],nearY-camera.eye[1])>1536)continue;
        if(depthOf(edge.ax,edge.ay)<-32&&depthOf(edge.bx,edge.by)<-32)continue;
        const sector_t &sector=sectors[edge.sector];
        int look=liquidLook(sector.floorpic);if(look==LIQUID_LOOK_NONE)continue;
        Image &flat=lumpImage(firstflat+flattranslation[sector.floorpic],true);
        float z=floorZ(&sector)+decalLift(nearX,nearY,camera)+0.1f;
        float light=liquidLight(edge.sector,flat,nearX,nearY,z,camera);
        const auto &base=sectorMist[edge.sector].color;
        // wavelength, period (s), reach, block width and height, strength
        // The shore map holds distances out to shoreRange plus two cells
        // (caustics.cpp), at least 24 units: reaches stay inside that.
        float wavelength=4.5f,period=0.9f,reach=21,width=0.8f,tall=1,strength=0.75f;
        if(look==LIQUID_LOOK_THICK) {wavelength=7;period=2.4f;reach=23;width=1.8f;tall=0.6f;strength=0.6f;}
        else if(look==LIQUID_LOOK_HOT) {wavelength=8;period=3.2f;reach=23;width=2.0f;tall=0.6f;strength=0.85f;}
        float phase=0.35f*(std::sin(edge.ax*0.011f+edge.ay*0.004f)+std::cos(edge.ay*0.013f-edge.ax*0.006f));
        float minDepth=std::max(4.0f,std::min(depthOf(edge.ax,edge.ay),depthOf(edge.bx,edge.by)));
        float u=now/period+phase,lead=u-std::floor(u);
        for(int j=0;;++j) {
            float r,step;std::array<float,3> albedo;
            if(look==LIQUID_LOOK_BLOOD) {
                if(j)break;
                r=1.5f;step=1;
                for(int c=0;c<3;++c)albedo[c]=base[c]*0.55f;
            } else {
                // Crests leave the wall a wavelength apart and fade out by reach.
                r=1.5f+wavelength*(lead+j);
                if(r>reach)break;
                step=(1-(r-1.5f)/(reach-1.5f))*strength;
                if(std::round(step*4)<=0)continue;
                albedo=crestOf(edge.sector,step);
            }
            float w=width+r*0.06f;
            bool blood=look==LIQUID_LOOK_BLOOD;
            std::array<float,3> tint=albedo;
            if(blood)for(int c=0;c<3;++c)tint[c]=std::min(1.0f,albedo[c]*light);
            float spacing=std::max(std::max(4.0f,minDepth)/(100*camera.projection[1])*0.9f,w*0.7f);
            // Only where this edge is the nearest shore: the crest stops
            // where another wall's crest at the same distance takes over.
            auto put=[&](float x,float y,bool arc) {
                float d=shoreDistanceAt(x,y);
                if(d<r-1.5f||(arc&&d>r+1.5f))return;
                if(blood)pixelBlock(particleVertices,camera,x,y,z,w,tall,tint,true,&albedo,false);
                else pixelBlock(crestVertices,camera,x,y,z,w,tall,tint,true,nullptr,false,&crestCells);
            };
            int points=std::clamp((int)(length/std::max(spacing,0.05f)),1,1024);
            for(int k=0;k<=points;++k) {
                float a=length*k/points;
                put(edge.ax+tx*a+edge.nx*r,edge.ay+ty*a+edge.ny*r,false);
            }
            // Round each end: kept only where that corner is the nearest
            // shore, which is round outside corners and pillars.
            int around=std::clamp((int)(r*doomPi*0.5f/std::max(spacing,0.05f)),1,64);
            for(int k=1;k<=around;++k) {
                float angle=k*doomPi*0.5f/around,cs=std::cos(angle)*r,sn=std::sin(angle)*r;
                put(edge.ax+edge.nx*cs-tx*sn,edge.ay+edge.ny*cs-ty*sn,true);
                put(edge.bx+edge.nx*cs+tx*sn,edge.by+edge.ny*cs+ty*sn,true);
            }
        }
    }
}
// Rings: up to three full circles, the inner ones a few tics behind, growing
// a frame every frameTics and thickening as they grow. They start a light
// step above the liquid as drawn (crestOf), lit by whatever lights it, the
// flashlight included, step down to it as they age, and are gone at the end.
void buildRings(Uniforms &camera) {
    buildShoreRipples(camera);
    if(!settings.splashes||splashLevelSerial!=r_levelserial) {rings.clear();spouts.clear();plunges.clear();return;}
    float now=gameSeconds();
    rings.erase(std::remove_if(rings.begin(),rings.end(),[&](const Ring &r){return now-r.born>r.life||now<r.born-0.1f;}),rings.end());
    for(const Ring &ring:rings) {
        float age=std::max(0.0f,std::floor((now-ring.born)*TICRATE/frameTics)*frameTics/TICRATE),fade=(1-age/ring.life)*std::max(ring.strength,0.5f);
        const sector_t &sector=sectors[ring.sector];
        float z=floorZ(&sector)+decalLift(ring.x,ring.y,camera)+0.1f;
        // Points along each circle close enough to touch at the nearest
        // depth it can have on screen, so it reads as one unbroken line.
        float cx=ring.x-camera.eye[0],cy=ring.y-camera.eye[1],cz=z-camera.eye[2];
        float centerDepth=cx*camera.forward[0]+cy*camera.forward[1]+cz*camera.forward[2];
        bool thick=sectorMist[ring.sector].thick;
        for(int j=0;j<3;++j) {
            float inner=age-j*(thick?0.2f:0.12f),step=fade*(1-0.25f*j);
            if(inner<0||step<0.12f)break;
            float r=4+ring.speed*inner,width=(thick?2.0f:0.8f)+r*0.06f;
            float spacing=std::max(std::max(4.0f,centerDepth-r)/(100*camera.projection[1])*0.9f,width*0.7f);
            int points=std::clamp((int)(2*doomPi*r/std::max(spacing,0.05f)),12,512);
            // Light steps from a crest down to the liquid's own look.
            std::array<float,3> tint=crestOf(ring.sector,step);
            for(int k=0;k<points;++k) {
                float angle=k*2*doomPi/points;
                pixelBlock(crestVertices,camera,ring.x+std::cos(angle)*r,ring.y+std::sin(angle)*r,z,width,thick?0.6f:1.0f,tint,true,nullptr,false,&crestCells);
            }
        }
    }
    buildSpouts(camera);
    buildPlunges(camera);
    buildLiquidBursts(camera);
}

// A thick liquid's color for its bubbles and collars: the brightest of a few
// texels of its flat picked by hash, lit like the spouts at (x,y,z).
std::array<float,3> gooColor(int sector,uint32_t seed,float x,float y,float z,const Uniforms &camera) {
    Image &flat=lumpImage(firstflat+flattranslation[sectors[sector].floorpic],true);
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    int best=0,bestSum=-1;
    for(int k=0;k<4;++k) {
        int texel=std::min(4095,(int)(hashUnit(k,(int)seed,seed^0xb0bu)*4096));
        int index=flat.pixels[2*texel],sum=palette[index*3]+palette[index*3+1]+palette[index*3+2];
        if(sum>bestSum){bestSum=sum;best=index;}
    }
    float light=liquidLight(sector,flat,x,y,z,camera);
    return {std::min(1.0f,palette[best*3]/255.0f*light*1.15f),std::min(1.0f,palette[best*3+1]/255.0f*light*1.15f),std::min(1.0f,palette[best*3+2]/255.0f*light*1.15f)};
}
// Bubbles on thick liquids near the eye, stateless like the dust: about a
// third of the 32-unit cells over them bubble once every few seconds, at a
// new spot each time. A bubble domes up in three steps, hangs, then bursts
// into four specks and a small dotted ring. Tic-stepped.
void buildBubbles(const Uniforms &camera) {
    if(!settings.splashes||camera.effects[2]!=0||sectorMist.size()!=(size_t)numsectors)return;
    constexpr float cell=32,reach=640;
    float now=std::floor(gameSeconds()*TICRATE)/TICRATE;
    for(int i=0;i<numsectors;++i) {
        if(!sectorMist[i].thick||sectors[i].floorpic==skyflatnum)continue;
        float floor=floorZ(&sectors[i]);
        if(camera.eye[2]<floor+1)continue;
        for(int leaf:sectorFloors[i]) {
            const auto &b=floorBounds[leaf];const auto &poly=floors[leaf];
            if(poly.size()<3)continue;
            float dx=std::max({b[0]-camera.eye[0],0.0f,camera.eye[0]-b[2]}),dy=std::max({b[1]-camera.eye[1],0.0f,camera.eye[1]-b[3]});
            if(std::hypot(dx,dy)>reach)continue;
            for(int gy=(int)std::floor(b[1]/cell);gy<=(int)std::floor(b[3]/cell);++gy)
            for(int gx=(int)std::floor(b[0]/cell);gx<=(int)std::floor(b[2]/cell);++gx) {
                if(hashUnit(gx,gy,0xb0b1u)>0.35f)continue;
                float period=2.5f+4*hashUnit(gx,gy,0xb0b2u),clock=now+hashUnit(gx,gy,0xb0b3u)*period;
                int cycle=(int)std::floor(clock/period);float s=clock-cycle*period;
                if(s>=1.4f)continue;
                uint32_t seed=(uint32_t)cycle*2654435761u^(uint32_t)(gx*73856093)^(uint32_t)(gy*19349663);
                float x=(gx+0.15f+0.7f*hashUnit(gx,gy,seed))*cell,y=(gy+0.15f+0.7f*hashUnit(gx,gy,seed^1u))*cell;
                if(x<b[0]||x>b[2]||y<b[1]||y>b[3])continue;
                bool inside=true;
                for(size_t n=0;n<poly.size()&&inside;++n) {
                    Point a=poly[n],c=poly[(n+1)%poly.size()];
                    inside=(c.x-a.x)*(y-a.y)-(c.y-a.y)*(x-a.x)>=0;
                }
                if(!inside||std::hypot(x-camera.eye[0],y-camera.eye[1])>reach)continue;
                auto color=gooColor(i,seed,x,y,floor,camera);
                // The dome and the ring it leaves lie on the surface: crests,
                // the liquid as drawn made brighter (crestOf). The droplets
                // thrown up keep the goo's own color.
                if(s<1.15f) {
                    float step=s<0.3f?1:s<0.6f?2:3;
                    pixelBlock(crestVertices,camera,x,y,floor+0.2f,1.6f*step,s<0.9f?0.6f:0.8f,crestOf(i,0.5f+0.5f*s/1.15f),true,nullptr,false,&crestCells);
                } else {
                    float q=(s-1.15f)/0.25f,r=2+8*q,z=floor+0.2f+5*std::sin(doomPi*q);
                    for(int k=0;k<4;++k)
                        pixelBlock(particleVertices,camera,x+(k&1?r:-r)*0.7f,y+(k&2?r:-r)*0.7f,z,1.2f,1,color,false,&color);
                    auto ring=crestOf(i,1-q);
                    for(int k=0;k<8;++k) {
                        if(hashUnit(k,(int)seed,seed^7u)<q)continue;
                        float angle=(k+0.5f)*doomPi/4;
                        pixelBlock(crestVertices,camera,x+std::cos(angle)*r,y+std::sin(angle)*r,floor+0.2f,1.2f,1,ring,true,nullptr,false,&crestCells);
                    }
                }
            }
        }
    }
}
void splashesShutdown() {runoffs.clear();splashWatch.clear();rings.clear();spouts.clear();plunges.clear();liquidDrops.clear();liquidRuns.clear();liquidBlasts.clear();liquidBlastLevel=~0u;liquidBlastTic=-1;}
} // namespace doom3d
