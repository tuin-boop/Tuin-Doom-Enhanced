/* Sunlight baked from the sky texture: sun visibility, sky openings and
   the sunbeams through them. */
#include "internal.h"

namespace doom3d {
bool sunBaked=false;
float sunLevel=0; // Typical sky-sector light; indoor sun patches rise toward it.
std::array<float,3> sunTint={1,1,1};
float sunAzimuth[2]={1,0}; // Horizontal direction toward the baked sun (sunElevation high).
float sunDirection[3]={0,0,1};

float sunYaw=0,sunGlow=0;
// The light's height above the horizon, and the drawn disc's (Game_Sun).
float sunElevation=40*doomPi/180,sunDiscElevation=40*doomPi/180,sunShade=0.3f,sunGain=0;
// Sky sectors joined by sky-to-sky lines form one opening (a hole may be
// several sectors, nested or side by side). Its outer edge decides how
// enclosed it is: indoor lines lead into non-sky sectors.
struct SkyOpening { float edge=0; std::vector<std::pair<const sector_t*,float>> indoor; };
static std::vector<int> skyOpeningOf;static std::vector<SkyOpening> skyOpenings;
std::vector<SunShaft> sunShafts;

std::vector<char> beamAir;
// Sun shadows, baked once per level into the contact map's blue channel and
// the wall atlas alpha. The sky texture's brightest columns face the sun
// (sky.frag maps column x to yaw -x*2pi/(4*width)) and also give its tint;
// the elevation is fixed at 40 degrees, or (Game_Sun fromSky) follows the
// height sky.frag shows their brightness at: painted on the horizon it
// lights from 35 degrees, painted 45 degrees up or more from 60. Lower
// suns left walled courtyards wholly in shade.
// Rays see the sector heights in bakeMap.
static float enclosure(const sector_t *sector,float *light=nullptr);
static void bakeShafts();
static void findSkyOpenings();
// A floor cell's sun visibility (0-255), or -1 for void and sky floors.
int sunCell(const BakeMap &map,size_t i,const float direction[3]) {
    int sector=seamCells[i].own;
    if(sector==noSector||sectors[sector].floorpic==skyflatnum)return -1;
    float px=mapOrigin[0]+(i%mapWidth+0.5f)*mapCell,py=mapOrigin[1]+(i/mapWidth+0.5f)*mapCell;
    return (int)std::lround(sunVisibility(map,sector,px,py,map.sectors[sector].floor+1,direction)*255);
}
// A wall strip's sun visibility into the atlas alpha. Walls fade with the
// angle to the sun so grazing walls fall into shade.
void sunStrip(const BakeMap &map,const BakeStrip &strip,const float direction[3]) {
    float length=std::hypot(direction[0],direction[1]),sunX=direction[0]/length,sunY=direction[1]/length;
    float facing=std::clamp((strip.dy*sunX-strip.dx*sunY)*1.5f,0.0f,1.0f);
    forStripTexels(strip,[&](size_t i,float x,float y,float z) {
        float visible=facing>0?facing*sunVisibility(map,strip.sector,x,y,z,direction):0;
        wallBakeCells[4*i+3]=(uint8_t)std::lround(visible*255);
    });
}
void bakeSun() {
    loadingStage(LoadStage::Sun);
    finishRelight();
    sunBaked=true;sunLevel=0;
    if(contactCells.empty())return;
    std::vector<float> skyLights;
    for(int i=0;i<numsectors;++i)if(sectors[i].ceilingpic==skyflatnum)skyLights.push_back(lighting(&sectors[i]));
    if(skyLights.empty()) {fprintf(stderr,"3D sun: no sky sectors.\n");return;}
    std::nth_element(skyLights.begin(),skyLights.begin()+skyLights.size()/2,skyLights.end());
    float level=skyLights[skyLights.size()/2];
    uint64_t start=SDL_GetTicksNS();
    const Image &sky=wallImage(skytexture);
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    // Fourth-power luminance favors highlights over large mid-tone areas.
    std::vector<double> columns(sky.width);
    auto weight=[&](int x,int y,const byte *&rgb) {
        size_t i=(size_t)y*sky.width+x;rgb=palette+sky.pixels[2*i]*3;
        if(!sky.pixels[2*i+1])return 0.0;
        double luma=(0.299*rgb[0]+0.587*rgb[1]+0.114*rgb[2])/255.0;
        return luma*luma*luma*luma;
    };
    for(int x=0;x<sky.width;++x)for(int y=0;y<sky.height;++y) {const byte *rgb;columns[x]+=weight(x,y,rgb);}
    int best=0;double bestScore=-1;
    for(int x=0;x<sky.width;++x) {
        double score=0;
        for(int d=-4;d<=4;++d)score+=columns[(x+d+sky.width)%sky.width];
        if(score>bestScore) {bestScore=score;best=x;}
    }
    double tint[3]={},total=0;
    for(int d=-4;d<=4;++d)for(int y=0;y<sky.height;++y) {
        const byte *rgb;double w=weight((best+d+sky.width)%sky.width,y,rgb);
        for(int c=0;c<3;++c)tint[c]+=rgb[c]/255.0*w;
        total+=w;
    }
    double peak=std::max({tint[0],tint[1],tint[2],1e-9});
    for(int c=0;c<3;++c)sunTint[c]=total>0?(float)(0.5+0.5*tint[c]/peak):1.0f;
    game_sun game;Game_Sun(&game);sunShade=game.shade;sunGain=game.gain;
    float elevation=40*doomPi/180,daylight=1;sunDiscElevation=elevation;
    if(game.fromSky) {
        // How bright the sky is on average: a grey day sky about 0.34, a
        // dim red one 0.14, night blue under 0.1. Dark skies shine weaker,
        // with softer shadows, in their own (paled) colour: moonlight.
        double sum=0,color[3]={};
        for(int y=0;y<sky.height;++y)for(int x=0;x<sky.width;++x) {
            size_t i=(size_t)y*sky.width+x;if(!sky.pixels[2*i+1])continue;
            const byte *rgb=palette+sky.pixels[2*i]*3;
            sum+=(0.299*rgb[0]+0.587*rgb[1]+0.114*rgb[2])/255.0;
            for(int c=0;c<3;++c)color[c]+=rgb[c];
        }
        daylight=std::clamp((float)(sum/std::max(1,sky.width*sky.height))-0.06f,0.0f,0.24f)/0.24f;
        sunShade*=0.6f+0.4f*daylight;sunGain*=0.35f+0.65f*daylight;
        double top=std::max({color[0],color[1],color[2],1e-9});
        for(int c=0;c<3;++c)sunTint[c]=sunTint[c]*daylight+(float)(0.6+0.4*color[c]/top)*(1-daylight);
    }
    if(game.fromSky&&total>0) {
        // sky.frag: texture row fraction v shows at rise (0.70-v)/0.68.
        double rows=0;
        for(int d=-4;d<=4;++d)for(int y=0;y<sky.height;++y) {const byte *rgb;rows+=weight((best+d+sky.width)%sky.width,y,rgb)*(y+0.5);}
        float painted=std::atan((0.70f-(float)(rows/total/sky.height))/0.68f);
        elevation=(35+25*std::clamp(painted/(45*doomPi/180),0.0f,1.0f))*doomPi/180;
        sunDiscElevation=std::max(painted,3*doomPi/180);
    }
    sunElevation=elevation;
    float yaw=-(best+0.5f)*2*doomPi/(4.0f*sky.width);
    float bright=(float)std::pow(total/(9.0*sky.height),0.25),glow=std::clamp((bright-0.2f)/0.4f,0.0f,1.0f);
    sunYaw=yaw;sunGlow=glow*glow*(3-2*glow)*daylight; // no sun in a night sky
    if(!levelMood().sun)sunGlow=0; // An episode with the sun hidden (Game_Mood).
    const float direction[3]={std::cos(yaw)*std::cos(elevation),std::sin(yaw)*std::cos(elevation),std::sin(elevation)};
    float sunLength=std::hypot(direction[0],direction[1]),sunX=direction[0]/sunLength,sunY=direction[1]/sunLength;
    sunAzimuth[0]=sunX;sunAzimuth[1]=sunY;
    std::atomic<size_t> lit{0};
    parallelFor(mapHeight,[&](int y) {
        for(int x=0;x<mapWidth;++x) {
            size_t i=(size_t)y*mapWidth+x;
            int visible=sunCell(bakeMap,i,direction);
            if(visible<0)continue;
            contactCells[4*i+2]=(uint8_t)visible;
            if(visible>0)++lit;
        }
    });
    parallelFor((int)bakeOrder.size(),[&](int n) {sunStrip(bakeMap,bakeStrips[bakeOrder[n]],direction);});
    contactTexture=gpuCreateTexture(GpuFormat::RGBA8,mapWidth,mapHeight,contactCells.data(),mapWidth*4);
    if(!contactTexture)I_Error((char*)"Could not allocate surface maps");
    uploadWallBake();
    for(int c=0;c<3;++c)sunDirection[c]=direction[c];
    // Hidden by the episode's mood: no sunlight, only its direction (which
    // way the rain blows) is kept.
    if(levelMood().sun)bakeShafts();else sunShafts.clear();
    sunLevel=levelMood().sun?level:0;
    fprintf(stderr,"3D sun: yaw %.0f degrees, %.0f up, %zu sunlit cells, %zu sunbeams, %zu wall strips in %dx%d, baked in %.0f ms.\n",
        yaw*180/doomPi,elevation*180/doomPi,lit.load(),sunShafts.size(),bakeOrder.size(),atlasWidth,atlasHeight,(SDL_GetTicksNS()-start)/1e6);
}
// Sunbeams: about one per 24 units of sunlit floor in beam air (beamAir),
// jittered within its block so the beams never line up in rows. Each runs
// from the floor back toward the sun, sampled on the map grid, while it stays
// in beam air; it ends a little past the point where it leaves through a sky
// ceiling or a window into open sky.
// Beams belong to dusty air lit through a narrow gap, so a beam fades where
// the light floods in instead: where the opening onto open ground it leaves
// through (a window, or the edge of a cave mouth or overhang) is wider than
// 128 units, gone at 320.
static void bakeShafts() {
    sunShafts.clear();
    if(skyOpeningOf.size()!=(size_t)numsectors)findSkyOpenings();
    std::vector<double> openingArea(skyOpenings.size());
    for(size_t i=0;i<seamCells.size();++i) {
        int at=seamCells[i].own;
        if(at!=noSector&&skyOpeningOf[at]>=0)openingArea[skyOpeningOf[at]]+=mapCell*mapCell;
    }
    std::vector<char> &indoor=beamAir;
    indoor.assign(numsectors,0);
    for(int i=0;i<numsectors;++i)
        indoor[i]=sectors[i].ceilingpic!=skyflatnum||(enclosure(&sectors[i])>0.5f&&openingArea[skyOpeningOf[i]]<=400*400);
    // Openings onto open ground: lines with beam air on one side and open sky
    // on the other, through a gap at least 24 high, joined into chains where
    // they share a vertex. A chain's length is how wide the opening is.
    auto opensOut=[&](const line_t &line) {
        if(!line.frontsector||!line.backsector)return false;
        int f=(int)(line.frontsector-sectors),k=(int)(line.backsector-sectors);
        if(indoor[f]==indoor[k]||!bakeMap.sectors[indoor[f]?k:f].sky)return false;
        return std::min(bakeMap.sectors[f].ceiling,bakeMap.sectors[k].ceiling)-std::max(bakeMap.sectors[f].floor,bakeMap.sectors[k].floor)>=24;
    };
    std::vector<int> root(numvertexes);
    for(int v=0;v<numvertexes;++v)root[v]=v;
    auto chainOf=[&](int v) {while(root[v]!=v)v=root[v]=root[root[v]];return v;};
    for(int i=0;i<numlines;++i)if(opensOut(lines[i]))root[chainOf((int)(lines[i].v1-vertexes))]=chainOf((int)(lines[i].v2-vertexes));
    std::vector<float> chainLength(numvertexes);
    for(int i=0;i<numlines;++i)if(opensOut(lines[i]))chainLength[chainOf((int)(lines[i].v1-vertexes))]+=std::hypot(units(lines[i].dx),units(lines[i].dy));
    // The width of the opening a beam leaves through from sector last into
    // open sector out near (x,y): the chain of the nearest line between them.
    auto openingWidth=[&](int last,int out,float x,float y) {
        float best=INFINITY,width=0;
        const sector_t &from=sectors[last];
        for(int i=0;i<from.linecount;++i) {
            const line_t &line=*from.lines[i];
            if(!opensOut(line)||(line.frontsector-sectors!=out&&line.backsector-sectors!=out))continue;
            float ax=units(line.v1->x),ay=units(line.v1->y),dx=units(line.dx),dy=units(line.dy);
            float u=std::clamp(((x-ax)*dx+(y-ay)*dy)/std::max(dx*dx+dy*dy,1e-6f),0.0f,1.0f);
            float distance=std::hypot(ax+dx*u-x,ay+dy*u-y);
            if(distance<best) {best=distance;width=chainLength[chainOf((int)(line.v1-vertexes))];}
        }
        return width;
    };
    int block=std::max(1,(int)std::lround(24/mapCell));
    for(int by=0;by<mapHeight;by+=block)for(int bx=0;bx<mapWidth;bx+=block) {
        int x=std::min(mapWidth-1,bx+(int)(hashUnit(bx,by,0x5a17u)*block)),y=std::min(mapHeight-1,by+(int)(hashUnit(bx,by,0x5a18u)*block));
        size_t i=(size_t)y*mapWidth+x;int sector=seamCells[i].own;
        if(sector==noSector||!indoor[sector]||sectors[sector].floorpic==skyflatnum)continue;
        float visible=contactCells[4*i+2]/255.0f;
        if(visible<0.5f)continue;
        float px=mapOrigin[0]+(x+0.5f)*mapCell,py=mapOrigin[1]+(y+0.5f)*mapCell,pz=bakeMap.sectors[sector].floor;
        float length=0,width=0;bool left=false;int last=sector;
        for(float t=4;t<1024;t+=4) {
            float qx=px+sunDirection[0]*t,qy=py+sunDirection[1]*t,qz=pz+sunDirection[2]*t;
            int cx=(int)std::floor((qx-mapOrigin[0])/mapCell),cy=(int)std::floor((qy-mapOrigin[1])/mapCell);
            if(cx<0||cy<0||cx>=mapWidth||cy>=mapHeight)break;
            int at=seamCells[(size_t)cy*mapWidth+cx].own;
            if(at==noSector||qz<bakeMap.sectors[at].floor)break;
            if(!indoor[at]||qz>bakeMap.sectors[at].ceiling) {
                left=bakeMap.sectors[at].sky;
                if(!indoor[at])width=openingWidth(last,at,qx,qy);
                break;
            }
            length=t;last=at;
        }
        if(!left||length<24)continue;
        float wide=std::clamp((width-128)/192,0.0f,1.0f),contrast=1-wide*wide*(3-2*wide);
        if(contrast<0.05f)continue;
        sunShafts.push_back({px,py,pz,length+24,visible*contrast,sector});
    }
}
static void findSkyOpenings() {
    skyOpeningOf.assign(numsectors,-1);skyOpenings.clear();
    for(int first=0;first<numsectors;++first) {
        if(sectors[first].ceilingpic!=skyflatnum||skyOpeningOf[first]>=0)continue;
        int id=(int)skyOpenings.size();skyOpenings.emplace_back();
        std::vector<int> stack={first};skyOpeningOf[first]=id;
        while(!stack.empty()) {
            const sector_t *sector=&sectors[stack.back()];stack.pop_back();
            for(int i=0;i<sector->linecount;++i) {
                const line_t *line=sector->lines[i];
                const sector_t *other=line->frontsector==sector?line->backsector:line->frontsector;
                if(other==sector)continue;
                if(other&&other->ceilingpic==skyflatnum) {
                    int n=(int)(other-sectors);
                    if(skyOpeningOf[n]<0) {skyOpeningOf[n]=id;stack.push_back(n);}
                    continue;
                }
                float length=std::hypot(units(line->dx),units(line->dy));
                skyOpenings[id].edge+=length;
                // Only a real gap counts (closed doors and ledges stay walls), and
                // only under a short rim: a ceiling hole sits just above the room's
                // ceiling, while cliffs around a canyon rise far above theirs.
                if(!other||std::min(ceilZ(sector),ceilZ(other))-std::max(floorZ(sector),floorZ(other))<24)continue;
                float rim=std::clamp((128-(ceilZ(sector)-ceilZ(other)))/64,0.0f,1.0f);
                if(rim>0)skyOpenings[id].indoor.push_back({other,length*rim});
            }
        }
    }
}
// How enclosed a sky sector's opening is: 0 for open air up to 1 where most
// of its edge opens into indoor sectors, as a hole in a ceiling does. light
// receives those sectors' current light, weighted by edge length.
static float enclosure(const sector_t *sector,float *light) {
    if(skyOpeningOf.size()!=(size_t)numsectors)findSkyOpenings();
    if(light)*light=0;
    int id=skyOpeningOf[sector-sectors];if(id<0)return 0;
    const SkyOpening &opening=skyOpenings[id];
    float open=0,sum=0;
    for(const auto &[other,length]:opening.indoor) {open+=length;sum+=length*lighting(other);}
    if(open<=0||opening.edge<=0)return 0;
    if(light)*light=sum/open;
    float t=std::clamp((open/opening.edge-0.3f)/0.3f,0.0f,1.0f);
    return t*t*(3-2*t);
}
// Sky sectors pass their enclosure and outdoor floor to the shader packed in
// sectorInfo w: 1 + round(enclosure*255) + round(floor*255)/256 (0 indoors).
// The floor is the lowest light sun shadow and sky occlusion may take the
// sector to: the light of the indoor sectors its opening leads into, by its
// enclosure. A hole in a bright room's ceiling then never shades below the
// room, while a courtyard with a door or two keeps its shadows.
float packOutdoor(const sector_t *sector,float *enclosed,float *floor) {
    float light,e=enclosure(sector,&light);
    if(enclosed)*enclosed=e;
    if(floor)*floor=light*e;
    return 1+std::round(e*255)+std::round(light*e*255)/256;
}
// CPU twin of the shader's sunLight on floors, for sprites and the weapon.
// Under a sky, shadow drops light steps outdoors while an enclosed opening
// lights like indoors, blended by its enclosure.
float sunLightAt(float x,float y,const sector_t *sector,float light) {
    if(!settings.sun||sunLevel<=0||contactCells.empty())return light;
    int cx=std::clamp((int)std::floor((x-mapOrigin[0])/mapCell),0,mapWidth-1);
    int cy=std::clamp((int)std::floor((y-mapOrigin[1])/mapCell),0,mapHeight-1);
    float visible=contactCells[4*((size_t)cy*mapWidth+cx)+2]/255.0f;
    float lifted=light+std::round(std::max(0.0f,sunLevel-light)*visible*16)/16;
    if(sector->ceilingpic!=skyflatnum)return lifted;
    float enclosed,floor;packOutdoor(sector,&enclosed,&floor);
    float shaded=light-std::round(light*sunShade*(1-visible)*16)/16+std::round(light*sunGain*visible*16)/16;
    return std::max(std::round((shaded+(lifted-shaded)*enclosed)*16)/16,std::min(light,floor));
}
// CPU twin of seamLight at the nearest cell, for sprites and the weapon.
float seamLightAt(float x,float y,const sector_t *sector,float fallback) {
    if(!settings.softLight||seamCells.empty())return fallback;
    int cx=(int)std::floor((x-mapOrigin[0])/mapCell),cy=(int)std::floor((y-mapOrigin[1])/mapCell);
    if(cx<0||cy<0||cx>=mapWidth||cy>=mapHeight)return fallback;
    const auto &cell=seamCells[(size_t)cy*mapWidth+cx];
    uint16_t own=(uint16_t)(sector-sectors);
    if(cell.own!=own||cell.neighbor==noSector)return fallback;
    return fallback+(lighting(&sectors[cell.neighbor])-fallback)*(cell.weight&255)/510.0f;
}
void sunNewLevel() {sunBaked=false;skyOpeningOf.clear();skyOpenings.clear();sunShafts.clear();beamAir.clear();}
void sunShutdown() {sunNewLevel();sunLevel=0;}
} // namespace doom3d
