#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(set=2,binding=0) uniform sampler2D image;
layout(set=2,binding=1) uniform sampler2D bloom;
layout(set=2,binding=2) uniform sampler2D hud;
layout(set=2,binding=3) uniform sampler3D paletteLUT;
layout(std140,set=3,binding=0) uniform HudRect { vec4 hudRect; vec4 hudDock; };
layout(std140,set=3,binding=1) uniform Camera CAMERA_BLOCK c;
layout(set=2,binding=4) uniform sampler2D photoDepth;
layout(std140,set=3,binding=2) uniform Photo { vec4 photo; vec4 shutter; };
layout(location=0) out vec4 outColor;
float photoZ(vec2 uv){float d=textureLod(photoDepth,uv,0).r;return c.projection.z*c.projection.w/(c.projection.w-d*(c.projection.w-c.projection.z));}
void main() {
    vec4 color=textureLod(image,vUV,0.0);
    if(photo.x>0.5&&photo.y>0.0){
        float z=photoZ(vUV),coc=clamp(abs(z-photo.z)/max(z,1.0)*photo.y*5.0,0.0,12.0);
        vec3 sum=color.rgb;float total=1.0;
        for(int i=0;i<24;++i){float a=float(i)*2.399963,r=sqrt((float(i)+.5)/24.0);
            vec2 uv=clamp(vUV+vec2(cos(a),sin(a))*r*coc/vec2(textureSize(image,0)),vec2(0),vec2(1));
            float other=photoZ(uv);float w=other<z-max(8.0,photo.z*.08)?0.0:1.0;
            sum+=textureLod(image,uv,0).rgb*w;total+=w;
        }color.rgb=sum/total;
    }
    // Heat haze: hot-air volumes left 1-heat in alpha. Rows of Doom pixels
    // slide sideways by whole Doom pixels at the tic rate, like an old
    // raster wobble; samples outside the hot area (weapon, HUD) are kept.
    float heat=1.0-color.a;
    if(heat>0.01&&c.fx2.x>0.0) {
        vec2 size=vec2(textureSize(image,0));
        float row=floor(vUV.y*size.y/c.fx2.x),tic=floor(c.fog.w*35.0);
        float shift=round(heat*(1.4*sin(row*0.83+tic*0.61)+0.7*sin(row*0.29-tic*0.37)));
        if(shift!=0.0) {
            vec4 moved=textureLod(image,vUV+vec2(shift*c.fx2.x/size.x,0.0),0.0);
            if(moved.a<0.999) color.rgb=moved.rgb;
        }
    }
    if(c.materials.y>0.0) {
        vec2 uv=(vUV-hudRect.xy)/hudRect.zw;
        float coverage=all(greaterThanEqual(uv,vec2(0)))&&all(lessThanEqual(uv,vec2(1)))?textureLod(hud,vec2(uv.x,uv.y*.5),0.0).a:0.0;
        for(int group=0;group<3;++group) {
            float shift=hudDock.x*(group==0?-hudRect.x:group==2?1.0-hudRect.x-hudRect.z:0.0);
            vec2 panel=(vUV-hudRect.xy-vec2(shift,0))/hudRect.zw;
            float lo=group==0?0.0:group==1?136.0/320.0:202.0/320.0;
            float hi=group==0?136.0/320.0:group==1?202.0/320.0:1.0;
            if(panel.x>=lo&&panel.x<hi&&panel.y>=0.0&&panel.y<=1.0)
                coverage=max(coverage,textureLod(hud,vec2(panel.x,.5+panel.y*.5),0.0).a);
        }
        // Screen-like additive glow retains detail without clipping to white.
        vec3 glow=textureLod(bloom,vUV,0.0).rgb*(1.0-coverage)*0.8;
        color.rgb=1.0-(1.0-color.rgb)*exp(-glow);
    }
    if(c.materials.w>0.0) color.rgb=texelFetch(paletteLUT,ivec3(round(saturate(color.rgb)*31.0)),0).rgb;
    if(photo.x>0.5){
        float l=dot(color.rgb,vec3(.2126,.7152,.0722));
        if(photo.w==1.0)color.rgb=vec3(l);
        else if(photo.w==2.0)color.rgb=mix(color.rgb,vec3(l)*vec3(1.2,.96,.72),.65);
        else if(photo.w==3.0)color.rgb=color.rgb*vec3(.80,1.03,1.20);
        else if(photo.w==4.0)color.rgb=clamp((mix(vec3(l),color.rgb,1.35)-.5)*1.15+.5,0.0,1.0);
    }
    outColor=vec4(mix(color.rgb,vec3(1.0),shutter.x),1.0);
}
