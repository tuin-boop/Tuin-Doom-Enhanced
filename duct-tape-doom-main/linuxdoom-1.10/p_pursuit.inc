#include "m_arcade.h"
#include <time.h>
/* Optional local obstacle navigation. Bounded work, no changes to actor/save
   layout, and no random-number consumption when disabled. */
int p_persistentpursuit;
#define PURSUIT_GRID 33
#define PURSUIT_NODES (PURSUIT_GRID*PURSUIT_GRID)
#define PURSUIT_STEP (32*FRACUNIT)
typedef struct {mobj_t *actor,*target;fixed_t x[32],y[32],lastx,lasty;int count,next,retry,touched;} pursuit_t;
static pursuit_t pursuit[256];
static unsigned pursuitSerial;
static int pursuitBudgetTic=-100;
typedef struct {fixed_t z;int parent,g;unsigned char state;} pursuit_node_t;
static pursuit_node_t pursuitNodes[PURSUIT_NODES];
static mobj_t *pursuitSearchActor;
static fixed_t pursuitOriginX,pursuitOriginY;
static int pursuitBest,pursuitBestDistance,pursuitExpanded,pursuitLastWork;
static int pursuitPending=-1,pursuitDirection;

// Probe a short edge with the same collision, headroom and step restrictions
// as walking. Restore actor coordinates before returning; no move is committed.
static boolean P_PursuitEdge(mobj_t *actor,fixed_t ax,fixed_t ay,fixed_t az,fixed_t bx,fixed_t by,fixed_t *z)
{
    extern fixed_t tmceilingz,tmdropoffz;
    extern boolean P_RetreatFromDropoff(mobj_t*,fixed_t,fixed_t);
    fixed_t ox=actor->x,oy=actor->y,oz=actor->z,of=actor->floorz;
    boolean ok=true;int k;
    actor->x=ax;actor->y=ay;actor->z=az;actor->floorz=az;
    for(k=1;k<=4;++k){
        fixed_t x=ax+(bx-ax)*k/4,y=ay+(by-ay)*k/4;
        if(!P_CheckPosition(actor,x,y)||tmceilingz-tmfloorz<actor->height
           ||tmceilingz-actor->z<actor->height||tmfloorz-actor->z>24*FRACUNIT
           ||(!(actor->flags&(MF_FLOAT|MF_DROPOFF))&&tmfloorz-tmdropoffz>24*FRACUNIT&&!P_RetreatFromDropoff(actor,x,y))){ok=false;break;}
        actor->x=x;actor->y=y;if(!(actor->flags&MF_FLOAT))actor->z=tmfloorz;
        actor->floorz=tmfloorz;
    }
    *z=actor->z;actor->x=ox;actor->y=oy;actor->z=oz;actor->floorz=of;
    return ok;
}
static int P_PursuitDistance(fixed_t x,fixed_t y,mobj_t *target)
{
    return (int)((llabs((long long)x-target->x)+llabs((long long)y-target->y))/FRACUNIT);
}
static boolean P_PlanPursuit(mobj_t *actor,pursuit_t *route)
{
    clock_t planStart=clock();
    int center=PURSUIT_NODES/2,n;
    static const int dx[4]={1,0,-1,0},dy[4]={0,1,0,-1};
    if(!pursuitSearchActor){
        pursuitSearchActor=actor;pursuitOriginX=actor->x;pursuitOriginY=actor->y;
        pursuitPending=-1;pursuitDirection=0;
        pursuitBest=center;pursuitBestDistance=P_PursuitDistance(actor->x,actor->y,actor->target);pursuitExpanded=0;
        memset(pursuitNodes,0,sizeof(pursuitNodes));
        pursuitNodes[center].state=1;pursuitNodes[center].parent=-1;pursuitNodes[center].z=actor->z;
        route->count=route->next=0;
    }
    fixed_t ox=pursuitOriginX,oy=pursuitOriginY;
    int best=pursuitBest,bestDistance=pursuitBestDistance;
    boolean finished=false;
    // Bound both node count and elapsed work: damaged geometry makes a node
    // much more expensive than an ordinary map probe. Resume the same search
    // on a later update without weakening its collision checks.
    // Route planning yields without suspending ordinary chase/attack updates.
    for(int work=0;work<24&&pursuitExpanded<160;++work,++pursuitExpanded){
        if(work>0&&clock()-planStart>=CLOCKS_PER_SEC/500)break;
        int current=pursuitPending,score=0x7fffffff;
        if(current<0)for(n=0;n<PURSUIT_NODES;++n)if(pursuitNodes[n].state==1){
            fixed_t x=ox+(n%PURSUIT_GRID-16)*PURSUIT_STEP,y=oy+(n/PURSUIT_GRID-16)*PURSUIT_STEP;
            int f=pursuitNodes[n].g+P_PursuitDistance(x,y,actor->target);
            if(f<score){score=f;current=n;}
        }
        if(current<0){finished=true;break;}
        pursuit_node_t *node=&pursuitNodes[current];node->state=2;
        int cx=current%PURSUIT_GRID,cy=current/PURSUIT_GRID;
        fixed_t x=ox+(cx-16)*PURSUIT_STEP,y=oy+(cy-16)*PURSUIT_STEP;
        int distance=P_PursuitDistance(x,y,actor->target);
        if(distance<bestDistance){bestDistance=distance;best=current;}
        if(distance<=64){best=current;finished=true;break;}
        for(int d=pursuitPending>=0?pursuitDirection:0;d<4;++d){
            if(clock()-planStart>=CLOCKS_PER_SEC/500){
                pursuitPending=current;pursuitDirection=d;
                pursuitBest=best;pursuitBestDistance=bestDistance;pursuitLastWork=leveltime;
                if(M_CheckParm("-profilegamesteps"))printf("PURSUIT yield tic %d %.3f ms\n",gametic,1000.0*(clock()-planStart)/CLOCKS_PER_SEC);
                return false;
            }
            int nx=cx+dx[d],ny=cy+dy[d];fixed_t z;
            if(nx<0||ny<0||nx>=PURSUIT_GRID||ny>=PURSUIT_GRID)continue;
            int index=ny*PURSUIT_GRID+nx; pursuit_node_t *next=&pursuitNodes[index];
            if(next->state==2||(next->state&&next->g<=node->g+32))continue;
            if(!P_PursuitEdge(actor,x,y,node->z,x+dx[d]*PURSUIT_STEP,y+dy[d]*PURSUIT_STEP,&z))continue;
            next->g=node->g+32;next->parent=current;next->z=z;next->state=1;
        }
        pursuitPending=-1;
    }
    if(M_CheckParm("-profilegamesteps"))printf("PURSUIT tic %d %.3f ms\n",gametic,1000.0*(clock()-planStart)/CLOCKS_PER_SEC);
    pursuitBest=best;pursuitBestDistance=bestDistance;pursuitLastWork=leveltime;
    if(!finished&&pursuitExpanded<160)return false;
    pursuitSearchActor=NULL;pursuitPending=-1;
    int reversed[PURSUIT_NODES],length=0;
    for(n=best;n!=center&&n>=0&&length<PURSUIT_NODES;n=pursuitNodes[n].parent)reversed[length++]=n;
    while(length&&route->count<32){n=reversed[--length];int i=route->count++;
        route->x[i]=ox+(n%PURSUIT_GRID-16)*PURSUIT_STEP;route->y[i]=oy+(n/PURSUIT_GRID-16)*PURSUIT_STEP;}
    if(M_CheckParm("-profilegamesteps"))printf("PURSUIT complete tic %d nodes %d waypoints %d\n",gametic,pursuitExpanded,route->count);
    route->retry=leveltime+TICRATE;return true;
}
boolean P_PersistentPursuit(mobj_t *actor)
{
    if(arcade_active||!p_persistentpursuit||demoplayback||demorecording||netgame||!actor->target
       ||!(actor->flags&MF_COUNTKILL)||(actor->flags&(MF_SKULLFLY|MF_MISSILE))||P_Unseen(actor->target))return false;
    if(pursuitSerial!=r_levelserial){memset(pursuit,0,sizeof(pursuit));pursuitSerial=r_levelserial;pursuitBudgetTic=-100;pursuitSearchActor=NULL;}
    if(pursuitSearchActor&&leveltime-pursuitLastWork>TICRATE)pursuitSearchActor=NULL;
    // Local chasing may move while planning; discard a search whose origin
    // is now too far behind, rather than steering back to stale waypoints.
    if(pursuitSearchActor==actor&&P_AproxDistance(actor->x-pursuitOriginX,actor->y-pursuitOriginY)>PURSUIT_STEP)
        pursuitSearchActor=NULL;
    pursuit_t *route=NULL;
    for(int i=0;i<256;++i)if(pursuit[i].actor==actor){route=&pursuit[i];break;}
    if(!route){for(int i=0;i<256;++i)if(!pursuit[i].actor){route=&pursuit[i];break;}}
    if(!route){route=&pursuit[0];for(int i=1;i<256;++i)if(pursuit[i].touched<route->touched)route=&pursuit[i];}
    if(route->actor!=actor||route->target!=actor->target||
       P_AproxDistance(actor->x-route->lastx,actor->y-route->lasty)>64*FRACUNIT){
        if(pursuitSearchActor==actor)pursuitSearchActor=NULL;
        memset(route,0,sizeof(*route));route->actor=actor;route->target=actor->target;
    }
    route->lastx=actor->x;route->lasty=actor->y;route->touched=leveltime;
    if(P_CheckSight(actor,actor->target)){if(pursuitSearchActor==actor)pursuitSearchActor=NULL;route->count=route->next=0;return false;}
    while(route->next<route->count&&P_AproxDistance(actor->x-route->x[route->next],actor->y-route->y[route->next])<8*FRACUNIT)++route->next;
    if(route->next>=route->count){
        if(pursuitSearchActor&&pursuitSearchActor!=actor)return false;
        if(!pursuitSearchActor&&leveltime<route->retry)return false;
        if(pursuitBudgetTic==leveltime)return false;
        pursuitBudgetTic=leveltime;
        if(!P_PlanPursuit(actor,route)){actor->movecount=0;return false;}
    }
    if(route->next>=route->count)return false;
    fixed_t dx=route->x[route->next]-actor->x,dy=route->y[route->next]-actor->y;
    int olddir=actor->movedir;
    if(abs(dx)<4*FRACUNIT)actor->movedir=dy>0?DI_NORTH:DI_SOUTH;
    else if(abs(dy)<4*FRACUNIT)actor->movedir=dx>0?DI_EAST:DI_WEST;
    else actor->movedir=diags[((dy<0)<<1)+(dx>0)];
    if(P_Move(actor)){actor->movecount=0;return true;}
    actor->movedir=olddir;route->count=route->next=0;return false;
}
