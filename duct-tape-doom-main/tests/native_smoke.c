/* Exercise an actual level and complete native save/load without desktop input. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "doomdef.h"
#include "doomstat.h"
#include "d_main.h"
#include "g_game.h"
#include "i_system.h"
#include "m_argv.h"
#include "r_state.h"
#include "r_main.h"
#include "g_mapinfo.h"
#include "s_sound.h"
#include "sounds.h"
#include "p_local.h"
#include "p_digwall.h"
#include "p_solid.h"
#include "p_fluid.h"
#include "p_notices.h"
#include "m_bbox.h"
#include "d_items.h"
#include "st_stuff.h"
#ifdef DOOM_RENDER3D
#include "../platform/i_render3d.h"
#include "../platform/mod_soundbank.h"
#endif
extern void A_PosAttack(mobj_t *actor);
extern void P_XYMovement(mobj_t *actor);
extern unsigned I_NamedSoundStarts(void);
#include <SDL3/SDL.h>
#ifndef DOOM_TEST_REAL_STARTUP
#include <SDL3/SDL_main.h>
#endif

static int notice_count(int kind){noticeevent_t e;int n=0;while(P_PopNotice(0,&e))if(e.kind==kind)++n;return n;}
static void test_notices(void){
    player_t* p=&players[0];int oldtime=leveltime,oldkills=p->killcount,oldhealth=p->health;
    int oldweapon=p->readyweapon;unsigned char saved[NOTICE_SAVE_BYTES];
    P_NoticeNewGame();P_NoticeLevel();
    P_NoticeWeapon(p,wp_bfg);P_NoticeWeapon(p,wp_bfg);
    if(notice_count(N_WEAPON)!=1)I_Error("Weapon discovery did not deduplicate");
    if(P_SaveNotices(saved)!=NOTICE_SAVE_BYTES)I_Error("Notice save size");
    P_NoticeNewGame();P_LoadNotices(saved,sizeof(saved));P_NoticeWeapon(p,wp_bfg);
    if(notice_count(N_WEAPON))I_Error("Saved weapon discovery replayed");
    for(int k=0;k<N_COUNT;++k){int mask=0;noticeevent_t e;
        for(int v=0;v<3;++v)P_Notice(0,k,0);
        while(P_PopNotice(0,&e))mask|=1<<e.variant;
        if(mask!=7)I_Error("Missing notice variant %d",k);
    }
    mobj_t* boss=P_SpawnMobj(p->mo->x+256*FRACUNIT,p->mo->y,ONFLOORZ,MT_CYBORG);
    P_DamageMobj(boss,p->mo,p->mo,100000);P_RemoveMobj(boss);
    if(notice_count(N_CYBER)!=1)I_Error("Cyberdemon kill notice hook");
    boss=P_SpawnMobj(p->mo->x+256*FRACUNIT,p->mo->y,ONFLOORZ,MT_SPIDER);
    P_DamageMobj(boss,p->mo,p->mo,100000);P_RemoveMobj(boss);
    if(notice_count(N_SPIDER)!=1)I_Error("Mastermind kill notice hook");
    P_NoticeLevel();mobj_t target={0},barrel={0};target.info=&mobjinfo[MT_BRUISER];target.type=MT_BRUISER;barrel.type=MT_BARREL;
    for(int n=0;n<3;++n)P_NoticeKill(p->mo,&target,&barrel);
    noticeevent_t e;int chain=0,multi=0;while(P_PopNotice(0,&e)){chain+=e.kind==N_CHAIN;multi+=e.kind==N_MULTI;}
    if(chain!=1||multi!=1)I_Error("Barrel and multi-kill grouping");
    leveltime++;for(int n=0;n<3;++n)P_NoticeKill(p->mo,&target,&barrel);
    if(P_PopNotice(0,&e))I_Error("Combat notice cooldown");
    p->readyweapon=wp_fist;P_NoticeKill(p->mo,&target,p->mo);
    if(notice_count(N_FISTS)!=1)I_Error("Tough fist kill notice");
    P_NoticeLevel();p->health=5;P_NoticeDamage(p,1,&target);leveltime=((leveltime/35)+6)*35;
    P_NoticeTick(p);if(notice_count(N_CLOSE)!=1)I_Error("Close call after combat");
    // All four awards remain spent after the old cooldown and across save/load.
    p->readyweapon=wp_fist;
    for(int n=0;n<3;++n)P_NoticeKill(p->mo,&target,&barrel);
    P_NoticeKill(p->mo,&target,p->mo);while(P_PopNotice(0,&e)){}
    P_SaveNotices(saved);P_NoticeNewGame();P_LoadNotices(saved,sizeof(saved));
    leveltime+=35*30;
    for(int n=0;n<3;++n)P_NoticeKill(p->mo,&target,&barrel);
    P_NoticeKill(p->mo,&target,p->mo);
    P_NoticeDamage(p,1,&target);leveltime=((leveltime/35)+6)*35;P_NoticeTick(p);
    if(P_PopNotice(0,&e))I_Error("Once-per-map award replayed after save/load");
    P_NoticeLevel();
    for(int n=0;n<3;++n)P_NoticeKill(p->mo,&target,&barrel);
    P_NoticeKill(p->mo,&target,p->mo);
    P_NoticeDamage(p,1,&target);leveltime=((leveltime/35)+6)*35;P_NoticeTick(p);
    int awards=0;while(P_PopNotice(0,&e))if(e.kind>=N_CLOSE&&e.kind<=N_FISTS)awards|=1<<(e.kind-N_CLOSE);
    if(awards!=15)I_Error("New map did not reset combat awards");
    P_NoticeLevel();p_noticecombat=0;P_NoticeKill(p->mo,&target,p->mo);
    p_noticecombat=1;P_NoticeKill(p->mo,&target,p->mo);
    if(notice_count(N_FISTS)!=1)I_Error("Disabled notices consumed award");
    P_NoticeLevel();P_NoticeDamage(p,1,p->mo);leveltime=((leveltime/35)+6)*35;
    P_NoticeTick(p);if(notice_count(N_CLOSE))I_Error("Self damage triggered close call");
    P_NoticeLevel();P_NoticeDamage(p,1,NULL);leveltime=((leveltime/35)+6)*35;
    P_NoticeTick(p);if(notice_count(N_CLOSE))I_Error("Environmental damage triggered close call");
    P_NoticeLevel();P_NoticeDamage(p,1,&target);leveltime=((leveltime/35)+22)*35;
    P_NoticeTick(p);if(notice_count(N_CLOSE))I_Error("Stale enemy damage triggered close call");
    P_NoticeLevel();P_NoticeDamage(p,1,&target);P_SaveNotices(saved);
    saved[16]=1; // Old unattributed low-health flag in the first player's record.
    P_LoadNotices(saved,sizeof(saved));leveltime=((leveltime/35)+6)*35;
    P_NoticeTick(p);if(notice_count(N_CLOSE))I_Error("Legacy pending close call replayed");
    p->health=oldhealth;p->killcount=oldkills;p->readyweapon=oldweapon;leveltime=oldtime;
    P_NoticeNewGame();P_NoticeLevel();
    puts("Special notices: variants, save persistence, boss hooks, combat grouping and once-per-map awards passed.");
}

static int stage, saved_ammo, saved_health, player_flashes, enemy_flashes;
static int test_weapon=wp_pistol,test_ammo=am_clip;
static int test_episode=1,test_start;
static int routed_sounds;
static fixed_t aim_velocity[3];static int aim_exploded;
static void capture_aim_impact(mobj_t *m){aim_velocity[0]=m->momx;aim_velocity[1]=m->momy;aim_velocity[2]=m->momz;aim_exploded=1;}
static void test_projectile_pitch(void){
    boolean oldfree=freeaim;float oldpitch=tourpitch;void (*oldhook)(mobj_t*)=R_RocketImpact;
    R_RocketImpact=capture_aim_impact;freeaim=true;
    for(int kind=0;kind<3;++kind)for(int dir=-1;dir<=1;++dir){
        mobjtype_t type=kind==0?MT_ROCKET:kind==1?MT_PLASMA:MT_BFG;
        tourpitch=dir*.65f;aim_exploded=0;P_SpawnPlayerMissile(players[0].mo,type);
        mobj_t *m=(mobj_t*)thinkercap.prev;
        if(m->type!=type||m->target!=players[0].mo)I_Error("Missing aimed projectile");
        if(!aim_exploded){aim_velocity[0]=m->momx;aim_velocity[1]=m->momy;aim_velocity[2]=m->momz;}
        if((dir>0&&aim_velocity[2]<=0)||(dir<0&&aim_velocity[2]>=0)||(dir==0&&aim_velocity[2]!=0))I_Error("Projectile did not follow vertical aim");
        double speed=sqrt((double)aim_velocity[0]*aim_velocity[0]+(double)aim_velocity[1]*aim_velocity[1]+(double)aim_velocity[2]*aim_velocity[2]);
        if(fabs(speed-m->info->speed)>FRACUNIT*.02)I_Error("Aimed projectile changed total speed");
        P_RemoveMobj(m);
    }
    R_RocketImpact=oldhook;freeaim=oldfree;tourpitch=oldpitch;
    puts("Rocket/plasma/BFG upward, downward and level aim preserves projectile speed.");
}
static void test_fast_pistol(void){
    if(!M_CheckParm("-fluidweapons"))return;
    extern int p_fluidfastpistol;
    int setting=p_fluidfastpistol;
    player_t *p=&players[0];
    for(int fast=0;fast<=1;++fast){
        p_fluidfastpistol=fast;p->readyweapon=wp_pistol;p->pendingweapon=wp_nochange;
        p->weaponowned[wp_pistol]=true;p->ammo[am_clip]=50;p->cmd.buttons=BT_ATTACK;
        P_SetupPsprites(p);P_FireWeapon(p);
        for(int tic=0;tic<30;++tic)P_MovePsprites(p);
        if(p->ammo[am_clip]!=(fast?46:47))I_Error("Pistol cadence mismatch fast=%d ammo=%d",fast,p->ammo[am_clip]);
    }
    p->cmd.buttons=0;p_fluidfastpistol=setting;P_SetupPsprites(p);
    p->readyweapon=wp_missile;p->weaponowned[wp_missile]=true;p->ammo[am_misl]=10;P_SetupPsprites(p);P_FireWeapon(p);
    if(p->psprites[ps_weapon].tics!=2||p->ammo[am_misl]!=10)I_Error("Fluid rocket windup mismatch");
    P_MovePsprites(p);if(p->ammo[am_misl]!=10)I_Error("Rocket fired before its source tick");
    P_MovePsprites(p);if(p->ammo[am_misl]!=9||p->psprites[ps_weapon].tics!=12)I_Error("Rocket launch or recovery timing mismatch");
    p->readyweapon=wp_pistol;P_SetupPsprites(p);
    puts("Pistol: optional 10-tic fast and 14-tic normal firing cadence passed.");
}
static int aggressiveShotsCount,aggressiveShotTimes[16],aggressiveShotHits[16];
static mobj_t *aggressiveTestActor;
static void aggressive_flash(mobj_t *actor,int weapon,int projectile){
    if(actor==aggressiveTestActor&&aggressiveShotsCount<16)aggressiveShotTimes[aggressiveShotsCount++]=leveltime;
}
static void aggressive_contact(mobj_t *target,mobj_t *inflictor,mobj_t *source,int damage){
    if(source==aggressiveTestActor&&target==players[0].mo&&aggressiveShotsCount>0)aggressiveShotHits[aggressiveShotsCount-1]=1;
}
static void test_aggressive_ai(void){
    extern int p_fluidfastpistol;
    extern boolean P_CheckMissileRange(mobj_t *actor);
    int enabled=p_aggressiveai,oldtime=leveltime,fast=p_fluidfastpistol;
    fixed_t laneX=0,laneY=0;
    player_t *player=&players[0];mobj_t *target=player->mo;
    int cheats=player->cheats;fixed_t mx=target->momx,my=target->momy;
    void (*flash)(mobj_t*,int,int)=R_MuzzleFlash;
    void (*damageHook)(mobj_t*,mobj_t*,mobj_t*,int)=R_DamageHit;R_DamageHit=aggressive_contact;
    P_SetAggressiveAI(1);player->cheats|=CF_GODMODE;R_MuzzleFlash=aggressive_flash;
    for(int mode=0;mode<3;++mode){
        mobj_t *actor=NULL;p_fluidfastpistol=mode==1;
        for(int direction=0;direction<4;++direction){
            fixed_t x=target->x+(direction==0?64:direction==1?-64:0)*FRACUNIT;
            fixed_t y=target->y+(direction==2?64:direction==3?-64:0)*FRACUNIT;
            actor=P_SpawnMobj(x,y,ONFLOORZ,mode==2?MT_SHOTGUY:MT_POSSESSED);actor->target=target;actor->reactiontime=0;
            if(P_CheckPosition(actor,x,y)&&P_CheckMissileRange(actor))break;
            P_RemoveMobj(actor);actor=NULL;
        }
        if(!actor)I_Error("No clear firing lane for aggressive AI test");
        laneX=actor->x;laneY=actor->y;
        aggressiveTestActor=actor;aggressiveShotsCount=0;memset(aggressiveShotHits,0,sizeof(aggressiveShotHits));
        P_SetMobjState(actor,actor->info->missilestate);
        for(int tic=0;tic<700&&aggressiveShotsCount<12;++tic){++leveltime;P_MobjThinker(actor);}
        int expected=mode==2?37:(mode==1&&P_FluidWeaponsEnabled()?10:14);
        if(aggressiveShotsCount<12)I_Error("Aggressive shooter stopped firing mode %d",mode);
        int hitIntervals=0;
        for(int n=1;n<aggressiveShotsCount;++n){
            int interval=aggressiveShotTimes[n]-aggressiveShotTimes[n-1];
            if(aggressiveShotHits[n-1]){++hitIntervals;if(interval!=expected)I_Error("Aggressive cadence after hit mode %d: %d vs %d",mode,interval,expected);}
            else if(interval<expected)I_Error("Miss unexpectedly accelerated firing");
        }
        if(hitIntervals<5)I_Error("Not enough successful hits to test sustained firing");
        // Real projectile feedback: a miss requests a movement burst; a hit does not.
        P_AggressiveRemove(actor);mobj_t missile={0};P_AggressiveMissile(&missile,actor);P_AggressiveImpact(&missile);
        if(P_CheckMissileRange(actor))I_Error("Missed projectile did not request repositioning");
        P_AggressiveRemove(actor);P_AggressiveMissile(&missile,actor);P_AggressiveContact(target,&missile,actor);P_AggressiveImpact(&missile);
        if(!P_CheckMissileRange(actor))I_Error("Successful projectile incorrectly requested repositioning");
        actor->health=100;P_AggressiveDamage(actor,target,target,40);
        if(P_CheckMissileRange(actor))I_Error("Heavy damage did not stagger shooter");
        P_RemoveMobj(actor);
    }
    {
        extern void A_Chase(mobj_t*);
        mobj_t *fat=P_SpawnMobj(laneX,laneY,ONFLOORZ,MT_FATSO),aim;
        fat->target=target;fat->reactiontime=0;fat->threshold=100;
        for(int volley=0;volley<3;++volley)A_Chase(fat);
        if(!P_AggressiveBarrageAim(fat,&aim))I_Error("Mancubus did not start occasional barrage");
        fixed_t originalX=target->x;target->x+=128*FRACUNIT;
        if(!P_AggressiveBarrageAim(fat,&aim)||aim.x!=originalX)I_Error("Barrage incorrectly tracks moving target");
        target->x=originalX;leveltime+=161;
        if(P_AggressiveBarrageAim(fat,&aim))I_Error("Mancubus barrage did not expire");
        P_RemoveMobj(fat);
    }
    demoplayback=true;if(P_AggressiveEnabled())I_Error("Aggressive AI changed demos");demoplayback=false;
    P_SetAggressiveAI(0);if(P_AggressiveStateTics(target,S_POSS_ATK1,10)!=10)I_Error("Disabled AI changed animation timings");
    P_SetAggressiveAI(enabled);leveltime=oldtime;p_fluidfastpistol=fast;player->cheats=cheats;target->momx=mx;target->momy=my;R_MuzzleFlash=flash;R_DamageHit=damageHook;aggressiveTestActor=NULL;
    puts("Aggressive AI: live pistol/shotgun cadence, projectile hits/misses, heavy-hit stagger, fixed-position barrages and demo guards passed.");
}
static void test_advanced_tactics(void){
    int setting=p_advancedtactics,oldtime=leveltime;P_SetAdvancedTactics(1);
    mobj_t target={0},actor={0},aim;target.health=100;target.flags=MF_SHOOTABLE;
    target.x=512*FRACUNIT;target.momx=12*FRACUNIT;
    actor.health=100;actor.flags=MF_COUNTKILL;actor.target=&target;actor.type=MT_CYBORG;
    P_TacticsFired(&actor);
    if(!P_TacticsAim(&actor,&aim)||aim.x<=target.x||aim.x>target.x+96*FRACUNIT)I_Error("Predictive rocket aim not bounded");
    P_TacticsRemove(&actor);actor.type=MT_FATSO;
    if(!P_TacticsAim(&actor,&aim))I_Error("Focused mancubus volley missing");
    fixed_t locked=aim.x;target.x+=128*FRACUNIT;P_TacticsFired(&actor);
    if(!P_TacticsAim(&actor,&aim)||aim.x!=locked)I_Error("Focused volley followed target");
    angle_t focused=P_TacticsFatSpread(&actor);
    for(int n=1;n<6;++n)P_TacticsFired(&actor);
    if(P_TacticsFatSpread(&actor)<=focused)I_Error("Mancubus did not alternate wide volley");
    P_TacticsRemove(&actor);
    // Exercise safe vertical motion in an actual loaded sector.
    mobj_t *player=players[0].mo,*caco=P_SpawnMobj(player->x+64*FRACUNIT,player->y,ONFLOORZ,MT_HEAD);
    caco->target=player;caco->z=caco->ceilingz-caco->height-8*FRACUNIT;
    fixed_t before=caco->z;leveltime=(leveltime+1)&~1;P_TacticsTick(caco);
    if(caco->z+caco->height>caco->ceilingz||caco->z<caco->floorz||abs(caco->z-before)>FRACUNIT)I_Error("Cacodemon tactics exceeded clearance or speed");
    P_RemoveMobj(caco);
    // Lost souls lack MF_COUNTKILL. Exercise the real decoration collision path,
    // including aggressive attack gating and a direct charge action retry.
    {
        extern mobj_t *tmthing;extern fixed_t tmx,tmy;
        extern boolean PIT_CheckThing(mobj_t*),P_CheckMissileRange(mobj_t*);
        extern void A_SkullAttack(mobj_t*);
        mobj_t *skull=P_SpawnMobj(player->x+64*FRACUNIT,player->y,ONFLOORZ,MT_SKULL);
        mobj_t torch={0};torch.flags=MF_SOLID;torch.radius=16*FRACUNIT;torch.health=1000;
        torch.x=skull->x;torch.y=skull->y;
        mobj_t *savedThing=tmthing;fixed_t savedX=tmx,savedY=tmy;
        skull->target=player;skull->flags|=MF_SKULLFLY;skull->momx=20*FRACUNIT;
        tmthing=skull;tmx=skull->x;tmy=skull->y;
        if(PIT_CheckThing(&torch)||!P_TacticsAttackBlocked(skull)||skull->flags&MF_SKULLFLY)
            I_Error("Lost soul did not recover from solid decoration");
        int aggressive=p_aggressiveai;P_SetAggressiveAI(1);
        if(P_CheckMissileRange(skull))I_Error("Aggressive AI bypassed charge recovery");
        A_SkullAttack(skull);
        if(skull->flags&MF_SKULLFLY)I_Error("Direct charge bypassed recovery");
        leveltime+=2*TICRATE;
        if(P_TacticsAttackBlocked(skull))I_Error("Charge recovery never expired");
        P_SetAdvancedTactics(0);P_TacticsChargeBlocked(skull);
        if(P_TacticsAttackBlocked(skull))I_Error("Disabled tactics changed lost soul");
        P_SetAdvancedTactics(1);P_SetAggressiveAI(aggressive);
        tmthing=savedThing;tmx=savedX;tmy=savedY;P_RemoveMobj(skull);
    }
    demoplayback=true;if(P_TacticsEnabled())I_Error("Advanced tactics changed demo behavior");demoplayback=false;
    actor.target=NULL;if(P_TacticsMove(&actor)||P_TacticsAim(&actor,&aim))I_Error("Tactics woke unaware monster");
    P_SetAdvancedTactics(0);actor.target=&target;
    if(P_TacticsAim(&actor,&aim)||P_TacticsFatSpread(&actor)!=ANG90/8)I_Error("Disabled tactics changed attacks");
    P_SetAdvancedTactics(setting);leveltime=oldtime;
    puts("Advanced tactics: bounded prediction, alternating/fixed volleys, aerial clearance and disabled/demo guards passed.");
}
static void test_pursuit_guards(void){
    extern int prndindex;
    int setting=p_persistentpursuit,random=prndindex;
    mobj_t actor={0},target={0};actor.flags=MF_COUNTKILL;actor.target=&target;
    p_persistentpursuit=0;
    if(P_PersistentPursuit(&actor))I_Error("Disabled pursuit changed movement");
    p_persistentpursuit=1;demoplayback=true;
    if(P_PersistentPursuit(&actor))I_Error("Pursuit changed demo playback");
    demoplayback=false;demorecording=true;
    if(P_PersistentPursuit(&actor))I_Error("Pursuit changed demo recording");
    demorecording=false;netgame=true;
    if(P_PersistentPursuit(&actor))I_Error("Pursuit changed network movement");
    netgame=false;actor.target=NULL;
    if(P_PersistentPursuit(&actor))I_Error("Pursuit woke an unaware monster");
    if(actor.x||actor.y||prndindex!=random)I_Error("Pursuit guard changed state or random stream");
    p_persistentpursuit=setting;
    puts("Persistent pursuit: toggle, unaware monsters, demos, network and RNG guards passed.");
}
static void test_overlap_escape(void){
    extern mobj_t *tmthing;extern fixed_t tmx,tmy;extern boolean PIT_CheckThing(mobj_t*);
    mobj_t a={0},b={0};mobj_t *previous=tmthing;
    a.flags=b.flags=MF_COUNTKILL|MF_SOLID;a.health=b.health=150;
    a.radius=b.radius=30*FRACUNIT;b.x=b.y=48*FRACUNIT;tmthing=&a;
    tmx=0;tmy=-3*FRACUNIT;if(!PIT_CheckThing(&b))I_Error("Overlapping monsters cannot separate");
    tmy=3*FRACUNIT;if(PIT_CheckThing(&b))I_Error("Overlap escape allowed a deeper overlap");
    b.x=61*FRACUNIT;tmx=3*FRACUNIT;tmy=0;if(PIT_CheckThing(&b))I_Error("Overlap escape allowed a new collision");
    b.x=48*FRACUNIT;tmx=0;tmy=-3*FRACUNIT;demoplayback=true;
    if(PIT_CheckThing(&b))I_Error("Overlap escape changed demo behavior");demoplayback=false;
    {
        extern fixed_t tmbbox[4];extern boolean PIT_CheckLine(line_t*);
        vertex_t v1={0,0},v2={0,100*FRACUNIT};line_t line={0};
        line.v1=&v1;line.v2=&v2;line.dy=100*FRACUNIT;line.slopetype=ST_VERTICAL;
        line.bbox[BOXBOTTOM]=0;line.bbox[BOXTOP]=100*FRACUNIT;
        a.x=16*FRACUNIT;a.y=50*FRACUNIT;tmx=19*FRACUNIT;tmy=a.y;
        tmbbox[BOXLEFT]=tmx-a.radius;tmbbox[BOXRIGHT]=tmx+a.radius;
        tmbbox[BOXBOTTOM]=tmy-a.radius;tmbbox[BOXTOP]=tmy+a.radius;
        if(!PIT_CheckLine(&line))I_Error("Spawn wall escape failed");
        tmx=13*FRACUNIT;if(PIT_CheckLine(&line))I_Error("Spawn wall escape moved into wall");
        tmx=-19*FRACUNIT;if(PIT_CheckLine(&line))I_Error("Spawn wall escape crossed wall");
        a.x=32*FRACUNIT;tmx=29*FRACUNIT;if(PIT_CheckLine(&line))I_Error("Spawn wall escape allowed new overlap");
    }
    {
        extern boolean P_RetreatFromDropoff(mobj_t*,fixed_t,fixed_t);
        extern fixed_t tmbbox[4],tmfloorz;
        fixed_t savedFloor=tmfloorz;
        sector_t high={0},low={0};subsector_t sub={0};
        vertex_t v1={0,0},v2={0,100*FRACUNIT};line_t line={0};line_t *edges[]={&line};
        high.linecount=1;high.lines=edges;low.floorheight=-104*FRACUNIT;
        line.v1=&v1;line.v2=&v2;line.dy=100*FRACUNIT;line.slopetype=ST_VERTICAL;
        line.frontsector=&high;line.backsector=&low;sub.sector=&high;a.subsector=&sub;
        a.x=24*FRACUNIT;a.y=50*FRACUNIT;a.z=a.floorz=tmfloorz=0;
        tmbbox[BOXLEFT]=-3*FRACUNIT;tmbbox[BOXRIGHT]=57*FRACUNIT;
        tmbbox[BOXBOTTOM]=20*FRACUNIT;tmbbox[BOXTOP]=80*FRACUNIT;
        if(!P_RetreatFromDropoff(&a,27*FRACUNIT,a.y))I_Error("Lowered lift retreat rejected");
        if(P_RetreatFromDropoff(&a,21*FRACUNIT,a.y))I_Error("Retreat allowed approach to cliff");
        if(P_RetreatFromDropoff(&a,-3*FRACUNIT,a.y))I_Error("Retreat allowed falling off cliff");
        a.x=32*FRACUNIT;if(P_RetreatFromDropoff(&a,27*FRACUNIT,a.y))I_Error("Retreat allowed new ledge overlap");
        a.x=24*FRACUNIT;demoplayback=true;
        if(P_RetreatFromDropoff(&a,27*FRACUNIT,a.y))I_Error("Retreat changed demo behavior");
        demoplayback=false;tmfloorz=savedFloor;
        puts("Lowered lift: safe retreat, cliff avoidance and demo compatibility passed.");
    }
    tmthing=previous;
    puts("Monster overlap: separation, deeper/new collisions and demo compatibility passed.");
}
static void test_fluid_monsters(void){
    if(!P_FluidEnabled())return;
    extern int p_persistentpursuit;int savedPursuit=p_persistentpursuit;p_persistentpursuit=0;
    mobjtype_t types[]={MT_POSSESSED,MT_SHOTGUY,MT_CHAINGUY,MT_WOLFSS,MT_TROOP,MT_SERGEANT,MT_SHADOWS,MT_BRUISER,MT_KNIGHT,MT_UNDEAD,MT_FATSO,MT_VILE,MT_BABY,MT_CYBORG,MT_SPIDER,MT_HEAD,MT_PAIN,MT_SKULL};
    mobj_t *player=players[0].mo;
    int original=states[S_POSS_RUN1].tics;
    for(unsigned i=0;i<sizeof(types)/sizeof(types[0]);++i)for(int duration=2;duration<=4;++duration){
        mobj_t *actor=P_SpawnMobj(player->x-256*FRACUNIT,player->y,ONFLOORZ,types[i]);
        actor->flags|=MF_NOCLIP;actor->target=player;actor->reactiontime=999;
        actor->threshold=100;actor->movecount=10;actor->movedir=0;actor->angle=0;
        fixed_t start=actor->x;
        states[S_POSS_RUN1].tics=duration;
        P_SetMobjState(actor,S_POSS_RUN1);
        fixed_t step=(fixed_t)((int64_t)actor->info->speed*FRACUNIT/duration);
        if(actor->x-start!=step)I_Error("Fluid initial step type %d duration %d",types[i],duration);
        for(int t=1;t<duration;++t)P_FluidTick(actor);
        if(actor->x-start!=step*duration||actor->movecount!=9||actor->reactiontime!=998)
            I_Error("Fluid speed or AI cadence type %d duration %d",types[i],duration);
        fixed_t end=actor->x;P_FluidTick(actor);
        if(actor->x!=end)I_Error("Fluid extra substep");
        P_SetMobjState(actor,S_POSS_RUN1);actor->movedir=8;end=actor->x;P_FluidTick(actor);
        if(actor->x!=end)I_Error("Fluid direction cancellation");
        actor->movedir=0;P_SetMobjState(actor,S_POSS_RUN1);
        P_UnsetThingPosition(actor);actor->x+=48*FRACUNIT;actor->y+=48*FRACUNIT;P_SetThingPosition(actor);end=actor->x;P_FluidTick(actor);
        if(actor->x!=end)I_Error("Fluid diagonal teleport cancellation");
        actor->movedir=0;P_SetMobjState(actor,S_POSS_RUN1);end=actor->x;
        // Use a solid decoration: overlapping living monsters intentionally separate.
        mobj_t *blocker=P_SpawnMobj(actor->x,actor->y,actor->z,MT_BARREL);
        actor->flags&=~MF_NOCLIP;P_FluidTick(actor);
        if(actor->x!=end)I_Error("Fluid collision bypass type %d duration %d delta %d z %d blockerz %d",types[i],duration,actor->x-end,actor->z/FRACUNIT,blocker->z/FRACUNIT);
        P_RemoveMobj(blocker);actor->flags|=MF_NOCLIP;P_FluidTick(actor);
        if(actor->x!=end)I_Error("Fluid blocked sequence was not cancelled");
        P_SetMobjState(actor,S_POSS_RUN1);end=actor->x;actor->health=0;P_FluidTick(actor);
        if(actor->x!=end)I_Error("Fluid death cancellation");
        if(types[i]==MT_SKULL){
            actor->health=100;actor->movedir=0;P_SetMobjState(actor,S_POSS_RUN1);
            end=actor->x;actor->flags|=MF_SKULLFLY;P_FluidTick(actor);
            if(actor->x!=end)I_Error("Fluid chase moved a charging Lost Soul");
        }
        P_RemoveMobj(actor);
    }
    states[S_POSS_RUN1].tics=original;
    {
        mobj_t *actor=P_SpawnMobj(player->x-512*FRACUNIT,player->y,ONFLOORZ,MT_POSSESSED);
        actor->flags|=MF_NOCLIP;actor->target=player;actor->reactiontime=999;
        actor->threshold=100;actor->movecount=100;actor->movedir=0;actor->angle=0;
        P_SetMobjState(actor,S_POSS_RUN1);
        for(int tic=0;tic<24;++tic){
            fixed_t x=actor->x;
            if(R_BeforeTic)R_BeforeTic();
            P_MobjThinker(actor);
            if(actor->x-x!=actor->info->speed*FRACUNIT/4)I_Error("Fluid walking paused or jumped on tic %d",tic);
#ifdef DOOM_RENDER3D
            {extern void I_TestSmoothActor(mobj_t*);I_TestSmoothActor(actor);}
#endif
        }
        if(actor->reactiontime!=992)I_Error("Fluid AI cadence changed across walking states");
        P_RemoveMobj(actor);
        puts("FluidMonsters: continuous per-tic walking across six live AI state transitions passed.");
    }
    p_persistentpursuit=savedPursuit;
    puts("FluidMonsters: 18 monster types, fractional speed, AI cadence and cancellation passed.");
}
static int observe_sound(int s,int v,int sep,int pitch,int priority,int weapon){++routed_sounds;return -1;}
static void test_map_sound_origins(void){
    int (*saved)(int,int,int,int,int,int)=S_WeaponSound;
    mobj_t fixture;
    mobj_t *player=players[0].mo;
    memset(&fixture,0,sizeof(fixture));
    fixture.x=player->x;fixture.y=player->y;fixture.z=player->z;
    /* A positional source is not an actor. Poison bytes past its prefix to
     * make accidental reads of player/target fail deterministically. */
    fixture.player=(player_t *)(size_t)1;
    fixture.target=(mobj_t *)(size_t)1;
    S_WeaponSound=observe_sound;routed_sounds=0;
    S_StartSound(&fixture,sfx_doropn);S_StopSound(&fixture);
    S_StartSound(&player->subsector->sector->soundorg,sfx_pstart);
    S_StopSound(&player->subsector->sector->soundorg);
    if(routed_sounds)I_Error("Map sound incorrectly routed as a weapon");
    S_StartSound(player,sfx_pistol);S_StopSound(player);
    if(routed_sounds!=1)I_Error("Player weapon sound routing lost");
    {
        mobj_t *rocket=P_SpawnMobj(player->x,player->y,player->z,MT_ROCKET);
        rocket->target=player;
        S_StartSound(rocket,sfx_barexp);S_StopSound(rocket);
        if(routed_sounds!=2)I_Error("Projectile weapon sound routing lost");
        P_RemoveMobj(rocket);
    }
    S_WeaponSound=saved;
    puts("Map/door sound origins and player/projectile routing passed.");
}
#ifdef DOOM_RENDER3D
static void key(evtype_t type, int value);

static int global_line=-1,global_side;
static double global_ax,global_ay,global_tx,global_ty,global_u,global_z;
static float global_depth;
static fixed_t global_startx,global_starty,global_startz;
static void global_hit_as(int line,int side,double u,double z,mobjtype_t type){
    line_t *l=&lines[line];vertex_t *a=side?l->v2:l->v1,*b=side?l->v1:l->v2;
    double tx=(double)(b->x-a->x)/FRACUNIT,ty=(double)(b->y-a->y)/FRACUNIT,len=hypot(tx,ty);tx/=len;ty/=len;
    double stand=2-P_GlobalDigDepth(line,side,(float)u,(float)z);
    mobj_t rocket={0};rocket.type=type;rocket.radius=11*FRACUNIT;rocket.height=8*FRACUNIT;
    rocket.x=(fixed_t)((a->x/(double)FRACUNIT+u*tx+ty*stand)*FRACUNIT);
    rocket.y=(fixed_t)((a->y/(double)FRACUNIT+u*ty-tx*stand)*FRACUNIT);
    rocket.z=(fixed_t)((z-4)*FRACUNIT);rocket.momx=(fixed_t)(-ty*20*FRACUNIT);rocket.momy=(fixed_t)(tx*20*FRACUNIT);
    if(!P_GlobalDigImpact(&rocket))I_Error("Global digging missed wall %d side %d",line,side);
}
static void global_hit(int line,int side,double u,double z){global_hit_as(line,side,u,z,MT_ROCKET);}
static void test_global_digging(void){
    static int last=-1,start=-1,phase=0;
    if(gamestate!=GS_LEVEL||gameaction!=ga_nothing||last==gametic)return;last=gametic;
    if(start<0)start=gametic;int t=gametic-start;player_t *p=&players[0];
    if(t==5){
        int tested=0,orientations=0;sector_t *room=p->mo->subsector->sector;
        for(int i=0;i<numlines;++i)for(int side=0;side<2;++side){
            line_t *l=&lines[i];if(l->sidenum[side]<0)continue;
            sector_t *f=side?l->backsector:l->frontsector,*b=side?l->frontsector:l->backsector;
            double len=hypot(l->dx/(double)FRACUNIT,l->dy/(double)FRACUNIT),floor=f->floorheight/(double)FRACUNIT,ceil=f->ceilingheight/(double)FRACUNIT;
            if(len<64||ceil-floor<32)continue;
            double z=(floor+ceil)*.5;
            if(b){double bf=b->floorheight/(double)FRACUNIT,bc=b->ceilingheight/(double)FRACUNIT;
                if(bf-floor>=16)z=(floor+fmin(bf,ceil))*.5;else if(ceil-bc>=16)z=(ceil+fmax(bc,floor))*.5;else continue;}
            P_GlobalDigReset();global_hit(i,side,len*.5,z);
            if(P_GlobalDigDepth(i,side,(float)(len*.5),(float)z)<=0)I_Error("Global digging damaged wrong wall %d",i);
            ++tested;orientations|=1<<((l->dx>0?1:0)+(l->dy>0?2:0));
            if(global_line<0&&!b&&f==room&&len>=96&&ceil-floor>=72){global_line=i;global_side=side;global_u=len*.5;global_z=floor+32;}
        }
        int cornerTested=0;
        for(int i=0;i<numlines&&!cornerTested;++i){line_t *a=&lines[i];if(a->backsector||!a->frontsector||hypot(a->dx/(double)FRACUNIT,a->dy/(double)FRACUNIT)<64)continue;
            for(int j=0;j<a->frontsector->linecount&&!cornerTested;++j){line_t *b=a->frontsector->lines[j];if(b==a||b->backsector||b->frontsector!=a->frontsector)continue;
                if(b->v2->x!=a->v1->x||b->v2->y!=a->v1->y)continue;
                if(fabs((double)a->dx*b->dy-(double)a->dy*b->dx)<1)continue;
                float z=(a->frontsector->floorheight/(float)FRACUNIT+a->frontsector->ceilingheight/(float)FRACUNIT)*.5f,bounds[4];
                if(a->frontsector->ceilingheight-a->frontsector->floorheight<64*FRACUNIT)continue;
                P_GlobalDigReset();global_hit(i,0,4,z);
                if(!P_GlobalDigBounds(i,0,bounds)||!P_GlobalDigBounds((int)(b-lines),0,bounds))I_Error("Blast failed to wrap connected wall corner");
                cornerTested=1;
            }
        }
        if(!cornerTested)I_Error("No corner available for wrap test");
        puts("Excavation: blast continued across a connected wall corner.");
        printf("Global excavation: %d ordinary map wall faces checked, direction mask %x\n",tested,orientations);
        if(tested<4)I_Error("Not enough ordinary walls tested");
        if(global_line<0){for(int i=0;i<numlines;++i)if(!lines[i].backsector&&hypot(lines[i].dx/(double)FRACUNIT,lines[i].dy/(double)FRACUNIT)>=128&&lines[i].frontsector->ceilingheight-lines[i].frontsector->floorheight>=80*FRACUNIT){global_line=i;global_side=0;global_u=hypot(lines[i].dx/(double)FRACUNIT,lines[i].dy/(double)FRACUNIT)*.5;global_z=lines[i].frontsector->floorheight/(double)FRACUNIT+32;break;}}
        if(global_line<0)I_Error("No test wall found");
        {
            int savedDamage=p_wall_damage;p_wall_damage=100;P_GlobalDigReset();
            global_hit(global_line,global_side,global_u,global_z);
            float normal=P_GlobalDigDepth(global_line,global_side,(float)global_u,(float)global_z);
            p_wall_damage=500;
            if(fabsf(P_GlobalDigDepth(global_line,global_side,(float)global_u,(float)global_z)-normal)>.001f)I_Error("Damage setting retroactively changed crater");
            P_GlobalDigReset();global_hit(global_line,global_side,global_u,global_z);
            float boosted=P_GlobalDigDepth(global_line,global_side,(float)global_u,(float)global_z);
            if(fabsf(boosted-normal*5)>.01f)I_Error("500 percent wall damage is not five times base: %f vs %f",boosted,normal);
            p_wall_damage=100;P_GlobalDigReset();
            for(int n=0;n<5;++n)global_hit(global_line,global_side,global_u,global_z);
            if(fabsf(P_GlobalDigDepth(global_line,global_side,(float)global_u,(float)global_z)-boosted)>.01f)I_Error("One boosted hit differs from five normal hits");
            p_wall_damage=savedDamage;P_GlobalDigReset();
            puts("Wall damage: one 500% hit equals five 100% hits; existing craters unchanged by setting.");
        }

        line_t *l=&lines[global_line];vertex_t *a=global_side?l->v2:l->v1,*b=global_side?l->v1:l->v2;
        global_ax=a->x/(double)FRACUNIT;global_ay=a->y/(double)FRACUNIT;
        double len=hypot((b->x-a->x)/(double)FRACUNIT,(b->y-a->y)/(double)FRACUNIT);
        global_tx=(b->x-a->x)/(double)FRACUNIT/len;global_ty=(b->y-a->y)/(double)FRACUNIT/len;
        P_GlobalDigReset();
        P_UnsetThingPosition(p->mo);p->mo->x=(fixed_t)((global_ax+global_u*global_tx+global_ty*72)*FRACUNIT);p->mo->y=(fixed_t)((global_ay+global_u*global_ty-global_tx*72)*FRACUNIT);
        p->mo->z=(fixed_t)((global_z-32)*FRACUNIT);P_SetThingPosition(p->mo);
        p->mo->angle=(angle_t)(atan2(global_tx,-global_ty)*(4294967296.0/(2*3.141592653589793)));
        p->mo->floorz=p->mo->subsector->sector->floorheight;p->mo->ceilingz=p->mo->subsector->sector->ceilingheight;
        global_startx=p->mo->x;global_starty=p->mo->y;global_startz=p->mo->z;
        p->cheats|=CF_GODMODE;
        I_Render3DScreenshotTo("global-before.png");
    }
    if(t==12)P_SpawnPlayerMissile(p->mo,MT_ROCKET);
    if(t==40)I_Render3DScreenshotTo("global-one-rocket.png");
    if(t==45){
        float b[4];if(!P_GlobalDigBounds(global_line,global_side,b))I_Error("Real rocket did not deform wall");

        for(int shot=0;shot<6;++shot){
            unsigned before=P_GlobalDigRevision();
            mobj_t *missile=P_SpawnMobj(p->mo->x,p->mo->y,(fixed_t)((global_z-4)*FRACUNIT),MT_ROCKET);
            missile->target=p->mo;missile->momx=(fixed_t)(-global_ty*20*FRACUNIT);missile->momy=(fixed_t)(global_tx*20*FRACUNIT);
            for(int step=0;step<20&&(missile->flags&MF_MISSILE);++step)P_XYMovement(missile);
            P_RemoveMobj(missile);
            if(P_GlobalDigRevision()==before)I_Error("Repeated actual rocket failed to deepen cavity: %d",shot);
        }
        // Rocket knockback is unrelated to the geometry clearance check.
        P_UnsetThingPosition(p->mo);p->mo->x=global_startx;p->mo->y=global_starty;p->mo->z=global_startz;
        p->mo->momx=p->mo->momy=p->mo->momz=0;P_SetThingPosition(p->mo);
        p->mo->floorz=p->mo->subsector->sector->floorheight;p->mo->ceilingz=p->mo->subsector->sector->ceilingheight;
        // Excavate a body-sized recess with many individually small hits.
        for(int n=0;n<18;++n)for(int u=-24;u<=24;u+=16)for(int z=-24;z<=24;z+=16)global_hit(global_line,global_side,global_u+u,global_z+z);
        mobj_t probe=*p->mo;probe.radius=16*FRACUNIT;probe.height=56*FRACUNIT;probe.z=(fixed_t)((global_z-32)*FRACUNIT);
        fixed_t x=(fixed_t)((global_ax+global_u*global_tx-global_ty*8)*FRACUNIT),y=(fixed_t)((global_ay+global_u*global_ty+global_tx*8)*FRACUNIT);
        if(!P_GlobalDigFit(global_line,global_side,&probe,x,y))I_Error("Excavated recess does not fit player");
        fixed_t deepx=(fixed_t)((global_ax+global_u*global_tx-global_ty*72)*FRACUNIT),deepy=(fixed_t)((global_ay+global_u*global_ty+global_tx*72)*FRACUNIT);int owner;
        if(P_GlobalDigPosition(&probe,deepx,deepy,&owner))I_Error("Excavated back wall does not block");
        global_depth=P_GlobalDigDepth(global_line,global_side,(float)global_u,(float)global_z);
        {extern int p_breakablewalls;float bounds[4];
         p_breakablewalls=0;
         if(P_GlobalDigEnabled()||P_GlobalDigBounds(global_line,global_side,bounds))I_Error("Disabled excavation still visible");
         p_breakablewalls=1;
         if(!P_GlobalDigBounds(global_line,global_side,bounds)||fabsf(global_depth-P_GlobalDigDepth(global_line,global_side,(float)global_u,(float)global_z))>.001f)I_Error("Toggle lost damage");}
        G_SaveGame(5,"Global excavation test");phase=1;
    }
    if(t==60&&phase==1){I_Render3DScreenshotTo("global-damaged.png");P_GlobalDigReset();G_LoadGame("doomsav5.dsg");phase=2;}
    if(t==80&&phase==2){if(fabsf(global_depth-P_GlobalDigDepth(global_line,global_side,(float)global_u,(float)global_z))>.001f)I_Error("Global excavation save/load mismatch");I_Render3DScreenshotTo("global-restored.png");puts("Global excavation: wall directions, actual rocket, body clearance, solid back wall and save/load passed.");phase=3;}
    if(t>85&&phase==3){
        for(int d=-64;d<=8;d+=4){fixed_t x=(fixed_t)((global_ax+global_u*global_tx-global_ty*d)*FRACUNIT),y=(fixed_t)((global_ay+global_u*global_ty+global_tx*d)*FRACUNIT);
            if(!P_TryMove(p->mo,x,y))I_Error("Actual movement into recess blocked at depth %d",d);
        }
        puts("Global excavation: player walked across original wall plane into saved cavity.");I_Quit();
    }
    if(t>200)I_Error("Global excavation test timeout");
}
static void test_breaches(void){
    static int phase=0,line=-1,exitRoom=-1,captureTic=0,doorSector=-1;static float u,z,depth;static double ax,ay,tx,ty;
    if(gamestate!=GS_LEVEL||gameaction!=ga_nothing||gametic<12)return;
    mobj_t *player=players[0].mo;
    if(!phase){
        p_throughwalls=1;
        int doorTest=getenv("DOOM_TEST_DOOR_BREACHES")!=NULL,thickTest=getenv("DOOM_TEST_THICK_BREACHES")!=NULL;
        p_breachdoors=doorTest;p_deeperdig=getenv("DOOM_TEST_DEEPER_DIG")!=NULL;
        for(int i=0;i<numlines&&line<0;++i){line_t *l=&lines[i];if(!l->frontsector)continue;
            if(doorTest){if(!l->backsector||l->backsector->ceilingheight!=l->backsector->floorheight)continue;}
            else if(l->backsector)continue;
            double len=hypot(l->dx/(double)FRACUNIT,l->dy/(double)FRACUNIT);
            if(len<96||l->frontsector->ceilingheight-l->frontsector->floorheight<(doorTest?64:80)*FRACUNIT)continue;
            P_GlobalDigReset();float center=l->frontsector->floorheight/(float)FRACUNIT+36;
            global_hit(i,0,len*.5,center);
            float d=P_GlobalDigBreachLimit(i,0,(float)(len*.5),center);
            if(d<4||d>P_GlobalDigMaxDepth()||(thickTest&&d<(atoi(getenv("DOOM_TEST_THICK_BREACHES"))>1?atoi(getenv("DOOM_TEST_THICK_BREACHES")):128)))continue;
            int room=P_GlobalDigBreachSector(i,0,0,-l->dy/(float)FRACUNIT,l->dx/(float)FRACUNIT);
            if(room<0||abs(sectors[room].floorheight-l->frontsector->floorheight)>24*FRACUNIT||sectors[room].ceilingheight<l->frontsector->floorheight+(doorTest?64:80)*FRACUNIT)continue;
            line=i;u=(float)(len*.5);z=center;depth=d;exitRoom=room;doorSector=doorTest?(int)(l->backsector-sectors):-1;
            ax=l->v1->x/(double)FRACUNIT;ay=l->v1->y/(double)FRACUNIT;tx=l->dx/(double)FRACUNIT/len;ty=l->dy/(double)FRACUNIT/len;
        }
        if(line<0)I_Error("No finite wall slab available for breach test");
        printf("Breach fixture: line %d, thickness %.2f, exit sector %d\n",line,depth,exitRoom);
        if(doorSector>=0){
            lines[line].special=26; // Blue-key manual door: the breach must bypass the lock only when enabled.
            for(int k=0;k<NUMCARDS;++k)players[0].cards[k]=false;
            P_UseSpecialLine(player,&lines[line],0);
            if(sectors[doorSector].specialdata)I_Error("Locked door unexpectedly opened without key");
            puts("Locked-door fixture: normal use rejected without key.");
        }
        if(P_GlobalDigBreachMargin(line,0,u,z)>0)I_Error("One small hit breached wall prematurely");
        for(int n=0;n<(p_deeperdig?640:thickTest?64:24);++n){
            for(int v=-32;v<=32;v+=16)for(int h=-32;h<=32;h+=16)global_hit(line,0,u+v,z+h);
            if(thickTest&&depth>=256&&n==12){
                float current=P_GlobalDigDepth(line,0,u,z);
                if(current<=80||current>=depth)I_Error("Deep partial-crater fixture out of range: %f",current);
                mobj_t *shot=P_SpawnMobj((fixed_t)((ax+u*tx-ty*40)*FRACUNIT),(fixed_t)((ay+u*ty+tx*40)*FRACUNIT),(fixed_t)((z-4)*FRACUNIT),MT_ROCKET);
                shot->target=player;shot->momx=(fixed_t)(-ty*20*FRACUNIT);shot->momy=(fixed_t)(tx*20*FRACUNIT);
                unsigned before=P_GlobalDigRevision();
                for(int step=0;step<20&&(shot->flags&MF_MISSILE);++step)P_XYMovement(shot);
                if((shot->flags&MF_MISSILE)||P_GlobalDigRevision()==before)I_Error("Deep partial wall failed to stop and receive rocket impact");
                P_RemoveMobj(shot);printf("Deep partial wall at %.1f units stopped and received real rocket impact.\n",current);
            }
        }
        float x=(float)(ax+u*tx),y=(float)(ay+u*ty);
        if(!P_GlobalDigBreachRay(line,x,y,z,(float)-ty,(float)tx,0))I_Error("Fully excavated wall does not pass shot");
        if(P_GlobalDigBreachRay(line,x+tx*70,y+ty*70,z,(float)-ty,(float)tx,0))I_Error("Shot escaped outside aperture");
        if(p_deeperdig&&depth>256){
            p_deeperdig=0;
            if(P_GlobalDigBreachRay(line,x,y,z,(float)-ty,(float)tx,0))I_Error("Deeper toggle off still opens thick wall");
            p_deeperdig=1;
            if(!P_GlobalDigBreachRay(line,x,y,z,(float)-ty,(float)tx,0))I_Error("Deeper toggle lost stored damage");
            puts("Deeper digging toggle restores standard limits and preserves stored damage.");
        }
        p_throughwalls=0;p_breachdoors=0;
        if(P_GlobalDigBreachRay(line,x,y,z,(float)-ty,(float)tx,0))I_Error("Breach toggle off still passes shot");
        p_throughwalls=doorTest?0:1;p_breachdoors=doorTest;
        P_UnsetThingPosition(player);player->x=(fixed_t)((x+ty*40)*FRACUNIT);player->y=(fixed_t)((y-tx*40)*FRACUNIT);player->z=(fixed_t)((z-36)*FRACUNIT);P_SetThingPosition(player);
        player->floorz=player->subsector->sector->floorheight;player->ceilingz=player->subsector->sector->ceilingheight;players[0].viewz=player->z+41*FRACUNIT;
        player->angle=(angle_t)(atan2(tx,-ty)*(4294967296.0/(2*3.141592653589793)));players[0].cheats|=CF_GODMODE;
        mobj_t *target=P_SpawnMobj((fixed_t)((x-ty*(depth+36))*FRACUNIT),(fixed_t)((y+tx*(depth+36))*FRACUNIT),player->z,MT_TROOP);
        target->health=1000;target->flags&=~MF_COUNTKILL;
        if(!P_CheckSight(player,target))I_Error("Sight failed through breach");
        P_LineAttack(player,player->angle,(fixed_t)((depth+128)*FRACUNIT),0,10);
        if(target->health>=1000)I_Error("Hitscan failed through breach");
        P_RemoveMobj(target);
        mobj_t *rocket=P_SpawnMobj(player->x,player->y,player->z+32*FRACUNIT,MT_ROCKET);rocket->target=player;rocket->momx=(fixed_t)(-ty*10*FRACUNIT);rocket->momy=(fixed_t)(tx*10*FRACUNIT);
        for(int n=0;n<(int)((depth+65)/10);++n)P_XYMovement(rocket);
        if(!(rocket->flags&MF_MISSILE))I_Error("Rocket blocked crossing breach");
        P_RemoveMobj(rocket);
        if(doorSector>=0){
            fixed_t old=sectors[doorSector].ceilingheight;
            float margin=P_GlobalDigDepth(line,0,u+35,z);
            sectors[doorSector].ceilingheight+=16*FRACUNIT;
            if(fabsf(P_GlobalDigDepth(line,0,u+35,z+16)-margin)>.01f)I_Error("Breach does not follow moving door");
            if(!P_GlobalDigBreachRay(line,x,y,z+16,(float)-ty,(float)tx,0))I_Error("Raised door hole blocks shot");
            sectors[doorSector].ceilingheight=old;
            puts("Door breach follows ceiling movement and returns when closed.");
        }
        I_Render3DScreenshotTo("breach-front.png");G_SaveGame(5,"Breach passage test");phase=1;return;
    }
    if(phase==1){P_GlobalDigReset();G_LoadGame("doomsav5.dsg");phase=2;return;}
    if(phase==2){
        if(!P_GlobalDigBreachRay(line,(float)(ax+u*tx),(float)(ay+u*ty),z,(float)-ty,(float)tx,0))I_Error("Breach save/load failed");
        for(int d=-36;d<depth+24;d+=2){fixed_t x=(fixed_t)((ax+u*tx-ty*d)*FRACUNIT),y=(fixed_t)((ay+u*ty+tx*d)*FRACUNIT);if(!P_TryMove(player,x,y))I_Error("Player blocked in breach at %d",d);}
        if(doorSector>=0){
            fixed_t old=sectors[doorSector].ceilingheight;
            if(!P_TryMove(player,(fixed_t)((ax+u*tx-ty*depth*.5)*FRACUNIT),(fixed_t)((ay+u*ty+tx*depth*.5)*FRACUNIT)))I_Error("Cannot stand inside breached door");
            sectors[doorSector].ceilingheight+=8*FRACUNIT;P_ChangeSector(&sectors[doorSector],false);
            if(player->ceilingz-player->floorz<player->height)I_Error("Moving breached door crushed clear passage");
            sectors[doorSector].ceilingheight=old;P_ChangeSector(&sectors[doorSector],false);
            if(player->ceilingz-player->floorz<player->height)I_Error("Closed breached door loses clearance");
            if(!P_TryMove(player,(fixed_t)((ax+u*tx-ty*(depth+20))*FRACUNIT),(fixed_t)((ay+u*ty+tx*(depth+20))*FRACUNIT)))I_Error("Cannot leave breached door");
        }
        player->angle+=ANG180;captureTic=gametic+4;phase=3;return;
    }
    if(phase==3){player->angle=R_PointToAngle2(player->x,player->y,(fixed_t)((ax+u*tx)*FRACUNIT),(fixed_t)((ay+u*ty)*FRACUNIT));if(gametic<captureTic)return;I_Render3DScreenshotTo("breach-back.png");captureTic=gametic+4;phase=4;return;}
    if(phase==4){
        if(gametic<captureTic)return;
        for(int d=(int)depth+20;d>=-36;d-=2){fixed_t x=(fixed_t)((ax+u*tx-ty*d)*FRACUNIT),y=(fixed_t)((ay+u*ty+tx*d)*FRACUNIT);if(!P_TryMove(player,x,y))I_Error("Reverse passage blocked at %d",d);}
        puts("Breach: partial hits, aperture, toggle, sight, hitscan, rocket, save/load and two-way player passage passed.");I_Quit();
    }
}
static void test_digging(void){
    static int last=-1,phase=0,begun=-1,finish=0;
    static long savedShape;
    if(gamestate!=GS_LEVEL||gameaction!=ga_nothing)return;
    if(last==gametic)return;last=gametic;
    if(begun<0)begun=gametic;
    int t=gametic-begun;player_t *p=&players[0];
    if(t==10){
        if(P_TryMove(p->mo,336*FRACUNIT,320*FRACUNIT))I_Error("Digging: intact wall was traversable");
        I_Render3DScreenshotTo("dig-before.png");
    }
    // Small excavation requires aiming around the rim, rather than
    // assuming a centerline shot always makes a player-sized opening.
    if(t>=30&&phase==0&&t%12==6){
        extern float tourpitch;extern boolean freeaim;
        float oldPitch=tourpitch;boolean oldFree=freeaim;angle_t oldAngle=p->mo->angle;
        int n=(t-30)/12;int yaw[5]={0,-1,1,0,0};float pitch[5]={0,0,0,.24f,-.24f};
        p->mo->angle=(angle_t)(yaw[n%5]*(ANG45/4));tourpitch=pitch[n%5];freeaim=true;
        P_SpawnPlayerMissile(p->mo,MT_ROCKET);--p->ammo[am_misl];
        p->mo->angle=oldAngle;tourpitch=oldPitch;freeaim=oldFree;
    }
    if(t==50)I_Render3DScreenshotTo("dig-first-impact.png");
    if(t>30&&phase==0){
        P_TryMove(p->mo,p->mo->x+4*FRACUNIT,320*FRACUNIT);
        if(p->mo->x>=816*FRACUNIT){
            key(ev_keyup,KEY_RCTRL);p->cmd.buttons=0;p->ammo[am_misl]=0;
            for(thinker_t *th=thinkercap.next,*next;th!=&thinkercap;th=next){
                next=th->next;
                if(th->function.acp1==(actionf_p1)P_MobjThinker){mobj_t *m=(mobj_t*)th;if(m->type==MT_ROCKET&&(m->flags&MF_MISSILE))P_RemoveMobj(m);}
            }
            finish=t;phase=1;
            I_Render3DScreenshotTo("dig-through.png");
            savedShape=0;
            for(int i=0;i<numsectors;++i)if(sectors[i].tag==19001)savedShape+=(sectors[i].ceilingheight-sectors[i].floorheight)/FRACUNIT;
            if(savedShape<2000)I_Error("Digging: no meaningful excavation");
            printf("Digging: walked through 384 units of concrete, excavated height sum %ld\n",savedShape);
            G_SaveGame(5,"Digging prototype test");
        }
    }
    if(phase==1&&t>finish+15){
        for(int i=0;i<numsectors;++i)if(sectors[i].tag==19001)sectors[i].floorheight=sectors[i].ceilingheight=64*FRACUNIT;
        G_LoadGame("doomsav5.dsg");phase=2;
    }else if(phase==2&&t>finish+30){
        long shape=0;
        for(int i=0;i<numsectors;++i)if(sectors[i].tag==19001)shape+=(sectors[i].ceilingheight-sectors[i].floorheight)/FRACUNIT;
        if(shape!=savedShape||p->mo->x<816*FRACUNIT)I_Error("Digging: geometry/save position not restored: %ld vs %ld, x=%d",shape,savedShape,p->mo->x/FRACUNIT);
        // Walk back through the saved tunnel; solid material must stay solid.
        for(int x=816;x>=160;x-=4)if(!P_TryMove(p->mo,x*FRACUNIT,320*FRACUNIT))I_Error("Digging: saved tunnel blocked at %d",x);
        if(P_TryMove(p->mo,512*FRACUNIT,128*FRACUNIT))I_Error("Digging: untouched concrete became traversable");
        I_Render3DScreenshotTo("dig-restored.png");phase=3;finish=t;
        puts("Digging: real rocket impacts, tunnel traversal, intact-material collision and save/load passed.");
    }else if(phase==3&&t>finish+5)I_Quit();
    if(t>1800)I_Error("Digging: could not reach destination, x=%d",p->mo->x/FRACUNIT);
}
static void test_surfaces(void){
    static int phase;static mobj_t *player;static float x,y;static int sector;static float savedFloor,savedCeil;
    if(gamestate!=GS_LEVEL||gametic<5)return;
    if(!phase){
        player=players[0].mo;players[0].cheats|=CF_GODMODE;x=player->x/(float)FRACUNIT;y=player->y/(float)FRACUNIT;sector=(int)(player->subsector->sector-sectors);
        // Actual missile floor/ceiling collision: fireball chips stay much weaker
        // and must not leak their strength into subsequent rocket impacts.
        extern void P_ZMovement(mobj_t*);
        const mobjtype_t chipTypes[]={MT_FATSHOT,MT_TROOPSHOT,MT_HEADSHOT,MT_BRUISERSHOT,MT_ARACHPLAZ,MT_TRACER};
        p_monster_surface_damage=1;
        for(int type=0;type<6;++type)for(int c=0;c<2;++c){
            float depths[2];
            for(int kind=0;kind<2;++kind){
                P_GlobalDigReset();
                mobj_t *shot=P_SpawnMobj(player->x,player->y,c?player->ceilingz-16*FRACUNIT:player->floorz+8*FRACUNIT,kind?MT_ROCKET:chipTypes[type]);
                shot->target=player;shot->momz=(c?20:-20)*FRACUNIT;P_ZMovement(shot);
                depths[kind]=P_GlobalDigFlatDepth(sector,c,x,y);P_RemoveMobj(shot);
            }
            if(depths[0]<=0||fabsf(depths[0]*8-depths[1])>.01f)I_Error("Mancubus crater strength mismatch %f / %f",depths[0],depths[1]);
        }
        int chipWall=-1;float chipU=0,chipZ=0;
        for(int i=0;i<numlines;++i)if(!lines[i].backsector&&lines[i].frontsector&&lines[i].sidenum[0]>=0){
            float len=hypotf(lines[i].dx/(float)FRACUNIT,lines[i].dy/(float)FRACUNIT);
            sector_t *room=lines[i].frontsector;
            if(len<96||room->ceilingheight-room->floorheight<96*FRACUNIT)continue;
            chipWall=i;chipU=len*.5f;chipZ=(room->floorheight+room->ceilingheight)/(2.f*FRACUNIT);break;
        }
        if(chipWall<0)I_Error("No wall for monster chip test");
        for(int type=0;type<6;++type){
            P_GlobalDigReset();global_hit_as(chipWall,0,chipU,chipZ,chipTypes[type]);
            float chip=P_GlobalDigDepth(chipWall,0,chipU,chipZ);
            P_GlobalDigReset();global_hit(chipWall,0,chipU,chipZ);
            float rocket=P_GlobalDigDepth(chipWall,0,chipU,chipZ);
            if(chip<=0||fabsf(chip*8-rocket)>.01f)I_Error("Monster wall chip ratio invalid %f / %f",chip,rocket);
        }
        // Exercise real player hitscan dispatch, including repeated hits and toggle.
        P_GlobalDigReset();p_player_surface_damage=1;
        line_t *wall=&lines[chipWall];float dx=wall->dx/(float)FRACUNIT,dy=wall->dy/(float)FRACUNIT,len=hypotf(dx,dy);dx/=len;dy/=len;
        fixed_t hx=(fixed_t)(wall->v1->x+chipU*dx*FRACUNIT),hy=(fixed_t)(wall->v1->y+chipU*dy*FRACUNIT);
        player_t chipPlayer=players[0];mobj_t shooter=*player;shooter.player=&chipPlayer;chipPlayer.mo=&shooter;chipPlayer.readyweapon=wp_pistol;
        shooter.x=hx+(fixed_t)(dy*16*FRACUNIT);shooter.y=hy-(fixed_t)(dx*16*FRACUNIT);
        shooter.z=(fixed_t)(chipZ*FRACUNIT)-shooter.height/2-8*FRACUNIT;chipPlayer.viewz=(fixed_t)(chipZ*FRACUNIT);
        shooter.subsector=R_PointInSubsector(shooter.x,shooter.y);
        angle_t toward=R_PointToAngle2(shooter.x,shooter.y,hx,hy);
        P_LineAttack(&shooter,toward,128*FRACUNIT,0,5);
        float firstChip=P_GlobalDigDepth(chipWall,0,chipU,chipZ);
        if(firstChip<=0||firstChip>.1f)I_Error("Player bullet failed or too strong: %f",firstChip);
        if(P_GlobalDigDepth(chipWall,0,chipU+12,chipZ)!=0)I_Error("Bullet chip footprint too wide");
        P_LineAttack(&shooter,toward,128*FRACUNIT,0,5);
        if(P_GlobalDigDepth(chipWall,0,chipU,chipZ)<=firstChip)I_Error("Repeated bullets did not accumulate");
        p_player_surface_damage=0;unsigned unchanged=P_GlobalDigRevision();P_LineAttack(&shooter,toward,128*FRACUNIT,0,5);
        if(unchanged!=P_GlobalDigRevision())I_Error("Disabled player surface damage carved wall");
        p_player_surface_damage=1;chipPlayer.readyweapon=wp_fist;P_LineAttack(&shooter,toward,128*FRACUNIT,0,5);
        if(unchanged!=P_GlobalDigRevision())I_Error("Melee carved wall");
        for(int kind=0;kind<2;++kind)for(int c=0;c<2;++c){
            P_GlobalDigReset();mobj_t *shot=P_SpawnMobj(player->x,player->y,c?player->ceilingz-16*FRACUNIT:player->floorz+8*FRACUNIT,kind?MT_BFG:MT_PLASMA);
            shot->target=player;shot->momz=(c?20:-20)*FRACUNIT;P_ZMovement(shot);
            float d=P_GlobalDigFlatDepth(sector,c,x,y);
            if(d<=0||(kind?d<4:d>1))I_Error("Player projectile chip invalid %f",d);
            if(!kind){
                if(P_GlobalDigFlatDepth(sector,c,x+16,y)!=0)I_Error("Plasma chip footprint too wide");
                int bytes=P_GlobalDigSaveSize();unsigned char *saved=malloc(bytes);P_GlobalDigSave(saved);P_GlobalDigReset();P_GlobalDigLoad(saved,bytes);free(saved);
                if(fabsf(P_GlobalDigFlatDepth(sector,c,x,y)-d)>.001f||P_GlobalDigFlatDepth(sector,c,x+16,y)!=0)I_Error("Chip profile save/load mismatch");
            }
            P_RemoveMobj(shot);
        }
        puts("Player hitscan accumulation, toggle, melee exclusion and plasma/BFG surface impacts passed.");
        P_GlobalDigReset();p_monster_surface_damage=0;
        unsigned chipRevision=P_GlobalDigRevision();
        for(int type=0;type<6;++type){mobj_t shot=*player;shot.type=chipTypes[type];P_GlobalDigFlatImpact(&shot,0);P_GlobalDigImpact(&shot);}
        if(P_GlobalDigRevision()!=chipRevision)I_Error("Disabled monster damage changed surfaces");
        p_monster_surface_damage=1;
        puts("All six monster projectile types: one eighth rocket strength and disabled toggle passed.");
        int savedDeep=p_deeperdig;p_deeperdig=1;
        for(int c=0;c<2;++c)for(int n=0;n<40;++n)for(int u=-16;u<=16;u+=16)for(int v=-16;v<=16;v+=16){
            mobj_t shot=*player;shot.type=MT_ROCKET;shot.x+=u*FRACUNIT;shot.y+=v*FRACUNIT;
            P_GlobalDigFlatImpact(&shot,c);P_GlobalDigImpact(&shot);
        }
        fixed_t cavityFloor=sectors[sector].floorheight,cavityCeil=sectors[sector].ceilingheight;
        P_GlobalDigFlatHeights(player,player->x,player->y,sector,&cavityFloor,&cavityCeil);
        if(cavityFloor>sectors[sector].floorheight-56*FRACUNIT||cavityCeil<sectors[sector].ceilingheight+56*FRACUNIT)I_Error("Deep surfaces did not create usable player space");
        fixed_t originalZ=player->z;player->z=cavityFloor;
        if(!P_CheckPosition(player,player->x,player->y))I_Error("Deep floor cavity blocked player");
        player->z=originalZ;p_deeperdig=savedDeep;P_GlobalDigReset();
        puts("Deep floor/ceiling cavities provide player-sized collision space.");
        // Exercise the actual barrel explosion action, including the master toggle.
        mobj_t *barrel=P_SpawnMobj(player->x,player->y,player->floorz,MT_BARREL);
        barrel->target=player;
        P_GlobalDigReset();
        P_SetMobjState(barrel,S_BEXP4);
        float barrelDepth=P_GlobalDigFlatDepth(sector,0,x,y);
        if(barrelDepth<=0)I_Error("Barrel explosion did not crater nearby floor");
        P_RemoveMobj(barrel);P_GlobalDigReset();
        extern int p_breakablewalls;int savedBreakable=p_breakablewalls;p_breakablewalls=0;
        barrel=P_SpawnMobj(player->x,player->y,player->floorz,MT_BARREL);
        unsigned barrelRevision=P_GlobalDigRevision();P_SetMobjState(barrel,S_BEXP4);
        if(P_GlobalDigRevision()!=barrelRevision)I_Error("Disabled destruction still craters barrel explosion");
        P_RemoveMobj(barrel);p_breakablewalls=savedBreakable;
        puts("Barrel explosion action: floor crater and disabled toggle passed.");
        // Actor-contact explosions must damage nearby floors, not remote ones.
        mobj_t splash=*player;splash.type=MT_ROCKET;splash.height=8*FRACUNIT;splash.z=player->floorz+20*FRACUNIT;
        P_GlobalDigMonsterContact(&splash,player->x,player->y);P_GlobalDigImpact(&splash);
        float nearDepth=P_GlobalDigFlatDepth(sector,0,x,y);
        unsigned floorVersion=P_GlobalDigFlatRevision(sector,0);
        mobj_t ceilingShot=splash;P_GlobalDigFlatImpact(&ceilingShot,1);P_GlobalDigImpact(&ceilingShot);
        if(P_GlobalDigFlatRevision(sector,0)!=floorVersion)I_Error("Ceiling impact invalidated floor cache");
        float farArea[4]={x+1024,x+1056,y+1024,y+1056},patch[4];
        if(P_GlobalDigFlatPatch(sector,0,farArea,patch))I_Error("Crater invalidated distant BSP leaf");
        if(nearDepth<=0||nearDepth>4.61f)I_Error("Monster splash missed nearby floor");
        P_GlobalDigReset();splash.z=player->floorz+80*FRACUNIT;
        P_GlobalDigMonsterContact(&splash,player->x,player->y);P_GlobalDigImpact(&splash);
        if(P_GlobalDigFlatDepth(sector,0,x,y)>0)I_Error("Monster splash reached remote floor");
        P_GlobalDigReset();
        int splashWall=0;
        for(int i=0;i<numlines&&!splashWall;++i){line_t *l=&lines[i];if(l->backsector||!l->frontsector)continue;
            float len=hypotf(l->dx/(float)FRACUNIT,l->dy/(float)FRACUNIT);if(len<80)continue;
            float tx=l->dx/(float)FRACUNIT/len,ty=l->dy/(float)FRACUNIT/len;
            splash.x=(l->v1->x+l->v2->x)/2+(fixed_t)(ty*16*FRACUNIT);splash.y=(l->v1->y+l->v2->y)/2-(fixed_t)(tx*16*FRACUNIT);
            if(R_PointInSubsector(splash.x,splash.y)->sector!=l->frontsector)continue;
            splash.z=l->frontsector->floorheight+28*FRACUNIT;
            P_GlobalDigMonsterContact(&splash,splash.x,splash.y);P_GlobalDigImpact(&splash);
            if(P_GlobalDigDepth(i,0,len*.5f,splash.z/(float)FRACUNIT+4)<=0)I_Error("Monster splash missed nearby wall");
            P_GlobalDigReset();
            barrel=P_SpawnMobj(splash.x,splash.y,splash.z+4*FRACUNIT-mobjinfo[MT_BARREL].height/2,MT_BARREL);
            P_SetMobjState(barrel,S_BEXP4);
            if(P_GlobalDigDepth(i,0,len*.5f,splash.z/(float)FRACUNIT+4)<=0)I_Error("Barrel explosion missed nearby wall");
            P_RemoveMobj(barrel);
            ++splashWall;
        }
        if(!splashWall)I_Error("No monster wall splash tested");P_GlobalDigReset();
        int doors=0;
        for(int i=0;i<numlines&&!doors;++i){line_t *l=&lines[i];if(!l->frontsector||!l->backsector||l->frontsector->ceilingheight-l->backsector->ceilingheight<40*FRACUNIT)continue;
            float len=hypotf(l->dx/(float)FRACUNIT,l->dy/(float)FRACUNIT);if(len<48)continue;
            float h=l->backsector->ceilingheight/(float)FRACUNIT+16;
            global_hit(i,0,len*.5f,h);float depth=P_GlobalDigDepth(i,0,len*.5f,h);if(depth<=0)continue;
            l->backsector->ceilingheight+=7*FRACUNIT;
            if(fabsf(depth-P_GlobalDigDepth(i,0,len*.5f,h+7))>.001f)I_Error("Door crater failed to follow ceiling");
            int size=P_GlobalDigSaveSize();unsigned char *buf=malloc(size);P_GlobalDigSave(buf);P_GlobalDigReset();P_GlobalDigLoad(buf,size);free(buf);
            l->backsector->ceilingheight-=7*FRACUNIT;
            if(fabsf(depth-P_GlobalDigDepth(i,0,len*.5f,h))>.001f)I_Error("Door anchor save/load mismatch");++doors;
        }
        if(!doors)I_Error("No door attachment tested");
        P_GlobalDigReset();puts("Monster splash near/far floor and moving door save/load passed.");
        extern void P_ZMovement(mobj_t*);
        for(int ceiling=0;ceiling<2;++ceiling){
            mobj_t *rocket=P_SpawnMobj(player->x,player->y,ceiling?player->ceilingz-16*FRACUNIT:player->floorz+8*FRACUNIT,MT_ROCKET);
            rocket->target=player;rocket->momz=(ceiling?20:-20)*FRACUNIT;P_ZMovement(rocket);
            float depth=P_GlobalDigFlatDepth(sector,ceiling,x,y);
            if(depth<=0||depth>4.61f)I_Error("First flat impact depth invalid %f",depth);
            P_RemoveMobj(rocket);
        }
        // Widen the crater enough for the player's complete footprint.
        for(int c=0;c<2;++c)for(int n=0;n<8;++n)for(int u=-16;u<=16;u+=16)for(int v=-16;v<=16;v+=16){
            mobj_t rocket=*player;rocket.type=MT_ROCKET;rocket.x+=(u*FRACUNIT);rocket.y+=(v*FRACUNIT);
            P_GlobalDigFlatImpact(&rocket,c);P_GlobalDigImpact(&rocket);
        }
        int originalFlat=sectors[sector].floorpic;sectors[sector].floorpic=R_FlatNumForName("NUKAGE1");
        unsigned beforeLiquid=P_GlobalDigRevision();mobj_t liquidShot=*player;liquidShot.type=MT_ROCKET;
        P_GlobalDigFlatImpact(&liquidShot,0);P_GlobalDigImpact(&liquidShot);
        float liquidArea[4]={x-32,x+32,y-32,y+32},liquidBounds[4];
        if(P_GlobalDigRevision()!=beforeLiquid||P_GlobalDigFlatDepth(sector,0,x,y)!=0||P_GlobalDigFlatPatch(sector,0,liquidArea,liquidBounds))I_Error("Liquid retained concrete excavation");
        sectors[sector].floorpic=originalFlat;
        puts("Liquid surface exclusion and old-crater suppression passed.");
        savedFloor=P_GlobalDigFlatDepth(sector,0,x,y);savedCeil=P_GlobalDigFlatDepth(sector,1,x,y);
        if(savedFloor>16.01f||savedCeil>32.01f)I_Error("Surface depth limit exceeded");
        P_GlobalDigRefreshHeights(player);
        if(player->floorz>=sectors[sector].floorheight||player->ceilingz<=sectors[sector].ceilingheight)I_Error("Flat excavation collision did not change");
        extern int p_breakablewalls;p_breakablewalls=0;P_GlobalDigRefreshHeights(player);
        if(player->floorz!=sectors[sector].floorheight||player->ceilingz!=sectors[sector].ceilingheight)I_Error("Disabled flat collision not restored");
        p_breakablewalls=1;
        int bytes=P_GlobalDigSaveSize();unsigned char *data=malloc(bytes);P_GlobalDigSave(data);P_GlobalDigReset();P_GlobalDigLoad(data,bytes);free(data);
        if(fabsf(savedFloor-P_GlobalDigFlatDepth(sector,0,x,y))>.001f||fabsf(savedCeil-P_GlobalDigFlatDepth(sector,1,x,y))>.001f)I_Error("Flat save/load mismatch");
        int limits=0;for(int i=0;i<numlines;++i)for(int side=0;side<2;++side){if(lines[i].sidenum[side]<0)continue;
            float len=hypotf((lines[i].v2->x-lines[i].v1->x)/(float)FRACUNIT,(lines[i].v2->y-lines[i].v1->y)/(float)FRACUNIT);
            float limit=P_GlobalDigWallLimit(i,side,len*.5f);if(limit<0||limit>64)I_Error("Invalid wall thickness");if(limit<63.9f)++limits;
        }
        if(!limits)I_Error("No thin wall detected on test map");
        printf("Surfaces: real floor/ceiling rockets, collision, toggle, save/load, depth limits; %d thin wall sides detected.\n",limits);
        I_Render3DLook(10000);phase=1;return;
    }
    if(phase==1){I_Render3DScreenshotTo("floor-crater.png");phase=2;return;}
    if(phase==2){I_Render3DLook(-20000);phase=3;return;}
    if(phase==3){I_Render3DScreenshotTo("ceiling-crater.png");phase=4;return;}
    if(phase==4){
        int found=0;
        for(int i=0;i<numlines&&!found;++i){line_t *l=&lines[i];if(l->backsector||!l->frontsector)continue;
            double tx=l->dx/(double)FRACUNIT,ty=l->dy/(double)FRACUNIT,len=hypot(tx,ty);if(len<96)continue;tx/=len;ty/=len;
            sector_t *room=l->frontsector;if(room->ceilingheight-room->floorheight<80*FRACUNIT)continue;
            int neighbor=0;for(int j=0;j<room->linecount;++j){line_t *b=room->lines[j];if(b!=l&&!b->backsector&&b->v2==l->v1&&fabs((double)b->dx*l->dy-(double)b->dy*l->dx)>1)neighbor=1;}if(!neighbor)continue;
            fixed_t px=l->v1->x+(fixed_t)((tx*48+ty*72)*FRACUNIT),py=l->v1->y+(fixed_t)((ty*48-tx*72)*FRACUNIT);
            if(R_PointInSubsector(px,py)->sector!=room||!P_CheckPosition(player,px,py))continue;
            P_GlobalDigReset();P_UnsetThingPosition(player);player->x=px;player->y=py;player->z=room->floorheight;player->momx=player->momy=player->momz=0;P_SetThingPosition(player);player->floorz=room->floorheight;player->ceilingz=room->ceilingheight;
            player->angle=R_PointToAngle2(px,py,l->v1->x+(fixed_t)(tx*4*FRACUNIT),l->v1->y+(fixed_t)(ty*4*FRACUNIT));
            for(int n=0;n<8;++n)global_hit(i,0,4,room->floorheight/(double)FRACUNIT+40);
            players[0].viewheight=41*FRACUNIT;players[0].viewz=player->z+players[0].viewheight;players[0].deltaviewheight=0;
            I_Render3DLook(700);found=1;
        }
        if(!found)I_Error("No visible corner capture location");phase=5;return;
    }
    if(phase==5){I_Render3DScreenshotTo("corner-crater.png");phase=6;return;}
    if(phase==6&&getenv("DOOM_TEST_FLAT_STRESS")){
        int shots=0;uint64_t began=SDL_GetTicksNS();
        // Spread impacts over the map, then measure steady frames separately.
        for(int i=0;i<numlines&&shots<120;++i){line_t *l=&lines[i];if(!l->frontsector)continue;
            float len=hypotf(l->dx/(float)FRACUNIT,l->dy/(float)FRACUNIT);if(len<96)continue;
            fixed_t px=(l->v1->x+l->v2->x)/2+(fixed_t)(l->dy/(float)FRACUNIT/len*40*FRACUNIT);
            fixed_t py=(l->v1->y+l->v2->y)/2-(fixed_t)(l->dx/(float)FRACUNIT/len*40*FRACUNIT);
            subsector_t *sub=R_PointInSubsector(px,py);if(sub->sector!=l->frontsector)continue;
            mobj_t rocket=*player;rocket.type=MT_ROCKET;rocket.x=px;rocket.y=py;rocket.subsector=sub;
            for(int c=0;c<2;++c){P_GlobalDigFlatImpact(&rocket,c);P_GlobalDigImpact(&rocket);}++shots;
        }
        printf("Flat stress: %d floor/ceiling pairs, impact CPU %.3f ms\n",shots,(SDL_GetTicksNS()-began)/1e6);phase=7;return;
    }
    if(phase>=7){if(++phase<265)return;I_Quit();}
    if(phase==6)I_Quit();
}
static void test_settings(void){
    static int phase,frames,was_fullscreen;static SDL_Window *test_window;
    if(gamestate!=GS_LEVEL||gameaction!=ga_nothing)return;
    if(!test_window){int count=0;SDL_Window **windows=SDL_GetWindows(&count);if(count)test_window=windows[0];SDL_free(windows);if(!test_window)return;was_fullscreen=(SDL_GetWindowFlags(test_window)&SDL_WINDOW_FULLSCREEN)!=0;}
    if(phase==0){
        if(++frames==1)I_Render3DSettingsMenu();
        if(frames==3)I_Render3DScreenshot();
        if(frames==8){I_Render3DSettingsKey(SDL_SCANCODE_RIGHT,0);I_Render3DSettingsKey(SDL_SCANCODE_DOWN,0);I_Render3DSettingsKey(SDL_SCANCODE_RETURN,0);phase=1;}
    }else if(phase==1&&gamemap==2){
        I_Render3DSettingsMenu();I_Render3DSettingsKey(SDL_SCANCODE_TAB,0);
        I_Render3DSettingsKey(SDL_SCANCODE_RETURN,0);I_Render3DSettingsKey(SDL_SCANCODE_ESCAPE,0);SDL_SyncWindow(test_window);phase=2;
    }else if(phase==2){
        if(((SDL_GetWindowFlags(test_window)&SDL_WINDOW_FULLSCREEN)!=0)==was_fullscreen)I_Error("Fullscreen setting did not apply");
        I_Render3DSettingsMenu();I_Render3DSettingsKey(SDL_SCANCODE_RETURN,0);I_Render3DSettingsKey(SDL_SCANCODE_ESCAPE,0);SDL_SyncWindow(test_window);phase=3;
    }else if(phase==3){
        if(((SDL_GetWindowFlags(test_window)&SDL_WINDOW_FULLSCREEN)!=0)!=was_fullscreen)I_Error("Fullscreen setting did not restore");
        puts("F4 map selection and fullscreen on/off passed.");I_Quit();
    }
}
static void test_wheel(void){
    static int frame;
    if(gamestate!=GS_LEVEL||gameaction!=ga_nothing)return;
    player_t *p=&players[0];
    if(++frame==1){
        for(int slot=0;slot<NUMWEAPONS;++slot)p->weaponowned[slot]=true;
        for(int slot=0;slot<NUMWEAPONS;++slot){p->pendingweapon=wp_nochange;float angle=slot*6.283185307f/9-1.570796327f;I_Render3DWheelKey(1);I_Render3DWheelMove(cosf(angle)*180,sinf(angle)*150);I_Render3DWheelKey(0);if(p->pendingweapon!=(slot==p->readyweapon?wp_nochange:slot))I_Error("Wheel wrong slot %d",slot);}
        p->weaponowned[wp_bfg]=false;p->pendingweapon=wp_nochange;
        I_Render3DWheelKey(1);I_Render3DWheelMove(cosf(6*6.283185307f/9-1.570796327f)*180,sinf(6*6.283185307f/9-1.570796327f)*150);I_Render3DWheelKey(0);
        if(p->pendingweapon!=wp_nochange)I_Error("Wheel granted unowned weapon");
        I_Render3DWheelKey(1);I_Render3DWheelKey(-1);if(I_Render3DWheelOpen()||p->pendingweapon!=wp_nochange)I_Error("Wheel cancel failed");
        p->readyweapon=wp_chainsaw;p->cmd.buttons=BT_CHANGE|(wp_fist<<BT_WEAPONSHIFT);P_PlayerThink(p);p->cmd.buttons=0;
        if(p->pendingweapon!=wp_fist)I_Error("Chainsaw prevented fist selection");
        p->pendingweapon=wp_nochange;I_Render3DWheelKey(1);I_Render3DWheelMove(90,-90);
    }
    if(frame==3)I_Render3DScreenshot();
    if(frame==8){I_Render3DWheelKey(-1);puts("Weapon wheel: nine slots, unowned rejection, cancel and chainsaw/fist toggle passed.");I_Quit();}
}
#endif
static void test_sigil_route(void){
    static int route,finale_frames;
    if(gameaction!=ga_nothing)return;
    if(route==0&&gamestate==GS_LEVEL){
        const mapinfo_t *info=G_MapInfo(5,1);
        if(gameepisode!=5||gamemap!=1||!info||strcmp(info->episodepic,"M_EPI5"))I_Error("SIGIL episode metadata missing");
        G_DeferedInitNew(sk_medium,5,6);route=1;
    }else if(route==1&&gamestate==GS_LEVEL&&gamemap==6){G_SecretExitLevel();route=2;}
    else if(route==2&&gamestate==GS_INTERMISSION){
        if(wminfo.next!=8)I_Error("SIGIL secret exit must lead to E5M9");G_WorldDone();route=3;
    }else if(route==3&&gamestate==GS_LEVEL&&gamemap==9){G_ExitLevel();route=4;}
    else if(route==4&&gamestate==GS_INTERMISSION){
        if(wminfo.next!=6)I_Error("SIGIL secret map must return to E5M7");G_WorldDone();route=5;
    }else if(route==5&&gamestate==GS_LEVEL&&gamemap==7){G_DeferedInitNew(sk_medium,5,8);route=6;}
    else if(route==6&&gamestate==GS_LEVEL&&gamemap==8){G_ExitLevel();route=7;}
    else if(route==7&&gamestate==GS_FINALE&&++finale_frames>10){
        const mapinfo_t *info=G_MapInfo(5,8);
        if(!info||strcmp(info->endpic,"CREDIT")||!strstr(info->intertext,"Baphomet"))I_Error("SIGIL ending missing");
        puts("SIGIL episode, intermission, E5M6 -> E5M9 -> E5M7, and ending passed.");I_Quit();
    }
}
static int player_attacks;
static void (*renderer_attack)(player_t *);
static void observe_attack(player_t *player) {
    ++player_attacks;
    if(renderer_attack)renderer_attack(player);
}
static void (*renderer_flash)(mobj_t *,int,int);
static void observe_flash(mobj_t *source,int weapon,int projectile)
{
    if(source->player) ++player_flashes; else ++enemy_flashes;
#ifdef DOOM_RENDER3D
    if(getenv("DOOM_CAPTURE_FLASH_TEST") &&
       ((source->player && player_flashes==1)||(!source->player && enemy_flashes==1))) I_Render3DScreenshot();
#endif
    if(renderer_flash) renderer_flash(source,weapon,projectile);
}
#ifdef DOOM_RENDER3D
static unsigned serial_before_change;
static int projectile_capture;
#endif
static void key(evtype_t type, int value)
{
    event_t event = {type, value, 0, 0};
    D_PostEvent(&event);
}
static int crumbleEvents;
static void countCrumble(float x,float y,float z){++crumbleEvents;}
void I_TestFrame(void)
{
    int test_tic=gametic-test_start;
    if(getenv("DOOM_TEST_BUNDLED_ONLY")){
        if(gamestate==GS_LEVEL&&gametic>10){
            test_fluid_monsters();
            extern void I_TestBundledModSettings(void);I_TestBundledModSettings();
            I_Quit();
        }return;
    }

    if(getenv("DOOM_TEST_PLAYER_STUCK")){
        if(gamestate!=GS_LEVEL||!players[0].mo||gametic<2)return;
        mobj_t *m=players[0].mo;extern int p_breakablewalls;
        printf("PLAYER map=%d pos=%.3f %.3f %.3f floor=%.3f ceil=%.3f sector=%d radius=%.1f height=%.1f dig=%d through=%d deep=%d\n",gamemap,m->x/(double)FRACUNIT,m->y/(double)FRACUNIT,m->z/(double)FRACUNIT,m->floorz/(double)FRACUNIT,m->ceilingz/(double)FRACUNIT,(int)(m->subsector->sector-sectors),m->radius/(double)FRACUNIT,m->height/(double)FRACUNIT,p_breakablewalls,p_throughwalls,p_deeperdig);
        for(int off=0;off<2;++off){
            int saved=p_breakablewalls;if(off)p_breakablewalls=0;
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){
                int owner=-1;fixed_t x=m->x+dx*4*FRACUNIT,y=m->y+dy*4*FRACUNIT;
                int dig=P_GlobalDigPosition(m,x,y,&owner),fit=P_CheckPosition(m,x,y);
                printf("PROBE off=%d dir=%d,%d dig=%d owner=%d fit=%d\n",off,dx,dy,dig,owner,fit);
            }p_breakablewalls=saved;
        }
        if(getenv("DOOM_TEST_LEDGE")){
            if(P_TryMove(m,m->x,m->y+4*FRACUNIT))I_Error("Ledge permits deeper wall penetration");
            fixed_t x=m->x,y=m->y;
            for(int n=1;n<=12;++n)if(!P_TryMove(m,x,y-n*4*FRACUNIT))I_Error("Ledge blocks outward retreat at %d",n);
            if(!P_TryMove(m,m->x+4*FRACUNIT,m->y)||!P_TryMove(m,m->x-8*FRACUNIT,m->y))I_Error("Movement remains stuck after retreat");
            puts("Ledge save: deeper penetration rejected; outward retreat and free movement passed.");I_Quit();return;
        }
        fixed_t sx=m->x,sy=m->y;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){
            if(!P_TryMove(m,sx+dx*4*FRACUNIT,sy+dy*4*FRACUNIT)||!P_TryMove(m,sx,sy))I_Error("Saved player cannot move and return in direction %d,%d",dx,dy);
        }
        puts("Stuck save: all eight directions and return passed with destruction enabled.");
        I_Render3DScreenshotTo("stuck-player.png");I_Quit();return;
    }
    if(getenv("DOOM_TEST_CRUMBLE")){
        if(gamestate!=GS_LEVEL||!players[0].mo||gametic<12)return;
        P_GlobalDigReset();p_throughwalls=1;
        void (*saved)(float,float,float)=R_CraterCrumble;R_CraterCrumble=countCrumble;crumbleEvents=0;
        for(int n=0;n<120;++n)global_hit(2,0,40+(n*17)%56,24+(n*13)%48);
        R_CraterCrumble=saved;
        if(!crumbleEvents)I_Error("No small-remnant crumble events");
        if(P_GlobalDigDepth(2,0,120,112)>0)I_Error("Crumble damaged untouched wall");
        printf("Crumble: %d events; untouched material preserved.\n",crumbleEvents);I_Quit();return;
    }
    if(getenv("DOOM_TEST_SHARED_EDGES")){
        static int capture=0;
        if(gamestate!=GS_LEVEL||!players[0].mo||gametic<12)return;
        if(capture){if(gametic<capture)return;I_Render3DScreenshotTo("shared-edge.png");I_Quit();return;}
        p_throughwalls=1;p_deeperdig=0;P_GlobalDigReset();
        global_hit(1,0,248,48);
        float ca=P_GlobalDigDepth(1,0,256,48),cb=P_GlobalDigDepth(2,0,0,48),cx,cy,dx,dy;
        P_GlobalDigWallPoint(1,0,256,48,ca,&cx,&cy);P_GlobalDigWallPoint(2,0,0,48,cb,&dx,&dy);
        if(ca<=0||fabsf(ca-cb)>.001f||hypotf(cx-dx,cy-dy)>.001f)I_Error("Angled crater join leaves a seam");
        P_GlobalDigReset();global_hit(2,0,120,48);
        float a=P_GlobalDigDepth(2,0,128,48),b=P_GlobalDigDepth(3,0,0,48);
        if(a<=0||fabsf(a-b)>.001f)I_Error("Joined wall depth mismatch: %f %f",a,b);
        float ax,ay,bx,by;P_GlobalDigWallPoint(2,0,128,48,a,&ax,&ay);P_GlobalDigWallPoint(3,0,0,48,b,&bx,&by);
        if(hypotf(ax-bx,ay-by)>.001f)I_Error("Joined wall vertex mismatch");
        for(int n=0;n<40;++n)for(int u=96;u<=160;u+=16)for(int z=0;z<=80;z+=16)global_hit(u<128?2:3,0,u<128?u:u-128,z);
        if(P_GlobalDigBreachMargin(2,0,128,32)<=0||P_GlobalDigBreachMargin(3,0,0,32)<=0)I_Error("Breach retained split wall post");
        if(!P_GlobalDigBreachRay(2,256,128,32,1,0,0)||!P_GlobalDigBreachRay(5,320,128,32,-1,0,0))I_Error("Split wall blocks shot");
        mobj_t *m=players[0].mo;
        P_UnsetThingPosition(m);m->x=232*FRACUNIT;m->y=128*FRACUNIT;m->z=0;P_SetThingPosition(m);m->floorz=0;m->ceilingz=128*FRACUNIT;
        for(int x=232;x<=344;x+=2)if(!P_TryMove(m,x*FRACUNIT,128*FRACUNIT))I_Error("Split wall blocks body at %d",x);
        for(int x=344;x>=232;x-=2)if(!P_TryMove(m,x*FRACUNIT,128*FRACUNIT))I_Error("Split wall blocks reverse body at %d",x);
        m->angle=0;players[0].viewz=41*FRACUNIT;I_Render3DScreenshotTo("shared-edge.png");
        puts("Shared wall edges: angled and coplanar mesh vertices, removed seam, shots and two-way walking passed.");capture=gametic+4;return;
    }
    if(getenv("DOOM_TEST_SAVE5")){
        static int capture;
        if(gamestate!=GS_LEVEL||!players[0].mo||gametic<12)return;
        if(capture){if(gametic<capture)return;if(capture>0){I_Render3DScreenshotTo("save5-passage.png");capture=-1;return;}I_Quit();return;}
        mobj_t *m=players[0].mo;fixed_t sx=m->x,sy=m->y,sz=m->z;
        if(gameepisode!=2||gamemap!=1)I_Error("Wrong save 5 fixture");
        for(int y=(int)(sy/FRACUNIT);y>=-1240;y-=2){
            if(!P_TryMove(m,sx,y*FRACUNIT))I_Error("Save 5 passage blocked at %d",y);
            m->z=m->floorz;
        }
        for(int y=-1240;y<=(int)(sy/FRACUNIT);y+=2){
            if(!P_TryMove(m,sx,y*FRACUNIT))I_Error("Save 5 reverse passage blocked at %d",y);
            m->z=m->floorz;
        }
        if(!P_TryMove(m,sx,sy))I_Error("Save 5 return blocked");
        m->z=sz;players[0].viewz=sz+VIEWHEIGHT;
        puts("Save 5: existing damage permits two-way walking without adding impacts.");
        capture=gametic+4;return;
    }
    if(getenv("DOOM_TEST_DOOR_SAVE")){
        if(gamestate!=GS_LEVEL||!players[0].mo||gametic<2)return;
        mobj_t *m=players[0].mo;double angle=m->angle*(6.283185307179586/4294967296.0),vx=cos(angle),vy=sin(angle),x=m->x/(double)FRACUNIT,y=m->y/(double)FRACUNIT;
        printf("DOORPLAYER episode=%d map=%d pos=%.1f %.1f %.1f angle=%.2f breach=%d\n",gameepisode,gamemap,x,y,m->z/(double)FRACUNIT,angle,p_breachdoors);
        for(int i=0;i<numlines;++i){line_t *l=&lines[i];double ax=l->v1->x/(double)FRACUNIT,ay=l->v1->y/(double)FRACUNIT,dx=l->dx/(double)FRACUNIT,dy=l->dy/(double)FRACUNIT,det=vx*dy-vy*dx;
            if(fabs(det)<.001)continue;
            double t=((ax-x)*dy-(ay-y)*dx)/det,u=((ax-x)*vy-(ay-y)*vx)/det;
            if(t<0||t>512||u<0||u>1)continue;
            printf("DOORLINE id=%d dist=%.1f u=%.1f front=%d back=%d heights=%.1f/%.1f %.1f/%.1f limit=%.1f margin=%.1f\n",i,t,u*hypot(dx,dy),l->frontsector?(int)(l->frontsector-sectors):-1,l->backsector?(int)(l->backsector-sectors):-1,l->frontsector?l->frontsector->floorheight/(double)FRACUNIT:0,l->frontsector?l->frontsector->ceilingheight/(double)FRACUNIT:0,l->backsector?l->backsector->floorheight/(double)FRACUNIT:0,l->backsector?l->backsector->ceilingheight/(double)FRACUNIT:0,P_GlobalDigBreachLimit(i,0,u*hypot(dx,dy),m->z/(float)FRACUNIT+32),P_GlobalDigBreachMargin(i,0,u*hypot(dx,dy),m->z/(float)FRACUNIT+32));
        }
        if(getenv("DOOM_TEST_DOOR_REPAIR")){
            extern fixed_t tmdropoffz;
            // Isolate geometry from the live monster waiting beyond the saved door.
            for(thinker_t *th=thinkercap.next,*next;th!=&thinkercap;th=next){
                next=th->next;if(th->function.acp1!=(actionf_p1)P_MobjThinker)continue;
                mobj_t *q=(mobj_t*)th;if(q!=m&&(q->flags&MF_COUNTKILL))P_RemoveMobj(q);
            }
            if(gameepisode!=3||gamemap!=1)I_Error("Wrong door fixture");
            for(int n=0;n<32;++n)for(int u=32;u<=96;u+=16)for(int h=0;h<=64;h+=16)global_hit(91,0,u,h);
            if(!P_GlobalDigBreachRay(91,448,64,32,0,1,0))I_Error("Floor-backed door still blocks breach ray");
            P_UnsetThingPosition(m);m->x=448*FRACUNIT;m->y=40*FRACUNIT;m->z=0;P_SetThingPosition(m);
            m->floorz=0;m->ceilingz=128*FRACUNIT;
            for(int y=40;y<=120;y+=2)if(!P_TryMove(m,448*FRACUNIT,y*FRACUNIT))I_Error("Floor-backed door blocks walking at %d floor %.1f ceil %.1f drop %.1f",y,tmfloorz/(double)FRACUNIT,tmceilingz/(double)FRACUNIT,tmdropoffz/(double)FRACUNIT);
            for(int y=120;y>=40;y-=2)if(!P_TryMove(m,448*FRACUNIT,y*FRACUNIT))I_Error("Floor-backed door blocks reverse walking at %d",y);
            puts("Saved floor-backed door: impact excavation, breach ray and two-way player passage passed.");
        }
        I_Render3DScreenshotTo("door-save.png");I_Quit();return;
    }
    if(getenv("DOOM_TEST_SOLID_PASSAGE")){
        static int capture,photographed;
        if(gamestate!=GS_LEVEL||!players[0].mo||gametic<12)return;
        if(capture){
            if(gametic>=capture&&!photographed){I_Render3DScreenshotTo("solid-map-passage.png");photographed=1;}
            if(gametic>capture+3)I_Quit();
            return;
        }
        if(!SV_MapMode())I_Error("Ordinary map mode required");
        p_throughwalls=p_breachdoors=1;p_wall_damage=100;
        float hit[6];int owner;
        if(SV_Trace(232,128,40,112,0,0,hit,&owner)>=1)I_Error("Fixture wall starts open");
        for(int x=256;x<=320;x+=16){
            float point[6]={(float)x,128,40,-1,0,0};
            SV_Impact(point,0,44);
        }
        if(SV_Trace(232,128,40,112,0,0,hit,&owner)<1||
           SV_Trace(344,128,40,-112,0,0,hit,&owner)<1)I_Error("Shared map wall still blocks shots");
        mobj_t* m=players[0].mo;
        P_UnsetThingPosition(m);m->x=232*FRACUNIT;m->y=128*FRACUNIT;m->z=FRACUNIT;
        P_SetThingPosition(m);m->floorz=0;m->ceilingz=128*FRACUNIT;
        for(int x=232;x<=344;x+=2)if(!P_TryMove(m,x*FRACUNIT,128*FRACUNIT))I_Error("Shared map volume blocks body at %d",x);
        for(int x=344;x>=232;x-=2)if(!P_TryMove(m,x*FRACUNIT,128*FRACUNIT))I_Error("Shared map volume blocks reverse body at %d",x);
        m->angle=0;players[0].viewz=41*FRACUNIT;
        puts("PASS ordinary map: split linedef destruction, bidirectional shots and native player walking.");
        capture=gametic+4;return;
    }
    if(getenv("DOOM_TEST_STUCKBARON")){
        static int start=-1,last=-1;static mobj_t *trapped=NULL;
        static fixed_t originalX,originalY;
        if(gamestate!=GS_LEVEL||!players[0].mo)return;
        if(start<0){start=gametic;players[0].cheats|=CF_GODMODE;}
        int elapsed=gametic-start;
        if(last!=elapsed&&elapsed%35==0){last=elapsed;
            for(thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next){
                if(th->function.acp1!=(actionf_p1)P_MobjThinker)continue;
                mobj_t *m=(mobj_t*)th;
                if(m->type!=MT_BRUISER||m->health<=0)continue;
                if(elapsed==0&&m->target&&m->ceilingz-m->floorz<m->height+FRACUNIT){
                    trapped=m;originalX=m->x;originalY=m->y;
                }
                printf("BARON %d xyz %.2f %.2f %.2f floor %.2f ceiling %.2f state %d target %d move %d radius %.1f\n",elapsed,m->x/(float)FRACUNIT,m->y/(float)FRACUNIT,m->z/(float)FRACUNIT,m->floorz/(float)FRACUNIT,m->ceilingz/(float)FRACUNIT,(int)(m->state-states),m->target!=NULL,m->movedir,m->radius/(float)FRACUNIT);
                if(elapsed==0){extern fixed_t tmfloorz,tmceilingz,tmdropoffz;
                    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){
                        int ok=P_CheckPosition(m,m->x+dx*8*FRACUNIT,m->y+dy*8*FRACUNIT);
                        printf("PROBE %d %d clear %d floor %.1f ceiling %.1f drop %.1f\n",dx,dy,ok,tmfloorz/(float)FRACUNIT,tmceilingz/(float)FRACUNIT,tmdropoffz/(float)FRACUNIT);
                    }
                }
            }
        }
        if(elapsed==10)I_Render3DScreenshotTo("stuckbaron-before.png");
        if(elapsed>=350){
            if(!trapped||P_AproxDistance(trapped->x-originalX,trapped->y-originalY)<64*FRACUNIT)
                I_Error("Saved Baron remained trapped in shallow crater under low ceiling");
            if(trapped->z+trapped->height>trapped->ceilingz+FRACUNIT)
                I_Error("Baron escaped through ceiling");
            puts("PASS saved Baron steps out of shallow crater and traverses low room");
            I_Render3DScreenshotTo("stuckbaron-after.png");I_Quit();
        }return;
    }
    if(getenv("DOOM_TEST_DEMONSTUCK")){
        static int start=-1,last=-1;static mobj_t *trapped=NULL;static fixed_t originalX,originalY;
        if(gamestate!=GS_LEVEL||!players[0].mo)return;
        if(start<0){start=gametic;players[0].cheats|=CF_GODMODE;}
        int elapsed=gametic-start;
        if(last!=elapsed&&elapsed%35==0){last=elapsed;
            for(thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next){
                if(th->function.acp1!=(actionf_p1)P_MobjThinker)continue;
                mobj_t *m=(mobj_t*)th;
                if(!trapped&&m->type==MT_SERGEANT&&m->health>0&&m->z<0){trapped=m;originalX=m->x;originalY=m->y;}
                if(m->type==MT_SERGEANT&&m->health>0)printf("DEMON %d xyz %.2f %.2f %.2f floor %.2f target %d move %d\n",elapsed,m->x/(float)FRACUNIT,m->y/(float)FRACUNIT,m->z/(float)FRACUNIT,m->floorz/(float)FRACUNIT,m->target!=NULL,m->movedir);
            }
        }
        if(elapsed==20){
            extern fixed_t tmfloorz,tmceilingz,tmdropoffz;
            for(thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next){
                if(th->function.acp1!=(actionf_p1)P_MobjThinker)continue;
                mobj_t *m=(mobj_t*)th;if(m->type!=MT_SERGEANT||m->health<=0||m->z>=0)continue;
                for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){
                    int ok=P_CheckPosition(m,m->x+dx*8*FRACUNIT,m->y+dy*8*FRACUNIT);
                    printf("PROBE %d %d clear %d floor %.1f ceiling %.1f drop %.1f radius %.1f\n",dx,dy,ok,tmfloorz/(float)FRACUNIT,tmceilingz/(float)FRACUNIT,tmdropoffz/(float)FRACUNIT,m->radius/(float)FRACUNIT);
                }
            }
            I_Render3DScreenshotTo("demonstuck-before.png");
        }
        if(elapsed>=210){
            if(!trapped||P_AproxDistance(trapped->x-originalX,trapped->y-originalY)<30*FRACUNIT)I_Error("Demon remained trapped at lower ledge");
            if(trapped->z!=-32*FRACUNIT)I_Error("Demon escaped by climbing the tall ledge");
            puts("PASS saved demon retreats from ledge and moves on the lower floor");
            I_Render3DScreenshotTo("demonstuck-after.png");I_Quit();}return;
    }
    if(getenv("DOOM_TEST_SAVED_ENV")){
        static int start=-1;if(gamestate!=GS_LEVEL||gametic<25)return;if(start<0){start=gametic;
            mobj_t *m=players[0].mo;printf("SAVED ENV E%dM%d position %.1f %.1f %.1f sector %d angle %u\n",gameepisode,gamemap,m->x/(float)FRACUNIT,m->y/(float)FRACUNIT,m->z/(float)FRACUNIT,(int)(m->subsector->sector-sectors),m->angle);
            extern void I_DebugSavedLiquid(void);I_DebugSavedLiquid();
            if(getenv("DOOM_TEST_ENV_LOOKDOWN"))I_Render3DLook(250);
        }
        if(gametic-start==210){extern void I_DebugSavedLiquid(void);I_DebugSavedLiquid();I_Render3DScreenshotTo(getenv("DOOM_TEST_SAVED_ENV"));}
        if(gametic-start>=215)I_Quit();return;
    }
    if(getenv("DOOM_TEST_ROMERO_POPUP")){
        static int start=-1;if(gamestate!=GS_LEVEL||gametic<30)return;
        if(start<0){extern unsigned p_romero_serial;++p_romero_serial;start=leveltime;}
        int elapsed=leveltime-start;
        if(elapsed==12)I_Render3DScreenshotTo("romero-popup-shake.png");
        if(elapsed==30)I_Render3DScreenshotTo("romero-popup-fly.png");
        if(elapsed>48){puts("PASS Romero popup sequence");I_Quit();}return;
    }
    if(getenv("DOOM_TEST_ROMERO")){
        if(gamestate!=GS_LEVEL||gametic<25)return;
        mobj_t *p=players[0].mo;int saved=players[0].cheats;
        players[0].cheats=CF_GODMODE;
        mobj_t *r=P_SpawnMobj(p->x+64*FRACUNIT,p->y,p->z+32*FRACUNIT,MT_ROCKET);r->target=p;
        if(P_RomeroStick(r,1,0,0))I_Error("Disabled cheat stuck rocket");
        for(int twice=0;twice<3;++twice){
            const char *code="johnromerogod";
            while(*code){event_t e={ev_keydown,*code++,0,0};ST_Responder(&e);}
            if(!!(players[0].cheats&CF_ROMERO)!=(twice!=1))I_Error("Typed cheat toggle failed");
        }
        if(P_RomeroStick(r,0,0,1))I_Error("Rocket stuck to floor");
        if(!P_RomeroStick(r,1,0,0))I_Error("Enabled rocket did not stick");
        fixed_t x=r->x,y=r->y,z=r->z;
        for(int i=0;i<TICRATE/2-1;++i)P_MobjThinker(r);
        if(r->x!=x||r->y!=y||r->z!=z||r->threshold!=-32001)I_Error("Fuse did not hold rocket");
        P_MobjThinker(r);
        int children=0;
        for(thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next){
            if(th->function.acp1!=(actionf_p1)P_MobjThinker)continue;
            mobj_t *c=(mobj_t*)th;
            if(c!=r&&c->type==MT_ROCKET&&c->threshold==-32002){++children;
                if(P_RomeroStick(c,1,0,0))I_Error("Child rocket multiplied");
            }
        }
        if(children!=25)I_Error("Expected 25 child rockets, got %d",children);
        freeaim=true;tourpitch=.6f;
        P_RomeroDrunk(p);
        int drunk=0;unsigned directions=0;
        for(thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next){
            if(th->function.acp1!=(actionf_p1)P_MobjThinker)continue;
            mobj_t *c=(mobj_t*)th;
            if(c->type==MT_ROCKET&&c->threshold==-32002&&c->movecount==1){
                ++drunk;directions|=1u<<(c->angle>>29);
                if(c->momz<=0||c->reactiontime!=TICRATE/2)I_Error("Vertical aim or straight fuse failed");
                if(P_RomeroStick(c,1,0,0))I_Error("Drunk missile multiplied");
            }
        }
        if(drunk!=25||directions!=(1u<<(p->angle>>29)))I_Error("Forward launch/count failed");
        puts("PASS drunk rockets: 25 tagged missiles, upward aim and straight launch fuse, recursion rejected");
        players[0].cheats=saved;
        puts("PASS Romero: toggle gating, wall-only sticking, stationary fuse, exactly 25 nonrecursive children");I_Quit();return;
    }
    if(getenv("DOOM_TEST_VOXELS")){
        static int frame=0;if(gamestate!=GS_LEVEL||gametic<25)return;
        ++frame;
        if(frame==1){extern void I_TestVoxelModels(void);I_TestVoxelModels();
            mobj_t *p=players[0].mo;int a=p->angle>>ANGLETOFINESHIFT;
            mobj_t *m=P_SpawnMobj(p->x+128*finecosine[a],p->y+128*finesine[a],p->z,MT_POSSESSED);
            m->angle=p->angle+ANG180;paused=true;}
        if(frame==4)I_Render3DScreenshotTo("voxel-models.png");
        if(frame==8){puts("PASS native voxel GPU scene");I_Quit();}return;
    }
    if(getenv("DOOM_TEST_PS2_STYLE")){
        static int frame=0;if(gamestate!=GS_LEVEL||gametic<25)return;
        extern void I_TestPS2Style(int);
        ++frame;
        if(frame==1){paused=true;I_TestPS2Style(0);}
        if(frame==4)I_Render3DScreenshotTo("ps2-style-off.png");
        if(frame==6)I_TestPS2Style(1);
        if(frame==10)I_Render3DScreenshotTo("ps2-style-on.png");
        if(frame==12)I_TestPS2Style(0);
        if(frame==16)I_Render3DScreenshotTo("ps2-style-restored.png");
        if(frame==18){puts("PASS PS2 style GPU on/off rendering");I_Quit();}return;
    }
    if(getenv("DOOM_TEST_ENV_MENU")){
        static int frame=0;if(gamestate!=GS_LEVEL||gametic<25)return;
        ++frame;
        if(frame==1){extern int I_TestSettingsControls(void);if(!I_TestSettingsControls())I_Error("Settings control regression");puts("PASS menu dependencies, preserved child values, discard, sidebar and navigation");I_Render3DSettingsMenu();I_Render3DSettingsKey(SDL_SCANCODE_TAB,0);I_Render3DSettingsKey(SDL_SCANCODE_TAB,0);}
        if(frame==4)I_Render3DScreenshotTo("environment-menu.png");
        if(frame==5)I_Render3DSettingsKey(SDL_SCANCODE_TAB,0);
        if(frame==8)I_Render3DScreenshotTo("destruction-menu.png");
        if(frame==10){puts("PASS environment and destruction menu render");I_Quit();}return;
    }
    if(getenv("DOOM_TEST_SOLID_PUFF")){
        if(gamestate!=GS_LEVEL||gametic<25)return;
        extern void I_TestElectricalFaults(void);I_TestElectricalFaults();
        extern void I_TestEnvironment(void);I_TestEnvironment();
        extern void I_TestOutdoorWind(void);I_TestOutdoorWind();
        extern void I_TestLiquidProjectiles(void);I_TestLiquidProjectiles();
        extern void I_TestRubbleSelection(void);I_TestRubbleSelection();
        extern void I_TestImpactOptions(void);I_TestImpactOptions();I_Quit();return;
    }
    if(getenv("DOOM_TEST_GLASS_HEIGHT")||getenv("DOOM_TEST_GLASS_SUPPORT")){
        if(gamestate!=GS_LEVEL||gametic<25)return;
        extern void P_ZMovement(mobj_t*);
        mobj_t puff={0};puff.type=MT_PUFF;puff.flags=MF_NOGRAVITY;
        puff.z=32*FRACUNIT;puff.floorz=128*FRACUNIT;puff.ceilingz=256*FRACUNIT;puff.momz=FRACUNIT;
        P_ZMovement(&puff);
        if(puff.z!=33*FRACUNIT)I_Error("Wall puff snapped up to original window floor");
        puff.z=160*FRACUNIT;puff.floorz=0;puff.ceilingz=128*FRACUNIT;
        P_ZMovement(&puff);
        if(puff.z!=161*FRACUNIT)I_Error("Wall puff snapped down to original ceiling");
        puts("PASS cosmetic puff retains impact height beside excavated sector planes");I_Quit();return;
    }
    if(getenv("DOOM_TEST_LAG5")){
        static int started=-1,last=-1,captured=0;
        if(gamestate!=GS_LEVEL||!players[0].mo)return;
        player_t* p=&players[0];mobj_t* m=p->mo;
        if(started<0){started=gametic;p->cheats|=CF_GODMODE;test_pursuit_guards();
            printf("LAG5 start %.1f %.1f %.1f angle %u\n",m->x/(float)FRACUNIT,m->y/(float)FRACUNIT,m->z/(float)FRACUNIT,m->angle);}
        int elapsed=gametic-started;
        if(getenv("DOOM_TEST_SOLID_CAPTURE")&&!captured&&elapsed>=15){captured=1;I_Render3DScreenshotTo("lag5-before.png");}
        if(elapsed>=35&&elapsed<140&&last!=gametic){
            last=gametic;double angle=m->angle*(6.283185307179586/4294967296.0);
            P_TryMove(m,m->x+(fixed_t)(cos(angle)*2*FRACUNIT),m->y+(fixed_t)(sin(angle)*2*FRACUNIT));
            if(elapsed%10==0)printf("LAG5 walking tic %d at %.1f %.1f\n",gametic,m->x/(float)FRACUNIT,m->y/(float)FRACUNIT);
        }
        if(getenv("DOOM_TEST_SOLID_CAPTURE")&&captured==1&&elapsed>=220){captured=2;I_Render3DScreenshotTo("lag5-after.png");}
        if(elapsed>=(getenv("DOOM_TEST_LAG5_LONG")?500:250)){puts("PASS lag5 walking playback");I_Quit();}
        return;
    }
    if(getenv("DOOM_TEST_ACTOR_GROUND")){
        extern int p_breakablewalls;
        if(gamestate!=GS_LEVEL||!players[0].mo||gametic<12)return;
        mobj_t victim=*players[0].mo,projectile={0},shooter={0};
        int types[]={MT_ROCKET,MT_BFG,MT_PLASMA,MT_TROOPSHOT,MT_BRUISERSHOT,MT_HEADSHOT};
        p_breakablewalls=p_player_surface_damage=p_monster_surface_damage=1;
        p_throughwalls=p_breachdoors=1;p_wall_damage=100;
        shooter.type=MT_CYBORG;projectile.target=&shooter;
        for(int i=0;i<6;++i){
            float hit[6];int owner;
            if(SV_Trace(victim.x/(float)FRACUNIT,victim.y/(float)FRACUNIT,
                players[0].mo->z/(float)FRACUNIT+32,0,0,-160,hit,&owner)>=1)
                I_Error("Actor impact fixture has no supporting floor");
            victim.z=(fixed_t)(hit[2]*FRACUNIT);projectile.type=types[i];
            projectile.flags=MF_MISSILE;projectile.x=victim.x;projectile.y=victim.y;
            projectile.z=victim.z+40*FRACUNIT;
            int before=SV_SaveSize();
            SV_MonsterContact(&projectile,&victim);
            if(!SV_Explode(&projectile)||SV_SaveSize()<=before)
                I_Error("Grounded actor hit failed to excavate for missile %d",types[i]);
            int after=SV_SaveSize();SV_Blast(&projectile,128);
            if(SV_SaveSize()!=after)I_Error("Actor impact duplicated explosion excavation");
        }
        int before=SV_SaveSize();victim.z+=96*FRACUNIT;
        if(SV_ActorGroundImpact(&projectile,&victim)||SV_SaveSize()!=before)
            I_Error("Airborne actor excavated a distant floor");
        victim.z-=96*FRACUNIT;p_monster_surface_damage=0;
        if(SV_ActorGroundImpact(&projectile,&victim)||SV_SaveSize()!=before)
            I_Error("Monster surface damage toggle ignored");
        projectile.target=players[0].mo;p_player_surface_damage=0;
        if(SV_ActorGroundImpact(&projectile,&victim)||SV_SaveSize()!=before)
            I_Error("Player surface damage toggle ignored");
        // Exercise PIT_CheckThing through a real rocket/Spider collision too.
        p_player_surface_damage=p_monster_surface_damage=1;
        float floorHit[6];int floorOwner;
        if(SV_Trace(victim.x/(float)FRACUNIT,victim.y/(float)FRACUNIT,
            players[0].mo->z/(float)FRACUNIT+32,0,0,-160,floorHit,&floorOwner)>=1)
            I_Error("Spider fixture has no floor");
        players[0].mo->flags&=~(MF_SOLID|MF_SHOOTABLE);
        mobj_t *spider=P_SpawnMobj(victim.x,victim.y,(fixed_t)(floorHit[2]*FRACUNIT),MT_SPIDER);
        mobj_t *rocket=P_SpawnMobj(victim.x,victim.y,spider->z+32*FRACUNIT,MT_ROCKET);
        rocket->target=&shooter;rocket->momx=20*FRACUNIT;
        int health=spider->health;before=SV_SaveSize();
        if(P_TryMove(rocket,spider->x,spider->y)||spider->health>=health||SV_SaveSize()<=before)
            I_Error("Cyberdemon rocket hitting Spider did not damage its supporting floor");
        int afterContact=SV_SaveSize();
        P_ExplodeMissile(rocket);
        if(SV_SaveSize()!=afterContact)I_Error("Real rocket collision excavated twice");
        puts("PASS actor ground impacts: rocket, BFG, plasma and monster fireballs; airborne exclusion, toggles, no duplicate blast");
        I_Quit();return;
    }
    if(getenv("DOOM_TEST_HOLE")){
        static int started=-1;static angle_t original;
        if(gamestate!=GS_LEVEL||!players[0].mo)return;
        mobj_t *m=players[0].mo;
        if(started<0){started=gametic;original=m->angle;players[0].cheats|=CF_GODMODE;
            printf("HOLE position %.2f %.2f %.2f angle %u\n",m->x/(float)FRACUNIT,m->y/(float)FRACUNIT,m->z/(float)FRACUNIT,m->angle);}
        int elapsed=gametic-started;
        if(elapsed==15)I_Render3DScreenshotTo("hole-front.png");
        if(elapsed==25)m->angle=original-ANG45/3;
        if(elapsed==30)I_Render3DScreenshotTo("hole-left.png");
        if(elapsed==40)m->angle=original+ANG45/3;
        if(elapsed==45)I_Render3DScreenshotTo("hole-right.png");
        if(elapsed==55){puts("PASS HOLE save inspection playback");I_Quit();}
        return;
    }
    if(getenv("DOOM_TEST_LAG3")){
        static int started=-1,lastShot=-1;static angle_t originalAngle;
        if(gamestate!=GS_LEVEL||!players[0].mo)return;
        player_t* p=&players[0];
        if(started<0){started=gametic;p->cheats|=CF_GODMODE;
            weapontype_t weapon=getenv("DOOM_TEST_RUBBLE_ROCKET")?wp_missile:getenv("DOOM_TEST_RUBBLE_BFG")?wp_bfg:wp_plasma;
            p->weaponowned[weapon]=true;p->ammo[am_cell]=600;p->ammo[am_misl]=60;
            p->readyweapon=weapon;p->pendingweapon=wp_nochange;P_SetPsprite(p,ps_weapon,weaponinfo[weapon].readystate);
            originalAngle=p->mo->angle;
            freeaim=true;tourpitch=-.3f;puts("LAG3 concrete plasma");}
        int elapsed=gametic-started;
        if(getenv("DOOM_TEST_SOLID_CAPTURE")&&elapsed==20)I_Render3DScreenshotTo("lag3-before.png");
        if(elapsed>=35&&elapsed<(getenv("DOOM_TEST_RUBBLE")?36:185)&&elapsed/3!=lastShot){
            if(getenv("DOOM_TEST_SOLID_SWEEP")){
                tourpitch=-.3f+.10f*sinf(elapsed*.17f);
                p->mo->angle=originalAngle+(int)(sinf(elapsed*.11f)*(ANG45/12));
            }
            P_FireWeapon(p);lastShot=elapsed/3;
        }
        if(getenv("DOOM_TEST_ROCKET_REFERENCE")){
            if(elapsed==37)I_Render3DScreenshotTo("rocket-reference-launch.png");
            if(elapsed==42)I_Render3DScreenshotTo("rocket-reference-flight.png");
            if(elapsed==47)I_Render3DScreenshotTo("rocket-reference-trail.png");
        }
        if(getenv("DOOM_TEST_RUBBLE_CLOSE")&&elapsed==60){
            double angle=p->mo->angle*(6.283185307179586/4294967296.0);
            P_TryMove(p->mo,p->mo->x+(fixed_t)(40*FRACUNIT*cos(angle)),p->mo->y+(fixed_t)(40*FRACUNIT*sin(angle)));
        }
        if(getenv("DOOM_TEST_SOLID_CAPTURE")&&elapsed==240)I_Render3DScreenshotTo("lag3-after.png");
        if(getenv("DOOM_TEST_RUBBLE")&&elapsed==110)I_Render3DScreenshotTo("rubble-cooling.png");
        if(getenv("DOOM_TEST_RUBBLE")&&elapsed==(getenv("DOOM_TEST_RUBBLE_BFG")?100:65))I_Render3DScreenshotTo(getenv("DOOM_TEST_RUBBLE_BFG")?"rubble-bfg-live.png":getenv("DOOM_TEST_RUBBLE_ROCKET")?"rubble-rocket-live.png":"rubble-plasma-live.png");
        if(elapsed>=260){puts("PASS lag3 plasma playback");I_Quit();}
        return;
    }
    if(getenv("DOOM_TEST_LAG2")){
        static int started=-1;
        if(gamestate!=GS_LEVEL||!players[0].mo)return;
        player_t* p=&players[0];
        if(started<0){started=gametic;p->cheats|=CF_GODMODE;puts("LAG2 idle");}
        int elapsed=gametic-started;
        if(elapsed==100)puts("LAG2 gunfire");
        if(elapsed>=100&&elapsed<200&&elapsed%15==0)P_FireWeapon(p);
        if(elapsed==200)puts("LAG2 stair movement");
        if(elapsed>=200&&elapsed<260){
            double angle=p->mo->angle*(6.283185307179586/4294967296.0);
            int direction=elapsed<230?1:-1;
            P_TryMove(p->mo,p->mo->x+(fixed_t)(cos(angle)*direction*FRACUNIT),
                           p->mo->y+(fixed_t)(sin(angle)*direction*FRACUNIT));
        }
        if(getenv("DOOM_TEST_SOLID_CAPTURE")&&elapsed==280)I_Render3DScreenshotTo("lag2-stairs.png");
        if(elapsed>=300){puts("PASS lag2 playback");I_Quit();}
        return;
    }
    if(getenv("DOOM_TEST_LAGBUTTON")){
        static int started=-1;
        if(gamestate!=GS_LEVEL||!players[0].mo)return;
        if(started<0){
            started=gametic;players[0].cheats|=CF_GODMODE;
            mobj_t* m=players[0].mo;
            printf("LAGBUTTON player %.1f %.1f %.1f angle %u map %d/%d\n",m->x/(float)FRACUNIT,m->y/(float)FRACUNIT,m->z/(float)FRACUNIT,m->angle,gameepisode,gamemap);
            for(int i=0;i<numlines;++i)if(lines[i].special){
                line_t* l=&lines[i];
                double x=((double)l->v1->x+l->v2->x)/2/FRACUNIT,y=((double)l->v1->y+l->v2->y)/2/FRACUNIT;
                if(hypot(x-m->x/(double)FRACUNIT,y-m->y/(double)FRACUNIT)<600)
                    printf("LAGBUTTON line %d special %d tag %d midpoint %.1f %.1f\n",i,l->special,l->tag,x,y);
            }
        }
        static int used=0;
        if(!used&&gametic-started>=20){used=1;P_UseLines(&players[0]);}
        static int captured=0;
        if(getenv("DOOM_TEST_SOLID_CAPTURE")&&!captured&&gametic-started>=390){captured=1;I_Render3DScreenshotTo("lagbutton-restored.png");}
        if(gametic-started>=400){puts("PASS lagbutton playback");I_Quit();}
        return;
    }
    if(getenv("DOOM_TEST_SOLID_MOVERS")){
        static int sectorIndex=-1,start;static fixed_t base;
        static float origin[3],direction[2],before;
        if(gamestate!=GS_LEVEL||!players[0].mo||gametic<12)return;
        if(sectorIndex<0){
            p_throughwalls=p_breachdoors=1;p_wall_damage=100;
            for(int i=0;i<numlines;++i){
                line_t* l=&lines[i];
                if(!l->backsector||l->backsector==l->frontsector||
                   l->backsector->ceilingheight>l->backsector->floorheight||
                   l->frontsector->ceilingheight-l->frontsector->floorheight<96*FRACUNIT)continue;
                float dx=(l->v2->x-l->v1->x)/(float)FRACUNIT,dy=(l->v2->y-l->v1->y)/(float)FRACUNIT,len=sqrtf(dx*dx+dy*dy);
                if(len<64)continue;
                float mx=(l->v1->x/(float)FRACUNIT+l->v2->x/(float)FRACUNIT)*.5f;
                float my=(l->v1->y/(float)FRACUNIT+l->v2->y/(float)FRACUNIT)*.5f;
                float z=l->backsector->ceilingheight/(float)FRACUNIT+40,hit[6];int owner;
                origin[0]=mx+dy/len*16;origin[1]=my-dx/len*16;origin[2]=z;
                direction[0]=-dy/len;direction[1]=dx/len;
                before=64*SV_Trace(origin[0],origin[1],z,direction[0]*64,direction[1]*64,0,hit,&owner);
                if(before<12||before>20)continue;
                sectorIndex=(int)(l->backsector-sectors);base=l->backsector->ceilingheight;
                SV_Impact(hit,owner,24);break;
            }
            if(sectorIndex<0)I_Error("No closed door found for mover test");
            start=gametic;
        }
        int elapsed=gametic-start;
        int height=elapsed<=16?elapsed*2:elapsed<=32?(32-elapsed)*2:0;
        sectors[sectorIndex].ceilingheight=base+height*FRACUNIT;
        P_ChangeSector(&sectors[sectorIndex],false);
        float hit[6];int owner;
        float depth=64*SV_Trace(origin[0],origin[1],origin[2]+height,direction[0]*64,direction[1]*64,0,hit,&owner);
        if(depth<before+5)I_Error("Door damage failed to follow sector at height %d",height);
        if(elapsed>=33){puts("PASS ordinary moving door: anchored hole retained through opening and closing.");I_Quit();}
        return;
    }
    if(getenv("DOOM_TEST_SOLID_MAPS")){
        static int phase,start;
        static float saveRay[5],saveDepth;
        if(gamestate!=GS_LEVEL||!players[0].mo||gametic<12)return;
        mobj_t* m=players[0].mo;
        if(!phase){
            if(!SV_MapMode()||!SV_Active())I_Error("Map volume mode not active");
            if(P_GlobalDigEnabled())I_Error("Legacy and solid geometry systems both enabled");
#ifdef DOOM_TEST_REAL_STARTUP
            if(M_CheckParm("-digglobal"))I_Error("Real startup injected legacy destruction");
            {extern int W_CheckNumForName(char*);if(W_CheckNumForName("DIGROCK")<0)I_Error("Concrete material missing from real startup");}
#endif
            p_throughwalls=1;p_breachdoors=1;p_wall_damage=100;
            float x=m->x/(float)FRACUNIT,y=m->y/(float)FRACUNIT,z=m->z/(float)FRACUNIT+40;
            float hit[6],best[6],distance=1;int owner=0;
            const float dirs[4][2]={{1,0},{0,1},{-1,0},{0,-1}};
            for(int d=0;d<4;++d){
                float f=SV_Trace(x,y,z,dirs[d][0]*2048,dirs[d][1]*2048,0,hit,&owner);
                if(f<distance){distance=f;memcpy(best,hit,sizeof(best));}
            }
            if(distance>=1)I_Error("No wall found for ordinary map impact");
            m->angle=R_PointToAngle2(m->x,m->y,(fixed_t)(best[0]*FRACUNIT),(fixed_t)(best[1]*FRACUNIT));
            SV_Impact(best,0,24);
            {
                float dx=best[0]-x,dy=best[1]-y,length=sqrtf(dx*dx+dy*dy);
                float f=SV_Trace(x,y,z,dx/length*4096,dy/length*4096,0,hit,&owner);
                if(f*4096<length+5)I_Error("Ordinary map impact did not remove wall: before %.2f after %.2f",length,f*4096);
                printf("Map impact ray depth before %.2f after %.2f\n",length,f*4096);
                saveRay[0]=x;saveRay[1]=y;saveRay[2]=z;saveRay[3]=dx/length;saveRay[4]=dy/length;saveDepth=f*4096;
            }
            int size=SV_SaveSize();unsigned char* bytes=malloc(size);
            if(SV_Save(bytes)!=size)I_Error("Map volume save size mismatch");
            SV_Load(bytes,size);free(bytes);
            float floorhit[6];
            if(SV_Trace(x+32,y,z,0,0,-128,floorhit,&owner)<1)SV_Impact(floorhit,owner,16);
            printf("PASS ordinary map impact/save test E%dM%d\n",gameepisode,gamemap);
            phase=1;start=gametic;
        }
        if(phase==1&&gametic-start>=12){I_Render3DScreenshotTo("solid-map.png");phase=2;}
        if(phase==2&&gametic-start>=16){
            if(!getenv("DOOM_TEST_SOLID_SAVE"))I_Quit();
            G_SaveGame(5,"Solid maps native save");
            {extern void G_DoSaveGame(void);extern boolean sendsave;sendsave=false;G_DoSaveGame();}
            phase=3;start=gametic;return;
        }
        if(phase==3&&gametic-start>=4){G_LoadGame("doomsav5.dsg");phase=4;return;}
        if(phase==4&&gameaction==ga_nothing){
            float hit[6];int owner;
            float depth=4096*SV_Trace(saveRay[0],saveRay[1],saveRay[2],saveRay[3]*4096,saveRay[4]*4096,0,hit,&owner);
            if(fabsf(depth-saveDepth)>.1f)I_Error("Native save lost solid map damage");
            puts("PASS solid maps full native save/load retained geometry.");
            I_Quit();
        }
        return;
    }
    if(getenv("DOOM_TEST_SOLID_FIRE")){
        static int start=-1,shots=0,captured=0;
        int shotCount=getenv("DOOM_TEST_SOLID_SHOTS")?atoi(getenv("DOOM_TEST_SOLID_SHOTS")):4;
        if(shotCount<1||shotCount>64)shotCount=4;
        if(gamestate!=GS_LEVEL||!players[0].mo||gametic<12)return;
        player_t *p=&players[0];
        if(start<0){
            start=gametic;p->cheats|=CF_GODMODE;
            if(SV_MapMode()){
                p->weaponowned[wp_missile]=true;p->ammo[am_misl]=50;
                p->readyweapon=wp_missile;p->pendingweapon=wp_nochange;
                P_SetPsprite(p,ps_weapon,S_MISSILE);
                p->mo->angle=ANG180;p_throughwalls=p_breachdoors=1;
                if(getenv("DOOM_TEST_SOLID_DOOR")){
                    for(int i=0;i<numlines;++i){
                        line_t* l=&lines[i];
                        if(!l->backsector||l->backsector==l->frontsector||
                           l->backsector->ceilingheight>l->backsector->floorheight||
                           l->frontsector->ceilingheight-l->frontsector->floorheight<64*FRACUNIT)continue;
                        float dx=(l->v2->x-l->v1->x)/(float)FRACUNIT,dy=(l->v2->y-l->v1->y)/(float)FRACUNIT,len=sqrtf(dx*dx+dy*dy);
                        if(len<64)continue;
                        fixed_t mx=(fixed_t)(((double)l->v1->x+l->v2->x)*.5),my=(fixed_t)(((double)l->v1->y+l->v2->y)*.5);
                        fixed_t x=mx+(fixed_t)(dy/len*96*FRACUNIT),y=my-(fixed_t)(dx/len*96*FRACUNIT);
                        if(!P_CheckPosition(p->mo,x,y))continue;
                        P_UnsetThingPosition(p->mo);p->mo->x=x;p->mo->y=y;p->mo->z=l->frontsector->floorheight;
                        P_SetThingPosition(p->mo);p->mo->floorz=l->frontsector->floorheight;p->mo->ceilingz=l->frontsector->ceilingheight;
                        p->mo->angle=R_PointToAngle2(x,y,mx,my);p->viewz=p->mo->z+41*FRACUNIT;break;
                    }
                }
            }
        }
        if(shots<shotCount&&gametic-start>=20+shots*35){printf("SOLID FIRE shot %d tic %d\n",shots,gametic);P_FireWeapon(p);++shots;}
        if(getenv("DOOM_TEST_SOLID_CAPTURE")&&shots>captured&&gametic-start>=45+(shots-1)*35){
            static char name[64];snprintf(name,sizeof(name),"solid-shot-%d.png",shots);I_Render3DScreenshotTo(name);captured=shots;
        }
        if(gametic-start>=40+shotCount*35){puts("PASS solid real weapon firing profile");I_Quit();}return;
    }
    if(getenv("DOOM_TEST_SOLID_PERF")){
        static int phase,start,shot;
        if(gamestate!=GS_LEVEL||!players[0].mo||gametic<12)return;
        if(!phase){SV_Use(&players[0]);start=gametic;phase=1;}
        if(!shot&&gametic-start>=15){float point[6]={56,96,0,0,0,1};SV_Impact(point,0,18);shot=1;}
        if(phase==1&&gametic-start>=60){SV_Use(&players[0]);phase=2;}
        if(gametic-start>=120){puts("PASS solid door motion profile");I_Quit();}return;
    }
    if(getenv("DOOM_TEST_SOLID_GAMEPLAY")){
        static int phase,savedTic;
        if(gamestate!=GS_LEVEL||!players[0].mo||gametic<12||gameaction!=ga_nothing)return;
        mobj_t *m=players[0].mo;
        if(!phase){
            if(!SV_Active())I_Error("Solid gameplay test not active");
            float hit[6];int owner;
            if(SV_Trace(56,96,42,160,0,0,hit,&owner)>=.5f||owner!=1)I_Error("Solid door ray not blocked");
            if(P_TryMove(m,128*FRACUNIT,96*FRACUNIT))I_Error("Solid door walking not blocked");
            mobj_t *enemy=P_SpawnMobj(208*FRACUNIT,96*FRACUNIT,0,MT_TROOP);
            int health=enemy->health;
            P_LineAttack(m,0,512*FRACUNIT,0,5);
            if(enemy->health!=health||P_CheckSight(m,enemy))I_Error("Solid door did not block shot/sight");
            P_RemoveMobj(enemy);
            SV_Use(&players[0]);for(int i=0;i<60;++i)SV_Tick();
            for(int x=56;x<=200;x+=2)if(!P_TryMove(m,x*FRACUNIT,96*FRACUNIT))I_Error("Open solid door blocks at %d",x);
            for(int x=200;x>=56;x-=2)if(!P_TryMove(m,x*FRACUNIT,96*FRACUNIT))I_Error("Open solid door blocks return at %d",x);
            SV_Use(&players[0]);for(int i=0;i<60;++i)SV_Tick();
            mobj_t *rocket=P_SpawnMobj(80*FRACUNIT,96*FRACUNIT,42*FRACUNIT,MT_ROCKET);
            rocket->momx=64*FRACUNIT;SV_Missile(rocket);
            if(rocket->flags&MF_MISSILE)I_Error("Rocket failed to collide with volume");P_RemoveMobj(rocket);
            float cut[6]={116,96,40,-1,0,0};SV_Impact(cut,1,48);
            for(int x=56;x<=200;x+=2)if(!P_TryMove(m,x*FRACUNIT,96*FRACUNIT))I_Error("Blasted solid door blocks at %d",x);
            for(int x=200;x>=56;x-=2)if(!P_TryMove(m,x*FRACUNIT,96*FRACUNIT))I_Error("Blasted door return blocks %d",x);
            enemy=P_SpawnMobj(208*FRACUNIT,96*FRACUNIT,0,MT_TROOP);health=enemy->health;
            P_LineAttack(m,0,512*FRACUNIT,0,5);
            if(enemy->health>=health||!P_CheckSight(m,enemy))I_Error("Blasted door still blocks shot/sight");P_RemoveMobj(enemy);
            G_SaveGame(5,"Solid geometry gameplay");{extern void G_DoSaveGame(void);extern boolean sendsave;sendsave=false;G_DoSaveGame();}savedTic=gametic;phase=1;return;
        }
        if(phase==1){if(gametic<savedTic+4)return;G_LoadGame("doomsav5.dsg");phase=2;return;}
        if(phase==2){
            for(int x=56;x<=200;x+=2)if(!P_TryMove(m,x*FRACUNIT,96*FRACUNIT))I_Error("Loaded solid door blocks %d",x);
            for(int x=200;x>=56;x-=2)if(!P_TryMove(m,x*FRACUNIT,96*FRACUNIT))I_Error("Loaded solid return blocks %d",x);
            float cutFloor[6]={56,96,0,0,0,1};SV_Impact(cutFloor,0,18);SV_Refresh(m);
            if(m->floorz>=0||m->floorz< -8*FRACUNIT)I_Error("Excavated floor support outside closed cell");
            {extern void P_ZMovement(mobj_t*);for(int i=0;i<70;++i){SV_Refresh(m);P_ZMovement(m);}}
            if(m->z< -8*FRACUNIT||m->z>0)I_Error("Player fell out of solid floor");
            float cutCeiling[6]={56,96,208,0,0,-1};SV_Impact(cutCeiling,0,18);SV_Refresh(m);
            if(m->ceilingz<=208*FRACUNIT||m->ceilingz>216*FRACUNIT)I_Error("Excavated ceiling support invalid");
            players[0].damagecount=0;players[0].bonuscount=0;players[0].health=m->health=100;
            puts("PASS solid gameplay: door use, collision, rockets, hitscan, sight, two-way walking, native save/load, floor/ceiling support.");
            savedTic=gametic;phase=3;return;
        }
        if(phase==3){if(gametic<savedTic+25)return;I_Render3DScreenshotTo("solid-gameplay.png");phase=4;return;}
        I_Quit();return;
    }
    if(getenv("DOOM_TEST_MISSINGWALL")){
        static int phase,sideCapture;
        if(phase==3){players[0].mo->angle=ANG90;if(gametic<sideCapture)return;phase=2;}
        if(gamestate!=GS_LEVEL||!players[0].mo||gametic<12)return;
        if(!phase&&getenv("DOOM_TEST_BREACH_ENCLOSURE")){
            // The shared vertex is on the enclosure, never inside removed
            // volume. Body-fit tolerances must not punch through this skin.
            int checks=0;
            for(int id=0;id<numlines;++id)for(int side=0;side<2;++side){
                if(lines[id].sidenum[side]<0||!P_GlobalDigBreachFace(id,side))continue;
                vertex_t *ends[2]={lines[id].v1,lines[id].v2};
                for(int e=0;e<2;++e)for(int other=0;other<numlines;++other){
                    if(other==id||(lines[other].v1!=ends[e]&&lines[other].v2!=ends[e]))continue;
                    for(int z=-56;z<=216;z+=4){
                        float margin=P_GlobalDigVoidMargin(other,ends[e]->x/(float)FRACUNIT,ends[e]->y/(float)FRACUNIT,(float)z);
                        if(margin>0)I_Error("Breach removed enclosing vertex %d/%d z=%d",id,other,z);
                        ++checks;
                    }
                }
            }
            if(!checks)I_Error("Enclosure test found no breaches");
            printf("Breach enclosure: %d boundary samples retained.\n",checks);
        }
        if(!phase&&getenv("DOOM_TEST_SIDE_PASSAGE")){
            p_throughwalls=1;p_breachdoors=1;
            for(int n=0;n<80;++n)for(int u=0;u<=32;u+=8)for(int h=0;h<=72;h+=8)global_hit(472,0,u,h);
            printf("SIDE bounds sectors 3 %d/%d 4 %d/%d 27 %d/%d margin low %.3f high %.3f\n",sectors[3].floorheight/FRACUNIT,sectors[3].ceilingheight/FRACUNIT,sectors[4].floorheight/FRACUNIT,sectors[4].ceilingheight/FRACUNIT,sectors[27].floorheight/FRACUNIT,sectors[27].ceilingheight/FRACUNIT,P_GlobalDigBreachMargin(472,0,16,.1f),P_GlobalDigBreachMargin(472,0,16,56));
            if(!P_GlobalDigBreachRay(472,1360,-3392,32,0,1,0))I_Error("Door side failed to breach to next boundary");
            int bytes=P_GlobalDigSaveSize();unsigned char *saved=malloc(bytes);P_GlobalDigSave(saved);P_GlobalDigReset();P_GlobalDigLoad(saved,bytes);free(saved);
            mobj_t *m=players[0].mo;P_UnsetThingPosition(m);m->x=1360*FRACUNIT;m->y=-3420*FRACUNIT;m->z=0;P_SetThingPosition(m);m->floorz=0;m->ceilingz=72*FRACUNIT;
            for(int y=-3420;y<=-3328;y+=2){if(!P_TryMove(m,1360*FRACUNIT,y*FRACUNIT))I_Error("Side passage blocks player y=%d floor=%d ceil=%d",y,tmfloorz/FRACUNIT,tmceilingz/FRACUNIT);m->z=m->floorz;}
            for(int y=-3328;y>=-3420;y-=2){if(!P_TryMove(m,1360*FRACUNIT,y*FRACUNIT))I_Error("Side passage reverse blocked y=%d",y);m->z=m->floorz;}
            puts("Door side: full excavation to next sidedef, ray, save/load and two-way walking passed.");m->angle=ANG90;sideCapture=gametic+4;phase=3;return;
        }
        if(!phase&&getenv("DOOM_TEST_EXPOSED_ENDS")){
            if(gameepisode!=1||gamemap!=1)I_Error("Wrong exposed ends fixture");
            for(int id=472;id<=473;++id){
                float before=P_GlobalDigDepth(id,0,16,32);
                for(int n=0;n<4;++n)global_hit(id,0,16,32);
                if(P_GlobalDigDepth(id,0,16,32)<=before)I_Error("Exposed door side %d rejected impacts",id);
            }
            mobj_t shot=*players[0].mo;shot.type=MT_ROCKET;shot.x=1360*FRACUNIT;shot.y=-3456*FRACUNIT;shot.subsector=R_PointInSubsector(shot.x,shot.y);
            int room=(int)(shot.subsector->sector-sectors);float before=P_GlobalDigFlatDepth(room,0,1360,-3456);
            P_GlobalDigFlatImpact(&shot,0);P_GlobalDigImpact(&shot);
            if(P_GlobalDigFlatDepth(room,0,1360,-3456)<=before)I_Error("Exposed door floor rejected impacts");
            float floor=0,ceiling=0;P_GlobalDigBreachRoomBounds(4,&floor,&ceiling);
            if(ceiling<72)I_Error("Exposed door roof has no enclosing height");
            puts("Exposed door sides and floor accept impacts; enclosing roof bounds passed.");
            I_Render3DLook(-400);phase=2;return;
        }
        if(!phase&&getenv("DOOM_TEST_WALLGLITCH")){
            if(!P_GlobalDigWallJoined(469,0,1)||!P_GlobalDigWallJoined(471,0,1))I_Error("Mixed backing faces were not joined");
            for(int h=8;h<72;h+=4){
                if(fabsf(P_GlobalDigDepth(469,0,32,h)-P_GlobalDigDepth(471,0,0,h))>.01f||
                   fabsf(P_GlobalDigDepth(471,0,128,h)-P_GlobalDigDepth(470,0,0,h))>.01f)I_Error("Jamb seam depths differ at %d",h);
            }
            fixed_t original=sectors[4].ceilingheight;unsigned rev=P_GlobalDigWallRevision(469,0);
            sectors[4].ceilingheight+=16*FRACUNIT;
            if(P_GlobalDigWallRevision(469,0)==rev)I_Error("Stationary jamb cache ignored door motion");
            sectors[4].ceilingheight=original;
            if(P_GlobalDigWallRevision(469,0)!=rev)I_Error("Door motion cache did not restore");
            puts("Mixed door/wall joins: both seam depths and moving-neighbor invalidation passed.");
            if(getenv("DOOM_TEST_JAMB_LEFT"))players[0].mo->angle+=ANG45/2;
            if(getenv("DOOM_TEST_JAMB_RIGHT"))players[0].mo->angle-=ANG45/2;
            if(getenv("DOOM_TEST_JAMB_OPEN")){sectors[4].ceilingheight=72*FRACUNIT;phase=2;return;}
        }
        if(!phase&&getenv("DOOM_TEST_DOOR_OPEN")){sectors[26].ceilingheight=124*FRACUNIT;phase=2;return;}
        if(!phase&&getenv("DOOM_TEST_MISSING_OFF")){extern int p_breakablewalls;p_breakablewalls=0;phase=2;return;}
        if(phase==2||!phase){
            if(getenv("DOOM_TEST_SEETHROUGH"))for(int id=143;id<=155;++id)for(int side=0;side<2;++side){
                if(lines[id].sidenum[side]<0)continue;
                float b[4];int found=P_GlobalDigBounds(id,side,b);
                printf("FACE %d/%d breach=%d cut=%d bounds=%.1f %.1f %.1f %.1f\n",id,side,P_GlobalDigBreachFace(id,side),found,b[0],b[1],b[2],b[3]);
            }
            mobj_t *m=players[0].mo;printf("MISSINGWALL E%dM%d pos %.2f %.2f %.2f sector %d angle %u\n",gameepisode,gamemap,m->x/(double)FRACUNIT,m->y/(double)FRACUNIT,m->z/(double)FRACUNIT,(int)(m->subsector->sector-sectors),m->angle);I_Render3DScreenshotTo("missingwall.png");phase=1;return;}
        I_Quit();return;
    }
    if(getenv("DOOM_TEST_SEAMS_SAVE")){
        static int phase;
        if(gamestate!=GS_LEVEL||!players[0].mo||gametic<12)return;
        if(!phase){I_Render3DLook(400);phase=1;return;}
        if(phase==1){I_Render3DScreenshotTo("seams.png");phase=2;return;}I_Quit();return;
    }
    if(getenv("DOOM_TEST_STUCK_SAVE")){
        static int last=-1;
        if(gamestate!=GS_LEVEL||!players[0].mo)return;
        players[0].cheats|=CF_GODMODE;
        if(gametic!=last&&(gametic<5||gametic%70==0)){
            last=gametic;int n=0;
            for(thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next){
                if(th->function.acp1!=(actionf_p1)P_MobjThinker)continue;
                mobj_t *m=(mobj_t*)th;
                if(m->type!=MT_SHOTGUY||m->health<=0)continue;
                fprintf(stderr,"SERGEANT tic=%d id=%d x=%.3f y=%.3f z=%.3f floor=%.3f dir=%d state=%d target=%d\n",gametic,n++,m->x/(double)FRACUNIT,m->y/(double)FRACUNIT,m->z/(double)FRACUNIT,m->floorz/(double)FRACUNIT,m->movedir,(int)(m->state-states),m->target!=NULL);
            }
        }
        if(gametic>350)I_Quit();return;
    }

#ifdef DOOM_RENDER3D
    if(getenv("DOOM_CAPTURE_IMPACTS")&&gamestate==GS_LEVEL){
        static int start=-1,shot=0,capture=0,menu=0;player_t *p=&players[0];
        if(start<0){
            line_t *face=0;
            for(int i=0;i<numlines;++i){line_t *l=&lines[i];
                if(l->special==1&&l->backsector&&l->backsector->floorheight==l->backsector->ceilingheight){face=l;break;}}
            if(!face)I_Error("No impact capture wall");
            double dx=(double)face->dx/FRACUNIT,dy=(double)face->dy/FRACUNIT,len=sqrt(dx*dx+dy*dy);
            fixed_t x=(fixed_t)(((double)face->v1->x+face->v2->x)/2+dy/len*96*FRACUNIT);
            fixed_t y=(fixed_t)(((double)face->v1->y+face->v2->y)/2-dx/len*96*FRACUNIT);
            P_TeleportMove(p->mo,x,y);p->mo->z=p->mo->floorz;p->viewz=p->mo->z+VIEWHEIGHT;
            p->mo->angle=R_PointToAngle2(x,y,(face->v1->x+face->v2->x)/2,(face->v1->y+face->v2->y)/2);
            p->cheats|=CF_GODMODE;start=gametic;R_SetViewSize(11,0);
        }
        int t=gametic-start;
        if(shot<3&&t>=10+shot*15){P_LineAttack(p->mo,p->mo->angle,MISSILERANGE,0,5);++shot;}
        if(shot==3&&t>=65){P_SpawnPlayerMissile(p->mo,MT_ROCKET);++shot;}
        int extended=getenv("DOOM_CAPTURE_FLUID_PROJECTILES")!=NULL;
        if(extended&&shot==4&&t>=105){P_SpawnPlayerMissile(p->mo,MT_PLASMA);++shot;}
        if(extended&&shot==5&&t>=145){P_SpawnPlayerMissile(p->mo,MT_BFG);++shot;}
        const int captures[]={12,27,42,72,76,83,100,112,130,155,180,210};
        if(capture<(extended?12:7)&&t>=captures[capture]){I_Render3DScreenshot();++capture;}
        if(!extended&&t>=95&&!menu){I_Render3DSettingsMenu();for(int i=0;i<2;++i)I_Render3DSettingsKey(SDL_SCANCODE_TAB,0);menu=1;}
        if(t>(extended?225:115))I_Quit();return;
    }
    if(getenv("DOOM_CAPTURE_DOORS")&&gamestate==GS_LEVEL){
        static int start=-1,last=-1,closed=0,shots=0;static line_t *face;static fixed_t x,y;
        player_t *p=&players[0];
        if(start<0){
            for(int i=0;i<numlines;++i){line_t *l=&lines[i];sector_t *sec=l->backsector;
                if(l->special==1&&sec&&!sec->tag&&!sec->special&&!sec->specialdata&&sec->floorheight==sec->ceilingheight){face=l;break;}}
            if(!face)I_Error("No door for capture");
            double dx=(double)face->dx/FRACUNIT,dy=(double)face->dy/FRACUNIT,len=sqrt(dx*dx+dy*dy);
            x=(fixed_t)(((double)face->v1->x+face->v2->x)/2+dy/len*96*FRACUNIT);
            y=(fixed_t)(((double)face->v1->y+face->v2->y)/2-dx/len*96*FRACUNIT);
            P_TeleportMove(p->mo,x,y);p->mo->z=p->mo->floorz;p->viewz=p->mo->z+VIEWHEIGHT;
            p->mo->angle=R_PointToAngle2(x,y,(face->v1->x+face->v2->x)/2,(face->v1->y+face->v2->y)/2);
            p->cheats|=CF_GODMODE;start=gametic;R_SetViewSize(11,0);EV_VerticalDoor(face,p->mo);
        }
        int t=gametic-start;if(last==t)return;last=t;

        if(t==8||t==12||t==22||t==30||t==40||t==65||t==163||t==175){fprintf(stderr,"Door capture tic %d height %d thinker %p hits %d\n",t,face->backsector->ceilingheight/FRACUNIT,face->backsector->specialdata,face->backsector->rocketdoorhits);I_Render3DScreenshot();}
        if(t>=75&&!closed){EV_VerticalDoor(face,p->mo);closed=1;}
        if(shots<3&&t>=150+10*shots){
            ++shots;
            mobj_t rocket={0};rocket.type=MT_ROCKET;rocket.target=p->mo;rocket.x=x;rocket.y=y;
            rocket.z=face->backsector->floorheight+48*FRACUNIT;rocket.height=8*FRACUNIT;
            p_destructibledoors=1;P_RocketDoorImpact(&rocket,face);
        }
        if(t>190)I_Quit();return;
    }
    if(getenv("DOOM_CAPTURE_SSG_FIDGET")) {
        static int captured=-1;
        if(test_tic!=captured&&(test_tic==205||test_tic==207||test_tic==210||test_tic==215||test_tic==275||test_tic==280)) {
            captured=test_tic;I_Render3DScreenshot();
        }
    }

    if(getenv("DOOM_CAPTURE_ORIGINAL_SWITCH")&&gamestate==GS_LEVEL){
        static int begun=-1,last=-1;int t;player_t *p=&players[0];
        if(begun<0){begun=gametic;p->cheats|=CF_GODMODE;R_SetViewSize(11,0);for(int w=0;w<NUMWEAPONS;++w)p->weaponowned[w]=true;for(int a=0;a<NUMAMMO;++a)p->ammo[a]=200;}
        if(last==gametic)return;last=gametic;t=gametic-begun;
        if(t>=35&&t<665&&(t-35)%70==0)p->pendingweapon=(t-35)/70;
        if(t>=35&&t<665){int phase=(t-35)%70;
            if(phase==7||phase==23||phase==29||phase==36||phase==52){
                printf("Switch capture: requested weapon %d phase %d\n",(t-35)/70,phase);I_Render3DScreenshot();
            }
        }
        if(t==675){puts("Original switch capture sequence passed.");I_Quit();}
        return;
    }
    if(getenv("DOOM_TEST_SWITCH_CLOAK")&&gamestate==GS_LEVEL){
        static int begun=-1,last=-1;int t;player_t *p=&players[0];
        if(begun<0){begun=gametic;p->cheats|=CF_GODMODE;R_SetViewSize(11,0);for(int w=0;w<NUMWEAPONS;++w)p->weaponowned[w]=true;for(int a=0;a<NUMAMMO;++a)p->ammo[a]=200;}
        if(last==gametic)return;last=gametic;t=gametic-begun;
        if(t>=35&&t<350&&(t-35)%35==0)p->pendingweapon=(t-35)/35;
        if(t>=35&&t<350&&((t-35)%35==7||(t-35)%35==23))I_Render3DScreenshot();
        if(t==355){p->powers[pw_invisibility]=500;}
        if(t==360)I_Render3DScreenshot();
        if(t==365)p->powers[pw_invisibility]=8;
        if(t==380){I_Render3DScreenshot();puts("Weapon switch and invisibility capture sequence passed.");I_Quit();}
        return;
    }
    if(getenv("DOOM_TEST_KEY_LIGHTS")&&gamestate==GS_LEVEL&&gametic>20){
        extern void I_TestKeyLights(void);I_TestKeyLights();I_Quit();return;
    }
    if(getenv("DOOM_TEST_BROKEN_FLICKER")&&gamestate==GS_LEVEL&&gametic>20){
        extern void I_TestBrokenFlicker(void);I_TestBrokenFlicker();I_Quit();return;
    }
    if(getenv("DOOM_TEST_MENU")) {
        static int last=-1,phase=0,start=-1;
        if(start<0)start=gametic;
        if(last==gametic)return;last=gametic;
        int t=gametic-start;
        if(t>=10&&phase==0){I_Render3DScreenshot();phase++;}
        else if(t>=15&&phase==1){key(ev_keydown,KEY_ESCAPE);phase++;}
        else if(t>=20&&phase==2){I_Render3DScreenshot();phase++;}
        else if(t>=25&&phase==3){key(ev_keydown,KEY_DOWNARROW);phase++;}
        else if(t>=30&&phase==4){key(ev_keydown,KEY_ENTER);phase++;}
        else if(t>=35&&phase==5){I_Render3DScreenshot();phase++;}
        else if(t>=40&&phase==6){key(ev_keydown,KEY_BACKSPACE);phase++;}
        else if(t>=45&&phase==7){key(ev_keydown,KEY_UPARROW);phase++;}
        else if(t>=50&&phase==8){key(ev_keydown,KEY_ENTER);phase++;}
        else if(t>=55&&phase==9){I_Render3DScreenshot();phase++;}
        else if(t>=60&&phase==10){key(ev_keydown,KEY_ENTER);phase++;}
        else if(t>=65&&phase==11){key(ev_keydown,KEY_ENTER);phase++;}
        if(phase==12&&gamestate==GS_LEVEL){puts("Title, main menu, options, episode and new-game navigation passed.");I_Quit();}
        if(t>350)I_Error("Startup menu navigation failed phase=%d",phase);
        return;
    }
    if(getenv("DOOM_TEST_GALLERY")&&gamestate==GS_LEVEL){
        static int begun=-1,last=-1,phase=0;int t;player_t* p=&players[0];
        if(begun<0){begun=gametic;p->cheats|=CF_GODMODE;R_SetViewSize(11,0);}
        if(last==gametic)return;last=gametic;t=gametic-begun;
        if(t>=10&&phase==0){key(ev_keydown,'w');phase=1;}
        if(t>=25&&phase==1){P_UseLines(p);phase=2;}
        if(t>=80&&phase==2){key(ev_keyup,'w');phase=3;}
        if(t>=90&&phase==3){if(p->mo->subsector->sector-sectors!=2)I_Error("Gallery traversal: sector=%d x=%d y=%d door=%d",(int)(p->mo->subsector->sector-sectors),p->mo->x/FRACUNIT,p->mo->y/FRACUNIT,sectors[3].ceilingheight/FRACUNIT);I_Render3DScreenshot();phase=4;}
        if(t>=110&&phase==4){p->mo->angle+=ANG180;phase=5;}
        if(t>=125&&phase==5){I_Render3DScreenshot();phase=6;}
        if(t>=145){puts("Material gallery loaded; working door and room traversal passed.");I_Quit();}
        return;
    }
    if(getenv("DOOM_TEST_KEYS")&&gamestate==GS_LEVEL){
        static int begun=-1,last=-1;static mobj_t* cards[3];static unsigned event;
        extern unsigned p_keypickup_serial[MAXPLAYERS];
        extern int I_TestKeyLight(mobj_t*);
        player_t* p=&players[0];int t;
        if(begun<0){
            int i;double angle=p->mo->angle*(6.283185307179586/4294967296.0);begun=gametic;
            p->cheats|=CF_GODMODE;R_SetViewSize(11,0);
            for(i=0;i<3;++i){
                fixed_t x=p->mo->x+(fixed_t)((cos(angle)*96-sin(angle)*(i-1)*48)*FRACUNIT);
                fixed_t y=p->mo->y+(fixed_t)((sin(angle)*96+cos(angle)*(i-1)*48)*FRACUNIT);
                cards[i]=P_SpawnMobj(x,y,ONFLOORZ,(mobjtype_t)(MT_MISC4+i));
            }
            p->cards[it_redcard]=0;event=p_keypickup_serial[0];
        }
        t=gametic-begun;
        if(last==gametic)return;last=gametic;
        if(t==35)I_Render3DScreenshot();
        if(t==50){int i;for(i=0;i<3;++i)if(!I_TestKeyLight(cards[i]))I_Error("Keycard light missing");}
        if(t==60){cards[1]->z=p->mo->z;P_TouchSpecialThing(cards[1],p->mo);if(!p->cards[it_redcard]||p_keypickup_serial[0]!=event+1)I_Error("Key pickup notification missing");P_GiveCard(p,it_redcard);if(p_keypickup_serial[0]!=event+1)I_Error("Duplicate key notification");}
        if(t==70&&I_TestKeyLight(cards[1]))I_Error("Collected key left a light behind");
        if(t==64||t==80||t==110||t==165||t==180)I_Render3DScreenshot();
        if(t==135){int i,mapped=0;for(i=0;i<numlines;++i)if(lines[i].flags&ML_MAPPED)++mapped;if(mapped==0||mapped==numlines)I_Error("Automap visibility discovery invalid");key(ev_keydown,KEY_TAB);}
        if(t==136)key(ev_keyup,KEY_TAB);
        if(t==145){if(!automapactive)I_Error("Automap did not open");I_Render3DScreenshot();}
        if(t==155)key(ev_keydown,KEY_TAB);
        if(t==156)key(ev_keyup,KEY_TAB);
        if(t>=190){if(automapactive)I_Error("Automap did not close");puts("Keycard lighting, pickup event, duplicate suppression, light removal and automap discovery passed.");I_Quit();}
        return;
    }
    if(getenv("DOOM_EFFECTS_OUT")&&gamestate==GS_LEVEL){extern void I_TestEffectsPreview(int);I_TestEffectsPreview(test_tic);return;}
    if(getenv("DOOM_TEST_BREACHES")){test_breaches();return;}
    if(getenv("DOOM_TEST_SURFACES")){test_surfaces();return;}
    if(getenv("DOOM_TEST_GLOBAL_DIGGING")){test_global_digging();return;}
    if(getenv("DOOM_TEST_DIGGING")){test_digging();return;}
    if(getenv("DOOM_TEST_SETTINGS")){test_settings();return;}
    if(getenv("DOOM_TEST_WHEEL")){test_wheel();return;}
#endif
    if(getenv("DOOM_TEST_SIGIL_ROUTE")){test_sigil_route();return;}
#ifdef DOOM_RENDER3D
    if(stage==14&&test_tic>=475){
        if(gamestate!=GS_INTERMISSION)I_Error("Exit notice fixture did not enter intermission");
        if(test_tic==475)I_Render3DScreenshot();
        if(test_tic>=480)I_Quit();
    }
#endif
    if (gamestate != GS_LEVEL) return;
    if(P_UseUZMovement() && stage==3){
        if(test_tic==60){
            if(abs(players[0].mo->momx)>FRACUNIT/2||abs(players[0].mo->momy)>FRACUNIT/2)I_Error("UZ movement did not slow after coasting");
            key(ev_keydown,' ');
        }
        if(test_tic==63){
            if(players[0].mo->z<=players[0].mo->floorz||players[0].mo->momz<=0)I_Error("Jump failed to leave floor");
            key(ev_keyup,' ');
        }
        if(test_tic==100){
            if(players[0].mo->z!=players[0].mo->floorz)I_Error("Jump failed to land");
            if(abs(players[0].mo->momx)>FRACUNIT/8||abs(players[0].mo->momy)>FRACUNIT/8)I_Error("UZ movement did not settle after landing");
            puts("UZ movement coast, jump and landing passed.");
        }
    }
#ifdef DOOM_RENDER3D
    if(getenv("DOOM_TEST_FIDGETS")&&(test_tic==204||test_tic==218||test_tic==285))I_Render3DScreenshot();
    if(getenv("DOOM_TEST_FIRING_LAYERS")&&(test_tic==383||test_tic==385||test_tic==389||test_tic==416))I_Render3DScreenshot();
    if(getenv("DOOM_TEST_PLASMA_RECOVERY")&&(test_tic==414||test_tic==417||test_tic==421||test_tic==427||test_tic==433))I_Render3DScreenshot();
    if(getenv("DOOM_TEST_HAND_BOUNDS")&&((test_tic>=192&&test_tic<=202&&test_tic%2==0)||(test_tic>=204&&test_tic<=215)))I_Render3DScreenshot();
    if(getenv("DOOM_TEST_LOOKDOWN")&&test_tic==360)I_Render3DScreenshot();
#endif
    if (stage == 0 && test_tic >= 5)
    {
        if(test_episode==5){
            const mapinfo_t *info=G_MapInfo(5,1);
            if(gameepisode!=5||!info||strcmp(info->title,"Baphomet's Demesne")||strcmp(info->sky,"SKY5")||strcmp(info->music,"D_E5M1"))I_Error("SIGIL episode/metadata not applied");
        }
        test_start=gametic;
        test_notices();
        test_map_sound_origins();
        test_fluid_monsters();
        test_overlap_escape();
        test_pursuit_guards();
        test_aggressive_ai();
        test_advanced_tactics();
        test_projectile_pitch();
        test_fast_pistol();
        {
            int weapons=p_fluidweapons,monsters=p_fluidmonsters;
            p_fluidweapons=0;p_fluidmonsters=0;
            if(P_FluidWeaponsEnabled()||P_FluidEnabled())I_Error("Mod disable failed");
            p_fluidweapons=weapons;p_fluidmonsters=monsters;
#ifdef DOOM_RENDER3D
            extern void I_TestBundledModSettings(void);I_TestBundledModSettings();
            {extern void I_TestImpactOptions(void);I_TestImpactOptions();}

#endif
        }
        const char *requested=getenv("DOOM_TEST_WEAPON");
        if(requested) {
            test_weapon=atoi(requested);
            if(test_weapon<wp_fist||test_weapon>=NUMWEAPONS)I_Error("Invalid test weapon");
            test_ammo=weaponinfo[test_weapon].ammo;if(test_ammo==am_noammo)test_ammo=am_clip;
            players[0].weaponowned[test_weapon]=true;
            players[0].readyweapon=test_weapon;
            players[0].ammo[test_ammo]=50;
            P_SetupPsprites(&players[0]);
        }
#ifdef DOOM_RENDER3D
        if (!R_AcceleratedView || !R_MuzzleFlash) I_Error("Metal smoke test: GPU renderer or flash hook is inactive");
        I_Render3DToggleFlashlight(); /* Exercise beam/bounce during movement and firing. */
        I_Render3DLook(-80);
#endif
        renderer_flash=R_MuzzleFlash;R_MuzzleFlash=observe_flash;
        renderer_attack=R_WeaponAttack;R_WeaponAttack=observe_attack;
        key(ev_keydown, 'w'); stage = 1;
    }
    else if (stage == 1 && test_tic >= 20)
    {
#ifdef DOOM_RENDER3D
        if(getenv("DOOM_CAPTURE_FLASH_TEST")) I_Render3DScreenshot();
#endif
        key(ev_keyup, 'w');
        /* A map may put a weapon pickup in the walking fixture (SIGIL does).
         * Test the requested weapon, not the pickup's automatic switch. */
        players[0].readyweapon=test_weapon;players[0].pendingweapon=wp_nochange;
        P_SetPsprite(&players[0],ps_weapon,weaponinfo[test_weapon].readystate);
        key(ev_keydown, KEY_RCTRL); stage = 2;
    }
    else if (stage == 2 && test_tic >= 35)
    {
        mobj_t *player=players[0].mo;
        if (!P_UseUZMovement() && (abs(player->momx) > FRACUNIT/8 || abs(player->momy) > FRACUNIT/8))
            I_Error("Smoke test: player still drifts after releasing movement");
        mobj_t *shooter=P_SpawnMobj(player->x,player->y,ONFLOORZ,MT_POSSESSED);
        key(ev_keyup, KEY_RCTRL);
        shooter->target=player;
        A_PosAttack(shooter);
        P_RemoveMobj(shooter);
        if(enemy_flashes!=1) I_Error("Smoke test: enemy shot did not notify flash renderer");
#ifdef DOOM_RENDER3D
        {extern void I_TestCombatPresentation(void);I_TestCombatPresentation();}
        /* Keep the projectile capture clear of the fixture shot's red pain tint. */
        players[0].damagecount=0;
        if(!getenv("DOOM_TEST_FIDGETS"))P_SpawnPlayerMissile(player,MT_BRUISERSHOT);
#endif
        stage = 3;
    }
#ifdef DOOM_RENDER3D
    else if (stage == 3 && test_tic >= 45 && !projectile_capture)
    {
        if(getenv("DOOM_CAPTURE_FLASH_TEST")) I_Render3DScreenshot();
        projectile_capture=1;
    }
#endif
    else if (stage == 3 && test_tic >= 140)
    {
#ifdef DOOM_RENDER3D
        I_Render3DToggleFlashlight(); /* Exercise the reflected-color fade-out. */
        I_Render3DLook(80);
#endif
        saved_ammo = players[0].ammo[test_ammo];
        saved_health = players[0].health;
        if(!player_attacks)I_Error("Smoke test: weapon attack never ran (weapon %d, pending %d, health %d, state %d, paused %d, menu %d, tick %d)",players[0].readyweapon,players[0].pendingweapon,players[0].health,players[0].psprites[0].state?(int)(players[0].psprites[0].state-states):-1,paused,menuactive,test_tic);
        if (test_weapon!=wp_fist&&test_weapon!=wp_chainsaw&&player_flashes == 0) I_Error("Smoke test: player shots did not notify flash renderer");
        if(M_CheckParm("-fluid-sounds") && !I_NamedSoundStarts()) I_Error("Smoke test: named Fluid Weapons sounds never started");
        if (test_weapon!=wp_fist&&test_weapon!=wp_chainsaw&&saved_ammo >= 50) I_Error("Smoke test: firing did not consume ammo");
        {
            mobj_t *reference=P_SpawnMobj(players[0].mo->x,players[0].mo->y,
                players[0].mo->z+100*FRACUNIT,MT_TROOP);
            reference->flags=MF_NOGRAVITY|MF_NOCLIP;
            reference->health=123456;reference->tics=-1;
            reference->target=players[0].mo;reference->tracer=players[0].mo;
        }
        P_NoticeWeapon(&players[0],wp_bfg);notice_count(N_WEAPON);
        sectors[0].rocketdoorhits=2;
        G_SaveGame(5, "Native smoke test"); stage = 4;
    }
    else if (stage == 4 && test_tic >= 150 && gameaction == ga_nothing)
    {
        FILE *file = fopen("doomsav5.dsg", "rb");
        if (!file) I_Error("Smoke test: save file was not created");
        fclose(file);
        players[0].ammo[test_ammo] = 1;
        sectors[0].rocketdoorhits=0;
        G_LoadGame("doomsav5.dsg"); stage = 5;
    }
    else if (stage == 5 && test_tic >= 165 && gameaction == ga_nothing)
    {
        if (players[0].ammo[test_ammo] != saved_ammo || players[0].health != saved_health)
            I_Error("Smoke test: saved player state was not restored");
        if(sectors[0].rocketdoorhits!=2)I_Error("Door damage lost through actual save/load");
        if (!players[0].mo) I_Error("Smoke test: player object was not restored");
        {
            thinker_t *thinker;int found=0;
            for(thinker=thinkercap.next;thinker!=&thinkercap;thinker=thinker->next)
                if(thinker->function.acp1==(actionf_p1)P_MobjThinker) {
                    mobj_t *actor=(mobj_t*)thinker;
                    if(actor->health==123456){
                        if(actor->target!=players[0].mo||actor->tracer!=players[0].mo)
                            I_Error("Smoke test: saved actor target/tracer references were lost");
                        ++found;P_RemoveMobj(actor);
                    }
                }
            if(found!=1)I_Error("Smoke test: actor reference fixture missing");
        }
        P_NoticeWeapon(&players[0],wp_bfg);
        if(notice_count(N_WEAPON))I_Error("Weapon discovery replayed after actual save/load");
        puts("Native gameplay, firing, save, and load passed.");
#ifdef DOOM_RENDER3D
        if(getenv("DOOM_TEST_FIDGETS")){
            R_SetViewSize(11,0);
            players[0].ammo[test_ammo]=200;
            players[0].readyweapon=test_weapon;players[0].pendingweapon=wp_nochange;
            P_SetupPsprites(&players[0]);stage=7;return;
        }
        I_Render3DToggleFlashlight(); /* Keep it on through load and level reset. */
        serial_before_change = r_levelserial;
        G_DeferedInitNew(sk_medium, test_episode, 2);
        stage = 6;
#else
        I_Quit();
#endif
    }
#ifdef DOOM_RENDER3D
    else if(stage==7&&test_tic>=190){
        if(getenv("DOOM_TEST_LOOKDOWN"))I_Render3DLook(10000);
        event_t aux={ev_mouse,2,0,0};players[0].ammo[test_ammo]=200;D_PostEvent(&aux);stage=8;
    }
    else if(stage==8&&test_tic>=235){
        event_t aux={ev_mouse,0,0,0};D_PostEvent(&aux);I_Render3DScreenshot();stage=9;
    }
    else if(stage==9&&test_tic>=260){key(ev_keydown,'r');stage=10;}
    else if(stage==10&&test_tic>=300){key(ev_keyup,'r');I_Render3DScreenshot();stage=11;}
    else if(stage==11&&test_tic>=380){
        if(players[0].ammo[test_ammo]!=200)I_Error("Fidget controls consumed ammo");
        key(ev_keydown,KEY_RCTRL);stage=12;
    }
    else if(stage==12&&test_tic>=410){key(ev_keyup,KEY_RCTRL);stage=13;}
    else if(stage==13&&test_tic>=450){
        if(test_weapon!=wp_fist&&test_weapon!=wp_chainsaw&&players[0].ammo[test_ammo]>=200)I_Error("Fidget blocked subsequent firing");
        puts("Right-click and R fidgets preserved ammo; subsequent firing passed.");
        if(getenv("DOOM_TEST_EXIT_NOTICES")){
            P_NoticeLevel();players[0].killcount=totalkills;players[0].secretcount=totalsecret;players[0].itemcount=totalitems;
            G_SecretExitLevel();stage=14;return;
        }
        serial_before_change=r_levelserial;G_DeferedInitNew(sk_medium,1,2);stage=6;
    }
    else if (stage == 6 && test_tic >= 190 && gameaction == ga_nothing)
    {
        if (gamemap != 2 || r_levelserial <= serial_before_change || !players[0].mo)
            I_Error("Metal smoke test: level change failed");
        puts("Metal level change passed.");
        I_Quit();
    }
#endif
}
#ifndef DOOM_TEST_REAL_STARTUP
int main(int argc, char **argv)
{
    if (argc < 2 || argc > 40) return 1;
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    FILE *config = fopen("smoke.cfg", "w");
    if (!config) return 1;
    fputs("key_up 119\nkey_fire 157\nsnd_channels 16\n", config);
    if(getenv("DOOM_TEST_FIDGETS"))fputs("screenblocks 11\n",config);
    fclose(config);
    char *args[64] = {"smoke", "-iwad", argv[1], "-warp", "1", "1",
        "-config", "smoke.cfg", "-nomonsters", "-nomusic"};
    int count=10;for(int i=2;i<argc;++i)args[count++]=argv[i];
    if(getenv("DOOM_TEST_WITH_MONSTERS"))args[8]="-devparm";
    myargc=count; myargv = args;
    if(getenv("DOOM_TEST_MUSIC"))args[9]="-opl";
    if(getenv("DOOM_TEST_EPISODE")){test_episode=atoi(getenv("DOOM_TEST_EPISODE"));args[4]=getenv("DOOM_TEST_EPISODE");}
    if(getenv("DOOM_TEST_MAP"))args[5]=getenv("DOOM_TEST_MAP");
    if(getenv("DOOM_TEST_MENU")){args[3]="-nomusic";args[4]="-nomusic";args[5]="-nomusic";}
    D_DoomMain();
    return 0;
}
#endif
