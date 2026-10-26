/* Ragdoll test runs (tests/ragdoll_arena.py): -ragdolltest <weapon>[:<aim>]
   kills every monster on the map in turn, a tic apart from tic 3 (before
   any has woken and turned), with the game's own hitscans fired from the
   player: pistol (one 10-damage bullet), shotgun (three 10-damage pellets)
   or ssg (seven), aimed at the chest, head, legs, or the chest's left or
   right edge as the shooter sees it; a third field, :n, keeps only the n-th
   monster of the row (west to east, from 0) and removes the rest, and a fourth,
   :t, kills it at tic t instead of 3 (the fall's own random draw follows the tic). Each victim's health is set so the last
   pellet kills it, so the corpse takes that weapon's push. -ragdollview x,y,
   angle then moves the player (the eye; the shots are already fired) to
   watch from there; -ragdollview side, 220 units to the right of the shot's
   line at the one victim, looking across its flight; front, 150 back along it. Pair with -rendercheck's series (scene.cpp).
   -ragdollloop runs it for show: once every body has come to rest (or 12 seconds on) the corpses
   are cleared, the row stands up again where it was placed and is shot again, until the game is
   quit; the player can't die and may walk about to watch. Aim mix gives each victim another aim
   each round (chest, legs, left, right, head). Weapon rocket kills each victim in turn, 20 tics
   apart, with a rocket's explosion beside it (the game's own blast, push and sound): with aim mix
   anywhere round it at any height, so bodies fly every way; otherwise chest and legs from the
   shooter's side (thrown away), left and right from that side (across the view), head from behind
   (toward the eye). Only the one in turn can be hurt, so each blast throws one body; it gibs as in
   the game, drawn as a body thrown with Flying enemies on. Weapon chaingun fires a burst of three bullets 4 tics apart (the gun's pace), the last
   one killing. A test switch only: it changes the game. */
#include "internal.h"
extern "C" {
void P_LineAttack(mobj_t *t1,angle_t angle,fixed_t distance,fixed_t slope,int damage);
fixed_t P_AproxDistance(fixed_t dx,fixed_t dy);
boolean P_TeleportMove(mobj_t *thing,fixed_t x,fixed_t y);
void P_RemoveMobj(mobj_t *th);
mobj_t *P_SpawnMobj(fixed_t x,fixed_t y,fixed_t z,mobjtype_t type);
void P_ExplodeMissile(mobj_t *mo);
}

namespace doom3d {
// -ragdollloop: the tic the current round began (its kills from first tics on), and its number
static int roundAt=0,round=0,settledFor=0;
// The row stood up again where it was placed, its corpses and their bodies gone.
static void standUp(std::vector<mobj_t*> &victims) {
    std::vector<mobj_t*> drops;         // the clips and guns the last round dropped
    for(int i=0;i<numsectors;++i)for(mobj_t *mo=sectors[i].thinglist;mo;mo=mo->snext)if(mo->flags&MF_DROPPED)drops.push_back(mo);
    for(mobj_t *drop:drops)P_RemoveMobj(drop);
    for(mobj_t *&dead:victims) {
        mapthing_t at=dead->spawnpoint;mobjtype_t type=dead->type;
        forgetRagdoll(dead);P_RemoveMobj(dead);
        dead=P_SpawnMobj(at.x*FRACUNIT,at.y*FRACUNIT,INT_MIN,type);   // INT_MIN: on the floor (ONFLOORZ)
        dead->spawnpoint=at;dead->angle=(angle_t)(ANG45*(at.angle/45));
    }
}
void ragdollTestTic() {
    int option=M_CheckParm((char*)"-ragdolltest");
    if(!option||option+1>=myargc||gamestate!=GS_LEVEL)return;
    mobj_t *shooter=players[consoleplayer].mo;
    if(!shooter)return;
    std::string spec=myargv[option+1];
    std::vector<std::string> fields;
    for(size_t at=0;;) {size_t end=spec.find(':',at);fields.push_back(spec.substr(at,end-at));if(end==std::string::npos)break;at=end+1;}
    std::string weapon=fields[0],aim=fields.size()>1?fields[1]:"chest";
    int only=fields.size()>2?atoi(fields[2].c_str()):-1,first=only>=0&&fields.size()>3?std::max(3,atoi(fields[3].c_str())):3;
    int pellets=weapon=="ssg"?7:weapon=="shotgun"?3:1,damage=10;
    bool blast=weapon=="rocket",burst=weapon=="chaingun";
    int spacing=blast?20:burst?12:1;    // tics between kills
    static int rocket=-2;               // the game's rocket, whose explosion the blasts are
    if(rocket==-2) {rocket=-1;for(int t=0;t<NUMMOBJTYPES&&rocket<0;++t)if(Game_ThingRole(t)==GAME_THING_ROCKET)rocket=t;}
    if(blast&&rocket<0)return;
    std::vector<mobj_t*> victims;
    for(int i=0;i<numsectors;++i)for(mobj_t *mo=sectors[i].thinglist;mo;mo=mo->snext)
        if((mo->flags&MF_COUNTKILL)&&mo!=shooter)victims.push_back(mo);
    std::sort(victims.begin(),victims.end(),[](const mobj_t *a,const mobj_t *b){return a->spawnpoint.x<b->spawnpoint.x;});   // in the row, west to east
    if(only>=0&&leveltime==2) {      // from then on the one victim is all the map holds
        for(int i=0;i<(int)victims.size();++i)if(i!=only)P_RemoveMobj(victims[i]);
        return;
    }
    if(M_CheckParm((char*)"-ragdollloop")) {
        players[consoleplayer].cheats|=CF_GODMODE;
        bool done=leveltime>roundAt+first+spacing*(int)victims.size();
        for(mobj_t *v:victims)done=done&&v->health<=0&&ragdollSettled(v);
        settledFor=done?settledFor+1:0;
        if(settledFor>=TICRATE*3/2||leveltime-roundAt>=TICRATE*12) {   // a moment to look at them lying, then again
            standUp(victims);roundAt=leveltime;++round;settledFor=0;
        }
    }
    static const char *aims[]={"chest","legs","left","right","head"};
    int at=leveltime-roundAt-first,which=at>=0&&at%spacing==0?at/spacing:-1;
    int bullet=0;                       // a burst's bullets: 0, 1, then the killing one
    if(burst&&at>=0&&at%spacing%4==0) {which=at/spacing;bullet=at%spacing/4;}
    bool anywhere=blast&&aim=="mix";   // the blast anywhere round the victim
    if(aim=="mix"&&which>=0)aim=aims[(which+round)%5];
    if(blast) {
        // Only the victim in turn shootable: a blast's reach takes in the neighbours.
        int turn=std::clamp(at/spacing,0,(int)victims.size()-1);
        for(int i=0;i<(int)victims.size();++i) {
            mobj_t *v=victims[i];
            if(v->health>0)v->flags=i==turn?v->flags|MF_SHOOTABLE:v->flags&~MF_SHOOTABLE;
        }
    }
    if(blast&&which>=0&&which<(int)victims.size()&&victims[which]->health>0) {
        mobj_t *target=victims[which];
        float dx=units(shooter->x-target->x),dy=units(shooter->y-target->y),l=std::max(1.0f,std::hypot(dx,dy));
        dx/=l;dy/=l;                    // toward the shooter
        // Where round the victim it goes off, turned from the shooter's side (positive: the shooter's right,
        // throwing the body to its left): by the aim, or anywhere.
        float turn=aim=="left"?-80.0f:aim=="right"?80.0f:aim=="head"?160.0f:0.0f;
        float height=units(target->height),z=aim=="legs"?6:aim=="head"?height-12:24;
        if(anywhere) {
            uint32_t draw=hash32((uint32_t)leveltime*2654435761u^0x5b1au);
            turn=(float)(draw%360);z=6+(float)(draw>>16)/65536.0f*(height-18);
        }
        float a=turn*(float)doomPi/180,c=std::cos(a),s=std::sin(a),bx=dx*c-dy*s,by=dx*s+dy*c;
        float out=units(target->radius)+8;
        target->health=1;
        mobj_t *boom=P_SpawnMobj(target->x+(fixed_t)(bx*out*FRACUNIT),target->y+(fixed_t)(by*out*FRACUNIT),
                                 target->z+(fixed_t)(z*FRACUNIT),(mobjtype_t)rocket);
        boom->target=shooter;
        P_ExplodeMissile(boom);
    }
    else if(which>=0&&which<(int)victims.size()&&victims[which]->health>0) {
        mobj_t *target=victims[which];
        float height=units(target->height),across=0,z=0.55f*height;
        if(aim=="head")z=height-5;
        else if(aim=="legs")z=0.2f*height;
        else if(aim=="left")across=0.6f*units(target->radius);
        else if(aim=="right")across=-0.6f*units(target->radius);
        fixed_t distance=P_AproxDistance(target->x-shooter->x,target->y-shooter->y);
        fixed_t shootZ=shooter->z+(shooter->height>>1)+8*FRACUNIT;
        fixed_t slope=FixedDiv(target->z+(fixed_t)(z*FRACUNIT)-shootZ,std::max(distance,FRACUNIT));
        angle_t angle=R_PointToAngle2(shooter->x,shooter->y,target->x,target->y)+
            (angle_t)(int64_t)(std::atan2(across,units(distance))/(2*doomPi)*4294967296.0);
        target->health=burst?(bullet==0?damage*2+1:std::min(target->health,damage*(2-bullet)+1)):damage*(pellets-1)+1;
        for(int n=0;n<pellets&&target->health>0;++n)P_LineAttack(shooter,angle,32*64*FRACUNIT,slope,damage);
    }
    int view=M_CheckParm((char*)"-ragdollview");
    if(view&&view+1<myargc&&leveltime==first+(int)victims.size()+1) {
        float x=0,y=0,facing=90;
        bool side=!strcmp(myargv[view+1],"side")&&victims.size()==1,front=!strcmp(myargv[view+1],"front")&&victims.size()==1;
        if(side||front) {
            float vx=victims[0]->spawnpoint.x,vy=victims[0]->spawnpoint.y,dx=vx-units(shooter->x),dy=vy-units(shooter->y),l=std::max(1.0f,std::hypot(dx,dy));
            dx/=l;dy/=l;                             // the shot's way; its right (dy, -dx)
            if(side) {x=vx+dy*220+dx*48;y=vy-dx*220+dy*48;facing=(float)(std::atan2(dx,-dy)*180/doomPi);}
            else {x=vx-dx*150;y=vy-dy*150;facing=(float)(std::atan2(dy,dx)*180/doomPi);}   // behind the shot, along it
            side=true;
        }
        if(side||sscanf(myargv[view+1],"%f,%f,%f",&x,&y,&facing)>=2) {
            P_TeleportMove(shooter,(fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT));
            shooter->angle=(angle_t)(int64_t)(facing/360.0*4294967296.0);
            players[consoleplayer].viewz=shooter->z+players[consoleplayer].viewheight;
        }
    }
}
}
