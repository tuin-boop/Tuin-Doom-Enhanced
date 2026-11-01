/* Reliable, host-sequenced LAN simulation. Presentation never owns the clock. */
#include "p_local.h"
#include "r_main.h"
#include <time.h>
extern int prndindex;
extern fixed_t tmdropoffz;
extern void P_SpawnPlayer(mapthing_t*);
extern mapthing_t playerstarts[MAXPLAYERS];
static int lanClock,lanInputClock;
static unsigned lanMix(unsigned h,int v){return (h^(unsigned)v)*16777619u;}
static unsigned lanHash(void){
    unsigned h=lanMix(2166136261u,prndindex);
    h=lanMix(h,gameepisode);h=lanMix(h,gamemap);h=lanMix(h,leveltime);h=lanMix(h,gamestate);
    for(int i=0;i<4;i++)if(playeringame[i]){
        h=lanMix(h,lan_pitch[i]);h=lanMix(h,lan_colors[i]);h=lanMix(h,players[i].readyweapon);h=lanMix(h,i);h=lanMix(h,players[i].health);
        for(int a=0;a<NUMAMMO;a++)h=lanMix(h,players[i].ammo[a]);
    }
    for(thinker_t *t=thinkercap.next;t!=&thinkercap;t=t->next)if(t->function.acp1==(actionf_p1)P_MobjThinker){
        mobj_t *m=(mobj_t*)t;
        h=lanMix(h,m->type);h=lanMix(h,m->x);h=lanMix(h,m->y);h=lanMix(h,m->z);
        h=lanMix(h,m->momx);h=lanMix(h,m->momy);h=lanMix(h,m->momz);
        h=lanMix(h,m->angle);h=lanMix(h,m->health);h=lanMix(h,m->tics);
    }
    for(int i=0;i<numsectors;i++){h=lanMix(h,sectors[i].floorheight);h=lanMix(h,sectors[i].ceilingheight);h=lanMix(h,sectors[i].lightlevel);}
    return h;
}
/* Probe without PICKUP/MISSILE flags: no damage or inventory side effects. */
static int lanSpawnSpot(int player,mapthing_t *spot){
    for(int base=0;base<4;base++)for(int ring=0;ring<5;ring++)for(int dir=0;dir<(ring?8:1);dir++){
        int start=(player+base)%4;if(playerstarts[start].type<1)continue;
        static const int dx[8]={1,1,0,-1,-1,-1,0,1},dy[8]={0,1,1,1,0,-1,-1,-1};
        *spot=playerstarts[start];spot->x+=ring*40*dx[dir];spot->y+=ring*40*dy[dir];spot->type=player+1;
        mobj_t probe;memset(&probe,0,sizeof(probe));probe.x=(fixed_t)spot->x*FRACUNIT;probe.y=(fixed_t)spot->y*FRACUNIT;
        probe.radius=16*FRACUNIT;probe.height=56*FRACUNIT;probe.flags=MF_SOLID;probe.player=&players[player];
        probe.subsector=R_PointInSubsector(probe.x,probe.y);probe.z=probe.subsector->sector->floorheight;
        if(P_CheckPosition(&probe,probe.x,probe.y)&&tmceilingz-tmfloorz>=probe.height&&tmfloorz-tmdropoffz<=24*FRACUNIT)return 1;
    }
    return 0;
}
static unsigned lanApplyMask(unsigned desired){
    unsigned actual=1;
    for(int i=1;i<4;i++){
        if(playeringame[i]&&!(desired&(1u<<i))){
            if(players[i].mo){players[i].mo->player=NULL;P_RemoveMobj(players[i].mo);players[i].mo=NULL;}
            playeringame[i]=false;
        }
        if(!playeringame[i]&&(desired&(1u<<i))&&gamestate==GS_LEVEL&&gameaction==ga_nothing){
            mapthing_t spot;
            if(lanSpawnSpot(i,&spot)){playeringame[i]=true;memset(&players[i],0,sizeof(players[i]));players[i].playerstate=PST_REBORN;P_SpawnPlayer(&spot);}
        }
        if(playeringame[i])actual|=1u<<i;
    }
    if(!lan_host){
        lan_count=0;for(int i=0;i<4;i++)lan_count+=playeringame[i]!=0;
        if(playeringame[lan_slot]){consoleplayer=displayplayer=lan_slot;lan_catchingup=0;strcpy(lan_status,"CONNECTED - MATCH IN PROGRESS");}
    }
    return actual;
}
static void lanRunTics(void){
    int now=I_GetTime();I_StartTic();D_ProcessEvents();M_Ticker();
    if(now>lanInputClock){
        ticcmd_t cmd;memset(&cmd,0,sizeof(cmd));G_BuildTiccmd(&cmd);
        /* Pause/save requests are local UI operations, not network commands. */
        if(cmd.buttons&BT_SPECIAL)cmd.buttons=0;
        if(r_photoactive)memset(&cmd,0,sizeof(cmd));
        I_LanInput(&cmd);lanInputClock=now;
    }
    int budget=lan_host?now-lanClock:128;if(budget>128)budget=128;
    if(lan_host&&budget>4){budget=4;lanClock=now-4;}
    clock_t begin=clock();
    for(int n=0;n<budget;n++){
        lan_frame_t frame;memset(&frame,0,sizeof(frame));
        if(lan_host){I_LanHostInputs(frame.inputs);frame.mask=(unsigned char)lanApplyMask(I_LanMask());}
        else {if(!I_LanGetFrame(gametic,&frame))break;if(lanApplyMask(frame.mask)!=frame.mask)I_Error("LAN player spawn differs at tic %d",gametic);}
        static uint32_t weaponserial[4];
        if(!gametic)memset(weaponserial,0,sizeof(weaponserial));
        for(int i=0;i<4;i++){
            lan_input_t *input=&frame.inputs[i];
            lan_pitch[i]=input->pitch;lan_colors[i]=input->color;
            netcmds[i][gametic%BACKUPTICS]=input->cmd;
            if(playeringame[i]&&players[i].mo){
                players[i].mo->flags=(players[i].mo->flags&~MF_TRANSLATION)|((input->color&3)<<MF_TRANSSHIFT);
                if(input->weaponserial!=weaponserial[i]&&input->weapon<NUMWEAPONS&&players[i].weaponowned[input->weapon])players[i].pendingweapon=input->weapon;
            }
            weaponserial[i]=input->weaponserial;
        }
        G_Ticker();gametic++;maketic=gametic;
        unsigned hash=lanHash();
        if(lan_host){frame.hash=hash;I_LanRecord(&frame);lanClock++;}
        else {if(frame.hash!=hash)I_Error("LAN state mismatch at tic %d (%08x / %08x)",gametic,hash,frame.hash);I_LanCaughtUp(gametic);}
        if(!lan_host&&clock()-begin>CLOCKS_PER_SEC/125)break;
    }
    I_LanPoll();
}
