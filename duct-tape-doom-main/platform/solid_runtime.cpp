#include "../../scene.h"
#include "../../map_source.h"
#include "../../map_world.h"
#include <sstream>
#include <chrono>
#include <cstring>
#include <algorithm>
extern "C" {
#include "doomstat.h"
#include "p_local.h"
#include "r_state.h"
#include "m_argv.h"
#include "w_wad.h"
#include "p_solid.h"
#include "r_main.h"
#include "p_digwall.h"
void P_ExplodeMissile(mobj_t *m);
extern int p_breakablewalls;
}
extern "C" {int sv_chain_collapses=1,sv_hanging_fragments=1,sv_collapse_size=12;}
extern "C" int SV_Edited(float x,float y,float z);
static lab::Scene scene;
static lab::MapWorld mapWorld;
extern "C" int SV_Edited(float x,float y,float z){return SV_MapMode()&&mapWorld.edited({x,y,z});}
static std::vector<std::array<fixed_t,2>> mapHeights;
static unsigned serial=~0u;
static std::string saved;
static float previousDoorZ=0;
static mobj_t *pendingMissile=nullptr;
static mobj_t *directImpact=nullptr;
static mobj_t *actorMissile=nullptr;
static int actorImpactTic=-1,actorImpactResult=0;
static float pendingPoint[6];
static int pendingOwner;
static float unit(fixed_t x){return x/(float)FRACUNIT;}
static bool sectorMoves(int s,int plane){
 auto& sector=sectors[s];
 if(sector.tag||sector.specialdata)return true;
 if(!plane)return false;
 if(sector.ceilingheight<=sector.floorheight)return true;
 for(int j=0;j<sector.linecount;++j){
  const auto* l=sector.lines[j];if(l->backsector!=&sector)continue;
  switch(l->special){case 1:case 26:case 27:case 28:case 31:case 32:case 33:case 34:case 117:case 118:return true;}
 }
 return false;
}
// Ordinary map adapter: original sector/line IDs are retained for subsequent
// material, mover and trigger integration. Internal self-referencing lines
// are not boundaries of a solid.
static lab::MapSource readMapSource(){
 std::vector<lab::MapRegion> regions;
 for(int s=0;s<numsectors;++s){
  auto& sector=sectors[s];lab::MapRegion region;
  region.sector=s;region.floor=unit(sector.floorheight);region.ceiling=unit(sector.ceilingheight);
  region.breakFloor=P_GlobalDigSolidFlat(s,0)!=0;region.breakCeiling=P_GlobalDigSolidFlat(s,1)!=0;
  region.movingFloor=sectorMoves(s,0);region.movingCeiling=sectorMoves(s,1);
  for(int j=0;j<sector.linecount;++j){
   auto* line=sector.lines[j];if(line->frontsector==line->backsector)continue;
   region.edges.push_back({{unit(line->v1->x),unit(line->v1->y),0},
    {unit(line->v2->x),unit(line->v2->y),0},int(line-lines)});
  }
  regions.push_back(std::move(region));
 }
 lab::MapSource result;result.reset(std::move(regions));return result;
}
extern "C" int SV_CheckMapSource(){
 auto source=readMapSource();auto* player=players[0].mo;if(!player)return 0;
 lab::Vec p{unit(player->x),unit(player->y),unit(player->z)+28};
 if(source.sample(p)>=0){printf("FAIL map source: player starts inside solid\n");return 0;}
 int tx=int(std::floor(p.x/64)),ty=int(std::floor(p.y/64)),tz=int(std::floor(p.z/64));
 auto a=source.tile(tx,ty,tz),b=source.tile(tx+1,ty,tz);
 for(int z=0;z<17;++z)for(int y=0;y<17;++y){
  if(a.density[a.index(16,y,z)]!=b.density[b.index(0,y,z)])return 0;
 }
 int checked=0;
 for(int s=0;s<numsectors;++s){
  auto& sector=sectors[s];if(sector.ceilingheight<=sector.floorheight)continue;
  for(int j=0;j<sector.linecount;++j){
   auto* l=sector.lines[j];if(l->frontsector==l->backsector)continue;
   // Interior-facing samples on both sides of each original boundary test
   // overlapping/adjacent region membership across the actual map topology.
   float x=(unit(l->v1->x)+unit(l->v2->x))*.5f;
   float y=(unit(l->v1->y)+unit(l->v2->y))*.5f;
   float z=(unit(sector.floorheight)+unit(sector.ceilingheight))*.5f;
   if(!std::isfinite(source.sample({x,y,z})))return 0;
   ++checked;
  }
 }
 printf("PASS ordinary map source E%dM%d: %zu sectors, %d boundary probes, player air, exact adjacent tile samples\n",
  gameepisode,gamemap,source.regionCount(),checked);
 return 1;
}
static lab::Vec position(const mobj_t *m){return {unit(m->x),unit(m->y),unit(m->z)};}
int SV_MapMode(){return M_CheckParm((char*)"-solidmaps")!=0&&!netgame&&!demoplayback&&!demorecording;}
int SV_Active(){
 if(SV_MapMode()){
  if(!numsectors||!sectors)return 0;
  if(serial!=r_levelserial){
   mapWorld.reset(readMapSource());mapHeights.clear();directImpact=nullptr;actorMissile=nullptr;
   for(int i=0;i<numsectors;++i)mapHeights.push_back({sectors[i].floorheight,sectors[i].ceilingheight});
   serial=r_levelserial;
  }
  return 1;
 }
 if(!M_CheckParm((char*)"-solidlab")||W_CheckNumForName((char*)"SOLIDLAB")<0||numsectors!=1||gameepisode!=1||gamemap!=1)return 0;
 if(serial!=r_levelserial){scene.createDoorTest();previousDoorZ=scene.doorZ;serial=r_levelserial;}
 return 1;
}
float SV_DoorRenderZ(float fraction){return paused?scene.doorZ:previousDoorZ+(scene.doorZ-previousDoorZ)*std::clamp(fraction,0.f,1.f);}
const lab::Scene *SV_Scene(){return !SV_MapMode()&&SV_Active()?&scene:nullptr;}
const lab::MapWorld *SV_MapWorld(){return SV_MapMode()&&SV_Active()?&mapWorld:nullptr;}
int SV_MapPosition(mobj_t *m,fixed_t x,fixed_t y){
 return SV_MapMode()&&SV_Active()&&mapWorld.affected({unit(x),unit(y),unit(m->z)},unit(m->radius),unit(m->height));
}
void SV_SectorChanged(sector_t *s){
 if(!SV_MapMode()||!SV_Active())return;
 int i=int(s-sectors);if(i<0||i>=numsectors)return;
 if(mapHeights[i][0]==s->floorheight&&mapHeights[i][1]==s->ceilingheight)return;
 mapHeights[i]={s->floorheight,s->ceilingheight};
 auto begin=std::chrono::steady_clock::now();
 mapWorld.heights(i,unit(s->floorheight),unit(s->ceilingheight));
 if(M_CheckParm((char*)"-profilesolid"))printf("SOLID sector %d tic %d %.3f ms\n",i,gametic,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count());
}
int SV_Slide(mobj_t* m){
 if(!SV_MapPosition(m,m->x+m->momx,m->y+m->momy))return 0;
 fixed_t dx=m->momx,dy=m->momy;
 // Surface collision has already rejected the full movement. Try tangent
 // directions, including diagonals for rounded crater rims, with projected
 // velocity so this cannot turn into a speed boost.
 float desiredX=unit(dx),desiredY=unit(dy),best=0;fixed_t bx=0,by=0;
 for(int i=0;i<16;++i){
  float angle=i*6.28318530718f/16,x=std::cos(angle),y=std::sin(angle);
  float amount=x*desiredX+y*desiredY;if(amount<=best)continue;
  fixed_t sx=(fixed_t)(x*amount*FRACUNIT),sy=(fixed_t)(y*amount*FRACUNIT),floor,ceiling;
  if(SV_Position(m,m->x+sx,m->y+sy,&floor,&ceiling)==1){best=amount;bx=sx;by=sy;}
 }
 if(best>0&&P_TryMove(m,m->x+bx,m->y+by)){m->momx=bx;m->momy=by;}
 else m->momx=m->momy=0;
 return 1;
}
static int solidPositionUncached(mobj_t *m,fixed_t x,fixed_t y,fixed_t *floor,fixed_t *ceiling){
 if(!SV_Active())return -1;
 float height=std::max(1.f,unit(m->height)),radius=std::min(unit(m->radius),height*.5f);
 lab::Vec p{unit(x),unit(y),unit(m->z)};
 bool mapped=SV_MapMode();
 if(mapped&&!SV_MapPosition(m,x,y))return -1;
 auto fits=[&](lab::Vec at){return mapped?mapWorld.fits(at,radius,height):scene.fits(at,radius,height);};
 if(!fits(p)){
  bool stepped=false;
  // A crater can lower support by less than our two-unit search stride.
  // Test the exact sector floor first: a full-height actor in a low room
  // cannot overshoot that floor without colliding with the ceiling.
  if(mapped&&!(m->flags&MF_MISSILE)){
   float floorZ=unit(R_PointInSubsector(x,y)->sector->floorheight);
   if(floorZ>p.z&&floorZ-p.z<=24&&fits({p.x,p.y,floorZ})){
    p.z=floorZ;stepped=true;
   }
  }
  // Doom step height is retained; missiles cannot step up.
  for(float dz=2;!stepped&&dz<=((m->flags&MF_MISSILE)?0:24);dz+=2)
   if(fits(p+lab::Vec{0,0,dz})){p.z+=dz;stepped=true;break;}
  if(!stepped)return 0;
 }
 // Missiles use the swept impact ray and the capsule fit above. They do not
 // stand on slopes, so searching for walkable support below/above a crater
 // adds no useful collision information.
 if(m->flags&MF_MISSILE){
  if(mapped){*floor=m->z-1024*FRACUNIT;*ceiling=m->z+1024*FRACUNIT;return 1;}
  *floor=(fixed_t)(scene.terrain.origin.z*FRACUNIT);
  *ceiling=(fixed_t)((scene.terrain.origin.z+(scene.terrain.nz-1)*scene.terrain.spacing)*FRACUNIT);
  return 1;
 }
 // The first vertical surface bounds the search, so it cannot jump through
 // a thin shelf. Refine capsule support only when an edge/slope needs it.
 auto bound=[&](bool upward){
  lab::Hit hit;int owner;
  lab::Vec start=p+lab::Vec{0,0,height*.5f},direction{0,0,upward?1.f:-1.f};
  if(!(mapped?mapWorld.ray(start,direction,4224,hit):scene.ray(start,direction,1024,hit,owner)))return p.z;
  float target=hit.point.z-(upward?height:0);
  if(fits({p.x,p.y,target}))return target;
  float good=p.z,bad=target;
  for(int i=0;i<16&&std::abs(good-bad)>.01f;++i){
   float mid=(good+bad)*.5f;
   if(fits({p.x,p.y,mid}))good=mid;else bad=mid;
  }
  return good;
 };
 *floor=(fixed_t)(bound(false)*FRACUNIT);*ceiling=(fixed_t)((bound(true)+height)*FRACUNIT);
 return 1;
}
int SV_Position(mobj_t *m,fixed_t x,fixed_t y,fixed_t *floor,fixed_t *ceiling){
 if(!SV_Active())return -1;
 if(!SV_MapMode())return solidPositionUncached(m,x,y,floor,ceiling);
 struct Support {std::array<fixed_t,6> query{};uint64_t revision=~uint64_t(0);int result=-1;fixed_t floor=0,ceiling=0;};
 static std::map<mobj_t*,Support> supports;
 static unsigned level=~0u;
 if(level!=r_levelserial||supports.size()>4096){supports.clear();level=r_levelserial;}
 auto& support=supports[m];
 std::array<fixed_t,6> query{x,y,m->z,m->height,m->radius,m->flags&MF_MISSILE};
 auto stamp=mapWorld.supportStamp({unit(x),unit(y),unit(m->z)},unit(m->radius));
 if(support.query==query&&support.revision==stamp){
  if(support.result==1){*floor=support.floor;*ceiling=support.ceiling;}
  return support.result;
 }
 int result=solidPositionUncached(m,x,y,floor,ceiling);
 support.query=query;support.revision=mapWorld.supportStamp({unit(x),unit(y),unit(m->z)},unit(m->radius));support.result=result;
 if(result==1){support.floor=*floor;support.ceiling=*ceiling;}
 return result;
}
void SV_Refresh(mobj_t *m){
 if(!SV_Active()||m->type==MT_PUFF)return;
 // A spent, gravity-free projectile is only an explosion animation. Its
 // impact point may overlap the crater rim; trying to step its old collision
 // capsule out of that rim every tic is both unnecessary and expensive.
 if((m->info->flags&MF_MISSILE)&&!(m->flags&MF_MISSILE)
    &&(m->flags&MF_NOGRAVITY))return;
 auto started=std::chrono::steady_clock::now();
 fixed_t floor,ceiling;
 if(SV_Position(m,m->x,m->y,&floor,&ceiling)==1){m->floorz=floor;m->ceilingz=ceiling;}
 if(M_CheckParm((char*)"-profilesolid")){double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();if(ms>.3)printf("SOLID support tic %d type %d flags %x %.3f ms\n",gametic,m->type,m->flags,ms);}
}
struct PendingCollapse {size_t cut;int due,generation;};
struct FallingSupport {lab::Vec start;int due,generation;};
static std::vector<PendingCollapse> pendingCollapses;
static std::vector<FallingSupport> fallingSupports;
static unsigned collapseLevel=~0u;
static int collapseGeneration=0,collapseTic=-1;
static void impact(float *point,int owner,float radius,float depth);
static void collapseSync(){
 if(collapseLevel!=r_levelserial||!sv_chain_collapses||!p_breakablewalls){pendingCollapses.clear();fallingSupports.clear();collapseLevel=r_levelserial;}
}
static void tickCollapses(){
 collapseSync();if(!sv_chain_collapses||!p_breakablewalls||collapseTic==leveltime||leveltime%4)return;collapseTic=leveltime;
 for(auto it=pendingCollapses.begin();it!=pendingCollapses.end();++it)if(it->due<=leveltime){
  auto item=*it;pendingCollapses.erase(it);
  auto begin=std::chrono::steady_clock::now();
  auto removed=mapWorld.crumble(std::clamp(sv_collapse_size,4,24),item.cut,24);
  if(M_CheckParm((char*)"-profilesolid"))printf("SOLID collapse tic %d cut %zu removed %zu %.3f ms\n",leveltime,item.cut,removed.size(),std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count());
  int falling=0;
  for(auto p:removed){
   if(R_CraterCrumble)R_CraterCrumble(p.x,p.y,p.z);
   if(item.generation>=2||falling>=2||fallingSupports.size()>=8)continue;
   lab::Hit hit;
   if(mapWorld.ray(p,{0,0,-1},192,hit)&&hit.distance>12){
    int delay=int(std::sqrt(2*hit.distance/420)*TICRATE);
    fallingSupports.push_back({p,leveltime+std::max(4,delay),item.generation+1});++falling;
   }
  }
  break;
 }
 for(auto it=fallingSupports.begin();it!=fallingSupports.end();++it)if(it->due<=leveltime){
  auto fall=*it;fallingSupports.erase(it);lab::Hit hit;
  if(mapWorld.ray(fall.start,{0,0,-1},192,hit)&&hit.normal.z>.5f){
   float point[6]={hit.point.x,hit.point.y,hit.point.z,hit.normal.x,hit.normal.y,hit.normal.z};
   collapseGeneration=fall.generation;impact(point,0,6,.4f);collapseGeneration=0;
   if(R_RubbleImpact)R_RubbleImpact(MT_TROOPSHOT,point[0],point[1],point[2],point[3],point[4],point[5]);
  }
  break;
 }
}
void SV_Tick(){
 if(!SV_Active()||!players[consoleplayer].mo)return;
 if(SV_MapMode()){
  tickCollapses();
  for(int i=0;i<numsectors;++i)if(mapHeights[i][0]!=sectors[i].floorheight||mapHeights[i][1]!=sectors[i].ceilingheight){
   SV_SectorChanged(&sectors[i]);
  }
  return;
 }
 auto *p=players[consoleplayer].mo;float previous=scene.doorZ;previousDoorZ=previous;
 if(scene.doorZ==scene.doorTarget)return;
 scene.tick(1.f/TICRATE,position(p),unit(p->radius),unit(p->height));
 // Other actors can obstruct the leaf too.
 for(int i=0;i<numsectors;++i)for(mobj_t *m=sectors[i].thinglist;m;m=m->snext){
  if(!(m->flags&MF_SOLID)||m==p)continue;
  float height=std::max(1.f,unit(m->height)),radius=std::min(unit(m->radius),height*.5f);
  if(!scene.door.fits(position(m)-lab::Vec{0,0,scene.doorZ},radius,height,false)){
   scene.doorZ=previous;scene.doorTarget=scene.doorTravel;return;
  }
 }
}
void SV_Use(player_t *p){
 if(!SV_Active()||!p->mo)return;
 if(std::abs(unit(p->mo->x)-128)<80&&unit(p->mo->y)>24&&unit(p->mo->y)<168)
  scene.toggleDoor();
}
void SV_Player(player_t *p){
 if(!SV_Active()||SV_MapMode())return;
 p->mo->z=FRACUNIT/10;
 p->weaponowned[wp_missile]=true;p->weaponowned[wp_shotgun]=true;
 p->ammo[am_misl]=50;p->ammo[am_shell]=50;
 p->readyweapon=wp_missile;p->pendingweapon=wp_nochange;
 p->message="SOLID GEOMETRY TEST - USE OPENS DOOR";
 SV_Refresh(p->mo);
}
float SV_Trace(float x,float y,float z,float dx,float dy,float dz,float *point,int *owner){
 if(!SV_Active())return 1;
 lab::Vec delta{dx,dy,dz};float distance=lab::length(delta);if(distance<.001f)return 1;
 lab::Hit hit;int selected;
 if(SV_MapMode()){
  auto traceStart=std::chrono::steady_clock::now();
  bool traced=mapWorld.ray({x,y,z},delta*(1/distance),distance,hit);
  if(M_CheckParm((char*)"-profilegamesteps")){
   double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-traceStart).count();
   if(ms>2)printf("SOLID trace tic %d range %.1f %.3f ms\n",gametic,distance,ms);
  }
  if(!traced)return 1;
  selected=0;
 }else if(!scene.ray({x,y,z},delta*(1/distance),distance,hit,selected))return 1;
 if(point){point[0]=hit.point.x;point[1]=hit.point.y;point[2]=hit.point.z;
  point[3]=hit.normal.x;point[4]=hit.normal.y;point[5]=hit.normal.z;}
 if(owner)*owner=selected;
 return hit.distance/distance;
}
static void impact(float *point,int owner,float radius,float depth=0){
 if(!SV_Active())return;
 if(SV_MapMode()){
  if(!p_breakablewalls)return;
  int sector=-1,plane=0;float anchor=0;
  lab::Vec origin{point[0],point[1],point[2]},inward{-point[3],-point[4],-point[5]};
  float gain=std::abs(point[5])>.8f?1.f:std::clamp(float(p_wall_damage)/100,1.f,5.f);
  float stretch=depth>0?std::max(.01f,depth*2/radius)*gain:gain;
  lab::Vec center=origin+inward*(depth>0?-depth*gain:radius*.4f*gain);
  mapWorld.solidOwner(origin+inward*.5f,sector,plane,anchor);
  if(sector>=0&&std::abs(point[5])>.8f&&!P_GlobalDigSolidFlat(sector,plane))return;
  if(sector>=0&&!sectorMoves(sector,plane))sector=-1;
  if(M_CheckParm((char*)"-profilesolid"))printf("MAP impact %.2f %.2f %.2f normal %.2f %.2f %.2f owner %d plane %d anchor %.2f\n",
   point[0],point[1],point[2],point[3],point[4],point[5],sector,plane,anchor);
  float cap=1e9f;
  bool door=sector>=0&&plane==1&&std::abs(point[5])<.5f;
  if(!(door?p_breachdoors:p_throughwalls)){
   // Retain a backing layer before the next original air interval. This
   // stops a cosmetic crater silently opening a shortcut with breaches off.
   bool entered=false;
   for(float d=1;d<=P_GlobalDigMaxDepth()+4;d+=2){
    float solid=mapWorld.originalDepth(origin+inward*d);
    if(solid>0)entered=true;
    else if(entered){cap=std::max(0.f,d-4);break;}
   }
  }
  auto blastStart=std::chrono::steady_clock::now();
  bool applied=mapWorld.blast(center,radius,sector,plane,anchor,origin,inward,cap,float(P_GlobalDigMaxDepth()),stretch);
  if(M_CheckParm((char*)"-profilesolid"))printf("SOLID map blast tic %d radius %.2f depth %.2f: %.3f ms\n",gametic,radius,depth,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-blastStart).count());
  if(M_CheckParm((char*)"-profilesolid"))printf("MAP cut applied %d center %.2f %.2f %.2f sample %.2f\n",
   int(applied),center.x,center.y,center.z,mapWorld.sample(origin+inward*.5f));
  if(depth>0&&depth<=.5f&&R_SurfaceChip)R_SurfaceChip(point[0],point[1],point[2],point[3],point[4],point[5]);
  if(applied){
   collapseSync();
   // Dynamic doors/lifts retain their existing immediate tiny-fragment cleanup.
   if(sv_chain_collapses&&sector<0&&radius>=4){
    // A full queue drops extra cosmetic support checks rather than falling
    // back to synchronous scans during sustained plasma fire.
    auto nearby=std::find_if(pendingCollapses.begin(),pendingCollapses.end(),[&](const auto &p){return mapWorld.cutNear(p.cut,center,24);});
    if(nearby!=pendingCollapses.end()){nearby->cut=mapWorld.cutCount()-1;nearby->generation=std::min(nearby->generation,collapseGeneration);}
    else if(pendingCollapses.size()<16)pendingCollapses.push_back({mapWorld.cutCount()-1,leveltime+(sv_hanging_fragments?18:1),collapseGeneration});
   }else for(auto p:mapWorld.crumble())if(R_CraterCrumble)R_CraterCrumble(p.x,p.y,p.z);
  }
  return;
 }
 auto begin=std::chrono::steady_clock::now();
 scene.blast({point[0]-point[3]*radius*.4f,point[1]-point[4]*radius*.4f,point[2]-point[5]*radius*.4f},radius,owner);
 if(M_CheckParm((char*)"-profilesolid"))printf("SOLID impact owner %d radius %.0f: %.3f ms\n",owner,radius,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count());
}
void SV_Impact(float *point,int owner,float radius){
 if(SV_MapMode()&&radius<=2)impact(point,owner,4,.12f);
 else impact(point,owner,radius);
}
void SV_Missile(mobj_t *m){
 if(!SV_Active()||!(m->flags&MF_MISSILE))return;
 float hit[6];int owner;
 float f=SV_Trace(unit(m->x),unit(m->y),unit(m->z),unit(m->momx),unit(m->momy),unit(m->momz),hit,&owner);
 if(f>=1)return;
 P_UnsetThingPosition(m);
 m->x=(fixed_t)((hit[0]+hit[3]*.2f)*FRACUNIT);m->y=(fixed_t)((hit[1]+hit[4]*.2f)*FRACUNIT);m->z=(fixed_t)((hit[2]+hit[5]*.2f)*FRACUNIT);
 P_SetThingPosition(m);
 if(P_RomeroStick(m,hit[3],hit[4],hit[5]))return;
 pendingMissile=m;pendingOwner=owner;std::copy(hit,hit+6,pendingPoint);
 P_ExplodeMissile(m);
 pendingMissile=nullptr;
}
// Actor collisions stop a missile before its forward surface trace. Probe
// the victim's actual support, including excavated floors, before death moves it.
int SV_ActorGroundImpact(mobj_t *m,mobj_t *victim){
 if(!m||!victim||!SV_MapMode()||!SV_Active()||!p_breakablewalls)return 0;
 if(!(m->target&&m->target->player?p_player_surface_damage:p_monster_surface_damage))return 0;
 float hit[6];int owner;
 if(SV_Trace(unit(victim->x),unit(victim->y),unit(victim->z)+2,0,0,-6,hit,&owner)>=1||hit[5]<.5f)return 0;
 float radius=m->type==MT_PLASMA?8.f:24.f;
 float depth=m->type==MT_ROCKET?4.f:m->type==MT_BFG?8.f:m->type==MT_PLASMA?.35f:.5f;
 impact(hit,owner,radius,depth);
 if(R_RubbleImpact)R_RubbleImpact(m->type,hit[0],hit[1],hit[2],hit[3],hit[4],hit[5]);
 return 1;
}
void SV_MonsterContact(mobj_t *m,mobj_t *victim){
 if(!SV_MapMode()||!SV_Active())return;
 actorMissile=m;actorImpactTic=gametic;
 actorImpactResult=SV_ActorGroundImpact(m,victim);
}
int SV_Explode(mobj_t *m){
 if(!SV_Active()||!(m->flags&MF_MISSILE))return 0;
 directImpact=nullptr;
 if(actorMissile==m&&actorImpactTic==gametic){
  actorMissile=nullptr;
  // A direct actor collision must not also carve a wall behind the victim.
  // Rocket radius damage still runs, but does not repeat this surface cut.
  directImpact=m;return actorImpactResult;
 }
 if(SV_MapMode()&&
    !(m->target&&m->target->player?p_player_surface_damage:p_monster_surface_damage))return 0;
 float hit[6];int owner;
 if(pendingMissile==m){std::copy(pendingPoint,pendingPoint+6,hit);owner=pendingOwner;}
 else {
  lab::Vec direction=lab::normalized({unit(m->momx),unit(m->momy),unit(m->momz)});
  float range=lab::length({unit(m->momx),unit(m->momy),unit(m->momz)})+unit(m->radius)+2;
  if(SV_Trace(unit(m->x),unit(m->y),unit(m->z),direction.x*range,direction.y*range,direction.z*range,hit,&owner)>=1)return 0;
 }
 if(SV_MapMode()){
  float radius=m->type==MT_PLASMA?8.f:24.f;
  float depth=m->type==MT_ROCKET?4.f:m->type==MT_BFG?8.f:m->type==MT_PLASMA?.35f:.5f;
  impact(hit,owner,radius,depth);
  if(p_breakablewalls&&R_RubbleImpact)R_RubbleImpact(m->type,hit[0],hit[1],hit[2],hit[3],hit[4],hit[5]);
 }else SV_Impact(hit,owner,m->type==MT_ROCKET?18.f:4.f);
 directImpact=m;return 1;
}
void SV_Blast(mobj_t *m,int radius){
 if(!SV_MapMode()||!SV_Active()||!p_breakablewalls)return;
 if(m->type!=MT_BARREL&&!(m->target&&m->target->player?p_player_surface_damage:p_monster_surface_damage))return;
 if(directImpact==m){directImpact=nullptr;return;}
 lab::Vec start=position(m);if(m->type==MT_BARREL)start.z+=unit(m->height)*.5f;
 float nearest=std::min(96.f,float(radius)),best[6];bool found=false;
 for(int z=-1;z<=1;++z)for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
  if(!x&&!y&&!z)continue;
  lab::Vec d=lab::normalized({float(x),float(y),float(z)});float point[6];int owner;
  float fraction=SV_Trace(start.x,start.y,start.z,d.x*nearest,d.y*nearest,d.z*nearest,point,&owner);
  if(fraction<1){nearest*=fraction;std::copy(point,point+6,best);found=true;}
 }
 if(found){
  impact(best,0,24.f,std::max(.5f,4.f*(1-nearest/128)));
  if(R_RubbleImpact)R_RubbleImpact(m->type==MT_BARREL?MT_ROCKET:m->type,best[0],best[1],best[2],best[3],best[4],best[5]);
 }
}
int SV_Sight(mobj_t *a,mobj_t *b){
 if(!SV_Active())return -1;
 for(float factor:{.25f,.5f,.75f})
  if(SV_Trace(unit(a->x),unit(a->y),unit(a->z)+unit(a->height)*.75f,
     unit(b->x-a->x),unit(b->y-a->y),unit(b->z)+unit(b->height)*factor-unit(a->z)-unit(a->height)*.75f,nullptr,nullptr)>=1)return 1;
 return 0;
}
int SV_SaveSize(){
 if(!SV_Active())return 0;
 std::ostringstream stream(std::ios::binary);
 if(SV_MapMode())mapWorld.save(stream);else scene.save(stream);
 saved=stream.str();return (int)saved.size()+8;
}
int SV_Save(unsigned char *data){
 if(!SV_Active())return 0;
 int length=(int)saved.size();memcpy(data,saved.data(),length);memcpy(data+length,&length,4);memcpy(data+length+4,"SVEN",4);return length+8;
}
void SV_Load(const unsigned char *data,int length){
 pendingCollapses.clear();fallingSupports.clear();collapseLevel=r_levelserial;
 if(!SV_Active()||length<8||memcmp(data+length-4,"SVEN",4))return;
 int size;memcpy(&size,data+length-8,4);if(size<0||size>length-8)return;
 std::string bytes((const char*)data+length-8-size,size);std::istringstream stream(bytes,std::ios::binary);
 if(SV_MapMode()){
  // The native save has already restored the sector heights. Rebuild the
  // source from those heights before applying anchored damage records.
  mapWorld.reset(readMapSource());mapHeights.clear();
  for(int i=0;i<numsectors;++i)mapHeights.push_back({sectors[i].floorheight,sectors[i].ceilingheight});
  if(!mapWorld.load(stream))printf("Solid map damage record was rejected; no geometry edits restored.\n");
  else if(sv_chain_collapses){
   size_t first=mapWorld.cutCount()>16?mapWorld.cutCount()-16:0;
   for(size_t i=first;i<mapWorld.cutCount();++i)if(mapWorld.staticCut(i))pendingCollapses.push_back({i,leveltime+18,0});
  }
 }else {scene.load(stream);previousDoorZ=scene.doorZ;}
}
