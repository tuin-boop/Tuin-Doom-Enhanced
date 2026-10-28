extern "C" int Menu_BrandOpen(void);
/* The scene's public state, its lifecycle and the per-frame entry points
   (prepareFrame), plus the backend-independent I_Render3D calls. */
#include "internal.h"
#include "../shaders/feature_flags.h"
#include "modern_hud.h"
#include "weapon_wheel.h"
#include "corner_peek.h"
#include "key_notice.h"
#include "key_hand.h"
#include "toasty.h"
#include "director.h"
#include "../../assets/arcade/podium_png.h"
extern "C" { int Arcade_PodiumRect(float*); extern int arcade_podium_hires; extern unsigned arcade_rank_pixels[SCREENWIDTH*SCREENHEIGHT]; }
#include "../mod_soundbank.h"
extern "C" int p_fluidfastpistol;
extern "C" int sv_chain_collapses,sv_hanging_fragments,sv_collapse_size;
extern "C" int p_player_surface_damage,p_monster_surface_damage,p_breakablewalls,p_throughwalls,p_breachdoors,p_wall_damage,p_deeperdig;
extern "C" {
#include "p_fluid.h"
void I_SetFluidSoundAddon(int enabled);
void I_SetNearMissSounds(int enabled);
void I_SetAtmosphereExtras(int hidden,int adjacent);
}
extern "C" {
extern int r_modernhud;
extern int st_helmet_style;
int I_Render3DPeekOrigin(mobj_t*,fixed_t*,fixed_t*,fixed_t*);
#include "../audio/sound_options.h"
#include "../../linuxdoom-1.10/p_local.h"
}
#include "../settings_panel.h"

namespace doom3d {
GpuTextureRef arcadePodiumTexture;
GpuTextureRef arcadeRankTexture;
Profile profile;
Settings settings;
SDL_Window *gameWindow;
FlashSet flashes;
FogLights fogLights;
std::vector<LightBlocker> lightBlockers;
std::unordered_map<int,Image> images;
std::map<const std::vector<Vertex>*,std::vector<VertexSection>> vertexSections;
std::map<int,std::vector<Vertex>> batches,shadowBatches,decalBatches,effectBatches,effectAddBatches,mirrorBatches;
std::vector<Vertex> mistVertices,particleVertices,crestVertices,heatVertices,skyVertices,shaftVertices;
GpuTextureRef seamTexture,contactTexture,cloudTexture,wallBakeTexture,flatLightTexture,causticTexture,causticPattern,shoreTexture;
GpuTextureRef wallDirectionTexture,flatDirectionTexture;
float reflectionPlane=0;
bool reflectionActive=false;
bool worldPending,screenshotPending,flashlightOn=false;
float pitch;
unsigned levelSerial;
static float oldEye[3], oldYaw;

std::vector<std::array<float,2>> oldHeights;
float renderFraction=1;
uint64_t tickTime;
static uint64_t titleTime;
static int frameCount;
static float fps,frameMs,peakFrameMs;
static uint64_t previousFrame;
static float framePeak;
static std::unordered_map<const mobj_t*, std::array<float,3>> oldThings;
void interpolatedPosition(const mobj_t &thing,float fraction,float &x,float &y,float &z) {
    x=units(thing.x);y=units(thing.y);z=units(thing.z);
    auto old=oldThings.find(&thing);
    if(old!=oldThings.end()&&std::hypot(x-old->second[0],y-old->second[1])<128) {
        x=old->second[0]+(x-old->second[0])*fraction;
        y=old->second[1]+(y-old->second[1])*fraction;
        z=old->second[2]+(z-old->second[2])*fraction;
    }
}
// -renderview x y angle [pitch] (map units, degrees; pitch up, needs free
// look) puts the player there once per level, to aim -rendercheck frames.
extern "C" boolean P_TeleportMove(mobj_t *thing,fixed_t x,fixed_t y);
static void renderViewPlace(player_t &player) {
    static unsigned placed=~0u;
    int view=M_CheckParm((char*)"-renderview");
    if(!view||view+3>=myargc||placed==r_levelserial)return;
    placed=r_levelserial;
    mobj_t *mo=player.mo;
    P_TeleportMove(mo,(fixed_t)(atof(myargv[view+1])*FRACUNIT),(fixed_t)(atof(myargv[view+2])*FRACUNIT));
    mo->z=mo->floorz;mo->angle=(angle_t)(int64_t)(atof(myargv[view+3])/360.0*4294967296.0);
    player.viewz=mo->z+player.viewheight;
    if(view+4<myargc&&myargv[view+4][0]!='-')pitch=std::clamp((float)atof(myargv[view+4])*(float)doomPi/180,-1.2f,1.2f);
}
// Advance visual interpolation in simulation time, while rendering every frame.
// Keep the previous/current snapshots for the entire slowed world tick.
static double worldInterpolation(uint64_t now){
    static uint64_t snapshot=0,last=0;static double progress=0;
    double rate=wheelActive&&!netgame&&!demorecording&&!demoplayback&&!menuactive&&!paused?.05:1.0;
    if(snapshot!=tickTime){snapshot=tickTime;progress=(now-tickTime)/(1e9/TICRATE)*rate;}
    else if(last&&now>=last)progress+=(now-last)/(1e9/TICRATE)*rate;
    last=now;return std::clamp(progress,0.0,1.0);
}
static void beforeTic() {
    if(gamestate!=GS_LEVEL||!players[displayplayer].mo||paused) {tickTime=0;return;}
    renderViewPlace(players[displayplayer]);
    mobj_t *mo=players[displayplayer].mo;
    snapshotWeaponPositions();
    oldEye[0]=units(mo->x);oldEye[1]=units(mo->y);oldEye[2]=units(players[displayplayer].viewz);
    oldYaw=(float)(mo->angle*(2.0*doomPi/4294967296.0));tickTime=SDL_GetTicksNS();
    oldThings.clear();
    oldHeights.resize(numsectors);
    for(int i=0;i<numsectors;++i) oldHeights[i]={units(sectors[i].floorheight),units(sectors[i].ceilingheight)};
    for(int i=0;i<numsectors;++i) for(mobj_t *thing=sectors[i].thinglist;thing;thing=thing->snext)
        {oldThings.emplace(thing,std::array<float,3>{units(thing->x),units(thing->y),units(thing->z)});fluidProjectileFlight(*thing);}
    tickImpactParticles();
    tickEnvironment();
    tickFluidEffects();
    watchBodies();
    watchSplashes();
    ragdollTestTic();
    watchBodies();
    watchSpray();
    watchGlass();
    watchWeather();
    watchWear();
    watchShotLights();
}
static void acceleratedView(player_t *player) {
    (void)player; worldPending=true; v_overlayactive=true;
    memset(v_overlaymask,255,sizeof(v_overlaymask));
    for(int y=viewwindowy;y<viewwindowy+viewheight;++y) {
        memset(screens[0]+y*SCREENWIDTH+viewwindowx,0,scaledviewwidth);
        memset(v_overlaymask+y*SCREENWIDTH+viewwindowx,0,scaledviewwidth);
    }
}
void applySettings() {
    fitGame(settings);
    p_breakablewalls=settings.breakableWalls;
    p_throughwalls=settings.throughWalls;
    p_breachdoors=settings.breachDoors;
    p_deeperdig=settings.deeperDig;
    p_monster_surface_damage=settings.monsterSurfaceDamage;
    p_player_surface_damage=settings.playerSurfaceDamage;
    p_wall_damage=std::clamp(settings.wallDamage,100,500);
    sv_chain_collapses=settings.chainCollapses;sv_hanging_fragments=settings.hangingFragments;sv_collapse_size=settings.collapseSize;
    if(p_fluidweapons!=settings.fluidWeapons||p_fluidmonsters!=settings.fluidMonsters) {
        P_FluidReset();resetWeaponPositions();
    }
    p_fluidweapons=settings.fluidWeapons;p_fluidmonsters=settings.fluidMonsters;
    p_persistentpursuit=settings.persistentPursuit;
    P_SetAggressiveAI(settings.aggressiveAI);
    P_SetAdvancedTactics(settings.advancedTactics);
    I_SetFluidSoundAddon(settings.fluidSounds);
    I_SetNearMissSounds(settings.nearMissSounds);
    p_fluidfastpistol=settings.fluidFastPistol;
    p_noticecombat=settings.combatNotices;
    if(gameWindow&&bool(SDL_GetWindowFlags(gameWindow)&SDL_WINDOW_FULLSCREEN)!=bool(settings.fullscreen))
        SDL_SetWindowFullscreen(gameWindow,settings.fullscreen!=0);
    R_AcceleratedView=settings.accelerated?acceleratedView:nullptr;
    r_modernhud=settings.accelerated&&settings.hudStyle!=0;
    st_helmet_style=settings.helmetFace;
    P_PeekOriginHook=I_Render3DPeekOrigin;
    R_BeforeTic=settings.accelerated?beforeTic:nullptr;
    R_WheelSlowMotion=+[]()->int{return wheelActive;};
    R_MuzzleFlash=settings.accelerated?fluidWeaponShot:nullptr;
    R_WeaponAttack=settings.accelerated?fluidWeaponAttack:nullptr;
    R_WeaponAux=settings.accelerated&&P_FluidWeaponsEnabled()?fluidWeaponAux:nullptr;
    p_destructibledoors=0; // Breaching prototype withdrawn; ignore old config values.
    R_RocketImpact=settings.accelerated?fluidProjectileImpact:nullptr;
    R_BFGHit=settings.accelerated?fluidBFGHit:nullptr;
    R_DoorMotion=settings.accelerated?doorMotion:nullptr;
    R_CraterCrumble=settings.accelerated?craterCrumble:nullptr;
    R_SurfaceChip=settings.accelerated?surfaceChip:nullptr;
    R_RubbleImpact=settings.accelerated?rubbleImpact:nullptr;
    R_SolidBulletImpact=settings.accelerated?solidBulletImpact:nullptr;
    R_DoorDamage=nullptr;
    R_WallImpact=settings.accelerated?wallImpact:nullptr;
    R_DamageHit=settings.accelerated?bodyDamaged:nullptr;
    R_BloodHit=settings.accelerated?bloodHit:nullptr;
    R_DamageHit=settings.accelerated?bodyDamaged:nullptr;
    R_PlaneImpact=settings.accelerated?planeImpact:nullptr;
    R_ShotPassed=settings.accelerated?glassShot:nullptr;
    freeaim=settings.accelerated&&settings.look;
    classicaim=settings.classicAim!=0;
    quickconfirm=settings.quickConfirm!=0;
    if(!settings.accelerated) clearFlashPulses();
    r_uncapped=settings.accelerated;
    if(!settings.look) pitch=0;
    R_SetViewSize(10,0);
    I_SetSoundOptions(settings.roomAcoustics,settings.reverbAmount,settings.muffleWalls,settings.muffleDistance,settings.naturalFalloff);
    I_SetSfxPlayback(settings.sfxResampling,settings.sfxTreble);
    I_SetMadeSounds(settings.madeImpacts,settings.glassSound,settings.ambience,settings.ambienceAmount,settings.footsteps);
    I_SetAlternativeComputerSounds(settings.alternativeComputerSounds);
    I_SetAtmosphereExtras(settings.hiddenMachinery,settings.adjacentReverb);
    I_AmbienceShowGlass(settings.accelerated&&settings.windowGlass);
    I_SetMusicDevice(settings.musicDevice);
    // The authored episode's mood is baked into the sun, lamps, bounce and
    // sky light (levelMood): switching it re-bakes them all.
    static int bakedAuthored=-1;
    if(bakedAuthored>=0&&bakedAuthored!=settings.authored) {sunBaked=false;lightBakeKey=-1;bounceKey=-1;ambientKey=-1;flowKey=-1;}
    bakedAuthored=settings.authored;
}
void sceneInit(SDL_Window *window) {
    gameWindow=window;
    int config=M_CheckParm((char*)"-graphics");
    if(config&&config+1<myargc)graphicsConfig=myargv[config+1];
    loadSettings();if(M_CheckParm((char*)"-classic"))settings.accelerated=0;
    if(M_CheckParm((char*)"-rendercheck"))settings.accelerated=1;
    int fov=M_CheckParm((char*)"-fov");if(fov&&fov+1<myargc)settings.fov=std::clamp((float)atof(myargv[fov+1]),60.0f,120.0f);
    flashlightOn=M_CheckParm((char*)"-flashlight")!=0&&Game_HasEffect(GAME_EFFECT_FLASHLIGHT);
    int tintOption=M_CheckParm((char*)"-flashlighttint");
    if(tintOption&&tintOption+1<myargc)
        settings.flashlightTintGain=std::clamp((float)atof(myargv[tintOption+1]),0.0f,2.0f);
    int fogOption=M_CheckParm((char*)"-fog");
    if(fogOption&&fogOption+1<myargc)settings.fog=atoi(myargv[fogOption+1])!=0;
    int paletteOption=M_CheckParm((char*)"-palette");
    if(paletteOption&&paletteOption+1<myargc)settings.palette=atoi(myargv[paletteOption+1])!=0;
}
void sceneShutdown() {
    arcadePodiumTexture.reset();arcade_podium_hires=0;
    arcadeRankTexture.reset();
    for(auto &texture:keyHandTextures)texture.reset();keyHandVisible=keyWeaponDrop=0;
    toastyTexture.reset();toastyVisible=0;romeroTexture.reset();romeroAge=-1;
    stopRelight();
    R_AcceleratedView=nullptr;R_BeforeTic=nullptr;R_MuzzleFlash=nullptr;R_WallImpact=nullptr;R_RocketImpact=nullptr;R_BFGHit=nullptr;R_DoorMotion=nullptr;R_DoorDamage=nullptr;R_CraterCrumble=nullptr;R_SurfaceChip=nullptr;R_SolidBulletImpact=nullptr;R_RubbleImpact=nullptr;R_DamageHit=nullptr;R_BloodHit=nullptr;R_PlaneImpact=nullptr;R_ShotPassed=nullptr;r_uncapped=0;v_overlayactive=0;
    lightBlockers.clear();flashes={};oldThings.clear();oldHeights.clear();
    vertexSections.clear();batches.clear();shadowBatches.clear();decalBatches.clear();effectBatches.clear();effectAddBatches.clear();mirrorBatches.clear();particleVertices.clear();
    mistVertices.clear();skyVertices.clear();shaftVertices.clear();heatVertices.clear();
    imagesShutdown();mapShutdown();causticsShutdown();clearSurfaceLights();lightsShutdown();glassLightShutdown();exposureShutdown();
    bakeAtlasShutdown();sunShutdown();lightBakeShutdown();clearStaticGrid();indirectShutdown();
    rubbleEmissionVertices.clear();bodyMotionShutdown();impactsShutdown();bloodShutdown();splashesShutdown();glassShutdown();shotLightsShutdown();
    weatherShutdown();wearShutdown();
}
// A new level, or the first after shutdown: each part forgets the last
// level, then the map and what is built from it at level load.
static void startLevel() {
    loadingStage(LoadStage::Map);
    stopRelight();
    vertexSections.clear();batches.clear();shadowBatches.clear();mirrorBatches.clear();clearSurfaceLights();shotLightsShutdown();
    bakeAtlasNewLevel();sunNewLevel();lightBakeNewLevel();clearStaticGrid();indirectNewLevel();
    oldThings.clear();oldHeights.clear();tickTime=0;
    resetWeaponPositions();
    bodyMotionShutdown();
    buildMap();
    prepareFalls();
    buildCaustics();
    buildShore();
    layoutBake();
    measureEmissionDensity();
    findDoors();
    prepareFalls();
    prepareRagdolls();
    levelSerial=r_levelserial;
}
static bool levelPending() {return levelSerial!=r_levelserial||floors.empty();}
bool bakesPending() {
    return (settings.bakedLights&&lightBakeKey!=lightBakeInputs())||(settings.bounce&&bounceKey!=bounceInputs())||
        ((settings.skyLight||settings.bakedAO)&&ambientKey!=bakeThingsKey)||(settings.lightFlow&&flowKey!=flowInputs())||(settings.sun&&!sunBaked);
}
bool loadPending() {return worldPending&&(levelPending()||settings.thingShadows!=bakeThingsKey||bakesPending());}
// What this frame builds and bakes, for the loading bar. A new level resets
// every bake, so each one switched on runs.
static void planStages(bool level) {
    bool lights=settings.bakedLights&&(level||lightBakeKey!=lightBakeInputs());
    planLoading(LoadStage::Map,level);planLoading(LoadStage::Textures,level);
    planLoading(LoadStage::Sun,settings.sun&&(level||!sunBaked));
    planLoading(LoadStage::Lights,lights);
    planLoading(LoadStage::Grid,settings.bakedLights&&settings.gridSpriteLight&&(lights||staticSerial!=lightBakeSerial));
    planLoading(LoadStage::Bounce,settings.bounce&&(level||lights||bounceKey!=bounceInputs()));
    planLoading(LoadStage::Ambient,(settings.skyLight||settings.bakedAO)&&(level||ambientKey!=bakeThingsKey));
    planLoading(LoadStage::Flow,settings.lightFlow&&(level||flowKey!=flowInputs()));
}
// What frameMean's alpha tells surfaceLight about a texture's material:
// 0 DOOM's palette ramps (3, stone, for Game_MatteTexture), 1 ice
// (iceFlat); with Game_SurfaceGloss, 2 a sprite (judged by colour alone),
// else 3 + its game_surface; fabric (9) takes materialGloss's matte
// default. Worn plates (wornPlate) add 16.
static_assert(GAME_SURFACE_STONE==0&&GAME_SURFACE_METAL==1&&GAME_SURFACE_WOOD==2&&GAME_SURFACE_FLESH==3&&GAME_SURFACE_SOIL==4&&GAME_SURFACE_LIQUID==5&&GAME_SURFACE_FABRIC==6,
              "materialGloss in world/surface.glsl numbers the surfaces");
static float surfaceMaterial(int key,int pic,bool flat) {
    if(flat&&iceFlat(pic))return 1;
    static std::unordered_map<int,float> known;
    auto found=known.find(key);
    if(found!=known.end())return found->second;
    char name[9]={};
    if(key>=0)R_TextureName(key,name);
    else if(flat)memcpy(name,lumpinfo[firstflat+pic].name,8);
    float material=2;
    if(!Game_SurfaceGloss())material=name[0]&&Game_MatteTexture(name,flat)?3+(float)GAME_SURFACE_STONE:0;
    else if(key>=0)material=3+(float)Game_WallSurface(name);
    else if(flat)material=3+(float)Game_FlatSurface(name);
    return known[key]=material;
}
// Plain metal plates that use has worn (Game_WornPlate; world/wear.glsl plateGrime).
static bool wornPlate(int key,int pic,bool flat) {
    static std::unordered_map<int,bool> known;
    auto found=known.find(key);
    if(found!=known.end())return found->second;
    char name[9]={};
    if(key>=0)R_TextureName(key,name);
    else if(flat)memcpy(name,lumpinfo[firstflat+pic].name,8);
    return known[key]=name[0]&&Game_WornPlate(name,flat);
}
// Binds a batch texture with its next animation frame and crossfade amount.
// The engine switches frames before leveltime++, so the phase uses leveltime-1.
SurfaceBinding surfaceBinding(int key) {
    Image &image=images.at(key);
    const Image *next=&image;float blend=0;
    int pic=key>=0?key:-1-key-firstflat,nextPic=0;
    bool flat=key<0&&pic>=0&&pic<numflats;
    int speed=key>=0||flat?P_PicAnimationNext(key>=0,pic,&nextPic):0;
    if((key>=0||flat)&&leveltime>0) {
        if(speed>0) {
            const Image &candidate=key>=0?wallImage(nextPic):lumpImage(firstflat+nextPic,true);
            if(candidate.width==image.width&&candidate.height==image.height) {
                next=&candidate;blend=((leveltime-1)%speed+renderFraction)/speed;
            }
        }
    }
    SurfaceBinding binding={&image,next,blend};
    binding.frameMean[3]=surfaceMaterial(key,pic,flat)+(settings.wornPlates&&wornPlate(key,pic,flat)?16:0);
    float floor=flat?liquidDetailFloor(pic):0;
    static unsigned meanLevel=~0u;
    static std::unordered_map<int,std::array<float,3>> means;
    if(meanLevel!=r_levelserial){means.clear();meanLevel=r_levelserial;}
    if(floor>0) {
        // The frames' average at the crossfade, and the average over the
        // whole cycle, which stays put as the frames change.
        const auto &a=averageColor(image),&b=averageColor(const_cast<Image&>(*next));
        auto found=means.find(pic);
        if(found==means.end()) {
            std::array<float,3> cycle={};std::vector<int> frames;
            for(int at=pic,n=0;n<64;++n) {
                if(std::find(frames.begin(),frames.end(),at)!=frames.end())break;
                frames.push_back(at);
                const auto &mean=averageColor(lumpImage(firstflat+at,true));
                for(int c=0;c<3;++c)cycle[c]+=mean[c];
                int following;
                if(P_PicAnimationNext(false,at,&following)<=0)break;
                at=following;
            }
            for(float &c:cycle)c/=frames.size();
            for(int frame:frames)means[frame]=cycle;
            found=means.find(pic);
        }
        for(int c=0;c<3;++c) {
            binding.frameMean[c]=a[c]+(b[c]-a[c])*blend;
            binding.cycleMean[c]=found->second[c];
        }
        binding.cycleMean[3]=floor;
    }
    return binding;
}
void screenQuad(std::vector<Vertex> &out,float x,float y,float w,float h,float u0,float v0,float u1,float v1,float light,unsigned mode) {
    quad(out,{x,y,0,u0,v0,light,mode},{x+w,y,0,u1,v0,light,mode},
        {x+w,y-h,0,u1,v1,light,mode},{x,y-h,0,u0,v1,light,mode});
}
// Profiling: this frame's lights, blockers and how many triangles they reach.
static void recordProfile() {
    I_Render3DFrameProfile &frame=profile.frame;
    frame.world=worldPending;frame.reflection=reflectionActive;frame.lights=(int)flashes.count;
    for(unsigned i=0;i<flashes.count;++i) {
        const Flash &f=flashes.lights[i];
        const mobj_t *source=lightSources[i];
        frame.light[i]={f.position[0],f.position[1],f.position[2],f.position[3],lightKinds[i],source?(int)source->type:-1,(int)f.count,0,lightSliceCost[i]};
        frame.blockers+=(int)f.count;
    }
    for(const auto &batch:batches) for(size_t n=0;n+2<batch.second.size();n+=3) {
        const unsigned *mask=batch.second[n].lightMask;
        ++frame.triangles;
        if(mask[0]|mask[1])++frame.litTriangles;
        for(unsigned i=0;i<flashes.count;++i) if(mask[i/32]&(1u<<(i%32))) {++frame.light[i].triangles;++frame.lightTriangles;}
    }
}
FrameView prepareFrame(int w,int h) {
    const bool spikes=M_CheckParm((char*)"-profilespikes")!=0;
    uint64_t diag=spikes?SDL_GetTicksNS():0;double stages[6]={};
    auto stamp=[&](int n){if(spikes){auto now=SDL_GetTicksNS();stages[n]=(now-diag)/1e6;diag=now;}};
    FrameView view={};Uniforms &camera=view.camera;
    uint64_t prepareStart=profileStart();
    static unsigned profileSerial;
    if(profile.enabled) {profile.frame=I_Render3DFrameProfile{};profile.frame.serial=++profileSerial;pitch=0;}
    camera.projection[2]=1;camera.projection[3]=32768;
    camera.style[0]=settings.ps2Style&&worldPending;
    camera.effects[0]=settings.filter;camera.effects[1]=settings.retro;
    camera.texFilter[0]=settings.sharpSoftness;camera.texFilter[1]=settings.paletteMips&&settings.filter;
    // A carried torch's colormap (Game_Torch) becomes real light (lights.cpp).
    float torchFlame;
    camera.effects[2]=Game_Torch(&torchFlame)?0:players[displayplayer].fixedcolormap;
    camera.effects[3]=settings.spriteFilter;camera.materials[0]=settings.emissive;
    unsigned features=(settings.detail?FEATURE_SURFACE_DETAIL:0)|(settings.retroReflections?FEATURE_RETRO_REFLECTIONS:0)|
        (settings.caustics&&causticTexture?FEATURE_CAUSTICS:0)|(settings.texelLight?FEATURE_TEXEL_BAKE:0)|
        (settings.caustics&&settings.ceilingCaustics&&causticTexture?FEATURE_CEILING_CAUSTICS:0)|
        (settings.lightFlow&&flowKey>=0&&flowKey==flowInputs()?FEATURE_LIGHT_FLOW:0)|
        (settings.bakedAO&&ambientKey>=0&&ambientKey==bakeThingsKey?FEATURE_BAKED_OCCLUSION:0)|
        (settings.glossyScreens?FEATURE_GLOSSY_SCREENS:0)|(settings.causticsComputed?FEATURE_CAUSTICS_COMPUTED:0)|
        (settings.causticsGrow?FEATURE_CAUSTICS_GROW:0)|(settings.causticsAngle?FEATURE_CAUSTICS_ANGLE:0)|
        (settings.causticsSway?FEATURE_CAUSTICS_SWAY:0)|(settings.causticsSprites?FEATURE_CAUSTICS_SPRITES:0)|
        (settings.causticsShots?FEATURE_CAUSTICS_SHOTS:0)|(settings.dampShores&&shoreTexture?FEATURE_DAMP_SHORES:0)|
        (settings.detail&&settings.variedHighlights?FEATURE_VARIED_HIGHLIGHTS:0)|
        (settings.windowLight&&bakedLightsActive?FEATURE_WINDOW_LIGHT:0)|
        (settings.detail&&settings.unevenTiles?FEATURE_UNEVEN_TILES:0)|(settings.detail&&settings.unevenWalls?FEATURE_UNEVEN_WALLS:0)|
        (settings.classicFlashlight?FEATURE_CLASSIC_FLASHLIGHT:0)|(settings.stippledShadows?FEATURE_STIPPLED_SHADOWS:0)|
        (settings.glowEdges&&shoreTexture&&causticPattern?FEATURE_GLOW_EDGES:0)|
        (settings.windowStreaks?FEATURE_WINDOW_STREAKS:0)|(settings.liveReadouts?FEATURE_LIVE_READOUTS:0);
    camera.flashlightTint[3]=(float)features; // see shaders/feature_flags.h
    camera.map[0]=mapOrigin[0];camera.map[1]=mapOrigin[1];camera.map[2]=1/mapCell;
    camera.map[3]=settings.softLight&&seamTexture&&worldPending;
    camera.materials[2]=settings.fog&&worldPending;
    camera.materials[3]=settings.palette&&worldPending;
    // No bloom while the in-game settings panel covers the view: it would glow
    // through the dimmed backdrop and soften the panel's text.
    camera.materials[1]=settings.emissive&&worldPending&&camera.effects[2]==0&&!I_Render3DSettingsOpen();
    camera.fx[0]=1;
    camera.eye[3]=Game_WaterClarity();
    // The episode's mood: a dark outdoors dims its sky, a little less than the ground.
    camera.mood[0]=worldPending?1+(levelMood().outdoor-1)*0.85f:1;
    for(int c=0;c<3;++c)camera.mood[1+c]=worldPending?levelMood().skyTint[c]:1; // Its sky's tint.
    float uiWidth=std::min((float)w,h*4.0f/3),uiHeight=uiWidth*0.75f,uiX=(w-uiWidth)/2;
    float uiY=worldPending&&settings.widescreen?h-uiHeight:(h-uiHeight)/2;
    float worldX=uiX+uiWidth*viewwindowx/320,worldY=uiY+uiHeight*viewwindowy/200.0f;
    float worldW=uiWidth*scaledviewwidth/320,worldH=uiHeight*viewheight/200.0f;
    if(settings.widescreen&&scaledviewwidth==320)
    { worldX=0;worldY=0;worldW=w;worldH=h-uiHeight*(1-viewheight/200.0f); }
    float referenceAspect=(4.0f/3)*(scaledviewwidth/320.0f)/(viewheight/200.0f);
    camera.projection[1]=referenceAspect/tan(settings.fov*doomPi/360);
    camera.projection[0]=camera.projection[1]/(worldW/std::max(1.0f,worldH));
    camera.fx2[0]=worldH/200;
    float fraction=1;
    if(worldPending) {
        bool level=levelPending();
        if(level) {planStages(true);startLevel();}
        syncBakeThings();
        if(!level)planStages(false);
        relightMovingSectors();
        stamp(0);
        if(settings.sun&&!sunBaked)bakeSun();
        bool sun=settings.sun&&sunLevel>0;
        for(int c=0;c<3;++c)camera.sun[c]=sun?sunTint[c]:0;
        camera.sun[3]=sun?sunLevel:0;
        camera.fx2[2]=sunYaw;camera.fx2[3]=sun&&settings.sunDisc?sunGlow:0;
        // outdoor: shade, sun gain, sky color share, the sun disc's height (Game_Sun).
        game_sun game;Game_Sun(&game);
        camera.outdoor[0]=sunShade;camera.outdoor[1]=sunGain;camera.outdoor[2]=game.skyCast;camera.outdoor[3]=sunDiscElevation;
        bakedLightsActive=settings.bakedLights&&lightBakeKey==lightBakeInputs();
        bakeOnlyActive=bakedLightsActive&&(lightBakeKey&2);
        // bake.z: 1 bake-only lights (directions and flicker groups), -1
        // flicker groups only, for lamps that switch with their sector.
        bool directions=wallDirectionTexture&&flatDirectionTexture;
        camera.bake[0]=bakedLightsActive;camera.bake[2]=directions?(bakeOnlyActive?1.0f:lampGroupCount>0?-1.0f:0.0f):0.0f;
        if(const mobj_t *viewer=players[displayplayer].mo) {
            float at[3]={units(viewer->x),units(viewer->y),0};
            updateLampGroups(at);
        }
        for(int g=0;g<flickerGroups;++g)camera.flicker[g/4][g%4]=groupFlicker(g);
        player_t *player=&players[displayplayer];mobj_t *mo=player->mo;
        // Generated material galleries identify each room without replacing any art.
        if(int lump=W_CheckNumForName((char*)"DTDGALRY");lump>=0){
            static int lastRoom=-1;static unsigned lastLevel=~0u;static char caption[80];
            if(lastLevel!=r_levelserial){lastRoom=-1;lastLevel=r_levelserial;}
            int sector=int(mo->subsector->sector-sectors),room=0;
            const byte* data=(const byte*)W_CacheLumpNum(lump,PU_CACHE);int length=W_LumpLength(lump);
            for(int at=0;at+36<=length;at+=36)if((data[at]|data[at+1]<<8)==sector){
                room=data[at+2]|data[at+3]<<8;
                if(room!=lastRoom){snprintf(caption,sizeof(caption),"%03d %.8s / %.8s / %.8s",room,data+at+4,data+at+12,data+at+20);player->message=caption;}
                break;
            }
            lastRoom=room;
        }
        camera.eye[0]=units(mo->x);camera.eye[1]=units(mo->y);camera.eye[2]=units(player->viewz);
        float yaw=(float)(mo->angle*(2.0*doomPi/4294967296.0));
        if(tickTime&&!paused&&!menuactive&&(!demoplayback||director_demo)) {
            fraction=worldInterpolation(SDL_GetTicksNS());
            if(std::hypot(camera.eye[0]-oldEye[0],camera.eye[1]-oldEye[1])<128) {
                for(int i=0;i<3;++i)camera.eye[i]=oldEye[i]+(camera.eye[i]-oldEye[i])*fraction;
                yaw=oldYaw+std::remainder(yaw-oldYaw,2*doomPi)*fraction;
            }
        }
        // A game with its own look (Heretic) sets the pitch, demos included;
        // mouselook turns that look (I_Render3DLook) so its aiming agrees.
        float gameLook;
        float look=Game_LookPitch(&gameLook)?gameLook:demoplayback&&!director_demo?0:pitch;
        tourpitch=look;freeaim=settings.look&&(!demoplayback||director_demo);
        camera.right[0]=sin(yaw);camera.right[1]=-cos(yaw);
        camera.forward[0]=cos(yaw)*cos(look);camera.forward[1]=sin(yaw)*cos(look);camera.forward[2]=sin(look);
        camera.up[0]=-cos(yaw)*sin(look);camera.up[1]=-sin(yaw)*sin(look);camera.up[2]=cos(look);
        applyPeek(camera);directorView(camera,float(fraction));
        camera.fog[0]=0.00045f*(player->mo->subsector->sector->ceilingpic==skyflatnum?0.35f:1.0f);
        camera.fog[1]=0.012f;camera.fog[2]=fogReferenceHeight;
        camera.fog[3]=(leveltime+fraction)/TICRATE;
        // Weather and wear (weather.cpp, wear.cpp): where wear.glsl finds its
        // records, how wet the windows and the ground under the sky are.
        camera.right[3]=wearBase();
        camera.forward[3]=settings.weather?glassWetNow:0;
        camera.up[3]=settings.weather?wetNow:0;
        // The far rain sky.frag draws over the sky (weather.cpp's rain stops at the map's sky).
        camera.weather[0]=settings.weather&&camera.effects[2]==0?rainNow:0;
        camera.weather[1]=rainWind[0];camera.weather[2]=rainWind[1];
        renderFraction=fraction;
        // Caustics follow the liquid's frame and crossfade like its floor (surfaceBinding).
        if(!causticFrames.empty()&&causticSpeed>0&&leveltime>0) {
            auto at=std::find(causticFrames.begin(),causticFrames.end(),flattranslation[causticFrames[0]]);
            camera.texFilter[2]=at!=causticFrames.end()?float(at-causticFrames.begin()):0;
            camera.texFilter[3]=((leveltime-1)%causticSpeed+fraction)/causticSpeed;
        }
        uint64_t geometryStart=profileStart();
        stamp(1);
        if(level)loadingStage(LoadStage::Textures);
        geometry(camera,fraction);
        stamp(2);
        profileAdd(profile.frame.geometryMs,geometryStart);
        if(profile.enabled)skipProfiledLights();
        // Surface lights come from this frame's geometry; the bake applies from the next frame.
        if(settings.bakedLights&&lightBakeKey!=lightBakeInputs())bakeLights();
        if(settings.bakedLights&&settings.gridSpriteLight&&lightBakeKey==lightBakeInputs()&&staticSerial!=lightBakeSerial)bakeStaticGrid();
        if(settings.bounce&&bounceKey!=bounceInputs())bakeBounce();
        if((settings.skyLight||settings.bakedAO)&&ambientKey!=bakeThingsKey)bakeAmbient();
        if(settings.lightFlow&&flowKey!=flowInputs())bakeFlow();
        camera.bake[1]=settings.bounce&&bounceKey>=0?bounceStrength:0;
        camera.bake[3]=camera.bake[1]>0?settings.bounceTint:0;
        bool ambient=ambientKey==bakeThingsKey;
        camera.fx2[1]=settings.skyLight&&ambient?skyLevel:0;
        // The fog haze takes the sky's color too, so it follows the sky without the sky-light bake.
        static int tintedSky=-1;
        static std::array<float,3> horizon;
        if(skytexture!=tintedSky){skyTint=skyColor();horizon=skyHorizon();tintedSky=skytexture;}
        for(int c=0;c<3;++c)camera.fx[1+c]=skyTint[c];
        // Outdoor haze in the sky's horizon color (Game_Sun), with the fog.
        for(int c=0;c<3;++c)camera.haze[c]=horizon[c]*camera.mood[0]*camera.mood[1+c];
        camera.haze[3]=settings.fog?game.haze:0;
        buildMist(camera);
        buildShafts(camera);
        buildRings(camera);
        updateExposure(camera);
        updateFlashlightTint(camera);
        // The Torch item's faint lift over the whole view, flickering with
        // the flame; most of its light is the carried flame (lights.cpp).
        if(torchFlame>0)for(int c=0;c<3;++c)camera.flashlightTint[c]+=0.1f*torchFlame*torchColor()[c];
        selectFogLights(camera);
        if(profile.skip&I_RENDER3D_SKIP_MIST) {mistVertices.clear();shaftVertices.clear();}
        if(profile.skip&I_RENDER3D_SKIP_FOG) {fogLights={};camera.materials[2]=0;}
        if(profile.skip&I_RENDER3D_SKIP_BLOOM) camera.materials[1]=0;
    }
    if(!worldPending) {mistVertices.clear();shaftVertices.clear();heatVertices.clear();skyVertices.clear();}
    uint64_t masksStart=profileStart();
    stamp(3);
    assignLightMasks();
    stamp(4);
    profileAdd(profile.frame.masksMs,masksStart);
    uint64_t wordsStart=profileStart();
    buildLightWords();
    stamp(5);
    if(spikes) {
        double sum=0;for(double t:stages)sum+=t;
        if(sum>14)fprintf(stderr,"Prepare spike tic %d: relight %.3f camera %.3f geometry %.3f effects %.3f masks %.3f packing %.3f ms\n",gametic,stages[0],stages[1],stages[2],stages[3],stages[4],stages[5]);
    }
    profileAdd(profile.frame.lightsMs,wordsStart);
    if(profile.enabled) {recordProfile();profileAdd(profile.frame.prepareMs,prepareStart);}
    view.uiX=uiX;view.uiY=uiY;view.uiWidth=uiWidth;view.uiHeight=uiHeight;
    view.worldX=worldX;view.worldY=worldY;view.worldW=worldW;view.worldH=worldH;
    return view;
}
std::vector<unsigned> hudPixels(const unsigned *pixels) {
    float podiumRect[4];
    if(Arcade_PodiumRect(podiumRect)&&!arcadePodiumTexture){
        SDL_Surface *png=SDL_LoadPNG_IO(SDL_IOFromConstMem(arcade_podium_png,sizeof(arcade_podium_png)),true);
        SDL_Surface *rgba=png?SDL_ConvertSurface(png,SDL_PIXELFORMAT_RGBA32):nullptr;
        SDL_DestroySurface(png);
        if(rgba){arcadePodiumTexture=gpuCreateTexture(GpuFormat::RGBA8,rgba->w,rgba->h,rgba->pixels,rgba->pitch);SDL_DestroySurface(rgba);}
        arcade_podium_hires=arcadePodiumTexture?1:0;
    }
    std::vector<unsigned> overlay(pixels,pixels+SCREENWIDTH*SCREENHEIGHT);
    if(worldPending) for(int i=0;i<SCREENWIDTH*SCREENHEIGHT;++i)overlay[i]=(overlay[i]&0xffffff)|(unsigned)v_overlaymask[i]<<24;
    if(Arcade_MenuOpen()||Menu_BrandOpen()){
        std::vector<unsigned> ranks(SCREENWIDTH*SCREENHEIGHT,0);
        // Render-time animation avoids the old eight-tic colour jumps.
        double time=SDL_GetTicksNS()*1e-9;
        for(int i=0;i<SCREENWIDTH*SCREENHEIGHT;++i)if(arcade_rank_pixels[i]){
            unsigned tag=arcade_rank_pixels[i];int rank=tag>>24,x=i%SCREENWIDTH,y=i/SCREENWIDTH;
            float r,g,b;
            if(rank==1||rank==4){
                float hue=float(time*(rank==4?.10:.19)+x*(rank==4?.009:.035));hue-=std::floor(hue);
                auto channel=[&](float offset){float h=std::fmod(hue+offset,1.f);return .24f+.76f*std::clamp(std::abs(h*6-3)-1,0.f,1.f);};
                r=channel(0);g=channel(2.f/3);b=channel(1.f/3);
                float shine=.18f*std::pow(std::max(0.f,std::sin(float(time*2.2-x*.25))),12.f);
                r+=shine*(1-r);g+=shine*(1-g);b+=shine*(1-b);
            }else if(rank==2){
                float shine=std::pow(std::max(0.f,std::sin(float(time*2.1-x*.22))),18.f);
                r=.70f+.22f*shine;g=.22f+.40f*shine;b=1.f;
            }else{
                // Silver remains quieter than the animated top two.
                float silver=.69f+.12f*(1+std::cos(y*.8f))*.5f;
                r=g=b=silver;
            }
            float shade=(tag&255)==2?1.f:.75f;
            ranks[i]=0xff000000u|((unsigned)(b*shade*255)<<16)|((unsigned)(g*shade*255)<<8)|(unsigned)(r*shade*255);
        }
        if(!arcadeRankTexture)arcadeRankTexture=gpuCreateTexture(GpuFormat::RGBA8,SCREENWIDTH,SCREENHEIGHT,ranks.data(),SCREENWIDTH*4);
        else gpuUpdateTexture(arcadeRankTexture,GpuFormat::RGBA8,{{0,0,SCREENWIDTH,SCREENHEIGHT}},ranks.data(),SCREENWIDTH*4);
    }
    std::vector<unsigned> modern(SCREENWIDTH*SCREENHEIGHT,0);
    if((director_watch||director_demo&&demoplayback)&&!menuactive){std::fill(overlay.begin(),overlay.end(),0);}
    else modernHud(modern);
    directorMarkers(overlay);
    if(!(director_demo&&demoplayback)){
    keyNotice(overlay);
    keyHandNotice();
    toastyNotice();
    romeroNotice();
    wheelDraw(overlay);
    }else {keyHandVisible=0;toastyVisible=0;romeroAge=-1;}
    if(settings.fps&&worldPending&&!menuactive&&fps>0&&!(director_demo&&demoplayback)) {
        // Readable in fullscreen, independent of HUD layout and scale.
        char lines[2][48];snprintf(lines[0],48,"%.0f FPS",fps);
        snprintf(lines[1],48,"%.1f MS / MAX %.1f",frameMs,peakFrameMs);
        auto glyph=[](char c)->const Image* {
            if(c==' ')return nullptr;
            char name[9];snprintf(name,sizeof(name),"STCFN%03d",int((unsigned char)c));
            int lump=W_CheckNumForName(name);return lump<0?nullptr:&lumpImage(lump,false);
        };
        const byte *pal=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
        for(int row=0;row<2;++row) {
            int width=0;for(char c:std::string(lines[row])){auto im=glyph(c);width+=im?im->width:4;}
            int x=(SCREENWIDTH-width)/2,y=3+row*12+(arcade_active?36:0);
            for(int yy=y-1;yy<std::min(SCREENHEIGHT,y+10);++yy)
                for(int xx=std::max(0,x-3);xx<std::min(SCREENWIDTH,x+width+3);++xx)overlay[yy*SCREENWIDTH+xx]=0xd010151a;
            for(char c:std::string(lines[row])) {
                auto im=glyph(c);if(!im){x+=4;continue;}
                for(int yy=0;yy<im->height;++yy)for(int xx=0;xx<im->width;++xx) {
                    size_t at=(yy*im->width+xx)*2;
                    if(!im->pixels[at+1]||x+xx<0||x+xx>=SCREENWIDTH||y+yy>=SCREENHEIGHT)continue;
                    auto rgb=pal+im->pixels[at]*3;unsigned shade=std::max({rgb[0],rgb[1],rgb[2]});
                    overlay[(y+yy)*SCREENWIDTH+x+xx]=0xff000000|(shade<<16)|(shade<<8)|shade;
                }
                x+=im->width;
            }
        }
    }
    overlay.insert(overlay.end(),modern.begin(),modern.end());
    return overlay;
}
// Current sector light and heights for seam blending and contact shading;
// w is 0 indoors, and under a sky packOutdoor.
std::vector<std::array<float,4>> sectorInfo() {
    std::vector<std::array<float,4>> info(std::max(1,numsectors));
    if(worldPending)for(int i=0;i<numsectors;++i)
        info[i]={lighting(&sectors[i]),floorZ(&sectors[i]),ceilZ(&sectors[i]),
            sectors[i].ceilingpic==skyflatnum?packOutdoor(&sectors[i]):0.0f};
    if(worldPending)appendWear(info);
    return info;
}
// -rendercheck <png> saves the first 3D frame after tic 10 (or -rendertic N)
// and quits; -rendersettings opens the F4 panel for that frame first.
// -renderframes M -renderevery K: M frames K tics apart, laid out as one contact
// sheet (-rendersheet columns, default 4) of the 3D view cropped to
// -rendercrop x0,y0,x1,y1 (shares of the frame) and scaled by -renderscale.
static int renderArg(const char *name,int fallback) {
    int at=M_CheckParm((char*)name);
    return at&&at+1<myargc?atoi(myargv[at+1]):fallback;
}
static int renderFramesTaken=0;
bool renderCheckDue() {
    int after=renderArg("-rendertic",10)+renderFramesTaken*std::max(1,renderArg("-renderevery",1));
    bool due=M_CheckParm((char*)"-rendercheck")&&worldPending&&gametic>after;
    if(due&&M_CheckParm((char*)"-rendersettings")&&!I_Render3DSettingsOpen()) {I_Render3DSettingsMenu();return false;}
    return due;
}
void saveScreenshot(const void *bgra,int width,int height,int stride) {
    SDL_CreateDirectory("Screenshots");
    char path[256];snprintf(path,sizeof(path),"Screenshots/%s-%llu.png",Game_Title(),(unsigned long long)SDL_GetTicksNS());
    int test=M_CheckParm((char*)"-rendercheck");
    const char *output=test&&test+1<myargc?myargv[test+1]:profile.screenshotPath?profile.screenshotPath:path;
    profile.screenshotPath=nullptr;
    SDL_Surface *surface=bgra?SDL_CreateSurfaceFrom(width,height,SDL_PIXELFORMAT_BGRA32,(void*)bgra,stride):nullptr;
    int frames=test?renderArg("-renderframes",1):1;
    if(frames>1&&surface) {
        static SDL_Surface *sheet=nullptr;
        float crop[4]={0.1f,0.2f,0.9f,0.84f},scale=0.25f;
        int at=M_CheckParm((char*)"-rendercrop");
        if(at&&at+1<myargc)sscanf(myargv[at+1],"%f,%f,%f,%f",&crop[0],&crop[1],&crop[2],&crop[3]);
        at=M_CheckParm((char*)"-renderscale");
        if(at&&at+1<myargc)scale=(float)atof(myargv[at+1]);
        SDL_Rect from={(int)(crop[0]*width),(int)(crop[1]*height),(int)((crop[2]-crop[0])*width),(int)((crop[3]-crop[1])*height)};
        int columns=std::max(1,renderArg("-rendersheet",4)),cw=std::max(1,(int)(from.w*scale)),ch=std::max(1,(int)(from.h*scale)),gap=4;
        if(!sheet) {
            int rows=(frames+columns-1)/columns;
            sheet=SDL_CreateSurface(columns*(cw+gap)+gap,rows*(ch+gap)+gap,SDL_PIXELFORMAT_BGRA32);
            SDL_FillSurfaceRect(sheet,nullptr,SDL_MapSurfaceRGB(sheet,24,24,28));
        }
        int n=renderFramesTaken++;
        SDL_Rect to={gap+(n%columns)*(cw+gap),gap+(n/columns)*(ch+gap),cw,ch};
        SDL_BlitSurfaceScaled(surface,&from,sheet,&to,SDL_SCALEMODE_LINEAR);
        SDL_DestroySurface(surface);
        fprintf(stderr,"Render frame %d of %d at tic %d.\n",n+1,frames,gametic);
        if(renderFramesTaken<frames)return;
        surface=sheet;
    }
    bool saved=surface&&SDL_SavePNG(surface,output);SDL_DestroySurface(surface);
    if(!test) players[consoleplayer].message=(char*)(saved?"SCREENSHOT SAVED":"SCREENSHOT FAILED");
    fprintf(stderr,"Screenshot %s: %s (%dx%d).\n",saved?"saved":"failed",output,width,height);
    if(test) {if(!saved)I_Error((char*)"Render check could not save image");I_Quit();}
}
void updateTitle(int width,int height,const char *renderer) {
    ++frameCount;
    uint64_t now=SDL_GetTicksNS();
    if(previousFrame)framePeak=std::max(framePeak,float((now-previousFrame)/1e6));
    previousFrame=now;
    if(!titleTime){titleTime=now;frameCount=0;}
    if(now-titleTime>1000000000) {
        fps=frameCount*1e9/(now-titleTime);frameMs=1000.f/std::max(fps,1.f);peakFrameMs=framePeak;framePeak=0;
        frameCount=0;titleTime=now;
        char rate[32]="";if(settings.fps)snprintf(rate,sizeof(rate)," · %.0f fps",fps);
        char title[256];snprintf(title,sizeof(title),"%s · %s · %dx%d%s%s · F4: graphics · F7: screenshot · Esc: menu",
            Game_Title(),settings.accelerated?renderer:"Classic",width,height,rate,
            Game_HasEffect(GAME_EFFECT_FLASHLIGHT)?" · F: flashlight":"");
        SDL_SetWindowTitle(gameWindow,title);
    }
}
} // namespace doom3d


// Backend-independent parts of the renderer interface.
using namespace doom3d;
void I_Render3DStartFrame(void) {worldPending=false;v_overlayactive=false;}
#ifdef DOOM_TEST_DRIVER
extern "C" void I_TestBundledModSettings() {
    int weapons=settings.fluidWeapons,sounds=settings.fluidSounds,monsters=settings.fluidMonsters;
    int digSetting=settings.breakableWalls;
    for(int value=0;value<2;++value){settings.breakableWalls=value;applySettings();if(p_breakablewalls!=value)I_Error((char*)"Breakable walls live toggle failed");}
    settings.breakableWalls=digSetting;applySettings();
    int throughSetting=settings.throughWalls;
    for(int value=0;value<2;++value){settings.throughWalls=value;applySettings();if(p_throughwalls!=value)I_Error((char*)"Through-wall breach live toggle failed");}
    settings.throughWalls=throughSetting;applySettings();
    int doorsSetting=settings.breachDoors;
    for(int value=0;value<2;++value){settings.breachDoors=value;applySettings();if(p_breachdoors!=value)I_Error((char*)"Door breach live toggle failed");}
    settings.breachDoors=doorsSetting;applySettings();
    int damageSetting=settings.wallDamage;
    for(int value:{100,250,500}){settings.wallDamage=value;applySettings();if(p_wall_damage!=value)I_Error((char*)"Wall damage live setting failed");}
    settings.wallDamage=damageSetting;applySettings();
    int deeperSetting=settings.deeperDig;
    for(int value=0;value<2;++value){settings.deeperDig=value;applySettings();if(p_deeperdig!=value)I_Error((char*)"Deeper digging live toggle failed");}
    settings.deeperDig=deeperSetting;applySettings();
    int aggressiveSetting=settings.aggressiveAI;
    for(int value=0;value<2;++value){settings.aggressiveAI=value;applySettings();if(p_aggressiveai!=value)I_Error((char*)"Aggressive AI live toggle failed");}
    settings.aggressiveAI=aggressiveSetting;applySettings();
    int pursuitSetting=settings.persistentPursuit;
    for(int value=0;value<2;++value){settings.persistentPursuit=value;applySettings();if(p_persistentpursuit!=value)I_Error((char*)"Pursuit live toggle failed");}
    settings.persistentPursuit=pursuitSetting;applySettings();
    bool hq=ModSounds_Has("MRIntW/HQ/ShotgunPumpDown");
    for(int mask=0;mask<8;++mask) {
        settings.fluidWeapons=mask&1;settings.fluidSounds=(mask>>1)&1;settings.fluidMonsters=(mask>>2)&1;applySettings();
        if(bool(P_FluidWeaponsEnabled())!=bool((mask&1)&&M_CheckParm((char*)"-fluidweapons")))I_Error((char*)"Weapon toggle failed");
        if(bool(P_FluidEnabled())!=bool((mask&4)&&M_CheckParm((char*)"-fluidmonsters")&&!demoplayback&&!demorecording&&!netgame))I_Error((char*)"Monster toggle failed");
        if(hq&&bool(ModSounds_Has("MRIntW/HQ/ShotgunPumpDown"))!=bool(mask&2))I_Error((char*)"SFX addon toggle failed");
        if(M_CheckParm((char*)"-modsounds")) {
            mod_sound_view sample;char error[512];
            const char *name=(hq&&(mask&2))?"MRIntW/HQ/PistolFire":"MRIntW/PistolFire";
            if(!ModSounds_Get(name,0,&sample,error,sizeof(error)))I_Error((char*)"Sound toggle decode: %s",error);
        }
    }
    settings.fluidWeapons=weapons;settings.fluidSounds=sounds;settings.fluidMonsters=monsters;applySettings();
    puts("Bundled mods: all eight live toggle combinations and sound-bank switching passed.");
}
extern "C" void I_TestSmoothActor(mobj_t *actor){
    float step=actor->info->speed/4.f;
    for(float fraction:{0.f,.25f,.5f,.75f,1.f}){
        float x,y,z;interpolatedPosition(*actor,fraction,x,y,z);
        if(std::abs(x-(units(actor->x)-step+step*fraction))>.0001f)
            I_Error((char*)"Monster render interpolation skipped a subframe");
    }
}
#endif
int I_Render3DWheelOpen(void){return wheelActive;}
int I_Render3DPeekKey(int scancode,int down){
    if(!peekAllowed())return 0;
    int direction=scancode&&scancode==settings.peekLeft?-1:scancode&&scancode==settings.peekRight?1:0;
    if(!direction)return 0;
    if(peekLevel!=r_levelserial||peekPlayer!=displayplayer)resetPeek();
    peekLevel=r_levelserial;peekPlayer=displayplayer;
    if(direction<0)peekHeldLeft=down!=0;else peekHeldRight=down!=0;
    peekDirection=int(peekHeldRight)-int(peekHeldLeft);
    return 1;
}
void I_Render3DPeekReset(void){resetPeek();}
extern "C" int I_Render3DPeekOrigin(mobj_t *actor,fixed_t *x,fixed_t *y,fixed_t *z){
    if(!peekAllowed()||actor!=players[displayplayer].mo||peekLevel!=r_levelserial||std::abs(peekAmount)<.001f)return 0;
    float yaw=double(actor->angle)*(6.283185307179586/4294967296.0);
    float sign=peekAmount<0?-1.f:1.f,dx=std::sin(yaw)*sign,dy=-std::cos(yaw)*sign;
    float angle=std::abs(peekAmount)/24.f*.785398163f;
    float reach=34.f*std::sin(angle),drop=34.f*(1-std::cos(angle));
    float eye[3]={units(actor->x),units(actor->y),units(actor->player->viewz)};
    float amount=peekClearance(eye,dx,dy,reach,-drop/reach);
    *x=actor->x+fixed_t(dx*amount*FRACUNIT);*y=actor->y+fixed_t(dy*amount*FRACUNIT);
    *z=actor->player->viewz-fixed_t(drop*(amount/reach)*FRACUNIT);
    return 1;
}

#ifdef DOOM_TEST_DRIVER
extern "C" void I_TestCornerPeek(void){
    Settings saved=settings;settings.cornerPeek=settings.accelerated=1;
    settings.peekLeft=SDL_SCANCODE_Z;settings.peekRight=SDL_SCANCODE_C;
    I_Render3DPeekReset();
    mobj_t *m=players[displayplayer].mo;fixed_t x=m->x,y=m->y,z=m->z;
    if(!I_Render3DPeekKey(SDL_SCANCODE_Z,1)||peekDirection!=-1)I_Error((char*)"Peek left press failed");
    I_Render3DPeekKey(SDL_SCANCODE_Z,1);
    if(peekDirection!=-1)I_Error((char*)"Peek repeat toggled hold");
    Uniforms view{};view.eye[0]=units(x);view.eye[1]=units(y);view.eye[2]=units(players[displayplayer].viewz);view.right[0]=1;view.up[2]=1;
    peekClock=SDL_GetTicksNS()-50000000;applyPeek(view);
    if(peekAmount>=0||std::abs(peekAmount)>8.1f)I_Error((char*)"Peek smoothing failed");
    I_Render3DPeekKey(SDL_SCANCODE_C,1);if(peekDirection!=0)I_Error((char*)"Opposed peek keys should center");
    I_Render3DPeekKey(SDL_SCANCODE_Z,0);if(peekDirection!=1)I_Error((char*)"Remaining held peek key lost");
    I_Render3DPeekKey(SDL_SCANCODE_C,0);if(peekDirection!=0)I_Error((char*)"Peek release failed");
    for(int i=0;i<4;++i){peekClock=SDL_GetTicksNS()-50000000;applyPeek(view);}
    if(peekAmount!=0||m->x!=x||m->y!=y||m->z!=z)I_Error((char*)"Peek moved player or failed to return");
    I_Render3DPeekKey(SDL_SCANCODE_Z,1);paused=true;applyPeek(view);paused=false;
    if(peekDirection||peekHeldLeft||peekAmount)I_Error((char*)"Peek stuck after pause");
    settings.cornerPeek=0;if(I_Render3DPeekKey(SDL_SCANCODE_Z,1))I_Error((char*)"Disabled peek consumed key");settings.cornerPeek=1;
    demoplayback=true;if(I_Render3DPeekKey(SDL_SCANCODE_Z,1))I_Error((char*)"Peek active during demo");demoplayback=false;
    settings.peekLeft=SDL_SCANCODE_V;
    if(I_Render3DPeekKey(SDL_SCANCODE_Z,1)||!I_Render3DPeekKey(SDL_SCANCODE_V,1))I_Error((char*)"Peek remapping failed");
    I_Render3DPeekReset();
    int checked=0;
    for(int i=0;i<numlines&&!checked;++i){const line_t &line=lines[i];if(line.backsector||!line.frontsector)continue;
        float dx=units(line.dx),dy=units(line.dy),length=std::hypot(dx,dy);if(length<64)continue;
        float nx=dy/length,ny=-dx/length;
        float eye[3]={(units(line.v1->x)+units(line.v2->x))*.5f+nx*8,(units(line.v1->y)+units(line.v2->y))*.5f+ny*8,units(line.frontsector->floorheight)+41};
        if(R_PointInSubsector(fixed_t(eye[0]*FRACUNIT),fixed_t(eye[1]*FRACUNIT))->sector!=line.frontsector)continue;
        float amount=peekClearance(eye,-nx,-ny,24);
        if(amount>5.1f)I_Error((char*)"Peek camera passed solid wall");checked=1;
    }
    if(!checked)I_Error((char*)"No wall fixture for peek camera");
    // Full lean in open space must reach 12 degrees while preserving an
    // orthonormal view basis; collision reduces the lean instead of clipping.
    int open=0;
    float eye[3]={units(x),units(y),units(players[displayplayer].viewz)};
    for(int n=0;n<8&&!open;++n){float a=n*.785398163f,dx=std::cos(a),dy=std::sin(a);
        if(peekClearance(eye,dx,dy,24.04163f,-.41421356f)<24.04f)continue;
        I_Render3DPeekKey(SDL_SCANCODE_C,1);
        for(int step=0;step<4;++step){view=Uniforms{};std::copy(eye,eye+3,view.eye);view.right[0]=dx;view.right[1]=dy;view.up[2]=1;
            peekClock=SDL_GetTicksNS()-50000000;applyPeek(view);}
        if(std::abs(cornerPeekRoll-.20943951f)>.001f||view.eye[2]>=eye[2]-9.9f)I_Error((char*)"Full peek did not use gentle 12-degree tilt along arc");
        float dot=0,len=0;for(int i=0;i<3;++i){dot+=view.right[i]*view.up[i];len+=view.right[i]*view.right[i];}
        if(std::abs(dot)>.001f||std::abs(len-1)>.001f)I_Error((char*)"Peek distorted camera basis");
        angle_t oldAngle=m->angle;
        double yaw=std::atan2(double(dx),double(-dy));if(yaw<0)yaw+=6.283185307179586;
        m->angle=angle_t(yaw*(4294967296.0/6.283185307179586));
        fixed_t sx=x,sy=y,sz=z;
        if(!P_PeekShotOrigin(m,&sx,&sy,&sz)||std::abs(units(sx)-view.eye[0])>.01f||std::abs(units(sy)-view.eye[1])>.01f||std::abs(units(sz)-view.eye[2])>.01f)
            I_Error((char*)"Peek firing origin does not match camera");
        P_LineAttack(m,m->angle,64*FRACUNIT,0,0);
        if(std::abs(trace.x-sx)>FRACUNIT||std::abs(trace.y-sy)>FRACUNIT)I_Error((char*)"Hitscan did not use leaning origin");
        if(cornerPeekAim[0]||cornerPeekAim[1])I_Error((char*)"Peek crosshair left screen centre");
        m->angle=oldAngle;
        open=1;I_Render3DPeekReset();
        sx=x;sy=y;sz=z;
        if(P_PeekShotOrigin(m,&sx,&sy,&sz)||sx!=x||sy!=y||sz!=z)I_Error((char*)"Inactive peek changed shot origin");
    }
    if(!open)I_Error((char*)"No open-space fixture for full lean");
    Settings config{};readOption(config,"peek_left",SDL_SCANCODE_V);readOption(config,"corner_peek",0);
    readOption(config,"peek_right",SDL_SCANCODE_F4);
    if(config.peekLeft!=SDL_SCANCODE_V||config.cornerPeek||config.peekRight!=SDL_SCANCODE_C)I_Error((char*)"Peek config validation failed");
    FILE *file=tmpfile();if(!file)I_Error((char*)"Peek config test file failed");
    writeOptions(file,config);rewind(file);Settings restored{};char key[128];double value;
    while(fscanf(file,"%127s %lf",key,&value)==2)readOption(restored,key,value);
    fclose(file);
    if(restored.peekLeft!=config.peekLeft||restored.peekRight!=config.peekRight||restored.cornerPeek!=config.cornerPeek)I_Error((char*)"Peek bindings did not persist");
    settings=saved;I_Render3DPeekReset();
}
#endif
extern "C" int I_TestWheelInterpolation(void){
    uint64_t saved=tickTime,now=SDL_GetTicksNS();
    bool wasOpen=wheelActive;wheelActive=true;tickTime=now;
    worldInterpolation(now);
    int ok=1;
    for(int i=1;i<=5;++i){
        double f=worldInterpolation(now+uint64_t(i)*100000000);
        if(std::abs(f-i*.175)>.0001)ok=0;
    }
    wheelActive=wasOpen;tickTime=saved;
    worldInterpolation(SDL_GetTicksNS());return ok;
}
void I_Render3DWheelKey(int down){
    if(down>0){if(gamestate!=GS_LEVEL||menuactive||paused||demoplayback||demorecording||netgame||players[consoleplayer].health<=0)return;wheelActive=true;wheelLevel=r_levelserial;wheelSelected=players[consoleplayer].readyweapon;wheelX=wheelY=0;}
    else {if(down==0&&wheelActive&&gamestate==GS_LEVEL&&!menuactive&&!paused&&wheelLevel==r_levelserial&&players[consoleplayer].health>0&&wheelOwned(wheelSelected)&&wheelSelected!=players[consoleplayer].readyweapon)players[consoleplayer].pendingweapon=(weapontype_t)wheelSelected;wheelActive=false;}
}
void I_Render3DWheelMove(float x,float y){if(!wheelActive)return;wheelX+=x*.35f;wheelY+=y*.35f;float radius=std::hypot(wheelX,wheelY*1.2f);if(radius>76){wheelX*=76/radius;wheelY*=76/radius;}if(radius>12){float angle=std::atan2(wheelY*1.2f,wheelX)+1.570796327f+3.141592654f/9;wheelSelected=int(std::floor(angle/(6.283185307f/9)+9))%9;}}
int I_Render3DIsAccelerated(void) {return settings.accelerated;}
int I_Render3DModernHUD(void) {return settings.accelerated&&settings.hudStyle!=0;}
void I_Render3DFullscreenChanged(int enabled) {settings.fullscreen=enabled!=0;}
void I_Render3DLook(float delta) {
    if(!settings.look||!settings.accelerated||demoplayback)return;
    if(settings.invertLook)delta=-delta;
    float gameLook;
    if(Game_LookPitch(&gameLook))Game_Look(-delta*0.002f);
    else pitch=std::clamp(pitch-delta*0.002f,P_UseUZMovement()?-1.55334f:-1.2f,P_UseUZMovement()?1.55334f:1.2f);
}
void I_Render3DToggleFlashlight(void) {if(settings.accelerated&&Game_HasEffect(GAME_EFFECT_FLASHLIGHT))flashlightOn=!flashlightOn;}
void I_Render3DScreenshot(void) {screenshotPending=true;}
const I_Render3DFrameProfile *I_Render3DLastFrame(void) {return &profile.frame;}
void I_Render3DProfileTeleported(void) {surfaceSelection.clear();surfaceLightTime=0;}
void I_Render3DScreenshotTo(const char *path) {profile.screenshotPath=path;screenshotPending=true;}



// Regression fixture invoked by the GPU smoke harness with a real WAD loaded.
extern "C" void I_TestCombatPresentation(void) {
    using namespace doom3d;
    Settings saved=settings;int oldtime=leveltime;
    settings.hitFlinch=settings.deathMotion=settings.deathDirector=settings.deathFalls=settings.wounds=1;
    mobj_t *player=players[consoleplayer].mo;
    const int types[]={MT_POSSESSED,MT_SHOTGUY,MT_CHAINGUY,MT_TROOP,MT_SERGEANT,MT_HEAD,MT_KNIGHT,MT_BRUISER,MT_UNDEAD,MT_VILE,MT_FATSO};
    int directed=0,procedural=0;
    for(int type:types){
        mobj_t *actor=P_SpawnMobj(player->x+96*FRACUNIT,player->y,ONFLOORZ,(mobjtype_t)type);
        watchBodies();int health=actor->health;
        P_DamageMobj(actor,player,player,5);++leveltime;watchBodies();
        ++leveltime;BodyPose pose;if(!bodyPose(*actor,0.5f,pose))I_Error((char*)"Hit reaction missing");
        if(actor->health!=health-5)I_Error((char*)"Presentation changed damage");
        P_DamageMobj(actor,player,player,actor->health+1);++leveltime;watchBodies();
        for(int tick=0;tick<8;++tick){
            DeathView view={actor->sprite,actor->frame,actor->angle,false,1,0,{}};
            if(deathView(*actor,0.5f,view)){
                ++directed;if(view.fall.active)++procedural;
                if(view.fall.rig){std::vector<Vertex> vertices;fallQuads(vertices,{},view.fall,0,0,0,1,0,false);if(vertices.empty())I_Error((char*)"Procedural fall has no geometry");}
            }
            ++leveltime;
        }
        settings.deathDirector=settings.deathMotion=0;
        DeathView off={actor->sprite,actor->frame,actor->angle,false,1,0,{}};
        if(deathView(*actor,.5f,off)||bodyPose(*actor,.5f,pose))I_Error((char*)"Disabled death effects still draw a cached pose");
        settings.deathDirector=settings.deathMotion=1;
        P_RemoveMobj(actor);watchBodies();
    }
    settings.deathDirector=settings.deathFalls=settings.deathMotion=0;
    mobj_t *flinching=P_SpawnMobj(player->x+96*FRACUNIT,player->y,ONFLOORZ,MT_POSSESSED);
    watchBodies();P_DamageMobj(flinching,player,player,5);++leveltime;watchBodies();++leveltime;
    BodyPose pose;
    if(!bodyPose(*flinching,.5f,pose))I_Error((char*)"Living flinch fixture missing");
    P_DamageMobj(flinching,player,player,flinching->health+1);
    if(bodyPose(*flinching,.5f,pose))I_Error((char*)"Living flinch leaked into classic death before watcher");
    ++leveltime;watchBodies();
    if(bodyPose(*flinching,.5f,pose))I_Error((char*)"Classic death gained body motion");
    P_RemoveMobj(flinching);watchBodies();
    puts("PASS classic deaths ignore cached falls and living hit flinches with death effects off");
    if(!directed||!procedural)I_Error((char*)"Varied/procedural deaths not exercised");
    settings=saved;leveltime=oldtime;bodyMotionShutdown();
    printf("Combat presentation: real monster damage, flinches, varied deaths and procedural geometry passed.\n");
}

extern "C" void I_TestEffectsPreview(int t){
    using namespace doom3d;static mobj_t *actors[3]={};
    static int last=-1;if(t==last)return;last=t;players[consoleplayer].cheats|=CF_GODMODE;
    mobj_t *p=players[consoleplayer].mo;if(!p)return;
    float angle=(float)(p->angle*(2*doomPi/4294967296.0)),dx=std::cos(angle),dy=std::sin(angle);
    if(t==20){
        int types[]={MT_TROOP,MT_POSSESSED,MT_SHOTGUY};
        for(int i=0;i<3;++i){float side=(i-1)*48.f;
            actors[i]=P_SpawnMobj(p->x+(fixed_t)((dx*128-dy*side)*FRACUNIT),p->y+(fixed_t)((dy*128+dx*side)*FRACUNIT),ONFLOORZ,(mobjtype_t)types[i]);
            actors[i]->angle=p->angle+ANG180;actors[i]->reactiontime=1000;
        }
    }
    if(t==35)for(mobj_t *a:actors){
        bloodHit(a,p,a->x,a->y,a->z+32*FRACUNIT,(fixed_t)(dx*FRACUNIT),(fixed_t)(dy*FRACUNIT),0,5);
        P_DamageMobj(a,p,p,5);a->target=nullptr;a->momx=a->momy=0;
    }
    if(t==55)for(int i=1;i<3;++i){P_DamageMobj(actors[i],p,p,actors[i]->health+1);actors[i]->momx=actors[i]->momy=0;}
    if(t==40||t==60||t==68){
        static char path[1024];snprintf(path,sizeof(path),"%s/effects-%d.png",getenv("DOOM_EFFECTS_OUT"),t);I_Render3DScreenshotTo(path);
    }
    if(t>=80)I_Quit();
}

extern "C" void I_TestPS2Style(int enabled) {
    using namespace doom3d;
    Settings probe{};
    if(probe.ps2Style)I_Error("PS2 style must default off");
    readOption(probe,"ps2_style",1);
    if(!probe.ps2Style)I_Error("PS2 style config did not enable");
    readOption(probe,"ps2_style",0);
    if(probe.ps2Style)I_Error("PS2 style config did not disable");
    int found=0;
    for(auto &tab:optionTabs(probe))for(auto &section:tab.sections)for(auto &o:section.options)
        if(!strcmp(o.key,"ps2_style")) {
            ++found;probe.accelerated=0;
            if(o.isEnabled(probe))I_Error("PS2 style enabled for software renderer");
            probe.accelerated=1;
            if(!o.isEnabled(probe))I_Error("PS2 style unavailable for GPU renderer");
        }
    if(found!=1)I_Error("Missing or duplicate PS2 option");
    int filter=settings.filter,retro=settings.retro,palette=settings.palette;
    readOption(settings,"ps2_style",enabled);
    if(settings.filter!=filter||settings.retro!=retro||settings.palette!=palette)I_Error("PS2 style changed other preferences");
    printf("PASS PS2 style config, defaults, dependency and independent toggle %d\n",enabled);
}
