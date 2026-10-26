// Wear (scene3d/wear.cpp, scene3d/weather.cpp): what weather and neglect have
// left on the map's surfaces, all on the texel grid of Doom's one-unit texels.
// - FEATURE_WINDOW_STREAKS: rain runs down the wall below a window from its
//   sill, column by column to ragged ends, darkest at the sill; rusty on
//   metal. They darken further while it rains.
// - Under the sky, while it rains (c.up.w), the ground lies wet in steps
//   baked per map cell (weather.cpp bakeWetness, in the floor light's alpha):
//   nearly dry in the lee of walls, soaked at the foot of walls facing the
//   rain, patches damp, wet and soaked between, puddles darkest. Without the
//   bake, patches of puddles. Walls a little, most near their feet. Where rain blew in through a
//   broken pane the sill and floor in front of it are wet. Darkening only:
//   damp, never glossy.
// - Leaks while it rains: a crack in the ceiling, damp round it, and a
//   small puddle on the floor below.
// - Worn plates (Game_WornPlate): plain metal plates marked by use, see
//   plateGrime. Not weather: they stay as they are.
// Records follow the sector entries in sectorInfo from c.right.w: per
// sector (first record, count, -, seed), then two entries a record:
//   1 streak: (1, line*2+side, sill height, amount; negative: rust), (a, b)
//   3 wet patch: (3, depth in, -, amount), (a, b) along the room's side
//   4 ceiling crack: (4, center, reach), (seed, -, -, -)
//   5 puddle: (5, center, radius), (seed, -, -, amount)
// Part of world.frag, which declares the resources these read.
struct Wear { float wet,streak;bool rust;float crack,grime; };
float wearHash(ivec2 p,uint seed) {return float(tileHash(p,seed)&0xffffu)/65535.0;}
float wearNoise(vec2 p,float cell,uint seed) {
    vec2 g=p/cell;ivec2 i=ivec2(floor(g));vec2 f=fract(g);f=f*f*(3.0-2.0*f);
    return mix(mix(wearHash(i,seed),wearHash(i+ivec2(1,0),seed),f.x),mix(wearHash(i+ivec2(0,1),seed),wearHash(i+ivec2(1,1),seed),f.x),f.y);
}
// A rain streak's darkness at a wall texel: column (along the wall from
// the window's edge, len wide) and depth below the sill. Columns run in
// groups of three to their own lengths, a few texels missing on the way.
float streakAt(float along,float len,float depth,float below,uint seed) {
    if(along<0.0||along>len||depth<0.0) return 0.0;
    int column=int(floor(along)),row=int(floor(depth));
    float own=wearHash(ivec2(column,0),seed),group=wearHash(ivec2(column/3,1),seed);
    float reach=4.0+below*(0.2+0.8*mix(group,own,0.35)*mix(group,own,0.35));
    if(min(along,len-along)<3.0) reach*=0.5;
    if(depth<2.0) return 0.8; // The drip line just under the sill.
    if(depth>reach||wearHash(ivec2(column,row),seed^0x68bc21ebu)<0.12) return 0.0;
    return pow(1.0-depth/reach,0.7)*(0.55+0.45*own);
}
float segmentDistance(vec2 p,vec2 a,vec2 b) {
    vec2 ab=b-a;float t=clamp(dot(p-a,ab)/max(dot(ab,ab),1e-4),0.0,1.0);
    return length(p-a-ab*t);
}
// A ceiling crack at a texel t: three branches from the center, each four
// steps turning as they go and thinning, about a texel wide; damp in a
// ragged patch round the center. Returns (crack, damp).
vec2 crackAt(vec2 t,vec2 center,float reach,uint seed) {
    float r=length(t-center);
    if(r>reach+2.0) return vec2(0.0);
    float crack=0.0,start=wearHash(ivec2(0,0),seed)*6.2831853;
    for(int branch=0;branch<3;++branch) {
        float angle=start+float(branch)*2.0944+(wearHash(ivec2(branch,1),seed)-0.5)*1.2,width=0.65;
        vec2 p=center;
        for(int step=0;step<4;++step) {
            angle+=(wearHash(ivec2(branch,step+2),seed)-0.5)*1.5;
            vec2 q=p+vec2(cos(angle),sin(angle))*reach*0.25*(0.7+0.6*wearHash(ivec2(branch,step+8),seed));
            if(segmentDistance(t,p,q)<width) crack=max(crack,1.0-0.15*float(step));
            p=q;width*=0.85;
        }
    }
    float limit=reach*0.5*(0.7+0.6*wearNoise(t,6.0,seed^0x9e3779b9u));
    return vec2(crack,r<limit?0.7:0.0);
}
// The baked wetness at a ground texel (-1: none baked), each texel reading
// a cell up to half a cell off, so the steps meet in ragged whole texels.
float bakedWetness(vec2 t) {
    ivec2 size=textureSize(flatLight,0),layer=max(size/ivec2(1,4),ivec2(1));
    vec2 jitter=vec2(wearHash(ivec2(t),0x2545f491u),wearHash(ivec2(t),0x7f4a7c15u))-0.5;
    ivec2 cell=clamp(ivec2(floor((t-c.map.xy)*c.map.z+jitter)),ivec2(0),layer-1);
    float a=texelFetch(flatLight,cell,0).a;
    return a>0.0?(a*255.0-1.0)/254.0:-1.0;
}
Wear surfaceWear(vec3 world,vec3 normal,uint sector,uint flags) {
    Wear w=Wear(0.0,0.0,false,0.0,0.0);
    uint base=uint(c.right.w);
    vec4 info=sectorInfo[sector];
    bool outdoors=info.w>0.0;
    bool ground=normal.z>0.5,wall=abs(normal.z)<=0.5;
    vec2 t=floor(world.xy)+0.5;
    if(normal.z<-0.5) {
        if(base==0u) return w;
        vec4 head=sectorInfo[base+sector];
        uint first=uint(head.x),count=uint(head.y);
        for(uint i=0u;i<count;++i) {
            vec4 a=sectorInfo[first+2u*i];
            if(uint(a.x)!=4u) continue;
            vec2 k=crackAt(t,a.yz,a.w,uint(sectorInfo[first+2u*i+1u].x)*0x9e3779b9u+1u);
            w.crack=max(w.crack,k.x);w.wet=max(w.wet,k.y);
        }
        return w;
    }
    if(ground) {
        if(outdoors&&c.up.w>0.0) {
            float baked=bakedWetness(t);
            if(baked>=0.0) w.wet=c.up.w*baked;
            else w.wet=c.up.w*(0.3+0.7*smoothstep(0.55,0.72,wearNoise(t,44.0,0x3c6ef372u)));
        }
        if(base==0u) return w;
        vec4 head=sectorInfo[base+sector];
        uint first=uint(head.x),count=uint(head.y),seed=uint(head.w);
        for(uint i=0u;i<count;++i) {
            vec4 a=sectorInfo[first+2u*i],b=sectorInfo[first+2u*i+1u];
            if(uint(a.x)==5u) {
                float r=length(t-a.yz),edge=a.w*(0.75+0.5*wearNoise(t,4.0,uint(b.x)*0x85ebca6bu+3u));
                if(r<edge+2.0) w.wet=max(w.wet,(r<edge?1.0:0.6)*b.w);
                continue;
            }
            if(uint(a.x)!=3u) continue;
            vec2 edge=b.zw-b.xy;float len=length(edge);
            if(len<1.0) continue;
            vec2 dir=edge/len,d=t-b.xy;
            float along=dot(d,dir),across=abs(dot(d,vec2(dir.y,-dir.x)));
            float limit=a.y*(0.5+0.5*wearNoise(vec2(along,0.0),10.0,seed^0x1b873593u));
            if(along<-2.0||along>len+2.0||across>limit) continue;
            float inner=1.0-across/limit;
            if(wearHash(ivec2(t),seed^0x85ebca6bu)*0.5<inner) w.wet=max(w.wet,a.w*(0.55+0.45*inner));
        }
        return w;
    }
    if(!wall) return w;
    if(outdoors&&c.up.w>0.0) w.wet=c.up.w*(0.2+0.3*(1.0-saturate((world.z-info.y)/64.0)));
    if(base==0u||(flags&FEATURE_WINDOW_STREAKS)==0u||vStatic==0u) return w;
    vec4 head=sectorInfo[base+sector];
    uint first=uint(head.x),count=uint(head.y),key=((vStatic-1u)>>3)*2u+(((vStatic-1u)>>2)&1u);
    for(uint i=0u;i<count;++i) {
        vec4 a=sectorInfo[first+2u*i];
        if(uint(a.x)!=1u||uint(a.y)!=key||world.z>=a.z) continue;
        vec4 b=sectorInfo[first+2u*i+1u];
        vec2 edge=b.zw-b.xy;float len=length(edge);
        if(len<1.0) continue;
        float along=dot(world.xy-b.xy,edge/len);
        float streak=streakAt(along,len,a.z-world.z,a.z-info.y,uint(a.y)*0x9e3779b9u)*abs(a.w);
        // Outside they darken while it rains.
        if(outdoors) streak*=1.0+0.4*c.up.w;
        if(streak>w.streak) {w.streak=streak;w.rust=a.w<0.0;}
    }
    return w;
}
// A worn plate's grime at a texel, in thirds: packed into the painted seams
// (the relief's steep texels) and a ragged texel round them, faint dull
// blotches (a third at most) over the plate, and on walls boot scuffs in the lowest 16 units, short
// dark marks along texel rows that thin out with height. Set per wall
// section (section, vStatic) so repeats and neighbors differ.
float plateGrime(vec2 uv,vec3 world,vec3 normal,uint sector,uint section) {
    ivec2 p=ivec2(floor(uv)),size=textureSize(relief,0);
    uint seed=section*0x9e3779b9u+0x27d4eb2fu;
    float seam=0.0;
    const ivec2 near[5]=ivec2[5](ivec2(0),ivec2(1,0),ivec2(-1,0),ivec2(0,1),ivec2(0,-1));
    for(int i=0;i<5;++i) {
        vec2 slope=(texelFetch(relief,wrapTexel(p+near[i],size),0).rg*255.0-128.0)/127.0;
        seam=max(seam,smoothstep(0.1,0.3,length(slope))*(i==0?1.0:0.7));
    }
    seam*=0.55+0.45*wearHash(p,seed);
    vec2 t=vec2(p)+0.5;
    float blotch=0.6*wearNoise(t,7.0,seed^0x1b873593u)+0.4*wearNoise(t,2.5,seed^0xcc9e2d51u);
    float grime=max(seam*0.8,smoothstep(0.55,0.8,blotch)*0.45);
    if(abs(normal.z)<=0.5) {
        float height=world.z-sectorInfo[sector].y;
        if(height<16.0) {
            int mark=(p.x+int(tileHash(ivec2(0,p.y),seed)&3u))>>2;
            float chance=0.3*(1.0-height/16.0);
            if(wearHash(ivec2(mark,p.y),seed^0x85ebca6bu)<chance&&wearHash(p,seed^0xc2b2ae35u)>0.2) grime=max(grime,0.7);
        }
    }
    return round(saturate(grime)*3.0)/3.0;
}
// The wear on a texel's color, before lighting; glowing texels stay clean.
vec3 applyWear(vec3 color,Wear w,float emission) {
    float keep=1.0-emission;
    if(w.streak>0.0) color*=mix(vec3(1),w.rust?vec3(0.78,0.5,0.32):vec3(0.5,0.52,0.46),min(w.streak,1.0)*keep);
    if(w.wet>0.0) color*=1.0-0.36*min(w.wet,1.0)*keep;
    if(w.crack>0.0) color*=1.0-0.75*w.crack*keep;
    if(w.grime>0.0) color*=mix(vec3(1),vec3(0.6,0.55,0.48),w.grime*keep);
    return color;
}
