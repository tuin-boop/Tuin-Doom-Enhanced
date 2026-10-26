/* Monitor glass found in the original artwork: screens in computer textures
   are dark rectangles framed by lighter housing. Each one becomes curved
   CRT glass that the shader treats as a lens over the picture. */
#ifndef DOOM_SCREEN_GLASS_H
#define DOOM_SCREEN_GLASS_H
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "emissive.h"

/* No glass; heights are stored in 1/16 texel steps below this. */
#define DOOM_GLASS_NONE 255

/* Textures that may hold screens; the pixels decide where. */
static inline int doom_screen_texture(const char *name, int flat)
{
    static const char *const flats[]={"CONS1_*",0};
    static const char *const walls[]={"COMP*","AQCOMP*","SW1COMP","SW2COMP","PLANET*","SPACEW*",
        "SILVER3","TEKWALL*",0};
    return doom_emissive_match(name,flat?flats:walls);
}
/* pixels: palette index and coverage pairs; palette: PLAYPAL RGB. Writes a
   height per pixel (DOOM_GLASS_NONE outside screens) and, when frame is not
   NULL, four bytes per pixel locating it on its screen: x and y from the
   screen's top-left texel, then the screen's width and height (up to 255).
   Returns the number of screens. Candidates are pixels darker than the texture's housing, or
   saturated readouts; a 3x3 closing joins text and graphs into one region.
   A region is a screen when it nearly fills its bounding box, is mostly
   dark, and the ring around the box is mostly housing. Vent grilles fill
   their box only after closing, so the raw candidate share rejects them. */
static inline int doom_screen_glass(int w, int h, const unsigned char *pixels,
                                    const unsigned char *palette, unsigned char *glass,
                                    unsigned char *frame)
{
    enum { DARK=1, CANDIDATE=2, GROWN=4, CLOSED=8, SEEN=16 };
    size_t n=(size_t)w*h,covered=0,i;
    unsigned histogram[256]={0};
    int screens=0;
    memset(glass,DOOM_GLASS_NONE,n);
    unsigned char *flags=(unsigned char*)calloc(n,1);
    int *stack=(int*)malloc(n*sizeof(int));
    for (i=0;i<n;++i) if (pixels[2*i+1]) {
        const unsigned char *c=palette+pixels[2*i]*3;
        int hi=c[0]>c[1]?c[0]:c[1]; if (c[2]>hi) hi=c[2];
        ++histogram[hi];++covered;
    }
    if (!flags || !stack || !covered) {free(flags);free(stack);return 0;}
    int median=0;
    for (size_t seen=0;median<256;++median) if ((seen+=histogram[median])*2>covered) break;
    float threshold=median*0.6f<40?median*0.6f:40;
    for (i=0;i<n;++i) if (pixels[2*i+1]) {
        const unsigned char *c=palette+pixels[2*i]*3;
        int hi=c[0]>c[1]?c[0]:c[1]; if (c[2]>hi) hi=c[2];
        int lo=c[0]<c[1]?c[0]:c[1]; if (c[2]<lo) lo=c[2];
        if (hi<threshold) flags[i]|=DARK|CANDIDATE;
        else if (hi-lo>=40 && hi>=64) flags[i]|=CANDIDATE;
    }
    for (int pass=0;pass<2;++pass) {
        int source=pass?GROWN:CANDIDATE,target=pass?CLOSED:GROWN;
        for (int y=0;y<h;++y) for (int x=0;x<w;++x) {
            int any=0,all=1;
            for (int dy=-1;dy<=1;++dy) for (int dx=-1;dx<=1;++dx) {
                int nx=x+dx,ny=y+dy;
                if (nx<0 || ny<0 || nx>=w || ny>=h) continue;
                if (flags[(size_t)ny*w+nx]&source) any=1; else all=0;
            }
            if (pass?all:any) flags[(size_t)y*w+x]|=target;
        }
    }
    for (i=0;i<n;++i) {
        if (!(flags[i]&CLOSED) || (flags[i]&SEEN)) continue;
        int top=0,x0=w,y0=h,x1=-1,y1=-1;size_t count=0;
        stack[top++]=(int)i;flags[i]|=SEEN;
        while (top) {
            int p=stack[--top],x=p%w,y=p/w;++count;
            if (x<x0) x0=x; if (x>x1) x1=x; if (y<y0) y0=y; if (y>y1) y1=y;
            const int next[4][2]={{x+1,y},{x-1,y},{x,y+1},{x,y-1}};
            for (int k=0;k<4;++k) {
                int nx=next[k][0],ny=next[k][1];
                if (nx<0 || ny<0 || nx>=w || ny>=h) continue;
                size_t q=(size_t)ny*w+nx;
                if ((flags[q]&CLOSED) && !(flags[q]&SEEN)) {flags[q]|=SEEN;stack[top++]=(int)q;}
            }
        }
        int bw=x1-x0+1,bh=y1-y0+1;size_t area=(size_t)bw*bh;
        if (bw<6 || bh<5 || bw>255 || bh>255 || area<48 || area*10>n*6 || count*100<area*65) continue;
        size_t raw=0,dark=0,ring=0,housing=0;
        for (int y=y0;y<=y1;++y) for (int x=x0;x<=x1;++x) {
            unsigned char f=flags[(size_t)y*w+x];
            raw+=(f&CANDIDATE)!=0;dark+=(f&DARK)!=0;
        }
        for (int y=y0-1;y<=y1+1;++y) for (int x=x0-1;x<=x1+1;++x) {
            if ((y>=y0 && y<=y1 && x>=x0 && x<=x1) || x<0 || y<0 || x>=w || y>=h) continue;
            ++ring;housing+=!(flags[(size_t)y*w+x]&CANDIDATE);
        }
        if (raw*100<area*56 || dark*100<area*45 || !ring || housing*10<ring*6) continue;
        /* A dome over the box: flat in the middle, steepest at the edges. */
        float peak=(bw<bh?bw:bh)*0.12f;
        peak=peak<1.5f?1.5f:peak>12?12:peak;
        for (int y=y0;y<=y1;++y) for (int x=x0;x<=x1;++x) {
            float u=(x+0.5f-x0)/bw*2-1,v=(y+0.5f-y0)/bh*2-1;
            long height=lroundf(peak*(1-u*u)*(1-v*v)*16);
            unsigned char *out=&glass[(size_t)y*w+x];
            if (height>254) height=254;
            if (*out!=DOOM_GLASS_NONE && height<=*out) continue;
            *out=(unsigned char)height;
            if (frame) {
                unsigned char *f=&frame[((size_t)y*w+x)*4];
                f[0]=(unsigned char)(x-x0);f[1]=(unsigned char)(y-y0);f[2]=(unsigned char)bw;f[3]=(unsigned char)bh;
            }
        }
        ++screens;
    }
    free(flags);free(stack);
    return screens;
}
/* A readout's frame bytes: width and height carry its cell size in their top
   bit (set: cells two texels wide or tall), so readouts stay under 128. */
#define DOOM_READOUT_PAIR 0x80
/* The hue a readout texel lights in (the panel rule of emissive.h at any
   brightness): 1 green, 2 blue, 3 red, 0 none. */
static inline int doom_readout_hue(const unsigned char *c)
{
    int r=c[0],g=c[1],b=c[2],hi=r>g?r:g; if (b>hi) hi=b;
    if (hi<16) return 0;
    if (g>r*1.4f && g>b*1.2f) return 1;
    if (b>r*1.4f && b>g*1.2f) return 2;
    if (r>g*2 && r>b*2) return 3;
    return 0;
}
/* Readouts: blocks of LED cells outside the screens, one hue over several of
   its palette steps (COMPUTE2's green grids). A readout is a 4-connected
   group of same-hue texels at least 6x2 that fills its box, is a fifth
   glowing, framed by housing and not one color over most of it. frame receives, for each texel, the four
   bytes a screen gets, width and height with DOOM_READOUT_PAIR where most
   texel pairs from the box's corner match, so the shader moves whole cells.
   glass: doom_screen_glass's heights; its screens stay as they are. Returns
   the number of readouts. */
static inline int doom_screen_readouts(int w, int h, const unsigned char *pixels,
                                       const unsigned char *palette, const unsigned char *glass,
                                       unsigned char *frame)
{
    size_t n=(size_t)w*h,i;
    unsigned char *hue=(unsigned char*)calloc(n,1),*seen=(unsigned char*)calloc(n,1);
    int *stack=(int*)malloc(n*sizeof(int)),readouts=0;
    if (!hue || !seen || !stack) {free(hue);free(seen);free(stack);return 0;}
    for (i=0;i<n;++i)
        if (pixels[2*i+1] && glass[i]==DOOM_GLASS_NONE) hue[i]=(unsigned char)doom_readout_hue(palette+pixels[2*i]*3);
    for (i=0;i<n;++i) {
        if (!hue[i] || seen[i]) continue;
        int top=0,x0=w,y0=h,x1=-1,y1=-1;size_t count=0,glowing=0;
        stack[top++]=(int)i;seen[i]=1;
        while (top) {
            int p=stack[--top],x=p%w,y=p/w;++count;
            glowing+=doom_emissive_pixel(DOOM_EMISSIVE_PANEL,palette+pixels[2*p]*3,255)>0;
            if (x<x0) x0=x; if (x>x1) x1=x; if (y<y0) y0=y; if (y>y1) y1=y;
            const int next[4][2]={{x+1,y},{x-1,y},{x,y+1},{x,y-1}};
            for (int k=0;k<4;++k) {
                int nx=next[k][0],ny=next[k][1];
                if (nx<0 || ny<0 || nx>=w || ny>=h) continue;
                size_t q=(size_t)ny*w+nx;
                if (hue[q]==hue[i] && !seen[q]) {seen[q]=1;stack[top++]=(int)q;}
            }
        }
        int bw=x1-x0+1,bh=y1-y0+1;size_t area=(size_t)bw*bh,ring=0,housing=0;
        if (bw<6 || bh<2 || bw>127 || bh>127 || count*10<area*9 || glowing*5<count) continue;
        for (int y=y0-1;y<=y1+1;++y) for (int x=x0-1;x<=x1+1;++x) {
            if ((y>=y0 && y<=y1 && x>=x0 && x<=x1) || x<0 || y<0 || x>=w || y>=h) continue;
            ++ring;housing+=!hue[(size_t)y*w+x];
        }
        if (ring && housing*10<ring*6) continue;
        /* A plain bar (a switch's lamp) would show nothing moving. */
        size_t uses[256]={0},most=0;
        for (int y=y0;y<=y1;++y) for (int x=x0;x<=x1;++x) {
            size_t u=++uses[pixels[2*((size_t)y*w+x)]];
            if (u>most) most=u;
        }
        if (most*5>area*4) continue;
        size_t pairsX=0,sameX=0,pairsY=0,sameY=0;
        for (int y=y0;y<=y1;++y) for (int x=x0;x<=x1;++x) {
            unsigned char v=pixels[2*((size_t)y*w+x)];
            if (!((x-x0)&1) && x<x1) {++pairsX;sameX+=v==pixels[2*((size_t)y*w+x+1)];}
            if (!((y-y0)&1) && y<y1) {++pairsY;sameY+=v==pixels[2*((size_t)(y+1)*w+x)];}
        }
        int pairX=pairsX && sameX*4>=pairsX*3,pairY=pairsY && sameY*4>=pairsY*3;
        for (int y=y0;y<=y1;++y) for (int x=x0;x<=x1;++x) {
            unsigned char *f=&frame[((size_t)y*w+x)*4];
            f[0]=(unsigned char)(x-x0);f[1]=(unsigned char)(y-y0);
            f[2]=(unsigned char)(bw|(pairX?DOOM_READOUT_PAIR:0));f[3]=(unsigned char)(bh|(pairY?DOOM_READOUT_PAIR:0));
        }
        ++readouts;
    }
    free(hue);free(seen);free(stack);
    return readouts;
}
#endif
