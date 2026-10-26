// Preserve the previous concrete assembly while ordinary map batches rebuild.
struct SolidAssembly {
 struct Range {size_t first,count;unsigned revision;};
 std::vector<Vertex> vertices;
 std::map<uint64_t,Range> ranges;
 int material=0;bool active=false;
};
static SolidAssembly solidAssembly;
static void retainSolidAssembly(){
 if(!solidAssembly.active)return;
 auto it=batches.find(solidAssembly.material);
 if(it!=batches.end())it->second.swap(solidAssembly.vertices);
 solidAssembly.active=false;
}
// Replace only edited world tiles. Original map faces outside these boxes
// retain their texture coordinates, lighting and material.
static void buildSolidMapMesh(){
 const auto* world=SV_MapWorld();if(!world||!world->cutCount())return;
 const bool profileMesh=M_CheckParm((char*)"-profileskins")!=0;
 auto meshStart=SDL_GetPerformanceCounter();
 struct Face {int material;Vertex a,b,c;lab::Vec normal;};
 struct Cached {uint64_t id=0;unsigned revision=~0u;std::map<int,std::vector<Vertex>> vertices;};
 struct SkinCache {uint64_t revision=0;std::vector<Vertex> source,output;};
 static uint64_t nextSection=0;
 static int colorMode=-1;
 static unsigned level=~0u;static std::map<lab::TileKey,Cached> cached;
 struct BatchCache {SkinCache whole;std::vector<Vertex> work;std::map<uint64_t,SkinCache> skins,patches;};
 static std::map<int,BatchCache> batchCaches;
 struct DamageBounds {unsigned revision=~0u;lab::Vec lo,hi;};
 static std::map<lab::TileKey,DamageBounds> damageBounds;
 if(level!=r_levelserial||colorMode!=settings.materialRubble){colorMode=settings.materialRubble;level=r_levelserial;cached.clear();batchCaches.clear();damageBounds.clear();solidAssembly.vertices.clear();solidAssembly.ranges.clear();}
 auto pos=[](const Vertex& v){return lab::Vec{v.x,v.y,v.z};};
 auto interpolate=[](Vertex a,const Vertex& b,float t){
  auto lerp=[&](float x,float y){return x+(y-x)*t;};
  a.x=lerp(a.x,b.x);a.y=lerp(a.y,b.y);a.z=lerp(a.z,b.z);
  a.u=lerp(a.u,b.u);a.v=lerp(a.v,b.v);a.light=lerp(a.light,b.light);
  a.red=lerp(a.red,b.red);a.green=lerp(a.green,b.green);a.blue=lerp(a.blue,b.blue);
  a.sunU=lerp(a.sunU,b.sunU);a.sunV=lerp(a.sunV,b.sunV);return a;
 };
 auto overlap=[](const Face& f,lab::Vec lo,lab::Vec hi){
  return std::max({f.a.x,f.b.x,f.c.x})>=lo.x&&std::min({f.a.x,f.b.x,f.c.x})<=hi.x&&
   std::max({f.a.y,f.b.y,f.c.y})>=lo.y&&std::min({f.a.y,f.b.y,f.c.y})<=hi.y&&
   std::max({f.a.z,f.b.z,f.c.z})>=lo.z&&std::min({f.a.z,f.b.z,f.c.z})<=hi.z;
 };
 int rock=W_CheckNumForName((char*)"DIGROCK");
 int concrete=rock>=0?-1-rock:R_TextureNumForName((char*)"GRAY1");
 if(rock>=0)lumpImage(rock,true);else wallImage(concrete);
 // Keep the original faces and cut only the opening, rather than replacing
 // an entire tile with an approximation of its textures and light bakes.
 std::vector<std::pair<lab::Vec,lab::Vec>> boxes;
 std::vector<unsigned> boxRevisions;
 uint64_t revision=1469598103934665603ULL;
 for(const auto& entry:world->tiles())if(entry.second.edited){
  auto& bounds=damageBounds[entry.first];const auto& volume=entry.second.volume;
  if(bounds.revision!=entry.second.revision){
   bounds.revision=entry.second.revision;
   bounds.lo=volume.point(16,16,16);bounds.hi=volume.origin;
   for(int z=0;z<volume.nz;++z)for(int y=0;y<volume.ny;++y)for(int x=0;x<volume.nx;++x){
    auto i=volume.index(x,y,z);if(volume.density[i]>=volume.base[i]-.01f)continue;
    auto p=volume.point(x,y,z);
    bounds.lo={std::min(bounds.lo.x,p.x-4),std::min(bounds.lo.y,p.y-4),std::min(bounds.lo.z,p.z-4)};
    bounds.hi={std::max(bounds.hi.x,p.x+4),std::max(bounds.hi.y,p.y+4),std::max(bounds.hi.z,p.z+4)};
   }
  }
  boxes.push_back({bounds.lo,bounds.hi});
  boxRevisions.push_back(entry.second.revision);
  revision=(revision^entry.second.revision)*1099511628211ULL;
 }
 auto boxesEnd=SDL_GetPerformanceCounter();
 // Give each material exclusive cache ownership before dispatch. Geometry and
 // sampled world fields are read-only until all workers have joined.
 struct SkinJob {std::pair<const int,std::vector<Vertex>>* batch;BatchCache* cache;};
 std::vector<SkinJob> jobs;
 for(auto& batch:batches)jobs.push_back({&batch,&batchCaches[batch.first]});
 auto clipBatch=[&](int index){
  auto& batch=*jobs[index].batch;auto& cache=*jobs[index].cache;
  auto& skins=cache.skins;auto& patches=cache.patches;
  if(skins.size()>30000)skins.clear();
  if(patches.size()>100000)patches.clear();
  auto& whole=cache.whole;
  if(whole.revision==revision&&whole.source.size()==batch.second.size()&&
     (batch.second.empty()||!memcmp(whole.source.data(),batch.second.data(),batch.second.size()*sizeof(Vertex)))){
   batch.second=whole.output;return;
  }
  whole.source=batch.second;whole.revision=revision;
  auto& output=cache.work;output.clear();output.reserve(batch.second.size());
  auto carve=[&](auto&& self,Vertex a,Vertex b,Vertex c,int depth,const std::vector<size_t>& candidates)->void{
   Face face{batch.first,a,b,c,{}};std::vector<size_t> nearby;
   for(size_t i:candidates)if(overlap(face,boxes[i].first,boxes[i].second))nearby.push_back(i);
   if(nearby.empty()){output.insert(output.end(),{a,b,c});return;}
   float ab=lab::length(pos(a)-pos(b)),bc=lab::length(pos(b)-pos(c)),ca=lab::length(pos(c)-pos(a));
   float edge=std::max({ab,bc,ca});
   if(edge>16&&depth<32){
    // Long, thin door strips must not subdivide in both directions until
    // their short edge is thousands of times smaller than a field cell.
    if(ab>=bc&&ab>=ca){Vertex middle=interpolate(a,b,.5f);self(self,a,middle,c,depth+1,nearby);self(self,middle,b,c,depth+1,nearby);}
    else if(bc>=ca){Vertex middle=interpolate(b,c,.5f);self(self,a,b,middle,depth+1,nearby);self(self,a,middle,c,depth+1,nearby);}
    else {Vertex middle=interpolate(c,a,.5f);self(self,a,b,middle,depth+1,nearby);self(self,middle,b,c,depth+1,nearby);}
    return;
   }
   Vertex source[3]={a,b,c};uint64_t patchHash=uint64_t(batch.first);
   const auto* patchBytes=reinterpret_cast<const unsigned char*>(source);
   for(size_t i=0;i<sizeof(source);++i)patchHash=(patchHash^patchBytes[i])*1099511628211ULL;
   bool mayRemove=true;
   uint64_t stamp=world->skinStamp({std::min({a.x,b.x,c.x}),std::min({a.y,b.y,c.y}),std::min({a.z,b.z,c.z})},
                                 {std::max({a.x,b.x,c.x}),std::max({a.y,b.y,c.y}),std::max({a.z,b.z,c.z})},&mayRemove);
   auto& patch=patches[patchHash];
   if(patch.revision==stamp&&patch.source.size()==3&&!memcmp(patch.source.data(),source,sizeof(source))){
    output.insert(output.end(),patch.output.begin(),patch.output.end());return;
   }
   if(!mayRemove){
    patch.revision=stamp;patch.source.assign(source,source+3);patch.output=patch.source;
    output.insert(output.end(),source,source+3);return;
   }
   size_t patchStart=output.size();bool fullyKept=true;
   for(const auto& polygon:lab::partitionSkin(a,b,c,pos,interpolate)){
   std::vector<Vertex> kept;Vertex previous=polygon.back();float pd=world->removal(pos(previous));
   for(Vertex current:polygon){
    float cd=world->removal(pos(current));fullyKept=fullyKept&&cd<=0;
    if((pd<=0)!=(cd<=0))kept.push_back(interpolate(previous,current,pd/(pd-cd)));
    if(cd<=0)kept.push_back(current);previous=current;pd=cd;
   }
   for(size_t n=1;n+1<kept.size();++n){
    output.insert(output.end(),{kept[0],kept[n],kept[n+1]});
   }
   }
   if(fullyKept){output.resize(patchStart);output.insert(output.end(),source,source+3);}
   patch.revision=stamp;patch.source.assign(source,source+3);
   patch.output.assign(output.begin()+patchStart,output.end());
  };
  for(size_t i=0;i+2<batch.second.size();i+=3){
   const Vertex* vertices=batch.second.data()+i;
   Face face{batch.first,vertices[0],vertices[1],vertices[2],{}};
   std::vector<size_t> candidates;uint64_t localRevision=1469598103934665603ULL;
   for(size_t j=0;j<boxes.size();++j)if(overlap(face,boxes[j].first,boxes[j].second)){
    candidates.push_back(j);localRevision=(localRevision^boxRevisions[j])*1099511628211ULL;
   }
   if(candidates.empty()){output.insert(output.end(),vertices,vertices+3);continue;}
   uint64_t hash=uint64_t(batch.first);
   const auto* bytes=reinterpret_cast<const unsigned char*>(vertices);
   for(size_t j=0;j<3*sizeof(Vertex);++j)hash=(hash^bytes[j])*1099511628211ULL;
   auto& skin=skins[hash];
   if(skin.revision==localRevision&&skin.source.size()==3&&!memcmp(skin.source.data(),vertices,3*sizeof(Vertex))){
    output.insert(output.end(),skin.output.begin(),skin.output.end());continue;
   }
   size_t start=output.size();
   carve(carve,vertices[0],vertices[1],vertices[2],0,candidates);
   skin.revision=localRevision;skin.source.assign(vertices,vertices+3);
   skin.output.assign(output.begin()+start,output.end());
  }
  whole.output=output;batch.second.swap(output);
 };
 const bool verifySkins=M_CheckParm((char*)"-verifyskinthreads")!=0;
 decltype(batches) originalBatches;
 decltype(batchCaches) originalCaches;
 if(verifySkins){originalBatches=batches;originalCaches=batchCaches;}
 if(M_CheckParm((char*)"-singlethreadskins"))for(int i=0;i<(int)jobs.size();++i)clipBatch(i);
 else if(M_CheckParm((char*)"-profileskins")){
  std::vector<double> costs(jobs.size());
  frameParallelFor((int)jobs.size(),[&](int i){auto start=SDL_GetPerformanceCounter();clipBatch(i);costs[i]=1000.0*(SDL_GetPerformanceCounter()-start)/SDL_GetPerformanceFrequency();});
  if(gametic==50)for(size_t i=0;i<jobs.size();++i)if(costs[i]>.2)printf("SKIN material %d source %zu output %zu %.3f ms\n",jobs[i].batch->first,jobs[i].cache->whole.source.size(),jobs[i].batch->second.size(),costs[i]);
 }else frameParallelFor((int)jobs.size(),clipBatch);
 if(verifySkins){
  auto threaded=batches;
  // Assign values rather than map containers: jobs retain pointers to nodes.
  for(auto& batch:batches)batch.second=originalBatches.at(batch.first);
  for(auto& cache:batchCaches)cache.second=std::move(originalCaches.at(cache.first));
  for(int i=0;i<(int)jobs.size();++i)clipBatch(i);
  for(const auto& batch:batches){const auto& expected=threaded.at(batch.first);
   if(expected.size()!=batch.second.size()||(!expected.empty()&&memcmp(expected.data(),batch.second.data(),expected.size()*sizeof(Vertex))))
    I_Error("Parallel skin clipping differs from serial geometry");
  }
 }
 auto skinsEnd=SDL_GetPerformanceCounter();
 for(const auto& entry:world->tiles()){
  const auto& tile=entry.second;if(!tile.edited)continue;
  lab::Vec lo=tile.volume.origin,hi=tile.volume.point(16,16,16);
  auto& cache=cached[entry.first];if(!cache.id)cache.id=++nextSection;
  if(cache.revision!=tile.revision){
   cache.revision=tile.revision;cache.vertices.clear();
   for(const auto& chunk:tile.volume.chunks)for(const auto& triangle:chunk.triangles){
    if(!triangle.damaged)continue;
    // The mesh is the zero set of the excavated field, whose samples never
    // exceed the original field. Its triangles therefore already lie inside
    // the original solid. Re-clipping near-zero rim vertices at large map
    // coordinates amplified float error into holes on shallow, slanted cuts.
    // Drop wholly unchanged faces; retain complete triangles at crater rims.
    float exposed=std::max({tile.volume.field(triangle.a,true),
                           tile.volume.field(triangle.b,true),tile.volume.field(triangle.c,true)});
    if(exposed<=lab::excavationEpsilon)continue;
    std::vector<lab::Vec> polygon{triangle.a,triangle.b,triangle.c};
    auto midpoint=(triangle.a+triangle.b+triangle.c)*(1.f/3);
    const sector_t* owning=R_PointInSubsector((fixed_t)(midpoint.x*FRACUNIT),(fixed_t)(midpoint.y*FRACUNIT))->sector;
    // Orthonormal face coordinates preserve texel size even on steep cuts.
    // Dominant-axis projection compressed the texture into bands there.
    auto normal=lab::normalized(triangle.normal);
    lab::Vec reference=std::abs(normal.z)>.75f?lab::Vec{1,0,0}:lab::Vec{0,0,1};
    auto texU=lab::normalized(lab::cross(reference,normal));
    auto texV=lab::cross(normal,texU);
    auto vertex=[&](lab::Vec p){
     Vertex v{};v.x=p.x;v.y=p.y;v.z=p.z;v.mode=sectorMode(owning)|MODE_ROCK;
     auto field=[&](lab::Vec q){return tile.volume.field(q,false);};
     lab::Vec gradient={field(p+lab::Vec{1,0,0})-field(p-lab::Vec{1,0,0}),
         field(p+lab::Vec{0,1,0})-field(p-lab::Vec{0,1,0}),
         field(p+lab::Vec{0,0,1})-field(p-lab::Vec{0,0,1})};
     auto smooth=lab::length(gradient)>.001f?lab::normalized(gradient):normal;
     v.red=std::abs(smooth.x);v.green=std::abs(smooth.y);v.blue=std::abs(smooth.z);
     auto tint=rubbleSurfaceColor(p.x,p.y,p.z);
     v.lampTint=0xff000000u|unsigned(tint[0]*255)|(unsigned(tint[1]*255)<<8)|(unsigned(tint[2]*255)<<16);
     v.u=lab::dot(p,texU);v.v=lab::dot(p,texV);
     float facet=.82f+.10f*std::abs(smooth.z);
     v.light=facet*(.58f+.42f*std::min(1.f,std::max(0.f,tile.volume.field(p,true))/1.5f));
     return v;
    };
    auto& out=cache.vertices[concrete];
    for(size_t n=1;n+1<polygon.size();++n){
     Vertex a=vertex(polygon[0]),b=vertex(polygon[n]),c=vertex(polygon[n+1]);
     excavationNormal(a,b,c);out.insert(out.end(),{a,b,c});
    }
   }
  }
 }
 auto tilesEnd=SDL_GetPerformanceCounter();
 // Add replacements after clipping ALL tiles; otherwise later boxes would
 // clip the replacement meshes of earlier neighbours.
 std::map<int,size_t> finalSizes;
 for(const auto& entry:world->tiles())if(entry.second.edited)
  for(const auto& batch:cached[entry.first].vertices)finalSizes[batch.first]+=batch.second.size();
 const bool reuse=finalSizes.size()==1&&finalSizes.begin()->first==concrete&&batches[concrete].empty()&&!M_CheckParm((char*)"-nocachedassembly");
 size_t cursor=0;
 if(reuse){
  auto& out=batches[concrete];out.swap(solidAssembly.vertices);
  out.resize(finalSizes.begin()->second);
  solidAssembly.material=concrete;solidAssembly.active=true;
 }else{
  solidAssembly.vertices.clear();solidAssembly.ranges.clear();
  for(const auto& size:finalSizes){auto& out=batches[size.first];out.reserve(out.size()+size.second);}
 }
 const bool verifyAssembly=M_CheckParm((char*)"-verifyassembly")!=0;
 for(const auto& entry:world->tiles())if(entry.second.edited)
  for(const auto& batch:cached[entry.first].vertices){
   auto& out=batches[batch.first];const auto& tile=cached[entry.first];
   size_t first=reuse?cursor:out.size(),count=batch.second.size();
   if(count)vertexSections[&out].push_back({tile.id,first,count,tile.revision});
   if(reuse){
    auto prior=solidAssembly.ranges.find(tile.id);
    if(prior==solidAssembly.ranges.end()||prior->second.first!=first||prior->second.count!=count||prior->second.revision!=tile.revision)
     std::copy(batch.second.begin(),batch.second.end(),out.begin()+first);
    solidAssembly.ranges[tile.id]={first,count,tile.revision};cursor+=count;
    if(verifyAssembly)for(size_t i=0;i<count;++i){
     Vertex actual=out[first+i],expected=batch.second[i];
     actual.lightMask[0]=actual.lightMask[1]=expected.lightMask[0]=expected.lightMask[1]=0;
     if(memcmp(&actual,&expected,sizeof(Vertex)))I_Error("Retained concrete differs from rebuilt assembly");
    }
   }else out.insert(out.end(),batch.second.begin(),batch.second.end());
  }
 if(profileMesh&&gametic==50){double scale=1000.0/SDL_GetPerformanceFrequency();printf("MESH boxes %.3f skins %.3f tiles %.3f append %.3f ms\n",(boxesEnd-meshStart)*scale,(skinsEnd-boxesEnd)*scale,(tilesEnd-skinsEnd)*scale,(SDL_GetPerformanceCounter()-tilesEnd)*scale);}
}

