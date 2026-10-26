/* The visible world each frame: leaf visibility, walls, flats and things
   batched per texture. */
#include "internal.h"
#include "frame_workers.h"
#include "p_digwall.h"
#include "../../../scene.h"
#include "../../../map_world.h"
#include "../../../skin_partition.h"
extern const lab::Scene *SV_Scene();
extern const lab::MapWorld *SV_MapWorld();
extern float SV_DoorRenderZ(float fraction);

namespace doom3d {
bool customGunPuff(const mobj_t *puff);
void quad(std::vector<Vertex> &out,Vertex a,Vertex b,Vertex c,Vertex d) {
    out.insert(out.end(),{a,b,c,a,c,d});
}
#include "dig_mesh.inc"
#include "dig_wall_mesh.inc"
#include "dig_flat_mesh.inc"
#include "solid_mesh.inc"
#include "solid_map_mesh.inc"
static void addWall(const line_t &line,int side,Point a,Point b,float bottom,float top,int tex,float anchor,float light,bool facing,int span) {
    wallLights(line,side,a,b,bottom,top,tex,anchor,span);
    if(!facing)return;
    if(top<=bottom)return;
    if(tex<=0){
        // Some classic maps omit a required upper/lower texture and rely on
        // software flat filling. Seal that solid span with its backing flat.
        const sector_t *back=side?line.frontsector:line.backsector;
        if(!back||(span!=1&&span!=2))return;
        int flat=span==1?back->ceilingpic:back->floorpic;
        if(flat==skyflatnum)return;
        tex=-1-(firstflat+flattranslation[flat]);lumpImage(-1-tex,true);
    }else {tex=texturetranslation[tex];wallImage(tex);}
    const Image &material=tex<0?lumpImage(-1-tex,true):wallImage(tex);
    float u=units(sides[line.sidenum[side]].textureoffset),len=std::hypot(b.x-a.x,b.y-a.y);
    if(addExcavatedWall(line,side,span,a,b,bottom,top,tex,anchor,light))return;
    auto &out=batches[tex];unsigned mode=sectorMode(sides[line.sidenum[side]].sector);
    if(material.leaded&&settings.emissive)mode|=MODE_STAINED_GLASS;
    size_t first=out.size();
    Vertex corners[4]={{a.x,a.y,top,u,anchor-top,light,mode},{b.x,b.y,top,u+len,anchor-top,light,mode},
                       {b.x,b.y,bottom,u+len,anchor-bottom,light,mode},{a.x,a.y,bottom,u,anchor-bottom,light,mode}};
    // Wall coordinates restart on every line, so each section seeds its own tiles.
    unsigned seed=(unsigned)(&line-lines)*8u+(unsigned)side*4u+(unsigned)span+1u;
    if(!addBrokenWall(out,line,side,span,material,a,b,bottom,top,u,anchor,light,mode,seed)) {
        for(int n=0;n<4;++n) {bakeWallCoordinates(line,side,n==1||n==2?len:0,corners[n],true);corners[n].statics=seed;}
        quad(out,corners[0],corners[1],corners[2],corners[3]);
    }
    lampGlow(out,first,sides[line.sidenum[side]].sector,wallImage(tex));
}
// The mirrored pass draws the world a second time, so it runs only while a
// reflective floor can be on screen. Doom's own visibility answers that: the
// BSP front to back from the eye, one-sided walls and closed doors covering
// the view angles behind them until the whole view is covered. Walls are full
// height and the sky hides what lies past it, so the map's plan is enough; a
// closed door under a sky ceiling still counts as open, since sky ceilings
// rise above their walls (skyTop). Angles run from the view direction.
struct ViewCover {
    std::vector<std::pair<float,float>> spans; // Sorted, merged.
    // A span from lo across width radians, split where it wraps past behind.
    template<class F> static bool parts(float lo,float width,F &&f) {
        float hi=lo+width;
        if(hi<=doomPi) return f(lo,hi);
        return f(lo,(float)doomPi)&&f((float)-doomPi,hi-2*(float)doomPi);
    }
    bool hidden(float lo,float width) const {
        return parts(lo,width,[&](float a,float b){
            for(const auto &s:spans) if(s.first<=a&&s.second>=b) return true;
            return false;
        });
    }
    void cover(float lo,float width) {
        parts(lo,width,[&](float a,float b){
            spans.push_back({a,b});std::sort(spans.begin(),spans.end());
            size_t count=0;
            for(size_t i=0;i<spans.size();++i) {
                auto s=spans[i];
                if(count&&s.first<=spans[count-1].second)spans[count-1].second=std::max(spans[count-1].second,s.second);
                else spans[count++]=s;
            }
            spans.resize(count);return true;
        });
    }
    bool full() const {return spans.size()==1&&spans[0].first<=-doomPi&&spans[0].second>=doomPi;}
};
// Which of the candidate leaves the eye can see (visible[leaf] set).
static void visibleLeaves(const Uniforms &camera,const std::vector<char> &candidate,std::vector<char> &visible,bool mapLines=false) {
    float ex=camera.eye[0],ey=camera.eye[1];
    float yaw=std::atan2(camera.right[0],-camera.right[1]);
    auto relative=[&](float x,float y){return std::remainder(std::atan2(y-ey,x-ex)-yaw,2*(float)doomPi);};
    // Looking up or down widens the view's footprint on the map: the frustum's
    // corners lean back toward the eye, and once its lower or upper edge passes
    // straight down or up the footprint is the whole circle.
    float tanH=1/camera.projection[0],tanV=1/camera.projection[1];
    float flat=std::hypot(camera.forward[0],camera.forward[1]),reach=flat-tanV*std::abs(camera.forward[2]);
    float half=reach>0.05f?std::min((float)doomPi,std::atan2(tanH,reach)+0.05f):(float)doomPi;
    static ViewCover view;view.spans.clear();
    if(half<doomPi){view.spans={{-(float)doomPi,-half},{half,(float)doomPi}};}
    // A convex shape's angles from the eye, a little wider so a wall's end
    // never quite hides a floor running up to it; false when the eye is inside.
    auto extent=[&](const Point *points,size_t count,float &lo,float &width) {
        float base=relative(points[0].x,points[0].y),low=0,high=0;
        for(size_t i=1;i<count;++i) {
            float d=std::remainder(relative(points[i].x,points[i].y)-base,2*(float)doomPi);
            low=std::min(low,d);high=std::max(high,d);
        }
        if(high-low>=doomPi-0.01f) return false;
        lo=std::remainder(base+low-0.01f,2*(float)doomPi);width=high-low+0.02f;return true;
    };
    auto seen=[&](const Point *points,size_t count) {
        float lo,width;
        return !extent(points,count,lo,width)||!view.hidden(lo,width);
    };
    std::function<void(unsigned)> walk=[&](unsigned node) {
        if(view.full()) return;
        if(node&NF_SUBSECTOR) {
            unsigned leaf=node&~NF_SUBSECTOR;
            if(leaf>=(unsigned)numsubsectors) return;
            const auto &poly=floors[leaf];
            if(!candidate.empty()&&candidate[leaf]&&poly.size()>=3&&seen(poly.data(),poly.size())) visible[leaf]=1;
            const subsector_t &sub=subsectors[leaf];
            for(int i=0;i<sub.numlines;++i) {
                const seg_t &seg=segs[sub.firstline+i];
                const sector_t *front=seg.frontsector,*back=seg.backsector;
                bool solid=!back||(front->ceilingpic!=skyflatnum&&back->ceilingpic!=skyflatnum&&
                    (ceilZ(back)<=floorZ(front)||floorZ(back)>=ceilZ(front)));
                float a1=relative(units(seg.v1->x),units(seg.v1->y)),a2=relative(units(seg.v2->x),units(seg.v2->y));
                float span=std::remainder(a1-a2,2*(float)doomPi);
                if(span>0){
                    if(mapLines&&!view.hidden(a2,span))seg.linedef->flags|=ML_MAPPED;
                    if(solid&&!P_GlobalDigBreachFace(int(seg.linedef-lines),seg.sidedef==&sides[seg.linedef->sidenum[0]]?0:1))view.cover(a2,span);
                }
            }
            return;
        }
        if(node>=(unsigned)numnodes) return;
        const node_t &n=nodes[node];
        float px=ex-units(n.x),py=ey-units(n.y);
        int side=units(n.dy)*px-units(n.dx)*py>0?0:1;
        walk(n.children[side]);
        // The far side only if some of its box is still in view.
        const fixed_t *box=n.bbox[side^1];
        Point corners[4]={{units(box[BOXLEFT]),units(box[BOXTOP])},{units(box[BOXRIGHT]),units(box[BOXTOP])},
                          {units(box[BOXRIGHT]),units(box[BOXBOTTOM])},{units(box[BOXLEFT]),units(box[BOXBOTTOM])}};
        bool inside=ex>=corners[0].x&&ex<=corners[1].x&&ey>=corners[2].y&&ey<=corners[0].y;
        if(inside||seen(corners,4)) walk(n.children[side^1]);
    };
    walk(numnodes?unsigned(numnodes-1):unsigned(NF_SUBSECTOR));
}
// The nearest reflective liquid below the eye that it can see sets this
// frame's mirror plane (visibleLeaves).
static void findReflection(const Uniforms &camera) {
    reflectionActive=false;
    if(!settings.reflections||(profile.skip&I_RENDER3D_SKIP_REFLECTIONS)||camera.effects[2]!=0)return;
    static std::vector<char> candidate,visible;
    static std::vector<std::pair<float,int>> near;near.clear();
    for(int i=0;i<numsectors;++i) {
        if(!sectorMist[i].reflective||sectors[i].floorpic==skyflatnum)continue;
        float z=floorZ(&sectors[i]);if(camera.eye[2]<z+1)continue;
        for(int leaf:sectorFloors[i]) {
            const auto &b=floorBounds[leaf];
            float dx=std::max({b[0]-camera.eye[0],0.0f,camera.eye[0]-b[2]}),dy=std::max({b[1]-camera.eye[1],0.0f,camera.eye[1]-b[3]});
            float d=std::hypot(dx,dy);
            if(d<1024)near.push_back({d,leaf});
        }
    }
    if(!near.empty()) {
        candidate.assign(numsubsectors,0);visible.assign(numsubsectors,0);
        for(const auto &n:near)candidate[n.second]=1;
        visibleLeaves(camera,candidate,visible);
        float nearest=1024;
        for(const auto &[d,leaf]:near) if(visible[leaf]&&d<nearest) {
            nearest=d;reflectionPlane=floorZ(subsectors[leaf].sector);reflectionActive=true;
        }
    }
}
// Vanilla draws sky wherever a sky flat would be, hiding everything past
// it; sky ceilings and the open band above a lower sky neighbor therefore
// go into depth with the sky shader. A sky ceiling rises to the tallest
// sprite in its sector, with sky walls along its edges up to that height,
// so trees and monsters still stand out against a low sky. skyTops gives
// each sector's sky height.
static const std::vector<float> &skyTops(float fraction) {
    static std::vector<float> skyTop;skyTop.resize(numsectors);
    for(int i=0;i<numsectors;++i) {
        skyTop[i]=ceilZ(&sectors[i]);
        if(sectors[i].ceilingpic!=skyflatnum)continue;
        for(const mobj_t *thing=sectors[i].thinglist;thing;thing=thing->snext) {
            if(thing->sprite<0||thing->sprite>=numsprites)continue;
            const auto &sprite=sprites[thing->sprite];int frame=thing->frame&FF_FRAMEMASK;
            if(frame>=sprite.numframes)continue;
            const auto &sf=sprite.spriteframes[frame];
            float x,y,z;interpolatedPosition(*thing,fraction,x,y,z);
            for(int r=0;r<(sf.rotate?8:1);++r)skyTop[i]=std::max(skyTop[i],z+units(spritetopoffset[sf.lump[r]])+1);
        }
    }
    return skyTop;
}
// Floors and ceilings, batched per flat; sky flats go into skyVertices.
static void flatGeometry(const std::vector<float> &skyTop) {
    for(int i=0;i<numsubsectors;++i) {
        const auto &poly=floors[i]; if(poly.size()<3) continue;
        const sector_t *sector=subsectors[i].sector;
        if(digMeshEnabled()&&sector->tag==19001)continue;
        int support=P_GlobalDigBreachSupportRoom(int(sector-sectors));
        for(int ceiling=0;ceiling<2;++ceiling) {
            // A destroyed closed slab needs a roof at the exposed room's
            // ceiling, not its original coincident floor/ceiling plane.
            int flat=ceiling?(support>=0?sectors[support].ceilingpic:sector->ceilingpic):support>=0?sectors[support].floorpic:sector->floorpic;
            if(flat==skyflatnum) {
                float z=ceiling?skyTop[sector-sectors]:floorZ(sector);
                for(size_t n=1;n+1<poly.size();++n) for(Point p:{poly[0],poly[n],poly[n+1]}) skyVertices.push_back({p.x,p.y,z});
                continue;
            }
            int lump=firstflat+flattranslation[flat]; lumpImage(lump,true);
            float z=sectorHeight(sector,ceiling),light=lighting(support>=0?&sectors[support]:sector);
            if(ceiling&&support>=0){float floor=floorZ(sector);P_GlobalDigBreachRoomBounds(int(sector-sectors),&floor,&z);}
            // MODE_MIRROR_FLOOR: this floor samples the mirrored pass.
            unsigned mirror=reflectionActive&&!ceiling&&sectorMist[sector-sectors].reflective&&std::abs(z-reflectionPlane)<0.5f?MODE_MIRROR_FLOOR:0;
            if(!ceiling&&sectorMist[sector-sectors].liquid)mirror|=MODE_LIQUID;

            if(addExcavatedFlat(i,ceiling,-1-lump,z,light,mirror|sectorMode(sector)))continue;
            auto &out=batches[-1-lump];size_t first=out.size();
            if(!addBrokenFlat(out,i,int(sector-sectors),ceiling,lumpImage(lump,true),z,light,mirror|sectorMode(sector)))
                for(size_t n=1;n+1<poly.size();++n) for(Point p:{poly[0],poly[n],poly[n+1]})
                    out.push_back({p.x,p.y,z,p.x,-p.y,light,mirror|sectorMode(sector)});
            lampGlow(out,first,sector,lumpImage(lump,true));
        }
    }
}
// The upper, lower, middle and one-sided wall sections of every line side,
// and sky walls along the edges of sky ceilings.
// allSides: also wall sides facing away from the eye, for frames that
// capture glass probes from elsewhere.
static void wallGeometry(const Uniforms &camera,const std::vector<float> &skyTop,bool allSides) {
    auto skyWall=[](Point a,Point b,float bottom,float top) {
        if(top<=bottom)return;
        for(const Vertex &v:{Vertex{a.x,a.y,bottom},Vertex{b.x,b.y,bottom},Vertex{b.x,b.y,top},
                             Vertex{a.x,a.y,bottom},Vertex{b.x,b.y,top},Vertex{a.x,a.y,top}})skyVertices.push_back(v);
    };
    for(int i=0;i<numlines;++i) {
        const line_t &line=lines[i];
        if(digMeshEnabled()&&((line.frontsector&&line.frontsector->tag==19001)||(line.backsector&&line.backsector->tag==19001)))continue;
        for(int side=0;side<2;++side) {
            if(line.sidenum[side]<0) continue;
            const side_t &s=sides[line.sidenum[side]];
            const sector_t *front=s.sector,*back=side?line.frontsector:line.backsector;
            Point a={units((side?line.v2:line.v1)->x),units((side?line.v2:line.v1)->y)};
            Point b={units((side?line.v1:line.v2)->x),units((side?line.v1:line.v2)->y)};
            bool facing=allSides||(b.x-a.x)*(camera.eye[1]-a.y)-(b.y-a.y)*(camera.eye[0]-a.x)<=0;
            if(!facing&&P_GlobalDigEnabled()){float damage[4];facing=P_GlobalDigBounds(i,side,damage)!=0;}
            float floor=floorZ(front),ceil=ceilZ(front),row=units(s.rowoffset),light=lighting(front);
            if(std::abs(b.y-a.y)<0.01) light=std::max(0.1f,light-0.04f);
            if(std::abs(b.x-a.x)<0.01) light=std::min(1.0f,light+0.04f);
            if(!back) {
                P_GlobalDigBreachRoomBounds(int(front-sectors),&floor,&ceil);
                float anchor=ceil;
                if(s.midtexture && (line.flags&ML_DONTPEGBOTTOM)) anchor=floor+units(textureheight[s.midtexture]);
                addWall(line,side,a,b,floor,ceil,s.midtexture,anchor+row,light,facing,0);
            } else {
                float bf=floorZ(back),bc=ceilZ(back);
                bool sharedSky=front->ceilingpic==skyflatnum&&back->ceilingpic==skyflatnum;
                if(bc<ceil&&!sharedSky) {
                    float anchor=(line.flags&ML_DONTPEGTOP)?ceil:bc+(s.toptexture?units(textureheight[s.toptexture]):0);
                    addWall(line,side,a,b,std::max(bc,floor),ceil,s.toptexture,anchor+row,light,facing,1);
                }
                if(bf>floor) {
                    float anchor=(line.flags&ML_DONTPEGBOTTOM)?ceil:bf;
                    addWall(line,side,a,b,floor,std::min(bf,ceil),s.bottomtexture,anchor+row,light,facing,2);
                }
                if(s.midtexture) {
                    int tex=texturetranslation[s.midtexture]; Image &image=wallImage(tex);
                    float low=std::max(floor,bf),high=std::min(ceil,bc);
                    float anchor=(line.flags&ML_DONTPEGBOTTOM)?low+image.height:high;
                    anchor+=row;
                    float top=std::min(high,anchor),bottom=std::max(low,anchor-image.height);
                    addWall(line,side,a,b,bottom,top,s.midtexture,anchor,light,facing,3);
                }
            }
            if(front->ceilingpic==skyflatnum)
                skyWall(a,b,back&&back->ceilingpic==skyflatnum?std::max(skyTop[back-sectors],floor):ceil,skyTop[front-sectors]);
        }
    }
}
struct Caster { const mobj_t *thing;const Image *image;int lump;bool flip;float x,y,z,distance;RagdollCard card{}; };
// The rotation of a sprite seen from (dx,dy) away, for a thing facing angle.
static unsigned spriteRotation(float dx,float dy,angle_t facing) {
    angle_t angle=(angle_t)(int64_t)(atan2(dy,dx)*(4294967296.0/(2*doomPi)));
    return (angle-facing+(unsigned)(ANG45/2)*9)>>29;
}
// The dynamic light on a thing (pulse, rgb) and the strongest light above it
// that can cast its shadow; returns that light's strength. The classic
// flashlight goes to beam instead, in the shaders' 1/8 steps (beamSteps).
static void dynamicThingLight(const mobj_t *thing,float x,float y,float z,std::array<float,3> &pulse,std::array<float,3> &beam,ShadowLights &shadowLights) {
    for(unsigned n=0;n<flashes.count;++n) {
        if(fogOnly[n])continue;
        float contribution=flashAt(x,y,z+units(thing->height)*0.5f,flashes.lights[n]);
        if(settings.classicFlashlight&&flashes.lights[n].direction[3]>0) {
            float stepped=std::floor(std::min(contribution,1.0f)*8+0.5f)/8;
            for(int c=0;c<3;++c) beam[c]+=stepped*flashes.lights[n].color[c];
        } else {
            // Static lights reach sprites through the baked light at their feet.
            float dynamic=contribution*(1-(bakedLightsActive?flashes.lights[n].baked:0));
            for(int c=0;c<3;++c) pulse[c]+=dynamic*flashes.lights[n].color[c];
        }
        bool bounce=lightKinds[n]==I_RENDER3D_LIGHT_FLASHLIGHT&&flashes.lights[n].direction[3]<=0;
        if(!bounce&&lightSources[n]!=thing&&flashes.lights[n].position[2]>z+8) {
            const Flash &f=flashes.lights[n];
            shadowLights.add({f.position[0],f.position[1],f.position[2],contribution});
        }
    }
}
// Immutable scene inputs, independent output per actor. No image-cache or
// geometry mutation occurs in the workers.
struct ThingLighting {
    mobj_t *thing;int sector;float x,y,z;bool mirrored;
    std::array<float,3> pulse{},beam{},baked{};
    ShadowLights shadows;StaticSample statics;
};
static void lightThing(ThingLighting &v) {
    v.pulse={};v.beam={};v.shadows={};v.statics={};
    dynamicThingLight(v.thing,v.x,v.y,v.z,v.pulse,v.beam,v.shadows);
    v.baked=bakedLightAt(v.x,v.y,v.z+units(v.thing->height)*0.5f,&sectors[v.sector],&v.statics);
    staticShadows(v.statics,v.z,v.shadows);
}
// Things standing in a liquid sink in a little, like Heretic's
// footclip (the floor hides their feet; small pickups only a
// little), and thick liquids wrap a wobbling collar of goo around
// them. Pools are often several sectors at one height, so every
// liquid floor counts, not only pits. Returns how far the sprite sinks.
static float sinkInLiquid(const mobj_t *thing,int i,const Image &image,float x,float y,const Uniforms &camera,bool collar=true,bool down=false) {
    if(!settings.splashes||(size_t)i>=sectorMist.size()||!sectorMist[i].liquid||(!down&&thing->z>sectors[i].floorheight)||
       (thing->flags&(MF_MISSILE|MF_NOBLOCKMAP|MF_FLOAT))||sectors[i].floorpic==skyflatnum)return 0;
    if(collar&&sectorMist[i].thick&&!(thing->flags&MF_SPECIAL)&&std::hypot(x-camera.eye[0],y-camera.eye[1])<1024) {
        float r=units(thing->radius)*0.55f;int tic=leveltime/4;
        uint32_t seed=(uint32_t)(uintptr_t)thing;
        auto color=gooColor(i,seed,x,y,floorZ(&sectors[i]),camera);
        for(int k=0;k<6;++k) {
            float angle=(k+hashUnit(k,tic,seed)*0.5f)*doomPi/3,wobble=r*(0.9f+0.25f*hashUnit(k,tic,seed^3u));
            pixelBlock(particleVertices,camera,x+std::cos(angle)*wobble,y+std::sin(angle)*wobble,floorZ(&sectors[i])+0.2f,2.0f,0.6f,color,true,&color);
        }
    }
    return std::min(8.0f,image.height*0.35f);
}
// The glass mirror's view (glassMirrorActive) for the sprites drawn into it:
// the mirrored eye, and the right of an ordinary camera there. The mirror
// pass's own axes are reflected, which already flips its whole image left
// to right, so sprites are laid out as that unflipped camera sees them, the
// reflected right reversed; otherwise they turn the wrong way in the glass.
struct MirrorView { float eye[2],right[2]; };
// One thing in sector i: its shadows, light, liquid footclip and sprite
// quad. Things the flashlight beam can outline join casters. With mirror,
// also its quad for the glass mirror (mirrorBatches), from the same light:
// seen from the mirrored eye, so its frame shows the side facing the glass,
// and facing the mirrored camera. The player's own sprite gets only that one.
static void addThing(mobj_t *thing,int i,const Uniforms &camera,float fraction,const Flash *beam,std::vector<Caster> &casters,
                     const MirrorView *mirror,const ThingLighting &lit) {
    bool self=thing==players[displayplayer].mo;
    if((self&&!mirror)||thing->sprite<0||thing->sprite>=numsprites) return;
    if(customGunPuff(thing)||liquidProjectileHidden(thing))return;
    DeathView death={thing->sprite,thing->frame,thing->angle,false,1,0,{}};
    bool directed=!self&&settings.deathDirector&&deathView(*thing,fraction,death);
    if(death.sprite<0||death.sprite>=numsprites)return;
    const auto &sprite=sprites[death.sprite]; int frame=death.frame&FF_FRAMEMASK;
    if(frame>=sprite.numframes) return;
    const auto &sf=sprite.spriteframes[frame];
    auto rotationFrom=[&](const float *eye){return sf.rotate&&!death.fall.active?spriteRotation(units(thing->x)-eye[0],units(thing->y)-eye[1],death.facing):0u;};
    auto flipped=[&](unsigned seen){return (bool)sf.flip[seen]!=death.mirror;};
    unsigned rotation=rotationFrom(camera.eye);
    int lump=firstspritelump+sf.lump[rotation]; Image &image=lumpImage(lump,false);
    float x,y,z;interpolatedPosition(*thing,fraction,x,y,z);
    // A ragdoll is lit, sunk and grounded where its body lies, not where the corpse slid to: thrown
    // across a light's edge or down steps, it was lit as where it died.
    bool dollDown=false;
    if(death.ragdoll) {
        dollDown=ragdollWhere(*death.ragdoll,fraction,x,y,z);
        i=(int)(R_PointInSubsector((fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT))->sector-sectors);
    }
    // Hit flinches and death jolts (body_motion.cpp): drawn in strips, a hop lifts the body.
    // A directed death scales the jolt by its phase and adds the landing's settle.
    BodyPose pose;bool posed=!self&&!death.fall.active&&(thing->flags&MF_COUNTKILL)&&bodyPose(*thing,fraction,pose);
    if(directed) {
        if(!posed)pose={};
        for(int k=0;k<2;++k){pose.lean[k]*=death.bend;pose.whip[k]*=death.bend;}
        pose.squash*=death.bend;pose.flatten=death.settle;
        posed=posed||death.settle>0;
    }
    if(posed)z+=pose.lift;
    bool ragdoll=false;
    float strongest=0;ShadowLight shadowLight{};
    if(!self)addEnemyShadow(*thing,image,lump,flipped(rotation),x,y,z,camera);
    if(!self&&beam&&!(thing->flags&(MF_MISSILE|MF_NOBLOCKMAP|MF_SPECIAL))&&(thing->flags&(MF_SOLID|MF_SHOOTABLE))&&!(death.frame&FF_FULLBRIGHT))
        casters.push_back({thing,&image,lump,(bool)flipped(rotation),x,y,z,std::hypot(x-camera.eye[0],y-camera.eye[1])});
    auto pulse=lit.pulse,beamLight=lit.beam,baked=lit.baked;
    const auto &shadowLights=lit.shadows;
    const auto &statics=lit.statics;
    if(!self)for(const auto &light:shadowLights.lights)if(light.strength>0.025f)
        addEnemyShadow(*thing,image,lump,flipped(rotation),x,y,z,camera,&light);
    float light=lighting(&sectors[i],death.frame&FF_FULLBRIGHT); unsigned mode=MODE_SPRITE|(thing->flags&MF_SHADOW?MODE_FUZZ:0);
    // Per-pixel lighting from silhouette normals (flipped with a mirrored frame, below).
    if(settings.detail&&!(death.frame&FF_FULLBRIGHT))mode|=MODE_RELIT;
    // Airborne blood shines like the floor blood (needs the normals of MODE_RELIT).
    if(settings.bloodShine&&Game_ThingRole(thing->type)==GAME_THING_BLOOD&&(mode&MODE_RELIT))mode|=MODE_WET_SPRITE;
    if(settings.glowingEyes&&(thing->flags&MF_COUNTKILL)&&thing->health>0)mode|=MODE_GLOWING_EYES;
    if(!(death.frame&FF_FULLBRIGHT))light=sunLightAt(x,y,&sectors[i],flowLightAt(x,y,&sectors[i],seamLightAt(x,y,&sectors[i],light)));
    // MODE_SOFT: effect sprites (shots, puffs, fogs) dither out at floors
    // and ceilings and draw in front of walls within half their width.
    bool soft=settings.softSprites&&((thing->flags&MF_MISSILE)||((thing->flags&MF_NOBLOCKMAP)&&(thing->flags&MF_NOGRAVITY)));
    if(soft)mode|=MODE_SOFT|sectorMode(&sectors[i]);
    if(thing->flags&MF_SPECIAL) {
        // Keep supplies readable; reserve bloom for light-emitting pickups.
        float phase=(float)((uintptr_t)thing%997)*0.37f;
        light=std::max(light,0.5f+0.08f*std::sin(gameSeconds()*3.0f+phase));if(glowingPickup(thing->sprite))mode|=MODE_PICKUP;
    }
    // Emissive WAD frames keep their original white cores and artwork;
    // their light still illuminates surrounding surfaces and other sprites.
    // Detail sprites light themselves per pixel from the light mask, so
    // their tint carries only the baked light. With bake-only lights they
    // shape it by where it comes from (statics).
    // The classic beam lifts toward BEAM_CEILING (lighting.glsl), never past it, as in world.frag.
    if(mode&MODE_RELIT)pulse=baked;
    else for(int c=0;c<3;++c) {pulse[c]+=baked[c];pulse[c]+=beamLight[c]*std::max(0.0f,1.2f-light-pulse[c]);}
    unsigned packedStatics=0;
    float towards=std::sqrt(statics.towards[0]*statics.towards[0]+statics.towards[1]*statics.towards[1]+statics.towards[2]*statics.towards[2]);
    if(bakeOnlyActive&&(mode&MODE_RELIT)&&statics.amount>0&&towards>1e-6f) {
        for(int c=0;c<3;++c)packedStatics|=(unsigned)std::lround((statics.towards[c]/towards*0.5f+0.5f)*255)<<(8*c);
        packedStatics|=(unsigned)std::lround(std::clamp(towards/statics.amount,0.0f,1.0f)*255)<<24;
    }
    if(death.frame&FF_FULLBRIGHT) pulse={};
    unsigned tint=death.frame&FF_FULLBRIGHT?0xFFFFFFFFu:packedLampTint(statics,light);
    // MODE_GROUNDED: grounding darkens the feet of things that rest on the floor.
    // Sprites have no sun map, so vSun carries strength and height above floor.
    float lift=units(thing->z-thing->floorz)+(posed?pose.lift:0),grounding=1-lift/32;
    bool grounded=settings.detail&&grounding>0&&!(death.frame&FF_FULLBRIGHT)&&!soft&&Game_ThingRole(thing->type)!=GAME_THING_BLOOD&&
        !(thing->flags&(MF_SHADOW|MF_MISSILE|MF_SPAWNCEILING));
    // The quad for one view: that view's frame, facing along its right.
    auto emit=[&](std::map<int,std::vector<Vertex>> &out,unsigned seen,float rx,float ry,bool collar) {
        if(!death.ragdoll&&drawVoxel(out,*thing,death.sprite,death.frame,death.facing,x,y,z,light,baked,packedStatics,tint,camera))return;
        int lump=firstspritelump+sf.lump[seen]; Image &image=lumpImage(lump,false);
        float left=-(float)image.left,right=left+image.width,top=z+image.top,bottom=top-image.height;
        if((thing->flags&MF_COUNTKILL)&&thing->health>0){
            float floor=floorZ(&sectors[i]),ceiling=ceilZ(&sectors[i]);
            float shift=std::max(0.f,floor-bottom);
            if(sectors[i].ceilingpic!=skyflatnum&&ceiling-floor>=image.height)shift=std::min(shift,ceiling-top);
            top+=shift;bottom+=shift;
        }
        float fittedFeet=top-image.top;
        if(death.fall.active&&!death.fall.rig) {
            // A fall's drawing set down on the floor, lifted, pressed flat.
            float keep=1-death.fall.flatten;
            top=z+(image.top-death.fall.ground)*keep+death.fall.lift;bottom=z+(image.top-image.height-death.fall.ground)*keep+death.fall.lift;
        }
        float sink=!ragdoll?sinkInLiquid(thing,i,image,x,y,camera,collar):dollDown?sinkInLiquid(thing,i,image,x,y,camera,collar,true):0;top-=sink;bottom-=sink;
        float u0=flipped(seen)?image.width:0,u1=flipped(seen)?0:image.width;
        unsigned m=mode|((mode&MODE_RELIT)&&flipped(seen)?MODE_FLIPPED:0);
        auto &vertices=out[-1-lump];size_t first=vertices.size();
        if(collar&&!self)woundView(thing,players[displayplayer].mo,image,flipped(seen),x,y,fittedFeet-sink,top,bottom,rx,ry,posed?&pose:nullptr);
        // Wound holes (wounds.cpp): texels shot through, and their rims.
        std::vector<byte> holes;bool wounded=!self&&woundMask(*thing,image,flipped(seen),rx,ry,holes);
        const std::vector<byte> *cut=wounded?&holes:nullptr;
        auto finish=[&](std::vector<Vertex> &batch,size_t from) {
            for(size_t n=from;n<batch.size();++n) {
                batch[n].red=pulse[0];batch[n].green=pulse[1];batch[n].blue=pulse[2];batch[n].statics=packedStatics;
                batch[n].lampTint=tint;
                if(grounded) {batch[n].mode|=MODE_GROUNDED;batch[n].sunU=grounding;batch[n].sunV=batch[n].z-z+lift;}
                if(soft)batch[n].sunU=std::clamp(image.width*0.5f,8.0f,64.0f);
            }
        };
        RagdollCard card;
        if(ragdoll) {
            // A ragdoll: its virtual camera's picture on an upright card (ragdoll.cpp), already mirrored.
            if(ragdollCard(*death.ragdoll,fraction,sink,camera.eye,cut,card)) {
                auto &batch=out[card.key];size_t from=batch.size();unsigned cm=m&~MODE_FLIPPED;
                float l=-card.half,r=card.half,t=card.top,b=card.top-card.size,s=(float)card.size;
                quad(batch,{card.x+rx*l,card.y+ry*l,t,0,0,light,cm},{card.x+rx*r,card.y+ry*r,t,s,0,light,cm},
                     {card.x+rx*r,card.y+ry*r,b,s,s,light,cm},{card.x+rx*l,card.y+ry*l,b,0,s,light,cm});
                finish(batch,from);
                if(collar) {
                    const Image &shadow=images.at(card.key);
                    int shadowLump=-1-card.key; // the shadow renderer resolves images by -1-lump
                    addEnemyShadow(*thing,shadow,shadowLump,false,card.x,card.y,z,camera,nullptr,false,&card);
                    if(strongest>0.025f)addEnemyShadow(*thing,shadow,shadowLump,false,card.x,card.y,z,camera,&shadowLight,false,&card);
                    if(beam&&!(thing->flags&(MF_MISSILE|MF_NOBLOCKMAP|MF_SPECIAL))&&(thing->flags&(MF_SOLID|MF_SHOOTABLE))&&!(death.frame&FF_FULLBRIGHT))
                        casters.push_back({thing,&shadow,shadowLump,false,card.x,card.y,z,std::hypot(card.x-camera.eye[0],card.y-camera.eye[1]),card});
                }
            }
            return;
        }
        if(death.fall.rig)fallQuads(vertices,{0,0,0,0,0,light,m},death.fall,x,y,z-sink,rx,ry,flipped(seen),cut);
        else if(posed)bodyQuads(vertices,pose,{0,0,0,0,0,light,m},x,y,z-sink,rx,ry,left,right,image.top,image.height,u0,u1,cut);
        else if(wounded)woundedQuads(vertices,{0,0,0,0,0,light,m},image,holes,x,y,top,rx,ry,flipped(seen),(top-bottom)/image.height);
        else quad(vertices,{x+rx*left,y+ry*left,top,u0,0,light,m},{x+rx*right,y+ry*right,top,u1,0,light,m},
            {x+rx*right,y+ry*right,bottom,u1,(float)image.height,light,m},{x+rx*left,y+ry*left,bottom,u0,(float)image.height,light,m});
        finish(vertices,first);
    };
    if(!self)emit(batches,rotation,camera.right[0],camera.right[1],true);
    if(mirror)emit(mirrorBatches,rotationFrom(mirror->eye),mirror->right[0],mirror->right[1],false);
}
// The player's own shadow: the player sprite as seen from the strongest
// light above (not the flashlight or the player's own shots), or from
// the sun where the player stands in it.
static void playerShadow(const Uniforms &camera,float fraction) {
    mobj_t *self=players[displayplayer].mo;
    if(!(settings.playerShadow&&camera.effects[2]==0&&self&&self->sprite>=0&&self->sprite<numsprites))return;
    float x,y,z;interpolatedPosition(*self,fraction,x,y,z);
    ShadowLights lights;
    for(unsigned n=0;n<flashes.count;++n) {
        const Flash &f=flashes.lights[n];
        if(fogOnly[n]||lightSources[n]==self||lightKinds[n]==I_RENDER3D_LIGHT_FLASHLIGHT||f.position[2]<=z+8)continue;
        float amount=flashAt(x,y,z+28,f);
        lights.add({f.position[0],f.position[1],f.position[2],amount});
    }
    if(bakeOnlyActive||gridLightActive()) {
        StaticSample statics;
        bakedLightAt(x,y,z+28,self->subsector->sector,&statics);
        staticShadows(statics,z,lights);
    }
    bool found=std::any_of(lights.lights.begin(),lights.lights.end(),[](const auto&light){return light.strength>0.025f;});
    if(!found&&settings.sun&&sunLevel>0&&!contactCells.empty()) {
        int cx=std::clamp((int)std::floor((x-mapOrigin[0])/mapCell),0,mapWidth-1),cy=std::clamp((int)std::floor((y-mapOrigin[1])/mapCell),0,mapHeight-1);
        float visible=contactCells[4*((size_t)cy*mapWidth+cx)+2]/255.0f;
        if(visible>0.5f)lights.add({x+sunAzimuth[0]*766,y+sunAzimuth[1]*766,z+643,0.6f*visible});
    }
    int frame=self->frame&FF_FRAMEMASK;
    if(frame<sprites[self->sprite].numframes)for(const auto &light:lights.lights)if(light.strength>0.025f) {
        const auto &sf=sprites[self->sprite].spriteframes[frame];
        unsigned rotation=sf.rotate?spriteRotation(light.x-x,light.y-y,self->angle):0;
        int lump=firstspritelump+sf.lump[rotation];
        addEnemyShadow(*self,lumpImage(lump,false),lump,sf.flip[rotation],x,y,z,camera,&light,true);
    }
}
void geometry(const Uniforms &camera,float fraction) {
    prepareVoxelModels();
    const bool spike=M_CheckParm((char*)"-profilespikes")!=0;
    const bool detailed=spike||M_CheckParm((char*)"-profilegeometry")!=0;
    uint64_t mark=detailed?SDL_GetTicksNS():0;
    static std::array<double,10> totals{};static int frames=0;
    std::array<double,10> current{};
    auto timing=[&](int n){if(detailed){auto now=SDL_GetTicksNS();current[n]=(now-mark)/1e6;totals[n]+=current[n];mark=now;}};

    // The hardware renderer bypasses R_StoreWallRange, which normally records
    // automap discovery. Reuse our front-to-back occlusion traversal once a tic.
    static int mappedTic=-1;static unsigned mappedLevel=~0u;
    if(mappedTic!=gametic||mappedLevel!=r_levelserial){
        mappedTic=gametic;mappedLevel=r_levelserial;
        std::vector<char> unused;
        visibleLeaves(camera,unused,unused,true);
    }
    retainSolidAssembly();
    vertexSections.clear();
    for(auto &batch:batches)batch.second.clear();
    for(auto &batch:shadowBatches)batch.second.clear();
    for(auto &batch:mirrorBatches)batch.second.clear();
    surfaceLights.clear();
    std::copy(camera.eye,camera.eye+3,surfaceEye);collectFlashes(fraction,camera);
    timing(0);collectFlatLights();timing(1);
    findReflection(camera);
    skyVertices.clear();
    const auto &skyTop=skyTops(fraction);
    timing(2);
    if(!buildSolidScene(fraction)){
        flatGeometry(skyTop);timing(3);
        wallGeometry(camera,skyTop,planGlassCaptures(camera));
        buildDigMesh();
        buildSolidMapMesh();
    }
    timing(4);collectSurfaceLights();timing(5);
    buildImpacts(camera);
    buildGlass(camera);
    timing(6);static std::vector<Caster> casters;casters.clear();
    const Flash *beam=settings.flashlightShadows&&flashlightOn&&camera.effects[2]==0&&flashes.count&&flashes.lights[0].direction[3]>0?&flashes.lights[0]:nullptr;
    uint64_t spritesStart=profileStart();
    // The glass mirror's sprites: every thing on the eye's side of its plane.
    MirrorView mirror;
    if(glassMirrorActive&&camera.effects[2]==0) {
        const float *n=glassMirrorPlane;
        float side=n[0]*camera.eye[0]+n[1]*camera.eye[1]-n[3],along=n[0]*camera.right[0]+n[1]*camera.right[1];
        mirror={{camera.eye[0]-2*side*n[0],camera.eye[1]-2*side*n[1]},{2*along*n[0]-camera.right[0],2*along*n[1]-camera.right[1]}};
    }
    bool mirrored=glassMirrorActive&&camera.effects[2]==0;
    static std::vector<ThingLighting> lightingJobs;lightingJobs.clear();
    for(int i=0;i<numsectors;++i) for(mobj_t *thing=sectors[i].thinglist;thing;thing=thing->snext) {
        bool inFront=mirrored&&glassMirrorPlane[0]*units(thing->x)+glassMirrorPlane[1]*units(thing->y)>glassMirrorPlane[3];
        if((thing==players[displayplayer].mo&&!inFront)||thing->sprite<0||thing->sprite>=numsprites||
            (thing->frame&FF_FRAMEMASK)>=sprites[thing->sprite].numframes)continue;
        ThingLighting item{};item.thing=thing;item.sector=i;item.mirrored=inFront;
        interpolatedPosition(*thing,fraction,item.x,item.y,item.z);lightingJobs.push_back(item);
    }
    auto calculate=[&](int n){lightThing(lightingJobs[n]);};
    if(lightingJobs.size()>=32)frameParallelFor((int)lightingJobs.size(),calculate);
    else for(int n=0;n<(int)lightingJobs.size();++n)calculate(n);
    if(M_CheckParm((char*)"-verifylightthreads"))for(const auto &item:lightingJobs) {
        auto serial=item;lightThing(serial);
        if(serial.pulse!=item.pulse||serial.beam!=item.beam||serial.baked!=item.baked||
            memcmp(&serial.statics,&item.statics,sizeof(StaticSample))||
            memcmp(&serial.shadows,&item.shadows,sizeof(ShadowLights)))
            I_Error((char*)"Parallel sprite lighting differs from serial lighting");
    }
    for(const auto &item:lightingJobs)
        addThing(item.thing,item.sector,camera,fraction,beam,casters,item.mirrored?&mirror:nullptr,item);
    timing(7);profileAdd(profile.frame.spritesMs,spritesStart);
    std::sort(casters.begin(),casters.end(),[](const Caster &a,const Caster &b){return a.distance<b.distance;});
    for(size_t n=0;n<std::min(size_t(6),casters.size());++n) {
        const Caster &c=casters[n];
        flashlightSilhouette(*c.thing,*c.image,c.lump,c.flip,c.x,c.y,c.z,camera,*beam,c.card.key?&c.card:nullptr);
    }
    playerShadow(camera,fraction);
    timing(8);buildBlood(camera);
    buildHeat(camera);
    buildDust(camera);
    buildEmbers(camera);
    buildOutdoorWind(camera);
    buildRain(camera);
    buildBubbles(camera);timing(9);
    if(spike) {
        double sum=0;for(double t:current)sum+=t;
        if(sum>8)fprintf(stderr,"Geometry spike tic %d: total %.3f collect %.3f flatlights %.3f visibility %.3f flats %.3f walls %.3f surface %.3f glass %.3f actors %.3f shadows %.3f effects %.3f ms\n",gametic,sum,current[0],current[1],current[2],current[3],current[4],current[5],current[6],current[7],current[8],current[9]);
    }
    if(detailed&&++frames==128) {
        fprintf(stderr,"Geometry ms: collect %.3f flatsources %.3f visibility %.3f flats %.3f walls %.3f surface %.3f glass %.3f actors %.3f shadows %.3f effects %.3f\n",
            totals[0]/frames,totals[1]/frames,totals[2]/frames,totals[3]/frames,totals[4]/frames,totals[5]/frames,totals[6]/frames,totals[7]/frames,totals[8]/frames,totals[9]/frames);
        totals={};frames=0;
    }
}
} // namespace doom3d
