/* Light through stained glass: every leaded window (Image::leaded) throws
   its panes' colors into the room in front of it, as if the sun stood behind
   it at its baked height (sunElevation) and shone straight in, so the floor
   and walls before it take colored patches in the shapes of its panes.
   Baked with the static lights (gatherStatic in bake_lights.cpp): a point
   traces back toward each window in reach and takes the color of the panes
   it sees there. Daylight comes from a wide sky, not a point, so the farther
   the light travels from the glass the softer it falls: a point averages the
   panes over a disc growing with that distance (glassSpread per unit), sharp
   pane shapes right below the window, a soft wash of its colors farther
   out. Every window does, whatever lies behind it: a stained-glass
   wall glows as if lit from behind, and its light falls into the room. */
#include "internal.h"

namespace doom3d {
// One wall piece in leaded glass: its line, side and traceRay part (0
// middle, 1 upper, 2 lower), the room's sector, its start and direction
// along the side, the normal into the room, its height range and texture
// placement (u at the start, v = anchor - z), as geometry.cpp draws it. The
// image is held, not looked up: the relight bakes on a worker thread while
// the main thread may add images.
struct GlassWindow { int line,side,part,sector;const Image *image; float ax,ay,ux,uy,nx,ny,length,bottom,top,u,anchor; };
static std::vector<GlassWindow> glassWindows;
constexpr float glassReach=512,glassStrength=1.8f,glassSpread=0.2f;

void findGlassWindows() {
    glassWindows.clear();
    if(!settings.emissive)return;
    for(int i=0;i<numlines;++i) {
        const line_t &line=lines[i];
        for(int side=0;side<2;++side) {
            if(line.sidenum[side]<0)continue;
            const side_t &s=sides[line.sidenum[side]];
            const sector_t *front=s.sector,*back=side?line.frontsector:line.backsector;
            const vertex_t *v1=side?line.v2:line.v1,*v2=side?line.v1:line.v2;
            float ax=units(v1->x),ay=units(v1->y),bx=units(v2->x),by=units(v2->y),length=std::hypot(bx-ax,by-ay);
            if(length<1)continue;
            float floor=floorZ(front),ceil=ceilZ(front),row=units(s.rowoffset);
            auto add=[&](int part,int texture,float bottom,float top,float anchor) {
                if(texture<=0||top<=bottom)return;
                const Image &image=wallImage(texturetranslation[texture]);
                if(!image.leaded)return;
                glassWindows.push_back({i,side,part,(int)(front-sectors),&image,ax,ay,(bx-ax)/length,(by-ay)/length,
                                        (by-ay)/length,-(bx-ax)/length,length,bottom,top,units(s.textureoffset),anchor+row});
            };
            if(!back) {
                float anchor=s.midtexture&&(line.flags&ML_DONTPEGBOTTOM)?floor+units(textureheight[s.midtexture]):ceil;
                add(0,s.midtexture,floor,ceil,anchor);
                continue;
            }
            float bf=floorZ(back),bc=ceilZ(back);
            if(bc<ceil&&!(front->ceilingpic==skyflatnum&&back->ceilingpic==skyflatnum))
                add(1,s.toptexture,std::max(bc,floor),ceil,
                    (line.flags&ML_DONTPEGTOP)?ceil:bc+(s.toptexture?units(textureheight[s.toptexture]):0));
            if(bf>floor)add(2,s.bottomtexture,floor,std::min(bf,ceil),(line.flags&ML_DONTPEGBOTTOM)?ceil:bf);
            if(s.midtexture) {
                float low=std::max(floor,bf),high=std::min(ceil,bc),height=units(textureheight[s.midtexture]);
                float anchor=(line.flags&ML_DONTPEGBOTTOM)?low+height:high;
                add(0,s.midtexture,std::max(low,anchor+row-height),std::min(high,anchor+row),anchor);
            }
        }
    }
}

void addGlassLight(const BakeMap &map,int sector,float x,float y,float z,const float normal[3],float out[3]) {
    if(glassWindows.empty())return;
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    float across=std::cos(sunElevation),up=std::sin(sunElevation);
    for(const GlassWindow &w:glassWindows) {
        float depth=(x-w.ax)*w.nx+(y-w.ay)*w.ny,along=(x-w.ax)*w.ux+(y-w.ay)*w.uy;
        if(depth<=1||depth>glassReach||along<=0||along>=w.length)continue;
        // The light falls along (n*across, -up): back toward the window it
        // rises depth*up/across by the time it meets the wall.
        float facing=-(normal[0]*w.nx+normal[1]*w.ny)*across+normal[2]*up;
        float length=depth/across,height=z+length*up;
        if(facing<=0||height<=w.bottom||height>=w.top)continue;
        float toward[3]={-w.nx*across,-w.ny*across,up};BakeHit hit;
        if(traceRay(map,sector,x,y,z,toward,length+2,SkyCeiling::open,&hit))continue;
        if(hit.kind!=BakeHit::wall||hit.line!=w.line||hit.part!=w.part||hit.sector!=w.sector)continue;
        // The panes over a disc on the glass (a golden-angle spiral); off the
        // window the disc sees wall, which lets no light through.
        const Image &image=*w.image;
        float radius=length*glassSpread,seen[3]={};
        int samples=radius<0.75f?1:16;
        for(int k=0;k<samples;++k) {
            float r=samples>1?radius*std::sqrt((k+0.5f)/samples):0,a=k*2.39996323f;
            float u=along+r*std::cos(a),h=height+r*std::sin(a);
            if(u<=0||u>=w.length||h<=w.bottom||h>=w.top)continue;
            int tx=((int)std::floor(w.u+u)%image.width+image.width)%image.width;
            int ty=((int)std::floor(w.anchor-h)%image.height+image.height)%image.height;
            size_t texel=(size_t)ty*image.width+tx;
            if(texel>=image.emission.size()||!image.emission[texel])continue;
            const byte *rgb=palette+image.pixels[2*texel]*3;
            for(int c=0;c<3;++c)seen[c]+=rgb[c]/255.0f*image.emission[texel]/255.0f;
        }
        float fade=1-depth/glassReach,amount=glassStrength*facing*fade/samples;
        for(int c=0;c<3;++c)out[c]+=seen[c]*amount;
    }
}
void glassLightShutdown() {glassWindows.clear();}
} // namespace doom3d
