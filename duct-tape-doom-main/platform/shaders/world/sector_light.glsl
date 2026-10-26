// Sector light across the map: seams between sectors, contact shading,
// sun patches and sky sectors, and light flowing through openings.
// Part of world.frag, which declares the resources these read.
// Sector light blended across open lines, from cells of this sector or of
// the neighbor it blends with, by the floor or the ceiling weight. Other cells
// (void, unrelated sectors) are skipped.
float seamLight(vec2 xy,uint sector,bool ceiling,float fallback) {
    vec2 g=(xy-c.map.xy)*c.map.z-0.5;ivec2 q=ivec2(floor(g));vec2 f=fract(g);
    ivec2 size=textureSize(seams,0);
    float value=0.0,total=0.0;
    for(int n=0;n<4;++n) {
        ivec2 o=ivec2(n&1,n>>1);
        uvec4 cell=uvec4(round(texelFetch(seams,clamp(q+o,ivec2(0),size-1),0)*65535.0));
        if(cell.x!=sector&&cell.y!=sector) continue;
        float w=(o.x!=0?f.x:1.0-f.x)*(o.y!=0?f.y:1.0-f.y);
        float l=sectorInfo[cell.x].x;
        if(cell.y!=0xFFFFu) l=mix(l,sectorInfo[cell.y].x,float(ceiling?cell.z>>8:cell.z&255u)/510.0);
        value+=l*w;total+=w;
    }
    return total>0.0001?value/total:fallback;
}
// Contact shading: floors and ceilings darken near walls, walls near their
// sector's floor and (non-sky) ceiling and in inside corners, each as a
// crease: darkest at the contact and mostly gone within half its reach.
// Sector heights follow moving floors. Distances in the contact map run to
// contactRange (scene3d/map.cpp).
const float contactRange=48.0;
float crease(float distance,float reach) {float open=1.0-saturate(distance/reach);return open*open;}
// A floor's or ceiling's distance to the nearest wall, blended from the four
// nearest cells of this sector only, so the bright cells on top of a step
// do not wash out the crease at its foot.
float flatContact(vec2 xy,uint sector,bool ceiling) {
    vec2 g=(xy-c.map.xy)*c.map.z-0.5;ivec2 q=ivec2(floor(g));vec2 f=fract(g);
    ivec2 size=textureSize(contact,0);
    float value=0.0,total=0.0;
    for(int n=0;n<4;++n) {
        ivec2 o=ivec2(n&1,n>>1),cell=clamp(q+o,ivec2(0),size-1);
        if(uint(round(texelFetch(seams,cell,0).x*65535.0))!=sector) continue;
        float w=(o.x!=0?f.x:1.0-f.x)*(o.y!=0?f.y:1.0-f.y)+1e-4;
        vec4 v=texelFetch(contact,cell,0);
        value+=(ceiling?v.g:v.r)*w;total+=w;
    }
    return total>0.0?value/total*contactRange:contactRange;
}
float contactShade(vec3 world,vec3 normal,uint sector) {
    vec4 info=sectorInfo[sector];
    if(normal.z>0.5) return 1.0-0.5*crease(flatContact(world.xy,sector,false),40.0);
    if(normal.z<-0.5) return 1.0-0.4*crease(flatContact(world.xy,sector,true),32.0);
    float shade=1.0-0.45*crease(world.z-info.y,40.0);
    if(info.w==0.0) shade*=1.0-0.35*crease(info.z-world.z,32.0);
    // Inside corners: a point cornerReach out from the wall lies that far
    // from the wall itself, but only as far from a wall meeting this one as
    // the fragment is from the corner. Walls holding up both floor and
    // ceiling shade the corner's full height; low and high ones (ledges,
    // steps, lintels) only near the floor or ceiling.
    const float cornerReach=32.0;
    vec2 uv=(world.xy+normal.xy*cornerReach-c.map.xy)*c.map.z/vec2(textureSize(contact,0));
    vec2 near=textureLod(contact,uv,0.0).rg*contactRange;
    vec2 corner=vec2(crease(near.x,cornerReach),crease(near.y,cornerReach));
    float cornered=max(min(corner.x,corner.y),
        max(corner.x*crease(world.z-info.y,48.0),info.w==0.0?corner.y*crease(info.z-world.z,48.0):0.0));
    return shade*(1.0-0.5*cornered);
}
// Sky sectors: x how enclosed their opening is (0 open air, 1 a hole in a
// ceiling), y the outdoor floor; unpacks packOutdoor in scene3d/bake_sun.cpp.
vec2 outdoorShape(uint sector) {
    float v=sectorInfo[sector].w-1.0;
    return vec2(floor(v)/255.0,round(fract(v)*256.0)/255.0);
}
// Sky sectors drop whole Doom light steps in shadow; indoor surfaces rise
// toward the outdoor level where sun falls through an opening, and so do
// enclosed sky sectors, by their enclosure. share is the sunlit part of the
// result, which takes the sun's tint. Keep sunLightAt in scene3d/bake_sun.cpp equivalent.
float sunLight(uint sector,float light,float visible,out float share) {
    float lift=round(max(0.0,c.sun.w-light)*visible*16.0)/16.0;
    if(sectorInfo[sector].w>0.0) {
        // Outdoors shade drops c.outdoor.x of the light and sun adds c.outdoor.y
        // (Game_Sun; walls by how squarely they face it, in the bake).
        float shaded=light-round(light*c.outdoor.x*(1.0-visible)*16.0)/16.0+round(light*c.outdoor.y*visible*16.0)/16.0;
        float result=round(mix(shaded,light+lift,outdoorShape(sector).x)*16.0)/16.0;
        share=max(0.0,result-light)/max(result,0.001);
        return result;
    }
    share=lift/max(light+lift,0.001);
    return light+lift;
}
// Sector light flow (bakeFlow): the cell's light rises toward the current
// light of the sector it sees through openings, in 1/16 steps. Walls read
// the cell one step toward their visible side. Keep flowLightAt in
// scene3d/bake_indirect.cpp equivalent.
float flowLight(vec3 world,vec3 normal,uint sector,float light) {
    vec2 at=abs(normal.z)>0.5?world.xy:world.xy+normal.xy/c.map.z;
    if((uint(c.flashlightTint.w)&FEATURE_TEXEL_BAKE)!=0u&&abs(normal.z)>0.5) at=floor(at)+0.5;
    ivec2 cell=clamp(ivec2(floor((at-c.map.xy)*c.map.z)),ivec2(0),textureSize(seams,0)-1);
    uvec4 s=uvec4(round(texelFetch(seams,cell,0)*65535.0));
    if(s.x!=sector||s.w==0xFFFFu) return light;
    return light+round(max(0.0,sectorInfo[s.w].x-light)*texelFetch(contact,cell,0).a*0.8*16.0)/16.0;
}
// The light a fragment takes from its sector and the bakes, in Doom's 1/16
// steps: seams and flow across sector edges, contact shading, the baked
// surface data, sun and sky light, occlusion and the distance fade. shown is
// the light before contact shading; sunShare and skyShare the parts that take
// the sun's and the sky's tint.
struct SectorLit { float light,shown,sunShare,skyShare,occlusion; vec4 baked,direction; vec3 bounce; };
SectorLit sectorLighting(vec3 normal,bool mapped,uint sector,uint bakeFlags) {
    float light=vLight,sunShare=0.0;
    if(mapped&&c.map.w>0.0&&abs(normal.z)>0.5) light=seamLight(vWorld.xy,sector,normal.z<0.0,light);
    if(mapped&&(bakeFlags&FEATURE_LIGHT_FLOW)!=0u) light=flowLight(vWorld,normal,sector,light);
    // Static light fills the headroom above this light, which seams and flow
    // already carry smoothly across sector edges, so a bright sector beside a
    // dark one shows no edge in the lamp's color.
    float shownLight=light;
    // Contact shading darkens the baked lamp and bounce light too (through
    // occlusion), or lamp-lit rooms would wash their corners out.
    float contact=mapped&&c.map.w>0.0?contactShade(vWorld,normal,sector):1.0;
    light*=contact;
    vec3 bounce=vec3(0);vec2 ambient=vec2(0,1);vec4 direction=vec4(0.5,0.5,0,0);
    vec4 baked=mapped?bakedSurface(normal,sector,bounce,ambient,direction):vec4(0,0,0,-1);
    // Under a sky, sun shadow and sky occlusion stop at the light of the
    // indoor sectors the sector opens into (see outdoorShape).
    float outdoorFloor=mapped&&sectorInfo[sector].w>0.0?min(light,outdoorShape(sector).y):0.0;
    if(mapped&&c.sun.w>0.0&&baked.a>=0.0) light=sunLight(sector,light,baked.a,sunShare);
    // Sky light: outdoors, surfaces that see less sky (under overhangs, in
    // alcoves) drop light steps and the rest take a little of the sky's tint;
    // indoors, surfaces that see sky through openings rise toward the sky
    // sectors' light, the lift taking the tint; enclosed sky sectors blend
    // toward the indoor rule by their enclosure. All in 1/16 steps. With
    // window light (FEATURE_WINDOW_LIGHT) the openings light indoor surfaces from the
    // bake, with direction, so they take no lift here.
    float skyShare=0.0;
    if(mapped&&c.fx2.y>0.0) {
        float lift=round(max(0.0,c.fx2.y-light)*saturate(ambient.x*1.2)*0.7*16.0)/16.0;
        float indoorShare=lift/max(light+lift,0.001);
        if(sectorInfo[sector].w>0.0) {
            float enclosed=outdoorShape(sector).x;
            float shaded=light-round(light*0.25*(1.0-ambient.x)*16.0)/16.0;
            light=round(mix(shaded,light+lift,enclosed)*16.0)/16.0;
            skyShare=mix(c.outdoor.z*ambient.x,indoorShare,enclosed);
        } else if((bakeFlags&FEATURE_WINDOW_LIGHT)==0u) {skyShare=indoorShare;light+=lift;}
    }
    light=max(light,outdoorFloor);
    // Baked occlusion (FEATURE_BAKED_OCCLUSION) darkens the sector and baked light, not dynamic lights.
    float occlusion=1.0;
    if(mapped&&(bakeFlags&FEATURE_BAKED_OCCLUSION)!=0u) {occlusion=round((1.0-0.45*(1.0-ambient.y))*16.0)/16.0;light*=occlusion;}
    occlusion*=contact;
    if(c.effects.z==0.0) light*=clamp(1.0-vDistance/3200.0,0.35,1.0);
    else light=1.0;
    return SectorLit(light,shownLight,sunShare,skyShare,occlusion,baked,direction,bounce);
}
