/* Experimental per-wall excavation. Original BSP stays immutable; deformed
 * wall patches are a collision/render overlay in map coordinates. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "doomstat.h"
#include "p_local.h"
#include "r_state.h"
#include "r_main.h"
#include "m_argv.h"
#include "p_digwall.h"
#define MAX_CUTS 4096
typedef struct { int line,side; float u,z,strength; } cut_t;
static cut_t cuts[MAX_CUTS];
static unsigned char cutProfile[MAX_CUTS];
static int impactProfile;
static float profileRadius(int p){return p==1?4.f:p==2?8.f:24.f;}
static unsigned cutVersions[MAX_CUTS];
typedef struct {int sector,plane;float base;} cutanchor_t;
static cutanchor_t cutAnchors[MAX_CUTS];
static mobj_t *monsterMissile;
static float monsterX,monsterY,monsterZ;
static float impactStrength=4;
/* Direct monster projectile hits chip at one eighth of rocket strength. */
int p_monster_surface_damage=1,p_player_surface_damage=1;
static float missileDigStrength(const mobj_t *mo){
    if(mo->type==MT_ROCKET)return 4;
    if(mo->type==MT_PLASMA||mo->type==MT_BFG)
        return p_player_surface_damage&&mo->target&&mo->target->player?(mo->type==MT_BFG?8.f:.35f):0;
    if(!p_monster_surface_damage)return 0;
    switch(mo->type){
    case MT_FATSHOT:case MT_TROOPSHOT:case MT_HEADSHOT:
    case MT_BRUISERSHOT:case MT_ARACHPLAZ:case MT_TRACER:return .5f;
    default:return 0;
    }
}
static int missileProfile(const mobj_t *mo){return mo->type==MT_PLASMA?2:0;}
static float anchorHeight(cutanchor_t a){return a.sector>=0?(a.plane?sectors[a.sector].ceilingheight:sectors[a.sector].floorheight)/(float)FRACUNIT:0;}
static float cutHeight(int i){return cuts[i].z+(cutAnchors[i].sector>=0?anchorHeight(cutAnchors[i])-cutAnchors[i].base:0);}
static float facePlane(int line,int side,float z){
    sector_t *back=side?lines[line].frontsector:lines[line].backsector;
    if(!back)return 0;
    if(z>=back->ceilingheight/(float)FRACUNIT)return back->ceilingheight/(float)FRACUNIT;
    if(z<=back->floorheight/(float)FRACUNIT)return back->floorheight/(float)FRACUNIT;
    return 0;
}
static int count;
static unsigned revision=1,indexRevision;
static mobj_t *blockedMissile;
static int blockedLine,blockedSide;
static float blockedU,blockedZ;
static int captureContact;
static int *heads,nextCut[MAX_CUTS];
static float *faceBounds;
static void indexCuts(void){
    if(indexRevision==revision&&heads)return;
    if(!heads){heads=malloc((size_t)numlines*2*sizeof(int));faceBounds=malloc((size_t)numlines*8*sizeof(float));
        if(!heads||!faceBounds)I_Error("Excavation index allocation failed");}
    for(int i=0;i<numlines*2;++i){heads[i]=-1;float *b=faceBounds+i*4;b[0]=b[2]=1e9f;b[1]=b[3]=-1e9f;}
    for(int i=0;i<count;++i){cut_t *c=&cuts[i];int key=c->line*2+c->side;nextCut[i]=heads[key];heads[key]=i;float *b=faceBounds+key*4;
        if(c->u-32<b[0])b[0]=c->u-32;if(c->u+32>b[1])b[1]=c->u+32;
        if(c->z-32<b[2])b[2]=c->z-32;if(c->z+32>b[3])b[3]=c->z+32;}
    indexRevision=revision;
}
int p_breakablewalls=1;
int p_throughwalls=0,p_breachdoors=0,p_wall_damage=100,p_deeperdig=0;
int P_GlobalDigThroughEnabled(void){int mode=(p_throughwalls?1:0)|(p_breachdoors?2:0);return P_GlobalDigEnabled()&&mode?(mode|(p_deeperdig?4:0)):0;}
int P_GlobalDigMaxDepth(void){return p_deeperdig?4096:256;}
static void resetBreaches(void);
static void breachBounds(int line,int side,float *b,int *found);
static unsigned breachRevision(int line,int side);
static unsigned otherBreachRevision(int line);
int P_GlobalDigEnabled(void){return p_breakablewalls&&M_CheckParm("-digglobal")!=0&&!M_CheckParm("-solidmaps")&&!M_CheckParm("-solidlab");}
void P_GlobalDigFlatReset(void);
void P_GlobalDigReset(void){P_GlobalDigFlatReset();resetBreaches();count=0;memset(cutProfile,0,sizeof(cutProfile));impactProfile=0;blockedMissile=NULL;monsterMissile=NULL;impactStrength=4;++revision;free(heads);free(faceBounds);heads=NULL;faceBounds=NULL;}
unsigned P_GlobalDigRevision(void){return revision;}
static int joinedFace(int line,int side,int end);
unsigned P_GlobalDigWallRevision(int line,int side){
    indexCuts();unsigned value=0,motion=2166136261u;
    for(int i=heads[line*2+side];i>=0;i=nextCut[i])if(cutVersions[i]>value)value=cutVersions[i];
    for(int end=0;end<2;++end){int key=joinedFace(line,side,end);if(key<0)continue;
        for(int i=heads[key];i>=0;i=nextCut[i])if(cutVersions[i]>value)value=cutVersions[i];
        // A stationary jamb shares depth samples with the moving door.
        // Its mesh must refresh even when its own heights/cuts do not change.
        sector_t *back=key%2?lines[key/2].frontsector:lines[key/2].backsector;
        if(back){motion=(motion^(unsigned)back->floorheight)*16777619u;motion=(motion^(unsigned)back->ceilingheight)*16777619u;}
    }
    if(P_GlobalDigThroughEnabled()){unsigned other=breachRevision(line,side);if(other>value)value=other;other=otherBreachRevision(line);if(other>value)value=other;}
    return value^motion;
}
static double unit(fixed_t v){return (double)v/FRACUNIT;}
static double frame(int line,int side,double *ax,double *ay,double *tx,double *ty){
    line_t *l=&lines[line];vertex_t *a=side?l->v2:l->v1,*b=side?l->v1:l->v2;
    *ax=unit(a->x);*ay=unit(a->y);*tx=unit(b->x-a->x);*ty=unit(b->y-a->y);
    double len=hypot(*tx,*ty);if(len<.001)return 0;*tx/=len;*ty/=len;return len;
}
// Immutable oriented endpoint adjacency. Only join faces bounding the same
// rooms/material volume; moving sector boundaries remain independent.
static int joinedFace(int line,int side,int end){
    static int *links,serial=-1,size;
    if(serial!=r_levelserial||size!=numlines){
        free(links);size=numlines;serial=r_levelserial;
        links=malloc((size_t)numlines*4*sizeof(int));
        if(!links)I_Error("Crater adjacency allocation failed");
        for(int i=0;i<numlines*4;++i)links[i]=-2;
    }
    int *cached=&links[(line*2+side)*2+end];if(*cached!=-2)return *cached;
    *cached=-1;line_t *l=&lines[line];
    sector_t *room=side?l->backsector:l->frontsector,*back=side?l->frontsector:l->backsector;
    if(!room)return -1;
    vertex_t *corner=end?(side?l->v1:l->v2):(side?l->v2:l->v1);
    for(int i=0;i<room->linecount;++i){line_t *other=room->lines[i];if(other==l)continue;
        for(int ns=0;ns<2;++ns){
            if(other->sidenum[ns]<0||(ns?other->backsector:other->frontsector)!=room)continue;
            if((ns?other->frontsector:other->backsector)!=back){
                // A door jamb may split a continuous front plane into a
                // one-sided wall and a sector-backed face. Join only that
                // collinear plane; turns still require identical backing.
                double ax,ay,tx,ty,bx,by,ux,uy;
                if(!frame(line,side,&ax,&ay,&tx,&ty)||!frame((int)(other-lines),ns,&bx,&by,&ux,&uy)||tx*ux+ty*uy<.99999)continue;
            }
            vertex_t *v=end?(ns?other->v2:other->v1):(ns?other->v1:other->v2);
            if(v->x==corner->x&&v->y==corner->y){*cached=(int)(other-lines)*2+ns;return *cached;}
        }
    }return -1;
}
/* Conservative solid thickness: stop before the next map boundary, even
 * when it belongs to another room. Cache only immutable XY topology. */
typedef struct {int line,side,u;unsigned serial;float limit;} thickness_t;
static thickness_t thickness[8192];
static double nextWallBoundary(int line,int side,float u,double limit){
    double ax,ay,tx,ty,len=frame(line,side,&ax,&ay,&tx,&ty);if(!len)return 0;
    double x=ax+u*tx,y=ay+u*ty,nx=-ty,ny=tx;
    for(int i=0;i<numlines;++i){
        if(i==line)continue;
        double bx=unit(lines[i].v1->x),by=unit(lines[i].v1->y);
        double dx=unit(lines[i].v2->x-lines[i].v1->x),dy=unit(lines[i].v2->y-lines[i].v1->y);
        double determinant=nx*dy-ny*dx;if(fabs(determinant)<1e-8)continue;
        double t=((bx-x)*dy-(by-y)*dx)/determinant;
        double v=((bx-x)*ny-(by-y)*nx)/determinant;
        if(t>.01&&v>=-.00001&&v<=1.00001&&t<limit)limit=t;
    }
    return limit;
}
float P_GlobalDigWallLimit(int line,int side,float u){
    return (float)fmax(0,nextWallBoundary(line,side,u,66)-2); // Leave a solid backing.
}
static float wallLimit(int line,int side,int u){
    unsigned key=((unsigned)line*73856093u^(unsigned)side*19349663u^(unsigned)u*83492791u)%8192;
    thickness_t *c=&thickness[key];
    if(c->serial!=r_levelserial+1||c->line!=line||c->side!=side||c->u!=u){
        *c=(thickness_t){line,side,u,r_levelserial+1,P_GlobalDigWallLimit(line,side,(float)u)};
    }return c->limit;
}
static void spreadFlatToWalls(int sector,int ceiling,float x,float y);
static void recoverCraterSupport(mobj_t *thing);
#include "p_digflat.inc"
/* Sampling at fixed lattice vertices gives both collision and rendering the
 * same continuous piecewise-planar surface. Stable noise breaks the edge. */
static float latticeUncached(int line,int side,int u,int z,float shift){
    float d=0;
    indexCuts();
    for(int i=heads[line*2+side];i>=0;i=nextCut[i]){const cut_t *c=&cuts[i];
        float x=(u-c->u)/profileRadius(cutProfile[i]),y=(z+shift-cutHeight(i))/profileRadius(cutProfile[i]);
        if(fabsf(x)>1.2f||fabsf(y)>1.2f)continue;
        float angle=atan2f(y,x);
        float phase=c->u*.17f+(c->z-cutAnchors[i].base)*.13f+c->line*.7f;
        float edge=1+.12f*sinf(angle*5+phase)+.07f*cosf(angle*9-phase);
        float r=(x*x+y*y)/(edge*edge);
        if(r<1)d+=c->strength*(1-r);
    }
    if(d<=0)return 0;
    unsigned hash=(unsigned)u*73856093u^(unsigned)z*19349663u^(unsigned)line*83492791u;
    d*=.85f+(hash%101)*.003f;
    return d;
}
static float rawLattice(int line,int side,int u,int z,float shift){
    typedef struct {int line,side,u,z;unsigned revision;float shift,value;fixed_t floor,ceiling;} Sample;
    static Sample cache[65536];
    unsigned key=((unsigned)line*73856093u^(unsigned)side*83492791u^(unsigned)u*19349663u^(unsigned)z*2654435761u)&65535;
    Sample *c=&cache[key];sector_t *back=side?lines[line].frontsector:lines[line].backsector;
    fixed_t floor=back?back->floorheight:0,ceiling=back?back->ceilingheight:0;
    if(c->revision!=revision||c->line!=line||c->side!=side||c->u!=u||c->z!=z||c->shift!=shift||c->floor!=floor||c->ceiling!=ceiling)
        *c=(Sample){line,side,u,z,revision,shift,latticeUncached(line,side,u,z,shift),floor,ceiling};
    return c->value;
}
// Remove only tiny peaks surrounded on all eight sides by deeper damage.
// Shared by rendering and collision; derived from saved cuts, never a frame job.
static float lattice(int line,int side,int u,int z,float shift){
    float center=rawLattice(line,side,u,z,shift);if(center<=0)return center;
    float ring=1e9f;
    for(int y=-4;y<=4;y+=4)for(int x=-4;x<=4;x+=4){
        if(!x&&!y)continue;
        ring=fminf(ring,rawLattice(line,side,u+x,z+y,shift));
        if(ring<=center+1)return center;
    }
    return fminf(ring,center+12);
}
static void crumbleDebris(int line,int side,float u,float z){
    if(!R_CraterCrumble)return;
    double ax,ay,tx,ty,len=frame(line,side,&ax,&ay,&tx,&ty);
    float shift=facePlane(line,side,z);int emitted=0;
    for(int y=-16;y<=16&&emitted<4;y+=8)for(int x=-16;x<=16&&emitted<4;x+=8){
        int a=(int)floorf((u+x)/4)*4,h=(int)floorf((z-shift+y)/4)*4;
        if(a<4||a>len-4)continue;
        float raw=rawLattice(line,side,a,h,shift),clean=lattice(line,side,a,h,shift);
        float limit=P_GlobalDigBreachLimit(line,side,(float)a,h+shift);
        int foot=limit>0&&raw<limit&&P_GlobalDigBreachMargin(line,side,(float)a,h+shift)>0;
        if(clean<=raw+1&&!foot)continue;
        // Place chips just outside the source surface, where they can fall
        // without being swallowed by the original closed-sector ceiling.
        R_CraterCrumble((float)(ax+a*tx+ty*2),(float)(ay+a*ty-tx*2),h+shift);
        ++emitted;
    }
}
static float sampleDepthBase(int line,int side,float u,float z,int raw){
    float worldZ=z,shift=facePlane(line,side,z);z-=shift;
    int x=(int)floorf(u/4)*4,y=(int)floorf(z/4)*4;
    float a=(u-x)/4,b=(z-y)/4;
    float p=lattice(line,side,x,y,shift),q=lattice(line,side,x+4,y+4,shift);
    float r=lattice(line,side,a>=b?x+4:x,a>=b?y:y+4,shift);
    if(!raw){p=fminf(p,wallLimit(line,side,x));q=fminf(q,wallLimit(line,side,x+4));r=fminf(r,wallLimit(line,side,a>=b?x+4:x));}
    float depth=a>=b?p*(1-a)+r*(a-b)+q*b:p*(1-b)+r*(b-a)+q*a;
    return depth;
}
static float sampleDepth(int line,int side,float u,float z,int raw){
    double ax,ay,tx,ty,len=frame(line,side,&ax,&ay,&tx,&ty);
    if(u<-.001f||u>len+.001f)return 0;
    float depth=sampleDepthBase(line,side,u,z,raw);
    // A confirmed finite breach may consume the whole face. The breach
    // volume itself clips to real room boundaries; an artificial zero-depth
    // strip here would leave a rectangular frame around a destroyed slab.
    int end=u>len*.5;float distance=end?(float)len-u:u;
    if(distance>=.75f)return depth;
    float shared=raw?depth:0;int neighbor=joinedFace(line,side,end);
    if(neighbor>=0){
        double bx,by,ux,uy,other=frame(neighbor/2,neighbor%2,&bx,&by,&ux,&uy);
        // Reversing surfaces do not enclose a meaningful shared corner.
        if(tx*ux+ty*uy>(raw?.9999:-.9)){
            float here=sampleDepthBase(line,side,end?(float)len:0,z,raw);
            float there=sampleDepthBase(neighbor/2,neighbor%2,end?0:(float)other,z,raw);
            shared=fminf(here,there);
        }
    }
    float blend=fmaxf(0,distance)/.75f;return shared*(1-blend)+depth*blend;
}
int P_GlobalDigWallJoined(int line,int side,int end){return joinedFace(line,side,end)>=0;}
// Miter joined crater edges so adjacent wall meshes meet at one world point.
void P_GlobalDigWallPoint(int line,int side,float u,float z,float depth,float *x,float *y){
    double ax,ay,tx,ty,len=frame(line,side,&ax,&ay,&tx,&ty);
    *x=(float)(ax+u*tx-depth*ty);*y=(float)(ay+u*ty+depth*tx);
    int end=u>len*.5;float distance=end?(float)len-u:u;
    if(depth<=0||distance>=.75f)return;
    int neighbor=joinedFace(line,side,end);if(neighbor<0)return;
    double bx,by,ux,uy;frame(neighbor/2,neighbor%2,&bx,&by,&ux,&uy);
    double dot=tx*ux+ty*uy;if(dot<=-.9)return;
    float blend=1-fmaxf(0,distance)/.75f;
    *x+=(float)(((-ty-uy)/(1+dot)+ty)*depth*blend);
    *y+=(float)(((tx+ux)/(1+dot)-tx)*depth*blend);
}
#include "p_digbreach.inc"
float P_GlobalDigDepth(int line,int side,float u,float z){
    float limit=P_GlobalDigBreachLimit(line,side,u,z);
    if(limit>0)return fminf(sampleDepth(line,side,u,z,1),limit);
    return sampleDepth(line,side,u,z,0);
}
int P_GlobalDigBounds(int line,int side,float *b){
    if(!P_GlobalDigEnabled()||line<0||line>=numlines)return 0;
    indexCuts();int found=heads[line*2+side]>=0;
    if(!found){b[0]=b[2]=1e9f;b[1]=b[3]=-1e9f;breachBounds(line,side,b,&found);return found;}
    memcpy(b,faceBounds+(line*2+side)*4,4*sizeof(float));
    b[2]=1e9f;b[3]=-1e9f;
    for(int i=heads[line*2+side];i>=0;i=nextCut[i]){float z=cutHeight(i);b[2]=fminf(b[2],z-32);b[3]=fmaxf(b[3],z+32);}
    breachBounds(line,side,b,&found);
    return 1;
}
static int solid(int line,int side,double z){
    line_t *l=&lines[line];sector_t *f=side?l->backsector:l->frontsector,*b=side?l->frontsector:l->backsector;
    if(!f)return 0;
    float floor=(float)unit(f->floorheight),ceiling=(float)unit(f->ceilingheight);
    // A breached slab exposes its perimeter from inside. The original
    // sector may still have zero height; use the same bounds as its mesh.
    if(!b)P_GlobalDigBreachRoomBounds((int)(f-sectors),&floor,&ceiling);
    if(z<floor||z>ceiling)return 0;
    return !b||z<unit(b->floorheight)||z>unit(b->ceilingheight);
}
static void addWallCut(int line,int side,float u,float z){
    float strength=impactStrength*fminf(5,fmaxf(1,p_wall_damage/100.f));
    sector_t *back=side?lines[line].frontsector:lines[line].backsector;
    cutanchor_t anchor={-1,0,0};
    if(back){anchor.sector=(int)(back-sectors);anchor.plane=z>=back->ceilingheight/(float)FRACUNIT;anchor.base=anchorHeight(anchor);}
    for(int i=0;i<count;++i)if(cutProfile[i]==impactProfile&&cuts[i].line==line&&cuts[i].side==side&&cutAnchors[i].sector==anchor.sector&&cutAnchors[i].plane==anchor.plane&&fabsf(cuts[i].u-u)<4&&fabsf(cutHeight(i)-z)<4){cuts[i].strength=fminf(fmaxf(cuts[i].strength,P_GlobalDigThroughEnabled()?P_GlobalDigMaxDepth()*4:256),cuts[i].strength+strength);if(impactProfile)cuts[i].strength=fminf(cuts[i].strength,impactProfile==1?2:4);cutVersions[i]=++revision;return;}
    if(count<MAX_CUTS){cutVersions[count]=revision+1;cutAnchors[count]=anchor;cutProfile[count]=(unsigned char)impactProfile;cuts[count++]=(cut_t){line,side,u,z,strength};++revision;}
}
static void spreadWallCut(int line,int side,float u,float z,int transferFlats){
    struct Node{int line,side;float u;} queue[32];int size=1;
    queue[0]=(struct Node){line,side,u};
    sector_t *room=side?lines[line].backsector:lines[line].frontsector;
    for(int q=0;q<size;++q){struct Node node=queue[q];
        double ax,ay,tx,ty,len=frame(node.line,node.side,&ax,&ay,&tx,&ty);
        if(!solid(node.line,node.side,z)&&!solid(node.line,node.side,z-23)&&!solid(node.line,node.side,z+23))continue;
        addWallCut(node.line,node.side,node.u,z);
        if(q==0&&!impactProfile)crumbleDebris(node.line,node.side,node.u,z);
        for(int end=0;end<2;++end){float distance=fabsf(node.u-(end?(float)len:0));if(distance>=profileRadius(impactProfile)*1.2f)continue;
            vertex_t *corner=end?(node.side?lines[node.line].v1:lines[node.line].v2):(node.side?lines[node.line].v2:lines[node.line].v1);
            for(int n=0;n<room->linecount&&size<32;++n){line_t *other=room->lines[n];int id=(int)(other-lines);
                if(id==node.line)continue;
                for(int ns=0;ns<2;++ns){if(other->sidenum[ns]<0||(ns?other->backsector:other->frontsector)!=room)continue;
                    vertex_t *start=ns?other->v2:other->v1,*finish=ns?other->v1:other->v2;
                    int atStart=start->x==corner->x&&start->y==corner->y,atEnd=finish->x==corner->x&&finish->y==corner->y;
                    if(!atStart&&!atEnd)continue;int seen=0;for(int k=0;k<size;++k)if(queue[k].line==id&&queue[k].side==ns)seen=1;if(seen)continue;
                    float length=(float)hypot(unit(finish->x-start->x),unit(finish->y-start->y));
                    queue[size++]=(struct Node){id,ns,atStart?-distance:length+distance};
                }
            }
        }
    }
    if(transferFlats){double ax,ay,tx,ty;frame(line,side,&ax,&ay,&tx,&ty);
        for(int ceiling=0;ceiling<2;++ceiling){float edge=(float)unit(ceiling?room->ceilingheight:room->floorheight),distance=fabsf(z-edge);
            if(distance<profileRadius(impactProfile)*1.2f&&(ceiling?room->ceilingpic:room->floorpic)!=skyflatnum)
                spreadFlatCuts((int)(room-sectors),ceiling,(float)(ax+u*tx-distance*ty),(float)(ay+u*ty+distance*tx));
        }
    }
}
static void spreadFlatToWalls(int sector,int ceiling,float x,float y){
    sector_t *room=&sectors[sector];float floor=(float)unit(room->floorheight),top=(float)unit(room->ceilingheight);
    if(P_GlobalDigBreachSupportRoom(sector)>=0)P_GlobalDigBreachRoomBounds(sector,&floor,&top);
    float edge=ceiling?top:floor;
    for(int n=0;n<room->linecount;++n){line_t *line=room->lines[n];
        if(line->frontsector==line->backsector)continue;
        for(int side=0;side<2;++side){if(line->sidenum[side]<0||(side?line->backsector:line->frontsector)!=room)continue;
            double ax,ay,tx,ty,len=frame((int)(line-lines),side,&ax,&ay,&tx,&ty);if(!len)continue;
            double u=(x-ax)*tx+(y-ay)*ty,d=-(x-ax)*ty+(y-ay)*tx;
            if(u<0||u>len||d>1||d<-profileRadius(impactProfile)*1.2f)continue;
            // Unfold the floor onto the wall around the shared edge.
            float center=edge+(ceiling?(float)-d:(float)d);
            int id=(int)(line-lines);
            // At a ledge the visible vertical edge belongs to the lower
            // neighbouring room, not the floor's own side of this linedef.
            if(!solid(id,side,center)&&line->sidenum[1-side]>=0&&solid(id,1-side,center))
                addWallCut(id,1-side,(float)(len-u),center);
            else if(solid(id,side,center)||solid(id,side,center-23)||solid(id,side,center+23))addWallCut(id,side,(float)u,center);
        }
    }
}
int P_GlobalDigFit(int line,int side,mobj_t *thing,fixed_t x,fixed_t y){
    float bounds[4];if(!P_GlobalDigEnabled()||!P_GlobalDigBounds(line,side,bounds))return 0;
    double ax,ay,tx,ty,len=frame(line,side,&ax,&ay,&tx,&ty);if(!len)return 0;
    if(breachFit(line,side,thing,x,y))return 1;
    double radius=unit(thing->radius);
    fixed_t stepped=P_GlobalDigBreachStepFloor(thing,x,y,thing->z);
    double bottom=unit(stepped)+.1,top=unit(stepped+thing->height)-.1;
    for(int i=-1;i<=1;++i)for(int j=-1;j<=1;++j){
        double px=unit(x)+i*radius-ax,py=unit(y)+j*radius-ay;
        double u=px*tx+py*ty,d=-px*ty+py*tx;
        if(d<=0)continue;
        for(double z=bottom;;z=fmin(z+4,top)){
            if((u<0||u>len|| (solid(line,side,z)&&d>P_GlobalDigDepth(line,side,(float)u,(float)z)+.05))&&
               !breachPointOpen(unit(x)+i*radius,unit(y)+j*radius,z)){
                if(captureContact&&missileDigStrength(thing)>0){blockedMissile=thing;blockedLine=line;blockedSide=side;blockedU=(float)u;blockedZ=(float)z;}
                return 0;
            }
            if(z>=top)break;
        }
    }return 1;
}
// A player may fall beside a ledge while their radius still overlaps it.
// Permit only outward motion from an existing overlap, never entry into solid.
int P_GlobalDigRetreat(int line,int side,mobj_t *thing,fixed_t x,fixed_t y){
    if(!thing->player||!P_GlobalDigEnabled())return 0;
    float bounds[4];if(!P_GlobalDigBounds(line,side,bounds))return 0;
    sector_t *room=side?lines[line].backsector:lines[line].frontsector;
    if(!room||!thing->subsector||thing->subsector->sector!=room)return 0;
    double ax,ay,tx,ty,len=frame(line,side,&ax,&ay,&tx,&ty);
    double u=(unit(thing->x)-ax)*tx+(unit(thing->y)-ay)*ty;
    double oldd=-(unit(thing->x)-ax)*ty+(unit(thing->y)-ay)*tx;
    double next=-(unit(x)-ax)*ty+(unit(y)-ay)*tx;
    double radius=unit(thing->radius)*(fabs(tx)+fabs(ty));
    if(u<radius||u>len-radius||oldd>1||oldd+radius<=0||next>=oldd-.00001)return 0;
    // Keep the retreat inside the same face; other faces retain their checks.
    double newu=(unit(x)-ax)*tx+(unit(y)-ay)*ty;
    if(newu<radius||newu>len-radius)return 0;
    return !P_GlobalDigFit(line,side,thing,thing->x,thing->y);
}
/* Supplemental checks remain active after the actor has crossed the old
 * plane. The original room's heights provide floor/ceiling in the recess. */
int P_GlobalDigPosition(mobj_t *thing,fixed_t x,fixed_t y,int *owner){
    blockedMissile=NULL;
    *owner=-1;if(!P_GlobalDigEnabled()||(thing->flags&MF_NOCLIP))return 1;
    indexCuts();
    for(int i=0;i<count;++i){cut_t *c=&cuts[i];
        if(heads[c->line*2+c->side]!=i)continue;
        double ax,ay,tx,ty,len=frame(c->line,c->side,&ax,&ay,&tx,&ty),radius=unit(thing->radius);
        double px=unit(x)-ax,py=unit(y)-ay,u=px*tx+py*ty,d=-px*ty+py*tx;
        if(u<0||u>len||d+radius<=0||d-radius>(P_GlobalDigThroughEnabled()?P_GlobalDigMaxDepth()+16:80))continue;
        double oldd=-(unit(thing->x)-ax)*ty+(unit(thing->y)-ay)*tx;
        sector_t *front=c->side?lines[c->line].backsector:lines[c->line].frontsector;
        float exitDepth=P_GlobalDigBreachLimit(c->line,c->side,(float)u,(float)(unit(thing->z)+unit(thing->height)*.5));
        if(exitDepth>0&&d-radius>=exitDepth)continue;
        if(exitDepth<=0&&(oldd>80||(oldd>0&&thing->subsector&&thing->subsector->sector!=front&&thing->subsector->sector!=(c->side?lines[c->line].frontsector:lines[c->line].backsector))))continue;
        if(!solid(c->line,c->side,unit(thing->z)+unit(thing->height)*.5)&&!solid(c->line,c->side,unit(thing->z+thing->height)-.1)&&!solid(c->line,c->side,unit(thing->z)+.1))continue;
        // A concave sector can return to the room behind this wall plane.
        // Once another map boundary separates the point from this face,
        // its crater must not impose collision on that unrelated space.
        if(exitDepth<=0&&d>0&&nextWallBoundary(c->line,c->side,(float)u,d+.01)<d-.01)continue;
        captureContact=1;int fit=P_GlobalDigFit(c->line,c->side,thing,x,y);captureContact=0;
        if(!fit){if(P_GlobalDigRetreat(c->line,c->side,thing,x,y))continue;return 0;}
        if(d>=0){sector_t *s=c->side?lines[c->line].backsector:lines[c->line].frontsector;
            if(exitDepth>0){int other=P_GlobalDigBreachSector(c->line,0,0,(float)-ty,(float)tx);if(other>=0&&sectors[other].floorheight>s->floorheight)s=&sectors[other];}
            *owner=(int)(s-sectors);
        }
    }return 1;
}
// A closed backing sector is not the vertical space of its excavated face.
// Recover a player trapped by an older invalid support height to the nearby
// mouth of that same crater, only after normal collision validates the spot.
static void recoverCraterSupport(mobj_t *thing){
    if(!P_GlobalDigEnabled()||!thing->player||(thing->flags&MF_NOCLIP)||!thing->subsector)return;
    sector_t *room=thing->subsector->sector;
    if(room->ceilingheight-room->floorheight>=thing->height)return;
    int owner=-1;
    if(thing->z>=room->floorheight&&P_GlobalDigPosition(thing,thing->x,thing->y,&owner)&&owner>=0)return;
    if(thing->z>=room->floorheight&&thing->ceilingz-thing->floorz>=thing->height)return;
    fixed_t oldz=thing->z;
    for(int i=0;i<count;++i){
        cut_t *c=&cuts[i];sector_t *front=c->side?lines[c->line].backsector:lines[c->line].frontsector;
        sector_t *back=c->side?lines[c->line].frontsector:lines[c->line].backsector;
        if(back!=room||!front||front->ceilingheight-front->floorheight<thing->height)continue;
        double ax,ay,tx,ty,len=frame(c->line,c->side,&ax,&ay,&tx,&ty);
        double u=(unit(thing->x)-ax)*tx+(unit(thing->y)-ay)*ty;
        double d=-(unit(thing->x)-ax)*ty+(unit(thing->y)-ay)*tx;
        if(u<0||u>len||d<0||d>80)continue;
        fixed_t x=(fixed_t)((ax+u*tx+ty*(unit(thing->radius)+2))*FRACUNIT);
        fixed_t y=(fixed_t)((ay+u*ty-tx*(unit(thing->radius)+2))*FRACUNIT);
        if(R_PointInSubsector(x,y)->sector!=front)continue;
        thing->z=front->floorheight;
        if(P_CheckPosition(thing,x,y)&&tmceilingz-tmfloorz>=thing->height){
            fixed_t floor=tmfloorz,ceiling=tmceilingz;
            P_UnsetThingPosition(thing);thing->x=x;thing->y=y;P_SetThingPosition(thing);
            thing->floorz=floor;thing->ceilingz=ceiling;thing->z=floor;
            thing->momx=thing->momy=thing->momz=0;
            thing->player->viewheight=VIEWHEIGHT;thing->player->deltaviewheight=0;
            thing->player->viewz=thing->z+VIEWHEIGHT;return;
        }
        thing->z=oldz;
    }
}
void P_GlobalDigMonsterContact(mobj_t *rocket,fixed_t x,fixed_t y){
    if(!P_GlobalDigEnabled()||missileDigStrength(rocket)<=0)return;
    monsterMissile=rocket;monsterX=x/(float)FRACUNIT;monsterY=y/(float)FRACUNIT;monsterZ=(rocket->z+rocket->height/2)/(float)FRACUNIT;
}
/* World-only occlusion: the struck monster does not shield the ground. */
static int blastVisible(float x,float y,float z,float ex,float ey,float ez,int ignore){
    double dx=ex-x,dy=ey-y;
    for(int i=0;i<numlines;++i){if(i==ignore)continue;
        line_t *l=&lines[i];double ax=unit(l->v1->x)-x,ay=unit(l->v1->y)-y,sx=unit(l->dx),sy=unit(l->dy),det=dx*sy-dy*sx;
        if(fabs(det)<1e-8)continue;
        double t=(ax*sy-ay*sx)/det,u=(ax*dy-ay*dx)/det;
        if(t<=.001||t>=.999||u<0||u>1)continue;
        double h=z+t*(ez-z);
        if(!l->frontsector||!l->backsector||h<=fmax(unit(l->frontsector->floorheight),unit(l->backsector->floorheight))||h>=fmin(unit(l->frontsector->ceilingheight),unit(l->backsector->ceilingheight)))return 0;
    }return 1;
}
static int blastCrater(float x,float y,float z){
    unsigned before=revision;
    sector_t *room=R_PointInSubsector((fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT))->sector;
    for(int ceiling=0;ceiling<2;++ceiling){float plane=(ceiling?room->ceilingheight:room->floorheight)/(float)FRACUNIT,dist=fabsf(z-plane);
        if(dist>=64||!P_GlobalDigSolidFlat((int)(room-sectors),ceiling))continue;
        impactStrength=4*(1-dist/64);spreadFlatCuts((int)(room-sectors),ceiling,x,y);
    }
    for(int i=0;i<numlines;++i)for(int side=0;side<2;++side){if(lines[i].sidenum[side]<0)continue;
        double ax,ay,tx,ty,len=frame(i,side,&ax,&ay,&tx,&ty);if(!len)continue;
        double u=fmax(0,fmin(len,(x-ax)*tx+(y-ay)*ty)),d=-(x-ax)*ty+(y-ay)*tx;
        double ex=ax+u*tx,ey=ay+u*ty,dist=hypot(x-ex,y-ey);
        if(d>0||dist>=64||!solid(i,side,z)||!blastVisible(x,y,z,(float)ex,(float)ey,z,i))continue;
        impactStrength=4*(1-(float)dist/64);spreadWallCut(i,side,(float)u,z,0);
    }
    impactStrength=4;return before!=revision;
}
void P_GlobalDigBarrelBlast(mobj_t *barrel){
    if(!P_GlobalDigEnabled()||!barrel||barrel->type!=MT_BARREL)return;
    blastCrater(unit(barrel->x),unit(barrel->y),unit(barrel->z)+unit(barrel->height)*.5f);
}
int P_GlobalDigImpact(mobj_t *rocket){
    if(monsterMissile==rocket){monsterMissile=NULL;return P_GlobalDigEnabled()&&rocket->type==MT_ROCKET?blastCrater(monsterX,monsterY,monsterZ):0;}
    if(flatMissile==rocket){flatMissile=NULL;return flatChanged;}
    if(!P_GlobalDigEnabled()||missileDigStrength(rocket)<=0)return 0;
    double vx=unit(rocket->momx),vy=unit(rocket->momy),vz=unit(rocket->momz),speed=sqrt(vx*vx+vy*vy+vz*vz);
    if(speed<.01)return 0;vx/=speed;vy/=speed;vz/=speed;
    double x=unit(rocket->x),y=unit(rocket->y),z=unit(rocket->z)+unit(rocket->height)*.5;
    int bestline=-1,bestside=0;double best=80,bu=0,bz=0;
    if(blockedMissile==rocket){bestline=blockedLine;bestside=blockedSide;bu=blockedU;bz=blockedZ;best=0;blockedMissile=NULL;goto apply_cut;}
    for(int i=0;i<numlines;++i)for(int side=0;side<2;++side){
        if(lines[i].sidenum[side]<0)continue;
        double ax,ay,tx,ty,len=frame(i,side,&ax,&ay,&tx,&ty);if(!len)continue;
        double approach=-vx*ty+vy*tx;if(approach<=.001)continue;
        double initial=-(x-ax)*ty+(y-ay)*tx;
        if(initial>(P_GlobalDigThroughEnabled()?P_GlobalDigMaxDepth()+16:80)||initial+80*approach<0)continue;
        if(initial>0){double u=(x-ax)*tx+(y-ay)*ty;
            if(initial>P_GlobalDigDepth(i,side,(float)u,(float)z)+.2)continue;
        }
        for(double t=0;t<best;t+=.5){
            double u=(x+vx*t-ax)*tx+(y+vy*t-ay)*ty,d=initial+approach*t,zz=z+vz*t;
            if(d<0||u<0||u>len||!solid(i,side,zz))continue;
            if(d+.1<P_GlobalDigDepth(i,side,(float)u,(float)zz))continue;
            best=t;bestline=i;bestside=side;bu=u;bz=zz;break;
        }
    }
    // A grazing hit can contact the missile's box before its center ray
    // meets the wall. Resolve that contact locally, in the wall's own frame.
    if(bestline<0){double closest=1e9;
        for(int i=0;i<numlines;++i)for(int side=0;side<2;++side){
            if(lines[i].sidenum[side]<0)continue;
            double ax,ay,tx,ty,len=frame(i,side,&ax,&ay,&tx,&ty);if(!len)continue;
            double approach=-vx*ty+vy*tx;if(approach<=.001)continue;
            double d=-(x-ax)*ty+(y-ay)*tx,u=(x-ax)*tx+(y-ay)*ty;
            double radius=unit(rocket->radius)*(fabs(tx)+fabs(ty));
            if(d>0||-d>radius+speed*approach||u<0||u>len||!solid(i,side,z))continue;
            if(-d<closest){closest=-d;bestline=i;bestside=side;bu=u;bz=z;best=0;}
        }
    }
apply_cut:
    /* Do not carve a wall remotely when the actual impact was a monster. */
    if(bestline<0||best>speed+unit(rocket->radius)+8)return 0;
    if(count==MAX_CUTS){players[consoleplayer].message="EXCAVATION PATCH LIMIT REACHED";return 0;}
    impactStrength=missileDigStrength(rocket);impactProfile=missileProfile(rocket);
    spreadWallCut(bestline,bestside,(float)bu,(float)bz,1);
    if(impactProfile&&R_SurfaceChip){double ax,ay,tx,ty;frame(bestline,bestside,&ax,&ay,&tx,&ty);R_SurfaceChip((float)(ax+bu*tx+ty*2),(float)(ay+bu*ty-tx*2),(float)bz,(float)ty,(float)-tx,0);}
    impactStrength=4;impactProfile=0;
    return 1;
}
/* Only firearm hits reaching world geometry carve: never melee, actor hits,
 * monster hitscan fire or the BFG's actor-only spray. */
static int playerBullet(mobj_t *shooter){
    if(!P_GlobalDigEnabled()||!p_player_surface_damage||!shooter||!shooter->player)return 0;
    weapontype_t weapon=shooter->player->readyweapon;
    return weapon==wp_pistol||weapon==wp_chaingun||weapon==wp_shotgun||weapon==wp_supershotgun;
}
void P_GlobalDigBulletWall(mobj_t *shooter,int line,int side,fixed_t x,fixed_t y,fixed_t z){
    if(!playerBullet(shooter)||line<0||line>=numlines||side<0||side>1||lines[line].sidenum[side]<0)return;
    double ax,ay,tx,ty,len=frame(line,side,&ax,&ay,&tx,&ty);
    float u=(float)((unit(x)-ax)*tx+(unit(y)-ay)*ty);
    if(u<0||u>len)return;
    impactStrength=.08f;impactProfile=1;spreadWallCut(line,side,u,(float)unit(z),1);impactStrength=4;impactProfile=0;
    if(R_SurfaceChip)R_SurfaceChip((float)(unit(x)+ty*2),(float)(unit(y)-tx*2),(float)unit(z),(float)ty,(float)-tx,0);
}
void P_GlobalDigBulletFlat(mobj_t *shooter,int sector,int ceiling,fixed_t x,fixed_t y){
    if(!playerBullet(shooter)||!P_GlobalDigSolidFlat(sector,ceiling))return;
    impactStrength=.08f;impactProfile=1;spreadFlatCuts(sector,ceiling,(float)unit(x),(float)unit(y));
    spreadFlatToWalls(sector,ceiling,(float)unit(x),(float)unit(y));impactStrength=4;impactProfile=0;
    if(R_SurfaceChip)R_SurfaceChip((float)unit(x),(float)unit(y),(float)unit(ceiling?sectors[sector].ceilingheight:sectors[sector].floorheight)+(ceiling?-2:2),0,0,ceiling?-1:1);
}
#include "p_volume_export.inc"
int P_GlobalDigSaveSize(void){return 12+count*(int)sizeof(cut_t)+12+flatCount*(int)sizeof(flatcut_t)+8+count*(int)sizeof(cutanchor_t)+4+count+flatCount;}
int P_GlobalDigSave(unsigned char *data){
    memcpy(data,"DIG2",4);memcpy(data+4,&numlines,4);memcpy(data+8,&count,4);memcpy(data+12,cuts,count*sizeof(cut_t));
    unsigned char *f=data+12+count*sizeof(cut_t);memcpy(f,"FLT1",4);memcpy(f+4,&numsectors,4);memcpy(f+8,&flatCount,4);memcpy(f+12,flatCuts,flatCount*sizeof(flatcut_t));
    unsigned char *a=f+12+flatCount*sizeof(flatcut_t);memcpy(a,"ANC1",4);memcpy(a+4,&count,4);memcpy(a+8,cutAnchors,count*sizeof(cutanchor_t));
    unsigned char *profiles=a+8+count*sizeof(cutanchor_t);memcpy(profiles,"CHP1",4);memcpy(profiles+4,cutProfile,count);memcpy(profiles+4+count,flatProfile,flatCount);
    return P_GlobalDigSaveSize();
}
void P_GlobalDigLoad(const unsigned char *data,int length){
    int n,ln;P_GlobalDigReset();if(length<12||memcmp(data,"DIG2",4))return;
    memcpy(&ln,data+4,4);memcpy(&n,data+8,4);if(ln!=numlines||n<0||n>MAX_CUTS||length<12+n*(int)sizeof(cut_t))return;
    for(int i=0;i<n;++i){cut_t c;memcpy(&c,data+12+i*sizeof(cut_t),sizeof(c));
        if(c.line<0||c.line>=numlines||c.side<0||c.side>1||lines[c.line].sidenum[c.side]<0||!isfinite(c.u)||!isfinite(c.z)||!isfinite(c.strength)||c.strength<0||c.strength>16384)return;
    }
    count=n;memcpy(cuts,data+12,n*sizeof(cut_t));for(int i=0;i<n;++i){cutAnchors[i]=(cutanchor_t){-1,0,0};cutVersions[i]=revision+1;}++revision;
    int offset=12+n*(int)sizeof(cut_t),sectorsSaved,fc;
    if(length-offset<12||memcmp(data+offset,"FLT1",4))return;
    memcpy(&sectorsSaved,data+offset+4,4);memcpy(&fc,data+offset+8,4);
    if(sectorsSaved!=numsectors||fc<0||fc>MAX_FLAT_CUTS||length-offset<12+fc*(int)sizeof(flatcut_t))return;
    for(int i=0;i<fc;++i){flatcut_t c;memcpy(&c,data+offset+12+i*sizeof(c),sizeof(c));
        if(c.sector<0||c.sector>=numsectors||c.ceiling<0||c.ceiling>1||!isfinite(c.x)||!isfinite(c.y)||!isfinite(c.strength)||c.strength<0||c.strength>256)return;
    }
    flatCount=fc;for(int i=0;i<fc;++i)flatVersions[i]=revision;memcpy(flatCuts,data+offset+12,fc*sizeof(flatcut_t));
    offset+=12+fc*(int)sizeof(flatcut_t);int anchors=0;
    if(length-offset<8||memcmp(data+offset,"ANC1",4))return;
    memcpy(&anchors,data+offset+4,4);if(anchors!=count||length-offset<8+count*(int)sizeof(cutanchor_t))return;
    for(int i=0;i<count;++i){cutanchor_t a;memcpy(&a,data+offset+8+i*sizeof(a),sizeof(a));if(a.sector < -1||a.sector>=numsectors||a.plane<0||a.plane>1||!isfinite(a.base))return;}
    memcpy(cutAnchors,data+offset+8,count*sizeof(cutanchor_t));
    offset+=8+count*sizeof(cutanchor_t);
    if(length-offset>=4+count+flatCount&&!memcmp(data+offset,"CHP1",4)){
        for(int i=0;i<count+flatCount;++i)if(data[offset+4+i]>2)return;
        memcpy(cutProfile,data+offset+4,count);memcpy(flatProfile,data+offset+4+count,flatCount);
    }
}



