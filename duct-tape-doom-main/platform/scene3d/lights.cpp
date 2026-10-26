/* Dynamic lights collected each frame: muzzle flashes, projectiles,
   decorations, barrels and door spill, packed for the GPU. */
#include "internal.h"
#include "frame_workers.h"
extern "C" {
#include "p_local.h"
}

namespace doom3d {
std::array<const mobj_t*,maxLights> lightSources={};
#ifdef DOOM_TEST_DRIVER
extern "C" int I_TestKeyLight(mobj_t* thing){return std::find(lightSources.begin(),lightSources.end(),thing)!=lightSources.end();}
#endif

std::array<bool,maxLights> fogOnly={};

std::array<uint8_t,maxLights> lightKinds={};
uint8_t lightKind=0;
struct FlashPulse { float x,y,z,radius,strength; int tic; const mobj_t *source; std::array<float,3> color; };
static std::vector<FlashPulse> flashPulses;
static unsigned flashLevelSerial;
static std::unordered_map<unsigned,std::array<float,3>> glowColors;
static std::array<float,3> frameGlow(int sprite,int frame) {
    frame&=FF_FRAMEMASK;
    if(sprite<0||sprite>=numsprites||frame>=sprites[sprite].numframes) return {1,1,1};
    unsigned key=(unsigned(sprite)<<16)|unsigned(frame);
    auto cached=glowColors.find(key);if(cached!=glowColors.end()) return cached->second;
    const auto &sf=sprites[sprite].spriteframes[frame];
    std::array<float,3> color={1,1,1};float weight=-1;
    for(int n=0;n<(sf.rotate?8:1);++n) {
        const Image &image=lumpImage(firstspritelump+sf.lump[n],false);
        if(image.glowWeight>weight) {weight=image.glowWeight;color=image.glow;}
    }
    glowColors.emplace(key,color);return color;
}
void muzzleFlash(mobj_t *source,int weapon,int projectile) {
    if(!source||gamestate!=GS_LEVEL) return;
    if(flashLevelSerial!=r_levelserial) {flashPulses.clear();flashLevelSerial=r_levelserial;}
    float radius=384,strength=1.9f;int flashState=-1;
    Game_MuzzleFlash(weapon,&radius,&strength,&flashState);
    float z=source->player?units(source->player->viewz)-8:units(source->z)+units(source->height)*0.65f;
    int sprite=source->sprite,frame=source->frame;
    if(projectile>=0&&projectile<NUMMOBJTYPES) {
        const auto &state=states[mobjinfo[projectile].spawnstate];sprite=state.sprite;frame=state.frame;
    } else if(source->player&&flashState>=0) {
        const auto &state=states[flashState];sprite=state.sprite;frame=state.frame;
    }
    FlashPulse pulse={units(source->x),units(source->y),z,radius,strength,leveltime,source,frameGlow(sprite,frame)};
    for(auto &old:flashPulses) if(old.source==source) {old=pulse;return;}
    if(flashPulses.size()==8) flashPulses.erase(flashPulses.begin());
    flashPulses.push_back(pulse);
}
// A short flash where a shot sparks off machinery or breaks a lamp
// (shot_lights.cpp); a new one replaces the last.
void sparkFlash(float x,float y,float z,float radius,float strength,const std::array<float,3> &color) {
    if(gamestate!=GS_LEVEL) return;
    if(flashLevelSerial!=r_levelserial) {flashPulses.clear();flashLevelSerial=r_levelserial;}
    FlashPulse pulse={x,y,z,radius,strength,leveltime,nullptr,color};
    for(auto &old:flashPulses) if(!old.source) {old=pulse;return;}
    if(flashPulses.size()==8) flashPulses.erase(flashPulses.begin());
    flashPulses.push_back(pulse);
}
// Directionality: 0 lights every surface alike, 1 is full Lambert facing.
// Unoccluded lights skip wall blockers, so shading them costs no ray tests.
// baked: the part of this light already in the bake maps, which world surfaces
// subtract so they only gain its flicker and detail (0 for dynamic-only lights).
// spot: a spotlight's unit axis and cone cosine (direction); without a cone,
// x is a glowing panel's half-height, which softens its wall shadows.
// Reuse exact wall occluders for stationary lights until sector geometry changes.
// Color/flicker/strength do not affect blocker construction; position and cone do.
struct CachedBlockers {std::vector<LightBlocker> geometry;uint64_t used=0;};
static std::map<std::array<float,8>,CachedBlockers> blockerCache;
static uint64_t blockerUse=0;
static std::vector<std::array<float,3>> blockerSectors;
static unsigned blockerLevel=~0u;
static void updateBlockerCache() {
    bool reset=blockerLevel!=r_levelserial||blockerSectors.size()!=size_t(numsectors);
    blockerLevel=r_levelserial;blockerSectors.resize(numsectors);
    static std::vector<std::array<float,4>> changed;changed.clear();
    for(int i=0;i<numsectors;++i){
        std::array<float,3> state={floorZ(&sectors[i]),ceilZ(&sectors[i]),float(sectors[i].ceilingpic==skyflatnum)};
        if(!reset&&state!=blockerSectors[i]) {
            // Only borders of this sector can acquire a different opening.
            // A light whose original XY query misses every border cannot
            // change its ordered blocker list or its hidden-wall culling.
            for(int n=0;n<sectors[i].linecount;++n) {
                const auto &line=*sectors[i].lines[n];
                float ax=units(line.v1->x),ay=units(line.v1->y),bx=units(line.v2->x),by=units(line.v2->y);
                changed.push_back({std::min(ax,bx),std::min(ay,by),std::max(ax,bx),std::max(ay,by)});
            }
        }
        blockerSectors[i]=state;
    }
    if(reset){blockerCache.clear();return;}
    if(changed.empty())return;
    for(auto it=blockerCache.begin();it!=blockerCache.end();) {
        const auto &k=it->first;bool affected=false;
        for(const auto &b:changed)if(!(b[2]<k[0]-k[3]||b[0]>k[0]+k[3]||b[3]<k[1]-k[3]||b[1]>k[1]+k[3])){affected=true;break;}
        if(affected)it=blockerCache.erase(it);else ++it;
    }
}
void appendLight(float x,float y,float z,float radius,float strength,const std::array<float,3> &color,const mobj_t *source,float directionality,bool occluded,float baked,
                 const std::array<float,4> &spot) {
    if(flashes.count>=maxLights) return;
    ProfileTimer timer{profile.frame.lightsMs};
    lightSources[flashes.count]=source;fogOnly[flashes.count]=false;lightKinds[flashes.count]=lightKind;
    Flash &f=flashes.lights[flashes.count++];
    f={{x,y,z,radius},strength,(unsigned)lightBlockers.size(),0,baked,{color[0],color[1],color[2],directionality},{spot[0],spot[1],spot[2],spot[3]}};
    if(!occluded) return;
    std::array<float,8> cacheKey={x,y,z,radius,spot[0],spot[1],spot[2],spot[3]};
    auto cached=blockerCache.find(cacheKey);
    if(cached!=blockerCache.end())cached->second.used=++blockerUse;
    const bool verify=M_CheckParm((char*)"-verifylightcache")!=0;
    if(cached!=blockerCache.end()&&!verify){
        const auto &geometry=cached->second.geometry;
        lightBlockers.insert(lightBlockers.end(),geometry.begin(),geometry.end());
        f.count=(unsigned)geometry.size();return;
    }
    for(int i=0;i<numlines;++i) {
        const line_t &line=lines[i];
        float ax=units(line.v1->x),ay=units(line.v1->y),bx=units(line.v2->x),by=units(line.v2->y);
        if(std::max(ax,bx)<x-radius||std::min(ax,bx)>x+radius||
           std::max(ay,by)<y-radius||std::min(ay,by)>y+radius) continue;
        float bottom=1,top=-1;
        if(line.frontsector&&line.backsector) {
            bottom=std::max(floorZ(line.frontsector),floorZ(line.backsector));
            top=std::min(ceilZ(line.frontsector),ceilZ(line.backsector));
            if(line.frontsector->ceilingpic==skyflatnum&&line.backsector->ceilingpic==skyflatnum) top=32768;
            if(line.frontsector==line.backsector) continue;
        }
        LightBlocker blocker={{ax,ay,bx,by},{bottom,top,0,0}};
        if(!doom_flash_blocker_relevant(&f,&blocker,!line.backsector)) continue;
        lightBlockers.push_back(blocker);
    }
    f.count=doom_flash_cull_hidden(&f,lightBlockers.data()+f.first,(uint32_t)lightBlockers.size()-f.first);
    lightBlockers.resize(f.first+f.count);
    if(verify&&cached!=blockerCache.end()){
        const auto &geometry=cached->second.geometry;
        if(geometry.size()!=f.count || (f.count&&memcmp(geometry.data(),lightBlockers.data()+f.first,f.count*sizeof(LightBlocker))))
            I_Error((char*)"Cached shadow blockers differ from freshly computed geometry");
    }
    // Moving projectiles must not flush all stationary lights at capacity.
    // Recycle just the least recently used entry, including its allocation.
    if(cached==blockerCache.end()&&blockerCache.size()>=256) {
        auto oldest=std::min_element(blockerCache.begin(),blockerCache.end(),[](const auto &a,const auto &b){return a.second.used<b.second.used;});
        auto node=blockerCache.extract(oldest);node.key()=cacheKey;
        cached=blockerCache.insert(std::move(node)).position;
    }
    if(cached==blockerCache.end())cached=blockerCache.try_emplace(cacheKey).first;
    cached->second.geometry.assign(lightBlockers.begin()+f.first,lightBlockers.end());cached->second.used=++blockerUse;
}
static void doorSpill(const Uniforms &camera);

// Exploding barrels: the light builds through the fireball and peaks on the
// blast tic (A_Explode), then fades; the blast itself also leaves sparks and
// a floor scorch, once per barrel.
static std::unordered_map<const mobj_t*,bool> burstBarrels;
static float barrelLight(const mobj_t &thing) {
    int frame=Game_BlastFrame(&thing);
    if(frame<0)return 0;
    static const float curve[GAME_BLAST_FRAMES]={0.7f,1.2f,1.8f,2.8f,1.5f};
    float strength=curve[frame];
    if(frame==GAME_BLAST_FRAMES-1)strength*=std::clamp(thing.tics/10.0f,0.0f,1.0f);
    return strength;
}
// Teleport fog (and the item respawn fog): a burst that peaks as it appears
// and fades over the rest of its frames, timed from the state chain so
// DEHACKED changes keep it in step. 0 for every other thing.
static float teleportLight(const mobj_t &thing) {
    if(Game_ThingRole(thing.type)!=GAME_THING_TELEPORT_FOG||!thing.state)return 0;
    int total=0,elapsed=-1,current=int(thing.state-states);
    for(int state=thing.info->spawnstate,n=0;state!=S_NULL&&n<32;state=states[state].nextstate,++n) {
        if(state==current)elapsed=total+std::max(0,(int)(states[state].tics-thing.tics));
        total+=std::max(0,(int)states[state].tics);
    }
    if(elapsed<0||total<=0)return 0;
    float t=elapsed/(float)total;
    return 1.8f*std::clamp((1-t)/0.7f,0.0f,1.0f);
}
// Decoration lights: things whose spawn frame is fullbright by design
// (torches, candles, lamps, burning barrels), recognized from thing data
// rather than names, and those the game names (Game_DecorationLight:
// Heretic's chandeliers and serpent torches). Monsters, shots, pickups and
// short-lived fogs and puffs are excluded. Animated flames flicker like DOOM's fire-flicker sectors:
// a stepped drop every 4 tics, from a hash rather than the game's RNG.
bool decorationLight(const mobj_t &thing) {
    if(thing.player||(thing.flags&(MF_MISSILE|MF_COUNTKILL|MF_SPECIAL|MF_NOBLOCKMAP)))return false;
    if(Game_DecorationLight(thing.sprite))return true;
    const state_t &spawn=states[thing.info->spawnstate];
    return (spawn.frame&FF_FULLBRIGHT)&&(thing.frame&FF_FULLBRIGHT);
}
// The flicker hashes finish with MurmurHash2's mixing step (Austin Appleby, public domain).
static float decorationFlicker(const mobj_t &thing) {
    const state_t &spawn=states[thing.info->spawnstate];
    if(spawn.tics<0||spawn.nextstate==thing.info->spawnstate)return 1;
    uint32_t hash=(uint32_t)(uintptr_t)&thing*2654435761u^(uint32_t)(leveltime/4)*40503u;
    hash^=hash>>13;hash*=0x5bd1e995u;hash^=hash>>15;
    return 1-0.12f*(hash&3);
}
// Bake-only lights flicker by group: each animated decoration falls in one
// of the groups switching lamps leave (up to 15) that step like
// decorationFlicker; group 0 stays steady. Lamp groups switch in every mode,
// decoration groups only with bake-only lights (their dynamic copies flicker
// them otherwise).
int flickerGroup(const mobj_t &thing) {
    const state_t &spawn=states[thing.info->spawnstate];
    int free=flickerGroups-1-lampGroupCount;
    if(spawn.tics<0||spawn.nextstate==thing.info->spawnstate||free<=0)return 0;
    uint32_t hash=(uint32_t)(uintptr_t)&thing*2654435761u;
    return 1+lampGroupCount+(int)((hash>>16)%free);
}
float groupFlicker(int group) {
    if(group<=0||group>=flickerGroups)return 1;
    if(group<=lampGroupCount)return lampGroupFlicker[group];
    if(!bakeOnlyActive)return 1;
    uint32_t hash=(uint32_t)group*2654435761u^(uint32_t)(leveltime/4)*40503u;
    hash^=hash>>13;hash*=0x5bd1e995u;hash^=hash>>15;
    return 1-0.12f*(hash&3);
}
// A decoration's unflickered light, shared by the dynamic set and the bake.
// Size follows the artwork: a candle glows small, a tall torch wide. It
// shines near the top, or, for art that isn't fullbright, from where its
// flames are painted (a chandelier's candles sit low under the chain).
BakeLight decorationSource(const mobj_t &thing) {
    const auto &frame=sprites[thing.sprite].spriteframes[thing.frame&FF_FRAMEMASK];
    const Image &image=lumpImage(firstspritelump+frame.lump[0],false);
    float height=(float)image.height,up=Game_DecorationLight(thing.sprite)?image.glowHeight:0.85f;
    auto color=frameGlow(thing.sprite,thing.frame);
    return {units(thing.x),units(thing.y),units(thing.z)+height*up,std::clamp(height*4.0f,96.0f,320.0f),
            std::clamp(height/60.0f,0.6f,1.2f),0.75f,{color[0],color[1],color[2]}};
}
std::vector<StaticFog> staticFog;
// The carried torch's color: the glow of the game's torch item sprite
// (Game_Sprite), paled halfway to white like firelight on stone, or a
// candle's warm white.
std::array<float,3> torchColor() {
    int sprite=Game_Sprite(GAME_SPRITE_TORCH);
    if(sprite<0)return {1.0f,0.75f,0.45f};
    auto glow=frameGlow(sprite,0);
    for(float &c:glow)c=0.5f+0.5f*c;
    return glow;
}
void collectFlashes(float fraction,const Uniforms &camera) {
    updateBlockerCache();
    flashes={};lightBlockers.clear();fogOnly={};lightKind=I_RENDER3D_LIGHT_FLASHLIGHT;
    // A carried torch (Game_Torch): a flame held low at the left hand,
    // lighting all around like the torches on the walls, flickering with
    // the game's own torch and a fine stepped flicker of its own. Things
    // throw shadows from it (decoration kind, not the flashlight's).
    float flame;
    if(Game_Torch(&flame)&&flame>0) {
        float at[3];
        for(int c=0;c<3;++c)at[c]=camera.eye[c]-camera.right[c]*12-camera.up[c]*10+camera.forward[c]*8;
        uint32_t hash=(uint32_t)(leveltime/3)*2654435761u;hash^=hash>>13;hash*=0x5bd1e995u;hash^=hash>>15;
        lightKind=I_RENDER3D_LIGHT_DECORATION;
        appendLight(at[0],at[1],at[2],640,1.0f*(0.7f+0.3f*flame)*(1-0.06f*(hash&3)),torchColor(),nullptr,0.75f);
        lightKind=I_RENDER3D_LIGHT_FLASHLIGHT;
    }
    // The flashlight rides at the right shoulder, a little below, right of and
    // behind the eye like Doom 3's, aimed to meet the view 256 units ahead.
    // High up it lights the floor ahead from above rather than skimming it,
    // and from behind the eye it reaches the floor just ahead; a light at the
    // eye would hide every shadow it casts behind its caster, and from the
    // side walls, door frames and things throw visible shadows (see
    // flashlightSilhouette).
    if(flashlightOn) {
        float lamp[3],aim[3],length=0,forward[3],right[3],up[3];
        auto unit=[](const float *v,float *out){float l=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);for(int c=0;c<3;++c)out[c]=l>0?v[c]/l:0;};
        unit(camera.forward,forward);unit(camera.right,right);unit(camera.up,up);
        for(int c=0;c<3;++c) lamp[c]=camera.eye[c]+right[c]*16-up[c]*6-forward[c]*20;
        // Like Doom 3's, the lamp backs out of walls: hugging a wall on the
        // right (the player's radius is the lamp's offset) or facing into a
        // corner put it on or behind the wall, which then blocked the beam.
        float dx=lamp[0]-camera.eye[0],dy=lamp[1]-camera.eye[1],reach=std::sqrt(dx*dx+dy*dy),clear=1;
        for(int i=0;i<numlines&&reach>0;++i) {
            const line_t &line=lines[i];
            if(line.frontsector&&line.backsector&&line.frontsector!=line.backsector&&
               lamp[2]>std::max(floorZ(line.frontsector),floorZ(line.backsector))&&
               lamp[2]<std::min(ceilZ(line.frontsector),ceilZ(line.backsector))) continue;
            float ax=units(line.v1->x),ay=units(line.v1->y),ex=units(line.v2->x)-ax,ey=units(line.v2->y)-ay;
            float denominator=dx*ey-dy*ex;if(std::abs(denominator)<1e-6f)continue;
            float t=((ax-camera.eye[0])*ey-(ay-camera.eye[1])*ex)/denominator;
            float u=((ax-camera.eye[0])*dy-(ay-camera.eye[1])*dx)/denominator;
            if(t>=0&&u>=0&&u<=1)clear=std::min(clear,std::max(0.0f,t-4/reach));
        }
        lamp[0]=camera.eye[0]+dx*clear;lamp[1]=camera.eye[1]+dy*clear;
        // The beam sways with the gun as Doom bobs it (A_WeaponReady): side to
        // side over 64 tics and dipping twice a cycle, by how fast the player
        // moves, plus a slow drift at rest, so it feels carried rather than
        // glued to the view.
        float bob=std::min(1.0f,players[displayplayer].bob/16.0f/FRACUNIT),time=leveltime+fraction;
        float phase=time*(2*doomPi/64),seconds=time/TICRATE;
        float yaw=(std::cos(phase)*bob*1.2f+std::sin(seconds*(2*doomPi/4.3f))*0.2f)*(doomPi/180);
        float pitch=(-std::abs(std::sin(phase))*bob*0.8f+std::sin(seconds*(2*doomPi/3.1f))*0.15f)*(doomPi/180);
        for(int c=0;c<3;++c) {
            aim[c]=camera.eye[c]+(forward[c]+right[c]*std::tan(yaw)+up[c]*std::tan(pitch))*256-lamp[c];
            length+=aim[c]*aim[c];
        }
        length=std::sqrt(length);
        // The classic beam is shorter and weaker, in an old bulb's warm
        // color; the shaders apply it like extralight (FEATURE_CLASSIC_FLASHLIGHT).
        bool classic=settings.classicFlashlight;
        appendLight(lamp[0],lamp[1],lamp[2],classic?640:900,classic?1.1f:1.9f,
                    classic?std::array<float,3>{1.0f,0.86f,0.62f}:std::array<float,3>{1.0f,0.96f,0.86f},nullptr,0.85f,true,0,
                    {aim[0]/length,aim[1]/length,aim[2]/length,0.90f});
        // Its bounce off what it lights (not for the classic beam, which
        // stays Doom's extralight); soft, and only the flashlight kind so
        // things cast no shadows from it (dynamicThingLight).
        float position[3],normal[2],strength;std::array<float,3> color;
        if(!classic&&flashlightBounce(position,normal,color,strength)) {
            appendLight(position[0],position[1],position[2],256,strength,color,nullptr,0.5f,true,0,{0,normal[0],normal[1],0});
        }
    }
    if(flashLevelSerial!=r_levelserial) {flashPulses.clear();flashLevelSerial=r_levelserial;}
    flashPulses.erase(std::remove_if(flashPulses.begin(),flashPulses.end(),[](const FlashPulse &p) {
        return leveltime<p.tic||leveltime-p.tic>=6;
    }),flashPulses.end());
    lightKind=I_RENDER3D_LIGHT_SHOT;
    for(const auto &p:flashPulses) {
        float strength=p.strength*doom_flash_fade(leveltime-p.tic,fraction);
        if(strength>0) appendLight(p.x,p.y,p.z,p.radius,strength,p.color,p.source,0.75f);
    }
    // Live WAD missile objects supply continuous light, including their bright
    // impact frames. Keep the nearest eight so rapid fire has a bounded cost.
    struct Candidate {mobj_t *thing;float distance;};
    std::vector<Candidate> projectiles,barrels,decorations,teleports,keys;
    for(auto &entry:burstBarrels)entry.second=false;
    for(int i=0;i<numsectors;++i) for(mobj_t *thing=sectors[i].thinglist;thing;thing=thing->snext) {
        if(barrelLight(*thing)>0) {
            float x,y,z;interpolatedPosition(*thing,fraction,x,y,z);
            float dx=x-camera.eye[0],dy=y-camera.eye[1],dz=z-camera.eye[2];
            barrels.push_back({thing,dx*dx+dy*dy+dz*dz});
            auto seen=burstBarrels.find(thing);
            if(seen!=burstBarrels.end())seen->second=true;
            else if(Game_BlastFrame(thing)>=GAME_BLAST_BURST) {burstBarrels[thing]=true;barrelBlast(*thing,x,y,z);}
            continue;
        }
        if(teleportLight(*thing)>0) {
            float dx=units(thing->x)-camera.eye[0],dy=units(thing->y)-camera.eye[1];
            float distance=std::hypot(dx,dy);
            if(distance<1400)teleports.push_back({thing,distance});
            continue;
        }
        if((thing->flags&MF_SPECIAL)&&glowingPickup(thing->sprite)) {
            float distance=std::hypot(units(thing->x)-camera.eye[0],units(thing->y)-camera.eye[1]);
            if(distance<640)keys.push_back({thing,distance});
            continue;
        }
        if(decorationLight(*thing)) {
            float dx=units(thing->x)-camera.eye[0],dy=units(thing->y)-camera.eye[1];
            float distance=std::hypot(dx,dy);
            if(distance<1024)decorations.push_back({thing,distance});
            continue;
        }
        if(!(thing->info->flags&MF_MISSILE)||!(thing->frame&FF_FULLBRIGHT)) continue;
        if(liquidProjectileHidden(thing))continue;
        float x,y,z;interpolatedPosition(*thing,fraction,x,y,z);
        float dx=x-camera.eye[0],dy=y-camera.eye[1],dz=z-camera.eye[2];
        projectiles.push_back({thing,dx*dx+dy*dy+dz*dz});
    }
    for(auto it=burstBarrels.begin();it!=burstBarrels.end();) it=it->second?std::next(it):burstBarrels.erase(it);
    std::sort(barrels.begin(),barrels.end(),[](const Candidate &a,const Candidate &b) {return a.distance<b.distance;});
    lightKind=I_RENDER3D_LIGHT_BARREL;
    for(size_t n=0;n<std::min(size_t(4),barrels.size());++n) {
        const mobj_t &thing=*barrels[n].thing;
        float x,y,z;interpolatedPosition(thing,fraction,x,y,z);
        appendLight(x,y,z+24,448,barrelLight(thing),frameGlow(thing.sprite,thing.frame),&thing,0.75f);
    }
    std::sort(decorations.begin(),decorations.end(),[](const Candidate &a,const Candidate &b) {return a.distance<b.distance;});
    staticFog.clear();lightKind=I_RENDER3D_LIGHT_DECORATION;
    for(size_t n=0;n<std::min(size_t(10),decorations.size());++n) {
        const mobj_t &thing=*decorations[n].thing;
        BakeLight light=decorationSource(thing);
        // Bake-only: the bake lights everything; only the fog glow remains.
        if(bakeOnlyActive) {
            float strength=light.strength*groupFlicker(flickerGroup(thing));
            staticFog.push_back({light.x,light.y,light.z,light.radius,strength,{light.color[0],light.color[1],light.color[2]}});
            continue;
        }
        float flicker=decorationFlicker(thing);
        float strength=light.strength*flicker*std::clamp((1024-decorations[n].distance)/256,0.0f,1.0f);
        appendLight(light.x,light.y,light.z,light.radius,strength,{light.color[0],light.color[1],light.color[2]},&thing,0.75f,
                    true,bakedLightsActive?1/flicker:0);
    }
    // Pickups stay dynamic even in bake-only mode: no lingering light after collection.
    std::sort(keys.begin(),keys.end(),[](const Candidate&a,const Candidate&b){return a.distance<b.distance;});
    lightKind=I_RENDER3D_LIGHT_DECORATION;
    for(size_t n=0;n<std::min(size_t(6),keys.size());++n){
        const auto& thing=*keys[n].thing;float x,y,z;interpolatedPosition(thing,fraction,x,y,z);
        std::array<float,3> color={1,.72f,.04f};
        switch(thing.sprite){
        case SPR_RKEY:case SPR_RSKU:case SPR_PSTR:color={1,.06f,.025f};break;
        case SPR_BKEY:case SPR_BSKU:case SPR_ARM2:case SPR_SOUL:color={.08f,.22f,1};break;
        case SPR_ARM1:case SPR_SUIT:case SPR_PVIS:color={.12f,1,.18f};break;
        case SPR_MEGA:color={1,.65f,.16f};break;
        case SPR_PINV:color={.45f,1,.2f};break;
        case SPR_PINS:color={.7f,.18f,1};break;
        case SPR_PMAP:color={.12f,.65f,1};break;
        default:break;
        }
        bool key=thing.sprite==SPR_RKEY||thing.sprite==SPR_RSKU||thing.sprite==SPR_BKEY||thing.sprite==SPR_BSKU||thing.sprite==SPR_YKEY||thing.sprite==SPR_YSKU;
        // Normalize clustered powerups locally, without dimming isolated ones.
        float strength=key?1.5f:.65f;
        if(!key){int neighbors=0;for(const auto& other:keys)if(other.thing!=&thing&&
            std::hypot(units(other.thing->x)-x,units(other.thing->y)-y)<160)++neighbors;
            strength/=std::sqrt(1.f+neighbors*.5f);}
        // A low, strongly directional lamp barely lights the floor at grazing
        // angles. Lift it into the pickup and retain a broad diffuse pool.
        // Negative cone marker identifies pickup light for the surface tint;
        // it remains an ordinary omnidirectional, occluded dynamic light.
        appendLight(x,y,z+(key?16:24),key?144:112,strength*std::clamp((640-keys[n].distance)/128,0.f,1.f),color,&thing,.35f,true,0,{0,0,0,-1});
    }
    std::sort(projectiles.begin(),projectiles.end(),[](const Candidate &a,const Candidate &b) {return a.distance<b.distance;});
    lightKind=I_RENDER3D_LIGHT_PROJECTILE;
    // Swarms (Heretic's teleport glitter, claw rippers, crossbow volleys,
    // flame trails) would stack a full light per spark and wash surfaces
    // out: shots within 96 units of a nearer one join its light, which
    // grows by a fifth per member up to half again as strong. Sparks
    // smaller than DOOM's plasma ball (13 pixels) shine by their area.
    struct Cluster {const mobj_t *thing;float x,y,z,radius,strength;int members;};
    std::vector<Cluster> clusters;
    for(const Candidate &candidate:projectiles) {
        const mobj_t &thing=*candidate.thing;
        float x,y,z;interpolatedPosition(thing,fraction,x,y,z);z+=units(thing.height)*0.5f;
        float radius=256,strength=1.7f;
        game_thing_role role=Game_ThingRole(thing.type);
        if(role==GAME_THING_BIG_SHOT) {radius=448;strength=2.4f;}
        else if(role==GAME_THING_ROCKET) {radius=288;strength=1.5f;}
        const spritedef_t &sprite=sprites[thing.sprite];
        if((thing.frame&FF_FRAMEMASK)<sprite.numframes) {
            const Image &image=lumpImage(firstspritelump+sprite.spriteframes[thing.frame&FF_FRAMEMASK].lump[0],false);
            strength*=std::clamp(image.width*image.height/169.0f,0.25f,1.0f);
        }
        auto joined=std::find_if(clusters.begin(),clusters.end(),[&](const Cluster &c) {
            return std::fabs(c.x-x)<96&&std::fabs(c.y-y)<96&&std::fabs(c.z-z)<96;});
        if(joined!=clusters.end()) {
            joined->radius=std::max(joined->radius,radius);joined->strength=std::max(joined->strength,strength);++joined->members;
        } else if(clusters.size()<8) clusters.push_back({&thing,x,y,z,radius,strength,1});
    }
    for(const Cluster &c:clusters)
        appendLight(c.x,c.y,c.z,c.radius,c.strength*std::min(1.5f,1+0.2f*(c.members-1)),frameGlow(c.thing->sprite,c.thing->frame),c.thing,0.75f);
    // Teleport flashes share the projectiles' light kind; the nearest four.
    std::sort(teleports.begin(),teleports.end(),[](const Candidate &a,const Candidate &b) {return a.distance<b.distance;});
    for(size_t n=0;n<std::min(size_t(4),teleports.size());++n) {
        const mobj_t &thing=*teleports[n].thing;
        appendLight(units(thing.x),units(thing.y),units(thing.z)+28,320,teleportLight(thing),frameGlow(thing.sprite,thing.frame),&thing,0.75f);
    }
    lightKind=I_RENDER3D_LIGHT_DOOR;
    doorSpill(camera);
}
float flashAt(float x,float y,float z,const Flash &f) {
    return doom_flash_at(x,y,z,&f,lightBlockers.data());
}
// Door light spill: sectors closed at level start are doors. While one is
// open, its brighter side lights the darker side through the opening: a
// light just outside each face on the dark side, as strong as the light
// difference times how far the door is open (in eighths), tinted by the
// bright side's floor. The four nearest join the dynamic lights.
struct DoorPortal { int door;std::vector<std::pair<int,int>> faces;float fullOpen; }; // faces: line, neighbor sector
static std::vector<DoorPortal> doorPortals;
void findDoors() {
    doorPortals.clear();
    for(int i=0;i<numsectors;++i) {
        const sector_t &sector=sectors[i];
        if(sector.ceilingheight>sector.floorheight)continue;
        DoorPortal door={i,{},0};float lowest=1e9f;
        for(int j=0;j<sector.linecount;++j) {
            const line_t &line=*sector.lines[j];
            const sector_t *other=line.frontsector==&sector?line.backsector:line.frontsector;
            if(!other||other==&sector)continue;
            door.faces.push_back({int(&line-lines),int(other-sectors)});lowest=std::min(lowest,units(other->ceilingheight));
        }
        if(door.faces.size()<2)continue;
        door.fullOpen=std::max(8.0f,lowest-4-units(sector.floorheight));
        doorPortals.push_back(std::move(door));
    }
}
static void doorSpill(const Uniforms &camera) {
    if(!settings.doorLight)return;
    struct Spill { float x,y,z,radius,strength,distance;std::array<float,3> color; };
    std::vector<Spill> spills;
    for(const auto &door:doorPortals) {
        const sector_t &sector=sectors[door.door];
        float open=ceilZ(&sector)-floorZ(&sector);
        if(open<2)continue;
        float fraction=std::ceil(std::clamp(open/door.fullOpen,0.0f,1.0f)*8)/8;
        for(auto [index,dark]:door.faces) {
            int bright=-1;
            for(auto [other,neighbor]:door.faces)
                if(neighbor!=dark&&(bright<0||lighting(&sectors[neighbor])>lighting(&sectors[bright])))bright=neighbor;
            if(bright<0)continue;
            float difference=lighting(&sectors[bright])-lighting(&sectors[dark]);
            if(difference<0.06f)continue;
            const line_t &line=lines[index];
            float ax=units(line.v1->x),ay=units(line.v1->y),ex=units(line.v2->x)-ax,ey=units(line.v2->y)-ay,len=std::hypot(ex,ey);
            if(len<1)continue;
            // The front side's normal is the right of v1->v2.
            float side=line.frontsector==&sectors[dark]?1.0f:-1.0f,nx=ey/len*side,ny=-ex/len*side;
            float x=ax+ex*0.5f+nx*20,y=ay+ey*0.5f+ny*20,distance=std::hypot(x-camera.eye[0],y-camera.eye[1]);
            if(distance>1400)continue;
            int pic=sectors[bright].floorpic;
            std::array<float,3> color={1,1,1};
            if(pic!=skyflatnum) {
                const auto &average=averageColor(lumpImage(firstflat+flattranslation[pic],true));
                float peak=std::max({average[0],average[1],average[2],0.01f});
                for(int c=0;c<3;++c)color[c]=0.5f+0.5f*average[c]/peak;
            }
            spills.push_back({x,y,floorZ(&sector)+std::min(open,96.0f)*0.55f,std::clamp(len*2+96,128.0f,288.0f),difference*fraction*1.8f,distance,color});
        }
    }
    std::sort(spills.begin(),spills.end(),[](const Spill &a,const Spill &b){return a.distance<b.distance;});
    for(size_t n=0;n<std::min(size_t(4),spills.size());++n) {
        const Spill &s=spills[n];
        appendLight(s.x,s.y,s.z,s.radius,s.strength,s.color,nullptr,0.75f);
    }
}

void selectFogLights(const Uniforms &camera) {
    fogLights={};if(!settings.fog)return;
    std::vector<std::pair<float,unsigned>> candidates;
    for(unsigned i=0;i<flashes.count;++i) {
        const auto &light=flashes.lights[i];
        float distance=std::hypot(light.position[0]-camera.eye[0],light.position[1]-camera.eye[1]);
        if(distance>light.position[3]+512)continue;
        float score=light.direction[3]>0?1000:light.strength/(1+distance/128);
        candidates.emplace_back(score,i);
    }
    std::stable_sort(candidates.begin(),candidates.end(),[](auto a,auto b){return a.first>b.first;});
    for(size_t i=0;i<std::min(size_t(4),candidates.size());++i)
        fogLights.indices[fogLights.count++]=candidates[i].second;
}
// A sphere that misses a triangle's bounds cannot illuminate any fragment
// of it. The conservative mask preserves every light inside its radius.
void assignLightMasks() {
    struct Job {Vertex *vertices;size_t count;VertexSection* section;uint64_t stamp=0;};
    static std::vector<Job> jobs;jobs.clear();size_t triangles=0;
    for(auto *group:{&batches,&mirrorBatches})for(auto &batch:*group) {
        auto &vertices=batch.second;
        auto add=[&](size_t first,size_t end,VertexSection* section){
            for(size_t n=first;n+2<end;n+=768){size_t count=std::min(size_t(768),(end-n)/3*3);jobs.push_back({vertices.data()+n,count,section});triangles+=count/3;}
        };
        auto sections=vertexSections.find(&vertices);size_t cursor=0;
        if(sections!=vertexSections.end())for(auto& section:sections->second){
            if(section.first<cursor||section.first+section.count>vertices.size())continue;
            add(cursor,section.first,nullptr);section.maskStamp=1469598103934665603ULL;
            add(section.first,section.first+section.count,&section);cursor=section.first+section.count;
        }
        add(cursor,vertices.size(),nullptr);
    }
    auto assign=[&](int index) {
        auto &job=jobs[index];auto vertices=job.vertices;job.stamp=1469598103934665603ULL;
        for(size_t n=0;n<job.count;n+=3) {
            unsigned mask[2]={};
            if(!(vertices[n].mode&MODE_SPRITE)||(vertices[n].mode&MODE_RELIT)) {
                const auto &a=vertices[n],&b=vertices[n+1],&c=vertices[n+2];
                float low[3]={std::min({a.x,b.x,c.x}),std::min({a.y,b.y,c.y}),std::min({a.z,b.z,c.z})};
                float high[3]={std::max({a.x,b.x,c.x}),std::max({a.y,b.y,c.y}),std::max({a.z,b.z,c.z})};
                for(unsigned i=0;i<flashes.count;++i) {
                    const auto &light=flashes.lights[i];
                    if(!fogOnly[i]&&doom_flash_reaches_bounds(&light,low,high))mask[i/32]|=1u<<(i%32);
                }
            }
            job.stamp=(job.stamp^mask[0])*1099511628211ULL;job.stamp=(job.stamp^mask[1])*1099511628211ULL;
            for(size_t v=n;v<n+3;++v)std::copy(mask,mask+2,vertices[v].lightMask);
        }
    };
    if(triangles*flashes.count>=65536)frameParallelFor((int)jobs.size(),assign);
    else for(int n=0;n<(int)jobs.size();++n)assign(n);
    for(const auto& job:jobs)if(job.section)job.section->maskStamp=(job.section->maskStamp^job.stamp)*1099511628211ULL;
    if(M_CheckParm((char*)"-verifylightthreads"))for(int n=0;n<(int)jobs.size();++n) {
        const auto &job=jobs[n];std::vector<std::array<unsigned,2>> expected(job.count);
        for(size_t v=0;v<job.count;++v)std::copy(job.vertices[v].lightMask,job.vertices[v].lightMask+2,expected[v].begin());
        assign(n);
        for(size_t v=0;v<job.count;++v)if(!std::equal(expected[v].begin(),expected[v].end(),job.vertices[v].lightMask))
            I_Error((char*)"Parallel light masks differ from serial lighting");
    }
}
// Profiling: leaves out the light kinds named in profile.skip.
void skipProfiledLights() {
    unsigned kept=0;
    for(unsigned i=0;i<flashes.count;++i) {
        if(profile.skip&(1u<<lightKinds[i]))continue;
        flashes.lights[kept]=flashes.lights[i];lightSources[kept]=lightSources[i];
        fogOnly[kept]=fogOnly[i];lightKinds[kept]=lightKinds[i];++kept;
    }
    flashes.count=kept;
}
// The GPU's lights: each light's blockers in lightWords (two words each, the
// floats' bits), then for lights with more than a few the slice table and
// its 16-bit indices (doom_flash_slice_blockers), so a pixel only tests the
// walls in its direction. first is the light's word offset, with the top bit
// set when it has slices.
FlashSet gpuFlashes;
std::vector<std::array<uint32_t,4>> lightWords;
std::array<float,maxLights> lightSliceCost={}; // Profiling: blockers a pixel tests on average.
void buildLightWords() {
    // Each light owns its scratch/output. Keep packing order identical to the
    // serial path; only the independent slice calculations run concurrently.
    struct Packed {
        std::vector<std::array<uint32_t,4>> words;
        std::vector<LightBlocker> blockers;
        std::array<float,8> shape{};
        bool valid=false,sliced=false;float cost=0;
    };
    static std::array<Packed,maxLights> packed;
    auto pack=[&](int i,bool force=false) {
        thread_local std::vector<uint16_t> indices(65535);
        auto &result=packed[i];auto &words=result.words;
        const Flash &f=flashes.lights[i];
        const LightBlocker *own=lightBlockers.data()+f.first;
        // Slice construction depends only on light position/radius/cone and
        // the ordered blocker geometry, never color, flicker or GPU offsets.
        std::array<float,8> shape;
        std::copy(f.position,f.position+4,shape.begin());std::copy(f.direction,f.direction+4,shape.begin()+4);
        bool same=result.valid&&result.shape==shape&&result.blockers.size()==f.count&&
            (!f.count||!memcmp(result.blockers.data(),own,f.count*sizeof(LightBlocker)));
        if(same&&!force)return;
        result.shape=shape;result.blockers.assign(own,own+f.count);result.valid=true;
        words.clear();result.sliced=false;
        for(uint32_t n=0;n<f.count;++n) {
            std::array<uint32_t,4> line,opening;
            memcpy(line.data(),own[n].line,sizeof(line));memcpy(opening.data(),own[n].opening,sizeof(opening));
            words.push_back(line);words.push_back(opening);
        }
        result.cost=(float)f.count;
        if(f.count<=4)return;
        uint32_t table[DOOM_FLASH_SLICES];
        uint32_t listed=doom_flash_slice_blockers(&f,own,f.count,table,indices.data(),(uint32_t)indices.size());
        if(listed==UINT32_MAX)return;
        for(int n=0;n<DOOM_FLASH_SLICES;n+=4)words.push_back({table[n],table[n+1],table[n+2],table[n+3]});
        for(uint32_t n=0;n<listed;n+=8) {
            std::array<uint32_t,4> word={};
            for(uint32_t k=0;k<8&&n+k<listed;++k)word[k/2]|=(uint32_t)indices[n+k]<<(16*(k%2));
            words.push_back(word);
        }
        result.sliced=true;result.cost=(float)listed/DOOM_FLASH_SLICES;
    };
    size_t blockers=0;for(unsigned i=0;i<flashes.count;++i)blockers+=flashes.lights[i].count;
    if(blockers>=128&&flashes.count>=4)frameParallelFor((int)flashes.count,[&](int i){pack(i);});
    else for(unsigned i=0;i<flashes.count;++i)pack(i);
    if(M_CheckParm((char*)"-verifylightthreads"))for(unsigned i=0;i<flashes.count;++i) {
        auto expected=packed[i];pack(i,true);
        if(expected.words!=packed[i].words||expected.sliced!=packed[i].sliced||expected.cost!=packed[i].cost)
            I_Error((char*)"Parallel light packing differs from serial lighting");
    }
    gpuFlashes=flashes;lightWords.clear();
    for(unsigned i=0;i<flashes.count;++i) {
        const auto &result=packed[i];
        gpuFlashes.lights[i].first=(uint32_t)lightWords.size()|(result.sliced?0x80000000u:0);
        lightWords.insert(lightWords.end(),result.words.begin(),result.words.end());
        lightSliceCost[i]=result.cost;
    }
    if(lightWords.empty())lightWords.push_back({});
}
void clearFlashPulses() {flashPulses.clear();}
void lightsShutdown() {flashPulses.clear();glowColors.clear();burstBarrels.clear();staticFog.clear();doorPortals.clear();}
extern "C" void I_TestKeyLights() {
    mobj_t *player=players[displayplayer].mo;
    Uniforms camera={};camera.eye[0]=units(player->x);camera.eye[1]=units(player->y);camera.eye[2]=units(player->z)+41;
    bool savedBeam=flashlightOn,savedBake=bakeOnlyActive;flashlightOn=false;
    for(bool only:{false,true})for(int number:{5,6,13,38,39,40}) {
        int type=0;while(type<NUMMOBJTYPES&&mobjinfo[type].doomednum!=number)++type;
        if(type==NUMMOBJTYPES)I_Error((char*)"Missing key fixture type");
        mobj_t *key=P_SpawnMobj(player->x,player->y,player->floorz,(mobjtype_t)type);
        bakeOnlyActive=only;collectFlashes(1,camera);
        const Flash *found=nullptr;
        for(unsigned n=0;n<flashes.count;++n)if(lightSources[n]==key)found=&flashes.lights[n];
        if(!found||found->baked!=0||flashAt(camera.eye[0]+24,camera.eye[1],units(player->floorz)+1,*found)<.5f)
            I_Error((char*)"Key %d missing useful dynamic floor light (bake-only %d)",number,only);
        int channel=(number==5||number==40)?2:0;
        if(found->color[channel]<.99f)I_Error((char*)"Wrong key light color");
        P_RemoveMobj(key);collectFlashes(1,camera);
        for(unsigned n=0;n<flashes.count;++n)if(lightSources[n]==key)I_Error((char*)"Collected key retained its light");
    }
    flashlightOn=savedBeam;bakeOnlyActive=savedBake;
    puts("All six key lights passed: floor illumination, colors, bake-only mode, and removal.");
}
} // namespace doom3d
