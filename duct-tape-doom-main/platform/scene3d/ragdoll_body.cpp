/* The body a ragdoll fall moves (ragdoll.cpp): a rigged drawing raised into
   a relief. Each texel rises as the tube sprite_normals.cpp finds under it,
   at true depth; the side drawing (rotation 3) then sets how far the body
   reaches forward and how far back, per band (legs, trunk, head), so a body
   lying down is as deep as its own side view rather than a round bolster.
   Front and back are raised separately (the back painted from the drawing
   seen from behind, rotation 5), walls close the outline, and every
   corner carries the weights of the parts it follows. From the lab:
   tests/death_relief.py (mesh, weights, side_profile). */
#include "ragdoll.h"
#include <memory>

namespace doom3d {
static float ramp(float z,float joint,float half=1) {return std::clamp((z-joint)/(2*half)+0.5f,0.0f,1.0f);}

// Which parts a rest point follows: below the hips a leg (left or right of
// where the legs part, blended over 3 units), its thigh above the knee and
// shin below; above them pelvis, chest and head, each blended over a unit
// or so at its joint. Texels the rig carries (a gun) ride the gun bone alone.
struct Joints { float hip,chest,neck,knee[2],split; };
static void weigh(const Joints &j,V3 p,bool carried,ReliefCorner &out) {
    float w[DOLL_PARTS]={};
    if(carried)w[PART_GUN]=1;
    else {
        float up=ramp(p.z,j.hip,1.5f);
        if(up>0) {
            float c=ramp(p.z,j.chest),hd=ramp(p.z,j.neck);
            w[PART_PELVIS]+=up*(1-c);w[PART_CHEST]+=up*c*(1-hd);w[PART_HEAD]+=up*c*hd;
        }
        if(up<1) {
            float left=std::clamp(0.5f-(p.x-j.split)/3,0.0f,1.0f);
            for(int s=0;s<2;++s) {
                float share=s?1-left:left;
                if(share<=0)continue;
                float shin=1-ramp(p.z,j.knee[s]);
                w[s?PART_THIGH_R:PART_THIGH_L]+=(1-up)*share*(1-shin);
                w[s?PART_SHIN_R:PART_SHIN_L]+=(1-up)*share*shin;
            }
        }
    }
    int order[DOLL_PARTS];
    for(int i=0;i<DOLL_PARTS;++i)order[i]=i;
    std::sort(order,order+DOLL_PARTS,[&](int a,int b){return w[a]>w[b];});
    float sum=w[order[0]]+w[order[1]]+w[order[2]]+w[order[3]];
    for(int k=0;k<4;++k) {out.part[k]=(uint8_t)order[k];out.w[k]=sum>0?w[order[k]]/sum:(k==0);}
}

// How far the body reaches forward and back at a height, from the side
// drawing (the body's face toward its left, its back to the right, the
// origin on the drawing's plane). One reach each way per band, a low
// percentile of the rows' (an arm or a gun held out reaches forward in some
// rows; taken per row they raised the chest into a fin); in the legs, which
// the side view's stride parts, each leg is as deep as its own run, split
// evenly. Blended between the bands' middles.
struct Reach { float z,front,back; };
static std::vector<Reach> sideReach(const Image &side,bool flip,int top) {
    static const float bands[3][2]={{0,0.45f},{0.45f,0.8f},{0.8f,1.2f}};
    int left=flip?side.width-side.left:side.left;
    std::vector<std::pair<float,std::pair<float,float>>> rows;   // height, (front, back)
    for(int r=0;r<side.height;++r) {
        std::vector<std::pair<int,int>> runs;
        int start=-1;
        for(int c=0;c<=side.width;++c) {
            int texel=flip?side.width-1-c:c;
            bool on=c<side.width&&side.pixels[((size_t)r*side.width+texel)*2+1];
            if(on&&start<0)start=c;
            if(!on&&start>=0) {if(c-start>=2)runs.push_back({start-left,c-left});start=-1;}
        }
        if(runs.empty())continue;
        float z=(float)(side.top-r-1);
        if(z+0.5f<0.45f*top) {
            std::vector<int> widths;
            for(auto &run:runs)widths.push_back(run.second-run.first);
            std::sort(widths.begin(),widths.end());
            float w=widths[widths.size()/2]/2.0f;
            rows.push_back({z,{w,w}});
        } else {
            auto nearest=*std::min_element(runs.begin(),runs.end(),[](const std::pair<int,int> &a,const std::pair<int,int> &b) {
                auto off=[](const std::pair<int,int> &q){return q.first<=0&&q.second>=0?0:std::min(std::abs(q.first),std::abs(q.second));};
                return off(a)<off(b);
            });
            rows.push_back({z,{std::max(0.5f,(float)-nearest.first),std::max(0.5f,(float)nearest.second)}});
        }
    }
    std::vector<Reach> marks;
    for(const auto &band:bands) {
        std::vector<float> fronts,backs;
        for(auto &row:rows)
            if(row.first+0.5f>=band[0]*top&&row.first+0.5f<band[1]*top) {fronts.push_back(row.second.first);backs.push_back(row.second.second);}
        if(fronts.empty())continue;
        std::sort(fronts.begin(),fronts.end());std::sort(backs.begin(),backs.end());
        size_t at=(size_t)(0.4f*(fronts.size()-1));
        marks.push_back({(band[0]+std::min(band[1],1.0f))/2*top,fronts[at],backs[at]});
    }
    return marks;
}
static void reachAt(const std::vector<Reach> &marks,float z,float &front,float &back) {
    if(z<=marks.front().z) {front=marks.front().front;back=marks.front().back;return;}
    for(size_t i=1;i<marks.size();++i)
        if(z<=marks[i].z) {
            float w=(z-marks[i-1].z)/(marks[i].z-marks[i-1].z);
            front=marks[i-1].front+(marks[i].front-marks[i-1].front)*w;back=marks[i-1].back+(marks[i].back-marks[i-1].back)*w;
            return;
        }
    front=marks.back().front;back=marks.back().back;
}

// The back's colours: each texel's from the drawing seen from behind, mirrored onto the body (the
// monster's right, on the front drawing's left, is on the back drawing's right), at the same height;
// where the outlines differ, the nearest opaque texel a few away (the same row first), else the front's.
static std::vector<byte> backColours(const std::vector<byte> &pixels,int w,int h,int left,int top,const Image &back,bool flip) {
    std::vector<byte> out((size_t)w*h);
    auto opaque=[&](int c,int r){return c>=0&&r>=0&&c<back.width&&r<back.height&&back.pixels[((size_t)r*back.width+c)*2+1];};
    for(int r=0;r<h;++r)for(int c=0;c<w;++c) {
        size_t i=(size_t)r*w+c;out[i]=pixels[i*2];
        int cb=flip?c-left+back.left:back.left+left-c-1,rb=r+back.top-top;
        int best=-1,bestDistance=INT_MAX;
        for(int dr=-2;dr<=2;++dr)for(int dc=-3;dc<=3;++dc) {
            int d=dc*dc+2*dr*dr;
            if(d<bestDistance&&opaque(cb+dc,rb+dr)) {bestDistance=d;best=(rb+dr)*back.width+cb+dc;}
        }
        if(best>=0)out[i]=back.pixels[(size_t)best*2];
    }
    return out;
}

static std::unordered_map<int,std::unique_ptr<Relief>> reliefs;   // by lump, null where none
void reliefsShutdown() {reliefs.clear();}
const Relief *reliefFor(int lump,int sideLump,bool sideFlip,int backLump,bool backFlip) {
    if(lump<0)return nullptr;
    auto found=reliefs.find(lump);
    if(found!=reliefs.end())return found->second.get();
    auto &slot=reliefs[lump];
    RigJoints rig;
    if(!rigJoints(lump,rig))return nullptr;
    const Image &image=lumpImage(lump,false);
    int w=image.width,h=image.height,left=image.left,top=image.top;
    std::vector<byte> pixels=image.pixels;
    auto opaque=[&](int c,int r){return c>=0&&r>=0&&c<w&&r<h&&pixels[((size_t)r*w+c)*2+1];};
    // Front and back heights: the tube, reshaped per row to the side drawing's reach
    // (each texel in proportion to its tube height, within 0.3-1.3 of the tube's).
    std::vector<float> tube=spriteTubeHeights(w,h,pixels.data()),front=tube,back=tube;
    if(sideLump>=0&&sideLump!=lump) {
        std::vector<Reach> marks=sideReach(lumpImage(sideLump,false),sideFlip,top);
        if(!marks.empty())
            for(int r=0;r<h;++r) {
                float peak=0;
                for(int c=0;c<w;++c)peak=std::max(peak,tube[(size_t)r*w+c]);
                if(peak<=0)continue;
                float f,b;reachAt(marks,(float)(top-r-1),f,b);
                f=std::clamp(f,0.3f*peak,1.3f*peak);b=std::clamp(b,0.3f*peak,1.3f*peak);
                for(int c=0;c<w;++c) {size_t i=(size_t)r*w+c;front[i]=f*tube[i]/peak;back[i]=b*tube[i]/peak;}
            }
    }
    auto relief=std::make_unique<Relief>();
    relief->width=w;relief->height=h;
    relief->colour.resize((size_t)w*h);
    for(size_t i=0;i<relief->colour.size();++i)relief->colour[i]=pixels[i*2];
    relief->backColour=backLump>=0&&backLump!=lump?backColours(pixels,w,h,left,top,lumpImage(backLump,false),backFlip):relief->colour;
    {
        float deepest=0;
        for(size_t i=0;i<tube.size();++i)deepest=std::max(deepest,front[i]+back[i]);
        int size=(int)std::ceil(std::sqrt((float)w*w+(float)h*h+deepest*deepest))+8;
        relief->cardSize=(size+3)&~3;
    }
    // The skeleton as the drawing stands: the rig's legs, the chest joint 45% of the way from the
    // hips to the neck, shoulders a little under the neck at 80% of the body's half width there.
    float hip=rig.hip[0][1],mid=(rig.hip[0][0]+rig.hip[1][0])/2;
    relief->hasHead=rig.hasHead;
    float neck=rig.hasHead?rig.neck:0.8f*top,headU=rig.hasHead?rig.headMiddle:mid,chest=hip+0.45f*(neck-hip);
    relief->neck=neck;relief->headMiddle=headU;relief->headWidth=rig.headWidth;relief->guided=rig.guided;
    float half=7;
    {
        int row=std::clamp(top-(int)(neck-3),0,h-1);float lo=1e9f,hi=-1e9f;
        for(int c=0;c<w;++c)if(opaque(c,row)) {lo=std::min(lo,(float)(c-left));hi=std::max(hi,(float)(c-left));}
        if(hi>=lo)half=std::max(2.0f,0.8f*std::min(mid-lo,hi-mid));
    }
    DollPose &rest=relief->rest;
    rest[P_HEAD]={headU,0,(float)top-4};rest[P_NECK]={headU,0,neck};
    rest[P_SH_L]={mid-half,0,neck-3};rest[P_SH_R]={mid+half,0,neck-3};rest[P_SPINE]={mid,0,chest};
    rest[P_HIP_L]={rig.hip[0][0],0,rig.hip[0][1]};rest[P_HIP_R]={rig.hip[1][0],0,rig.hip[1][1]};
    rest[P_KNEE_L]={rig.knee[0][0],0,rig.knee[0][1]};rest[P_KNEE_R]={rig.knee[1][0],0,rig.knee[1][1]};
    rest[P_ANK_L]={rig.ankle[0][0],0,rig.ankle[0][1]};rest[P_ANK_R]={rig.ankle[1][0],0,rig.ankle[1][1]};
    Joints joints={hip,chest,neck,{rig.knee[0][1],rig.knee[1][1]},rig.split};
    // Corners: each the mean of the four texels around it (empty ones 0), so the outline sits half raised.
    auto cornerHeight=[&](const std::vector<float> &heights,int c,int r) {
        float sum=0;
        for(int a=0;a<2;++a)for(int b=0;b<2;++b) {int x=c-a,y=r-b;if(opaque(x,y))sum+=heights[(size_t)y*w+x];}
        return sum/4;
    };
    std::unordered_map<uint32_t,uint32_t> index;
    auto corner=[&](int c,int r,int side,bool carried)->uint32_t {
        uint32_t key=(((uint32_t)r*(w+1)+c)*2+side)*2+carried;
        auto at=index.find(key);
        if(at!=index.end())return at->second;
        ReliefCorner rc;
        float d=side?cornerHeight(back,c,r):-cornerHeight(front,c,r);
        rc.p={(float)(c-left),d,(float)(top-r)};
        weigh(joints,rc.p,carried,rc);
        relief->corners.push_back(rc);
        return index[key]=(uint32_t)relief->corners.size()-1;
    };
    static const int edges[4][4]={{0,-1,0,1},{1,0,1,2},{0,1,2,3},{-1,0,3,0}};   // neighbour, then the edge's two corners
    for(int r=0;r<h;++r)for(int c=0;c<w;++c) {
        if(!opaque(c,r))continue;
        bool carried=rig.carried&&(*rig.carried)[(size_t)r*w+c];
        const int cc[4][2]={{c,r},{c+1,r},{c+1,r+1},{c,r+1}};
        ReliefFace face{};face.c=(uint16_t)c;face.r=(uint16_t)r;
        face.kind=FACE_FRONT;face.dim=1;
        for(int k=0;k<4;++k)face.corner[k]=corner(cc[k][0],cc[k][1],0,carried);
        relief->faces.push_back(face);
        face.kind=FACE_BACK;
        for(int k=0;k<4;++k)face.corner[k]=corner(cc[3-k][0],cc[3-k][1],1,carried);
        relief->faces.push_back(face);
        for(const auto &e:edges) {
            if(opaque(c+e[0],r+e[1]))continue;
            const int *p=cc[e[2]],*q=cc[e[3]];
            face.kind=FACE_WALL;face.dim=0.7f;
            face.corner[0]=corner(p[0],p[1],0,carried);face.corner[1]=corner(p[0],p[1],1,carried);
            face.corner[2]=corner(q[0],q[1],1,carried);face.corner[3]=corner(q[0],q[1],0,carried);
            relief->faces.push_back(face);
        }
    }
    // No drawing shows a surface turned edge-on to it: darker the more edge-on it stands.
    for(ReliefFace &face:relief->faces) {
        V3 a=relief->corners[face.corner[0]].p,b=relief->corners[face.corner[1]].p,c=relief->corners[face.corner[2]].p,d=relief->corners[face.corner[3]].p;
        V3 n=unit(cross(d-b,c-a));
        face.dim*=0.4f+0.6f*std::abs(n.y);
    }
    slot=std::move(relief);
    return slot.get();
}

// A part's frame: from its joint toward the next one, turned about that line
// by the side the part's points give (the hips for the legs and pelvis, the
// shoulders for chest, head and gun).
static void frame(const DollPose &p,DollPart part,V3 &origin,V3 axes[3]) {
    static const int parts[DOLL_PARTS][4]={      // origin (-1: between the hips), tip, side from, side to
        {-1,P_SPINE,P_HIP_L,P_HIP_R},{P_SPINE,P_NECK,P_SH_L,P_SH_R},{P_NECK,P_HEAD,P_SH_L,P_SH_R},
        {P_HIP_L,P_KNEE_L,P_HIP_L,P_HIP_R},{P_KNEE_L,P_ANK_L,P_HIP_L,P_HIP_R},
        {P_HIP_R,P_KNEE_R,P_HIP_L,P_HIP_R},{P_KNEE_R,P_ANK_R,P_HIP_L,P_HIP_R},{P_SPINE,P_NECK,P_SH_L,P_SH_R}};
    const int *d=parts[part];
    origin=d[0]<0?middle(p[P_HIP_L],p[P_HIP_R]):p[d[0]];
    V3 u=unit(p[d[1]]-origin),s=p[d[3]]-p[d[2]];
    s=unit(s-u*dot(s,u));
    axes[0]=u;axes[1]=s;axes[2]=cross(u,s);
}
void partMotions(const DollPose &rest,const DollPose &now,std::array<PartMotion,DOLL_PARTS> &out) {
    for(int part=0;part<DOLL_PARTS;++part) {
        V3 o0,o1,b0[3],b1[3];
        frame(rest,(DollPart)part,o0,b0);frame(now,(DollPart)part,o1,b1);
        PartMotion &k=out[part];
        // m = B1 * B0^T: each rest axis goes to its axis now.
        for(int i=0;i<3;++i)for(int j=0;j<3;++j) {
            float sum=0;
            for(int a=0;a<3;++a) {
                const float b1i=i==0?b1[a].x:i==1?b1[a].y:b1[a].z,b0j=j==0?b0[a].x:j==1?b0[a].y:b0[a].z;
                sum+=b1i*b0j;
            }
            k.m[i][j]=sum;
        }
        k.t={0,0,0};
        V3 moved=carry(k,o0);
        k.t=o1-moved;
    }
}
}
