#version 450
// World, sprite and screen-space geometry; REFLECT adds the mirror's clip plane.
#extension GL_GOOGLE_include_directive : require
#define VERTEX
#include "common.glsl"
layout(location=0) in vec3 inPosition;
layout(location=1) in vec2 inUV;
layout(location=2) in float inLight;
layout(location=3) in uint inMode;
layout(location=4) in vec3 inTint;
layout(location=5) in uvec2 inLightMask;
layout(location=6) in vec2 inSun;
layout(location=7) in uint inStatic;
layout(location=8) in uint inLampTint;
layout(std140,set=1,binding=0) uniform Camera CAMERA_BLOCK c;
// The depth prepass and the shading pass must place every pixel identically.
invariant gl_Position;
#ifdef REFLECT
// Mirrored-camera pass: geometry behind the mirror never appears in the
// reflection. plane: unit normal toward the eye (xyz) and offset (w); the
// liquid's is (0,0,1,height), window glass's is upright.
layout(std140,set=1,binding=1) uniform Plane { vec4 plane; };
out float gl_ClipDistance[1];
#endif
#ifdef REFLECT
layout(std140,set=1,binding=2)
#else
layout(std140,set=1,binding=1)
#endif
uniform Voxel {vec4 origin;vec4 pivotLight;vec4 rotation;vec4 baked;vec4 localEye;uvec4 shading;} voxel;
void main() {
    vec3 position=inPosition;
    bool localMesh=(inMode&0x80000000u)!=0u;
    if(localMesh){
        vec3 p=inPosition-voxel.pivotLight.xyz;p.y=-p.y;p.z=-p.z;
        position=voxel.origin.xyz+vec3(voxel.rotation.x*p.x-voxel.rotation.y*p.y,voxel.rotation.y*p.x+voxel.rotation.x*p.y,p.z);
    }
    vToEye=c.eye.xyz-position;
    vUV=inUV;vLight=inLight;vMode=inMode;vWorld=position;vTint=inTint;vLightMask=inLightMask;vSun=inSun;vStatic=inStatic;vLampTint=inLampTint;
    if(localMesh){vLight=voxel.pivotLight.w;vMode=voxel.shading.x;vTint=voxel.baked.xyz;vStatic=voxel.shading.y;vLampTint=voxel.shading.z;}
    if((inMode&MODE_SCREEN)!=0u) {gl_Position=vec4(inPosition.xy,0,1);vDistance=0;}
    else {
        vec3 d=position-c.eye.xyz;
        float z=dot(d,c.forward.xyz);vDistance=z;
        float n=c.projection.z,f=c.projection.w;
        gl_Position=vec4(dot(d,c.right.xyz)*c.projection.x,
                         dot(d,c.up.xyz)*c.projection.y,z*f/(f-n)-n*f/(f-n),z);
    }
    if(localMesh&&dot(voxel.localEye.xyz-inPosition,inTint)<=0.0)gl_Position=vec4(2,2,2,1);
#ifdef REFLECT
    gl_ClipDistance[0]=dot(position,plane.xyz)-plane.w-0.5;
#endif
}
