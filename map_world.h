#pragma once
#include "map_source.h"
#include <map>
#include <array>
#include <iosfwd>
namespace lab {
using TileKey=std::array<int,3>;
struct MapCut {
 Vec center;float radius;int sector=-1,plane=0;float anchor=0;
 Vec capOrigin{},capNormal{};float capDepth=1e9f,maxDepth=4096,stretch=1;
};
struct WorldTile {Volume volume;bool edited=false;unsigned revision=0;uint64_t used=0;};
// One field in world coordinates; tile boundaries never own damage.
class MapWorld {
public:
 void reset(MapSource source);
 bool blast(Vec center,float radius,int sector=-1,int plane=0,float anchor=0,
  Vec capOrigin={},Vec capNormal={},float capDepth=1e9f,float maxDepth=4096,float stretch=1);
 bool heights(int sector,float floor,float ceiling);
 std::vector<Vec> crumble(int maximum=4,size_t anchorIndex=size_t(-1),int totalLimit=32);
 float sample(Vec p)const;
 const WorldTile& tile(TileKey key);
 bool ray(Vec start,Vec direction,float range,Hit& hit);
 bool fits(Vec feet,float radius,float height);
 bool edited(Vec p)const;
 float removal(Vec p)const;
 bool affected(Vec feet,float radius,float height)const;
 bool save(std::ostream&)const;
 bool load(std::istream&);
 const std::map<TileKey,WorldTile>& tiles()const{return cache;}
 size_t cutCount()const{return cuts.size();}
 bool staticCut(size_t i)const{return i<cuts.size()&&cuts[i].sector<0;}
 bool cutNear(size_t i,Vec p,float radius)const{return i<cuts.size()&&length(center(cuts[i])-p)<radius;}
 unsigned revision()const{return nextRevision;}
 uint64_t supportStamp(Vec point,float radius)const;
 uint64_t skinStamp(Vec lo,Vec hi,bool* mayRemove=nullptr)const;
 bool solidOwner(Vec p,int& sector,int& plane,float& anchor)const{return source.solidOwner(p,sector,plane,anchor);}
 float originalDepth(Vec p)const{return source.depth(p);}
private:
 MapSource source;
 std::vector<MapCut> cuts;
 std::map<int,std::array<float,2>> planes;
 std::map<TileKey,WorldTile> cache;
 std::map<std::array<int,2>,unsigned> columns;
 unsigned nextRevision=0;
 uint64_t clock=0;
 Vec center(const MapCut&)const;
 float cutValue(Vec p,const MapCut&)const;
 void update(TileKey,WorldTile&,bool incremental=false,size_t firstCut=size_t(-1));
 static TileKey key(Vec);
 static bool intersects(TileKey,Vec,float);
};
}
