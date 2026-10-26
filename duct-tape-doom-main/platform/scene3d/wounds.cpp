/* Wound holes: where hitscan shots strike a monster, its sprite is shot
   through. Drawing only: the game's hitboxes are untouched. */
#include "internal.h"
#include "../wound_attachment.h"

namespace doom3d {
// Capture the texel struck before damage switches the monster to its pain
// or death drawing. Other frames attach to that original patch of artwork,
// never to the previous frame (which would accumulate drift). The tunnel
// follows the shot in the monster's local coordinates, including side hits.
struct Wound {
    float height,across,radius,yaw;
    const Image *source;
    doom_wounds::Point point;
    bool flipped;
    uint32_t seed;
    std::unordered_map<const Image*,doom_wounds::Point> frames;
    int tic=0;
};
static std::unordered_map<const mobj_t*,std::vector<Wound>> wounds;
static std::unordered_map<int,float> standing;
struct WoundSurface {
    const mobj_t *viewer;
    const Image *image;
    float x,y,feet,top,bottom,rx,ry,yaw;
    BodyPose pose;
    int tic;
    bool flipped,posed;
};
static std::unordered_map<const mobj_t*,WoundSurface> surfaces;
// A lying drawing's body: its opaque columns, the middle row of each, and which end the head is.
struct Lying { int first=-1,last=-1;bool headRight=true;std::vector<float> middle; };
static std::unordered_map<const Image*,Lying> lyings;
constexpr size_t maxWounds=10;
constexpr float maxRadius=3.0f;

void woundView(const mobj_t *thing,const mobj_t *viewer,const Image &image,bool flipped,float x,float y,float feet,
               float top,float bottom,float rx,float ry,const BodyPose *pose) {
    if(!settings.wounds||!thing||!thing->info||thing->health<=0)return;
    if(!(thing->info->flags&MF_COUNTKILL)&&thing->type!=MT_SKULL)return;
    surfaces[thing]={viewer,&image,x,y,feet,top,bottom,rx,ry,
        (float)(thing->angle*(2*doomPi/4294967296.0)),pose?*pose:BodyPose{},leveltime,flipped,pose!=nullptr};
}
static bool opaqueAt(const Image &image,doom_wounds::Point point) {
    int c=(int)std::floor(point.u),r=(int)std::floor(point.v);
    return c>=0&&r>=0&&c<image.width&&r<image.height&&image.pixels[((size_t)r*image.width+c)*2+1];
}
// Recreate the uncut surface only when a shot arrives. Its triangles carry
// the same UVs as the drawn strips, so intersection also inverts flinching.
static bool surfaceHit(const WoundSurface &surface,const mobj_t &shooter,float x,float y,float z,
                       float dx,float dy,float slope,doom_wounds::Point &point) {
    const Image &image=*surface.image;
    float left=-(float)image.left,right=left+image.width;
    float u0=surface.flipped?(float)image.width:0,u1=surface.flipped?0:(float)image.width;
    std::vector<Vertex> vertices;
    if(surface.posed)
        bodyQuads(vertices,surface.pose,{},surface.x,surface.y,surface.feet,surface.rx,surface.ry,
                  left,right,image.top,image.height,u0,u1);
    else
        quad(vertices,{surface.x+surface.rx*left,surface.y+surface.ry*left,surface.top,u0,0},
                      {surface.x+surface.rx*right,surface.y+surface.ry*right,surface.top,u1,0},
                      {surface.x+surface.rx*right,surface.y+surface.ry*right,surface.bottom,u1,(float)image.height},
                      {surface.x+surface.rx*left,surface.y+surface.ry*left,surface.bottom,u0,(float)image.height});
    // Start at the shooter, rather than the puff: a flinching body can bend
    // toward the eye far enough to be in front of the game's blood point.
    float sx=units(shooter.x),sy=units(shooter.y),travel=(x-sx)*dx+(y-sy)*dy;
    using Vec=std::array<float,3>;
    Vec origin={sx,sy,z-travel*slope},direction={dx,dy,slope};
    auto cross=[](const Vec &a,const Vec &b)->Vec {return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};};
    auto dot=[](const Vec &a,const Vec &b) {return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];};
    float nearest=INFINITY;bool hit=false;
    for(size_t i=0;i+2<vertices.size();i+=3) {
        const Vertex &a=vertices[i],&b=vertices[i+1],&c=vertices[i+2];
        Vec e1={b.x-a.x,b.y-a.y,b.z-a.z},e2={c.x-a.x,c.y-a.y,c.z-a.z};
        Vec p=cross(direction,e2);float determinant=dot(e1,p);
        if(std::abs(determinant)<1e-6f)continue;
        Vec offset={origin[0]-a.x,origin[1]-a.y,origin[2]-a.z};
        float u=dot(offset,p)/determinant;
        if(u<0||u>1)continue;
        Vec q=cross(offset,e1);float v=dot(direction,q)/determinant;
        if(v<0||u+v>1)continue;
        float t=dot(e2,q)/determinant;
        if(t<0||t>=nearest)continue;
        doom_wounds::Point at={a.u+(b.u-a.u)*u+(c.u-a.u)*v,a.v+(b.v-a.v)*u+(c.v-a.v)*v};
        if(!opaqueAt(image,at))continue;
        nearest=t;point=at;hit=true;
    }
    return hit;
}

static doom_wounds::Frame art(const Image &image) {
    return {image.width,image.height,image.left,image.top,image.pixels.data()};
}
static doom_wounds::Point attached(Wound &wound,const Image &image) {
    if(wound.source==&image)return wound.point;
    auto found=wound.frames.find(&image);
    if(found!=wound.frames.end())return found->second;
    return wound.frames[&image]=doom_wounds::map(art(*wound.source),art(image),wound.point);
}
// Project an attached patch from its entry side to the current billboard.
static doom_wounds::Point projected(Wound &wound,const Image &image,bool flipped,float facing) {
    auto point=attached(wound,image);
    float left=-(float)image.left,right=left+image.width;
    float across=wound.flipped?right-point.u:left+point.u;
    point.u=flipped?right-across*facing:across*facing-left;
    return point;
}

void woundHit(mobj_t *target,fixed_t fx,fixed_t fy,fixed_t fz,fixed_t fdx,fixed_t fdy,fixed_t slope,int damage,const mobj_t *shooter) {
    if(!settings.wounds||!target||!target->info)return;
    if(!(target->info->flags&MF_COUNTKILL)&&target->type!=MT_SKULL)return;
    if(target->sprite<0||target->sprite>=numsprites)return;
    const auto &sprite=sprites[target->sprite];int frame=target->frame&FF_FRAMEMASK;
    if(frame>=sprite.numframes)return;
    float dx=units(fdx),dy=units(fdy),length=std::hypot(dx,dy);
    if(length<1e-4f)return;
    dx/=length;dy/=length;
    float viewYaw=std::atan2(dy,dx),sourceYaw;
    float a=(float)(target->angle*(2*doomPi/4294967296.0));
    const Image *source=nullptr;bool flipped=false;doom_wounds::Point point={};
    auto drawn=surfaces.find(target);
    if(shooter&&drawn!=surfaces.end()&&drawn->second.viewer==shooter&&leveltime-drawn->second.tic<=2) {
        const WoundSurface &surface=drawn->second;
        if(!surfaceHit(surface,*shooter,units(fx),units(fy),units(fz),dx,dy,units(slope),point))return;
        source=surface.image;flipped=surface.flipped;sourceYaw=surface.yaw;
        viewYaw=std::atan2(surface.rx,-surface.ry);
    } else {
        // Off-screen hits use the attacker's view. Frame selection is based
        // on the bearing to the body's center, not an off-center pellet ray.
        float bearing=shooter?std::atan2(units(target->y)-units(shooter->y),units(target->x)-units(shooter->x)):viewYaw;
        angle_t angle=(angle_t)(int64_t)(bearing*(4294967296.0/(2*doomPi)));
        const auto &sf=sprite.spriteframes[frame];
        unsigned rotation=sf.rotate?(angle-target->angle+(unsigned)(ANG45/2)*9)>>29:0;
        source=&lumpImage(firstspritelump+sf.lump[rotation],false);flipped=sf.flip[rotation];sourceYaw=a;
        if(source->pixels.empty())return;
        if(shooter&&shooter->player)viewYaw=(float)(shooter->angle*(2*doomPi/4294967296.0));
        float nx=std::cos(viewYaw),ny=std::sin(viewYaw),along=dx*nx+dy*ny;
        if(std::abs(along)<1e-4f)return;
        float ox=units(fx)-units(target->x),oy=units(fy)-units(target->y),t=-(ox*nx+oy*ny)/along;
        float height=units(fz)-units(target->z)+t*units(slope);
        float across=(ox+dx*t)*ny-(oy+dy*t)*nx;
        float left=-(float)source->left,right=left+source->width;
        point={flipped?right-across:across-left,source->top-height};
    }
    const Image &image=*source;
    if(!opaqueAt(image,point))return;
    float height=image.top-point.v,across=flipped?image.width-image.left-point.u:point.u-image.left;
    uint32_t seed=hash32((uint32_t)std::lround(point.u*16)*73856093u^(uint32_t)std::lround(point.v*16)*19349663u^
                         (uint32_t)leveltime*83492791u^(uint32_t)target->type*2654435761u);
    Wound wound={height,across,std::clamp(0.7f+damage/14.0f,0.8f,1.8f),viewYaw-sourceYaw,&image,point,flipped,seed,{},leveltime};
    auto &list=wounds[target];
    // Compare holes on this drawing, so hits on a moving limb still merge.
    auto distance=[&](Wound &w) {
        float facing=std::cos(w.yaw-wound.yaw);
        if(facing<0.8f)return 1e9f;
        auto at=projected(w,image,flipped,facing);
        return std::hypot(at.u-point.u,at.v-point.v);
    };
    for(Wound &w:list) {
        if(distance(w)<(w.radius+wound.radius)*0.9f) {
            auto at=projected(w,image,flipped,std::cos(w.yaw-wound.yaw));
            doom_wounds::Point middle={(at.u+point.u)*0.5f,(at.v+point.v)*0.5f};
            if(!opaqueAt(image,middle))continue;
            wound.radius=std::min(maxRadius,std::sqrt(w.radius*w.radius+wound.radius*wound.radius));
            wound.point=middle;wound.height=image.top-middle.v;
            wound.across=flipped?image.width-image.left-middle.u:middle.u-image.left;
            wound.seed=w.seed;
            w=std::move(wound);
            return;
        }
    }
    if(list.size()>=maxWounds) {
        auto nearest=std::min_element(list.begin(),list.end(),[&](Wound &p,Wound &q) {return distance(p)<distance(q);});
        nearest->radius=std::min(maxRadius,nearest->radius+0.3f);nearest->tic=leveltime;
        return;
    }
    list.push_back(wound);
}
void forgetWounds(const mobj_t *thing) {wounds.erase(thing);surfaces.erase(thing);}
int woundSpots(const mobj_t *thing,WoundSpot *out,int most) {
    auto found=wounds.find(thing);
    if(found==wounds.end())return 0;
    int count=0;
    for(const Wound &w:found->second) {
        if(count>=most)break;
        out[count++]={w.height,w.across,w.yaw,w.radius,w.tic};
    }
    return count;
}
void woundsShutdown() {wounds.clear();surfaces.clear();lyings.clear();standing.clear();}

// How tall a monster stands, by its first drawing, to tell lying death drawings.
static float standingTop(const mobj_t &thing) {
    auto found=standing.find(thing.type);
    if(found!=standing.end())return found->second;
    float top=0;
    int state=thing.info->spawnstate;
    if(state>0&&states[state].sprite>=0&&states[state].sprite<numsprites) {
        const auto &sprite=sprites[states[state].sprite];int frame=states[state].frame&FF_FRAMEMASK;
        if(frame<sprite.numframes)top=(float)lumpImage(firstspritelump+sprite.spriteframes[frame].lump[0],false).top;
    }
    return standing[thing.type]=top;
}

static const Lying &lyingOf(const Image &image) {
    auto found=lyings.find(&image);
    if(found!=lyings.end())return found->second;
    Lying &l=lyings[&image];
    int w=image.width,h=image.height;
    l.middle.assign(w,-1);
    double upper=0,lower=0;int upperCount=0,lowerCount=0;
    for(int c=0;c<w;++c) {
        int top=-1,bottom=-1;
        for(int r=0;r<h;++r) if(image.pixels[((size_t)r*w+c)*2+1]) {
            if(top<0)top=r;bottom=r;
            if(r<h/2){upper+=c;++upperCount;}else{lower+=c;++lowerCount;}
        }
        if(top<0)continue;
        if(l.first<0)l.first=c;l.last=c;
        l.middle[c]=(top+bottom+1)*0.5f;
    }
    l.headRight=!upperCount||!lowerCount||upper/upperCount>=lower/lowerCount;
    return l;
}

// Wound-local marks, held for the wound's lifetime: a narrow dark torn
// edge, ragged red flesh, a few brighter lip texels, and a mottled stain.
// The short trail runs down the source drawing and bends with the body.
static byte woundMark(float x,float y,float radius,uint32_t seed) {
    float d=std::hypot(x,y);
    if(d<=radius)return 1;
    if(d<=radius+0.55f)return 2;
    float angle=std::atan2(y,x),phase=(seed&1023u)*(2*doomPi/1024);
    float edge=radius+1.15f+0.35f*std::sin(3*angle+phase)+0.2f*std::sin(7*angle-phase);
    uint32_t cell=hash32(seed^(uint32_t)(int)std::lround(x)*374761393u^(uint32_t)(int)std::lround(y)*668265263u);
    float grain=(cell&255u)/255.0f;
    if(d<=edge) {
        // A small broken highlight along the upper lip, never a shiny ring.
        return y<-0.25f&&x<0.4f&&d>edge-0.7f&&grain>0.35f?4:3;
    }
    byte stain=d<=edge+0.8f&&grain>0.3f?5:0;
    float length=radius+2.5f+(seed%3u)*0.5f;
    if(y>radius*0.5f&&y<length) {
        float progress=(y-radius*0.5f)/(length-radius*0.5f);
        float middle=((int)((seed>>10)&3u)-1.5f)*0.25f+std::sin(y*0.8f+phase)*0.2f;
        if(std::abs(x-middle)<0.35f*(1-progress)+0.55f)return progress<0.75f?3:5;
    }
    return stain;
}

bool woundMask(const mobj_t &thing,const Image &image,bool flipped,float rx,float ry,std::vector<byte> &mask) {
    auto found=wounds.find(&thing);
    if(!settings.wounds||found==wounds.end()||found->second.empty()||image.pixels.empty())return false;
    bool dead=thing.health<=0;
    // Death drawings face the viewer; lying ones carry the holes along the body.
    float tall=standingTop(thing);
    bool lying=dead&&image.top<0.85f*tall;
    const Lying *body=lying?&lyingOf(image):nullptr;
    if(lying&&body->first<0)return false;
    float a=(float)(thing.angle*(2*doomPi/4294967296.0));
    int w=image.width,h=image.height;
    mask.assign((size_t)w*h,0);
    bool any=false;
    for(Wound &wound:found->second) {
        float facing=std::sin(a+wound.yaw)*rx-std::cos(a+wound.yaw)*ry;
        if(!dead&&std::abs(facing)<0.4f)continue;
        // Procedural bends then carry these source texels through the same
        // bodyQuads/fallQuads deformation as the rest of the drawing.
        auto point=dead?attached(wound,image):projected(wound,image,flipped,facing);
        float cu=point.u,cv=point.v;
        if(lying) {
            float along=std::clamp(wound.height/std::max(1.0f,tall),0.05f,0.95f)*(body->last+1-body->first);
            cu=body->headRight?body->first+along:body->last+1-along;
            int column=std::clamp((int)cu,body->first,body->last);
            if(body->middle[column]<0)continue;
            cv=body->middle[column]-wound.across*0.3f;
        }
        int c0=(int)std::floor(cu),r0=(int)std::floor(cv);
        if(c0<0||r0<0||c0>=w||r0>=h||!image.pixels[((size_t)r0*w+c0)*2+1])continue;   // missed the body here
        int reach=(int)std::ceil(wound.radius+2.5f),down=(int)std::ceil(wound.radius+4);
        float direction=dead?1.0f:((wound.flipped!=flipped)?-1.0f:1.0f)*(facing<0?-1.0f:1.0f);
        for(int r=std::max(0,r0-reach);r<=std::min(h-1,r0+down);++r)
            for(int c=std::max(0,c0-reach);c<=std::min(w-1,c0+reach);++c) {
                size_t i=(size_t)r*w+c;
                if(!image.pixels[i*2+1]||mask[i]==1)continue;
                byte mark=woundMark((c+0.5f-cu)*direction,r+0.5f-cv,wound.radius,wound.seed);
                // Nearby stains combine, but never paint over a hole or its
                // recessed edge. Highlights win over the red lip and stain.
                static const byte priority[6]={0,5,4,2,3,1};
                if(priority[mark]>priority[mask[i]])mask[i]=mark;
                any=any||mark!=0;
            }
    }
    return any;
}

void woundVertex(Vertex &vertex,byte state) {
    if(state<2||state>5)return;
    const auto &shades=bloodPalette();
    if(shades.empty())return;
    static const float shade[6]={0,0,0.02f,0.45f,0.82f,0.22f};
    byte color=shades[(size_t)std::lround(shade[state]*(shades.size()-1))];
    vertex.mode&=((1u<<MODE_WOUND_PALETTE_SHIFT)-1u)&~(MODE_GLOWING_EYES|MODE_WET_SPRITE);
    vertex.mode|=MODE_WOUND|((unsigned)color<<MODE_WOUND_PALETTE_SHIFT);
    if(state==5)vertex.mode|=MODE_WOUND_STAIN;
    else if(settings.bloodShine&&(state==3||state==4))vertex.mode|=MODE_WET_SPRITE;
}

// One texel row cut by the mask: runs of body texels, holes skipped, rim runs
// painted with the wound material. corner(edge, bottom) gives the vertex at the
// texture column edge (0..width) on the row's top or bottom; flip says the
// texture runs right to left on screen.
void maskedRow(std::vector<Vertex> &out,const std::vector<byte> &mask,int width,int r,bool flip,
               const std::function<Vertex(int,bool)> &corner) {
    int c=0;
    while(c<width) {
        byte state=mask[(size_t)r*width+c];
        int end=c+1;
        while(end<width&&mask[(size_t)r*width+end]==state)++end;
        if(state!=1) {
            int a=flip?end:c,b=flip?c:end;    // left to right on screen
            Vertex v[4]={corner(a,false),corner(b,false),corner(b,true),corner(a,true)};
            if(state>=2)for(Vertex &vertex:v)woundVertex(vertex,state);
            quad(out,v[0],v[1],v[2],v[3]);
        }
        c=end;
    }
}
static bool rowWounded(const std::vector<byte> &mask,int width,int r) {
    for(int c=0;c<width;++c)if(mask[(size_t)r*width+c])return true;
    return false;
}

void woundedQuads(std::vector<Vertex> &out,const Vertex &base,const Image &image,const std::vector<byte> &mask,
                  float x,float y,float top,float rx,float ry,bool flip,float rowHeight) {
    int w=image.width,h=image.height;
    float left=-(float)image.left,right=left+w;
    auto at=[&](int edge,float z,float v) {
        float across=flip?right-edge:left+edge;
        Vertex vertex=base;vertex.x=x+rx*across;vertex.y=y+ry*across;vertex.z=z;vertex.u=(float)edge;vertex.v=v;
        return vertex;
    };
    // Unwounded rows go out as whole blocks, wounded ones as runs.
    int block=-1;
    auto flush=[&](int end) {
        if(block<0)return;
        int a=flip?w:0,b=flip?0:w;
        quad(out,at(a,top-block*rowHeight,(float)block),at(b,top-block*rowHeight,(float)block),
             at(b,top-end*rowHeight,(float)end),at(a,top-end*rowHeight,(float)end));
        block=-1;
    };
    for(int r=0;r<h;++r) {
        if(!rowWounded(mask,w,r)) {if(block<0)block=r;continue;}
        flush(r);
        maskedRow(out,mask,w,r,flip,[&](int edge,bool bottom){return at(edge,top-(r+(bottom?1:0))*rowHeight,(float)(r+(bottom?1:0)));});
    }
    flush(h);
}
} // namespace doom3d
