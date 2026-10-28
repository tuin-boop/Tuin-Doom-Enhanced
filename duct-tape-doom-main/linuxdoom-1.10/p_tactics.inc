#include "m_arcade.h"
/* Bounded, opt-in species tactics. All actual movement goes through P_Move. */
int p_advancedtactics;
typedef struct {mobj_t *actor;int decision,until,dir,side,shots,seen,burst,burstAfter,press;fixed_t x,y,z,lockx,locky,lockz;int lockUntil,chargeRetry;boolean visible,hold;} tactic_t;
static tactic_t tactics[1024];static unsigned tacticsSerial;static int tacticsBudget=-1;
boolean P_TacticsEnabled(void){return !arcade_active&&p_advancedtactics&&!demoplayback&&!demorecording&&!netgame;}
void P_SetAdvancedTactics(int enabled){if(enabled!=p_advancedtactics){memset(tactics,0,sizeof(tactics));tacticsBudget=-1;}p_advancedtactics=enabled;}
static tactic_t *P_TacticData(mobj_t *actor){
    if(tacticsSerial!=r_levelserial){memset(tactics,0,sizeof(tactics));tacticsSerial=r_levelserial;tacticsBudget=-1;}
    tactic_t *t=&tactics[((uintptr_t)actor>>4)%1024];
    if(t->actor!=actor){memset(t,0,sizeof(*t));t->actor=actor;t->side=(((uintptr_t)actor>>5)&1)?1:-1;t->seen=-1000;}
    return t;
}
void P_TacticsRemove(mobj_t *actor){if(!p_advancedtactics)return;tactic_t *t=&tactics[((uintptr_t)actor>>4)%1024];if(t->actor==actor)t->actor=NULL;}
static boolean P_TacticValid(mobj_t *actor){return P_TacticsEnabled()&&actor&&((actor->flags&MF_COUNTKILL)||actor->type==MT_SKULL)&&actor->health>0&&actor->target&&actor->target->health>0&&!P_Unseen(actor->target);}
// A failed charge must yield to movement even with Aggressive AI enabled.
boolean P_TacticsAttackBlocked(mobj_t *actor){
    return P_TacticValid(actor)&&actor->type==MT_SKULL&&leveltime<P_TacticData(actor)->chargeRetry;
}
void P_TacticsChargeBlocked(mobj_t *actor){
    if(!P_TacticValid(actor)||actor->type!=MT_SKULL)return;
    tactic_t *t=P_TacticData(actor);
    t->chargeRetry=leveltime+2*TICRATE;
    t->dir=((int)(R_PointToAngle2(actor->x,actor->y,actor->target->x,actor->target->y)>>29)+t->side*2+8)&7;
    t->until=t->chargeRetry;
}
static boolean P_TacticClearShot(mobj_t *actor){
    if(!P_CheckSight(actor,actor->target))return false;
    P_AimLineAttack(actor,R_PointToAngle2(actor->x,actor->y,actor->target->x,actor->target->y),MISSILERANGE);
    return linetarget==actor->target;
}
static fixed_t P_TacticsHitscanSlope(mobj_t *actor,fixed_t slope){
    mobj_t aim;if(actor->type!=MT_SPIDER||!P_TacticsAim(actor,&aim))return slope;
    fixed_t distance=P_AproxDistance(aim.x-actor->x,aim.y-actor->y);if(distance<FRACUNIT)distance=FRACUNIT;
    return FixedDiv(aim.z+actor->target->height/2-(actor->z+actor->height/2+8*FRACUNIT),distance);
}
static int P_TacticDirection(mobj_t *actor,fixed_t x,fixed_t y){return (int)((R_PointToAngle2(actor->x,actor->y,x,y)+ANG45/2)>>29)&7;}
static void P_TacticObserve(mobj_t *actor,tactic_t *t){
    t->visible=P_CheckSight(actor,actor->target);
    if(t->visible){t->x=actor->target->x;t->y=actor->target->y;t->z=actor->target->z;t->seen=leveltime;}
}
static boolean P_TacticStep(mobj_t *actor,int dir){int old=actor->movedir;actor->movedir=(dir+8)&7;if(P_Move(actor)){actor->movecount=0;return true;}actor->movedir=old;return false;}
static mobj_t *tacticNeighborActor,*tacticCorpse;static int tacticNeighbors,tacticVisited;static fixed_t tacticAwayX,tacticAwayY,tacticCorpseDistance;
static boolean PIT_TacticNeighbor(mobj_t *other){
    if(++tacticVisited>48)return false;
    mobj_t *actor=tacticNeighborActor;if(other==actor)return true;
    fixed_t distance=P_AproxDistance(other->x-actor->x,other->y-actor->y);
    if(other->health>0&&(other->flags&MF_COUNTKILL)&&other->target==actor->target&&distance<actor->radius+other->radius+32*FRACUNIT){
        ++tacticNeighbors;tacticAwayX+=(actor->x-other->x)/8;tacticAwayY+=(actor->y-other->y)/8;
    }
    if(actor->type==MT_VILE&&(other->flags&MF_CORPSE)&&other->tics==-1&&other->info->raisestate!=S_NULL&&distance<tacticCorpseDistance&&P_CheckSight(actor,other)){
        // Only pursue corpses which have room to stand up again.
        fixed_t height=other->height;other->height=other->info->height;
        boolean fits=P_CheckPosition(other,other->x,other->y);other->height=height;
        if(fits){tacticCorpse=other;tacticCorpseDistance=distance;}
    }
    return true;
}
static void P_TacticNeighbors(mobj_t *actor){
    tacticNeighborActor=actor;tacticNeighbors=tacticVisited=0;tacticAwayX=tacticAwayY=0;tacticCorpse=NULL;tacticCorpseDistance=192*FRACUNIT;
    int x=(actor->x-bmaporgx)>>MAPBLOCKSHIFT,y=(actor->y-bmaporgy)>>MAPBLOCKSHIFT;
    for(int by=y-1;by<=y+1&&tacticVisited<48;++by)for(int bx=x-1;bx<=x+1&&tacticVisited<48;++bx)P_BlockThingsIterator(bx,by,PIT_TacticNeighbor);
}
boolean P_TacticsMove(mobj_t *actor){
    if(!P_TacticValid(actor)||(actor->flags&MF_SKULLFLY))return false;tactic_t *t=P_TacticData(actor);
    if(leveltime<t->until){if(P_TacticStep(actor,t->dir))return true;t->until=0;t->side=-t->side;}
    if(leveltime<t->decision||tacticsBudget==leveltime)return false;
    tacticsBudget=leveltime;t->decision=leveltime+7;P_TacticObserve(actor,t);P_TacticNeighbors(actor);
    int toward=P_TacticDirection(actor,actor->target->x,actor->target->y),dir=-1;
    fixed_t distance=P_AproxDistance(actor->x-actor->target->x,actor->y-actor->target->y);
    if(tacticNeighbors&&distance>80*FRACUNIT){dir=P_TacticDirection(actor,actor->x+tacticAwayX,actor->y+tacticAwayY);if(!tacticAwayX&&!tacticAwayY)dir=(toward+t->side*2+8)&7;}
    if(actor->type==MT_SERGEANT||actor->type==MT_SHADOWS){if(t->visible&&distance>96*FRACUNIT&&distance<512*FRACUNIT)dir=(toward+t->side+8)&7;}
    if(actor->type==MT_UNDEAD&&t->visible&&distance>80*FRACUNIT&&distance<224*FRACUNIT)dir=(toward+4)&7;
    if(actor->type==MT_SKULL&&leveltime<t->chargeRetry)dir=(toward+t->side*2+8)&7;
    if(actor->type==MT_PAIN){
        fixed_t z;boolean open=P_PursuitEdge(actor,actor->x,actor->y,actor->z,actor->x+FixedMul(80*FRACUNIT,xspeed[toward]),actor->y+FixedMul(80*FRACUNIT,yspeed[toward]),&z);
        if(distance<256*FRACUNIT||!open)dir=(toward+(distance<256*FRACUNIT?4:t->side*2)+8)&7;
    }
    if((actor->type==MT_BABY||actor->type==MT_SPIDER)&&!P_TacticClearShot(actor))dir=(toward+t->side*2+8)&7;
    if(actor->type==MT_VILE){
        if(tacticCorpse)dir=P_TacticDirection(actor,tacticCorpse->x,tacticCorpse->y);
        else if(t->visible&&leveltime%70<14){
            // Probe nearby cover without moving or relinking the live actor.
            for(int d=0;d<8;++d){fixed_t x=actor->x+FixedMul(32*FRACUNIT,xspeed[d]),y=actor->y+FixedMul(32*FRACUNIT,yspeed[d]),z;
                if(!P_PursuitEdge(actor,actor->x,actor->y,actor->z,x,y,&z))continue;
                mobj_t probe=*actor;probe.x=x;probe.y=y;probe.z=z;probe.subsector=R_PointInSubsector(x,y);
                if(!P_CheckSight(&probe,actor->target)){dir=d;break;}
            }
        }
    }
    if(dir<0)return false;t->dir=dir;t->until=leveltime+7;
    if(P_TacticStep(actor,dir))return true;
    if(P_TacticStep(actor,toward-t->side*2)){t->dir=(toward-t->side*2+8)&7;return true;}
    t->until=0;return false;
}
boolean P_TacticsHold(mobj_t *actor){
    if(!P_TacticValid(actor)||actor->type!=MT_BRUISER)return false;
    tactic_t *t=P_TacticData(actor);if(!t->visible)return false;
    sector_t *sector=actor->subsector->sector;
    for(int i=0;i<sector->linecount&&i<64;++i){line_t *line=sector->lines[i];if(!line->backsector)continue;
        if(P_AproxDistance(line->dx,line->dy)<=192*FRACUNIT&&P_AproxDistance(actor->x-(line->v1->x/2+line->v2->x/2),actor->y-(line->v1->y/2+line->v2->y/2))<96*FRACUNIT){actor->movedir=DI_NODIR;return true;}}
    return false;
}
boolean P_TacticsCanFire(mobj_t *actor){
    if(!P_TacticValid(actor))return false;tactic_t *t=P_TacticData(actor);
    if(leveltime<t->until)return false;
    if(actor->type==MT_SPIDER){
        boolean visible=P_CheckSight(actor,actor->target);
        if(visible){P_TacticObserve(actor,t);return P_TacticClearShot(actor)&&t->shots%24<18;}
        return leveltime-t->seen<24&&t->shots%24<18;
    }
    if(actor->type==MT_BABY)return P_TacticClearShot(actor)&&t->shots%10<7;
    if(actor->type==MT_BRUISER&&P_TacticClearShot(actor)){
        if(leveltime>=t->burstAfter){t->burst=3;t->burstAfter=leveltime+8*TICRATE;}
        return t->burst>0;
    }
    return false;
}
boolean P_TacticsAim(mobj_t *actor,mobj_t *aim){
    if(!P_TacticValid(actor))return false;tactic_t *t=P_TacticData(actor);
    if(actor->type==MT_FATSO&&(t->shots/6)%2==0){
        if(t->shots%6==0&&leveltime>=t->lockUntil){t->lockx=actor->target->x;t->locky=actor->target->y;t->lockz=actor->target->z;t->lockUntil=leveltime+3*TICRATE;}
        memset(aim,0,sizeof(*aim));aim->x=t->lockx;aim->y=t->locky;aim->z=t->lockz;return true;
    }
    if(actor->type==MT_CYBORG&&t->shots%3==1){
        *aim=*actor->target;int lead=P_AproxDistance(actor->x-aim->x,actor->y-aim->y)/(20*FRACUNIT);if(lead>12)lead=12;
        // Modest, deliberately imperfect prediction; never change missile speed.
        fixed_t dx=aim->momx*lead*3/4,dy=aim->momy*lead*3/4;
        if(dx>96*FRACUNIT)dx=96*FRACUNIT;if(dx< -96*FRACUNIT)dx=-96*FRACUNIT;
        if(dy>96*FRACUNIT)dy=96*FRACUNIT;if(dy< -96*FRACUNIT)dy=-96*FRACUNIT;
        aim->x+=dx;aim->y+=dy;return true;
    }
    if(actor->type==MT_SPIDER&&!P_CheckSight(actor,actor->target)&&leveltime-t->seen<24){memset(aim,0,sizeof(*aim));aim->x=t->x;aim->y=t->y;aim->z=t->z;return true;}
    return false;
}
angle_t P_TacticsFatSpread(mobj_t *actor){if(!P_TacticsEnabled())return ANG90/8;tactic_t *t=P_TacticData(actor);return (t->shots/6)%2?ANG90/5:ANG90/16;}
void P_TacticsFired(mobj_t *actor){
    if(!P_TacticValid(actor))return;tactic_t *t=P_TacticData(actor);++t->shots;
    int toward=P_TacticDirection(actor,actor->target->x,actor->target->y);
    if(actor->type==MT_BRUISER&&t->burst>0)--t->burst;
    if(actor->type==MT_KNIGHT)t->press=leveltime+12;
    if(actor->type==MT_TROOP||actor->type==MT_HEAD||(actor->type==MT_BABY&&t->shots%10==7)||(actor->type==MT_SPIDER&&t->shots%24==18)){
        t->dir=(toward+t->side*2+8)&7;t->side=-t->side;t->until=leveltime+28;
        if(actor->type==MT_BABY)t->shots=0;if(actor->type==MT_SPIDER)t->shots=0;
    }
}
void P_TacticsTick(mobj_t *actor){
    if(!P_TacticValid(actor))return;tactic_t *t=P_TacticData(actor);
    if(actor->type==MT_KNIGHT&&leveltime<t->press&&leveltime%4==0)P_TacticStep(actor,P_TacticDirection(actor,actor->target->x,actor->target->y));
    if(actor->type==MT_HEAD&&leveltime%2==0){
        if(P_CheckPosition(actor,actor->x,actor->y)){
            extern fixed_t tmceilingz,tmfloorz;fixed_t lo=tmfloorz+8*FRACUNIT,hi=tmceilingz-actor->height-8*FRACUNIT;
            if(hi>lo){fixed_t wanted=actor->target->z+(((leveltime/35+(int)((uintptr_t)actor>>5))&1)?48:8)*FRACUNIT;
                if(wanted<lo)wanted=lo;if(wanted>hi)wanted=hi;
                fixed_t delta=wanted-actor->z;if(delta>FRACUNIT)delta=FRACUNIT;if(delta< -FRACUNIT)delta=-FRACUNIT;actor->z+=delta;}
        }
    }
}
