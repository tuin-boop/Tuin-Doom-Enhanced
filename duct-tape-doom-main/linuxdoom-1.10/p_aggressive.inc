#include "m_arcade.h"
/* Optional attack tactics; transient state deliberately stays out of saves. */
int p_aggressiveai;
extern int p_fluidfastpistol;
typedef struct {mobj_t *actor;int moveUntil,staggerUntil,side,shots,barrages,barrageUntil,flankAfter;boolean hit;fixed_t aimx,aimy,aimz;} aggressive_t;
typedef struct {mobj_t *missile,*source,*target;boolean hit;} aggressive_shot_t;
static aggressive_t aggressiveActors[512];
static aggressive_shot_t aggressiveShots[1024];
static unsigned aggressiveSerial;
boolean P_Move(mobj_t *actor);
void P_SetAggressiveAI(int enabled){
    if(p_aggressiveai!=enabled){memset(aggressiveActors,0,sizeof(aggressiveActors));memset(aggressiveShots,0,sizeof(aggressiveShots));aggressiveSerial=r_levelserial;}
    p_aggressiveai=enabled;
}
boolean P_AggressiveEnabled(void){return !arcade_active&&p_aggressiveai&&!demoplayback&&!demorecording&&!netgame;}
static void P_AggressiveReset(void){if(aggressiveSerial!=r_levelserial){memset(aggressiveActors,0,sizeof(aggressiveActors));memset(aggressiveShots,0,sizeof(aggressiveShots));aggressiveSerial=r_levelserial;}}
static aggressive_t *P_AggressiveData(mobj_t *actor){
    P_AggressiveReset();
    unsigned index=((uintptr_t)actor>>4)%512;
    aggressive_t *data=&aggressiveActors[index];
    if(data->actor!=actor){memset(data,0,sizeof(*data));data->actor=actor;data->side=(index&1)?1:-1;}
    return data;
}
static void P_AggressiveMiss(mobj_t *actor){
    if(!actor||actor->health<=0||!(actor->flags&MF_COUNTKILL))return;
    aggressive_t *data=P_AggressiveData(actor);
    if(leveltime<data->barrageUntil)return;
    if(data->moveUntil>=0&&leveltime>=data->moveUntil){data->side=-data->side;data->moveUntil=-14;}
}
boolean P_AggressiveBarrageAim(mobj_t *actor,mobj_t *aim){
    if(!P_AggressiveEnabled()||!actor||actor->type!=MT_FATSO)return false;
    aggressive_t *data=P_AggressiveData(actor);
    if(leveltime>=data->barrageUntil||!actor->target||actor->target->health<=0||P_Unseen(actor->target))return false;
    memset(aim,0,sizeof(*aim));aim->x=data->aimx;aim->y=data->aimy;aim->z=data->aimz;return true;
}
static boolean P_AggressiveCanFire(mobj_t *actor){
    if(!actor->target||actor->target->health<=0||P_Unseen(actor->target))return false;
    aggressive_t *data=P_AggressiveData(actor);
    if(data->moveUntil<0||leveltime<data->moveUntil||leveltime<data->staggerUntil)return false;
    mobj_t aim;if(P_AggressiveBarrageAim(actor,&aim))return true;
    if(!P_CheckSight(actor,actor->target))return false;
    if(actor->type==MT_VILE&&P_AproxDistance(actor->x-actor->target->x,actor->y-actor->target->y)>896*FRACUNIT)return false;
    // Check the actual firing line, not just visibility through a window.
    angle_t angle=R_PointToAngle2(actor->x,actor->y,actor->target->x,actor->target->y);
    P_AimLineAttack(actor,angle,MISSILERANGE);
    return linetarget==actor->target;
}
static void P_AggressiveStartAttack(mobj_t *actor){
    if(!P_AggressiveEnabled())return;
    actor->flags&=~MF_JUSTATTACKED;
    aggressive_t *data=P_AggressiveData(actor);
    if(actor->type==MT_FATSO&&leveltime>=data->barrageUntil&&++data->barrages%3==0){
        data->aimx=actor->target->x;data->aimy=actor->target->y;data->aimz=actor->target->z;
        data->barrageUntil=leveltime+160;data->moveUntil=0;
    }
}
static boolean P_AggressiveManeuver(mobj_t *actor){
    if(!P_AggressiveEnabled()||!actor->target)return false;
    aggressive_t *data=P_AggressiveData(actor);
    if(leveltime<data->staggerUntil){actor->movedir=DI_NODIR;return true;}
    if(data->moveUntil<0)data->moveUntil=leveltime-data->moveUntil;
    boolean demon=actor->type==MT_SERGEANT||actor->type==MT_SHADOWS;
    if(demon&&leveltime>=data->flankAfter&&P_CheckSight(actor,actor->target)
       &&P_AproxDistance(actor->x-actor->target->x,actor->y-actor->target->y)>96*FRACUNIT){
        data->moveUntil=leveltime+14;data->flankAfter=leveltime+3*TICRATE;data->side=-data->side;
    }
    if(leveltime>=data->moveUntil)return false;
    int toward=(int)((R_PointToAngle2(actor->x,actor->y,actor->target->x,actor->target->y)+ANG45/2)>>29)&7;
    int olddir=actor->movedir;
    int dirs[4]={(toward+data->side*(demon?1:2)+8)&7,(toward-data->side*2+8)&7,(toward+4)&7,toward};
    for(int i=0;i<4;++i){actor->movedir=dirs[i];if(P_Move(actor)){actor->movecount=0;return true;}}
    actor->movedir=olddir;data->moveUntil=0;return false;
}
static void P_AggressiveShotBegin(mobj_t *actor){if(P_AggressiveEnabled())P_AggressiveData(actor)->hit=false;}
static void P_AggressiveShotEnd(mobj_t *actor){
    if(!P_AggressiveEnabled())return;
    aggressive_t *data=P_AggressiveData(actor);
    if(!data->hit)P_AggressiveMiss(actor);
    // Successful hits keep the firing lane; reposition only after a miss.
}
void P_AggressiveMissile(mobj_t *missile,mobj_t *source){
    if(!P_AggressiveEnabled()||!(source->flags&MF_COUNTKILL))return;
    P_AggressiveReset();aggressive_shot_t *shot=&aggressiveShots[((uintptr_t)missile>>4)%1024];
    shot->missile=missile;shot->source=source;shot->target=source->target;shot->hit=false;
}
void P_AggressiveImpact(mobj_t *missile){
    if(!P_AggressiveEnabled())return;P_AggressiveReset();
    aggressive_shot_t *shot=&aggressiveShots[((uintptr_t)missile>>4)%1024];
    if(shot->missile!=missile)return;
    if(!shot->hit)P_AggressiveMiss(shot->source);

    shot->missile=NULL;
}
void P_AggressiveRemove(mobj_t *actor){
    if(!p_aggressiveai)return;
    P_AggressiveReset();
    aggressive_t *data=&aggressiveActors[((uintptr_t)actor>>4)%512];if(data->actor==actor)data->actor=NULL;
    aggressive_shot_t *shot=&aggressiveShots[((uintptr_t)actor>>4)%1024];if(shot->missile==actor)shot->missile=NULL;
    // Clear references before the allocator can reuse a dead shooter's address.
    if(actor->flags&MF_COUNTKILL)for(int i=0;i<1024;++i)if(aggressiveShots[i].source==actor)aggressiveShots[i].missile=NULL;
}
void P_AggressiveContact(mobj_t *victim,mobj_t *inflictor,mobj_t *source){
    if(!P_AggressiveEnabled())return;
    P_AggressiveReset();
    if(source&&(source->flags&MF_COUNTKILL)&&source->target==victim)P_AggressiveData(source)->hit=true;
    if(inflictor){aggressive_shot_t *shot=&aggressiveShots[((uintptr_t)inflictor>>4)%1024];if(shot->missile==inflictor&&shot->target==victim)shot->hit=true;}
}
void P_AggressiveDamage(mobj_t *victim,mobj_t *inflictor,mobj_t *source,int damage){
    if(!P_AggressiveEnabled())return;
    if((victim->flags&MF_COUNTKILL)&&damage>=40&&victim->health>damage){
        aggressive_t *data=P_AggressiveData(victim);data->staggerUntil=leveltime+7;
        if(victim->info->painstate!=S_NULL)P_SetMobjState(victim,victim->info->painstate);
    }
}
int P_AggressiveStateTics(mobj_t *actor,statenum_t state,int tics){
    if(!P_AggressiveEnabled()||!(actor->flags&MF_COUNTKILL))return tics;
    int cycle=14;if(P_FluidWeaponsEnabled()&&p_fluidfastpistol)cycle=10;
    if(state==S_POSS_ATK1||state==S_SPOS_ATK1)return 1;
    if(state==S_POSS_ATK2||state==S_SPOS_ATK2)return 3;
    if(state==S_POSS_ATK3)return cycle-4;
    if(state==S_SPOS_ATK3)return 33; // Player shotgun's 37-tic pump cycle.
    return tics;
}
