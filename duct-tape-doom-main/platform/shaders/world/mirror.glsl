// Liquid floors mirroring the reflection pass.
// Part of world.frag, which declares the resources these read.
float texelLuma(ivec2 p) {
    return dot(texel(image,palette,p,true).rgb,vec3(0.299,0.587,0.114));
}
// Big pools read as one body of water: poolNoise (shore.glsl) over cells of
// a few hundred units, turned off the map grid so it never lines up with
// the flat's tiling.
// Calm (0) and rough (1) stretches of a pool: broad regions with soft edges,
// not a smooth gradient, so the surface has a few distinct moods.
float poolRough(vec2 xy) {
    vec2 p=mat2(0.8,0.6,-0.6,0.8)*xy;
    float n=poolNoise(p/360.0)*0.7+poolNoise(p.yx/150.0+17.0)*0.3;
    return smoothstep(0.4,0.6,n);
}
// The pool's own tone (multiplies a liquid's color): calm stretches a little
// deeper, rough ones a little paler, with a slow drift in brightness and hue
// across a lake. A few percent at most; the WAD's colors stay in charge.
vec3 poolTone(vec2 xy) {
    float rough=poolRough(xy);
    vec2 p=mat2(0.6,-0.8,0.8,0.6)*xy;
    float drift=poolNoise(p/640.0+43.0)-0.5,shade=poolNoise(p.yx/520.0+71.0)-0.5;
    return vec3(mix(0.95,1.04,rough)+drift*0.08)*(vec3(1)+vec3(-0.03,0.0,0.03)*shade*2.0);
}
// MODE_MIRROR_FLOOR: a liquid floor mirroring the reflection pass. albedo is the
// liquid's own texel color.
void liquidMirror(inout vec4 color,vec3 albedo) {
    // The mirrored camera maps the surface to the same screen point; a
    // scrolling wobble distorts it, and grazing views reflect more. Broad
    // wave bands (150-300 units, slanted off the map grid) carry most of it
    // and say "large body of water"; a weaker short ripple rides on them.
    // Calm stretches of the pool barely move the mirror, rough ones break it.
    // Retro style steps the wobble at 12 Hz, snaps it to whole reflection
    // pixels, skips filtering and quantizes to the WAD palette.
    bool retro=(uint(c.flashlightTint.w)&FEATURE_RETRO_REFLECTIONS)!=0u;
    float motion=1.0-0.8*smoothstep(192.0,1024.0,length(vWorld-c.eye.xyz));
    float t=retro?floor(c.fog.w*12.0)/12.0:c.fog.w;
    float rough=poolRough(vWorld.xy);
    vec2 w=vWorld.xy;
    vec2 bands=vec2(sin(dot(w,vec2(0.033,0.012))+t*0.9)+0.6*sin(dot(w,vec2(-0.010,0.027))+t*0.7),
                    cos(dot(w,vec2(0.014,-0.030))-t*0.8)+0.6*cos(dot(w,vec2(0.024,0.019))+t*1.1));
    vec2 ripple=vec2(sin(w.x*0.07+t*1.9)+sin(w.y*0.11+t*1.3),
                     cos(w.y*0.08-t*1.6)+cos(w.x*0.05+t*2.1));
    vec2 wobble=(bands*0.8+ripple*0.35)*mix(0.3,1.5,rough)*(retro?0.005:0.003);
    // The reflection's alpha: how near the mirrored point lies to the
    // surface along its ray (1 touching it). Far things fade and break up.
    float near=textureLod(reflection,(gl_FragCoord.xy-c.water.xy)*c.water.zw+wobble,0.0).a;
    // The liquid's own artwork breaks the mirror up texel by texel: the
    // slope of its luminance shoves the lookup, so the image shatters on
    // Doom's texel grid and churns with the flat's animation.
    ivec2 cell=ivec2(floor(vUV));
    float here=texelLuma(cell);
    wobble+=vec2(texelLuma(cell+ivec2(1,0))-here,texelLuma(cell+ivec2(0,1))-here)*mix(0.15,0.04,near);
    // Bound distortion in reference pixels, not a fraction of the screen.
    // Large gradients used to drag distant silhouettes across the pool.
    vec2 limit=vec2(2.5*max(c.fx2.x,1.0))*c.water.zw;
    wobble=clamp(wobble,-limit,limit)*motion;
    vec2 uv=(gl_FragCoord.xy-c.water.xy)*c.water.zw+wobble;
    // Never stretch a clamped border texel into the visible floor.
    float edge=min(min(uv.x,uv.y),min(1.0-uv.x,1.0-uv.y));
    float valid=smoothstep(0.0,0.025,edge);
    near=textureLod(reflection,uv,0.0).a;
    vec3 mirrored;
    if(retro) {
        ivec2 size=textureSize(reflection,0);
        mirrored=texelFetch(reflection,clamp(ivec2(floor(uv*vec2(size))),ivec2(0),size-1),0).rgb;
        mirrored=texelFetch(paletteLUT,ivec3(round(saturate(mirrored)*31.0)),0).rgb;
    } else mirrored=textureLod(reflection,uv,0.0).rgb;
    // Murky, not glass: the liquid stains what it mirrors in its own hue,
    // and only bright things (lights, sky, fire) punch through; dim walls
    // sink into the water. Grazing views still reflect more. The game's
    // water clarity (c.eye.w, Game_WaterClarity) clears it toward a pool
    // that mirrors the walls too, stained less.
    float clear=c.eye.w;
    float hue=max(max(albedo.r,albedo.g),max(albedo.b,0.05));
    mirrored*=mix(vec3(1),albedo/hue,mix(0.3,0.15,clear));
    float bright=smoothstep(0.08,0.6,dot(mirrored,vec3(0.299,0.587,0.114)));
    float grazing=1.0-saturate(abs(normalize(c.eye.xyz-vWorld).z));
    // Calm stretches mirror a little more clearly than rough ones.
    color.rgb=mix(color.rgb,mirrored,valid*(mix(0.18,0.35,clear)+0.42*pow(grazing,3.0))*mix(1.15,0.85,rough)*mix(mix(0.55,0.85,clear),1.0,bright)*mix(mix(0.15,0.5,clear),1.0,near));
}
