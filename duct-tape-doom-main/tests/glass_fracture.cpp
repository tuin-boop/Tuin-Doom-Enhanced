#include "../platform/glass_fracture.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

static void check(bool condition,const char *message) {
    if(!condition) {std::fprintf(stderr,"%s\n",message);std::exit(1);}
}
static bool same(const std::vector<doom_glass::Fragment> &a,const std::vector<doom_glass::Fragment> &b) {
    if(a.size()!=b.size())return false;
    for(std::size_t i=0;i<a.size();++i) {
        if(a[i].center.x!=b[i].center.x||a[i].center.y!=b[i].center.y||
           a[i].area!=b[i].area||a[i].points.size()!=b[i].points.size())return false;
        for(std::size_t j=0;j<a[i].points.size();++j)
            if(a[i].points[j].x!=b[i].points[j].x||a[i].points[j].y!=b[i].points[j].y)return false;
    }
    return true;
}
static bool contains(const doom_glass::Fragment &f,double x,double y) {
    x-=f.center.x;y-=f.center.y;
    for(std::size_t i=0;i<f.points.size();++i) {
        auto a=f.points[i],b=f.points[(i+1)%f.points.size()];
        if((b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x)<-1e-5)return false;
    }
    return true;
}
static void verify(float w,float h,float hitX,float hitY,std::uint32_t seed,bool sample=true) {
    const auto pieces=doom_glass::fracture(w,h,hitX,hitY,seed);
    check(!pieces.empty()&&pieces.size()<=96,"Fracture must produce a bounded number of solid pieces");
    double sum=0;
    const double epsilon=std::max(w,h)*2e-6;
    const double minimumArea=std::min(30.0,(double)w*h/96*0.15);
    for(const auto &piece:pieces) {
        check(piece.points.size()>=3&&piece.points.size()<=100,"Each fragment must be a polygon");
        check(std::isfinite(piece.area)&&piece.area>0,"Fragments must have finite positive area");
        check(piece.area>=minimumArea*0.999,"A fracture must not contain minuscule fragments");
        double twiceArea=0,perimeter=0;
        for(std::size_t i=0;i<piece.points.size();++i) {
            const auto a=piece.points[i],b=piece.points[(i+1)%piece.points.size()];
            const auto c=piece.points[(i+2)%piece.points.size()];
            const double x=(double)piece.center.x+a.x,y=(double)piece.center.y+a.y;
            check(std::isfinite(x)&&std::isfinite(y),"Vertices must stay finite");
            check(x>=-epsilon&&x<=w+epsilon&&y>=-epsilon&&y<=h+epsilon,
                  "All fragment vertices must stay on the pane");
            const double cross=(double)(b.x-a.x)*(c.y-b.y)-(double)(b.y-a.y)*(c.x-b.x);
            check(cross>=-(double)w*h*1e-6,"Fragments must stay convex and counterclockwise");
            twiceArea+=(double)a.x*b.y-(double)b.x*a.y;
            perimeter+=std::hypot((double)b.x-a.x,(double)b.y-a.y);
        }
        check(std::abs(twiceArea*0.5-piece.area)<=piece.area*2e-5,"Reported area must match renderable polygon");
        check(contains(piece,piece.center.x,piece.center.y),"Local origin must be inside each fragment");
        if(std::min(w,h)>=40&&std::max(w,h)/std::min(w,h)<=4)
            check(piece.area/(perimeter*perimeter)>0.025,"Normal panes must produce substantial shards instead of slivers");
        sum+=twiceArea*0.5;
    }
    check(std::abs(sum-(double)w*h)<=(double)w*h*2e-5,"Solid fragments must conserve the full pane area");
    if(sample) {
        std::uint32_t state=seed^0xe90deb6du;
        auto random=[&]() {state=state*1664525u+1013904223u;return (state>>8)*(1.0/16777216.0);};
        for(int k=0;k<300;++k) {
            const double x=w*(0.0001+random()*0.9998),y=h*(0.0001+random()*0.9998);
            int coverage=0;
            for(const auto &piece:pieces)coverage+=contains(piece,x,y);
            check(coverage==1,"A point on the pane must be covered once, without holes or overlap");
        }
    }
}
static bool sameRaster(const doom_glass::Raster &a,const doom_glass::Raster &b) {
    if(a.cell!=b.cell||a.bands.size()!=b.bands.size())return false;
    for(std::size_t i=0;i<a.bands.size();++i) {
        const auto x=a.bands[i],y=b.bands[i];
        if(x.left!=y.left||x.right!=y.right||x.bottom!=y.bottom||x.top!=y.top)return false;
    }
    return true;
}
static bool polygonContains(const std::vector<doom_glass::Point> &shape,double x,double y) {
    bool inside=false;
    for(std::size_t i=0,j=shape.size()-1;i<shape.size();j=i++) {
        const auto a=shape[i],b=shape[j];
        const double cross=((double)b.x-a.x)*(y-a.y)-((double)b.y-a.y)*(x-a.x);
        if(cross==0&&x>=std::min(a.x,b.x)&&x<=std::max(a.x,b.x)&&
           y>=std::min(a.y,b.y)&&y<=std::max(a.y,b.y))return true;
        if((a.y>y)!=(b.y>y)&&x<((double)b.x-a.x)*(y-a.y)/((double)b.y-a.y)+a.x)inside=!inside;
    }
    return inside;
}
static void verifyRaster(const std::vector<doom_glass::Point> &shape) {
    const auto mask=doom_glass::raster(shape);
    check(mask.cell>=2&&std::isfinite(mask.cell),"Raster texels must be at least two map units");
    int exponent=0;
    check(std::frexp(mask.cell,&exponent)==0.5,"Raster cell size must stay a power of two");
    check(!mask.bands.empty()&&mask.bands.size()<=32,"A valid shard must retain a bounded visible raster");
    check(sameRaster(mask,doom_glass::raster(shape)),"A shard's raster must remain deterministic");
    double minX=1e100,minY=1e100,maxX=-1e100,maxY=-1e100;
    for(auto p:shape) {minX=std::min(minX,(double)p.x);maxX=std::max(maxX,(double)p.x);
                      minY=std::min(minY,(double)p.y);maxY=std::max(maxY,(double)p.y);}
    int actualCells=0;
    for(std::size_t i=0;i<mask.bands.size();++i) {
        const auto band=mask.bands[i];
        check(band.left<band.right&&band.bottom<band.top,"Raster runs must have positive dimensions");
        check(band.top-band.bottom==mask.cell,"Each raster run must be exactly one texel high");
        check(band.right-band.left<=mask.cell*32,"Raster runs must stay within the texel budget");
        if(i)check(band.bottom>=mask.bands[i-1].top,"Raster rows must be ordered and must not overlap");
        for(float coordinate:{band.left,band.right,band.bottom,band.top})
            check(coordinate/mask.cell==std::round(coordinate/mask.cell),"Every run boundary must align with the local texel grid");
        actualCells+=(int)((band.right-band.left)/mask.cell);
    }
    const int left=(int)std::floor(minX/mask.cell),right=(int)std::ceil(maxX/mask.cell);
    const int bottom=(int)std::floor(minY/mask.cell),top=(int)std::ceil(maxY/mask.cell);
    check(right-left<=32&&top-bottom<=32,"Both raster bounding dimensions must stay within 32 texels");
    int expectedCells=0;
    auto covered=[&](double x,double y) {
        int count=0;
        for(const auto band:mask.bands)
            count+=x>=band.left&&x<band.right&&y>=band.bottom&&y<band.top;
        return count;
    };
    for(int y=bottom;y<top;++y)for(int x=left;x<right;++x)
        expectedCells+=polygonContains(shape,(x+0.5)*mask.cell,(y+0.5)*mask.cell);
    if(expectedCells) {
        check(actualCells==expectedCells,"Merged runs must preserve exactly the occupied texel centers");
        for(int y=bottom;y<top;++y)for(int x=left;x<right;++x) {
            const double px=(x+0.5)*mask.cell,py=(y+0.5)*mask.cell;
            check(covered(px,py)==(int)polygonContains(shape,px,py),"The raster silhouette must match center samples of the polygon");
        }
    } else check(actualCells==1&&covered(0,0)==1,"An undersized shard must retain its center texel");
}
int main() {
    for(std::uint32_t seed=0;seed<40;++seed) {
        verify(128,96,64,48,seed);
        verify(64,128,0,0,seed);
        verify(128,64,128,64,seed);
        verify(80,48,-400,900,seed);
    }
    verify(1,1024,0,512,52,false);
    verify(1024,2,512,0,53,false);
    verify(0.01f,16,0,8,54,false);
    verify(4,4,2,2,55);
    verify(2048,2048,1024,1024,56);
    verify(1000000,32,500000,16,57,false);
    verify(96,96,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),58);
    auto first=doom_glass::fracture(128,96,64,48,13);
    check(same(first,doom_glass::fracture(128,96,64,48,13)),"The same seed must reproduce exactly the same fracture");
    check(!same(first,doom_glass::fracture(128,96,64,48,14)),"Different seeds must change the shard shapes");
    check(doom_glass::fracture(0,10,0,0,0).empty(),"Zero-sized panes must be rejected");
    check(doom_glass::fracture(-10,10,0,0,0).empty(),"Negative-sized panes must be rejected");
    check(doom_glass::fracture(std::numeric_limits<float>::infinity(),10,0,0,0).empty(),"Infinite panes must be rejected");
    double nearArea=0,farArea=0;int nearCount=0,farCount=0;
    for(std::uint32_t seed=0;seed<12;++seed)for(const auto &piece:doom_glass::fracture(192,192,96,96,seed)) {
        const double distance=std::hypot(piece.center.x-96,piece.center.y-96);
        if(distance<36) {nearArea+=piece.area;++nearCount;}
        if(distance>70) {farArea+=piece.area;++farCount;}
    }
    check(nearCount&&farCount&&nearArea/nearCount<farArea/farCount*0.8,
          "The impact must fracture more finely than the rest of the pane");
    check(first.size()<40,"A normal pane must remain large shards instead of hundreds of particles");
    const std::vector<doom_glass::Point> rectangle={{-4,-2},{4,-2},{4,2},{-4,2}};
    const std::vector<doom_glass::Point> diamond={{-4,0},{0,-4},{4,0},{0,4}};
    verifyRaster(rectangle);verifyRaster(diamond);
    auto rectangleMask=doom_glass::raster(rectangle),diamondMask=doom_glass::raster(diamond);
    check(rectangleMask.cell==2&&rectangleMask.bands.size()==2&&rectangleMask.bands[0].left==-4&&
          rectangleMask.bands[0].right==4,"Normal shards must keep the two-unit texel scale and merge complete rows");
    check(diamondMask.bands.size()==4&&diamondMask.bands[0].left==-2&&diamondMask.bands[0].right==2&&
          diamondMask.bands[1].left==-4&&diamondMask.bands[1].right==4,"Diagonal outlines must become visible stair steps");
    auto clockwise=diamond;std::reverse(clockwise.begin(),clockwise.end());verifyRaster(clockwise);
    check(sameRaster(diamondMask,doom_glass::raster(clockwise)),"Raster coverage must not depend on polygon winding");
    verifyRaster({{-0.1f,-0.1f},{0.1f,-0.1f},{0,0.1f}});
    verifyRaster({{-0.01f,-32},{0.01f,-32},{0.01f,32},{-0.01f,32}});
    verifyRaster({{-1024,-1024},{1024,-1024},{1024,1024},{-1024,1024}});
    verifyRaster({{-1000000,-2},{1000000,-2},{1000000,2},{-1000000,2}});
    verifyRaster({{-31.5f,-1},{32.1f,-1},{32.1f,1},{-31.5f,1}});
    for(std::uint32_t seed=0;seed<20;++seed)for(const auto &piece:doom_glass::fracture(128,96,64,48,seed)) {
        verifyRaster(piece.points);
        check(doom_glass::raster(piece.points).cell==2,"Ordinary fractured shards must retain chunky two-unit pixels");
    }
    check(doom_glass::raster({}).bands.empty(),"Missing polygons must not create debris");
    check(doom_glass::raster({{0,0},{1,1},{2,2}}).bands.empty(),"Degenerate polygons must not create debris");
    check(doom_glass::raster({{0,0},{1,0},{0,std::numeric_limits<float>::infinity()}}).bands.empty(),
          "Invalid polygons must not create debris");
    std::puts("Glass fracture geometry and bounded pixel raster coverage, grid scale and determinism passed.");
}
