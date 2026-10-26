#include "map_world.h"
#include "skin_partition.h"
#include <sstream>
#include <iostream>
#include <stdexcept>
using namespace lab;
static void check(bool b,const char* msg){if(!b)throw std::runtime_error(msg);}
static MapRegion room(int id,float xmin,float xmax){
 MapRegion r;r.sector=id;r.floor=0;r.ceiling=128;
 Vec p[]={{xmin,-128,0},{xmax,-128,0},{xmax,128,0},{xmin,128,0}};
 for(int i=0;i<4;++i)r.edges.push_back({p[i],p[(i+1)%4],i});
 return r;
}
static MapSource source(){
 MapSource s;s.reset({room(0,-128,56),room(1,72,256)});return s;
}
static void seams(MapWorld& world){
 for(const auto& entry:world.tiles()){
  auto k=entry.first;const auto& a=entry.second.volume;
  for(int axis=0;axis<3;++axis){
   auto next=k;++next[axis];auto it=world.tiles().find(next);if(it==world.tiles().end())continue;
   const auto& b=it->second.volume;
   for(int i=0;i<17;++i)for(int j=0;j<17;++j){
    int axyz[3]={i,j,16},bxyz[3]={i,j,0};
    if(axis!=2){std::swap(axyz[axis],axyz[2]);std::swap(bxyz[axis],bxyz[2]);}
    check(a.density[a.index(axyz[0],axyz[1],axyz[2])]==b.density[b.index(bxyz[0],bxyz[1],bxyz[2])],
      "Damage differs across tile boundary");
   }
  }
 }
}
int main(){try{
 {MapWorld round;round.reset(source());round.blast({64,0,64},4);
  float reference=round.sample({64,3,64});
  for(int i=0;i<16;++i){float a=i*6.2831853f/16;
   check(std::abs(round.sample({64,3*std::cos(a),64+3*std::sin(a)})-reference)<.001f,"Small gun crater is not radially symmetric");}
 }

 // Arbitrarily oriented map faces cross the field's tetrahedral boundaries.
 // Every partition must conserve area and have an affine field throughout.
 {
  Volume v;v.create(9,9,9,4,{-16,-16,-16});
  for(size_t i=0;i<v.density.size();++i)v.density[i]=std::sin(float(i)*1.73f)*7;
  Vec a{-7.3f,-5.1f,2.4f},b{9.7f,-3.2f,8.1f},c{1.1f,11.3f,-6.2f};
  float area=0;
  for(const auto& p:partitionSkin(a,b,c,[](Vec x){return x;},[](Vec x,Vec y,float t){return x+(y-x)*t;}))
   for(size_t i=1;i+1<p.size();++i){
    Vec x=p[0],y=p[i],z=p[i+1];area+=length(cross(y-x,z-x));
    Vec inside=x*.2f+y*.3f+z*.5f;
    check(std::abs(v.field(inside)-(v.field(x)*.2f+v.field(y)*.3f+v.field(z)*.5f))<.0002f,
          "Skin clipping interpolated across a field tetrahedron");
   }
  check(std::abs(area-length(cross(b-a,c-a)))<.005f,"Skin partition lost surface coverage");
 }
 // Skin clipping and the crater mesh must share exactly the same zero set,
 // including deep cuts and almost-zero boundary samples.
 {
  Volume v;v.create(4,4,4,4,{0,0,0});
  for(size_t i=0;i<v.density.size();++i)v.density[i]=std::sin(float(i)*1.73f)*32;
  v.density[0]=0;v.density[1]=.003f;v.density[2]=-.004f;
  v.rebuild();size_t checked=0;
  for(const auto& chunk:v.chunks)for(const auto& t:chunk.triangles){
   for(Vec p:{t.a,t.b,t.c,t.a*.2f+t.b*.3f+t.c*.5f})
    check(std::abs(v.field(p))<.0002f,"Crater mesh differs from the skin clipping field");
   ++checked;
  }
  check(checked>0,"Mesh/skin regression had no surface");
 }
 // Shallow damage still clips the map skin: its replacement faces must
 // not be discarded by a larger, independent mesh damage tolerance.
 for(float depth:{.005f,.0001f}){
  Volume v;v.create(2,2,2,4,{4600,-4200,-220});
  for(int z=0;z<2;++z)for(int y=0;y<2;++y)for(int x=0;x<2;++x){
   auto i=v.index(x,y,z);v.base[i]=float(x)*4-2;v.density[i]=v.base[i]-depth;
  }
  v.rebuild();size_t checked=0;
  for(const auto& chunk:v.chunks)for(const auto& t:chunk.triangles){
   check(t.damaged==(depth>excavationEpsilon),"Shallow crater replacement disagrees with skin removal tolerance");
   ++checked;
  }
  check(checked>0,"Shallow damage regression had no surface");
 }
 MapWorld world;world.reset(source());Hit hit;
 check(world.ray({0,0,40},{1,0,0},200,hit)&&hit.point.x<60,"Original wall missing");
 check(!world.fits({64,0,1},12,56),"Original wall admits player");
 check(world.blast({64,0,32},40),"Boundary blast failed");
 {
  size_t count=world.cutCount();unsigned revision=world.revision();
  check(!world.blast({64,0,32},40),"Identical hit reported new excavation");
  check(world.cutCount()==count&&world.revision()==revision,"No-op hit invalidated geometry or grew the save journal");
 }
 seams(world);
 check(world.ray({0,0,40},{1,0,0},300,hit)&&hit.point.x>250,"Blast left an internal seam");
 check(world.fits({64,0,4},12,56),"Blasted passage blocks capsule");
 check(world.ray({128,0,40},{-1,0,0},300,hit)&&hit.point.x< -120,"Reverse passage blocks ray");
 world.blast({64,64,0},20);world.blast({64,64,128},20);seams(world);
 world.blast({64,0,32},44);seams(world);
 std::ostringstream saved(std::ios::binary);check(world.save(saved),"Save failed");
 MapWorld loaded;loaded.reset(source());std::istringstream input(saved.str(),std::ios::binary);
 check(loaded.load(input)&&loaded.cutCount()==world.cutCount(),"Reload lost cuts");seams(loaded);
 for(const auto& entry:world.tiles())
  check(entry.second.volume.density==loaded.tile(entry.first).volume.density,
        "Incremental edits disagree with journal reconstruction");
 check(loaded.fits({64,0,4},12,56),"Reload lost traversable opening");
 std::string bad=saved.str().substr(0,12);std::istringstream truncated(bad,std::ios::binary);
 size_t before=loaded.cutCount();check(!loaded.load(truncated)&&loaded.cutCount()==before,"Bad save mutated world");
 // Request new tiles after restoring: the saved cut journal must also
 // reproduce the shared values in tiles which were not cached in the save.
 loaded.tile({-2,0,0});seams(loaded);
 MapSource doorSource;auto door=room(2,56,72);door.ceiling=0;door.movingCeiling=true;
 doorSource.reset({room(0,-128,56),room(1,72,256),door});
 MapWorld untouched;untouched.reset(doorSource);
 untouched.tile({1,0,0});untouched.heights(2,0,64);
 check(untouched.tile({1,0,0}).volume.field({64,16,32})<0,"Lazy door tile retained closed collision");
 untouched.heights(2,48,64);
 check(untouched.tile({1,0,0}).volume.field({64,16,32})>0,"Lazy lift tile lost raised-floor collision");
 MapWorld moving;moving.reset(doorSource);
 check(moving.blast({64,0,32},24,2,1,0),"Door impact failed");
 check(moving.sample({64,0,32})<0,"Door hole missing");
 moving.heights(2,0,64);seams(moving);
 check(moving.sample({64,0,96})<0,"Door damage did not translate");
 check(moving.sample({64,80,96})>0,"Unhit door material vanished");
 std::ostringstream movingSave(std::ios::binary);moving.save(movingSave);
 door.ceiling=64;doorSource.reset({room(0,-128,56),room(1,72,256),door});
 MapWorld movingLoad;movingLoad.reset(doorSource);
 std::istringstream movingInput(movingSave.str(),std::ios::binary);
 check(movingLoad.load(movingInput)&&movingLoad.sample({64,0,96})<0,"Moving door save lost anchor");
 for(const auto& entry:moving.tiles())
  check(entry.second.volume.density==movingLoad.tile(entry.first).volume.density,
        "Incremental moving-sector cache differs from a fresh save rebuild");
 auto thinDoor=room(2,56,60);thinDoor.ceiling=0;thinDoor.movingCeiling=true;
 MapSource thinSource;thinSource.reset({room(0,-128,56),room(1,60,256),thinDoor});
 MapWorld thin;thin.reset(thinSource);
 thin.blast({66,0,32},24,2,1,0);
 check(thin.sample({58,0,32})<0,"Brush centre beyond thin door rejected intersecting damage");
 auto water=room(3,-128,128);water.breakFloor=false;
 MapSource waterSource;waterSource.reset({water});MapWorld liquid;liquid.reset(waterSource);
 liquid.blast({0,0,-8},24);
 check(liquid.sample({0,0,-4})>0,"Liquid acquired a concrete crater");
 auto pillarRoom=room(4,-128,128);
 Vec hole[]={{62,-2,0},{66,-2,0},{66,2,0},{62,2,0}};
 for(int i=0;i<4;++i)pillarRoom.edges.push_back({hole[i],hole[(i+1)%4],10+i});
 MapSource pillarSource;pillarSource.reset({pillarRoom});
 MapWorld fragments;fragments.reset(pillarSource);
 fragments.blast({64,0,54},8);fragments.blast({64,0,74},8);
 check(fragments.sample({64,0,64})>0,"Fragment fixture missing its detached piece");
 size_t collapseAnchor=fragments.cutCount()-1;
 fragments.blast({110,100,30},5);
 check(!fragments.crumble(12,collapseAnchor,24).empty(),"Delayed collapse lost its original damage region");
 check(fragments.sample({64,0,64})<0,"Detached fragment remains solid");
 seams(fragments);
 std::ostringstream fragmentSave(std::ios::binary);fragments.save(fragmentSave);
 MapWorld restoredFragments;restoredFragments.reset(pillarSource);
 std::istringstream fragmentInput(fragmentSave.str(),std::ios::binary);
 check(restoredFragments.load(fragmentInput),"Crumble journal failed to load");
 for(const auto& entry:fragments.tiles())
  check(entry.second.volume.density==restoredFragments.tile(entry.first).volume.density,
        "Crumble journal changed the field after loading");
 MapWorld shallow;shallow.reset(source());
 // Original wall at x=56: a wide 24-unit impact with 4-unit depth.
 shallow.blast({52,0,40},24,-1,0,0,{56,0,40},{1,0,0},1e9f,4096,1.f/3);
 check(shallow.ray({0,0,40},{1,0,0},100,hit)&&hit.point.x>58&&hit.point.x<63,
       "Shallow rocket profile became a deep bore");
 check(shallow.sample({57,12,40})<0,"Shallow rocket lost its broad impact rim");
 auto nearStamp=shallow.skinStamp({56,-4,36},{56,4,44});
 auto farStamp=shallow.skinStamp({56,52,8},{56,56,12});
 shallow.blast({56,0,40},12);
 check(shallow.skinStamp({56,-4,36},{56,4,44})!=nearStamp,"Changed skin patch reused stale clipping");
 check(shallow.skinStamp({56,52,8},{56,56,12})==farStamp,"Unchanged skin patch invalidated by a distant hit");
 bool mayRemove=false;
 shallow.skinStamp({56,-4,36},{56,4,44},&mayRemove);
 check(mayRemove,"Excavated patch incorrectly bypassed clipping");
 shallow.skinStamp({56,1000,36},{56,1004,44},&mayRemove);
 check(!mayRemove,"Untouched patch needs no clipping");
 auto doorSupport=moving.supportStamp({64,0,32},16);
 auto remoteSupport=moving.supportStamp({2000,2000,32},16);
 moving.heights(2,0,80);
 check(moving.supportStamp({64,0,32},16)!=doorSupport,"Door move did not invalidate local support");
 check(moving.supportStamp({2000,2000,32},16)==remoteSupport,"Door move invalidated unrelated support");
 auto changedSupport=shallow.supportStamp({56,0,40},16);
 shallow.blast({56,0,40},28);
 check(shallow.supportStamp({56,0,40},16)!=changedSupport,"New damage reused stale support");
 std::cout<<"PASS map world: cross-tile blasts, bidirectional rays, capsule passage, floor/ceiling junctions, overlapping edits, save/load and malformed-save atomicity.\n";
 std::cout<<"PASS moving-sector anchors, mid-motion save/load and liquid protection.\n";
 return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
