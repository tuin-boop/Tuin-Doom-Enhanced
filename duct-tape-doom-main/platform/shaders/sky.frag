#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// Far-plane sky in uncovered pixels; early depth skips fog behind geometry.
// SURFACE: sky ceilings and the open band above lower sky neighbors, drawn
// into depth so the level beyond them stays hidden as in vanilla.
layout(early_fragment_tests) in;
layout(set=2,binding=0) uniform sampler2D image;
layout(set=2,binding=1) uniform sampler2D palette;
layout(std140,set=3,binding=0) uniform Camera CAMERA_BLOCK c;
layout(std140,set=3,binding=1) uniform Lights FLASH_BLOCK;
layout(std140,set=3,binding=2) uniform Fog FOG_BLOCK;
layout(location=0) out vec4 outColor;
#include "indexed.glsl"
// The flashlight beam ends at geometry; through the sky it would gather its
// whole length of fog and seem to light the sky itself.
#define NO_BEAM_FOG
#define NO_LOCAL_FOG
#include "lighting.glsl"
void main() {
    vec2 size=vec2(textureSize(image,0));
#ifdef SURFACE
    vec3 ray=normalize(vWorld-c.eye.xyz);
#else
    vec2 screen=vUV*2.0-1.0;
    vec3 ray=normalize(c.forward.xyz+c.right.xyz*screen.x/c.projection.x-c.up.xyz*screen.y/c.projection.y);
#endif
    // World-anchored cylinder: yaw and height come from the view ray, so the
    // sky no longer shears with pitch or stretches with FOV. Scaled a little
    // toward vanilla proportions; the horizon row moves down with it so the
    // painted top edge stays near 46 degrees up.
    float angle=atan(ray.y,ray.x);
    float rise=ray.z/max(length(ray.xy),1e-4);
    float v=0.70-rise*0.68;
    vec2 uv=vec2(-angle/6.2831853*size.x*4.0,clamp(v,0.001,0.999)*size.y);
    bool filtered=c.effects.x>0.0;
    // Light distance softening: blend in a 2x2 texel box so the sky sits a
    // touch softer than the crisp walls in front of it. Taps stay inside the
    // texture rows so the top and bottom don't wrap into each other.
    vec4 color=indexed(image,palette,uv,filtered,true);
    vec3 soft=vec3(0);
    for(int i=0;i<4;++i) {
        vec2 o=vec2(i&1,i>>1)-0.5;
        soft+=indexed(image,palette,vec2(uv.x+o.x,clamp(uv.y+o.y,0.5,size.y-0.5)),filtered,true).rgb;
    }
    color.rgb=mix(color.rgb,soft/4.0,0.6);
    // The sky's own low-row color, the tint of air at the horizon.
    vec3 low=vec3(0);
    for(int i=0;i<8;++i)
        low+=indexed(image,palette,vec2((float(i)+0.5)*size.x/8.0,size.y*0.92),false,true).rgb;
    low/=8.0;
    // Aerial perspective: contrast and saturation fall toward the horizon,
    // pulled toward a grey-tinted mix of that low color. Restrained overall.
    float air=0.12+0.33*(1.0-smoothstep(0.0,0.6,rise));
    float luma=dot(color.rgb,vec3(0.299,0.587,0.114));
    color.rgb=mix(color.rgb,mix(vec3(luma),low,0.6),air);
    // High-altitude haze in the sky's own upper-row color hides the clamped,
    // stretched top rows. It begins just above the frame top when looking
    // level and is opaque a little past the painted edge.
    float high=smoothstep(0.12,-0.08,v);
    if(high>0.0) {
        vec3 cap=vec3(0);
        for(int i=0;i<8;++i)
            for(int j=0;j<2;++j)
                cap+=indexed(image,palette,vec2((float(i)+0.5)*size.x/8.0,size.y*(0.03+0.08*float(j))),false,true).rgb;
        color.rgb=mix(color.rgb,cap/16.0,high);
    }
    // Past the painted bottom edge, fade into the low color the same way so
    // the clamped bottom row doesn't streak when looking down from a ledge.
    color.rgb=mix(color.rgb,low,smoothstep(0.88,1.02,v));
    // Haze toward the horizon in the sky's own low-row color, so distant
    // silhouettes meet a soft band rather than a hard painted edge.
    if(c.materials.z>0.0&&c.effects.z==0.0)
        color.rgb=mix(color.rgb,low,0.55*(1.0-smoothstep(-0.05,0.3,ray.z)));
    // The sun at the baked yaw and the height the sky paints it (c.outdoor.w;
    // bakeSun in scene3d/bake_sun.cpp, 40 degrees for DOOM): a small
    // disc and a wide glow in its tint. Smooth, like the softened sky around
    // it; a dither here shimmers over a large part of the sky.
    if(c.fx2.w>0.0) {
        vec3 toward=vec3(cos(c.fx2.z)*cos(c.outdoor.w),sin(c.fx2.z)*cos(c.outdoor.w),sin(c.outdoor.w));
        float a=acos(clamp(dot(ray,toward),-1.0,1.0));
        float disc=1.0-smoothstep(0.016,0.024,a),glow=exp(-a*a/0.012)*0.45+exp(-a/0.35)*0.12;
        float amount=saturate(disc*0.75+glow)*c.fx2.w;
        color.rgb=mix(color.rgb,max(color.rgb,c.sun.rgb),saturate(disc*c.fx2.w))+c.sun.rgb*amount*(1.0-disc*0.5);
    }
    // Far rain: weather.cpp drops rain only in the map's open sectors near
    // the eye, and the sky (ceilings and the band over lower neighbors
    // included) hides everything past it, so the rain would end at the
    // courtyard walls and the sky ceiling. Past them it falls on in the sky
    // itself: two sheets of streaks one Doom pixel wide, about 900 and 1800
    // units out, world-anchored like the sky, falling at the rain's speed
    // and slanted by its wind, stepping at the tic rate. Pale, the far sheet
    // fainter, fading out toward the zenith.
    if(c.weather.x>0.0) {
        float pixel=160.0*c.projection.x; // Doom pixels per radian
        float tic=floor(c.fog.w*35.0)/35.0;
        vec2 across=vec2(-sin(angle),cos(angle));
        float slant=dot(c.weather.yz,across)/520.0;
        float drops=0.0;
        for(int layer=0;layer<2;++layer) {
            float far=layer==0?900.0:1800.0;
            vec2 cellAt=floor(vec2(angle,rise)*pixel);
            float column=floor(cellAt.x+slant*cellAt.y+0.5)+float(layer)*7919.0;
            float seed=fract(sin(column*12.9898+float(layer)*78.233)*43758.5453);
            if(seed>=0.45*c.weather.x) continue;
            float period=36.0+28.0*fract(seed*91.7),streak=3.0+4.0*fract(seed*37.1);
            float along=cellAt.y+520.0/far*pixel*tic+seed*period*11.0;
            if(mod(along,period)<streak) drops=max(drops,layer==0?0.42:0.22);
        }
        drops*=1.0-smoothstep(0.9,1.8,rise);
        color.rgb=mix(color.rgb,max(color.rgb*1.5,low*1.3+0.05),drops);
    }
    // An episode whose outdoors lie dark (Game_Mood) dims its sky with them,
    // and its hour can tint it.
    color.rgb*=c.mood.x*c.mood.yzw;
    vec4 fog=fogAlong(c.eye.xyz+ray*2000.0);
    color.rgb=(color.rgb*fog.a+fog.rgb)*c.fx.x;
    // Mirror pass (world.frag): the sky is far, but stays a faint reflection.
    color.a=(c.water.z==0.0&&c.water.w>0.0)?0.35:1.0;
    outColor=color;
}
