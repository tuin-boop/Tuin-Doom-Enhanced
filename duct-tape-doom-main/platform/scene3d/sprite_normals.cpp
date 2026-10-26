/* Sprite normals: the silhouette inflated into a body, so a light near the
   view shades a monster like a figure rather than a flat cutout. */
#include "internal.h"

namespace doom3d {
// Squared Euclidean distance transform along one line (Felzenszwalb).
static void distanceLine(const float *f,int n,float *d,int *v,float *z) {
    int k=0;v[0]=0;z[0]=-1e20f;z[1]=1e20f;
    for(int q=1;q<n;++q) {
        float s;
        while((s=((f[q]+q*q)-(f[v[k]]+v[k]*v[k]))/(2.0f*(q-v[k])))<=z[k]) --k;
        ++k;v[k]=q;z[k]=s;z[k+1]=1e20f;
    }
    k=0;
    for(int q=0;q<n;++q) {while(z[k+1]<q)++k;d[q]=(q-v[k])*(q-v[k])+f[v[k]];}
}
// Blur along x (dx=1) or y (dy=1) over opaque pixels only.
static void blurOpaque(std::vector<float> &height,const std::vector<char> &opaque,int w,int h,int dx,int dy,float sigma) {
    int radius=(int)std::ceil(sigma*2.5f);
    std::vector<float> weights(radius+1),out(height.size());
    for(int i=0;i<=radius;++i)weights[i]=std::exp(-0.5f*i*i/(sigma*sigma));
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        size_t i=(size_t)y*w+x;if(!opaque[i])continue;
        float sum=0,weight=0;
        for(int t=-radius;t<=radius;++t) {
            int sx=x+t*dx,sy=y+t*dy;
            if(sx<0||sy<0||sx>=w||sy>=h||!opaque[(size_t)sy*w+sx])continue;
            sum+=height[(size_t)sy*w+sx]*weights[std::abs(t)];weight+=weights[std::abs(t)];
        }
        out[i]=sum/weight;
    }
    height.swap(out);
}
// Each pixel takes the radius of the largest disk inside the silhouette that
// covers it and rises as a round tube of that radius: the torso a broad
// cylinder, arms, legs and guns thin tubes of their own, rather than one
// distance field whose ridges bulge at shoulders, elbows and the gun. A
// mostly vertical blur keeps the body an upright cylinder. True depth, in
// texels (0 where empty); the ragdoll's relief (ragdoll_body.cpp) stands on it.
std::vector<float> spriteTubeHeights(int w,int h,const byte *pixels) {
    // Distance to the nearest transparent pixel; outside the frame counts.
    int pw=w+2,ph=h+2,longest=std::max(pw,ph);
    std::vector<float> grid((size_t)pw*ph,0),line(longest),out(longest),z(longest+1);
    std::vector<int> v(longest);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)if(pixels[((size_t)y*w+x)*2+1])grid[(size_t)(y+1)*pw+x+1]=1e20f;
    for(int x=0;x<pw;++x) {
        for(int y=0;y<ph;++y)line[y]=grid[(size_t)y*pw+x];
        distanceLine(line.data(),ph,out.data(),v.data(),z.data());
        for(int y=0;y<ph;++y)grid[(size_t)y*pw+x]=out[y];
    }
    for(int y=0;y<ph;++y) {
        distanceLine(&grid[(size_t)y*pw],pw,out.data(),v.data(),z.data());
        std::copy(out.begin(),out.begin()+pw,grid.begin()+(size_t)y*pw);
    }
    std::vector<char> opaque((size_t)w*h);
    std::vector<float> distance((size_t)w*h,0),thickness((size_t)w*h,0);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        size_t i=(size_t)y*w+x;opaque[i]=pixels[i*2+1]!=0;
        if(opaque[i])thickness[i]=distance[i]=std::sqrt(grid[(size_t)(y+1)*pw+x+1]);
    }
    // Paint each maximal disk (one no neighbor's disk contains) over the
    // pixels it covers.
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        float d=distance[(size_t)y*w+x];if(d==0)continue;
        bool maximal=true;
        for(int j=-1;j<=1&&maximal;++j)for(int k=-1;k<=1;++k) {
            int nx=x+k,ny=y+j;if((!j&&!k)||nx<0||ny<0||nx>=w||ny>=h)continue;
            if(distance[(size_t)ny*w+nx]>=d+std::sqrt((float)(j*j+k*k))-0.01f) {maximal=false;break;}
        }
        if(!maximal)continue;
        int r=(int)d;
        for(int j=-r;j<=r;++j)for(int k=-r;k<=r;++k) {
            int nx=x+k,ny=y+j;if(nx<0||ny<0||nx>=w||ny>=h||j*j+k*k>d*d)continue;
            float &t=thickness[(size_t)ny*w+nx];t=std::max(t,d);
        }
    }
    // A round cross-section. Distances run between pixel centers; the outline lies half a pixel out.
    std::vector<float> height((size_t)w*h,0);
    for(size_t i=0;i<height.size();++i)if(opaque[i]) {
        float e=distance[i]-0.5f,r=thickness[i]-0.5f;
        height[i]=std::sqrt(std::max(0.0f,e*(2*r-e)));
    }
    blurOpaque(height,opaque,w,h,1,0,1.2f); // softens the tubes' center crease
    blurOpaque(height,opaque,w,h,0,1,std::clamp(h*0.04f,1.5f,4.0f));
    return height;
}
// The tubes as normals, deepened: under a light from the view a true
// cylinder stays bright almost to its outline and reads flat again. A weaker
// vertical tilt keeps the body upright. Packed as unsigned x/y (texture
// down) for the sprite's B/A.
std::vector<byte> spriteNormals(int w,int h,const byte *pixels) {
    std::vector<float> height=spriteTubeHeights(w,h,pixels);
    for(float &v:height)v*=1.75f;
    auto at=[&](int x,int y){return x<0||y<0||x>=w||y>=h?0.0f:height[(size_t)y*w+x];};
    std::vector<byte> normals((size_t)w*h*2,128);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        if(!pixels[((size_t)y*w+x)*2+1])continue;
        float nx=-(at(x+1,y)-at(x-1,y))*0.5f,ny=-(at(x,y+1)-at(x,y-1))*0.5f*0.6f;
        float length=std::sqrt(nx*nx+ny*ny+1);
        normals[((size_t)y*w+x)*2]=(byte)std::lround((nx/length*0.5f+0.5f)*255);
        normals[((size_t)y*w+x)*2+1]=(byte)std::lround((ny/length*0.5f+0.5f)*255);
    }
    return normals;
}
}
