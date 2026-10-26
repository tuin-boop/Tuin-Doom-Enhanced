/* Native adaptation of the supplied FluidMonsters FluidWalk mixin.
 * Transient substeps stay outside mobj_t so existing saves remain readable.
 * AI actions run once at state entry; only collision-checked walking repeats. */
#include <stdint.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "doomstat.h"
#include "m_argv.h"
#include "p_local.h"
#include "p_fluid.h"
#include "z_zone.h"
void A_Chase(mobj_t*);
void A_VileChase(mobj_t*);
void A_Hoof(mobj_t*);
void A_Metal(mobj_t*);
void A_BabyMetal(mobj_t*);
boolean P_Move(mobj_t*);
typedef struct walk_s {
    struct walk_s *next;
    mobj_t *actor;
    state_t *state;
    int remaining,duration,direction;
    fixed_t x,y,z;
} walk_t;
static walk_t *walks[1024];
static mobj_t *stepping;
static int divisor=1;
extern fixed_t tmdropoffz;
static unsigned bucket(mobj_t *actor){return (unsigned)((uintptr_t)actor>>4)&1023;}
int p_fluidweapons=1,p_fluidmonsters=1;
int P_FluidWeaponsEnabled(void){return p_fluidweapons&&M_CheckParm("-fluidweapons");}
int P_FluidEnabled(void){return p_fluidmonsters&&M_CheckParm("-fluidmonsters")&&!demoplayback&&!demorecording&&!netgame;}
static int supported(mobj_t *actor){
    switch(actor->type){
    case MT_POSSESSED:case MT_SHOTGUY:case MT_CHAINGUY:case MT_WOLFSS:
    case MT_TROOP:case MT_SERGEANT:case MT_SHADOWS:case MT_BRUISER:case MT_KNIGHT:
    case MT_HEAD:case MT_PAIN:case MT_SKULL:
    case MT_UNDEAD:case MT_FATSO:case MT_VILE:case MT_BABY:case MT_CYBORG:case MT_SPIDER:return 1;
    default:return 0;
    }
}
void P_FluidRemove(mobj_t *actor){
    walk_t **link=&walks[bucket(actor)];
    while(*link){walk_t *entry=*link;if(entry->actor==actor){*link=entry->next;Z_Free(entry);return;}link=&entry->next;}
}
void P_FluidReset(void){
    for(int i=0;i<1024;++i){walk_t *entry=walks[i];while(entry){walk_t *next=entry->next;Z_Free(entry);entry=next;}walks[i]=NULL;}
    stepping=NULL;divisor=1;
}
fixed_t P_FluidMoveSpeed(mobj_t *actor){return (fixed_t)((int64_t)actor->info->speed*FRACUNIT/(stepping==actor?divisor:1));}
fixed_t P_FluidFloatSpeed(mobj_t *actor){return FLOATSPEED/(stepping==actor?divisor:1);}
int P_FluidChase(mobj_t *actor,actionf_p1 action){
    if(!P_FluidEnabled()||!supported(actor)||(actor->flags&MF_SKULLFLY))return 0;
    if(action!=(actionf_p1)A_Chase&&action!=(actionf_p1)A_VileChase&&action!=(actionf_p1)A_Hoof&&action!=(actionf_p1)A_Metal&&action!=(actionf_p1)A_BabyMetal)return 0;
    P_FluidRemove(actor);
    state_t *state=actor->state;int duration=actor->tics>0?actor->tics:1;
    fixed_t x=actor->x,y=actor->y,z=actor->z;
    /* Some vanilla maps spawn actors slightly overlapping a blocking line.
       The ordinary chase step can leave that overlap, but every fractional
       step can still intersect it. Keep vanilla's full, collision-checked
       action for that state; do not add substeps after consuming the step.
       This also recovers affected actors in existing saves. */
    int overlap=!P_CheckPosition(actor,x,y);
    /* Position checks accept intersecting sector boundaries; TryMove also
       checks their vertical opening and step height. A short substep may
       remain inside a raised floor even though the normal step clears it. */
    if(!(actor->flags&MF_NOCLIP)&&
       (tmfloorz-actor->z>24*FRACUNIT || tmceilingz-actor->z<actor->height ||
        tmceilingz-tmfloorz<actor->height ||
        (!(actor->flags&(MF_DROPOFF|MF_FLOAT))&&tmfloorz-tmdropoffz>24*FRACUNIT)))overlap=1;
    mobj_t *previous=stepping;int oldDivisor=divisor;stepping=actor;divisor=overlap?1:duration;
    action(actor);stepping=previous;divisor=oldDivisor;
    int64_t dx=(int64_t)actor->x-x,dy=(int64_t)actor->y-y;
    double distance=hypot(hypot((double)dx/FRACUNIT,(double)dy/FRACUNIT),((double)actor->z-z)/FRACUNIT);
    if(!overlap&&actor->state==state&&actor->health>0&&duration>1&&actor->movedir<8&&actor->movedir>=0&&distance>.001&&distance<=abs(actor->info->speed)*2+1){
        walk_t *entry=Z_Malloc(sizeof(*entry),PU_LEVEL,NULL);memset(entry,0,sizeof(*entry));
        unsigned slot=bucket(actor);entry->next=walks[slot];walks[slot]=entry;
        entry->actor=actor;entry->state=state;entry->duration=duration;entry->remaining=duration-1;entry->direction=actor->movedir;
        entry->x=actor->x;entry->y=actor->y;entry->z=actor->z;
    }
    return 1;
}
void P_FluidTick(mobj_t *actor){
    walk_t *entry=walks[bucket(actor)];while(entry&&entry->actor!=actor)entry=entry->next;
    if(!entry)return;
    double dx=((double)actor->x-entry->x)/FRACUNIT;
    double dy=((double)actor->y-entry->y)/FRACUNIT;
    double dz=((double)actor->z-entry->z)/FRACUNIT;
    if(!P_FluidEnabled()||(actor->flags&MF_SKULLFLY)||actor->state!=entry->state||actor->health<=0||actor->movedir!=entry->direction||
       dx*dx+dy*dy+dz*dz>64.0*64.0){P_FluidRemove(actor);return;}
    mobj_t *previous=stepping;int oldDivisor=divisor;stepping=actor;divisor=entry->duration;
    int moved=P_Move(actor);stepping=previous;divisor=oldDivisor;
    if(!moved||--entry->remaining<=0){P_FluidRemove(actor);return;}
    entry->x=actor->x;entry->y=actor->y;entry->z=actor->z;
}

