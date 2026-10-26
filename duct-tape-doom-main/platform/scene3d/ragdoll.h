/* Ragdoll falls: what ragdoll.cpp (the skeleton and its fall) and
   ragdoll_body.cpp (the relief it moves) share. Body space: x right as the
   drawing is seen, y away from its viewer, z up, units from the thing's
   origin on the floor. Worked out in the lab: tests/death_ragdoll.py. */
#pragma once
#include "internal.h"

namespace doom3d {
struct V3 { float x=0,y=0,z=0; };
inline V3 operator+(V3 a,V3 b) {return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline V3 operator-(V3 a,V3 b) {return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline V3 operator*(V3 a,float s) {return {a.x*s,a.y*s,a.z*s};}
inline float dot(V3 a,V3 b) {return a.x*b.x+a.y*b.y+a.z*b.z;}
inline V3 cross(V3 a,V3 b) {return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline float length(V3 a) {return std::sqrt(dot(a,a));}
inline V3 unit(V3 a) {float n=length(a);return n>1e-9f?a*(1/n):V3{};}
inline V3 middle(V3 a,V3 b) {return (a+b)*0.5f;}

// The skeleton's points, and the parts of the body that follow them. The gun is a bone of its own:
// every texel the rig carries follows it alone, never blended, so it stays hard however the body
// bends. It is held in the chest's frame (let go, it would be the weapon dropping).
enum DollPoint { P_HEAD,P_NECK,P_SH_L,P_SH_R,P_SPINE,P_HIP_L,P_HIP_R,P_KNEE_L,P_KNEE_R,P_ANK_L,P_ANK_R,DOLL_POINTS };
enum DollPart { PART_PELVIS,PART_CHEST,PART_HEAD,PART_THIGH_L,PART_SHIN_L,PART_THIGH_R,PART_SHIN_R,PART_GUN,DOLL_PARTS };
using DollPose=std::array<V3,DOLL_POINTS>;

// The relief: a texel grid's corners in body space (front raised toward the
// viewer, back away from it), each following up to four parts, and its faces:
// per opaque texel a front, a back, and walls where the outline is open.
struct ReliefCorner { V3 p;uint8_t part[4]={};float w[4]={}; };
enum { FACE_FRONT,FACE_BACK,FACE_WALL };
struct ReliefFace { uint32_t corner[4];uint16_t c,r;uint8_t kind;float dim; };
struct Relief {
    int width=0,height=0;
    std::vector<byte> colour;           // per texel: its palette index
    std::vector<byte> backColour;       // per texel: the back's, from the drawing seen from behind (the front's where none)
    int cardSize=0;                     // the virtual camera's square picture: wide enough for the body any way round
    std::vector<ReliefCorner> corners;
    std::vector<ReliefFace> faces;
    DollPose rest{};                    // the skeleton as the drawing stands
    bool hasHead=false;float neck=0,headMiddle=0,headWidth=0;
    bool guided=false;                  // its hand rig lets a chest blast play the shot death on strings
};
// The relief of a rigged drawing (null without a rig), its depth from the side
// drawing (sideLump, mirrored when sideFlip; -1: none, a round tube) and its
// back's colours from the drawing seen from behind (backLump, likewise).
const Relief *reliefFor(int lump,int sideLump,bool sideFlip,int backLump,bool backFlip);
void reliefsShutdown();

// Each part's motion from rest to now: world = m * rest + t.
struct PartMotion { float m[3][3];V3 t; };
void partMotions(const DollPose &rest,const DollPose &now,std::array<PartMotion,DOLL_PARTS> &out);
inline V3 carry(const PartMotion &k,V3 p) {
    return {k.m[0][0]*p.x+k.m[0][1]*p.y+k.m[0][2]*p.z+k.t.x,k.m[1][0]*p.x+k.m[1][1]*p.y+k.m[1][2]*p.z+k.t.y,
            k.m[2][0]*p.x+k.m[2][1]*p.y+k.m[2][2]*p.z+k.t.z};
}
}
