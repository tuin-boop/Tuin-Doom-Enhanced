// Shared declarations. Sources are Vulkan GLSL; Metal gets MSL translated by
// SPIRV-Cross (scripts/compile_gpu_shaders.cmake).
// SDL_gpu resource sets: vertex 0 resources / 1 uniforms, fragment 2 / 3.
#include "feature_flags.h"
#include "vertex_modes.h"
// Clip space is y up; SDL_gpu flips the Vulkan viewport to match Metal.
// baked: the part of the light already in the bake maps (0: dynamic only).
struct Flash { vec4 position; float strength; uint first,count; float baked; vec4 color,direction; };
struct LightBlocker { vec4 line,opening; };
// right.w: where the wear records start in the sector buffer (0: none);
// forward.w, up.w: how wet the windows and the ground under the sky are.
// mood: x how bright the sky is drawn, yzw its tint (the episode's mood, Game_Mood).
// weather: x how hard it rains (0: dry), yz the wind blowing it (units/s).
// fx: exposure, sky light tint; fx2: render-target pixels per Doom pixel,
// sky light level (0: off), sun yaw, sun disc strength (0: off); texFilter:
// sharp bilinear edge softness in texels, palette mipmaps (0: off);
// flicker: the current light of the baked flicker groups, group 0 steady.
#define CAMERA_BLOCK { vec4 eye,right,forward,up,projection,effects,materials,flashlightTint,fog,map,water,sun,bake,fx,fx2,texFilter,outdoor,haze,mood,weather,style; vec4 flicker[4]; }
#define FLASH_BLOCK { uint flashCount,flashReserved0,flashReserved1,flashReserved2; Flash lights[63]; }
// count, then four light indices, then padding (see doom3d::FogLights).
#define FOG_BLOCK { uvec4 fogA,fogB; }
// Fog haze: the cool grey under a neutral sky, leaning toward the sky's
// color as it saturates (Inferno's red). skyTint is paled halfway to white.
vec3 fogHaze(vec3 skyTint) {
    vec3 hue=clamp(skyTint*2.0-1.0,0.0,1.0);
    float saturation=max(hue.r,max(hue.g,hue.b))-min(hue.r,min(hue.g,hue.b));
    return mix(vec3(0.045,0.055,0.065),0.07*mix(vec3(1),hue,0.65),saturation);
}

#ifdef VERTEX
#define VARYING out
#else
#define VARYING in
#endif
layout(location=0) VARYING vec2 vUV;
layout(location=1) VARYING float vLight;
layout(location=2) VARYING float vDistance;
layout(location=3) flat VARYING uint vMode;
layout(location=4) VARYING vec3 vWorld;
layout(location=5) flat VARYING vec3 vTint;
layout(location=6) flat VARYING uvec2 vLightMask;
layout(location=7) VARYING vec2 vSun;
// Sprites with bake-only lights: static light direction (xyz) and share (w), unorm bytes.
// Walls: a seed per wall section for their uneven tiles (tiles.glsl).
layout(location=8) flat VARYING uint vStatic;
// Sprites: the sector light's lamp color (lampTint in baked_lighting.h), unorm bytes.
layout(location=9) flat VARYING uint vLampTint;

// Camera vector for shallow relief on transient impact materials.
layout(location=10) VARYING vec3 vToEye;

float saturate(float x) {return clamp(x,0.0,1.0);}
vec3 saturate(vec3 x) {return clamp(x,vec3(0.0),vec3(1.0));}
// GLSL leaves % undefined for negative operands.
ivec2 wrapTexel(ivec2 p,ivec2 size) {return p-size*ivec2(floor(vec2(p)/vec2(size)));}
// Ordered 4x4 dither threshold in (0,1), for retro fades instead of smooth alpha.
float bayer4(ivec2 p) {
    const float m[16]=float[16](0.0,8.0,2.0,10.0,12.0,4.0,14.0,6.0,3.0,11.0,1.0,9.0,15.0,7.0,13.0,5.0);
    ivec2 q=p&3;
    return (m[q.y*4+q.x]+0.5)/16.0;
}
