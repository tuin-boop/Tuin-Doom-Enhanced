#include <math.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include "doomstat.h"
#include "p_arena.h"
#include "r_state.h"
#include "s_sound.h"
#include "sounds.h"
#include "m_arcade.h"
extern fixed_t tmdropoffz;
int p_keyarena=0,p_arenaradius=512,p_monsterrecovery=1;
void (*R_ArenaAnnouncement)(int safe);
#define ROUTE 1024
static struct ArenaState {
 int active,queued,count,cursor,spawned,wanted,attempts,next,budget,live;
 fixed_t x,y,radius;
 unsigned random;
 unsigned char types[NUMMOBJTYPES];
 struct {fixed_t x,y,z;} route[ROUTE];
 int wave,waveEnd;
 unsigned char spawnedTypes[NUMMOBJTYPES];
 int elapsed;
} arena;
/* Runtime-only movement history: rebuilt after map changes and save loads. */
static struct {mobj_t *actor;fixed_t x,y;int since,lastFail,checked;} recovery[1024];
static unsigned serial=~0u;
static unsigned roll(void){arena.random=arena.random*1664525u+1013904223u;return arena.random;}
static int supported(int type){switch(type){
 case MT_POSSESSED:case MT_SHOTGUY:case MT_CHAINGUY:case MT_TROOP:case MT_SERGEANT:
 case MT_SHADOWS:case MT_HEAD:case MT_KNIGHT:case MT_BRUISER:case MT_UNDEAD:
 case MT_FATSO:case MT_BABY:case MT_CYBORG:case MT_SPIDER:return 1;
 default:return 0;}}
static void initialize(void){
 thinker_t *th;
 if(serial==r_levelserial)return;
 memset(&arena,0,sizeof(arena));memset(recovery,0,sizeof(recovery));serial=r_levelserial;arena.random=12345+gamemap*97;
 for(th=thinkercap.next;th!=&thinkercap;th=th->next)if(th->function.acp1==(actionf_p1)P_MobjThinker){
  mobj_t *m=(mobj_t*)th;if(supported(m->type)&&!(m->flags&MF_ARENA))arena.types[m->type]=1;
 }
}
void P_ArenaKey(player_t *player){
 initialize();
 if(p_keyarena&&!netgame&&!demoplayback&&!demorecording&&!arcade_active&&player==&players[consoleplayer])arena.queued++;
}
int P_ArenaBlocks(mobj_t *thing,fixed_t x,fixed_t y){
 double oldd,newd,limit;
 if(serial!=r_levelserial||arena.active!=2||!p_keyarena||!(thing->flags&MF_SOLID)||thing->flags&MF_NOCLIP)return 0;
 if(!thing->player&&!(thing->flags&MF_COUNTKILL))return 0;
 oldd=hypot((thing->x-arena.x)/(double)FRACUNIT,(thing->y-arena.y)/(double)FRACUNIT);
 newd=hypot((x-arena.x)/(double)FRACUNIT,(y-arena.y)/(double)FRACUNIT);
 limit=(arena.radius-thing->radius)/(double)FRACUNIT;
 if(oldd<=limit)return newd>limit;
 return newd>limit&&newd>oldd; /* Allow an overlapping actor to move back inside. */
}
int P_ArenaView(float *x,float *y,float *radius,int *remaining){
 if(serial!=r_levelserial||arena.active!=2||!p_keyarena)return 0;
 *x=arena.x/(float)FRACUNIT;*y=arena.y/(float)FRACUNIT;*radius=arena.radius/(float)FRACUNIT;*remaining=arena.live+arena.wanted-arena.spawned;return 1;
}
int P_ArenaWave(void){return serial==r_levelserial&&arena.active?arena.wave:0;}
static int underCap(int type){
 int cap=(type==MT_CYBORG||type==MT_SPIDER)?1:mobjinfo[type].spawnhealth>=500?2:25;
 return arena.spawnedTypes[type]<cap;
}
void P_ArenaMoveResult(mobj_t *m,int moved){
 int i;
 if(!p_monsterrecovery||netgame||demoplayback||demorecording||arcade_active||serial!=r_levelserial||!(m->flags&MF_COUNTKILL)||m->health<=0)return;
 /* Direct-mapped runtime cache keeps chase overhead constant on big maps. */
 i=((uintptr_t)m>>4)%1024;
 if(recovery[i].actor!=m){memset(&recovery[i],0,sizeof(recovery[i]));}
 if(!recovery[i].actor){recovery[i].actor=m;recovery[i].x=m->x;recovery[i].y=m->y;recovery[i].since=leveltime;}
 if(P_AproxDistance(m->x-recovery[i].x,m->y-recovery[i].y)>8*FRACUNIT||leveltime-recovery[i].lastFail>2*TICRATE){
  recovery[i].x=m->x;recovery[i].y=m->y;recovery[i].since=leveltime;
 }
 if(!moved)recovery[i].lastFail=leveltime;
}
static void recoverMonster(mobj_t *m,player_t *p){
 int i,n;
 static const int dx[8]={1,1,0,-1,-1,-1,0,1},dy[8]={0,1,1,1,0,-1,-1,-1};
 i=((uintptr_t)m>>4)%1024;
 if(recovery[i].actor!=m||m->health<=0)return;
 if(leveltime-recovery[i].lastFail>2*TICRATE||P_AproxDistance(m->x-recovery[i].x,m->y-recovery[i].y)>8*FRACUNIT){
  recovery[i].since=leveltime;recovery[i].x=m->x;recovery[i].y=m->y;return;
 }
 /* Failed movement, not merely standing still firing, triggers recovery. */
 if(leveltime-recovery[i].since<3*TICRATE||leveltime-recovery[i].checked<TICRATE)return;
 recovery[i].checked=leveltime;
 for(n=0;n<8;++n){
  int d=(n+i)%8;
  if(P_TryMove(m,m->x+dx[d]*8*FRACUNIT,m->y+dy[d]*8*FRACUNIT)){
   m->movedir=d;m->movecount=8;recovery[i].since=leveltime;recovery[i].x=m->x;recovery[i].y=m->y;return;
  }
 }
 if(leveltime-recovery[i].since<6*TICRATE)return;
 int lockdown=p_keyarena&&arena.active==2&&(m->flags&MF_ARENA);
 if(!lockdown){
  /* Ordinary monsters never teleport just because a chase path is blocked.
     Require an invalid current footing/collision, then stay nearby. */
  int fits=P_CheckPosition(m,m->x,m->y);
  if(fits&&tmfloorz<=m->z+24*FRACUNIT&&tmceilingz-m->z>=m->height&&
     ((m->flags&MF_FLOAT)||tmfloorz-tmdropoffz<=24*FRACUNIT))return;
 }

 /* A genuinely wedged actor gets a teleport rescue to explored safe ground.
    Check occupancy first; never use telefragging P_TeleportMove here. */
 for(n=0;n<24;++n){
  fixed_t x,y;
  if(lockdown){if(!arena.count)break;int slot=roll()%arena.count;x=arena.route[slot].x;y=arena.route[slot].y;}
  else{int d=n%8,dist=32+(n/8)*16;x=m->x+dx[d]*dist*FRACUNIT;y=m->y+dy[d]*dist*FRACUNIT;}
  sector_t *sec=R_PointInSubsector(x,y)->sector;
  if(sec->special||P_ArenaBlocks(m,x,y)||P_AproxDistance(x-p->mo->x,y-p->mo->y)<128*FRACUNIT||
     P_AproxDistance(x-m->x,y-m->y)<32*FRACUNIT)continue;
  if(!P_CheckPosition(m,x,y)||tmceilingz-tmfloorz<m->height||tmfloorz-tmdropoffz>24*FRACUNIT)continue;
  fixed_t floor=tmfloorz,ceiling=tmceilingz,ox=m->x,oy=m->y,oz=m->z;
  if(!lockdown){mobj_t probe=*m;probe.x=x;probe.y=y;probe.z=floor;probe.subsector=R_PointInSubsector(x,y);if(!P_CheckSight(m,&probe))continue;}
  P_UnsetThingPosition(m);m->x=x;m->y=y;m->z=m->floorz=floor;m->ceilingz=ceiling;P_SetThingPosition(m);
  m->momx=m->momy=m->momz=0;if(lockdown)m->target=p->mo;m->reactiontime=18;m->movedir=8;m->movecount=0;
  recovery[i].since=leveltime;recovery[i].lastFail=leveltime;recovery[i].x=x;recovery[i].y=y;
  P_SpawnMobj(ox,oy,oz,MT_TFOG);P_SpawnMobj(x,y,floor,MT_TFOG);S_StartSound(m,sfx_telept);
  return;
 }
}
void P_ArenaForgetMonster(mobj_t *m){
 int i=((uintptr_t)m>>4)%1024;if(recovery[i].actor==m)memset(&recovery[i],0,sizeof(recovery[i]));
}
static void recoverTick(player_t *p){
 if(!p_monsterrecovery){memset(recovery,0,sizeof(recovery));return;}
 if(netgame||demoplayback||demorecording||arcade_active||!p->mo||p->health<=0||leveltime%8)return;
 for(int i=0;i<1024;++i)if(recovery[i].actor)recoverMonster(recovery[i].actor,p);
}
static int spawn(player_t *p){
 int i,type=-1,choices[NUMMOBJTYPES],n=0,slot; mobj_t *m;fixed_t x,y,z;sector_t *sec;
 int allowance=arena.budget/(arena.wanted-arena.spawned);
 if(!arena.count)return 0;
 slot=roll()%arena.count;x=arena.route[slot].x;y=arena.route[slot].y;
 if(hypot((x-arena.x)/(double)FRACUNIT,(y-arena.y)/(double)FRACUNIT)>arena.radius/(double)FRACUNIT-80)return 0;
 if(P_AproxDistance(x-p->mo->x,y-p->mo->y)<128*FRACUNIT)return 0;
 for(i=0;i<NUMMOBJTYPES;++i)if(arena.types[i]&&underCap(i)&&mobjinfo[i].spawnhealth<=allowance)choices[n++]=i;
 if(!n){int cheapest=999999;for(i=0;i<NUMMOBJTYPES;++i)if(arena.types[i]&&underCap(i)&&mobjinfo[i].spawnhealth<cheapest){type=i;cheapest=mobjinfo[i].spawnhealth;}}
 else type=choices[roll()%n];
 if(type<0)return 0;
 sec=R_PointInSubsector(x,y)->sector;z=sec->floorheight;
 if(sec->special||sec->ceilingheight-z<mobjinfo[type].height)return 0;
 m=P_SpawnMobj(x,y,z,type);
 if(!P_CheckPosition(m,x,y)||tmceilingz-tmfloorz<m->height||(!(m->flags&MF_FLOAT)&&tmfloorz-tmdropoffz>24*FRACUNIT)||!P_CheckSight(m,p->mo)) {P_RemoveMobj(m);return 0;}
 m->z=m->floorz=tmfloorz;m->ceilingz=tmceilingz;
 m->flags|=MF_ARENA;m->target=p->mo;m->reactiontime=18;
 if(m->info->seestate)P_SetMobjState(m,m->info->seestate);
 P_SpawnMobj(x,y,m->z,MT_TFOG);S_StartSound(m,sfx_telept);
 arena.budget-=m->info->spawnhealth;arena.spawned++;arena.spawnedTypes[type]++;totalkills++;return 1;
}
void P_ArenaTick(void){
 player_t *p=&players[consoleplayer];thinker_t *th;int live=0,i;
 initialize();
 recoverTick(p);
 if(!p_keyarena||netgame||demoplayback||demorecording||arcade_active||!p->mo||p->health<=0){arena.active=arena.queued=0;return;}
 if(!arena.active&&!(leveltime%4)){
  int last=(arena.cursor+ROUTE-1)%ROUTE;
  if(!arena.count||P_AproxDistance(p->mo->x-arena.route[last].x,p->mo->y-arena.route[last].y)>24*FRACUNIT){
   arena.route[arena.cursor].x=p->mo->x;arena.route[arena.cursor].y=p->mo->y;arena.route[arena.cursor].z=p->mo->z;
   arena.cursor=(arena.cursor+1)%ROUTE;if(arena.count<ROUTE)arena.count++;
  }
 }
 if(!arena.active&&arena.queued){
  int damage=p->ammo[am_clip]*10+(p->weaponowned[wp_shotgun]||p->weaponowned[wp_supershotgun]?p->ammo[am_shell]*70:0)+(p->weaponowned[wp_missile]?p->ammo[am_misl]*140:0)+(p->weaponowned[wp_plasma]||p->weaponowned[wp_bfg]?p->ammo[am_cell]*18:0);
  for(th=thinkercap.next;th!=&thinkercap;th=th->next)if(th->function.acp1==(actionf_p1)P_MobjThinker){mobj_t *m=(mobj_t*)th;if(m->flags&MF_ARENA)m->flags=(m->flags&~MF_ARENA)|MF_ARENAOLD;}
  memset(recovery,0,sizeof(recovery));arena.queued--;arena.active=2;arena.x=p->mo->x;arena.y=p->mo->y;
  arena.radius=(p_arenaradius<256?256:p_arenaradius>1024?1024:p_arenaradius)*FRACUNIT*3/2;
  arena.wanted=7+damage/800;if(arena.wanted>25)arena.wanted=25;
  arena.budget=damage/2;if(arena.budget<100)arena.budget=100;
  arena.elapsed=0;arena.spawned=arena.attempts=arena.live=0;arena.wave=1;arena.waveEnd=(arena.wanted+2)/3;
  memset(arena.spawnedTypes,0,sizeof(arena.spawnedTypes));
  /* 1.wav is 177870 frames at 44100 Hz; wait the full clip plus two tics. */
  arena.next=leveltime+(177870*TICRATE+44099)/44100+2;
  if(R_ArenaAnnouncement)R_ArenaAnnouncement(0);
  
 }
 if(!arena.active)return;
 if(++arena.elapsed>=120*TICRATE){
  /* Remove encounter actors without kills, drops, boss actions or explosions.
     Cancel queued keys too so the safety release cannot immediately relock. */
  for(th=thinkercap.next;th!=&thinkercap;th=th->next)if(th->function.acp1==(actionf_p1)P_MobjThinker){
   mobj_t *m=(mobj_t*)th;
   if(m->flags&MF_ARENA){if(m->health>0&&(m->flags&MF_COUNTKILL)&&totalkills>0)totalkills--;P_RemoveMobj(m);}
  }
  arena.active=arena.queued=arena.live=0;
  if(R_ArenaAnnouncement)R_ArenaAnnouncement(1);
  return;
 }
 if(arena.spawned<arena.waveEnd&&leveltime>=arena.next){
  for(i=0;i<24;++i){arena.attempts++;if(spawn(p))break;}
  arena.next=leveltime+12;
  if(arena.attempts>1200){
   /* Never relax size/species caps to fill an impossible wave. */
   arena.wanted=arena.waveEnd=arena.spawned;arena.wave=3;
   
  }
 }
 for(th=thinkercap.next;th!=&thinkercap;th=th->next)if(th->function.acp1==(actionf_p1)P_MobjThinker){mobj_t *m=(mobj_t*)th;if((m->flags&MF_ARENA)&&m->health>0){live++;}}
 arena.live=live;
 if(arena.active==2&&arena.spawned>=arena.waveEnd&&!live&&arena.wave<3){
  arena.wave++;arena.waveEnd=(arena.wanted*arena.wave+2)/3;arena.attempts=0;arena.next=leveltime+2*TICRATE;
  
 }
 if(arena.active==2&&arena.wave==3&&arena.spawned>=arena.wanted&&!live){arena.active=0;if(R_ArenaAnnouncement)R_ArenaAnnouncement(1);}
}
int P_ArenaSave(unsigned char *data){if(data){memcpy(data,"ARN3",4);memcpy(data+4,&arena,sizeof(arena));}return 4+sizeof(arena);}
int P_ArenaLoad(const unsigned char *data,int length){
 int size=P_ArenaSave(0),v2size=4+offsetof(struct ArenaState,elapsed),oldsize=4+offsetof(struct ArenaState,wave);
 memset(&arena,0,sizeof(arena));memset(recovery,0,sizeof(recovery));serial=~0u;
 if(length>=size&&!memcmp(data+length-size,"ARN3",4))memcpy(&arena,data+length-size+4,sizeof(arena));
 else if(length>=v2size&&!memcmp(data+length-v2size,"ARN2",4)){
  size=v2size;memcpy(&arena,data+length-size+4,size-4);
  /* Existing saves have no timer: grant a fresh two-minute safety window. */
  arena.elapsed=0;
 }
 else if(length>=oldsize&&!memcmp(data+length-oldsize,"ARN1",4)){
  size=oldsize;memcpy(&arena,data+length-size+4,size-4);
  arena.wave=3;arena.waveEnd=arena.wanted;arena.active=arena.active?2:0;
  /* Old single-wave saves finish their existing wave without adding monsters. */
  arena.wanted=arena.waveEnd=arena.spawned;
 }else return 0;
 if(arena.elapsed<0||arena.elapsed>120*TICRATE||arena.count<0||arena.count>ROUTE||arena.cursor<0||arena.cursor>=ROUTE||arena.wanted<0||arena.wanted>25||arena.spawned<0||arena.spawned>arena.wanted||arena.wave<0||arena.wave>3||arena.waveEnd>arena.wanted||arena.waveEnd<arena.spawned||arena.active<0||arena.active>2||(arena.active&&(arena.radius<256*FRACUNIT||arena.radius>1536*FRACUNIT))){memset(&arena,0,sizeof(arena));return size;}
 serial=r_levelserial;return size;
}
