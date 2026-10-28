/* Visual-only wall cameras; they never enter the thinker/collision lists. */
extern "C" {
struct DirectorCamera {float x,y,z;};
extern DirectorCamera director_cameras[32];
extern int director_count,director_edit,director_watch,director_menu,director_demo;
int director_view_active=0;
}
namespace doom3d {
static float directorRay(const float *start,const float *end){
    float dx=end[0]-start[0],dy=end[1]-start[1],best=1;
    for(int i=0;i<numlines;++i){const auto &l=lines[i];
        float ax=units(l.v1->x),ay=units(l.v1->y),lx=units(l.dx),ly=units(l.dy),den=dx*ly-dy*lx;
        if(std::abs(den)<.0001f)continue;
        float t=((ax-start[0])*ly-(ay-start[1])*lx)/den;
        float u=((ax-start[0])*dy-(ay-start[1])*dx)/den;
        if(t<=.001f||t>=best||u<0||u>1)continue;
        float z=start[2]+(end[2]-start[2])*t;
        if(l.frontsector&&l.backsector){float floor=std::max(units(l.frontsector->floorheight),units(l.backsector->floorheight));
            float ceiling=std::min(units(l.frontsector->ceilingheight),units(l.backsector->ceilingheight));
            if(z>floor&&z<ceiling)continue;}
        best=t;
    }return best;
}
static void directorView(Uniforms &camera,float fraction){
    director_view_active=0;
    static int selected=-1,switched=-1000;static unsigned serial=~0u;
    if(serial!=r_levelserial){serial=r_levelserial;selected=-1;switched=-1000;}
    if(!director_watch||director_count<=0||!worldPending||gamestate!=GS_LEVEL)return;
    // Track the actor's body, not the first-person eye (which includes
    // walking bob, landing recovery and peek offsets).
    auto *actor=players[displayplayer].mo;if(!actor)return;
    float target[3];interpolatedPosition(*actor,fraction,target[0],target[1],target[2]);
    target[2]+=units(actor->height)*.5f;
    int best=-1;float nearest=1e30f,current=1e30f;
    for(int i=0;i<director_count;++i){const auto &c=director_cameras[i];float at[3]={c.x,c.y,c.z};
        float d=std::hypot(c.x-target[0],c.y-target[1]);
        if(directorRay(at,target)<.99f)d+=100000;
        if(d<nearest){nearest=d;best=i;}if(i==selected)current=d;
    }
    if(selected<0||selected>=director_count||(best!=selected&&gametic-switched>25&&nearest<current*.85f)){selected=best;switched=gametic;}
    if(selected<0)return;
    const auto &c=director_cameras[selected];camera.eye[0]=c.x;camera.eye[1]=c.y;camera.eye[2]=c.z;
    float dx=target[0]-c.x,dy=target[1]-c.y,dz=target[2]-c.z,yaw=std::atan2(dy,dx),look=std::atan2(dz,std::hypot(dx,dy));
    camera.right[0]=std::sin(yaw);camera.right[1]=-std::cos(yaw);camera.right[2]=0;
    camera.forward[0]=std::cos(yaw)*std::cos(look);camera.forward[1]=std::sin(yaw)*std::cos(look);camera.forward[2]=std::sin(look);
    camera.up[0]=-std::cos(yaw)*std::sin(look);camera.up[1]=-std::sin(yaw)*std::sin(look);camera.up[2]=std::cos(look);
    director_view_active=1;
}
static void directorMarkers(std::vector<unsigned> &overlay){
    if(!director_edit||director_menu||!worldPending||!players[displayplayer].mo)return;
    auto *m=players[displayplayer].mo;float eye[3]={units(m->x),units(m->y),units(players[displayplayer].viewz)};
    float yaw=float(m->angle*(2.0*doomPi/4294967296.0)),look=pitch;
    for(int i=0;i<director_count;++i){auto &c=director_cameras[i];float at[3]={c.x,c.y,c.z};if(directorRay(eye,at)<.98f)continue;
        float dx=c.x-eye[0],dy=c.y-eye[1],dz=c.z-eye[2];float ahead=dx*std::cos(yaw)+dy*std::sin(yaw);
        float depth=ahead*std::cos(look)+dz*std::sin(look);if(depth<1)continue;
        float right=dx*std::sin(yaw)-dy*std::cos(yaw),up=-ahead*std::sin(look)+dz*std::cos(look);
        float focal=160/std::tan(settings.fov*doomPi/360);
        int x=int(160+right*focal/depth),y=int(100-up*focal/depth/1.2f),size=std::clamp(int(900/depth),4,10);
        for(int yy=-size;yy<=size;++yy)for(int xx=-size;xx<=size;++xx){int px=x+xx,py=y+yy;if(px<0||px>=320||py<0||py>=200)continue;
            bool edge=std::abs(xx)>=size-1||std::abs(yy)>=size-1;overlay[py*320+px]=edge?0xff40ff70:0xb0126c30;}
        if(x>=0&&x<320&&y>=0&&y<200)overlay[y*320+x]=0xffd0ffe0;
    }
}
}
extern "C" float I_DirectorPitch(void){return doom3d::pitch;}
extern "C" void I_DirectorSetPitch(float p){doom3d::pitch=std::isfinite(p)?std::clamp(p,-1.4f,1.4f):0;tourpitch=doom3d::pitch;freeaim=doom3d::settings.look;}
extern "C" int I_DirectorPlace(float *out){using namespace doom3d;
    if(gamestate!=GS_LEVEL||!players[displayplayer].mo)return 0;
    auto *m=players[displayplayer].mo;float yaw=float(m->angle*(2.0*doomPi/4294967296.0));
    float start[3]={units(m->x),units(m->y),units(players[displayplayer].viewz)};
    float delta[3]={1024*std::cos(yaw)*std::cos(pitch),1024*std::sin(yaw)*std::cos(pitch),1024*std::sin(pitch)};
    float end[3]={start[0]+delta[0],start[1]+delta[1],start[2]+delta[2]};float t=directorRay(start,end);
    // Accept walls at normal player-radius distances too. Keep the lens on
    // the player's side without backing it past the player at close range.
    if(t>=1||t*1024<2)return 0;
    t-=std::min(4.f,t*1024*.25f)/1024;
    for(int j=0;j<3;++j)out[j]=start[j]+delta[j]*t;
    auto *sec=R_PointInSubsector((fixed_t)(out[0]*FRACUNIT),(fixed_t)(out[1]*FRACUNIT))->sector;
    return out[2]>units(sec->floorheight)+4&&out[2]<units(sec->ceilingheight)-4;
}


// Regression: a valid wall remains placeable when the player stands close to it,
// and input-time placement does not depend on a render-frame pending flag.
extern "C" int I_TestDirectorClosePlacement(void){
    using namespace doom3d;
    auto *m=players[displayplayer].mo;if(!m)return 0;
    float at[3];if(!I_DirectorPlace(at))return 0;
    fixed_t x=m->x,y=m->y;bool pending=worldPending;
    float yaw=float(m->angle*(2.0*doomPi/4294967296.0));
    m->x=(fixed_t)((at[0]-8*std::cos(yaw))*FRACUNIT);
    m->y=(fixed_t)((at[1]-8*std::sin(yaw))*FRACUNIT);
    worldPending=false;
    int ok=I_DirectorPlace(at);
    m->x=x;m->y=y;worldPending=pending;return ok;
}


extern "C" int I_TestDirectorNoBob(void){
    using namespace doom3d;
    int watching=director_watch,active=director_view_active;
    bool pending=worldPending;director_watch=1;worldPending=true;
    Uniforms a={},b={};a.eye[2]=16;b.eye[0]=9;b.eye[1]=-7;b.eye[2]=96;
    directorView(a,1);directorView(b,1);
    int ok=director_view_active;
    for(int i=0;i<3;++i)if(a.eye[i]!=b.eye[i]||a.forward[i]!=b.forward[i]||a.up[i]!=b.up[i])ok=0;
    director_watch=watching;director_view_active=active;worldPending=pending;return ok;
}
