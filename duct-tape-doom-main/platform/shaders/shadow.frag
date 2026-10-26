#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// Shadows use only WAD alpha coverage, independent of palette and sprite upscale.
// DECAL: bullet marks reuse the puff sprite's coverage as a dark, fading stain;
// flashlight silhouettes reuse a thing's sprite.
layout(set=2,binding=0) uniform sampler2D image;
layout(location=0) out vec4 outColor;
#include "rock_heat.glsl"
float shadowCoverage(vec2 uv) {
    vec2 size=vec2(textureSize(image,0));
    if(any(lessThan(uv,vec2(0)))||any(greaterThanEqual(uv,size))) return 0.0;
    return textureLod(image,uv/size,0.0).g;
}
void main() {
#ifdef DECAL
    if((vMode&2048u)!=0u){outColor=rockHeat();return;}
    float alpha=shadowCoverage(vUV);
    // MODE_HARD_SILHOUETTE: hard-edged silhouettes keep the sprite's pixel outline.
    // MODE_STIPPLED: as the flashlight's other shadows (flashStipple in
    // lighting.glsl), the edge breaks into a 2x2 ordered dither in 4 steps.
    // Coverage spread a texel each way is held against the pattern, laid on
    // the sprite's own texels: the stipple matches the outline's pixel steps,
    // moves with the shadow so it never crawls, and widens with the shadow's
    // enlargement, the farther the wall behind the thing.
    if((vMode&MODE_STIPPLED)!=0u&&(vMode&MODE_HARD_SILHOUETTE)!=0u) {
        float spread=alpha*0.4+0.15*(shadowCoverage(vUV+vec2(1,0))+shadowCoverage(vUV-vec2(1,0))+
                                     shadowCoverage(vUV+vec2(0,1))+shadowCoverage(vUV-vec2(0,1)));
        ivec2 q=ivec2(floor(vUV))&1;
        alpha=step((float(2*(q.x^q.y)+q.y)+0.5)/4.0,spread);
    } else if((vMode&MODE_HARD_SILHOUETTE)!=0u) alpha=step(0.5,alpha);
    if(alpha<0.02) discard;
    // Square rocket fire: retain translucent colour while breaking up the
    // flat fill like the reference particle renderer. Screen-sized grain
    // stays fine when the plume passes close to the camera.
    if((vMode&4096u)!=0u){
        vec2 q=floor(gl_FragCoord.xy);
        float grain=fract(52.9829189*fract(dot(q,vec2(.06711056,.00583715))));
        // Original square fire uses coverage stippling: bright surviving pixels,
        // not a second opacity multiplier that turns the flame into pale haze.
        alpha=step(grain,alpha*vLight);
        if(alpha<.5)discard;
        outColor=vec4(vTint,1);return;
    }
    outColor=vec4(vTint,alpha*vLight);
#else
    float softness=1.0;
    float alpha=shadowCoverage(vUV)*0.4;
    alpha+=shadowCoverage(vUV+vec2(softness,0))*0.15;
    alpha+=shadowCoverage(vUV-vec2(softness,0))*0.15;
    alpha+=shadowCoverage(vUV+vec2(0,softness))*0.15;
    alpha+=shadowCoverage(vUV-vec2(0,softness))*0.15;
    float along=clamp(vUV.y/float(textureSize(image,0).y),0.0,1.0);
    outColor=vec4(0,0,0,alpha*vLight*(0.35+0.65*along));
#endif
}
