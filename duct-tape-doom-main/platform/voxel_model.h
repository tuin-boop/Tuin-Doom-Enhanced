// Native KVX reader and surface-only greedy mesher. No game scripts are executed.
#pragma once
#include <array>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <algorithm>
#include "sprite_eyes.h"
namespace doomvoxel {
struct Face {std::array<float,3> p;int axis,sign,w,h;uint8_t color;bool eye;int tx,ty;};
struct Model {int size[3];float pivot[3];std::array<uint8_t,768> palette;std::vector<Face> faces;int texWidth=512,texHeight=0;std::vector<uint8_t> texels;};
inline Model decode(const std::vector<uint8_t>&d) {
 auto u32=[&](size_t at)->uint32_t{if(at+4>d.size())throw std::runtime_error("Truncated KVX");return uint32_t(d[at])|uint32_t(d[at+1])<<8|uint32_t(d[at+2])<<16|uint32_t(d[at+3])<<24;};
 auto u16=[&](size_t at)->unsigned{if(at+2>d.size())throw std::runtime_error("Truncated KVX offsets");return unsigned(d[at])|unsigned(d[at+1])<<8;};
 if(d.size()<800)throw std::runtime_error("Short KVX");
 Model m{};size_t volume=1;
 for(int a=0;a<3;++a){m.size[a]=int(u32(4+4*a));m.pivot[a]=int32_t(u32(16+4*a))/256.f;if(m.size[a]<1||m.size[a]>512)throw std::runtime_error("Invalid KVX dimensions");volume*=m.size[a];}
 if(volume>32*1024*1024)throw std::runtime_error("Oversized KVX");
 size_t end=size_t(u32(0))+4,xy=28+size_t(m.size[0]+1)*4,tableEnd=xy+size_t(m.size[0])*(m.size[1]+1)*2;
 if(end>d.size()-768||tableEnd>end)throw std::runtime_error("Invalid KVX mip size");
 std::vector<int16_t> cells(volume,-1);std::vector<uint8_t> visible(volume),eyes(volume);
 auto at=[&](int x,int y,int z){return (size_t(x)*m.size[1]+y)*m.size[2]+z;};
 for(int x=0;x<m.size[0];++x){size_t start=28+u32(28+4*x),next=28+u32(32+4*x);if(start<tableEnd||next<start||next>end)throw std::runtime_error("Invalid KVX X offsets");
  for(int y=0;y<m.size[1];++y){size_t q=xy+(size_t(x)*(m.size[1]+1)+y)*2,b=start+u16(q),e=start+u16(q+2);if(b>e||e>next)throw std::runtime_error("Invalid KVX columns");
   while(b<e){if(e-b<3)throw std::runtime_error("Truncated KVX slab");int z=d[b++],n=d[b++],v=d[b++];if(!n||z+n>m.size[2]||size_t(n)>e-b)throw std::runtime_error("Invalid KVX slab");
    for(int k=0;k<n;++k){size_t i=at(x,y,z+k);if(cells[i]>=0)throw std::runtime_error("Overlapping KVX slab");cells[i]=d[b++];visible[i]=(v&15)|((k==0)?v&16:0)|((k==n-1)?v&32:0);}
   }
  }
 }
 for(size_t i=0;i<768;++i)m.palette[i]=uint8_t(std::min(255,int(d[d.size()-768+i])*4));
 // Project four side views, classify the same enclosed colored eye spots as
 // sprites, then mark only the visible voxel surface (never the whole palette).
 for(int axis=0;axis<2;++axis)for(int side=0;side<2;++side){int w=m.size[1-axis],h=m.size[2];std::vector<uint8_t> image(size_t(w)*h*2);std::vector<size_t> ids(size_t(w)*h,volume);
  for(int u=0;u<w;++u)for(int z=0;z<h;++z)for(int k=0;k<m.size[axis];++k){int q=side?m.size[axis]-1-k:k;size_t i=axis?at(u,q,z):at(q,u,z);if(cells[i]<0)continue;size_t p=size_t(z)*w+u;image[2*p]=uint8_t(cells[i]);image[2*p+1]=255;ids[p]=i;break;}
  auto mask=doom_sprite_eyes(w,h,image.data(),m.palette.data());for(size_t i=0;i<mask.size();++i)if(mask[i]&&ids[i]<volume)eyes[ids[i]]=1;
 }
 // Merge coplanar surfaces across color changes; preserve detail in an indexed atlas.
 int shelfX=0,shelfY=0,shelfH=0;
 for(int axis=0;axis<3;++axis)for(int side=0;side<2;++side){int u=(axis+1)%3,v=(axis+2)%3,w=m.size[u],h=m.size[v];std::vector<int> mask(size_t(w)*h);
  for(int plane=0;plane<m.size[axis];++plane){std::fill(mask.begin(),mask.end(),-1);
   for(int a=0;a<w;++a)for(int b=0;b<h;++b){int xyz[3];xyz[axis]=plane;xyz[u]=a;xyz[v]=b;size_t i=at(xyz[0],xyz[1],xyz[2]);if(cells[i]>=0&&(visible[i]&(1<<(axis*2+side))))mask[size_t(b)*w+a]=cells[i]+(eyes[i]?256:0);}
   for(int b=0;b<h;++b)for(int a=0;a<w;){int value=mask[size_t(b)*w+a];if(value<0){++a;continue;}int width=1,height=1;while(a+width<w&&mask[size_t(b)*w+a+width]>=0)++width;
    bool more=true;while(b+height<h&&more){for(int k=0;k<width;++k)if(mask[size_t(b+height)*w+a+k]<0){more=false;break;}if(more)++height;}
    Face f{};f.p[axis]=float(plane+side);f.p[u]=float(a);f.p[v]=float(b);f.axis=axis;f.sign=side?1:-1;f.w=width;f.h=height;f.color=uint8_t(value&255);f.eye=(value&256)!=0;if(shelfX+width>m.texWidth){shelfY+=shelfH;shelfX=0;shelfH=0;}
    f.tx=shelfX;f.ty=shelfY;shelfX+=width;shelfH=std::max(shelfH,height);
    m.texHeight=std::max(m.texHeight,shelfY+height);
    m.texels.resize(size_t(m.texWidth)*m.texHeight*2);
    f.eye=false;
    for(int j=0;j<height;++j)for(int k=0;k<width;++k){int c=mask[size_t(b+j)*w+a+k];size_t dest=(size_t(f.ty+j)*m.texWidth+f.tx+k)*2;m.texels[dest]=uint8_t(c&255);m.texels[dest+1]=(c&256)?255:0;f.eye|=(c&256)!=0;}
    m.faces.push_back(f);
    if(m.faces.size()>1000000)throw std::runtime_error("KVX mesh exceeds limit");
    for(int j=0;j<height;++j)for(int k=0;k<width;++k)mask[size_t(b+j)*w+a+k]=-1;a+=width;
   }
  }
 }
 return m;
}
}
