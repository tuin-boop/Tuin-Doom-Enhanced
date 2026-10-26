#include "map_world.h"
#include <algorithm>
#include <istream>
#include <ostream>
#include <cstring>
#include <set>

namespace lab {
static constexpr float extent=MapSource::tileCells*MapSource::step;
TileKey MapWorld::key(Vec p){return {int(std::floor(p.x/extent)),int(std::floor(p.y/extent)),int(std::floor(p.z/extent))};}
void MapWorld::reset(MapSource next){
 source=std::move(next);cuts.clear();planes.clear();cache.clear();columns.clear();
 for(const auto& r:source.topology())planes[r.sector]={r.floor,r.ceiling};
}
Vec MapWorld::center(const MapCut& cut)const{
 Vec p=cut.center;auto found=planes.find(cut.sector);
 if(cut.sector>=0&&found!=planes.end())p.z+=found->second[cut.plane]-cut.anchor;
 return p;
}
bool MapWorld::intersects(TileKey k,Vec c,float r){
 return c.x+r>=k[0]*extent&&c.x-r<=(k[0]+1)*extent&&
  c.y+r>=k[1]*extent&&c.y-r<=(k[1]+1)*extent&&
  c.z+r>=k[2]*extent&&c.z-r<=(k[2]+1)*extent;
}
static float cutValue(Vec p,Vec c,float radius,float phase){
 // Noise follows the cut's local coordinates, including when a door moves.
 Vec d=p-c;
 // Broad asymmetric lobes break the repeated bowl silhouette; fine fracture
 // stays inside the existing 1.06 conservative influence bounds. The phase
 // comes from the saved cut center, so movement and reload remain stable.
 float broad=std::sin((d.x*.8f+d.y*.57f+d.z*.43f)/std::max(4.f,radius)*3.f+phase);
 float fine=std::sin(d.x*.31f+d.z*.27f+phase)*std::cos(d.y*.29f-d.z*.17f-phase);
 // Tiny gun craters need a circular footprint; large blasts retain fracture.
 float rough=std::clamp((radius-4.f)/8.f,0.f,1.f);
 float fractured=radius*(1.f+rough*(-.05f+.06f*broad+.04f*fine));
 return length(d)-fractured;
}
float MapWorld::cutValue(Vec p,const MapCut& c)const{
 Vec shifted=center(c),origin=c.capOrigin+(shifted-c.center);
 Vec d=p-shifted;
 d=d-c.capNormal*(dot(d,c.capNormal)*(1-1/c.stretch));
 return std::max(lab::cutValue(d,{},c.radius,c.center.x*.037f+c.center.y*.053f+c.center.z*.071f),dot(p-origin,c.capNormal)-c.capDepth);
}
// Blend only existing excavations, never the original solid boundary.
// A compact two-unit join removes the ridges between overlapping bowls.
static float joinedCut(float previous,float next,float base){
 if(previous>=base-.01f)return std::min(previous,next);
 constexpr float width=2.f;
 float h=std::max(0.f,1.f-std::abs(previous-next)/width);
 return std::min(previous,next)-h*h*width*.25f;
}
float MapWorld::sample(Vec p)const{
 float value=source.sample(p);
 for(const auto& c:cuts)if(length(p-center(c))<c.radius*std::max(1.f,c.stretch)*1.06f+MapSource::step*2&&source.damageAllowed(p,c.sector,c.plane)&&source.depth(p)<=c.maxDepth)
  value=joinedCut(value,cutValue(p,c),source.sample(p));
 return value;
}
bool MapWorld::edited(Vec p)const{
 for(const auto& c:cuts)if(cutValue(p,c)<=0)return true;
 return false;
}
float MapWorld::removal(Vec p)const{
 auto found=cache.find(key(p));
 if(found==cache.end()||!found->second.edited)return -1;
 const auto& v=found->second.volume;
 // Only actual excavation cuts the original skin. Unmodified air and
 // decorative geometry extending into a room are not damaged surfaces.
 float base=v.field(p,true),after=v.field(p);
 // Keep only a numerical lip. A .03 field tolerance expands into visible
 // strips on shallow gradients around unchanged grid nodes.
 return std::min(base-after-excavationEpsilon,-after-excavationEpsilon);
}
uint64_t MapWorld::supportStamp(Vec p,float radius)const{
 radius+=MapSource::step*2;
 auto lo=key(p-Vec{radius,radius,0}),hi=key(p+Vec{radius,radius,0});
 uint64_t stamp=1469598103934665603ULL;
 for(int y=lo[1];y<=hi[1];++y)for(int x=lo[0];x<=hi[0];++x){
  auto it=columns.find({x,y});stamp=(stamp^(it==columns.end()?0:it->second))*1099511628211ULL;
 }
 return stamp;
}
uint64_t MapWorld::skinStamp(Vec lo,Vec hi,bool* mayRemove)const{
 bool changed=false,air=false;
 uint64_t stamp=1469598103934665603ULL;
 int x0=int(std::floor(lo.x/4)),y0=int(std::floor(lo.y/4)),z0=int(std::floor(lo.z/4));
 int x1=int(std::floor(hi.x/4))+1,y1=int(std::floor(hi.y/4))+1,z1=int(std::floor(hi.z/4))+1;
 TileKey previous{INT32_MAX,0,0};const Volume* volume=nullptr;
 for(int z=z0;z<=z1;++z)for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){
  Vec p{float(x)*4,float(y)*4,float(z)*4};auto k=key(p);
  if(k!=previous){auto it=cache.find(k);volume=it!=cache.end()&&it->second.edited?&it->second.volume:nullptr;previous=k;}
  uint32_t a=0,b=0;
  if(volume){auto i=volume->index(x-k[0]*16,y-k[1]*16,z-k[2]*16);std::memcpy(&a,&volume->base[i],4);std::memcpy(&b,&volume->density[i],4);changed|=volume->density[i]<volume->base[i]-excavationEpsilon;air|=volume->density[i]<-excavationEpsilon;}
  stamp=(stamp^a)*1099511628211ULL;stamp=(stamp^b)*1099511628211ULL;
 }
 if(mayRemove)*mayRemove=changed&&air;
 return stamp;
}
bool MapWorld::affected(Vec p,float radius,float height)const{
 for(const auto& c:cuts){
  Vec q=center(c);float r=c.radius*std::max(1.f,c.stretch)*1.06f+MapSource::step*2;
  if(q.x+r>=p.x-radius&&q.x-r<=p.x+radius&&q.y+r>=p.y-radius&&q.y-r<=p.y+radius&&
     q.z+r>=p.z&&q.z-r<=p.z+height)return true;
 }
 return false;
}
void MapWorld::update(TileKey k,WorldTile& tile,bool incremental,size_t firstCut){
 // An unchanged base does not need sampling or meshing again on each hit.
 bool fresh=!incremental||tile.volume.density.empty();
 std::vector<float> oldBase,oldDensity;
 std::vector<Chunk> oldChunks;
 if(fresh&&!tile.volume.density.empty()){
  oldBase=tile.volume.base;oldDensity=tile.volume.density;
  oldChunks=std::move(tile.volume.chunks);
 }
 if(fresh){tile.volume=source.tile(k[0],k[1],k[2],false);tile.edited=false;}
 Volume& next=tile.volume;bool changed=false;
 std::map<std::pair<int,int>,std::vector<signed char>> permissions;
 size_t first=(!fresh&&!cuts.empty())?(firstCut==size_t(-1)?cuts.size()-1:std::min(firstCut,cuts.size())):0;
 for(size_t cutIndex=first;cutIndex<cuts.size();++cutIndex){
  const auto& cut=cuts[cutIndex];
  Vec c=center(cut);if(!intersects(k,c,cut.radius*std::max(1.f,cut.stretch)*1.06f+MapSource::step*2))continue;
  bool checkDepth=source.depth(c)+cut.radius*std::max(1.f,cut.stretch)*1.1f+MapSource::step*2>cut.maxDepth;
  auto& allowed=permissions[{cut.sector,cut.plane}];
  if(allowed.empty())allowed.assign(next.density.size(),-1);
  // Only nodes inside the ellipsoid's narrow band can lower this field.
  // Shallow chips should not evaluate every node of every nearby tile.
  float band=cut.radius*1.06f+MapSource::step*2;
  auto reach=[&](float n){return band*std::sqrt(std::max(0.f,1+(cut.stretch*cut.stretch-1)*n*n));};
  Vec r{reach(cut.capNormal.x),reach(cut.capNormal.y),reach(cut.capNormal.z)};
  auto low=[&](float p,float origin,float radius){return std::max(0,int(std::floor((p-radius-origin)/next.spacing)));};
  auto high=[&](float p,float origin,float radius,int size){return std::min(size-1,int(std::ceil((p+radius-origin)/next.spacing)));};
  int x0=low(c.x,next.origin.x,r.x),x1=high(c.x,next.origin.x,r.x,next.nx);
  int y0=low(c.y,next.origin.y,r.y),y1=high(c.y,next.origin.y,r.y,next.ny);
  int z0=low(c.z,next.origin.z,r.z),z1=high(c.z,next.origin.z,r.z,next.nz);
  for(int z=z0;z<=z1;++z)for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){
   size_t i=next.index(x,y,z);
   Vec p=next.point(x,y,z);
   float value=joinedCut(next.density[i],cutValue(p,cut),next.base[i]);
   if(value>=next.density[i])continue;
   if(allowed[i]<0)allowed[i]=source.damageAllowed(p,cut.sector,cut.plane)?1:0;
   if(!allowed[i])continue;
   if(checkDepth&&source.depth(p)>cut.maxDepth)continue;
   if(value<next.density[i]){next.density[i]=value;changed=true;}
  }
 }
 if(fresh||changed){
  if(fresh&&oldBase==next.base&&oldDensity==next.density){
   next.chunks=std::move(oldChunks);tile.edited=changed;return;
  }
  for(auto& c:next.chunks)c.dirty=true;
  next.rebuild();tile.revision=++nextRevision;columns[{k[0],k[1]}]=nextRevision;
 }
 tile.edited=tile.edited||changed;
}
const WorldTile& MapWorld::tile(TileKey k){
 // Keep room for collision/support columns as well as excavated tiles.
 // The 512-tile cap thrashed on ordinary maps with accumulated damage.
 if(cache.size()>=2048&&!cache.count(k)){
  auto oldest=cache.end();
  for(auto it=cache.begin();it!=cache.end();++it)
   if(!it->second.edited&&(oldest==cache.end()||it->second.used<oldest->second.used))oldest=it;
  if(oldest!=cache.end())cache.erase(oldest);
 }
 auto pair=cache.try_emplace(k);if(pair.second)update(k,pair.first->second);
 pair.first->second.used=++clock;
 return pair.first->second;
}
bool MapWorld::blast(Vec c,float r,int sector,int plane,float anchor,Vec capOrigin,Vec capNormal,float capDepth,float maxDepth,float stretch){
 if(!std::isfinite(c.x)||!std::isfinite(c.y)||!std::isfinite(c.z)||!std::isfinite(r)||
    r<=0||r>128||cuts.size()>=65536||plane<0||plane>1||!std::isfinite(anchor))return false;
 // The brush centre may already be beyond a thin door. Ownership and
 // protected surfaces are checked per sample, not at the brush centre.
 if(!std::isfinite(capDepth)||!std::isfinite(maxDepth)||maxDepth<=0||maxDepth>4096||!std::isfinite(stretch)||stretch<.01f||stretch>5)return false;
 // Reapplying an identical brush must not deepen a smoothed join.
 for(const auto& old:cuts)if(old.radius==r&&old.sector==sector&&old.plane==plane&&old.anchor==anchor&&
     old.capDepth==capDepth&&old.maxDepth==maxDepth&&old.stretch==stretch&&
     length(old.center-c)<.0001f&&length(old.capOrigin-capOrigin)<.0001f&&length(old.capNormal-capNormal)<.0001f)return false;
 cuts.push_back({c,r,sector,plane,anchor,capOrigin,capNormal,capDepth,maxDepth,stretch});
 unsigned beforeRevision=nextRevision;
 // Include a narrow-band collar: shared corner/edge nodes get identical
 // updates in every incident tile, even if the blast centre is on a border.
 float reach=r*std::max(1.f,stretch)*1.06f+MapSource::step*2;
 TileKey lo=key(c-Vec{reach,reach,reach}),hi=key(c+Vec{reach,reach,reach});
 for(int z=lo[2];z<=hi[2];++z)for(int y=lo[1];y<=hi[1];++y)for(int x=lo[0];x<=hi[0];++x){
  TileKey k{x,y,z};update(k,cache[k],true);
 }
 if(nextRevision==beforeRevision){cuts.pop_back();return false;}
 return true;
}
bool MapWorld::heights(int sector,float floor,float ceiling){
 auto found=planes.find(sector);
 if(found!=planes.end()&&found->second[0]==floor&&found->second[1]==ceiling)return false;
 const auto previous=found!=planes.end()?found->second:std::array<float,2>{floor,ceiling};
 if(!source.heights(sector,floor,ceiling))return false;
 planes[sector]={floor,ceiling};
 Vec lo,hi;source.bounds(sector,lo,hi);
 auto firstColumn=key(lo-Vec{8,8,0}),lastColumn=key(hi+Vec{8,8,0});
 unsigned planeRevision=++nextRevision;
 for(int y=firstColumn[1];y<=lastColumn[1];++y)for(int x=firstColumn[0];x<=lastColumn[0];++x)columns[{x,y}]=planeRevision;
 // A changed sector only affects tiles overlapping its XY footprint. Keep
 // tiles elsewhere (and their render caches) untouched while a door moves.
 for(auto it=cache.begin();it!=cache.end();){
  auto current=it++;auto& entry=*current;auto k=entry.first;
  if((k[0]+1)*extent<lo.x-MapSource::step*2||k[0]*extent>hi.x+MapSource::step*2||
     (k[1]+1)*extent<lo.y-MapSource::step*2||k[1]*extent>hi.y+MapSource::step*2)continue;
  bool affected=false;
  for(int plane=0;plane<2;++plane){
   float now=plane?ceiling:floor,old=previous[plane];
   if(now!=old&&k[2]*extent<=std::max(now,old)+8&&(k[2]+1)*extent>=std::min(now,old)-8)affected=true;
  }
  for(const auto& cut:cuts)if(!affected&&cut.sector==sector&&previous[cut.plane]!=planes[sector][cut.plane]){
   Vec now=center(cut),old=now;old.z+=previous[cut.plane]-planes[sector][cut.plane];
   float radius=cut.radius*std::max(1.f,cut.stretch)*1.06f+8;
   if(intersects(k,now,radius)||intersects(k,old,radius))affected=true;
  }
  if(!affected)continue;
  // Undamaged collision tiles have no rendered crater to keep up to date.
  // Invalidate them lazily instead of remeshing the whole lift footprint.
  // A later support/ray query samples the new sector heights on demand.
  if(!entry.second.edited){cache.erase(current);continue;}
  update(k,entry.second);
 }
 for(const auto& cut:cuts)if(cut.sector==sector&&previous[cut.plane]!=planes[sector][cut.plane]){
  Vec c=center(cut);float reach=cut.radius*std::max(1.f,cut.stretch)*1.06f+MapSource::step*2;
  auto first=key(c-Vec{reach,reach,reach}),last=key(c+Vec{reach,reach,reach});
  for(int z=first[2];z<=last[2];++z)for(int y=first[1];y<=last[1];++y)for(int x=first[0];x<=last[0];++x)tile({x,y,z});
 }
 return true;
}
std::vector<Vec> MapWorld::crumble(int maximum,size_t anchorIndex,int totalLimit){
 std::vector<Vec> removed;
 if(cuts.empty()||maximum<1)return removed;
 if(anchorIndex==size_t(-1))anchorIndex=cuts.size()-1;
 if(anchorIndex>=cuts.size())return removed;
 MapCut anchor=cuts[anchorIndex];Vec centre=center(anchor);
 float reach=std::min(48.f,anchor.radius*std::max(1.f,anchor.stretch)*1.06f+MapSource::step*2);
 size_t firstRemoval=cuts.size();
 std::set<TileKey> seen,dirty;
 const int edges[7][3]={{1,0,0},{0,1,0},{0,0,1},{1,1,0},{1,0,1},{0,1,1},{1,1,1}};
 auto point=[](TileKey n){return Vec{n[0]*MapSource::step,n[1]*MapSource::step,n[2]*MapSource::step};};
 auto value=[&](TileKey n){
  Vec p=point(n);auto it=cache.find(key(p));
  // Resolve uncached neighbours from the saved field, not camera-loaded
  // mesh tiles. Looking away must not change whether a piece is supported.
  return it==cache.end()?sample(p):it->second.volume.field(p);
 };
 std::vector<TileKey> seeds;
 for(const auto& entry:cache)if(entry.second.edited&&intersects(entry.first,centre,reach)){
  const auto& v=entry.second.volume;
  int lo[3],hi[3];float c[3]={centre.x,centre.y,centre.z};
  for(int a=0;a<3;++a){lo[a]=std::clamp(int(std::floor((c[a]-reach)/MapSource::step))-entry.first[a]*16,0,16);hi[a]=std::clamp(int(std::ceil((c[a]+reach)/MapSource::step))-entry.first[a]*16,0,16);}
  for(int z=lo[2];z<hi[2];++z)for(int y=lo[1];y<hi[1];++y)for(int x=lo[0];x<hi[0];++x)
   if(v.density[v.index(x,y,z)]>=0){
    // An interior node with more neighbours than the fragment limit cannot
    // belong to a removable component. Avoid expensive map/BFS work for it.
    int neighbours=0;
    if(x>0&&v.density[v.index(x-1,y,z)]>=0)++neighbours;
    if(v.density[v.index(x+1,y,z)]>=0)++neighbours;
    if(y>0&&v.density[v.index(x,y-1,z)]>=0)++neighbours;
    if(v.density[v.index(x,y+1,z)]>=0)++neighbours;
    if(z>0&&v.density[v.index(x,y,z-1)]>=0)++neighbours;
    if(v.density[v.index(x,y,z+1)]>=0)++neighbours;
    if(neighbours==6||neighbours+1>maximum)continue;
    seeds.push_back({entry.first[0]*16+x,entry.first[1]*16+y,entry.first[2]*16+z});
   }
 }
 for(auto seed:seeds){
  if(removed.size()>=size_t(totalLimit))break;
  if(seen.count(seed)||value(seed)<0)continue;
  std::vector<TileKey> component{seed};seen.insert(seed);
  bool attached=false;
  for(size_t i=0;i<component.size()&&!attached;++i){
   for(const auto& edge:edges)for(int sign:{-1,1}){
    TileKey n{component[i][0]+sign*edge[0],component[i][1]+sign*edge[1],component[i][2]+sign*edge[2]};
    if(value(n)<0)continue;
    if(std::find(component.begin(),component.end(),n)!=component.end())continue;
    if(seen.count(n)||component.size()>=size_t(maximum)){attached=true;break;}
    component.push_back(n);seen.insert(n);
   }
  }
  if(attached)continue;
  for(auto n:component)if(!source.damageAllowed(point(n),anchor.sector,anchor.plane))attached=true;
  if(attached||cuts.size()+component.size()>65536||removed.size()+component.size()>size_t(totalLimit))continue;
  for(auto n:component){
   Vec p=point(n);
   cuts.push_back({p,1,anchor.sector,anchor.plane,anchor.sector>=0?planes[anchor.sector][anchor.plane]:0});
   removed.push_back(p);
   auto lo=key(p-Vec{10,10,10}),hi=key(p+Vec{10,10,10});
   for(int z=lo[2];z<=hi[2];++z)for(int y=lo[1];y<=hi[1];++y)for(int x=lo[0];x<=hi[0];++x)dirty.insert({x,y,z});
  }
 }
 for(auto k:dirty)update(k,cache[k],true,firstRemoval);
 return removed;
}
bool MapWorld::ray(Vec start,Vec direction,float range,Hit& hit){
 if(!std::isfinite(range)||range<=0||range>65536)return false;
 Vec d=normalized(direction);if(length(d)<.5f)return false;
 TileKey k=key(start);float next[3],delta[3];int step[3];
 float p[3]={start.x,start.y,start.z},v[3]={d.x,d.y,d.z};
 for(int i=0;i<3;++i){
  step[i]=v[i]>=0?1:-1;delta[i]=std::abs(v[i])>1e-8f?extent/std::abs(v[i]):INFINITY;
  next[i]=std::abs(v[i])>1e-8f?(((k[i]+(step[i]>0?1:0))*extent)-p[i])/v[i]:INFINITY;
 }
 float travelled=0;
 for(int n=0;n<4096&&travelled<=range;++n){
  Hit candidate;
  if(tile(k).volume.ray(start,d,range,candidate)){
   float end=std::min({next[0],next[1],next[2],range});
   if(candidate.distance<=end+.001f){hit=candidate;return true;}
  }
  int axis=next[0]<next[1]?0:1;if(next[2]<next[axis])axis=2;
  travelled=next[axis];next[axis]+=delta[axis];k[axis]+=step[axis];
 }
 return false;
}
bool MapWorld::fits(Vec p,float radius,float height){
 if(radius<=0||height<=0||!std::isfinite(radius)||!std::isfinite(height))return false;
 if(sample(p+Vec{0,0,height*.5f})>0)return false;
 TileKey lo=key(p-Vec{radius,radius,0}),hi=key(p+Vec{radius,radius,height});
 for(int z=lo[2];z<=hi[2];++z)for(int y=lo[1];y<=hi[1];++y)for(int x=lo[0];x<=hi[0];++x)
  if(!tile({x,y,z}).volume.fits(p,radius,height,false))return false;
 return true;
}
bool MapWorld::save(std::ostream& out)const{
 out.write("SMW3",4);uint32_t count=uint32_t(cuts.size());out.write((const char*)&count,4);
 for(const auto& c:cuts){
  out.write((const char*)&c.center,sizeof(Vec));out.write((const char*)&c.radius,4);
  out.write((const char*)&c.sector,4);out.write((const char*)&c.plane,4);out.write((const char*)&c.anchor,4);
  out.write((const char*)&c.capOrigin,sizeof(Vec));out.write((const char*)&c.capNormal,sizeof(Vec));
  out.write((const char*)&c.capDepth,4);out.write((const char*)&c.maxDepth,4);
  out.write((const char*)&c.stretch,4);
 }
 return bool(out);
}
bool MapWorld::load(std::istream& in){
 char magic[4];uint32_t count;
 in.read(magic,4);in.read((char*)&count,4);
 bool legacy=std::memcmp(magic,"SMW1",4)==0;
 bool version3=std::memcmp(magic,"SMW3",4)==0;
 if(!in||(!legacy&&!version3&&std::memcmp(magic,"SMW2",4))||count>65536)return false;
 std::vector<MapCut> next(count);
 for(auto& c:next){
  in.read((char*)&c.center,sizeof(Vec));in.read((char*)&c.radius,4);
  in.read((char*)&c.sector,4);in.read((char*)&c.plane,4);in.read((char*)&c.anchor,4);
  if(!legacy){
   in.read((char*)&c.capOrigin,sizeof(Vec));in.read((char*)&c.capNormal,sizeof(Vec));
   in.read((char*)&c.capDepth,4);in.read((char*)&c.maxDepth,4);
   if(version3)in.read((char*)&c.stretch,4);
  }
  if(!in||!std::isfinite(c.center.x)||!std::isfinite(c.center.y)||!std::isfinite(c.center.z)||
    std::abs(c.center.x)>1000000||std::abs(c.center.y)>1000000||std::abs(c.center.z)>1000000||
    !std::isfinite(c.radius)||c.radius<=0||c.radius>128||c.sector< -1||
    c.plane<0||c.plane>1||!std::isfinite(c.anchor)||!std::isfinite(c.capDepth)||
    !std::isfinite(c.maxDepth)||c.maxDepth<=0||c.maxDepth>4096||
    !std::isfinite(c.capOrigin.x)||!std::isfinite(c.capOrigin.y)||!std::isfinite(c.capOrigin.z)||
    !std::isfinite(c.capNormal.x)||!std::isfinite(c.capNormal.y)||!std::isfinite(c.capNormal.z)||
    !std::isfinite(c.stretch)||c.stretch<.01f||c.stretch>5)return false;
 }
 cuts=std::move(next);cache.clear();
 for(const auto& cut:cuts){
  Vec c=center(cut);float reach=cut.radius*std::max(1.f,cut.stretch)*1.06f+MapSource::step*2;
  auto lo=key(c-Vec{reach,reach,reach}),hi=key(c+Vec{reach,reach,reach});
  for(int z=lo[2];z<=hi[2];++z)for(int y=lo[1];y<=hi[1];++y)for(int x=lo[0];x<=hi[0];++x)tile({x,y,z});
 }
 return true;
}
}
