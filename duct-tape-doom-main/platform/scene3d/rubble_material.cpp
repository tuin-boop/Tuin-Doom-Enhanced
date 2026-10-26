#include "internal.h"
#include <tuple>
namespace doom3d {
namespace {
// A BSP leaf names a sector even behind a one-sided wall, in solid space.
// Its floor/ceiling are not real surfaces there.
bool insideSector(const sector_t &sector,float x,float y) {
    bool inside=false;
    for(int n=0;n<sector.linecount;++n) {
        const line_t &l=*sector.lines[n];if(l.frontsector==l.backsector)continue;
        float ax=units(l.v1->x),ay=units(l.v1->y),bx=units(l.v2->x),by=units(l.v2->y);
        if((ay>y)!=(by>y)&&x<ax+(y-ay)*(bx-ax)/(by-ay))inside=!inside;
    }
    return inside;
}
}
extern "C" void I_TestRubbleSelection() {
    sector_t sector={};vertex_t corners[4]={};line_t edges[5]={};line_t *boundary[5];
    const int xy[4][2]={{0,0},{256,0},{256,256},{0,256}};
    for(int i=0;i<4;++i){corners[i].x=xy[i][0]*FRACUNIT;corners[i].y=xy[i][1]*FRACUNIT;}
    for(int i=0;i<4;++i){edges[i].v1=&corners[i];edges[i].v2=&corners[(i+1)%4];edges[i].frontsector=&sector;boundary[i]=&edges[i];}
    edges[4].v1=&corners[0];edges[4].v2=&corners[2];edges[4].frontsector=edges[4].backsector=&sector;boundary[4]=&edges[4];
    sector.linecount=5;sector.lines=boundary;
    if(!doom3d::insideSector(sector,128,128)||doom3d::insideSector(sector,-20,128)||doom3d::insideSector(sector,276,128))
        I_Error("Rubble sampler treated a BSP sector's floor as extending behind its walls");
    puts("PASS rubble material rejects sector flats behind walls and ignores internal lines");
}
// Cached in map space, independent of lights and camera. Samples the original
// skin, never a previously tinted crater, so successive hits keep their color.
std::array<float,3> rubbleSurfaceColor(float x,float y,float z) {
    if(!settings.materialRubble||!numsectors)return {1,1,1};
    using Key=std::tuple<int,int,int>;
    static unsigned serial=~0u;
    static std::map<Key,std::array<float,3>> cache;
    static std::map<std::pair<int,int>,std::vector<int>> nearby;
    if(serial!=r_levelserial){serial=r_levelserial;cache.clear();nearby.clear();}
    Key key{int(std::floor(x/8)),int(std::floor(y/8)),int(std::floor(z/8))};
    auto found=cache.find(key);if(found!=cache.end())return found->second;
    const sector_t *sector=R_PointInSubsector(fixed_t(x*FRACUNIT),fixed_t(y*FRACUNIT))->sector;
    Image *skin=nullptr;float u=x,v=-y,best=1e30f;
    auto flat=[&](int pic,float height){if(pic!=skyflatnum&&std::abs(z-height)<best){best=std::abs(z-height);skin=&lumpImage(firstflat+flattranslation[pic],true);u=x;v=-y;}};
    if(insideSector(*sector,x,y)) {
        flat(sector->floorpic,units(sector->floorheight));flat(sector->ceilingpic,units(sector->ceilingheight));
    }
    // Search actual nearby faces across BSP boundaries. Cache the candidate
    // lines per XY cell; heights are evaluated live for doors and lifts.
    std::pair<int,int> cell{int(std::floor(x/128)),int(std::floor(y/128))};
    auto candidates=nearby.find(cell);
    if(candidates==nearby.end()) {
        std::vector<int> ids;float cx=(cell.first+.5f)*128,cy=(cell.second+.5f)*128;
        for(int i=0;i<numlines;++i) {
            const line_t &l=lines[i];float ax=units(l.v1->x),ay=units(l.v1->y);
            float dx=units(l.v2->x)-ax,dy=units(l.v2->y)-ay;
            float t=std::clamp(((cx-ax)*dx+(cy-ay)*dy)/std::max(.001f,dx*dx+dy*dy),0.f,1.f);
            if(std::hypot(cx-ax-t*dx,cy-ay-t*dy)<=384)ids.push_back(i);
        }
        candidates=nearby.emplace(cell,std::move(ids)).first;
    }
    for(int index:candidates->second) {
        const line_t &line=lines[index];
        for(int side=0;side<2;++side)for(int part=0;part<3;++part) {
            WallSpan span;if(!wallSpan(line,side,part,span)||span.texture<=0)continue;
            float dx=span.b.x-span.a.x,dy=span.b.y-span.a.y,len=std::hypot(dx,dy);if(len<.001f)continue;
            float along=std::clamp(((x-span.a.x)*dx+(y-span.a.y)*dy)/len,0.f,len);
            float hz=std::clamp(z,span.bottom,span.top);
            float distance=std::hypot(std::hypot(x-span.a.x-dx*along/len,y-span.a.y-dy*along/len),z-hz);
            if(distance>=best)continue;
            best=distance;skin=&wallImage(texturetranslation[span.texture]);
            u=along+units(sides[line.sidenum[side]].textureoffset);v=span.anchor-hz;
        }
    }
    std::array<float,3> color{1,1,1};
    if(skin&&skin->width>0&&skin->height>0&&!skin->pixels.empty()) {
        const byte *pal=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
        color={};int samples=0;
        for(int dy=-4;dy<=4;dy+=2)for(int dx=-4;dx<=4;dx+=2) {
            int sx=((int(std::floor(u))+dx)%skin->width+skin->width)%skin->width;
            int sy=((int(std::floor(v))+dy)%skin->height+skin->height)%skin->height;
            int at=2*(sy*skin->width+sx);if(!skin->pixels[at+1])continue;
            int index=skin->pixels[at]*3;for(int c=0;c<3;++c)color[c]+=pal[index+c];++samples;
        }
        if(samples) {
            // Preserve reflectance, not just hue. Normalizing to the brightest
            // channel made dark green/brown walls turn into pale stone.
            // A little whole-surface color stops a thin trim stripe taking over.
            const auto surface=averageColor(*skin);
            int rock=W_CheckNumForName((char*)"DIGROCK");
            const auto stone=averageColor(rock>=0?lumpImage(rock,true):wallImage(R_TextureNumForName((char*)"GRAY1")));
            for(int c=0;c<3;++c) {
                float reflectance=.65f*color[c]/(samples*255.f)+.35f*surface[c];
                color[c]=std::clamp(reflectance/std::max(.08f,stone[c]),0.f,1.f);
            }
        } else color={1,1,1};
    }
    if(cache.size()>=32768)cache.clear();
    cache.emplace(key,color);return color;
}
}
