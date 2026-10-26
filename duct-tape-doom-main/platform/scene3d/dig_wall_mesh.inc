// Constant facet normals prevent per-pixel floor/wall lighting classification noise.
static void excavationNormal(Vertex &a,Vertex &b,Vertex &c){
    float ux=b.x-a.x,uy=b.y-a.y,uz=b.z-a.z,vx=c.x-a.x,vy=c.y-a.y,vz=c.z-a.z;
    float x=uy*vz-uz*vy,y=uz*vx-ux*vz,z=ux*vy-uy*vx,len=std::sqrt(x*x+y*y+z*z);
    if(len<1e-8f)return;
    auto pack=[&](float v){return unsigned(std::clamp(std::lround((v/len+1)*511.5f),0l,1023l));};
    unsigned normal=pack(x)|(pack(y)<<10)|(pack(z)<<20);
    for(Vertex *v:{&a,&b,&c}){v->mode|=MODE_FACET_NORMAL;v->statics=normal;}
}
// Rebuild only a damaged wall's small rectangular patch; retain UVs outside.
static bool addExcavatedWall(const line_t &line,int side,int span,Point a,Point b,float bottom,float top,int tex,float anchor,float light){
    if(!P_GlobalDigEnabled()||span==3)return false;
    float bounds[4];int id=int(&line-lines);
    if(!P_GlobalDigBounds(id,side,bounds))return false;
    float length=std::hypot(b.x-a.x,b.y-a.y);if(length<.001f)return false;
    float left=std::max(0.f,std::floor(bounds[0]/8)*8),right=std::min(length,std::ceil(bounds[1]/8)*8);
    float low=std::max(bottom,std::floor(bounds[2]/8)*8),high=std::min(top,std::ceil(bounds[3]/8)*8);
    if(left>=right||low>=high)return false;
    int lump=W_CheckNumForName((char*)"DIGROCK");
    int concrete=lump>=0?-1-lump:R_TextureNumForName((char*)"GRAY1");
    if(lump>=0)lumpImage(lump,true);else wallImage(concrete);
    struct Cache {int through=-1;unsigned revision=0;int serial=-1,texture=0;float bottom=0,top=0,anchor=0,light=0,offset=0;std::map<int,std::vector<Vertex>> vertices;};
    static std::map<int,Cache> cache;static int serial=-1;
    if(serial!=r_levelserial){cache.clear();serial=r_levelserial;}
    Cache &c=cache[(id*2+side)*4+span];float offset=units(sides[line.sidenum[side]].textureoffset);
    if(c.through!=P_GlobalDigThroughEnabled()||c.revision!=P_GlobalDigWallRevision(id,side)||c.bottom!=bottom||c.top!=top||c.texture!=tex||c.anchor!=anchor||c.offset!=offset){
        c.through=P_GlobalDigThroughEnabled();c.revision=P_GlobalDigWallRevision(id,side);c.bottom=bottom;c.top=top;c.texture=tex;c.light=light;c.anchor=anchor;c.offset=offset;c.vertices.clear();
        float tx=(b.x-a.x)/length,ty=(b.y-a.y)/length;unsigned mode=sectorMode(sides[line.sidenum[side]].sector);
        auto vertex=[&](float u,float z,float depth,bool rock){
            Vertex v={a.x+u*tx-depth*ty,a.y+u*ty+depth*tx,z,rock?u:offset+u,rock?-z:anchor-z,1,mode};
            if(depth>0)P_GlobalDigWallPoint(id,side,u,z,depth,&v.x,&v.y);
            if(!rock)bakeWallCoordinates(line,side,u,v,true);
            return v;
        };
        auto emitTriangle=[&](int material,Vertex a,Vertex b,Vertex third){
            std::vector<Vertex> poly={a,b,third},kept;
            Vertex previous=poly.back();float pm=P_GlobalDigVoidMargin(id,previous.x,previous.y,previous.z);
            for(Vertex current:poly){float cm=P_GlobalDigVoidMargin(id,current.x,current.y,current.z);
                if((pm<=0)!=(cm<=0)){float t=pm/(pm-cm);Vertex v=previous;
                    v.x+=(current.x-previous.x)*t;v.y+=(current.y-previous.y)*t;v.z+=(current.z-previous.z)*t;
                    v.u+=(current.u-previous.u)*t;v.v+=(current.v-previous.v)*t;v.light+=(current.light-previous.light)*t;kept.push_back(v);
                }if(cm<=0)kept.push_back(current);previous=current;pm=cm;
            }
            auto &out=c.vertices[material];for(size_t n=1;n+1<kept.size();++n)out.insert(out.end(),{kept[0],kept[n],kept[n+1]});
        };
        auto rectangle=[&](float x0,float x1,float z0,float z1){if(x1<=x0||z1<=z0)return;
            quad(c.vertices[tex],vertex(x0,z1,0,false),vertex(x1,z1,0,false),vertex(x1,z0,0,false),vertex(x0,z0,0,false));};
        rectangle(0,left,bottom,top);rectangle(right,length,bottom,top);
        rectangle(left,right,bottom,low);rectangle(left,right,high,top);
        struct Sample{float u,z,d;};
        auto triangle=[&](float u0,float z0,float u1,float z1,float u2,float z2){
            std::vector<Sample> points={{u0,z0,P_GlobalDigDepth(id,side,u0,z0)},{u1,z1,P_GlobalDigDepth(id,side,u1,z1)},{u2,z2,P_GlobalDigDepth(id,side,u2,z2)}};
            // Both ends of a passage use the same source aperture. Remove only
            // the broken-through portion, retaining the fractured concrete rim.
            if(P_GlobalDigBreachFace(id,side)){
                std::vector<Sample> kept;
                Sample previous=points.back();float pm=P_GlobalDigBreachMargin(id,side,previous.u,previous.z);
                for(Sample current:points){float cm=P_GlobalDigBreachMargin(id,side,current.u,current.z);
                    if((pm<0)!=(cm<0)){
                        float t=pm/(pm-cm);Sample v={previous.u+(current.u-previous.u)*t,previous.z+(current.z-previous.z)*t,previous.d+(current.d-previous.d)*t};
                        v.d=P_GlobalDigDepth(id,side,v.u,v.z);kept.push_back(v);
                    }
                    if(cm<0)kept.push_back(current);previous=current;pm=cm;
                }
                points=std::move(kept);if(points.size()<3)return;
            }
            // Clip the material boundary through triangles instead of painting
            // whole grid cells concrete. Both polygons share the exact rim.
            for(bool rock:{false,true}){
                std::vector<Sample> clipped;
                Sample previous=points.back();bool inside=rock?previous.d>=.12f:previous.d<=.12f;
                for(Sample current:points){bool next=rock?current.d>=.12f:current.d<=.12f;
                    if(next!=inside){float t=(.12f-previous.d)/(current.d-previous.d);clipped.push_back({previous.u+(current.u-previous.u)*t,previous.z+(current.z-previous.z)*t,.12f});}
                    if(next)clipped.push_back(current);previous=current;inside=next;
                }
                for(size_t n=1;n+1<clipped.size();++n){
                    Sample a=clipped[0],b=clipped[n],third=clipped[n+1];
                    Vertex v0=vertex(a.u,a.z,a.d,rock),v1=vertex(b.u,b.z,b.d,rock),v2=vertex(third.u,third.z,third.d,rock);
                    if(rock){
                        float ax=v1.x-v0.x,ay=v1.y-v0.y,az=v1.z-v0.z,bx=v2.x-v0.x,by=v2.y-v0.y,bz=v2.z-v0.z;
                        float nx=ay*bz-az*by,ny=az*bx-ax*bz,nz=ax*by-ay*bx;
                        float normal=std::sqrt(nx*nx+ny*ny+nz*nz);
                        for(Vertex *v:{&v0,&v1,&v2}){
                            if(std::abs(nz)>std::max(std::abs(nx),std::abs(ny))){v->u=v->x;v->v=-v->y;}
                            else {v->u=std::abs(nx)>std::abs(ny)?v->y:v->x;v->v=-v->z;}
                        }
                        float facet=normal>.0001f?(.7f+.22f*std::abs(nz)/normal+.08f*std::abs(nx)/normal):.7f;
                        // A narrow dark fracture lip, then exposed gray aggregate.
                        for(auto pair:{std::pair<Vertex*,float>{&v0,a.d},{&v1,b.d},{&v2,third.d}})pair.first->light=facet*(.58f+.42f*std::min(1.f,pair.second/1.5f));
                    }
                    excavationNormal(v0,v1,v2);
                    emitTriangle(rock?concrete:tex,v0,v2,v1);
                }
            }
        };
        // Close exposed ends of an excavated face. These are side surfaces,
        // not strips across the aperture. Without them a corner can reveal
        // the space behind its neighbour when the two depths differ.
        for(float edge:{0.f,length}){
            if(P_GlobalDigWallJoined(id,side,edge>0?1:0))continue;
            for(float z=low;z<high;z+=4){float up=std::min(high,z+4);
                float d0=P_GlobalDigDepth(id,side,edge,z),d1=P_GlobalDigDepth(id,side,edge,up);
                if(d0<=.001f&&d1<=.001f)continue;
                Vertex v0=vertex(edge,z,0,true),v1=vertex(edge,up,0,true),v2=vertex(edge,up,d1,true),v3=vertex(edge,z,d0,true);
                for(Vertex *v:{&v0,&v1,&v2,&v3}){v->u=v->x+v->y;v->v=-v->z;v->light=.72f;}
                excavationNormal(v0,v1,v2);v3.mode=v0.mode;v3.statics=v0.statics;
                emitTriangle(concrete,v0,v1,v2);emitTriangle(concrete,v0,v2,v3);
            }
        }
        // Lattice boundaries match P_GlobalDigDepth, including partial cells.
        for(float u=left;u<right;){float next=std::min(right,(std::floor(u/4)+1)*4);if(next<=u)break;
            sector_t *moving=side?line.frontsector:line.backsector;
            float shift=moving?(span==1?units(moving->ceilingheight):span==2?units(moving->floorheight):0):0;
            for(float z=low;z<high;){float up=std::min(high,shift+(std::floor((z-shift)/4)+1)*4);if(up<=z)break;
                triangle(u,z,next,z,next,up);triangle(u,z,next,up,u,up);z=up;}
            for(float edge:{bottom,top})if(edge>=low&&edge<=high){
                sector_t *back=line.sidenum[1-side]>=0?sides[line.sidenum[1-side]].sector:nullptr;
                if(back&&(std::abs(edge-floorZ(back))<.001f||std::abs(edge-ceilZ(back))<.001f))continue;
                if(back&&P_GlobalDigBreachSupportRoom(int(back-sectors))>=0){
                    float exposedFloor=floorZ(back),exposedTop=ceilZ(back);P_GlobalDigBreachRoomBounds(int(back-sectors),&exposedFloor,&exposedTop);
                    if(std::abs(edge-exposedFloor)<.001f||std::abs(edge-exposedTop)<.001f)continue;
                }
                float d0=P_GlobalDigDepth(id,side,u,edge),d1=P_GlobalDigDepth(id,side,next,edge);
                if(d0>0||d1>0)quad(c.vertices[concrete],vertex(u,edge,0,true),vertex(next,edge,0,true),vertex(next,edge,d1,true),vertex(u,edge,d0,true));
            }u=next;
        }
    }
    for(auto &part:c.vertices){auto &out=batches[part.first];for(Vertex v:part.second){v.light*=light;out.push_back(v);}}
    return true;
}


