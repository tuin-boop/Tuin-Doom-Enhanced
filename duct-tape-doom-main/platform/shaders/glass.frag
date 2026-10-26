#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// Window glass (scene3d/windows.cpp), blended over the finished world. It
// reads through contrast rather than a wash: nearly clear head-on, with
// grime gathered at the frame and the sill, shine streaks painted on it as
// in Duke Nukem 3D, highlights from the lights in front of it as on glossy
// metal, and from outside the sky mirrored in it, darker toward the ground.
// Everything sits on the pane's own one-unit texel grid, like Doom's walls.
// Broken panes keep only jagged teeth along the frame.
// Vertex: vUV the point on the pane in units, vSun its size, vTint the face
// normal (xy) and whether the face is outdoors (z), vLight that side's light,
// vStatic the pane's seed with the top bit set once broken; vLightMask.y
// bit 1 on panes out under the sky (rain gathers on them), bit 2 where u
// runs from the pane's far end. In each mask, bit 30 marks a bullet hole,
// bits 2..15 and 16..29 hold its canonical pane coordinates.
// MODE_GLASS_SHARD: vTint is the rotating normal, vSun is the pixel size and
// fade. lightMask packs neighbour/current row limits as biased bytes; its
// second word's low bit says outdoors. MODE_GROUNDED makes settled shards matte.
layout(set=2,binding=0) uniform sampler2D sky;
layout(set=2,binding=1) uniform sampler2D palette;
// Glass reflections: the world drawn again mirrored in the chosen pane
// (drawGlassReflection in gpu3d/frame.cpp).
layout(set=2,binding=2) uniform sampler2D mirror;
// Glass probes: a row of six cube faces per probe (+x, -x, +y, -y, +z, -z;
// captureGlassProbes in gpu3d/frame.cpp).
layout(set=2,binding=3) uniform sampler2D probes;
// The lights' blockers (buildLightWords in scene3d/lights.cpp), numbered after
// the samplers in SPIR-V and after the four uniform blocks on Metal.
#define BLOCKER_BINDING 4
layout(std430,set=2,binding=BLOCKER_BINDING) readonly buffer Blockers { uvec4 lightWords[]; };
layout(std140,set=3,binding=0) uniform Camera CAMERA_BLOCK c;
layout(std140,set=3,binding=1) uniform Lights FLASH_BLOCK;
layout(std140,set=3,binding=2) uniform Fog FOG_BLOCK;
// The world view's corner (xy) and inverse size (zw) on screen, to read the
// mirrored view (z is 0 when no pane is mirrored this frame); the probe face
// size (probeInfo.x) and how far the mirror has faded in over its probe
// (probeInfo.y); per probe its point, and its room box's low and high
// corners (scene3d.h GlassProbe).
layout(std140,set=3,binding=3) uniform Mirror { vec4 mirrorView,probeInfo; vec4 probeData[192]; };
layout(location=0) out vec4 outColor;
#include "indexed.glsl"
#define BLOCKERS
#include "lighting.glsl"
float hash(vec2 p,uint seed) {
    uvec2 q=uvec2(ivec2(floor(p))+32768);
    uint h=q.x*73856093u^q.y*19349663u^seed;
    // Chris Wellons' lowbias32 (hash-prospector, public domain).
    h^=h>>16u;h*=0x7feb352du;h^=h>>15u;h*=0x846ca68bu;h^=h>>16u;
    return float(h&0xffffffu)/16777216.0;
}
// How deep the broken glass's teeth reach in from one edge at position x
// along it: random depths every 7 units joined by straight cuts.
float tooth(float x,uint seed) {
    float cell=x/7.0,f=fract(cell),i=floor(cell);
    return mix(hash(vec2(i,0),seed),hash(vec2(i+1.0,0),seed),f)*11.0+1.0;
}
float segmentDistance(vec2 p,vec2 a,vec2 b) {
    vec2 ab=b-a;
    return length(p-a-ab*clamp(dot(p-a,ab)/max(dot(ab,ab),1e-4),0.0,1.0));
}
// A small transparent puncture, chipped white edge and branching hairlines,
// sampled on the pane's texel grid. The shadow below each crack gives glass
// thickness without turning the hit into a soot mark. xyz: hole, light, dark.
vec3 bulletMark(vec2 texel,uint packed,uint seed) {
    if((packed&(1u<<30u))==0u)return vec3(0);
    vec2 hit=vec2(float((packed>>2u)&16383u),float((packed>>16u)&16383u))+0.5;
    vec2 p=texel-hit;
    if(max(abs(p.x),abs(p.y))>13.0)return vec3(0);
    float r=length(p);
    if(r<1.0)return vec3(1,0,0);
    float angle=atan(p.y,p.x);
    float chip=2.0+0.9*hash(vec2(floor((angle+3.141593)*3.0),3),seed);
    if(r<chip)return vec3(1,0,0);
    float bright=r<chip+1.0?1.0:0.0,dark=r<chip+1.9?0.75:0.0;
    for(int i=0;i<7;++i) {
        float ray=(float(i)+hash(vec2(i,1),seed)*0.55)*0.8975979;
        vec2 direction=vec2(cos(ray),sin(ray)),side=vec2(-direction.y,direction.x);
        float reach=5.5+5.5*hash(vec2(i,2),seed);
        vec2 kink=direction*reach*0.48+side*(hash(vec2(i,3),seed)-0.5)*1.5;
        vec2 tip=direction*reach+side*(hash(vec2(i,4),seed)-0.5)*2.5;
        vec2 fork=kink+direction*reach*0.25+side*(1.0+hash(vec2(i,5),seed)*1.5);
        float line=min(segmentDistance(p,direction*2.0,kink),min(segmentDistance(p,kink,tip),segmentDistance(p,kink,fork)));
        vec2 shadow=p-vec2(0.8,-0.8);
        float below=min(segmentDistance(shadow,direction*2.0,kink),min(segmentDistance(shadow,kink,tip),segmentDistance(shadow,kink,fork)));
        if(line<0.52)bright=max(bright,0.85);
        if(below<0.62)dark=max(dark,0.65);
    }
    return vec3(0,bright,dark);
}
// Rain on the glass (c.forward.w: how wet the windows are), on panes out
// under the sky, in the same place from both sides: x and y count texels
// from the pane's first end and its sill. Beads sit in some of its 5x6
// cells, each for its own few seconds before it goes and another forms;
// in some 7-texel columns now and then one runs down, a texel a step at
// the tic rate, wandering a texel left or right every five rows and leaving
// a broken trail. A drop is a texel catching the light over a dark one,
// as water lenses the view: 1 the lit texel, -1 the dark, 0.5 the trail.
float bead(vec2 cell,float t,uint seed,float wet,out vec2 at) {
    at=vec2(-1e4);
    if(hash(cell,seed^0x2a1d64f3u)>wet*0.22) return 0.0;
    float period=4.0+5.0*hash(cell,seed^0x6b43a9b5u),clock=t+hash(cell,seed^0x1e3779b1u)*period;
    float cycle=floor(clock/period);
    if(clock-cycle*period>period*0.85) return 0.0;
    at=cell*vec2(5,6)+floor(vec2(hash(cell+cycle*13.0,seed^0x4cf5ad43u)*5.0,1.0+hash(cell+cycle*17.0,seed^0x7a2b3c4du)*5.0));
    return 1.0;
}
float runner(float x,float y,float top,float t,uint seed,float wet) {
    float column=floor(x/7.0);
    for(int k=-1;k<=1;++k) {
        float col=column+float(k);
        if(hash(vec2(col,7),seed)>wet*0.7) continue;
        float period=3.0+5.0*hash(vec2(col,8),seed),clock=t+hash(vec2(col,9),seed)*period;
        float cycle=floor(clock/period),age=clock-cycle*period;
        float speed=12.0+14.0*hash(vec2(col,cycle),seed^0x5d0f1a2bu);
        float head=top-1.0-floor(age*speed);
        if(head<-2.0) continue;
        float shift=floor(hash(vec2(col,floor(y/5.0)+cycle*31.0),seed^0x3c1f9e77u)*3.0)-1.0;
        float base=col*7.0+1.0+floor(hash(vec2(col,cycle+50.0),seed)*5.0);
        if(x!=base+shift) continue;
        if(y==head) return 1.0;
        if(y==head-1.0) return -1.0;
        if(y>head&&y<head+10.0&&hash(vec2(x,y+cycle*7.0),seed^0x18a4c3e9u)<0.5*(1.0-(y-head)/10.0)) return 0.5;
    }
    return 0.0;
}
float rainOnGlass(vec2 texel,vec2 extent,bool flipped,uint seed,float wet) {
    float x=floor(flipped?extent.x-texel.x:texel.x),y=floor(texel.y);
    float t=floor(c.fog.w*35.0)/35.0;
    float run=runner(x,y,floor(extent.y),t,seed,wet);
    if(run!=0.0) return run;
    vec2 at;
    if(bead(floor(vec2(x/5.0,y/6.0)),t,seed,wet,at)>0.0&&at==vec2(x,y)) return 1.0;
    if(bead(floor(vec2(x/5.0,(y+1.0)/6.0)),t,seed,wet,at)>0.0&&at==vec2(x,y+1.0)) return -1.0;
    return 0.0;
}
// The sky as sky.frag maps it, along a ray; below the horizon it falls off
// to a dark ground so the reflection keeps a hard bright-over-dark line.
vec3 skyAlong(vec3 ray) {
    vec2 size=vec2(textureSize(sky,0));
    float angle=atan(ray.y,ray.x);
    float rise=ray.z/max(length(ray.xy),1e-4);
    vec2 uv=vec2(-angle/6.2831853*size.x*4.0,clamp(0.70-rise*0.68,0.001,0.999)*size.y);
    vec3 color=indexed(sky,palette,uv,false,true).rgb*c.mood.x*c.mood.yzw;
    return mix(color,color*0.12,step(rise,-0.02));
}
// What a probe sees along ray from point: the ray runs on to where it leaves
// the probe's room box, and the cube face looking that way from the probe
// holds what is there (box projection), so near walls stay in place as the
// eye moves. Faces as captureGlassProbes draws them: forward, up, and
// right = forward x up, 90 degrees across; read texel by texel.
vec3 probeAlong(uint probe,vec3 point,vec3 ray) {
    vec3 origin=probeData[3u*probe].xyz,low=probeData[3u*probe+1u].xyz,high=probeData[3u*probe+2u].xyz;
    vec3 safe=mix(vec3(1e-4),ray,greaterThan(abs(ray),vec3(1e-4)));
    vec3 leave=max((high-point)/safe,(low-point)/safe);
    vec3 d=point+ray*max(min(min(leave.x,leave.y),leave.z),0.0)-origin;
    vec3 a=abs(d);
    int face=a.x>=a.y&&a.x>=a.z?(d.x>0.0?0:1):a.y>=a.z?(d.y>0.0?2:3):(d.z>0.0?4:5);
    const vec3 forwards[6]=vec3[6](vec3(1,0,0),vec3(-1,0,0),vec3(0,1,0),vec3(0,-1,0),vec3(0,0,1),vec3(0,0,-1));
    const vec3 ups[6]=vec3[6](vec3(0,0,1),vec3(0,0,1),vec3(0,0,1),vec3(0,0,1),vec3(0,1,0),vec3(0,1,0));
    vec3 forward=forwards[face],up=ups[face],right=cross(forward,up);
    float depth=max(dot(d,forward),1e-4),size=probeInfo.x;
    vec2 at=vec2(dot(d,right),-dot(d,up))/depth*0.5+0.5;
    ivec2 texel=ivec2(clamp(floor(at*size),vec2(0),vec2(size-1.0)));
    return texelFetch(probes,texel+ivec2(face,int(probe))*int(size),0).rgb;
}
// Premultiplied layering, front layer last.
void over(inout vec4 result,vec3 color,float alpha) {result=vec4(color*alpha+result.rgb*(1.0-alpha),alpha+result.a*(1.0-alpha));}
// The moving pieces are cut from square texels like the broken frame glass.
// Reflections and lighting stay on the face, with chunky edge pixels instead
// of a fine vector outline. Settled pieces keep only simple lit glass color.
void shard() {
    vec3 toEye=c.eye.xyz-vWorld;
    float distance=length(toEye);
    vec3 view=toEye/max(distance,1e-3),normal=normalize(vTint);
    if(dot(normal,view)<0.0)normal=-normal;
    bool resting=(vMode&MODE_GROUNDED)!=0u;
    float grazing=resting?0.0:pow(1.0-saturate(dot(normal,view)),3.0);
    bool outdoors=(vLightMask.y&1u)!=0u;
    float light=vLight;
    if(c.effects.z==0.0)light*=clamp(1.0-vDistance/3200.0,0.35,1.0);
    vec3 lit=vec3(light),specular=vec3(0),ray=reflect(-view,normal);
    vec4 result=vec4(0);
    over(result,vec3(0.36,0.49,0.51)*light*(resting?0.85:1.0),0.24+0.3*grazing);
    if(!resting&&vLampTint>0u) {
        vec3 reflected=probeAlong(vLampTint-1u,vWorld,ray);
        over(result,reflected,0.32+0.45*grazing);
    }
    else if(!resting&&outdoors)over(result,skyAlong(ray)*max(light,0.5),0.25+0.5*grazing);
    float stipple=flashStipple(vWorld,normal);
    for(uint i=0u;i<flashCount;++i) {
        float amount=flashAt(vWorld,normal,i,stipple);
        if(amount<=0.0)continue;
        vec3 toLight=normalize(lights[i].position.xyz-vWorld);
        float front=saturate(dot(normal,toLight));
        lit+=lights[i].color.rgb*amount*front;
        if(!resting)specular+=lights[i].color.rgb*amount*pow(saturate(dot(normal,normalize(toLight+view))),96.0)*2.2;
    }
    if(!resting&&outdoors&&c.sun.w>0.0) {
        vec3 sun=vec3(cos(c.fx2.z)*cos(c.outdoor.w),sin(c.fx2.z)*cos(c.outdoor.w),sin(c.outdoor.w));
        specular+=c.sun.rgb*c.sun.w*pow(saturate(dot(ray,sun)),256.0)*2.0;
    }
    int column=int(floor(vUV.x/vSun.x));
    ivec4 adjacent=ivec4(int(vLightMask.x&255u),int((vLightMask.x>>8u)&255u),
                         int((vLightMask.x>>16u)&255u),int((vLightMask.x>>24u)&255u))-128;
    ivec2 row=ivec2(int((vLightMask.y>>8u)&255u),int((vLightMask.y>>16u)&255u))-128;
    column=clamp(column,row.x,row.y-1); // MSAA may shade a covered edge with its center just outside the row.
    float edge=column==row.x||column==row.y-1||column<adjacent.x||column>=adjacent.y||
               column<adjacent.z||column>=adjacent.w?1.0:0.0;
    // Once settled, chipped edges should blend into the floor's light
    // rather than reading as bright white outlines.
    vec3 edgeTint=resting?vec3(0.58,0.68,0.72):vec3(0.78,0.92,1.0);
    over(result,edgeTint*min(lit,vec3(1.2)),edge*(resting?0.32:0.52+0.25*grazing));
    specular=saturate(specular);
    float shine=max(specular.r,max(specular.g,specular.b));
    if(shine>0.004)over(result,specular/shine,shine);
    vec3 color=result.rgb/max(result.a,0.001)*c.fx.x;
    result.a*=vSun.y;
    if(c.materials.z>0.0&&c.effects.z==0.0)
        result.a*=exp(-heightFogTau(c.eye.z,vWorld.z,distance,c.fog.x,c.fog.y,c.fog.z));
    outColor=vec4(saturate(color),result.a);
}
void main() {
    if((vMode&MODE_GLASS_SHARD)!=0u) {shard();return;}
    vec2 extent=vSun,texel=floor(vUV)+0.5;
    uint seed=vStatic&0x7fffffffu;
    bool broken=(vStatic&0x80000000u)!=0u;
    bool held=(vMode&MODE_GLASS_HELD)!=0u,crack=(vMode&MODE_GLASS_CRACK)!=0u;
    vec3 normal=vec3(vTint.xy,0.0);
    vec3 toEye=c.eye.xyz-vWorld;
    float distance=length(toEye);
    vec3 view=toEye/max(distance,1e-3);
    float facing=saturate(dot(view,normal));
    float grazing=pow(1.0-facing,3.0);
    // Distance to the frame in texels, and the jagged break.
    float edge=min(min(texel.x,extent.x-texel.x),min(texel.y,extent.y-texel.y));
    float cut=0.0;
    if(broken||held||crack) {
        float bottom=tooth(texel.x,seed),top=tooth(texel.x+311.0,seed),left=tooth(texel.y+97.0,seed),right=tooth(texel.y+503.0,seed);
        cut=max(max(bottom-texel.y,top-(extent.y-texel.y)),max(left-texel.x,right-(extent.x-texel.x)));
        if(broken?cut<0.0:cut>=0.0) discard;
    }
    // Hits have one fixed location from either side of the pane.
    vec2 canonical=floor(vec2((vLightMask.y&2u)!=0u?extent.x-vUV.x:vUV.x,vUV.y))+0.5;
    vec3 mark=max(bulletMark(canonical,vLightMask.x,seed^0x51ed27u),bulletMark(canonical,vLightMask.y,seed^0x9e3779b9u));
    if(mark.x>0.5)discard;
    float light=vLight;
    if(c.effects.z==0.0) light*=clamp(1.0-vDistance/3200.0,0.35,1.0);
    vec4 result=vec4(0);
    // The glass itself: faint green-grey, thicker to the eye at a glance.
    over(result,vec3(0.38,0.5,0.5)*light,0.15+0.25*grazing);
    // A pane in the mirror's plane (lightMask.x) shows the world drawn again
    // behind it, read at its own screen position like the liquid mirror,
    // in whole reflection pixels in retro style. It is already lit and
    // exposed. Reflectance rises toward a glance, and bright things (lamps,
    // sky, fire, shots) hold up while dim walls stay faint, so it reads
    // through contrast rather than as a film. Other panes take the probe on
    // the eye's side (vLampTint: probe+1) the same way; without one they
    // fall back to the sky outdoors, and indoors to the dim room, barely.
    bool live=(vLightMask.x&1u)!=0u&&mirrorView.z>0.0;
    if(live||vLampTint>0u) {
        vec3 probed=vLampTint>0u?probeAlong(vLampTint-1u,vWorld,reflect(-view,normal)):vec3(0),mirrored=probed;
        if(live) {
            vec2 uv=(gl_FragCoord.xy-mirrorView.xy)*mirrorView.zw;
            if((uint(c.flashlightTint.w)&FEATURE_RETRO_REFLECTIONS)!=0u) {
                ivec2 size=textureSize(mirror,0);
                mirrored=texelFetch(mirror,clamp(ivec2(floor(uv*vec2(size))),ivec2(0),size-1),0).rgb;
            } else mirrored=textureLod(mirror,uv,0.0).rgb;
            // A pane that just became the mirror fades in from its probe.
            mirrored=mix(probed,mirrored/max(c.fx.x,0.01),vLampTint>0u?probeInfo.y:1.0);
        }
        float bright=smoothstep(0.08,0.6,dot(mirrored,vec3(0.299,0.587,0.114)));
        over(result,mirrored,(0.22+0.45*grazing)*mix(0.35,1.0,bright)*(live&&vLampTint==0u?probeInfo.y:1.0));
    }
    else if(vTint.z>0.5) over(result,skyAlong(reflect(-view,normal))*max(light,0.5),0.18+0.5*grazing);
    else over(result,vec3(0.3,0.33,0.32)*light,0.05+0.25*grazing);
    // Grime: dust settled along the frame and thickest on the sill, single
    // texels gathered in patches.
    float dirt=0.7*(1.0-smoothstep(0.0,12.0,edge))+0.45*(1.0-smoothstep(0.0,20.0,texel.y));
    dirt*=0.3+1.2*hash(floor(texel/4.0),seed^0x2545f491u);
    if(hash(texel,seed^0x9e3779b9u)<dirt) over(result,vec3(0.24,0.22,0.17)*light,0.3+0.3*hash(texel,seed^0x51ed27u));
    // Highlights as on glossy metal (surface_light.glsl): the lights on the
    // eye's side of the glass, walls between included, mirrored in it as a
    // tight Blinn-Phong spot that slides as the eye or the light moves, a
    // little stronger at a glance. Static lamps near the eye are among them
    // as dynamic copies. Outdoors the sun shows the same way, tighter.
    // The same lights also light the glass itself (lit), for the streaks.
    vec3 mirrored=reflect(-view,normal),specular=vec3(0),lit=vec3(light);
    float gloss=1.2*(0.7+0.6*grazing);
    float stipple=flashStipple(vWorld,normal);
    for(uint i=0u;i<flashCount;++i) {
        float amount=flashAt(vWorld,normal,i,stipple);
        if(amount<=0.0) continue;
        vec3 toLight=normalize(lights[i].position.xyz-vWorld);
        float front=saturate(dot(normal,toLight)*4.0);
        float highlight=pow(saturate(dot(normal,normalize(toLight+view))),128.0);
        specular+=lights[i].color.rgb*amount*highlight*gloss*front;
        lit+=lights[i].color.rgb*amount*front;
    }
    if(crack) {
        bool dark=(vMode&MODE_GLASS_DARK)!=0u;
        vec3 shade=dark?vec3(0.035,0.045,0.045)*light:vec3(0.8,0.9,0.94)*min(vec3(0.12)+lit*0.88,vec3(1.2));
        float alpha=dark?0.6:0.8;
        if(c.materials.z>0.0&&c.effects.z==0.0)
            alpha*=exp(-heightFogTau(c.eye.z,vWorld.z,distance,c.fog.x,c.fog.y,c.fog.z));
        outColor=vec4(saturate(shade*c.fx.x),alpha);return;
    }
    // Rain on the glass, lit like it: bright drops, dark lenses under them.
    if((vLightMask.y&1u)!=0u&&c.forward.w>0.0&&!broken) {
        float drop=rainOnGlass(texel,extent,(vLightMask.y&2u)!=0u,seed,c.forward.w);
        if(drop>0.0) over(result,saturate(vec3(0.82,0.92,1.0)*min(vec3(0.2)+lit*0.8,vec3(1.2))),drop*(0.3+0.25*grazing));
        else if(drop<0.0) over(result,vec3(0.04,0.05,0.06)*light,0.35);
    }
    // Shine streaks painted on the glass, as in Duke Nukem 3D's window
    // texture: fixed to the pane, never sliding. They slant up across it in
    // groups, one per 56-unit cell along the pane where a cell has one: a
    // wide stripe with two thin ones, two thin ones, or a single thin one,
    // each group at its own height, length and strength, tapering to points
    // at both ends. They are lit like the glass, so a flashlight beam makes
    // them flare.
    float across=texel.x-texel.y*0.75+extent.y;
    float cell=floor(across/56.0);
    float inCell=across-cell*56.0-4.0-18.0*hash(vec2(cell,1),seed);
    float kind=hash(vec2(cell,2),seed);
    float middle=extent.y*(0.3+0.4*hash(vec2(cell,3),seed));
    float reach=max(extent.y*(0.2+0.18*hash(vec2(cell,4),seed)),12.0);
    float taper=1.0-pow((texel.y-middle)/reach,2.0);
    float streak=0.0;
    if(kind<0.85&&taper>0.0) {
        float wide=kind<0.5?6.0:0.0;
        if(inCell>=0.0&&inCell<wide*taper) streak=1.0;
        else if(inCell>=wide+3.0&&inCell<wide+3.0+3.0*taper) streak=0.8;
        else if(kind<0.68&&inCell>=wide+8.0&&inCell<wide+8.0+2.0*taper) streak=0.6;
        streak*=0.7+0.3*hash(vec2(cell,5),seed);
    }
    if(streak>0.0) {
        // A whole pane's streaks keep some light so they read in dim rooms;
        // the teeth of a broken one take only the light there.
        vec3 shade=vec3(0.88,0.96,1.0)*min(broken?lit:0.45+lit*0.75,vec3(1.2));
        over(result,saturate(shade),streak*(0.3+0.22*grazing));
    }
    if(vTint.z>0.5&&c.sun.w>0.0) {
        vec3 sun=vec3(cos(c.fx2.z)*cos(c.outdoor.w),sin(c.fx2.z)*cos(c.outdoor.w),sin(c.outdoor.w));
        specular+=c.sun.rgb*c.sun.w*pow(saturate(dot(mirrored,sun)),400.0)*gloss*1.5;
    }
    specular=saturate(specular);
    float shine=max(specular.r,max(specular.g,specular.b));
    if(shine>0.004) over(result,specular/shine,shine);
    if(mark.z>0.0)over(result,vec3(0.035,0.045,0.045)*light,mark.z*0.65);
    if(mark.y>0.0)over(result,vec3(0.82,0.92,0.96)*min(vec3(0.12)+lit*0.88,vec3(1.2)),mark.y*0.9);
    // A fresh break catches the light along its edge: the room's and the
    // dynamic lights', so it glints under the flashlight and stays dark in
    // a dark room.
    if(broken&&!held&&cut<1.5) over(result,vec3(0.85,0.95,1.0)*min(lit,vec3(1.2)),0.45);
    if(result.a<=0.002) discard;
    vec3 color=result.rgb/result.a*c.fx.x;
    if(c.materials.z>0.0&&c.effects.z==0.0)
        result.a*=exp(-heightFogTau(c.eye.z,vWorld.z,distance,c.fog.x,c.fog.y,c.fog.z));
    outColor=vec4(saturate(color),result.a);
}
