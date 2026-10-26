#include "internal.h"
#include "../mod_archive.h"
#include "../voxel_model.h"
#include <regex>
#include <memory>
#include <stdexcept>
namespace doom3d {
namespace {
std::unique_ptr<doommod::Archive> voxelArchive;
bool attempted=false;
struct Entry {std::string file;float angle=0;std::unique_ptr<doomvoxel::Model> model;int key=0;};
std::map<std::string,Entry> voxelEntries;
unsigned voxelMap=~0u;bool registered=false;
size_t meshBytes=0;
void registerVoxels() {
 if(registered||!voxelDetected())return;registered=true;
 std::map<std::string,std::string> files;
 for(const auto &n:voxelArchive->names())if(n.rfind("voxels/",0)==0&&n.size()>4&&n.substr(n.size()-4)==".kvx")files[n.substr(7,n.size()-11)]=n;
 if(gamemode==commercial)for(const auto &n:voxelArchive->names())if(n.rfind("filter/doom.id.doom2/voxels/",0)==0&&n.substr(n.size()-4)==".kvx")files[n.substr(28,n.size()-32)]=n;
 auto definitions=[&](const char *name){if(!voxelArchive->contains(name))return;
  std::string text=voxelArchive->text(name);text=std::regex_replace(text,std::regex(R"(/\*[\s\S]*?\*/|//[^\r\n]*)"),"");
  std::regex line(R"rx(([A-Za-z0-9_\[\]\\^]{5})\s*=\s*"([^"]+)"\s*\{([^}]*)\})rx");
  for(std::sregex_iterator i(text.begin(),text.end(),line),end;i!=end;++i){std::string key=(*i)[1],model=(*i)[2],props=(*i)[3];for(char &c:model)c=(char)tolower((unsigned char)c);for(char &c:key)c=(char)toupper((unsigned char)c);
   auto f=files.find(model);if(f==files.end())continue;Entry entry;entry.file=f->second;std::smatch a;if(std::regex_search(props,a,std::regex(R"(AngleOffset\s*=\s*(-?[0-9.]+))")))entry.angle=std::stof(a[1]);voxelEntries[key]=std::move(entry);
  }
 };
 definitions("voxeldef.txt");if(gamemode==commercial)definitions("filter/doom.id.doom2/voxeldef.txt");
 fprintf(stderr,"VoxelDoom detected: %zu model frames (Cheello). Native models; scripts are not executed.\n",voxelEntries.size());
}
void loadModel(Entry &e) {
 if(e.model)return;
 auto model=std::make_unique<doomvoxel::Model>(doomvoxel::decode(voxelArchive->read(e.file)));
 size_t bytes=model->faces.size()*sizeof(doomvoxel::Face)+model->texels.size();
 if(meshBytes+bytes>384*1024*1024)throw std::runtime_error("Voxel model cache exceeds 384 MB");
 meshBytes+=bytes;e.model=std::move(model);
}
void ensureTexture(Entry &e) {
 if(!e.key)e.key=-3000000; // Assigned below, independent of WAD lump keys.
 static int next=-3000001;if(e.key==-3000000)e.key=next--;
 if(images.count(e.key))return;
 const auto &m=*e.model;const byte *pal=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
 byte remap[256];
 for(int c=0;c<256;++c){int best=0,dist=INT_MAX;for(int i=0;i<256;++i){int d=0;for(int k=0;k<3;++k){int delta=int(m.palette[c*3+k])-pal[i*3+k];d+=delta*delta;}if(d<dist){dist=d;best=i;}}remap[c]=byte(best);}
 std::vector<byte> pixels(m.texels.size()),normals(m.texels.size());
 for(size_t i=0;i<m.texels.size();i+=2){pixels[i]=remap[m.texels[i]];pixels[i+1]=255;normals[i]=normals[i+1]=m.texels[i+1]?255:128;}
 images.emplace(e.key,upload(m.texWidth,m.texHeight,pixels.data(),0,0,nullptr,normals.data()));
}
}
bool voxelDetected() {
 if(attempted)return bool(voxelArchive);attempted=true;
 int arg=M_CheckParm((char*)"-voxeldoom");if(!arg)return false;
 if(arg+1>=myargc||myargv[arg+1][0]=='-')I_Error("-voxeldoom requires a PK3 path");
 try {
  auto a=std::make_unique<doommod::Archive>(myargv[arg+1]);
  if(!a->contains("voxeldef.txt")||!a->contains("voxels/possa.kvx")||!a->contains("zscript/cheellovox/cheellomonstersdoom1.zc"))throw std::runtime_error("Not a supported VoxelDoom pack");
  voxelArchive=std::move(a);
 }catch(const std::exception &e){I_Error("VoxelDoom: %s",e.what());}
 return bool(voxelArchive);
}
void prepareVoxelModels() {
 if(!settings.voxelModels||!voxelDetected())return;
 try {registerVoxels();if(voxelMap==r_levelserial)return;voxelMap=r_levelserial;
  // Preload every animation in each actor family already present on the map.
  // This avoids a disk read when a monster first turns or attacks.
  std::set<std::string> families;
  for(int i=0;i<numsectors;++i)for(mobj_t *m=sectors[i].thinglist;m;m=m->snext)if(m->sprite>=0&&m->sprite<NUMSPRITES)families.insert(sprnames[m->sprite]);
  for(auto &e:voxelEntries)if(families.count(e.first.substr(0,4))){loadModel(e.second);ensureTexture(e.second);}
 }catch(const std::exception &e){I_Error("VoxelDoom preload: %s",e.what());}
}
bool drawVoxel(std::map<int,std::vector<Vertex>> &out,const mobj_t &thing,int sprite,int frame,angle_t facing,float x,float y,float z,float light,const std::array<float,3> &baked,unsigned statics,unsigned tint,const Uniforms &camera) {
 if(!settings.voxelModels||!voxelDetected()||sprite<0||sprite>=NUMSPRITES)return false;
 std::string key=sprnames[sprite];key+=char('A'+(frame&FF_FRAMEMASK));auto found=voxelEntries.find(key);if(found==voxelEntries.end())return false;
 try {
  Entry &entry=found->second;loadModel(entry);ensureTexture(entry);const auto &m=*entry.model;
  float angle=float(facing*(2.0*doomPi/4294967296.0))+(entry.angle+90)*doomPi/180;
  float cs=std::cos(angle),sn=std::sin(angle);auto &vertices=out[entry.key];
  unsigned mode=MODE_SPRITE|MODE_VOXEL|(thing.flags&MF_SHADOW?MODE_FUZZ:0);
  bool full=frame&FF_FULLBRIGHT;if(!full)mode|=MODE_RELIT;
  bool eyes=settings.glowingEyes&&settings.voxelEyes&&(thing.flags&MF_COUNTKILL)&&thing.health>0;
  if(eyes)mode|=MODE_GLOWING_EYES;
  float dx=camera.eye[0]-x,dy=camera.eye[1]-y;
  const float localEye[3]={cs*dx+sn*dy+m.pivot[0],sn*dx-cs*dy+m.pivot[1],m.pivot[2]-(camera.eye[2]-z)};
  // Reject invisible back faces before transforming or uploading any vertices.
  for(const auto &f:m.faces){if((localEye[f.axis]-f.p[f.axis])*f.sign<=0)continue;int u=(f.axis+1)%3,v=(f.axis+2)%3;Vertex q[4];
   for(int n=0;n<4;++n){auto p=f.p;p[u]+=(n==1||n==2)?f.w:0;p[v]+=n>=2?f.h:0;
    float lx=p[0]-m.pivot[0],ly=-(p[1]-m.pivot[1]);
    q[n]={x+cs*lx-sn*ly,y+sn*lx+cs*ly,z+m.pivot[2]-p[2],float(f.tx+((n==1||n==2)?f.w:0)),float(f.ty+(n>=2?f.h:0)),light,mode};
    q[n].red=full?0:baked[0];q[n].green=full?0:baked[1];q[n].blue=full?0:baked[2];q[n].statics=statics;q[n].lampTint=tint;
   }
   if(f.sign>0)quad(vertices,q[0],q[1],q[2],q[3]);else quad(vertices,q[3],q[2],q[1],q[0]);
  }
  return true;
 }catch(const std::exception &e){I_Error("VoxelDoom model %s: %s",key.c_str(),e.what());}
 return false;
}
extern "C" void I_TestVoxelModels() {
 if(!voxelDetected())I_Error("Voxel test needs -voxeldoom");registerVoxels();size_t faces=0,eyes=0;
 for(auto &e:voxelEntries){loadModel(e.second);for(auto &f:e.second.model->faces){++faces;if(f.eye)++eyes;}}
 if(voxelEntries.size()<200||faces==0||eyes==0)I_Error("Incomplete voxel model decode");
 // Malformed assets must fail before allocating a model or reading past data.
 bool rejected=false;try{doomvoxel::decode(std::vector<uint8_t>(800,255));}catch(const std::exception&){rejected=true;}
 if(!rejected)I_Error("Malformed KVX accepted");
 printf("PASS VoxelDoom: %zu frames, %zu merged faces, %zu eye faces; malformed asset rejected; cache %zu bytes\n",voxelEntries.size(),faces,eyes,meshBytes);
}
}
