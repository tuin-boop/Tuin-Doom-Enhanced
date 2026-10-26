/* Platform-neutral 3D scene: map geometry, lights, effects and settings built
   at runtime from the loaded WAD. A GPU backend uploads and draws the result. */
#ifndef DOOM_SCENE3D_H
#define DOOM_SCENE3D_H
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>
#include "shaders/vertex_modes.h"
extern "C" {
#include "doomtype.h"
}
#include "flash_lighting.h"
#include "i_render3d.h"

struct SDL_Window;
// Defined by the backend; the scene only holds and passes references.
struct GpuTexture;
using GpuTextureRef=std::shared_ptr<GpuTexture>;
enum class GpuFormat { R8, RG8, RGBA8, RGBA16Uint };

// Backend hooks used while the scene builds its resources.
// levels>1: pixels holds that many mip levels packed tightly one after another.
GpuTextureRef gpuCreateTexture(GpuFormat format,int width,int height,const void *pixels,int bytesPerRow,int levels=1);
// Replaces rects (x, y, width, height) of a texture with the same rects of
// pixels, an image of the texture's size; the rest keeps its contents.
void gpuUpdateTexture(const GpuTextureRef &texture,GpuFormat format,const std::vector<std::array<int,4>> &rects,const void *pixels,int bytesPerRow);
// Reads an emissive mask image as value*alpha in [0,1]; false if unreadable or the wrong size.
bool platformReadMask(const char *path,int width,int height,std::vector<float> &values);
// The most MSAA samples the GPU offers for the world targets (1: none).
int gpuMaxSamples();

namespace doom3d {
constexpr float doomPi=3.14159265358979323846f;
inline float units(int value) { return value/65536.0f; }
struct Point { float x,y; };
// sunU/sunV: texel in the wall bake atlas, or -1 for surfaces without one.
// statics: on sprites with bake-only lights, the static light's direction
// (x, y, z as unorm bytes) and how much of it comes from that direction (w);
// on walls, a seed per wall section so their uneven tiles differ (tiles.glsl).
// lampTint: the sector light's color from the lamps around a sprite (lampTint
// in baked_lighting.h), unorm bytes; white for surfaces, which compute their own.
struct Vertex { float x,y,z,u,v,light; unsigned mode; float red=0,green=0,blue=0; unsigned lightMask[2]={}; float sunU=-1,sunV=0; unsigned statics=0; unsigned lampTint=0xFFFFFFFFu; };
// right.w: where the wear records start in the sector buffer (0: none);
// forward.w, up.w: how wet the windows and the ground under the sky are.
// mood: x how bright the sky is drawn (the episode's mood, Game_Mood).
// fx: exposure, sky light tint; fx2: render-target pixels per Doom pixel,
// sky light level (0: off); texFilter: sharp bilinear edge softness in texels, palette mipmaps (0: off);
// flicker: the current light of the baked flicker groups, group 0 steady.
struct Uniforms { float eye[4],right[4],forward[4],up[4],projection[4],effects[4],materials[4],flashlightTint[4],fog[4],map[4],water[4],sun[4],bake[4],fx[4],fx2[4],texFilter[4],outdoor[4],haze[4],mood[4],weather[4],style[4],flicker[4][4]; };
static_assert(sizeof(Vertex)==64&&sizeof(Uniforms)==400,"Shader buffer layout");
using Flash=doom_flash_t;
// 63 lights keep the FlashSet uniform block under 4 KB and fit the 64-bit light masks.
constexpr unsigned maxLights=63;
struct FlashSet { unsigned count=0,reserved[3]={}; Flash lights[maxLights]={}; };
using LightBlocker=doom_light_blocker_t;
static_assert(sizeof(Flash)==64&&sizeof(FlashSet)==4048&&sizeof(LightBlocker)==32,"Flash shader layout");
struct FogLights { unsigned count=0,indices[4]={},padding[3]={}; };
static_assert(sizeof(FogLights)==32,"Fog shader layout");
bool voxelDetected();
struct Settings {
    int nearMissSounds=1;
    int fullscreen=0;
    int hudStyle=0,hudFace=1,hudLayout=0;
    float hudScale=100;
    int accelerated=1, widescreen=1, crosshair=1, look=1, retro=0, fps=1, scale=100;
    int invertLook=0; // Mouse look: moving the mouse forward tilts the view down.
    int classicAim=0; // With mouse look: bullets autoaim at a monster in front as in DOOM, following the view only with none.
    int quickConfirm=1; // Quicksave and quickload ask before overwriting or loading.
    float msaa=0; // Geometry edge anti-aliasing: 0 off, then 2x, 4x and 8x MSAA (capped by gpuMaxSamples).
    // Walls and floors: 0 crisp pixels, 1 smooth (bilinear), 2 sharp bilinear
    // (flat texels, edges blended over sharpSoftness texels plus a pixel).
    int filter=0;
    float sharpSoftness=0;
    int paletteMips=0; // Palette-snapped mip levels for distant walls and floors when smooth or sharp.
    float fov=90;
    float flashlightTintGain=1;
    int emissive=1, fog=1, palette=0;
    int voxelModels=1,voxelEyes=1;
    int ps2Style=0; // Optional chunky texture / stepped-lighting preset.
    int detail=1; // Derived normal maps and palette gloss.
    int variedHighlights=1; // Gloss highlights tighten on smooth artwork and spread on busy artwork, per palette ramp.
    int unevenTiles=1; // Tiles painted into floors each lie a little crooked, with a lit or shadowed lip where they meet.
    int unevenWalls=1; // The same for the tiles, plates and bricks painted into walls and ceilings.
    int softLight=1; // Light seam blending and edge contact shading.
    int reflections=1; // Mirrored scene on water.
    int retroReflections=1; // Eighth resolution, palette colors, stepped wobble.
    int spriteFilter=0; // 0: crisp, 1: bilinear.
    int sun=1; // Sun shadows baked at level load from the sky texture.
    int bakedLights=1; // Static lights baked at level load; nearby dynamic copies add flicker and detail.
    int windowLight=1; // Openings to the sky light the rooms beside them like big soft lamps, in the bake.
    float windowStrength=1; // How brightly window light shines in (0.5-2).
    int dimLitRooms=1; // Rooms the baked lamps and windows light well lose part of their flat sector light.
    float unlitDarkening=0.25f; // With dimLitRooms: the least share of flat light any indoor room loses (0-0.5).
    int bounce=1; // Light bounced from sunlit and lit surfaces, baked at level load.
    float bounceTint=3; // With bounce: how strongly bounced lamp light colors the sector light around it (0-4).
    int caustics=1; // Rippling light from liquids on nearby walls.
    // Experimental effects, each separately switchable.
    // F4's combined choices; fitGame sets the switches below them from these.
    int deaths=0; // Deaths: 0 the game's frames, 1 varied, 2 procedural falls, 3 ragdolls, 4 ragdolls Hollywood (flying, punch, spins).
    int hitReactions=1; // Hit reactions: death reactions and hit flinches.
    int bloodLevel=2; // Blood: 0 off, 1 splats and pools, 2 full (spray, shine).
    int blood=1; // Splats on floors and walls from hitscan hits; pools under corpses.
    int bloodSpray=1; // With blood: hits and kills throw drops that land as splats; wounds drip, sliding bodies smear, bodies thrown into walls splash them.
    int bloodShine=1; // Fresh blood glints under dynamic lights, then dries matte.
    int deathMotion=0; // Optional deaths jolt with the killing blow's push and height.
    int wounds=1; // Hitscan hits shoot holes through monsters' sprites, rimmed with blood; drawing only.
    int deathFalls=0; // Optional procedural falls: 0 off, 1 by hit, 2 legs, 3 chest, 4 head.
    int deathRagdoll=0; // Rigged monsters fall as ragdolls instead: the pain drawing raised into a relief on a small skeleton the hit knocks over.
    int deathGuided=1; // With deathRagdoll: chest blasts from the front sometimes play the shot death on strings (hand rigs that fit it: the zombieman).
    int deathBlast=0; // With deathRagdoll: explosions (rockets, barrels, the BFG) send bodies flying, glancing off walls until they land.
    int deathFaceViewer=0; // With deathDirector: a monster killed facing away turns toward the eye before its death frames.
    int deathDirector=0; // Optional varied staggers and turns from the monster's own frames.
    int hitFlinch=1; // Living monsters flinch from hits, leaning with the push.
    float bodyMotionStrength=1; // How far death reactions and flinches throw the body (0.5-3).
    int flashlightShadows=1; // Things in the flashlight beam cast their silhouette on the wall behind.
    int classicFlashlight=1; // A shorter, warmer beam that raises light in steps to a little past full brightness, never further.
    int stippledShadows=1; // The flashlight's shadow edges, from walls and things, break into an ordered-dither stipple instead of a clean line.
    int softSprites=1; // Explosions and puffs stop slicing into walls; dithered toward floors.
    int heatHaze=0; // Stepped raster shimmer above lava, hot floors and flames.
    int eyeAdaptation=1; // Brief stepped over/underexposure when the light changes.
    int splashes=1; // Ripple rings where things land or walk in liquids, spouts where shots hit them; bubbles and sinking in thick ones.
    int environmentEffects=1,pressureWaves=1,settlingDust=1,debrisLiquids=1,liquidRunoff=0,electricalDamage=1,rocketChunks=1;
    int hangingFragments=1,chainCollapses=1,collapseSize=12,effectBudget=128;
    int outdoorWind=1;
    float outdoorWindDensity=1;
    int dust=1; // Specks glinting in sunbeams and the flashlight beam.
    int embers=1; // A few sparks rising from flames, blown along by the game's wind.
    int authored=1; // Authored episodes (Game_Mood): hand-picked looks for the game's own maps (DOOM's E1 night in the rain); off: the derived look everywhere.
    int weather=1; // Rain under the sky: 0 never, 1 where an authored episode has it, 2 always.
    int wornPlates=1; // Plain metal plates (Game_WornPlate) show years of use: grime in the seams, dull blotches, scuffs near the floor.
    int windowStreaks=1; // Dark rain runs down the outside walls below windows, rusty on metal; inside too once rain blows through a broken one.
    int playerShadow=1; // The player's own sprite shadow from the sun or a strong light.
    int doorLight=1; // Light from a bright room spills through a door as it opens.
    int movingRelight=1; // Baked lamp light and sun re-bake around doors, lifts and lowering walls as they move.
    int texelLight=1; // Baked light, sun and bounce blend per texture pixel instead of hard map cells.
    int skyLight=1; // Cool fill from the visible sky, baked at level load; dark under overhangs.
    int bakedAO=1; // Corners, ledges and alcoves darken, baked at level load.
    int thingShadows=1; // Decorations (columns, trees, hanging bodies) cast baked shadows.
    int lightFlow=1; // Brighter sectors light their neighbors through openings, flicker included.
    int ceilingCaustics=1; // Liquid caustics also ripple on the ceilings above.
    int causticsComputed=1; // Caustics are the light the liquid flat's waves gather, sharpening with distance from the water.
    int causticsGrow=1; // Caustic shapes grow from 2 to 4 units per texel with distance from the water.
    int causticsAngle=1; // Flashlight caustics follow how much the water reflects at the beam's angle.
    int causticsSway=1; // Caustics sway with the wave slope, more the farther the light travels.
    int causticsSprites=1; // Monsters and things near liquids catch the caustics too.
    int causticsShots=0; // Muzzle flashes and projectiles over liquids throw caustics, not just the flashlight.
    int dampShores=1; // Walls and banks just above liquids turn darker and damp up to a ragged line.
    int glowEdges=1; // Walls meeting nukage, slime or lava take the strip the game paints on its edge textures.
    int shoreRipples=1; // Liquids ripple a little where they meet walls, each in its own way.
    int sunDisc=1; // A faint sun in the sky where the baked sunlight comes from.
    int sunShafts=1; // Soft sunbeams slanting down through ceiling holes and windows, with the dust motes in them.
    int sunScatter=1; // Sunbeams glow brighter seen toward the sun and fainter from behind it.
    int glossyScreens=1; // Monitor glass found in computer textures bulges, refracts the screen behind it and catches light.
    int liveReadouts=1; // The LED readouts painted into computer textures change their cells now and then (doom_screen_readouts).
    int glowingEyes=1; // The eyes painted into monster artwork glow in the dark, with bloom (sprite_eyes.h).
    int windowGlass=1; // Windows found in the map get glass that reflects and shatters when shot through.
    int gunImpacts=1;
    int breakableWalls=1;
    int throughWalls=0;
    int breachDoors=0;
    int wallDamage=250;
    int deeperDig=0;
    int monsterSurfaceDamage=1;
    int playerSurfaceDamage=1;
    int doorSparks=1; // Cosmetic sparks along opening door tracks.
    int shootableLights=1; // Experiment: tech walls spark when shot; shots into a lamp or screen break its fixture and re-bake its light.
    int killShotsCarry=1; // With shootableLights: a hitscan shot that kills its target carries on to spark and break lights behind it.
    int glassReflections=1; // Experiment: the nearest window mirrors the world, drawn again like the liquid reflection.
    int weaponLighting=1; // The weapon's painted sheen and light come out at load; lights around the player relight it.
    int fluidGloves=0; int fluidBareHands=1; int fluidFastPistol=1;
    int combatNotices=1;
    int fluidWeapons=1,fluidSounds=1,fluidMonsters=1;
    int fluidRocketFire=1;
    int materialRubble=1;
    int liquidProjectileImpacts=1;
    int progressiveGlass=1,hiddenMachinery=1,adjacentReverb=1;
    int persistentPursuit=0,aggressiveAI=0,advancedTactics=0;
    float fluidGloveRed=239,fluidGloveGreen=239,fluidGloveBlue=239;
    float fluidShadeRed=2,fluidShadeGreen=2,fluidShadeBlue=2;
    // Sound, though graphics.cfg keeps it: the mixer is told in applySettings.
    int musicDevice=0; // What plays the music: 0 SoundFont, 1 OPL FM, 2 the Mac's own synthesizer.
    int sfxResampling=0; // Effects between their samples: 0 held (original sizzle), 1 cubic, 2 windowed sinc.
    float sfxTreble=1; // Made-up treble above the samples' own band, past Original playback (0-2).
    int roomAcoustics=1; // Echoes and a reverb tail measured from the room around the player.
    float reverbAmount=1; // How loud the echoes and tail are (0-2).
    int muffleWalls=1; // Sounds out of sight are muffled, more behind closed doors.
    int muffleDistance=1; // Far sounds lose their treble.
    int naturalFalloff=1; // Far sounds fade as in air (half as strong per doubled distance), not in DOOM's straight line.
    int madeImpacts=1; // Made-up sounds for sparks, lamps and screens dying, and glass breaking (audio/made_bank.h).
    int glassSound=0; // Which breaking glass: 0 the shatter, 1 the fracture (a branching crack front).
    int ambience=1; // Lamps, machines, liquids, fire and wind sound where the map's textures show them.
    int alternativeComputerSounds=0; // Machine hum, hard-drive seeks and relay chatter in place of the computer bleeps.
    float ambienceAmount=1; // How loud the ambience is next to the effects (0-2).
    int footsteps=1; // The player's footsteps by floor, splashes in liquids, monsters splashing as they wade.
    // Performance: cheaper stand-ins for per-frame light work.
    int bakeOnlyLights=0; // Static lights only in the bake, with its light direction and flicker groups; pools as area lights.
    int gridSpriteLight=0; // Things take static light and their shadow light from a grid baked at level load.
    int unoccludedSurfaceLights=0; // Glowing textures' dynamic light skips wall tests; its gloss can show through walls.
    int fewerSurfaceLights=0; // Only the 8 nearest glowing-texture lights stay dynamic, not 24.
};
// glassFrame: on textures with monitor screens, where each glass pixel sits
// on its screen (see screen_glass.h); null otherwise. relief: on walls and
// flats, the bump slope and grain the shader shades them with (withRelief).
struct Image { bool opaque=true; std::vector<byte> pixels; GpuTextureRef texture,glassFrame,relief; int width=0,height=0,left=0,top=0; std::array<float,3> glow={1,1,1}; float glowWeight=0,glowHeight=0.85f; std::array<float,3> emissionColor={}; float emissionWeight=0,emissionCoverage=0,emissionU=0,emissionV=0; std::vector<byte> emission,screens; std::array<float,3> average={},brightest={}; bool averaged=false,brightened=false; bool leaded=false,liquidEmitter=false,switchIndicator=false; };

// Frame profiling for the performance smoke test (I_Render3DProfile).
struct Profile { bool enabled=false; unsigned skip=0; const char *screenshotPath=nullptr; I_Render3DFrameProfile frame={}; };
extern Profile profile;

extern Settings settings;
extern SDL_Window *gameWindow;
extern FlashSet flashes;
// What the GPU gets: flashes with first/count into lightWords, the blockers
// and their per-direction slices (buildLightWords in scene3d/lights.cpp).
extern FlashSet gpuFlashes;
extern std::vector<std::array<uint32_t,4>> lightWords;
extern FogLights fogLights;
// The relit weapon's surroundings: ahead, behind, right, left, up, down (weaponDraws).
extern float weaponProbe[24];
extern std::vector<LightBlocker> lightBlockers;
/* Texture keys: wall index >=0, WAD lump = -1-lump. */
extern std::unordered_map<int,Image> images;
// Stable identities for independently rebuilt mesh sections. Offsets describe
// this frame's CPU batches; GPU residency validates the final vertex bytes.
struct VertexSection {uint64_t id;size_t first,count;unsigned revision;uint64_t maskStamp=0;};
extern std::map<const std::vector<Vertex>*,std::vector<VertexSection>> vertexSections;
extern std::map<int,std::vector<Vertex>> batches,shadowBatches,decalBatches,effectBatches,effectAddBatches;
// The sprites of the glass mirror pass, seen from the mirrored eye, the
// player's own included (addThing in scene3d/geometry.cpp).
extern std::map<int,std::vector<Vertex>> mirrorBatches;
extern std::vector<Vertex> mistVertices,particleVertices,heatVertices;
// HDR-only rubble emission, depth tested before the bloom blur.
extern std::vector<Vertex> rubbleEmissionVertices;
// Liquid crests (splash rings, shore ripples): brighten what is under them by
// one plus their color (crestPipeline).
extern std::vector<Vertex> crestVertices;
// Sunbeam ribbons, drawn additively in the mist pass.
extern std::vector<Vertex> shaftVertices;
// Sky ceilings and sky walls, drawn into depth with the sky shader.
extern std::vector<Vertex> skyVertices;
// Window panes (scene3d/windows.cpp), blended over the finished world by glass.frag.
extern std::vector<Vertex> glassVertices;
// With glass reflections: whether the world is mirrored in a window this
// frame, that pane's plane (unit normal toward the eye, offset), and how far
// it has faded in from its probe since it became the mirror (0-1).
extern bool glassMirrorActive;
extern float glassMirrorPlane[4],glassMirrorFade;
// Glass reflection probes: cube maps captured from in front of each window
// on each side, for panes without the live mirror. at: where; low/high: the
// room's box, which reflected rays are projected onto for parallax. The
// backend renders glassCaptures this frame into its probe atlas, which it
// reallocates when glassProbeSerial changes.
constexpr size_t maxGlassProbes=64;
constexpr int glassProbeSize=64;
struct GlassProbe { float at[3],low[3],high[3]; };
extern std::vector<GlassProbe> glassProbes;
extern std::vector<int> glassCaptures;
extern unsigned glassProbeSerial;
// Counts settings changes, so what is built from them (probes) can redo it.
extern unsigned settingsSerial;
extern GpuTextureRef seamTexture,contactTexture,cloudTexture,wallBakeTexture,flatLightTexture,causticTexture,causticPattern,shoreTexture;
// Bake-only lights: where static light comes from, per wall atlas texel and
// floor/ceiling cell (see bakeLights).
extern GpuTextureRef wallDirectionTexture,flatDirectionTexture;
extern float reflectionPlane;
extern bool reflectionActive;
extern bool worldPending,screenshotPending,flashlightOn;
extern float pitch;

// Screen layout of the world view and the 4:3 HUD in render-target pixels.
struct FrameView {
    Uniforms camera;
    float uiX,uiY,uiWidth,uiHeight,worldX,worldY,worldW,worldH;
};
// frameMean and cycleMean (liquid flats only): the shown frames' average color
// at the crossfade, and the whole animation's; cycleMean's alpha is how much
// of the artwork's contrast stays far away (0: not a liquid, no fade);
// frameMean's alpha is the material (surfaceMaterial in scene3d/scene.cpp).
struct SurfaceBinding { const Image *image,*next; float blend; std::array<float,4> frameMean{},cycleMean{}; };
struct SpriteDraw { const Image *image; std::vector<Vertex> vertices; bool additive=false; };

// Lifecycle: init reads options and graphics.cfg, the backend may override
// them, then applySettings installs the engine hooks.
void sceneInit(SDL_Window *window);
void sceneShutdown();
void applySettings();
void settingsChanged();
void buildCloudTexture();
Image &wallImage(int index);
Image &lumpImage(int lump,bool flat);
// Builds the camera, layout and (when the world is visible) all geometry and lights.
FrameView prepareFrame(int width,int height);
// Level loading (loading.cpp): when loadPending, the backend calls
// runLoading before drawing the frame. It does the frame's level building
// and bakes while show draws the loading screen (level or other title, the
// current stage, 0-1 done), from the calling thread, a few times a second.
using LoadingShow=void(*)(const char *title,const char *stage,float fraction);
bool loadPending();
void runLoading(int width,int height,LoadingShow show);
std::vector<unsigned> hudPixels(const unsigned *pixels);
extern GpuTextureRef toastyTexture,romeroTexture;
extern float romeroAge,romeroAspect;
extern int romeroCorner;
extern float toastyVisible,toastyAspect;
std::vector<std::array<float,4>> sectorInfo();
SurfaceBinding surfaceBinding(int key);
std::vector<SpriteDraw> weaponDraws(const Uniforms &camera);
std::vector<Vertex> crosshairVertices(float worldW,float worldH);
void screenQuad(std::vector<Vertex> &out,float x,float y,float w,float h,float u0,float v0,float u1,float v1,float light=1,unsigned mode=MODE_SCREEN);
// 32³ nearest-palette lookup, BGRA, indexed (b*32+g)*32+r.
std::vector<unsigned> paletteLUTColors(const unsigned *palette);
bool renderCheckDue();
void saveScreenshot(const void *bgra,int width,int height,int stride);
void updateTitle(int width,int height,const char *renderer);
} // namespace doom3d
#endif



