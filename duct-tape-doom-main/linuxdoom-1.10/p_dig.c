/* Isolated excavation experiment. Only explicitly tagged test-map columns
 * are destructible. Sector heights carry the excavated interval and are
 * serialized by the ordinary save system. No live BSP edits are necessary. */
#include <math.h>
#include <stdio.h>
#include "doomstat.h"
#include "p_local.h"
#include "p_digwall.h"
#include "p_solid.h"
#include "r_state.h"
#include "m_argv.h"
#include "w_wad.h"

int p_diglastchanged;

int P_DigEnabled(void) {
    return M_CheckParm("-digging") && W_CheckNumForName("DIGTEST")>=0;
}

void P_DigSetupPlayer(player_t *player) {
    if(!M_CheckParm("-digsandbox"))return;
    player->weaponowned[wp_missile]=true;
    player->maxammo[am_misl]=999;player->ammo[am_misl]=999;
    player->readyweapon=wp_missile;player->pendingweapon=wp_nochange;
    player->cheats|=CF_GODMODE;
    player->message="DIGGING LAB: ROCKETS CARVE CONCRETE - GOD MODE";
}

int P_DigImpact(mobj_t *rocket) {
    p_diglastchanged=0;
    if(R_DoorRocketImpact && R_DoorRocketImpact(rocket))return 0;
    if(SV_Active()){p_diglastchanged=SV_Explode(rocket);return p_diglastchanged;}
    if(P_GlobalDigEnabled()){p_diglastchanged=P_GlobalDigImpact(rocket);return p_diglastchanged;}
    if(!P_DigEnabled()||rocket->type!=MT_ROCKET)return 0;
    double vx=(double)rocket->momx/FRACUNIT,vy=(double)rocket->momy/FRACUNIT;
    double vz=(double)rocket->momz/FRACUNIT,len=sqrt(vx*vx+vy*vy+vz*vz);
    if(len<0.01)return 0;
    /* The missile stops just short of collision: advance its excavation
     * center into the material, then subtract a rounded volume. */
    double x=(double)rocket->x/FRACUNIT+vx/len*4;
    double y=(double)rocket->y/FRACUNIT+vy/len*4;
    double z=(double)rocket->z/FRACUNIT+vz/len*4;
    int changed=0;
    for(int i=0;i<numsectors;++i){
        sector_t *s=&sectors[i];
        if(s->tag!=19001||s->linecount==0)continue;
        double xmin=32767,xmax=-32768,ymin=32767,ymax=-32768;
        for(int j=0;j<s->linecount;++j){
            line_t *l=s->lines[j];
            double xx=(double)l->v1->x/FRACUNIT,yy=(double)l->v1->y/FRACUNIT;
            if(xx<xmin)xmin=xx;if(xx>xmax)xmax=xx;
            if(yy<ymin)ymin=yy;if(yy>ymax)ymax=yy;
            xx=(double)l->v2->x/FRACUNIT;yy=(double)l->v2->y/FRACUNIT;
            if(xx<xmin)xmin=xx;if(xx>xmax)xmax=xx;
            if(yy<ymin)ymin=yy;if(yy>ymax)ymax=yy;
        }
        double dx=(xmin+xmax)*0.5-x,dy=(ymin+ymax)*0.5-y;
        double d2=dx*dx+dy*dy;
        unsigned seed=(unsigned)i*2654435761u;seed^=seed>>16;
        double radius=20+(seed%5); // Fixed fractured edge, not a perfect sphere.
        if(d2>=radius*radius)continue;
        double extent=sqrt(1-d2/(radius*radius))*(26+((seed>>8)%5));
        int lo=(int)floor((z-extent)/8)*8,hi=(int)ceil((z+extent)/8)*8;
        if(lo<0)lo=0;if(hi>128)hi=128;
        if(hi<=lo)continue;
        fixed_t floor=lo*FRACUNIT,ceiling=hi*FRACUNIT;
        if(s->floorheight<s->ceilingheight){
            /* One vertical interval per column: do not silently join two
             * disconnected cavities through untouched solid material. */
            if(floor>s->ceilingheight||ceiling<s->floorheight)continue;
            if(floor>s->floorheight)floor=s->floorheight;
            if(ceiling<s->ceilingheight)ceiling=s->ceilingheight;
        }
        if(floor==s->floorheight&&ceiling==s->ceilingheight)continue;
        s->floorheight=floor;s->ceilingheight=ceiling;
        P_ChangeSector(s,false);++changed;
    }
    if(changed){
        players[consoleplayer].message="CONCRETE EXCAVATED";
        printf("Digging: %d columns excavated at %.0f, %.0f, %.0f\n",changed,x,y,z);
    }
    p_diglastchanged=changed;
    return changed;
}
