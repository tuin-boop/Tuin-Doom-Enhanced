#pragma once
extern "C" {
#include "../../linuxdoom-1.10/p_solid.h"
}
namespace doom3d {
static int peekDirection=0,peekPlayer=-1;
static bool peekHeldLeft=false,peekHeldRight=false;
static unsigned peekLevel=~0u;
static float peekAmount=0;
float cornerPeekRoll=0;
float cornerPeekAim[2]={};
static uint64_t peekClock=0;
static bool peekAllowed(){
    return settings.accelerated&&settings.cornerPeek&&gamestate==GS_LEVEL&&!menuactive&&!paused
        &&!I_Render3DSettingsOpen()&&!demoplayback&&!demorecording&&!netgame
        &&displayplayer==consoleplayer&&players[displayplayer].health>0&&players[displayplayer].mo;
}
static void resetPeek(){peekHeldLeft=peekHeldRight=false;peekDirection=0;peekAmount=0;cornerPeekRoll=0;cornerPeekAim[0]=cornerPeekAim[1]=0;peekClock=0;}
static float peekClearance(const float *eye,float dx,float dy,float distance,float slope=0){
    float clear=distance;
    // A small camera envelope, not just a single point that can slip into cracks.
    for(int probe=0;probe<5;++probe){
        float across=probe==1?3.f:probe==2?-3.f:0;
        float z=eye[2]+(probe==3?3.f:probe==4?-3.f:0);
        float x=eye[0]-dy*across,y=eye[1]+dx*across;
        float limit=distance+3;
        if(SV_Active()){
            limit*=SV_Trace(x,y,z,dx*limit,dy*limit,slope*limit,nullptr,nullptr);
        }else{
            WallHit hit;
            if(firstSolidWall(x,y,z,dx,dy,slope,limit,hit))limit=hit.t;
            for(float t=1;t<limit;t+=1){
                auto *room=R_PointInSubsector(fixed_t((x+dx*t)*FRACUNIT),fixed_t((y+dy*t)*FRACUNIT))->sector;
                if(z+slope*t<=units(room->floorheight)||z+slope*t>=units(room->ceilingheight)){limit=t;break;}
            }
        }
        clear=std::min(clear,std::max(0.f,limit-3));
    }
    return clear;
}
static void applyPeek(Uniforms &camera){
    if(peekLevel!=r_levelserial||peekPlayer!=displayplayer){resetPeek();peekLevel=r_levelserial;peekPlayer=displayplayer;}
    if(!peekAllowed()){resetPeek();return;}
    uint64_t now=SDL_GetTicksNS();
    float dt=peekClock?std::min(.05f,float((now-peekClock)/1e9)):0;peekClock=now;
    float desired=peekDirection*24.f;
    peekAmount+=std::clamp(desired-peekAmount,-dt*160,dt*160);
    cornerPeekRoll=0;
    cornerPeekAim[0]=cornerPeekAim[1]=0;
    if(std::abs(peekAmount)<.001f)return;
    float sign=peekAmount<0?-1.f:1.f;
    float dx=camera.right[0]*sign,dy=camera.right[1]*sign;
    // Head arcs around a lower torso pivot rather than translating sideways.
    float angle=std::abs(peekAmount)/24.f*.785398163f;
    float reach=34.f*std::sin(angle),drop=34.f*(1-std::cos(angle));
    float amount=peekClearance(camera.eye,dx,dy,reach,-drop/reach);
    camera.eye[0]+=dx*amount;camera.eye[1]+=dy*amount;camera.eye[2]-=drop*(amount/reach);
    cornerPeekRoll=sign*angle*(amount/reach)*(12.f/45.f);
    float c=std::cos(cornerPeekRoll),s=std::sin(cornerPeekRoll);
    for(int i=0;i<3;++i){float right=camera.right[i],up=camera.up[i];
        camera.right[i]=right*c-up*s;camera.up[i]=right*s+up*c;
    }
}
}
