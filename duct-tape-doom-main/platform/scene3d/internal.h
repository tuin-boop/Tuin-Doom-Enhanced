/* Internal to the 3D scene in platform/scene3d/: the state and helpers its
   files share. The public interface is scene3d.h. */
#ifndef DOOM_SCENE3D_INTERNAL_H
#define DOOM_SCENE3D_INTERNAL_H
#include "../shadow_lights.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <climits>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <ctime>
#include <functional>
#include <random>
#include <string>
#include <thread>
extern "C" {
#include "doomdef.h"
#include "doomstat.h"
#include "r_state.h"
#include "r_sky.h"
#include "doomdata.h"
#include "v_video.h"
#include "w_wad.h"
#include "z_zone.h"
#include "m_swap.h"
#include "m_argv.h"
#include "i_system.h"
#include "r_main.h"
#include "m_bbox.h"
extern int viewwindowx, viewwindowy;
extern boolean menuactive, automapactive, paused;
extern int numflats, numtextures;
extern float tourpitch;
extern boolean freeaim, classicaim;
extern boolean quickconfirm; // The game's menu (F4 "Confirm quicksave and quickload").
extern fixed_t aimslope;
int P_PicAnimationNext(boolean istexture,int pic,int *next);
}
#include <SDL3/SDL.h>
#include "../i_render3d.h"
#include "../game_profile.h"
#include "../game_materials.h"
#include "../scene3d.h"
#include "../emissive.h"
#include "../screen_glass.h"
#include "../painted_tiles.h"
#include "../surface_lighting.h"
#include "../baked_lighting.h"
#include "../weapon_lighting.h"
#include "../sprite_eyes.h"
#include "../sprite_eyes_stock.h"


namespace doom3d {
// Only pickups that emit colored light get a bloom halo. Ordinary supplies
// retain their visibility lift without washing out their painted pixels.
inline bool glowingPickup(int sprite) {
    switch(sprite) {
    case SPR_BKEY:case SPR_YKEY:case SPR_RKEY:case SPR_BSKU:case SPR_YSKU:case SPR_RSKU:
    case SPR_ARM1:case SPR_ARM2:case SPR_SOUL:case SPR_MEGA:case SPR_PINV:case SPR_PSTR:
    case SPR_PINS:case SPR_SUIT:case SPR_PMAP:case SPR_PVIS:return true;
    default:return false;
    }
}

// hot: lava, or a damaging floor whose flat is mostly red or orange (heat haze).
struct SectorMist { bool pit=false,liquid=false,toxic=false,outdoor=false,reflective=false,hot=false,thick=false; std::array<float,3> color={0.24f,0.29f,0.32f}; };
// Map-space surface data, built once per level from the linedefs. Each cell
// stores its sector, the nearest open neighbor sector with blend weights
// (low byte for the floor, high byte for the ceiling, 255 = half the
// neighbor's light), the sector its light flows from (bakeFlow), and floor/ceiling distances to
// walls for contact shading.
constexpr uint16_t noSector=0xFFFF;
struct SeamCell { uint16_t own=noSector,neighbor=noSector,weight=0,flow=noSector; };
// Sunbeams (bakeShafts): from a sunlit indoor floor point (x,y,z) back toward
// the sun for length units, until the ray leaves the building.
struct SunShaft { float x,y,z,length,strength; int sector; };
// Walls: one strip per linedef side (index line*2+side) in an RGBA8 atlas
// (rgb baked light at half scale, a sun), columns along the side from its
// first drawn vertex at a, rows up from low to the highest ceiling its
// sectors and their neighbors reach. The atlas's right half holds bounce
// light at the same coordinates.
struct BakeStrip { int x=-1,y=0,width=0,height=0,sector=0; float low=0,high=0,scale=0,ax=0,ay=0,dx=0,dy=0,length=0; };
constexpr int atlasWidth=4096,atlasStride=2*atlasWidth;
// Four map-sized layers stacked in rows: floor light, ceiling light, floor
// bounce, ceiling bounce; rgb at half scale.
enum FlatLayer { floorLight, ceilingLight, floorBounce, ceilingBounce };
constexpr int flickerGroups=16;
// Static light at a point as things take it, without facing: the color
// (unflickered), where it comes from (unit directions weighted by each
// light's luminance, and their total), the strongest flicker group's share,
// and the strongest point light not far below that can cast a thing's shadow.
struct StaticSample {
    std::array<float,3> color={},towards={};float amount=0;
    std::array<float,3> bounce={}; // Filled by bakedLightAt: the bounce light there.
    int group=0;float groupShare=0;
    ShadowSources shadows;
};
// Grid lighting cells (bakeStaticGrid).
constexpr float staticCell=32;
struct StaticCell { bool used=false; float low=0,high=0; StaticSample at[2]; };
constexpr float bounceStrength=1.0f;
// A light that casts things' floor shadows (addEnemyShadow).
// Bake-only static lights near the eye, for the fog glow (collectSurfaceLights).
struct StaticFog { float x,y,z,radius,strength;std::array<float,3> color; };
struct Debris { float x,y,z,vx,vy,vz,born,life,size; std::array<float,3> color; float light=1;bool liquidHit=false; }; // color unlit.
struct FlatDecal { float x,y,born,size;int sector;bool ceiling,flip; };
struct WallSpan { Point a,b;float bottom,top,anchor;int texture; };
// Where a ray meets a wall (firstSolidWall).
struct WallHit { int line=-1,side=0,section=0; float t=0; };

// scene.cpp
extern unsigned levelSerial;
// Sector heights at the previous tic; doors and lifts blend like moving things.
extern std::vector<std::array<float,2>> oldHeights;
extern float renderFraction;
extern uint64_t tickTime;
void interpolatedPosition(const mobj_t &thing,float fraction,float &x,float &y,float &z);
void snapshotWeaponPositions();
void fluidWeaponShot(mobj_t *origin,int weapon,int flash);
void fluidWeaponAttack(player_t *player);
void fluidWeaponAux(int alt,int reload);
void resetWeaponPositions();

// config.cpp
extern const char *graphicsConfig;
void loadSettings();

// images.cpp
void imagesShutdown();
void prepareVoxelModels();
bool drawVoxel(std::map<int,std::vector<Vertex>> &,const mobj_t &,int,int,angle_t,float,float,float,float,const std::array<float,3>&,unsigned,unsigned,const Uniforms&);
Image upload(int w,int h,const byte *pixels,int left=0,int top=0,const byte *mask=nullptr,const byte *normals=nullptr,const byte *glass=nullptr,bool mipmaps=false);
const std::array<float,3> &averageColor(Image &image);
std::array<float,3> paletteColor(const std::array<float,3> &color);
const std::array<float,3> &brightestColor(Image &image);
std::array<float,3> texelColor(Image &image);

// sprite_normals.cpp
std::vector<float> spriteTubeHeights(int w,int h,const byte *pixels);   // texels' true tube depth, 0 where empty
std::vector<byte> spriteNormals(int w,int h,const byte *pixels);

// map.cpp
void mapShutdown();
// How a liquid flat looks at its shores (shore ripples) and how much of its
// contrast remains far away (0: not a liquid).
enum { LIQUID_LOOK_NONE,LIQUID_LOOK_WATER,LIQUID_LOOK_THICK,LIQUID_LOOK_BLOOD,LIQUID_LOOK_HOT };
int liquidLook(int pic);
// Ice floors (Game_IceFlat): deep grooves, glassy highlights and a sheen.
bool iceFlat(int pic);
float liquidDetailFloor(int pic);
extern std::vector<std::vector<Point>> floors;
extern std::vector<std::vector<int>> sectorFloors;
extern std::vector<std::array<float,4>> floorBounds;
extern std::vector<SectorMist> sectorMist;
extern float fogReferenceHeight;
extern std::vector<SeamCell> seamCells;
// RGBA8 per cell: floor and ceiling contact distance, baked sun visibility,
// light flow weight.
extern std::vector<uint8_t> contactCells;
extern float mapOrigin[2],mapCell;
extern int mapWidth,mapHeight;
std::vector<Point> clip(const std::vector<Point> &poly,Point origin,Point direction,bool right);
void clipInto(const std::vector<Point> &poly,Point origin,Point direction,bool right,std::vector<Point> &out);
void buildMap();
float sectorHeight(const sector_t *sector,bool ceiling);
// The level's mood: the authored episode's (Game_Mood, with settings.authored) or the plain derived one:
// outdoor light, sun, window light turned round, rain and its angle.
const game_mood &levelMood();
float lighting(const sector_t *sector,bool bright=false);

// caustics.cpp
void causticsShutdown();
extern std::vector<int> causticFrames;
extern int causticSpeed;
void buildCaustics();

// liquid_edges.cpp
// A glowing liquid's edge strip, from its edge texture (height 0: none):
// per row up from the foot, 64 texels of palette index, paint and glow.
struct EdgeStrip { int kind=0,height=0,texture=-1; std::vector<byte> index,paint,glow; };
const EdgeStrip &edgeStrip(int kind);
int liquidGlowKind(int sector);
bool paintsOwnEdge(int texture,int kind);
const EdgeStrip *levelEdgeStrip();
void buildShore();
// Liquid edges from buildShore: a to b, the liquid sector, and the unit
// normal pointing into the liquid. shoreDistance: signed distance per map
// cell (into the liquid positive), as the shore texture holds it.
struct ShoreEdge { float ax,ay,bx,by,nx,ny; int sector; };
extern std::vector<ShoreEdge> shoreEdges;
float shoreDistanceAt(float x,float y);

// bake_atlas.cpp
void bakeAtlasNewLevel();
void bakeAtlasShutdown();
extern std::vector<BakeStrip> bakeStrips;
extern std::vector<int> bakeOrder;
extern int atlasHeight;
extern std::vector<uint8_t> wallBakeCells;
extern std::vector<uint8_t> flatLightCells;
extern BakeMap bakeMap;
void layoutBake();
void parallelFor(int count,const std::function<void(int)> &work,unsigned threads=16);
void uploadWallBake();
void uploadFlatLight();
void bakeWallCoordinates(const line_t &line,int side,float along,Vertex &vertex,bool strip=false);

// bake_sun.cpp
void sunNewLevel();
void sunShutdown();
extern bool sunBaked;
extern float sunLevel;
extern std::array<float,3> sunTint;
extern float sunAzimuth[2];
extern float sunDirection[3];
// The sun drawn in the sky: yaw of the baked direction, and its strength
// from how bright the sky texture is around it (a dark sky shows only a hint).
extern float sunYaw,sunGlow;
extern float sunElevation,sunDiscElevation,sunShade,sunGain;
std::array<float,3> skyHorizon();
std::array<float,3> torchColor();
// Light through stained glass, baked with the static lights (glass_light.cpp).
void findGlassWindows();
void addGlassLight(const BakeMap &map,int sector,float x,float y,float z,const float normal[3],float out[3]);
void glassLightShutdown();
extern std::vector<SunShaft> sunShafts;
// Sectors whose air sunbeams and sunlit dust show in (bakeShafts): under a
// ceiling, or in a sky opening that is enclosed and at most 400x400 units in
// area, a ceiling hole or a walled pocket the light drops into. A courtyard
// or open ground, however walled in, is plain daylight with no beams in it.
extern std::vector<char> beamAir;
int sunCell(const BakeMap &map,size_t i,const float direction[3]);
void sunStrip(const BakeMap &map,const BakeStrip &strip,const float direction[3]);
void bakeSun();
float packOutdoor(const sector_t *sector,float *enclosed=nullptr,float *floor=nullptr);
float sunLightAt(float x,float y,const sector_t *sector,float light);
float seamLightAt(float x,float y,const sector_t *sector,float fallback);

// bake_lights.cpp
void lightBakeNewLevel();
void lightBakeShutdown();
// Static lights were baked from these inputs (see lightBakeInputs); -1
// before the level's first light bake. The serial counts light bakes.
extern int lightBakeKey;
extern unsigned lightBakeSerial;
extern bool bakedLightsActive;
// Per sector, how far its light level drops to the room around it while baked
// lights are on (classifyLampSectors): the brightness its mapper gave it for
// the lamps over it, which the bake now draws itself.
extern std::vector<float> lampDrop;
extern bool bakeOnlyActive;
// The baked lights, kept for sprites, and a coarse grid listing the lights
// that can reach each of its cells.
extern std::vector<BakeLight> bakeSources;
// Per bakeSources entry: the merge cell of the glowing tiles it stands for
// (shot_lights.cpp breaks them by it), all zeros for other lights.
extern std::vector<SurfaceLightKey> bakeSourceKeys;
// Re-merges bakeSources[n], a merged surface light, from the tiles of its
// merge cell not shot out, as bakeLights merged it (strength 0 with none
// left), and lists it in bakeGrid wherever it now reaches.
void remergeBakeSource(int n);
extern int firstWindowSource;
float capWindowLight(float rgb[3]);
extern std::vector<std::vector<int>> bakeGrid;
// Bake-only lights: liquid pools as area lights, listed in bakeGrid after the
// point lights; each point light's flicker group (0: steady); and per wall
// atlas texel and floor/ceiling cell where the static light comes from.
extern std::vector<BakeArea> bakeAreas;
extern std::vector<uint8_t> sourceGroups;
extern std::vector<uint8_t> wallDirectionCells,flatDirectionCells;
const std::vector<int> &bakeGridLights(float x,float y);
int lightBakeInputs();
void lightCell(const BakeMap &map,size_t i,bool only);
void lightStrip(const BakeMap &map,const BakeStrip &strip,bool only);
std::array<float,3> skyColor();
void bakeLights();

// bake_indirect.cpp
void indirectNewLevel();
void indirectShutdown();
// Bounce light was baked from these inputs (see bounceInputs); -1: not yet.
extern int bounceKey;
// Decorations in the bakes (settings.thingShadows) as last synced; -1 before
// the level's first sync. Sky/occlusion and flow were baked with this key.
extern int bakeThingsKey,ambientKey,flowKey;
extern float skyLevel;
extern std::array<float,3> skyTint;
int bounceInputs();
void bakeBounce();
void syncBakeThings();
void bakeAmbient();
void bakeFlow();
float flowLightAt(float x,float y,const sector_t *sector,float light);

// relight.cpp
void stopRelight();
// A merged light (a bakeSourceKeys entry) lost shot-out tiles: its source
// keeps the tiles left (remergeBakeSource) or goes dark, and the next
// background re-bake follows.
void relightBrokenLight(const SurfaceLightKey &cell);
// Waits for the background re-bake around moving sectors (relightMovingSectors)
// and, with apply, uploads what it baked. Every full bake calls it first.
void finishRelight(bool apply=true);
void relightMovingSectors();

// thing_light.cpp
void clearStaticGrid();
extern int staticWidth,staticHeight;
extern unsigned staticSerial;
extern std::vector<StaticCell> staticGrid;
bool gridLightActive();
StaticCell staticCellAt(const BakeMap &map,int gx,int gy);
void bakeStaticGrid();
void invalidateStaticSamples();
std::array<float,3> bakedLightAt(float x,float y,float z,const sector_t *sector,StaticSample *sample=nullptr);
unsigned packedLampTint(const StaticSample &statics,float light);
void staticShadows(const StaticSample &sample,float z,ShadowLights &lights);

// lights.cpp
void clearFlashPulses();
void lightsShutdown();
extern std::array<const mobj_t*,maxLights> lightSources;
// Lights kept only for the fog glow (bake-only static lights): no surface,
// sprite or shadow uses them.
extern std::array<bool,maxLights> fogOnly;
// For profiling: each light's I_RENDER3D_LIGHT_* kind, set by its collector.
extern std::array<uint8_t,maxLights> lightKinds;
extern uint8_t lightKind;
void muzzleFlash(mobj_t *source,int weapon,int projectile);
void sparkFlash(float x,float y,float z,float radius,float strength,const std::array<float,3> &color);
void appendLight(float x,float y,float z,float radius,float strength,const std::array<float,3> &color,const mobj_t *source,float directionality,bool occluded=true,float baked=0,
                 const std::array<float,4> &spot={});
bool decorationLight(const mobj_t &thing);
int flickerGroup(const mobj_t &thing);
float groupFlicker(int group);
// Flicker groups 1..lampGroupCount belong to switching lamps (bake_lights.cpp).
extern int lampGroupCount;
extern float lampGroupFlicker[flickerGroups];
void updateLampGroups(const float eye[3]);
BakeLight decorationSource(const mobj_t &thing);
extern std::vector<StaticFog> staticFog;
void collectFlashes(float fraction,const Uniforms &camera);
float flashAt(float x,float y,float z,const Flash &f);
void findDoors();
void selectFogLights(const Uniforms &camera);
void assignLightMasks();
void skipProfiledLights();
extern std::array<float,maxLights> lightSliceCost;
void buildLightWords();

// surface_lights.cpp
void clearSurfaceLights();
extern std::vector<SurfaceLight> surfaceLights;
extern float surfaceEye[3];
extern SurfaceLightSelection surfaceSelection;
extern uint64_t surfaceLightTime;
std::array<float,2> panelNormal(const SurfaceLight &light);
std::vector<SurfaceLight> mergedSurfaceLights(std::vector<SurfaceLight> tiles);
void collectFlatLights();
void collectSurfaceLights();
void wallLights(const line_t &line,int side,Point a,Point b,float bottom,float top,int tex,float anchor,int span);
// The merge cell (mergeSurfaceLights key) a tile's light joins.
SurfaceLightKey surfaceCellOf(const SurfaceLight &tile);
// Lamps of a sector whose light flickers follow it: how far on this tic
// (0-1), and whether switched off (blinking and strobing sectors only).
// With an image: whether that texture follows (screens stay on beside lamps).
bool lampFlickers(int sector);
bool lampFlickers(int sector,const Image &image);
float lampShine(int sector);
bool lampOut(int sector,const Image &image);
// A sector's light level as drawn: one whose lamps flicker by group keeps
// its low, the lamps' light carrying the swing.
int lampLightLevel(const sector_t *sector);
bool lampGroupSector(int sector); // Its lamps hold a bake flicker group.
// Vertices from first on carry a fading sector's lamp glow for their
// glowing texels (Vertex::lampTint alpha on walls and flats).
void lampGlow(std::vector<Vertex> &out,size_t first,const sector_t *sector,const Image &image);
void measureEmissionDensity();
float materialEmissionGain(const Image &image);

// geometry.cpp
void quad(std::vector<Vertex> &out,Vertex a,Vertex b,Vertex c,Vertex d);
void geometry(const Uniforms &camera,float fraction);

// shadows.cpp
struct RagdollCard;
void addEnemyShadow(const mobj_t &thing,const Image &image,int lump,bool flip,
                    float x,float y,float z,const Uniforms &camera,const ShadowLight *light=nullptr,bool anyThing=false,const RagdollCard *card=nullptr);
void flashlightSilhouette(const mobj_t &thing,const Image &image,int lump,bool flip,float x,float y,float z,const Uniforms &camera,const Flash &beam,const RagdollCard *card=nullptr);

// atmosphere.cpp
void buildShafts(const Uniforms &camera);
void buildMist(const Uniforms &camera);
void buildHeat(const Uniforms &camera);
void buildDust(const Uniforms &camera);
// embers.cpp
void buildEmbers(const Uniforms &camera);

// impacts.cpp
void dropOldLevelImpacts();
void impactsShutdown();
extern std::vector<FlatDecal> flatDecals;
extern std::vector<Debris> debris;
extern std::minstd_rand impactRandom;
void pixelBlock(std::vector<Vertex> &out,const Uniforms &camera,float x,float y,float z,float size,float tall,const std::array<float,3> &color,bool resting=false,
                const std::array<float,3> *albedo=nullptr,bool palette=true,std::unordered_set<uint64_t> *taken=nullptr,float alpha=1);
// Doom-pixel cells on screen already holding a liquid crest this frame:
// crests brighten what is under them, so overlapping ones would compound.
extern std::unordered_set<uint64_t> crestCells;
bool wallSpan(const line_t &line,int side,int section,WallSpan &span);
void wallImpact(line_t *line,int side,fixed_t fx,fixed_t fy,fixed_t fz);
void barrelBlast(const mobj_t &thing,float x,float y,float z);
void buildImpacts(const Uniforms &camera);
bool firstSolidWall(float x,float y,float z,float dx,float dy,float dz,float length,WallHit &hit);
float valueNoise(float x,float y,float cell,uint32_t seed);

// shot_lights.cpp
void shotLightsShutdown();
// Per sector, what its light level keeps after shot-out lamps (1: all).
extern std::vector<float> shotDim;
// A sector lost share of its baked floor light to shot-out lamps.
void dimShotSector(int sector,float share);
// Whether a glowing tile is out now (sputtering out for a moment after the shot).
bool tileDark(const SurfaceLightKey &key);
// Whether a glowing tile has been shot out, sputtering or not.
bool tileShotOut(const SurfaceLightKey &key);
void shotWall(const line_t &line,int side,int section,float along,float x,float y,float z,int reach=1);
void shotFlat(int sector,bool ceiling,float x,float y,float z,int reach=1);
// Rockets and barrels bursting break the lights around them.
void blastLights(float x,float y,float z,float radius);
// Each tic: projectiles bursting against lights break them.
void watchShotLights();
void shotThrough(const mobj_t *target,fixed_t fx,fixed_t fy,fixed_t fz,fixed_t fdx,fixed_t fdy,int damage);
// The world quads of a wall piece or floor leaf with shot-out tiles, split so
// those tiles carry MODE_DEAD_GLOW; false (nothing added) when none are out.
bool addBrokenWall(std::vector<Vertex> &out,const line_t &line,int side,int span,const Image &image,Point a,Point b,
                   float bottom,float top,float u,float anchor,float light,unsigned mode,unsigned seed);
bool addBrokenFlat(std::vector<Vertex> &out,int leaf,int sector,bool ceiling,const Image &image,float z,float light,unsigned mode);

// blood.cpp
void bloodShutdown();
// Opaque blood-sprite palette indices, ordered from dark to bright.
const std::vector<byte> &bloodPalette();
// Blood shown at all: switched on, in a level, the WAD's blood sprite found.
bool bloodOn();
// A blood shade (0 dark - 1 bright) of the blood sprite, unlit.
std::array<float,3> bloodColor(float shade);
void bloodHit(mobj_t *target,mobj_t *shooter,fixed_t fx,fixed_t fy,fixed_t fz,fixed_t fdx,fixed_t fdy,fixed_t slope,int damage);
void buildBlood(const Uniforms &camera);
// Splats shown from born on: a splat, a drop's speck, or a body's smear
// (thick end forward along angle). size across, stretched along angle.
enum { BLOOD_SPLAT,BLOOD_SPECK,BLOOD_SMEAR };
void bloodFloorSplat(float x,float y,int sector,float size,float angle,float stretch,bool ceiling,float born,int kind);
// On the wall hit at (x,y,z), stretched along it; drips: one or two run down from it.
void bloodWallSplat(const WallHit &hit,float x,float y,float z,float size,float stretch,float born,int kind,bool drips);

// spray.cpp
// What a hitscan hit throws: an exit spray past the body along the shot, a speck or two back.
void sprayHit(const mobj_t *target,float x,float y,float z,float dx,float dy,float slope,int damage);
// A kill's burst: along the push (dx,dy, speed units a tic) from where it struck (hitZ above
// the feet, -1 unknown), more for overkill, all around for a gib.
void sprayKill(const mobj_t &thing,float dx,float dy,float speed,float hitZ,bool gib,int overkill);
// A hit that didn't kill, by a missile or blast, at hitZ above the feet.
void sprayHurt(const mobj_t &thing,float dx,float dy,float hitZ,int damage);
// A ragdoll's torso coming down at (x,y) on the floor z, at speed (units a tic): the harder, the more.
void sprayLanding(float x,float y,float z,float speed);
// A ragdoll's torso slamming into the wall along (dx,dy) from (x,y,z) at speed (units a tic).
void sprayWallHit(float x,float y,float z,float dx,float dy,float speed);
// A body sliding on the floor: a smear at (x,y), thick end forward along angle.
void spraySmear(float x,float y,float angle,float size);
// Each tic: wounds drip, sliding bodies smear, bodies thrown into walls splash them.
void watchSpray();
void buildSpray(const Uniforms &camera);
void sprayShutdown();

// body_motion.cpp
// A monster's drawn pose from the blows it took: lean and upper-body whip
// as world-space vectors (radians, toward the push), legs squashed by that
// share, lifted this many units; fliers pivot about their middle.
// flatten: the whole body shortened by that share (a landing's settle, from the death director).
struct BodyPose { float lean[2],whip[2],squash,lift,flatten;bool flier; };
void bodyHit(mobj_t *target,fixed_t fx,fixed_t fy,fixed_t fz,fixed_t fdx,fixed_t fdy,int damage);
void bodyDamaged(mobj_t *target,mobj_t *inflictor,mobj_t *source,int damage);
void watchBodies();
void bodyMotionShutdown();
bool bodyPose(const mobj_t &thing,float fraction,BodyPose &pose);
void bodyQuads(std::vector<Vertex> &out,const BodyPose &pose,const Vertex &base,float x,float y,float feet,float rx,float ry,
               float left,float right,int imageTop,int rows,float u0,float u1,const std::vector<byte> *holes=nullptr);

// death_director.cpp
// How a dying monster is drawn this frame: the sprite and frame (with its
// full-bright bit), the facing its rotations are picked by, and whether
// the frames are mirrored.
// A procedural fall's drawing this frame (death_falls.cpp): rigged (rig set:
// legs' hip drops, sideways shift, torso turn, legs' swing about the pelvis,
// sink, all in units and radians, + turning left) or a drawing set down by
// ground units, lifted, pressed flat by a share. Front view (rotation 1).
struct FallRig;
struct FallPose { const FallRig *rig=nullptr;float drops[2]={},shift=0,torso=0,legs=0,sink=0,head=0,ground=0,lift=0,flatten=0;bool active=false; };
// bend: how much of the death jolt (body_motion.cpp) the pictured body
// still takes, 1 before the collapse, 0 once it lies; settle: a landing's
// brief flattening; fall: a procedural fall, which draws instead.
// ragdoll: a ragdoll fall (ragdoll.cpp), which draws instead of all of the above.
struct Ragdoll;
struct DeathView { int sprite,frame;angle_t facing;bool mirror;float bend,settle;FallPose fall;const Ragdoll *ragdoll=nullptr; };
void startDeath(const mobj_t &thing,float dx,float dy,float speed,float side,float high,bool gib,const mobj_t *attack,float hitZ=-1,float hitU=0);
void forgetDeath(const mobj_t *thing);
void deathsShutdown();
bool deathView(const mobj_t &thing,float fraction,DeathView &view);

// death_falls.cpp
// hitZ, hitU: where the killing blow struck, units above the feet and right of the middle as the
// shooter sees it (hitZ < 0: not known, as for a missile).
bool startFall(const mobj_t &thing,float speed,float high,float side,float hitZ,float hitU);
void forgetFall(const mobj_t *thing);
void fallsShutdown();
void prepareFalls();      // at level load: rigs for every killable monster's pain and first death drawing
bool fallView(const mobj_t &thing,float fraction,DeathView &view);
void fallQuads(std::vector<Vertex> &out,const Vertex &base,const FallPose &pose,float x,float y,float feet,float rx,float ry,bool flip,
               const std::vector<byte> *holes=nullptr);
// The rig a drawing falls on, for the ragdoll's body: hips, knees and ankles ([left, right][u, h], units
// right of the origin and above the feet), where the legs part, the head (if found), whether its hand
// rig plays the guided shot death (ragdoll.cpp), and per texel whether it moves with the torso whatever
// its height (a gun).
struct RigJoints { float hip[2][2],knee[2][2],ankle[2][2],split;bool hasHead,guided;float neck,headMiddle,headWidth;
                   const std::vector<byte> *carried;int width,height,left,top; };
bool rigJoints(int lump,RigJoints &out);

// ragdoll.cpp
// A rigged monster's body thrown down as a ragdoll: its pain drawing raised into a relief
// (ragdoll_body.cpp) on a small 3D skeleton the hit knocks over. hitZ, hitU as startFall; blast: an
// explosion killed it (thrown flying).
bool startRagdoll(const mobj_t &thing,float dx,float dy,float speed,float side,float high,float hitZ,float hitU,bool blast);
bool ragdollView(const mobj_t &thing,DeathView &view);
void forgetRagdoll(const mobj_t *thing);
void ragdollsShutdown();
void prepareRagdolls();   // at level load: bodies for every killable monster's pain drawing
void ragdollTestTic();    // ragdoll_test.cpp: -ragdolltest's scripted kills, each tic
// The body as a sprite card: drawn by a virtual camera of its own (looking at it from the eye's side,
// stepped to 45 degrees, straight on while it stands and 30 degrees down once it lies, one pixel
// per unit), and that picture stood upright at the body facing the eye like any sprite, its lowest
// solid row on the floor. Once the body lies still the card is baked: the last picture shown the same
// from every angle (rotation 0, like id's corpse frames), the doll's posed body freed.
// key: the picture's image; centred at (x, y), top at top, half wide, size square.
// The doll flies in the world from where it died, not with the thing: fraction is the frame's
// share of the tic, sink how far the thing sinks into a liquid.
struct RagdollCard { int key;float x,y,top,half;int size;int u0,v0,u1,v1; }; // opaque texel bounds, upper limits exclusive
bool ragdollCard(const Ragdoll &doll,float fraction,float sink,const float *eye,const std::vector<byte> *holes,RagdollCard &card);
// Where the body is drawn (its card's middle) and the floor it died on: its light and liquid are found
// there. True once it is down (a body in the air doesn't sink into the liquid under it).
bool ragdollWhere(const Ragdoll &doll,float fraction,float &x,float &y,float &z);
bool hasRagdoll(const mobj_t *thing);   // a ragdoll draws it: its own contacts make its blood and slaps
bool ragdollSettled(const mobj_t *thing);   // no ragdoll, or one come to rest (ragdoll_test.cpp's -ragdollloop)

// wounds.cpp
// Holes shot through monsters' sprites. woundMask: per texel of a drawn frame,
// 0 body, 1 hole, 2 dark rim, 3 red rim, 4 bright lip, 5 stain (false when none show).
void woundHit(mobj_t *target,fixed_t fx,fixed_t fy,fixed_t fz,fixed_t fdx,fixed_t fdy,fixed_t slope,int damage,const mobj_t *shooter=nullptr);
// Remember the main view so impacts can intersect the artwork after flinch,
// lifting, interpolation and liquid clipping, before damage changes its frame.
void woundView(const mobj_t *thing,const mobj_t *viewer,const Image &image,bool flipped,float x,float y,float feet,
               float top,float bottom,float rx,float ry,const BodyPose *pose=nullptr);
void forgetWounds(const mobj_t *thing);
void woundsShutdown();
// A monster's wounds as spray.cpp drips from them: height above the feet,
// across the drawing it was seen in, that view's turn from the monster's
// facing (radians), the hole's size and the tic it was last shot.
struct WoundSpot { float height,across,yaw,radius;int tic; };
int woundSpots(const mobj_t *thing,WoundSpot *out,int most);
bool woundMask(const mobj_t &thing,const Image &image,bool flipped,float rx,float ry,std::vector<byte> &mask);
void woundVertex(Vertex &vertex,byte state);
void maskedRow(std::vector<Vertex> &out,const std::vector<byte> &mask,int width,int r,bool flip,
               const std::function<Vertex(int,bool)> &corner);
void woundedQuads(std::vector<Vertex> &out,const Vertex &base,const Image &image,const std::vector<byte> &mask,
                  float x,float y,float top,float rx,float ry,bool flip,float rowHeight=1);

void splashesShutdown();
bool liquidProjectileImpact(mobj_t *missile);
bool liquidProjectileHidden(const mobj_t *missile);
boolean planeImpact(sector_t *sector,boolean ceiling,fixed_t fx,fixed_t fy,fixed_t fz);
void tickImpactParticles();
void fluidProjectileFlight(const mobj_t &thing);
void fluidRocketLaunch(mobj_t *source);
void fluidProjectileImpact(mobj_t *thing);
void tickFluidEffects();
void fluidBulletFlat(int sector,bool ceiling,float x,float y);
void fluidBFGHit(mobj_t *ball,mobj_t *victim);
void flatImpactParticles(float x,float y,float z,bool ceiling,Image &image,float light);
void doorMotion(sector_t *sector);
void craterCrumble(float x,float y,float z);
std::array<float,3> rubbleSurfaceColor(float x,float y,float z);
void surfaceChip(float x,float y,float z,float nx,float ny,float nz);
void rubbleImpact(int type,float x,float y,float z,float nx,float ny,float nz);
void solidBulletImpact(float x,float y,float z,float nx,float ny,float nz);
void doorDamage(line_t *line,int side,fixed_t x,fixed_t y,fixed_t z,int broken);
// A body come down at (x,y) in a liquid: rings, and a plunge as hard as its fall (units a tic) and mass.
// False when not over a liquid.
bool bodySplash(float x,float y,float fall,float mass,float radius);
void watchSplashes();
void buildRings(Uniforms &camera);
float liquidLight(int sector,Image &flat,float x,float y,float z,const Uniforms &camera);
std::array<float,3> gooColor(int sector,uint32_t seed,float x,float y,float z,const Uniforms &camera);
void buildBubbles(const Uniforms &camera);

// windows.cpp
void glassShutdown();
void glassShot(line_t *line,fixed_t x,fixed_t y,fixed_t z,fixed_t dx,fixed_t dy);
void glassBlast(float x,float y,float z,float radius);
void watchGlass();
void buildGlass(const Uniforms &camera);
bool planGlassCaptures(const Uniforms &camera);
// A pane's opening, for the weather and wear: its map line (frame to outer),
// the sectors, the pane's ends (halfway through the frame), sill and lintel,
// the unit normal pointing in from the outer side, and whether it is broken
// and looks out under the sky. The serial changes when the panes are found anew.
struct PaneView { int line,frame,outer,inner; float ax,ay,bx,by,bottom,top,inX,inY; bool broken,sky; uint32_t seed; };
unsigned windowPaneSerial();
int windowPaneCount();
bool windowPane(int index,PaneView &out);

// weather.cpp
// Rain under the sky (settings.weather): how hard it rains this tic (0-1),
// whether the ground and walls outside are wet (1 while it rains), how wet
// the windows (following the rain, drying slower than they soak), the wind carrying it
// (units a second). Per pane, how much rain a broken window has let in.
extern float rainNow,wetNow,glassWetNow;
extern float rainWind[2];
float paneSoak(int pane);
// Where the rain blows (unit vector); the ground's wetness under the sky
// baked into the floor light's alpha (1 + wetness * 254, 0: none).
void rainHeading(float &x,float &y);
void bakeWetness();
void watchWeather();
void environmentBlast(float x,float y,float z,float strength);
void tickEnvironment();
void buildEnvironment(const Uniforms &camera);
void buildElectricalArcs(const Uniforms &camera);
void resetEnvironment();
void debrisLiquidSplash(float x,float y,float z,int sector,float size);
void discoverRunoff(float x,float y,float z);
void buildRain(const Uniforms &camera);
void buildOutdoorWind(const Uniforms &camera);
void weatherShutdown();

// wear.cpp
// Wear drawn by world/wear.glsl: rain streaks under windows, wet patches
// where rain came in, leaking ceilings. Records follow the sector entries in the sector
// buffer from wearBase() on (0: none this frame).
float wearBase();
void appendWear(std::vector<std::array<float,4>> &info);
// Cracks in a few ceilings beside the sky, dripping while it rains (none
// otherwise): the room and the crack's center.
struct CeilingLeak { int sector; float x,y; uint32_t seed; };
const std::vector<CeilingLeak> &ceilingLeaks();
// Whether rain blown in through a broken window has soaked the floor at (x, y) in a sector.
bool rainSoaked(int sector,float x,float y);
void watchWear();
void wearShutdown();

// exposure.cpp
void exposureShutdown();
void updateExposure(Uniforms &camera);
void updateFlashlightTint(Uniforms &camera);
bool flashlightBounce(float position[3],float normal[2],std::array<float,3> &color,float &strength);

// Small helpers and templates shared across files.
// Adds the time since start to total while profiling.
inline uint64_t profileStart() {return profile.enabled?SDL_GetTicksNS():0;}
inline void profileAdd(double &total,uint64_t start) {if(profile.enabled)total+=(SDL_GetTicksNS()-start)/1e6;}
// loading.cpp: the stages of the loading pass (runLoading), in the order they
// run. Each marks its start; planLoading says which this pass will run (what
// the bar counts); work on the loading thread calls loadingTick to keep the
// screen drawn, with how much of its stage is done when it knows.
enum class LoadStage { Map, Textures, Sun, Lights, Grid, Bounce, Ambient, Flow, Count };
void planLoading(LoadStage stage,bool planned);
void loadingStage(LoadStage stage);
void loadingTick(float part=-1);
bool loadingOnThisThread();
// Whether the next world frame builds a level or bakes (scene.cpp).
bool bakesPending();
struct ProfileTimer { double &total; uint64_t start=profileStart(); ~ProfileTimer() {profileAdd(total,start);} };
inline uint8_t *flatCell(FlatLayer layer,size_t cell) {return &flatLightCells[4*((size_t)layer*mapWidth*mapHeight+cell)];}
inline float floorZ(const sector_t *sector) {return sectorHeight(sector,false);}
inline float ceilZ(const sector_t *sector) {return sectorHeight(sector,true);}
// Calls texel(cell index, x, y, z) for each texel of a wall strip, at a
// point just in front of the side, in its own sector.
template<class Texel> void forStripTexels(const BakeStrip &strip,Texel texel) {
    for(int column=0;column<strip.width;++column)for(int row=0;row<strip.height;++row) {
        float along=std::clamp((column+0.5f)/strip.scale,0.5f,strip.length-0.5f);
        float z=std::min(strip.low+(row+0.5f)/strip.scale,strip.high);
        texel((size_t)(strip.y+row)*atlasStride+strip.x+column,
              strip.ax+strip.dx*along+strip.dy*0.5f,strip.ay+strip.dy*along-strip.dx*0.5f,z);
    }
}
inline float luminance(const float color[3]) {return 0.299f*color[0]+0.587f*color[1]+0.114f*color[2];}
// The vertex mode bits that carry sector+1 for map-space lighting (vertex_modes.h).
inline unsigned sectorMode(const sector_t *sector) {return unsigned(sector-sectors+1)<<MODE_SECTOR_SHIFT;}
inline float gameSeconds() {return (leveltime+renderFraction)/(float)TICRATE;}
inline float randomRange(float low,float high) {return std::uniform_real_distribution<float>(low,high)(impactRandom);}
// Chris Wellons' lowbias32 integer hash (hash-prospector, public domain).
inline uint32_t hash32(uint32_t x) {x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;return x;}
inline float hashUnit(int x,int y,uint32_t seed) {return (hash32((uint32_t)x*73856093u^(uint32_t)y*19349663u^seed)&0xffffff)/16777216.0f;}
// Decal depth offset: grows with distance so depth precision never lets the
// surface below show through.
inline float decalLift(float x,float y,const Uniforms &camera) {return 0.15f+std::hypot(x-camera.eye[0],y-camera.eye[1])*0.0004f;}
// What flow was baked from: decoration shadows, and 2 with window light,
// which brings the outdoor light in itself, so flow then skips sky sectors.
inline int flowInputs() {return bakeThingsKey+(settings.windowLight&&settings.bakedLights?2:0);}
} // namespace doom3d
#endif

