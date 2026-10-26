/* Sprite shadows: things' shadows from the sun or a strong light, and
   silhouettes the flashlight throws on the wall behind. */
#include "internal.h"

namespace doom3d {
// The strongest visible shot or projectile supplies a world-space light here.
// Contact shadows stay separate and remain after the transient shadow fades.
void addEnemyShadow(const mobj_t &thing,const Image &image,int lump,bool flip,
                    float x,float y,float z,const Uniforms &camera,const ShadowLight *light,bool anyThing,const RagdollCard *card) {
    if(!anyThing&&!(thing.info->flags&MF_COUNTKILL)&&Game_ThingRole(thing.type)!=GAME_THING_UNCOUNTED_MONSTER) return;
    float u0=0,v0=0,u1=image.width,v1=image.height;
    if(card) {
        if(card->u1<=card->u0||card->v1<=card->v0)return;
        u0=card->u0;v0=card->v0;u1=card->u1;v1=card->v1;
        float centre=(u0+u1)*0.5f-card->half;
        x+=camera.right[0]*centre;y+=camera.right[1]*centre;z=card->top-v1;
    }
    const sector_t *sector=card?R_PointInSubsector((fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT))->sector:thing.subsector->sector;
    if(sector->floorpic==skyflatnum) return;
    float floor=floorZ(sector),height=std::max(0.0f,z-floor);
    float opacity=0.28f*lighting(sector)/(1.0f+height/48.0f);
    if(opacity<0.015f) return;
    float width=std::clamp(units(thing.radius)*2.0f,20.0f,80.0f)*std::min(1.7f,1.0f+height/160.0f);
    // A settled corpse lies on the floor. Projecting its gameplay collision
    // height from low pool lights stretched it into long rectangular bands.
    bool prone=(thing.flags&MF_CORPSE)&&thing.tics==-1;
    float baseLength=width*(prone?0.2f:0.65f);
    float casterHeight=prone?std::min(4.0f,image.height*0.15f):units(thing.height);
    float length=baseLength,start=-length*0.35f;
    Point across={camera.right[0],camera.right[1]},away={-across.y,across.x};
    if(light) {
        float dx=x-light->x,dy=y-light->y,distance=std::hypot(dx,dy);
        if(distance>1.0f&&light->z>z+8.0f) {
            away={dx/distance,dy/distance};across={away.y,-away.x};
            length=std::clamp(distance*casterHeight/(light->z-z),baseLength,prone?width:192.0f);
            start=0;opacity=std::clamp(light->strength*0.55f,0.0f,0.65f);
        }
    }
    auto point=[&](float u,float v) {return Point{x+across.x*u+away.x*v,y+across.y*u+away.y*v};};
    std::array<Point,4> footprint={point(-width/2,start),point(width/2,start),
                                  point(width/2,start+length),point(-width/2,start+length)};
    std::array<float,4> bounds={1e9f,1e9f,-1e9f,-1e9f};
    for(Point p:footprint) {
        bounds[0]=std::min(bounds[0],p.x);bounds[1]=std::min(bounds[1],p.y);
        bounds[2]=std::max(bounds[2],p.x);bounds[3]=std::max(bounds[3],p.y);
    }
    auto &out=shadowBatches[lump];
    for(int leaf:sectorFloors[sector-sectors]) {
        const auto &b=floorBounds[leaf];
        if(b[2]<bounds[0]||b[0]>bounds[2]||b[3]<bounds[1]||b[1]>bounds[3]) continue;
        const auto &receiver=floors[leaf];if(receiver.size()<3) continue;
        // Render-thread scratch; retained capacity avoids allocating four
        // new clipping buffers for each receiver of every sprite shadow.
        static std::vector<Point> polygon,scratch;
        polygon.assign(receiver.begin(),receiver.end());
        for(size_t n=0;n<footprint.size()&&!polygon.empty();++n) {
            Point a=footprint[n],b=footprint[(n+1)%footprint.size()];
            clipInto(polygon,a,{b.x-a.x,b.y-a.y},false,scratch);polygon.swap(scratch);
        }
        auto vertex=[&](Point p) {
            float dx=p.x-x,dy=p.y-y;
            float u=(dx*across.x+dy*across.y)/width+0.5f;
            float v=1.0f-((dx*away.x+dy*away.y)-start)/length;
            // Twice the blood's lift, so shadows fall on top of blood decals.
            return Vertex{p.x,p.y,floor+2*decalLift(p.x,p.y,camera),u0+(flip?1-u:u)*(u1-u0),v0+v*(v1-v0),opacity,0};
        };
        for(size_t n=1;n+1<polygon.size();++n)
            for(Point p:{polygon[0],polygon[n],polygon[n+1]}) out.push_back(vertex(p));
    }
}

// Flashlight silhouettes: the beam's light sits right of the eye
// (collectFlashes), so a thing it catches throws its sprite's outline onto
// the wall behind it, enlarged with distance. The
// outline is projected through a grid of cells clipped to the wall's span
// and drawn hard-edged.
struct WallPoint { float s,z,u,v; };
static std::vector<WallPoint> clipWallPolygon(const std::vector<WallPoint> &polygon,bool height,float limit,bool above) {
    std::vector<WallPoint> out;if(polygon.empty())return out;
    auto inside=[&](const WallPoint &p){float value=height?p.z:p.s;return above?value>=limit:value<=limit;};
    WallPoint a=polygon.back();
    for(const WallPoint &b:polygon) {
        if(inside(a)!=inside(b)) {
            float va=height?a.z:a.s,vb=height?b.z:b.s,t=(limit-va)/(vb-va);
            out.push_back({a.s+(b.s-a.s)*t,a.z+(b.z-a.z)*t,a.u+(b.u-a.u)*t,a.v+(b.v-a.v)*t});
        }
        if(inside(b))out.push_back(b);
        a=b;
    }
    return out;
}
void flashlightSilhouette(const mobj_t &thing,const Image &image,int lump,bool flip,float x,float y,float z,const Uniforms &camera,const Flash &beam,const RagdollCard *card) {
    if(card&&(card->u1<=card->u0||card->v1<=card->v0))return;
    const float *lamp=beam.position;
    float cz=card?card->top-(card->v0+card->v1)*0.5f:z+units(thing.height)*0.5f;
    float dx=x-lamp[0],dy=y-lamp[1],horizontal=std::hypot(dx,dy);
    if(horizontal<16)return;
    float ex=x-camera.eye[0],ey=y-camera.eye[1],ez=cz-camera.eye[2],distance=std::sqrt(ex*ex+ey*ey+ez*ez);
    if(distance>700||(ex*camera.forward[0]+ey*camera.forward[1]+ez*camera.forward[2])/distance<beam.direction[3])return;
    float ux=dx/horizontal,uy=dy/horizontal,slope=(cz-lamp[2])/horizontal;
    WallHit hit;WallSpan span;
    if(!firstSolidWall(x,y,cz,ux,uy,slope,384,hit)||!wallSpan(lines[hit.line],hit.side,hit.section,span))return;
    float reach=flashAt(x+ux*(hit.t-2),y+uy*(hit.t-2),cz+slope*(hit.t-2),beam)/beam.strength;
    float opacity=std::min(0.6f,reach*1.4f)*std::clamp(1.25f-lighting(sides[lines[hit.line].sidenum[hit.side]].sector),0.3f,1.0f);
    if(opacity<0.03f)return;
    float sx=span.b.x-span.a.x,sy=span.b.y-span.a.y,len=std::hypot(sx,sy);if(len<1)return;
    float tx=sx/len,ty=sy/len,nx=ty,ny=-tx;
    float planeDistance=(span.a.x-lamp[0])*nx+(span.a.y-lamp[1])*ny;
    float rx=camera.right[0],ry=camera.right[1];
    float u0=card?card->u0:0,v0=card?card->v0:0,u1=card?card->u1:image.width,v1=card?card->v1:image.height;
    float left=card?u0-card->half:-(float)image.left,right=left+(u1-u0);
    float top=card?card->top-v0:z+image.top,bottom=top-(v1-v0);
    constexpr int columns=6,rows=8;
    std::array<WallPoint,(columns+1)*(rows+1)> grid;std::array<bool,(columns+1)*(rows+1)> valid;
    for(int j=0;j<=rows;++j)for(int i=0;i<=columns;++i) {
        float across=left+(right-left)*i/columns,height=top-(top-bottom)*j/rows;
        float px=x+rx*across-lamp[0],py=y+ry*across-lamp[1],pz=height-lamp[2];
        float toward=px*nx+py*ny,scale=toward<-1e-3f?planeDistance/toward:-1;
        int k=j*(columns+1)+i;valid[k]=scale>=1;
        float qx=lamp[0]+px*scale,qy=lamp[1]+py*scale;
        grid[k]={(qx-span.a.x)*tx+(qy-span.a.y)*ty,lamp[2]+pz*scale,u0+(flip?1-(float)i/columns:(float)i/columns)*(u1-u0),v0+(float)j/rows*(v1-v0)};
    }
    float lift=2*decalLift(span.a.x+tx*len*0.5f,span.a.y+ty*len*0.5f,camera)+0.6f; // Above wall blood.
    auto &out=decalBatches[lump];
    unsigned mode=MODE_HARD_SILHOUETTE|(settings.stippledShadows?MODE_STIPPLED:0);
    for(int j=0;j<rows;++j)for(int i=0;i<columns;++i) {
        int k[4]={j*(columns+1)+i,j*(columns+1)+i+1,(j+1)*(columns+1)+i+1,(j+1)*(columns+1)+i};
        if(!valid[k[0]]||!valid[k[1]]||!valid[k[2]]||!valid[k[3]])continue;
        std::vector<WallPoint> cell={grid[k[0]],grid[k[1]],grid[k[2]],grid[k[3]]};
        cell=clipWallPolygon(cell,false,0,true);cell=clipWallPolygon(cell,false,len,false);
        cell=clipWallPolygon(cell,true,span.bottom,true);cell=clipWallPolygon(cell,true,span.top,false);
        auto vertex=[&](const WallPoint &p){return Vertex{span.a.x+tx*p.s+nx*lift,span.a.y+ty*p.s+ny*lift,p.z,p.u,p.v,opacity,mode};};
        for(size_t n=1;n+1<cell.size();++n)for(size_t m:{size_t(0),n,n+1})out.push_back(vertex(cell[m]));
    }
}
} // namespace doom3d
