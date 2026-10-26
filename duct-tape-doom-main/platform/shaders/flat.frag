#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// Debris: flat colored billboards; tint is the lit color, light the opacity.
// SOLID: the crosshair. HEAT: hot-air volumes lower the target's alpha to
// 1-heat (alpha-only writes, MIN blending); present.frag shimmers there.
layout(location=0) out vec4 outColor;
#include "rock_heat.glsl"
void main() {
#ifdef SOLID
    outColor=vec4(0.85,1,0.85,0.8);
#elif defined(HEAT)
    outColor=vec4(0,0,0,1.0-saturate(vLight));
#else
    if((vMode&8192u)!=0u){
        // Liquid rivulet: moving thickness, soft edges and a small wet highlight.
        // Phase comes from the effect, so no extra textures or render pass.
        float bend=.15*sin(vUV.y*2.7-vSun.x*5.0)+.07*sin(vUV.y*6.1+vSun.x*3.0);
        float x=vUV.x-bend;
        float width=.55+.12*sin(vUV.y*3.8-vSun.x*7.0);
        float edge=1.0-smoothstep(width*.4,width,abs(x));
        float beads=.65+.35*sin(vUV.y*4.2-vSun.x*8.0);
        float shine=exp(-pow((x+.17)/.10,2.0))*.2;
        outColor=vec4(vTint*(.7+.3*beads)+shine*max(vTint,vec3(.12)),vLight*edge);
    }else if((vMode&16384u)!=0u){
        float radius=length(vUV);
        float phase=fract(vSun.x);
        float alpha=vSun.x<0.0?1.0-smoothstep(.2,1.0,radius):
            (1.0-smoothstep(.045,.12,abs(radius-(.2+.65*phase))))*(1.0-phase);
        outColor=vec4(vTint,vLight*alpha);
    }else if((vMode&2048u)!=0u)outColor=rockHeat();
    else outColor=vec4(vTint,vLight);
#endif
}
