extern "C" {
extern thinker_t thinkercap;
void P_MobjThinker(mobj_t *);
}
/* Frozen-world orbit camera; no actor positions or gameplay angles are changed. */
namespace doom3d {
static unsigned photoLevel;
static std::vector<mobj_t*> photoTargets;
static int photoTarget=0;
static float photoYaw=0,photoPitch=.18f,photoDistance=128;
static int photoWorldTime=-1;
static void photoCollect(){
    mobj_t *previous=photoTargets.empty()?nullptr:photoTargets[photoTarget];
        auto *p=players[displayplayer].mo;photoTargets.clear();photoTargets.push_back(p);
        float start[3]={units(p->x),units(p->y),units(players[displayplayer].viewz)};
        for(thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next){
            if(th->function.acp1!=(actionf_p1)P_MobjThinker)continue;auto *m=(mobj_t*)th;
            if(m!=r_photoeventsubject&&((!(m->flags&MF_COUNTKILL)&&m->type!=MT_SKULL)||m->health<=0))continue;
            float end[3]={units(m->x),units(m->y),units(m->z)+units(m->height)*.5f};
            if(std::hypot(end[0]-start[0],end[1]-start[1])<=2048)photoTargets.push_back(m);
        }
        // Do not use original linedef visibility: destruction can open those walls.
        std::stable_sort(photoTargets.begin()+1,photoTargets.end(),[p](mobj_t *a,mobj_t *b){
            return std::hypot(double(a->x)-p->x,double(a->y)-p->y)<std::hypot(double(b->x)-p->x,double(b->y)-p->y);
        });
    photoTarget=0;
    for(size_t i=0;i<photoTargets.size();++i)if(photoTargets[i]==previous)photoTarget=(int)i;
    photoWorldTime=leveltime;
}
static void photoEnd(){r_photoevent=0;r_photoeventsubject=nullptr;r_photocooldown=leveltime+18;r_photoactive=0;r_photosteps=0;photoTargets.clear();director_view_active=0;}
static void photoBegin(){
    auto *p=players[displayplayer].mo;photoCollect();
    photoTarget=0;photoLevel=r_levelserial;photoYaw=float(p->angle*(2*doomPi/4294967296.0));photoPitch=.18f;photoDistance=128;
    if(r_photoeventsubject)for(size_t i=0;i<photoTargets.size();++i)if(photoTargets[i]==r_photoeventsubject)photoTarget=(int)i;
    wheelActive=false;r_photohelp=1;r_photoactive=1;
}
static void photoView(Uniforms &camera){
    if(r_photoactive==2){
        if(director_edit||director_watch||gamestate!=GS_LEVEL||!players[displayplayer].mo){photoEnd();return;}
        photoBegin();
    }
    if(!r_photoactive)return;
    if(photoLevel!=r_levelserial||gamestate!=GS_LEVEL||!players[displayplayer].mo){photoEnd();return;}
    if(photoWorldTime!=leveltime)photoCollect();
    auto *m=photoTargets[photoTarget];float target[3]={units(m->x),units(m->y),units(m->z)+units(m->height)*.5f};
    float at[3]={target[0]-std::cos(photoYaw)*std::cos(photoPitch)*photoDistance,
                 target[1]-std::sin(photoYaw)*std::cos(photoPitch)*photoDistance,target[2]+std::sin(photoPitch)*photoDistance};
    float hit=directorRay(target,at);if(hit<1)hit=std::max(0.f,hit-4.f/photoDistance);
    for(int i=0;i<3;++i)at[i]=target[i]+(at[i]-target[i])*hit;
    auto *sector=R_PointInSubsector((fixed_t)(at[0]*FRACUNIT),(fixed_t)(at[1]*FRACUNIT))->sector;
    float lo=units(sector->floorheight)+4,hi=units(sector->ceilingheight)-4;
    if(hi>=lo)at[2]=std::clamp(at[2],lo,hi);
    float dx=target[0]-at[0],dy=target[1]-at[1],dz=target[2]-at[2];
    float yaw=std::atan2(dy,dx),look=std::atan2(dz,std::max(.01f,std::hypot(dx,dy)));
    for(int i=0;i<3;++i)camera.eye[i]=at[i];
    camera.right[0]=std::sin(yaw);camera.right[1]=-std::cos(yaw);camera.right[2]=0;
    camera.forward[0]=std::cos(yaw)*std::cos(look);camera.forward[1]=std::sin(yaw)*std::cos(look);camera.forward[2]=std::sin(look);
    camera.up[0]=-std::cos(yaw)*std::sin(look);camera.up[1]=-std::sin(yaw)*std::sin(look);camera.up[2]=std::cos(look);
    r_photofocus=std::max(1.f,std::sqrt(dx*dx+dy*dy+dz*dz));director_view_active=1;
}
}
extern "C" int I_PhotoKey(int key){using namespace doom3d;
    if(key==SDL_SCANCODE_F8){
        if(r_photoactive){photoEnd();return 1;}
        if(gamestate!=GS_LEVEL||menuactive||paused||demorecording||demoplayback||(netgame&&!lan_session)||director_edit||director_watch||!players[displayplayer].mo)return 1;
        photoBegin();return 1;
    }
    if(!r_photoactive)return 0;
    if(netgame&&(key==SDL_SCANCODE_LEFTBRACKET||key==SDL_SCANCODE_RIGHTBRACKET||key==SDL_SCANCODE_T||(key>=SDL_SCANCODE_1&&key<=SDL_SCANCODE_6)))return 1;
    if(key==SDL_SCANCODE_ESCAPE)photoEnd();
    else if(key==SDL_SCANCODE_LEFTBRACKET||key==SDL_SCANCODE_RIGHTBRACKET)r_photosteps=std::min(r_photosteps+1,8);
    else if(key==SDL_SCANCODE_T)r_photoauto=!r_photoauto;
    else if(key>=SDL_SCANCODE_1&&key<=SDL_SCANCODE_5)r_photomask^=1<<(key-SDL_SCANCODE_1);
    else if(key==SDL_SCANCODE_6)r_photoscope=(r_photoscope+1)%3;
    else if(key==SDL_SCANCODE_TAB)photoTarget=(photoTarget+1)%photoTargets.size();
    else if(key==SDL_SCANCODE_P)photoTarget=0;
    else if(key==SDL_SCANCODE_D)r_photodof=(r_photodof+1)%4;
    else if(key==SDL_SCANCODE_C)r_photostyle=(r_photostyle+1)%5;
    else if(key==SDL_SCANCODE_H)r_photohelp=!r_photohelp;
    else if(key==SDL_SCANCODE_F7){r_photohelp=0;I_Render3DScreenshot();}
    return 1;
}
extern "C" void I_PhotoMove(float x,float y){using namespace doom3d;photoYaw-=x*.004f;photoPitch=std::clamp(photoPitch+y*.004f,-1.25f,1.25f);}
extern "C" void I_PhotoZoom(float wheel){using namespace doom3d;photoDistance=std::clamp(photoDistance*std::pow(.85f,wheel),24.f,768.f);}

extern "C" void *I_PhotoSubject(){using namespace doom3d;return r_photoactive&&!photoTargets.empty()?photoTargets[photoTarget]:nullptr;}

namespace doom3d {
static Uint64 photoSavedAt=0;
static bool photoSavedOK=false;
extern "C" {extern char r_photoSavedPath[2048];extern int r_photoNoticeUntil,r_photoSavedOK;}
static void photoCaptureFeedback(bool saved,const char *path){
    photoSavedAt=SDL_GetTicks();photoSavedOK=saved;
    char *cwd=SDL_GetCurrentDirectory();
    const bool absolute=path[0]=='/'||path[0]=='\\'||(path[0]&&path[1]==':');
    snprintf(r_photoSavedPath,sizeof(r_photoSavedPath),"%s%s",absolute?"":cwd?cwd:"",path);
    SDL_free(cwd);r_photoNoticeUntil=I_GetTime()+7*35;r_photoSavedOK=saved;
}
}
extern "C" float I_PhotoFlash(void){using namespace doom3d;
    if(!r_photoactive||!photoSavedAt||!photoSavedOK)return 0;
    float t=float(SDL_GetTicks()-photoSavedAt)/240.f;
    return t<1?.75f*(1-t)*(1-t):0;
}
