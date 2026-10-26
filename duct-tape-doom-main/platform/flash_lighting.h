/* Shared CPU flash sampling, also exercised by the data-free checks.
 * Keep flashAt in platform/shaders/lighting.glsl equivalent. */
#ifndef DOOM_FLASH_LIGHTING_H
#define DOOM_FLASH_LIGHTING_H
#include <math.h>
#include <stdint.h>
#include <stddef.h>
/* baked: the part of the light already in the bake maps (0: dynamic only). */
typedef struct { float position[4],strength; uint32_t first,count; float baked; float color[4],direction[4]; } doom_flash_t;
typedef struct { float line[4],opening[4]; } doom_light_blocker_t;
/* Pick the strongest bright, saturated hue cluster, ignoring transparent,
 * dark and white core pixels. This preserves a projectile's colored halo. */
static inline float doom_sprite_glow(const uint8_t *pixels,size_t count,const uint8_t *palette,float out[3]) {
    float weights[12]={0},colors[12][3]={{0}};
    size_t opaque=0;
    for(size_t i=0;i<count;++i) {
        if(!pixels[i*2+1]) continue;
        ++opaque;
        const uint8_t *rgb=palette+pixels[i*2]*3;
        float r=rgb[0]/255.0f,g=rgb[1]/255.0f,b=rgb[2]/255.0f;
        float hi=fmaxf(r,fmaxf(g,b)),lo=fminf(r,fminf(g,b)),chroma=hi-lo;
        if(hi<0.5f||chroma<hi*0.2f) continue;
        float hue=hi==r?(g-b)/chroma:hi==g?2+(b-r)/chroma:4+(r-g)/chroma;
        if(hue<0) hue+=6;
        int bin=(int)(hue*2)%12;
        float weight=hi*hi*hi*hi*chroma*chroma;
        weights[bin]+=weight;
        colors[bin][0]+=r*weight;colors[bin][1]+=g*weight;colors[bin][2]+=b*weight;
    }
    int best=0;float score=0;
    for(int i=0;i<12;++i) {
        float weight=weights[i]+0.5f*(weights[(i+11)%12]+weights[(i+1)%12]);
        if(weight>score) {score=weight;best=i;}
    }
    if(score<=0) {out[0]=out[1]=out[2]=1;return 0;}
    for(int c=0;c<3;++c)
        out[c]=colors[best][c]+0.5f*(colors[(best+11)%12][c]+colors[(best+1)%12][c]);
    float peak=fmaxf(out[0],fmaxf(out[1],out[2]));
    for(int c=0;c<3;++c) out[c]=fmaxf(0.06f,out[c]/peak);
    return opaque?score/(float)opaque:0;
}
/* Where a sprite's glow sits: the height of its pixels in the glow color
 * (out of doom_sprite_glow), weighted like there, as a fraction of the
 * height from the bottom; 0.85 when nothing glows. */
static inline float doom_sprite_glow_height(const uint8_t *pixels,int w,int h,const uint8_t *palette,const float glow[3]) {
    float gl=sqrtf(glow[0]*glow[0]+glow[1]*glow[1]+glow[2]*glow[2]),sum=0,weighted=0;
    for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
        size_t i=(size_t)y*w+x;
        if(!pixels[i*2+1]) continue;
        const uint8_t *rgb=palette+pixels[i*2]*3;
        float r=rgb[0]/255.0f,g=rgb[1]/255.0f,b=rgb[2]/255.0f;
        float hi=fmaxf(r,fmaxf(g,b)),lo=fminf(r,fminf(g,b)),chroma=hi-lo,l=sqrtf(r*r+g*g+b*b);
        if(hi<0.5f||chroma<hi*0.2f||gl<=0||(r*glow[0]+g*glow[1]+b*glow[2])/(l*gl)<0.95f) continue;
        float weight=hi*hi*hi*hi*chroma*chroma;
        sum+=weight;weighted+=weight*(h-0.5f-y);
    }
    return sum>0&&h>0?weighted/sum/h:0.85f;
}
/* Conservative sphere/AABB rejection: retain tangent bounds and never drop
 * illumination inside the light radius, including very large triangles. */
static inline int doom_flash_reaches_bounds(const doom_flash_t *light,const float low[3],const float high[3]) {
    float distance2=0;
    for(int axis=0;axis<3;++axis) {
        float p=light->position[axis];
        /* Map bounds and light positions are finite. Direct comparisons avoid
           six out-of-line fminf/fmaxf calls per triangle/light on MSVC. */
        float d=p-(p<low[axis]?low[axis]:p>high[axis]?high[axis]:p);
        distance2+=d*d;
    }
    return distance2<=light->position[3]*light->position[3];
}
static inline float doom_flash_cross(float ax,float ay,float bx,float by) {return ax*by-ay*bx;}
static inline float doom_flash_fade(int elapsed,float fraction) {
    float age=fmaxf(0.0f,(elapsed-2+fraction)/5.0f);
    return fmaxf(0.0f,1.0f-age);
}
/* The light's falloff and cone toward (dx, dy, dz) from it, before walls. */
static inline float doom_flash_reach(float dx,float dy,float dz,const doom_flash_t *f) {
    float falloff=fmaxf(0.0f,1.0f-sqrtf(dx*dx+dy*dy+dz*dz)/f->position[3]);
    if(falloff<=0) return 0;
    if(f->direction[3]>0) {
        float distance=sqrtf(dx*dx+dy*dy+dz*dz);
        float cosine=distance>0.001f?(dx*f->direction[0]+dy*f->direction[1]+dz*f->direction[2])/distance:1;
        float cone=fminf(1,fmaxf(0,(cosine-f->direction[3])/(0.985f-f->direction[3])));
        falloff*=cone*cone*(3-2*cone);
    }
    return falloff;
}
/* Glowing wall panels sit DOOM_PANEL_OFFSET units in front of their wall and
 * shine only ahead of it: the light fades out toward the wall's plane, so a
 * wall cutting it off there (a box's own edge, a niche's corner) leaves no
 * hard shadow line. (nx, ny) is the wall's unit normal, 0 for other lights.
 * The receiver's normal, if given, keeps the wall around the panel lit.
 * Keep panelEmission in platform/shaders/lighting.glsl equivalent. */
#define DOOM_PANEL_OFFSET 4.0f
static inline float doom_panel_emission(float dx,float dy,float dz,float nx,float ny,const float *normal) {
    if(nx==0&&ny==0) return 1;
    float ahead=dx*nx+dy*ny+DOOM_PANEL_OFFSET,distance=sqrtf(dx*dx+dy*dy+dz*dz);
    float t=fminf(1,fmaxf(0,ahead/fmaxf(distance,1)*2)),emission=t*t*(3-2*t);
    if(normal) emission=fmaxf(emission,fmaxf(0,normal[0]*nx+normal[1]*ny)*fmaxf(0,1-fabsf(ahead)/8));
    return emission;
}
/* A light's panel normal lives in direction[1..2] when it has no cone. */
static inline float doom_flash_emission(float dx,float dy,float dz,const doom_flash_t *f) {
    return f->direction[3]>0?1:doom_panel_emission(dx,dy,dz,f->direction[1],f->direction[2],NULL);
}
/* One blocker against the ray toward (dx, dy, dz): 0 when it stops the ray.
 * A glowing panel's span (flashAt in lighting.glsl): the heights s on it
 * whose rays pass every opening narrow to [low,high]. */
static inline int doom_flash_passes(const doom_flash_t *f,const doom_light_blocker_t *b,float dx,float dy,float dz,float *low,float *high) {
    float ex=b->line[2]-b->line[0],ey=b->line[3]-b->line[1];
    float ox=b->line[0]-f->position[0],oy=b->line[1]-f->position[1];
    float det=doom_flash_cross(dx,dy,ex,ey);
    if(fabsf(det)<0.0001f) return 1;
    float t=doom_flash_cross(ox,oy,ex,ey)/det,u=doom_flash_cross(ox,oy,dx,dy)/det;
    if(t>0.001f&&t<0.999f&&u>=0&&u<=1) {
        float hit=f->position[2]+dz*t,lever=1-t;
        float bottom=b->opening[0]+0.02f,top=b->opening[1]-0.02f;
        if(bottom>=top) return 0;
        float below=(bottom-hit)/lever,above=(top-hit)/lever;
        if(below>=*high||above<=*low) return 0;
        *low=fmaxf(*low,below);*high=fminf(*high,above);
    }
    return 1;
}
static inline float doom_flash_extent(const doom_flash_t *f) {return f->direction[3]>0?0:f->direction[0];}
static inline float doom_flash_amount(const doom_flash_t *f,float falloff,float extent,float low,float high) {
    float visible=extent>0?(high-low)/(2*extent):1;
    return f->strength*falloff*falloff*(3.0f-2.0f*falloff)*visible;
}
static inline float doom_flash_at(float x,float y,float z,const doom_flash_t *f,
                                const doom_light_blocker_t *blockers) {
    float dx=x-f->position[0],dy=y-f->position[1],dz=z-f->position[2];
    float falloff=doom_flash_reach(dx,dy,dz,f),emission=doom_flash_emission(dx,dy,dz,f);
    if(falloff<=0||emission<=0) return 0;
    float extent=doom_flash_extent(f),low=-extent,high=extent;
    for(uint32_t i=f->first;i<f->first+f->count;++i)
        if(!doom_flash_passes(f,&blockers[i],dx,dy,dz,&low,&high)) return 0;
    return doom_flash_amount(f,falloff,extent,low,high)*emission;
}
/* Exact blocker culling; dropping a line never changes doom_flash_at. Lines
 * beyond the radius cannot be crossed, nor can openings spanning every reachable
 * height. A light never sits in the void, so a ray meeting a one-sided wall
 * from behind already left the map through a wall that faces the light. */
static inline int doom_flash_blocker_relevant(const doom_flash_t *f,const doom_light_blocker_t *b,int one_sided) {
    float x=f->position[0],y=f->position[1],z=f->position[2],r=f->position[3];
    float ex=b->line[2]-b->line[0],ey=b->line[3]-b->line[1];
    float px=x-b->line[0],py=y-b->line[1],length2=ex*ex+ey*ey;
    float t=length2>0?fminf(1.0f,fmaxf(0.0f,(px*ex+py*ey)/length2)):0;
    float dx=px-ex*t,dy=py-ey*t;
    if(dx*dx+dy*dy>r*r) return 0;
    if(one_sided) return doom_flash_cross(ex,ey,px,py)<=0; /* Light on the front (right) side. */
    return !(b->opening[0]<=z-r-0.02f&&b->opening[1]>=z+r+0.02f);
}
/* Exact occlusion culling of one light's relevant blockers, in place; returns
 * the new count. Around the light, angular bins hold how far walls that stop
 * every ray (one-sided, closed, or open only beyond the light's reach) cover
 * the whole bin. A line nearer to none of its bins than that is hidden: any
 * ray through it already crossed such a wall, so dropping it never changes
 * doom_flash_at. A spotlight also drops lines outside its cone's footprint.
 * Dense maps keep hundreds of lines in a large radius; most are hidden. */
#define DOOM_FLASH_BINS 1024
static inline float doom_flash_segment_distance(float x,float y,const doom_light_blocker_t *b) {
    float ex=b->line[2]-b->line[0],ey=b->line[3]-b->line[1],px=x-b->line[0],py=y-b->line[1],length2=ex*ex+ey*ey;
    float t=length2>0?fminf(1.0f,fmaxf(0.0f,(px*ex+py*ey)/length2)):0;
    return sqrtf((px-ex*t)*(px-ex*t)+(py-ey*t)*(py-ey*t));
}
/* Per angular bin (bin k starts at azimuth -pi+k*2pi/DOOM_FLASH_BINS), how
 * far every ray in it reaches at most before a wall that stops all rays. */
static inline void doom_flash_depths(const doom_flash_t *f,const doom_light_blocker_t *blockers,uint32_t count,float depth[DOOM_FLASH_BINS]) {
    const float pi=3.14159265f,width=2*pi/DOOM_FLASH_BINS;
    float x=f->position[0],y=f->position[1],z=f->position[2],r=f->position[3];
    for(int k=0;k<DOOM_FLASH_BINS;++k) depth[k]=1e30f;
    if(f->direction[3]>0) {
        /* Azimuths a cone of half-angle a around an axis at elevation e
         * reaches: within asin(sin a/cos e) of the axis, or all around. */
        float h=sqrtf(f->direction[0]*f->direction[0]+f->direction[1]*f->direction[1]);
        float s=sqrtf(fmaxf(0.0f,1-f->direction[3]*f->direction[3]));
        if(h>s+0.001f) {
            float center=atan2f(f->direction[1],f->direction[0]),reach=asinf(s/h)+2.5f*width;
            for(int k=0;k<DOOM_FLASH_BINS;++k) {
                float d=remainderf(-pi+(k+0.5f)*width-center,2*pi);
                if(fabsf(d)>reach) depth[k]=-1e30f;
            }
        }
    }
    for(uint32_t i=0;i<count;++i) {
        const doom_light_blocker_t *b=&blockers[i];
        int closed=b->opening[0]+0.02f>=b->opening[1]-0.02f;
        if(!(closed||b->opening[0]+0.01f>=z+r||b->opening[1]-0.01f<=z-r)) continue;
        if(doom_flash_segment_distance(x,y,b)<1+0.002f*r) continue; /* t>0.001 */
        float ax=b->line[0]-x,ay=b->line[1]-y,ex=b->line[2]-b->line[0],ey=b->line[3]-b->line[1];
        float a0=atan2f(ay,ax),d=remainderf(atan2f(b->line[3]-y,b->line[2]-x)-a0,2*pi);
        float start=fmodf((d>=0?a0:a0+d)+3*pi,2*pi)+1e-4f,end=start+fabsf(d)-2e-4f;
        float numerator=doom_flash_cross(ax,ay,ex,ey);
        for(int k=(int)ceilf(start/width);(k+1)*width<=end;++k) {
            float far=0;
            for(int edge=0;edge<2;++edge) {
                float angle=-pi+(k+edge)*width,along=doom_flash_cross(cosf(angle),sinf(angle),ex,ey);
                float distance=fabsf(along)>1e-6f?numerator/along:1e30f;
                far=fmaxf(far,distance>0?distance:1e30f);
            }
            int bin=k%DOOM_FLASH_BINS;
            depth[bin]=fminf(depth[bin],far);
        }
    }
}
/* A blocker's bins, one either side as margin: [*first,*last], possibly past
 * DOOM_FLASH_BINS (wrap with doom_flash_bin). False for lines at the light,
 * which span too wide an angle to bin and stay in every list. */
static inline int doom_flash_bins_of(const doom_flash_t *f,const doom_light_blocker_t *b,float nearest,int *first,int *last) {
    const float pi=3.14159265f,width=2*pi/DOOM_FLASH_BINS;
    float x=f->position[0],y=f->position[1];
    if(nearest<1) return 0;
    float a0=atan2f(b->line[1]-y,b->line[0]-x),d=remainderf(atan2f(b->line[3]-y,b->line[2]-x)-a0,2*pi);
    float start=fmodf((d>=0?a0:a0+d)+3*pi,2*pi);
    *first=(int)floorf(start/width)-1;*last=(int)floorf((start+fabsf(d))/width)+1;
    return 1;
}
static inline int doom_flash_bin(int k) {return (k%DOOM_FLASH_BINS+DOOM_FLASH_BINS)%DOOM_FLASH_BINS;}
static inline uint32_t doom_flash_cull_hidden(const doom_flash_t *f,doom_light_blocker_t *blockers,uint32_t count) {
    float x=f->position[0],y=f->position[1];
    float depth[DOOM_FLASH_BINS];
    doom_flash_depths(f,blockers,count,depth);
    uint32_t kept=0;
    for(uint32_t i=0;i<count;++i) {
        const doom_light_blocker_t *b=&blockers[i];
        float nearest=doom_flash_segment_distance(x,y,b);
        int first,last,hidden=doom_flash_bins_of(f,b,nearest,&first,&last);
        for(int k=first;hidden&&k<=last;++k) hidden=depth[doom_flash_bin(k)]<nearest-0.25f;
        if(!hidden) blockers[kept++]=*b;
    }
    return kept;
}
/* Directional lists for the GPU (flashAt in lighting.glsl): every pixel
 * would otherwise test all of a light's blockers. Each of DOOM_FLASH_SLICES
 * azimuth slices lists the blockers a ray in it can meet before a wall that
 * stops every ray there; a ray meets no line outside its azimuth, so
 * doom_flash_at_sliced equals doom_flash_at. Slices reach one bin past
 * their edges, so a pixel's azimuth may be off by that much. table[s] packs
 * the slice's first index << 16 | count; indices are into the light's own
 * blockers. Returns how many indices were written, or UINT32_MAX when they
 * would not fit in capacity (at most 65535). */
#define DOOM_FLASH_SLICES 256
static inline uint32_t doom_flash_slice(float dx,float dy) {
    const float pi=3.14159265f;
    int s=(int)((atan2f(dy,dx)+pi)*(DOOM_FLASH_SLICES/(2*pi)));
    return (uint32_t)(s<0?0:s>=DOOM_FLASH_SLICES?DOOM_FLASH_SLICES-1:s);
}
static inline uint32_t doom_flash_slice_blockers(const doom_flash_t *f,const doom_light_blocker_t *blockers,uint32_t count,
                                                 uint32_t table[DOOM_FLASH_SLICES],uint16_t *indices,uint32_t capacity) {
    const int per=DOOM_FLASH_BINS/DOOM_FLASH_SLICES;
    float depth[DOOM_FLASH_BINS];
    uint32_t sizes[DOOM_FLASH_SLICES]={0},stamp[DOOM_FLASH_SLICES]={0},total=0;
    if(count>65535||capacity>65535) return UINT32_MAX;
    doom_flash_depths(f,blockers,count,depth);
    /* Pass 0 counts each slice's blockers, pass 1 lists them. */
    for(int pass=0;pass<2;++pass) {
        if(pass) {
            for(int s=0;s<DOOM_FLASH_SLICES;++s) {table[s]=total<<16;total+=sizes[s];stamp[s]=0;}
            if(total>capacity) return UINT32_MAX;
        }
        for(uint32_t i=0;i<count;++i) {
            const doom_light_blocker_t *b=&blockers[i];
            float nearest=doom_flash_segment_distance(f->position[0],f->position[1],b);
            int first,last,binned=doom_flash_bins_of(f,b,nearest,&first,&last);
            if(!binned) {first=0;last=DOOM_FLASH_BINS-1;}
            for(int k=first;k<=last;++k) {
                if(binned&&depth[doom_flash_bin(k)]<nearest-0.25f) continue;
                /* Visible in bin k: listed where k or a neighbor lies. */
                for(int n=-1;n<=1;++n) {
                    int s=doom_flash_bin(k+n)/per;
                    if(stamp[s]==i+1) continue;
                    stamp[s]=i+1;
                    if(!pass) {++sizes[s];continue;}
                    indices[(table[s]>>16)+(table[s]&0xffff)]=(uint16_t)i;
                    ++table[s];
                }
            }
        }
    }
    return total;
}
static inline float doom_flash_at_sliced(float x,float y,float z,const doom_flash_t *f,const doom_light_blocker_t *blockers,
                                         const uint32_t table[DOOM_FLASH_SLICES],const uint16_t *indices) {
    float dx=x-f->position[0],dy=y-f->position[1],dz=z-f->position[2];
    float falloff=doom_flash_reach(dx,dy,dz,f),emission=doom_flash_emission(dx,dy,dz,f);
    if(falloff<=0||emission<=0) return 0;
    float extent=doom_flash_extent(f),low=-extent,high=extent;
    /* Straight above or below the light no line is crossed (det is 0). */
    if(dx!=0||dy!=0) {
        uint32_t entry=table[doom_flash_slice(dx,dy)];
        for(uint32_t n=entry>>16;n<(entry>>16)+(entry&0xffff);++n)
            if(!doom_flash_passes(f,&blockers[indices[n]],dx,dy,dz,&low,&high)) return 0;
    }
    return doom_flash_amount(f,falloff,extent,low,high)*emission;
}
/* Keep equivalent to flashFacing in platform/shaders/lighting.glsl. The unit normal faces
 * the viewer; color[3] blends from omnidirectional (0) to Lambert (1). */
static inline float doom_flash_facing(float x,float y,float z,const float normal[3],const doom_flash_t *f) {
    float lx=f->position[0]-x,ly=f->position[1]-y,lz=f->position[2]-z;
    float distance=sqrtf(lx*lx+ly*ly+lz*lz);
    float lambert=distance>0.001f?fmaxf(0.0f,(normal[0]*lx+normal[1]*ly+normal[2]*lz)/distance):1.0f;
    return 1.0f+(lambert-1.0f)*f->color[3];
}
#endif
