/* Effects in the air: mist, sunbeams, heat haze and dust motes. */
#include "internal.h"

namespace doom3d {
// Dust scatters sunlight mostly onward: a Henyey-Greenstein lobe (g 0.35,
// averaging 1 over all directions) brightens beams seen toward the sun, about
// 1.7x, and dims them from behind, about 0.55x. x, y, z: eye to the point.
static float sunScatter(float x,float y,float z) {
    if(!settings.sunScatter)return 1;
    constexpr float g=0.35f;
    float distance=std::sqrt(x*x+y*y+z*z);
    if(distance<1e-3f)return 1;
    float toward=(x*sunDirection[0]+y*sunDirection[1]+z*sunDirection[2])/distance;
    return (1-g*g)/std::pow(1+g*g-2*g*toward,1.5f);
}
// Sunbeam ribbons: each baked shaft as a quad turned about its axis to face
// the eye, drawn additively after the world without writing depth; mist.frag
// softens its edges, ends and contact with geometry. Shafts over a floor that
// has since moved are skipped.
void buildShafts(const Uniforms &camera) {
    shaftVertices.clear();
    if(!settings.sunShafts||!settings.sun||sunLevel<=0||camera.effects[2]!=0)return;
    const float *d=sunDirection,*eye=camera.eye;
    for(const SunShaft &s:sunShafts) {
        if(std::fabs(floorZ(&sectors[s.sector])-s.z)>1)continue;
        float half=s.length*0.5f,mid[3]={s.x+d[0]*half-eye[0],s.y+d[1]*half-eye[1],s.z+d[2]*half-eye[2]};
        float distance=std::sqrt(mid[0]*mid[0]+mid[1]*mid[1]+mid[2]*mid[2]);
        if(distance>1600||mid[0]*camera.forward[0]+mid[1]*camera.forward[1]+mid[2]*camera.forward[2]<-half)continue;
        float side[3]={d[1]*mid[2]-d[2]*mid[1],d[2]*mid[0]-d[0]*mid[2],d[0]*mid[1]-d[1]*mid[0]};
        float across=std::sqrt(side[0]*side[0]+side[1]*side[1]+side[2]*side[2]);
        if(across<1e-3f)continue;
        // Seen down the axis a ribbon turns edge-on, so it fades out.
        float facing=std::min(1.0f,across/distance*2.5f),far=std::clamp((1600-distance)/500,0.0f,1.0f);
        float alpha=s.strength*facing*far;
        if(alpha<0.02f)continue;
        for(float &c:side)c*=14/across;
        auto vertex=[&](float u,float v) {
            float t=v*s.length,x=s.x+d[0]*t+side[0]*u,y=s.y+d[1]*t+side[1]*u,z=s.z+d[2]*t+side[2]*u;
            Vertex out={x,y,z,u,v,alpha*sunScatter(x-eye[0],y-eye[1],z-eye[2]),MODE_SUNBEAM};
            out.red=sunTint[0];out.green=sunTint[1];out.blue=sunTint[2];
            return out;
        };
        quad(shaftVertices,vertex(-1,0),vertex(1,0),vertex(1,1),vertex(-1,1));
    }
}
void buildMist(const Uniforms &camera) {
    mistVertices.clear();
    if(!settings.fog||camera.effects[2]!=0)return;
    struct Triangle { float depth; Vertex v[3]; };
    std::vector<Triangle> triangles;
    for(int i=0;i<numsubsectors;++i) {
        const auto &poly=floors[i];if(poly.size()<3)continue;
        const sector_t &sector=*subsectors[i].sector;const auto &mist=sectorMist[&sector-sectors];
        if(!(mist.pit||mist.liquid||mist.toxic)||sector.floorpic==skyflatnum)continue;
        const auto &bounds=floorBounds[i];
        if(camera.eye[0]<bounds[0]-2000||camera.eye[0]>bounds[2]+2000||
           camera.eye[1]<bounds[1]-2000||camera.eye[1]>bounds[3]+2000)continue;
        float bottom=floorZ(&sector),top=ceilZ(&sector);
        float extent=std::min(mist.pit?24.0f:16.0f,std::max(0.0f,top-bottom-2));
        int layers=mist.pit?4:3;
        for(int layer=0;layer<layers&&extent>=4;++layer) {
            float z=bottom+extent*(layer+1)/(layers+1);
            float alpha=(mist.toxic?0.09f:mist.liquid?0.075f:0.06f)*(mist.outdoor?0.65f:1.0f);
            alpha*=1.0f-layer*0.15f;
            for(size_t n=1;n+1<poly.size();++n) {
                Triangle triangle={};int v=0;
                for(Point p:{poly[0],poly[n],poly[n+1]}) {
                    triangle.v[v++]={p.x,p.y,z,layer*0.31f,layer*0.17f,alpha,0,mist.color[0],mist.color[1],mist.color[2]};
                    triangle.depth+=(p.x-camera.eye[0])*camera.forward[0]+(p.y-camera.eye[1])*camera.forward[1]+(z-camera.eye[2])*camera.forward[2];
                }
                triangles.push_back(triangle);
            }
        }
    }
    std::stable_sort(triangles.begin(),triangles.end(),[](const auto &a,const auto &b){return a.depth>b.depth;});
    for(const auto &triangle:triangles)mistVertices.insert(mistVertices.end(),triangle.v,triangle.v+3);
}

// Heat haze: hot-air volumes over lava and hot damaging floors (40 units
// high, side walls along the sector's edges to cooler neighbors) and small
// boxes over warm flames. Heat per vertex: full at the source, a third at
// the top; present.frag turns it into a raster shimmer.
void buildHeat(const Uniforms &camera) {
    heatVertices.clear();
    if(!settings.heatHaze||camera.effects[2]!=0||sectorMist.size()!=(size_t)numsectors)return;
    auto vertex=[&](float x,float y,float z,float heat) {
        float fade=std::clamp((2400-std::hypot(x-camera.eye[0],y-camera.eye[1]))/600,0.0f,1.0f);
        return Vertex{x,y,z,0,0,heat*fade,0};
    };
    constexpr float rise=40;
    for(int i=0;i<numsubsectors;++i) {
        const auto &poly=floors[i];if(poly.size()<3)continue;
        const sector_t &sector=*subsectors[i].sector;
        if(!sectorMist[&sector-sectors].hot||sector.floorpic==skyflatnum)continue;
        const auto &b=floorBounds[i];
        if(camera.eye[0]<b[0]-2400||camera.eye[0]>b[2]+2400||camera.eye[1]<b[1]-2400||camera.eye[1]>b[3]+2400)continue;
        float top=std::min(ceilZ(&sector),floorZ(&sector)+rise);
        if(top<=floorZ(&sector)+2)continue;
        for(size_t n=1;n+1<poly.size();++n)for(Point p:{poly[0],poly[n],poly[n+1]})heatVertices.push_back(vertex(p.x,p.y,top,0.35f));
    }
    for(int i=0;i<numlines;++i) {
        const line_t &line=lines[i];
        for(int side=0;side<2;++side) {
            const sector_t *own=side?line.backsector:line.frontsector,*other=side?line.frontsector:line.backsector;
            if(!own||!sectorMist[own-sectors].hot||own->floorpic==skyflatnum||(other&&sectorMist[other-sectors].hot))continue;
            float ax=units(line.v1->x),ay=units(line.v1->y),bx=units(line.v2->x),by=units(line.v2->y);
            if(std::hypot((ax+bx)/2-camera.eye[0],(ay+by)/2-camera.eye[1])>2400)continue;
            float floor=floorZ(own),top=std::min(ceilZ(own),floor+rise);
            if(top<=floor+2)continue;
            quad(heatVertices,vertex(ax,ay,floor,1),vertex(bx,by,floor,1),vertex(bx,by,top,0.35f),vertex(ax,ay,top,0.35f));
        }
    }
    for(int i=0;i<numsectors;++i)for(mobj_t *thing=sectors[i].thinglist;thing;thing=thing->snext) {
        if(!decorationLight(*thing))continue;
        BakeLight flame=decorationSource(*thing);
        if(std::hypot(flame.x-camera.eye[0],flame.y-camera.eye[1])>1200)continue;
        if(!(flame.color[0]>flame.color[1]*1.15f&&flame.color[0]>flame.color[2]*1.5f))continue;
        float h=10,z0=flame.z-12,z1=flame.z+36;
        Point c[4]={{flame.x-h,flame.y-h},{flame.x+h,flame.y-h},{flame.x+h,flame.y+h},{flame.x-h,flame.y+h}};
        for(int n=0;n<4;++n) {
            Point a=c[n],b=c[(n+1)%4];
            quad(heatVertices,vertex(a.x,a.y,z0,0.8f),vertex(b.x,b.y,z0,0.8f),vertex(b.x,b.y,z1,0),vertex(a.x,a.y,z1,0));
        }
        quad(heatVertices,vertex(c[0].x,c[0].y,z1,0),vertex(c[1].x,c[1].y,z1,0),vertex(c[2].x,c[2].y,z1,0),vertex(c[3].x,c[3].y,z1,0));
    }
}
// Dust motes: one speck in about half of the 48-unit cells around the eye,
// world-anchored with a slow tic-stepped drift, shown only where the
// flashlight beam or a sunbeam (in beamAir) catches it: a point is sunlit when the
// floor point its sun ray leaves from is (the floor sun map, baked at
// sunElevation). Each is one Doom pixel across at its distance.
void buildDust(const Uniforms &camera) {
    if(!settings.dust||camera.effects[2]!=0||seamCells.empty())return;
    const Flash *beam=flashlightOn&&flashes.count&&flashes.lights[0].direction[3]>0?&flashes.lights[0]:nullptr;
    bool sun=settings.sun&&sunLevel>0&&!contactCells.empty();
    if(!beam&&!sun)return;
    constexpr float cell=48;
    float t=leveltime/(float)TICRATE;
    int gx0=(int)std::floor(camera.eye[0]/cell),gy0=(int)std::floor(camera.eye[1]/cell),gz0=(int)std::floor(camera.eye[2]/cell);
    for(int gz=gz0-2;gz<=gz0+2;++gz)for(int gy=gy0-6;gy<=gy0+6;++gy)for(int gx=gx0-6;gx<=gx0+6;++gx) {
        uint32_t seed=(uint32_t)gz*83492791u;
        if(hashUnit(gx,gy,seed)<0.5f)continue;
        float phase=hashUnit(gx,gy,seed^7u)*2*doomPi;
        float p[3]={(gx+hashUnit(gx,gy,seed^1u))*cell+6*std::sin(t*0.31f+phase),(gy+hashUnit(gx,gy,seed^2u))*cell+6*std::cos(t*0.27f+phase),
                    (gz+hashUnit(gx,gy,seed^3u))*cell+4*std::sin(t*0.19f+phase*1.7f)};
        float dx=p[0]-camera.eye[0],dy=p[1]-camera.eye[1],dz=p[2]-camera.eye[2],distance=std::sqrt(dx*dx+dy*dy+dz*dz);
        if(distance<24||distance>320)continue;
        int cx=(int)std::floor((p[0]-mapOrigin[0])/mapCell),cy=(int)std::floor((p[1]-mapOrigin[1])/mapCell);
        if(cx<0||cy<0||cx>=mapWidth||cy>=mapHeight)continue;
        int own=seamCells[(size_t)cy*mapWidth+cx].own;
        if(own==noSector)continue;
        const sector_t &sector=sectors[own];
        // Rain washes the air under the sky clear of dust (weather.cpp).
        if(rainNow>0.01f&&sector.ceilingpic==skyflatnum)continue;
        float floor=floorZ(&sector);
        if(p[2]<floor+2||p[2]>ceilZ(&sector)-2)continue;
        float bright=0,color[3]={};
        if(beam) {
            float amount=flashAt(p[0],p[1],p[2],*beam)/beam->strength;
            bright+=amount;for(int c=0;c<3;++c)color[c]+=beam->color[c]*amount;
        }
        if(sun&&(size_t)own<beamAir.size()&&beamAir[own]) {
            float lift=(p[2]-floor)/std::tan(sunElevation);
            int fx=std::clamp((int)std::floor((p[0]-sunAzimuth[0]*lift-mapOrigin[0])/mapCell),0,mapWidth-1);
            int fy=std::clamp((int)std::floor((p[1]-sunAzimuth[1]*lift-mapOrigin[1])/mapCell),0,mapHeight-1);
            float visible=contactCells[4*((size_t)fy*mapWidth+fx)+2]/255.0f*0.6f;
            bright+=visible;for(int c=0;c<3;++c)color[c]+=sunTint[c]*visible;
        }
        if(bright<0.05f)continue;
        float glint=std::min(1.0f,0.55f+0.5f*bright),h=std::max(0.25f,distance/(100*camera.projection[1]))*0.5f,alpha=std::min(0.75f,bright*0.9f);
        auto vertex=[&](float sx,float sz) {
            Vertex v={p[0]+camera.right[0]*sx+camera.up[0]*sz,p[1]+camera.right[1]*sx+camera.up[1]*sz,p[2]+camera.right[2]*sx+camera.up[2]*sz,0,0,alpha,0};
            v.red=std::min(1.0f,color[0]/bright*glint);v.green=std::min(1.0f,color[1]/bright*glint);v.blue=std::min(1.0f,color[2]/bright*glint);
            return v;
        };
        quad(particleVertices,vertex(-h,h),vertex(h,h),vertex(h,-h),vertex(-h,-h));
    }
}
} // namespace doom3d
