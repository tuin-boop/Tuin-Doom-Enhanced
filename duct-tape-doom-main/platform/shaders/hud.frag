#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(set=2,binding=0) uniform sampler2D image;
layout(location=0) out vec4 outColor;
void main() {outColor=textureLod(image,vUV,0.0);outColor.a*=vLight;}
