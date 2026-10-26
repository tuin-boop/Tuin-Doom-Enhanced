/* A pane breaks into solid, convex pieces, rather than shrinking particles.
   Voronoi boundaries share edges, so the pieces cover the original rectangle.
   Units are the caller's pane units; the normal shard span is about 16--28.
   This is intentionally independent of the renderer and the game's RNG. */
#ifndef DOOM_GLASS_FRACTURE_H
#define DOOM_GLASS_FRACTURE_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace doom_glass {
struct Point { float x,y; };
struct Fragment {
    // Counterclockwise offsets from center, suitable for a triangle fan.
    // center itself is in pane coordinates: [0,width] x [0,height].
    std::vector<Point> points;
    Point center;
    float area;
};
struct Band { float left,right,bottom,top; };
struct Raster {
    float cell;
    // One horizontal run of occupied square texels per nonempty row.
    std::vector<Band> bands;
};

// Render a shard with a stable, coarse silhouette in its own plane. The
// original polygon defines its mass; bands give the visible bounds for contacts.
// Texel edges lie at integer multiples of cell relative to the shard center.
inline Raster raster(const std::vector<Point> &shape) {
    Raster result{2,{}};
    if(shape.size()<3)return result;
    double left=std::numeric_limits<double>::max(),right=-left,bottom=left,top=-left;
    double twiceArea=0;
    const Point origin=shape.front();
    for(std::size_t i=0;i<shape.size();++i) {
        const Point a=shape[i],b=shape[(i+1)%shape.size()];
        if(!std::isfinite(a.x)||!std::isfinite(a.y))return result;
        left=std::min(left,(double)a.x);right=std::max(right,(double)a.x);
        bottom=std::min(bottom,(double)a.y);top=std::max(top,(double)a.y);
        twiceArea+=((double)a.x-origin.x)*((double)b.y-origin.y)-
                   ((double)b.x-origin.x)*((double)a.y-origin.y);
    }
    if(twiceArea==0)return result;
    double cell=2,x0,y0,x1,y1;
    for(;;) {
        x0=std::floor(left/cell);x1=std::ceil(right/cell);
        y0=std::floor(bottom/cell);y1=std::ceil(top/cell);
        if(x1-x0<=32&&y1-y0<=32)break;
        cell*=2;
    }
    result.cell=(float)cell;
    const int columns=(int)(x1-x0),rows=(int)(y1-y0);
    result.bands.reserve(rows);
    for(int row=0;row<rows;++row) {
        const double y=(y0+row+0.5)*cell;
        int first=-1,last=-1;
        for(int column=0;column<columns;++column) {
            const double x=(x0+column+0.5)*cell;
            bool inside=true;
            for(std::size_t i=0;i<shape.size();++i) {
                const Point a=shape[i],b=shape[(i+1)%shape.size()];
                const double ex=(double)b.x-a.x,ey=(double)b.y-a.y;
                const double cross=ex*(y-a.y)-ey*(x-a.x);
                const double epsilon=std::hypot(ex,ey)*cell*1e-12;
                if((twiceArea>0&&cross<-epsilon)||(twiceArea<0&&cross>epsilon)) {inside=false;break;}
            }
            if(inside) {if(first<0)first=column;last=column;}
        }
        if(first>=0)result.bands.push_back({(float)((x0+first)*cell),(float)((x0+last+1)*cell),
                                          (float)((y0+row)*cell),(float)((y0+row+1)*cell)});
    }
    // A valid small fragment may fall between all sample centers. Retain one
    // whole texel at its center instead of silently losing visible debris.
    if(result.bands.empty())result.bands.push_back({0,result.cell,0,result.cell});
    return result;
}

namespace detail {
struct Vec { double x,y; };
struct Cell { std::vector<Vec> points; Vec center; double area; };
struct Random {
    std::uint32_t state;
    explicit Random(std::uint32_t seed):state(seed?seed:0x85ebca6bu) {}
    double unit() {
        state^=state<<13;state^=state>>17;state^=state<<5;
        return (state>>8)*(1.0/16777216.0);
    }
};
inline double spacing(Vec p,Vec hit) {
    return 16.0+12.0*std::min(1.0,std::hypot(p.x-hit.x,p.y-hit.y)/54.0);
}
inline Cell cell(const std::vector<Vec> &sites,std::size_t index,double w,double h) {
    Cell result{{{0,0},{w,0},{w,h},{0,h}},{0,0},0};
    const Vec site=sites[index];
    for(std::size_t j=0;j<sites.size()&&!result.points.empty();++j) {
        if(j==index)continue;
        const Vec other=sites[j];
        const Vec normal={other.x-site.x,other.y-site.y};
        const Vec middle={(other.x+site.x)*0.5,(other.y+site.y)*0.5};
        auto side=[&](Vec p) {return (p.x-middle.x)*normal.x+(p.y-middle.y)*normal.y;};
        std::vector<Vec> clipped;
        clipped.reserve(result.points.size()+1);
        Vec previous=result.points.back();double previousSide=side(previous);
        for(Vec current:result.points) {
            const double currentSide=side(current);
            if((previousSide<=0)!=(currentSide<=0)) {
                const double t=previousSide/(previousSide-currentSide);
                clipped.push_back({previous.x+(current.x-previous.x)*t,
                                   previous.y+(current.y-previous.y)*t});
            }
            if(currentSide<=0)clipped.push_back(current);
            previous=current;previousSide=currentSide;
        }
        result.points=std::move(clipped);
    }
    if(result.points.size()<3)return result;
    // Measure relative to one vertex to avoid cancellation far from the origin.
    const Vec origin=result.points.front();
    double twiceArea=0,cx=0,cy=0;
    for(std::size_t i=0;i<result.points.size();++i) {
        Vec a={result.points[i].x-origin.x,result.points[i].y-origin.y};
        Vec b={result.points[(i+1)%result.points.size()].x-origin.x,
               result.points[(i+1)%result.points.size()].y-origin.y};
        const double cross=a.x*b.y-b.x*a.y;
        twiceArea+=cross;cx+=(a.x+b.x)*cross;cy+=(a.y+b.y)*cross;
    }
    if(twiceArea>0) {
        result.area=twiceArea*0.5;
        result.center={origin.x+cx/(3*twiceArea),origin.y+cy/(3*twiceArea)};
    }
    return result;
}
inline std::vector<Cell> cells(const std::vector<Vec> &sites,double w,double h) {
    std::vector<Cell> result;result.reserve(sites.size());
    for(std::size_t i=0;i<sites.size();++i)result.push_back(cell(sites,i,w,h));
    return result;
}
} // namespace detail

inline std::vector<Fragment> fracture(float width,float height,float hitX,float hitY,std::uint32_t seed) {
    const double w=width,h=height,area=w*h;
    if(!std::isfinite(w)||!std::isfinite(h)||w<=0||h<=0||
       area>std::numeric_limits<float>::max())return {};
    const detail::Vec hit={std::isfinite(hitX)?std::clamp((double)hitX,0.0,w):w*0.5,
                           std::isfinite(hitY)?std::clamp((double)hitY,0.0,h):h*0.5};
    // Add small pieces around the strike, leaving the rest of the pane coarse.
    const double impactArea=(std::min(w,hit.x+36)-std::max(0.0,hit.x-36))*
                            (std::min(h,hit.y+36)-std::max(0.0,hit.y-36));
    double wanted=std::ceil(area/(28*28)+impactArea*0.00145);
    if(std::min(w,h)<20)wanted=std::max(wanted,std::ceil(std::max(w,h)/28));
    const int count=(int)std::clamp(wanted,1.0,96.0);
    detail::Random random(seed);
    const double marginX=std::min(5.0,w*0.19),marginY=std::min(5.0,h*0.19);
    std::vector<detail::Vec> sites;sites.reserve(count);
    // Inset the strike site so a shot at the frame does not create a tiny chip.
    sites.push_back({std::clamp(hit.x+(random.unit()-0.5)*3,marginX,w-marginX),
                     std::clamp(hit.y+(random.unit()-0.5)*3,marginY,h-marginY)});
    while((int)sites.size()<count) {
        detail::Vec best={0,0};double bestScore=-1;
        // Best-candidate sampling gives irregular spacing without a grid.
        for(int attempt=0;attempt<72;++attempt) {
            const detail::Vec candidate={marginX+random.unit()*(w-2*marginX),
                                          marginY+random.unit()*(h-2*marginY)};
            const double candidateSpacing=detail::spacing(candidate,hit);
            double score=std::numeric_limits<double>::max();
            for(detail::Vec existing:sites) {
                const double span=(candidateSpacing+detail::spacing(existing,hit))*0.5;
                const double dx=candidate.x-existing.x,dy=candidate.y-existing.y;
                score=std::min(score,(dx*dx+dy*dy)/(span*span));
            }
            if(score>bestScore) {best=candidate;bestScore=score;}
        }
        sites.push_back(best);
    }
    auto cells=detail::cells(sites,w,h);
    // A small relaxation removes awkward edge slivers while retaining randomness.
    for(std::size_t i=0;i<sites.size();++i)if(cells[i].area>0) {
        sites[i].x+=0.35*(cells[i].center.x-sites[i].x);
        sites[i].y+=0.35*(cells[i].center.y-sites[i].y);
    }
    cells=detail::cells(sites,w,h);
    const double minimumArea=std::min(30.0,area/count*0.15);
    // Remove the generating site, never a polygon: its neighbours inherit its
    // entire region, preserving coverage instead of leaving holes in the pane.
    while(sites.size()>1) {
        std::size_t smallest=0;
        for(std::size_t i=1;i<cells.size();++i)
            if(cells[i].area<cells[smallest].area)smallest=i;
        if(cells[smallest].area>=minimumArea)break;
        sites.erase(sites.begin()+smallest);
        cells=detail::cells(sites,w,h);
    }
    std::vector<Fragment> result;result.reserve(cells.size());
    for(const auto &cell:cells)if(cell.area>0) {
        Fragment fragment{{},{(float)cell.center.x,(float)cell.center.y},(float)cell.area};
        fragment.points.reserve(cell.points.size());
        for(detail::Vec p:cell.points)
            fragment.points.push_back({(float)(p.x-cell.center.x),(float)(p.y-cell.center.y)});
        result.push_back(std::move(fragment));
    }
    return result;
}
} // namespace doom_glass
#endif
