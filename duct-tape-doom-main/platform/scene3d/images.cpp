/* Textures from the WAD: upload with palette mips (sprites with their normals), bump
   relief, emissive masks and monitor glass; average and brightest colors. */
#include "internal.h"

namespace doom3d {
std::vector<unsigned> paletteLUTColors(const unsigned *palette) {
    std::vector<unsigned> colors(32*32*32);
    for(int b=0;b<32;++b)for(int g=0;g<32;++g)for(int r=0;r<32;++r) {
        int red=(r*255+15)/31,green=(g*255+15)/31,blue=(b*255+15)/31,best=0,bestDistance=INT_MAX;
        for(int i=0;i<256;++i) {
            int dr=red-int((palette[i]>>16)&255),dg=green-int((palette[i]>>8)&255),db=blue-int(palette[i]&255);
            int distance=dr*dr+dg*dg+db*db;
            if(distance<bestDistance) {bestDistance=distance;best=i;}
        }
        colors[(b*32+g)*32+r]=palette[best];
    }
    return colors;
}
void buildCloudTexture() {
    // Periodic, deterministic low-frequency cloud texture; no downloaded assets.
    std::array<byte,64*64> pixels;
    for(int y=0;y<64;++y)for(int x=0;x<64;++x) {
        float u=x*doomPi/32,v=y*doomPi/32;
        float cloud=0.5f+0.19f*sin(u+sin(v))+0.16f*cos(v*2-u)+0.10f*sin(u*3+v*2);
        pixels[y*64+x]=(byte)(std::clamp(cloud,0.0f,1.0f)*255);
    }
    cloudTexture=gpuCreateTexture(GpuFormat::R8,64,64,pixels.data(),64);
    if(!cloudTexture)I_Error((char*)"Could not allocate cloud texture");
}
// Palette mip levels for walls and flats, as software Quake built them: each
// level texel averages the colors it covers (weighted by coverage) and snaps
// back to the nearest PLAYPAL entry, so the shader keeps applying the live
// palette (damage and pickup flashes). Coverage and the emissive mask
// average; monitor glass takes the middle texel. Returns every level after
// the first, packed at the texture's channel count, and counts all levels.
static std::vector<byte> paletteMips(int w,int h,const byte *base,int channels,int &levels) {
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    // Nearest entries by 5-bit color cell, filled as cells are first used.
    static std::array<byte,768> cachedPalette={};
    static std::vector<short> nearest;
    if(nearest.empty()||memcmp(cachedPalette.data(),palette,768)) {
        memcpy(cachedPalette.data(),palette,768);nearest.assign(32*32*32,-1);
    }
    auto snap=[&](float r,float g,float b) {
        int cell=(std::min(31,(int)(r/8))<<10)|(std::min(31,(int)(g/8))<<5)|std::min(31,(int)(b/8));
        if(nearest[cell]<0) {
            float cr=(cell>>10)*8+4.0f,cg=((cell>>5)&31)*8+4.0f,cb=(cell&31)*8+4.0f,best=1e30f;
            for(int i=0;i<256;++i) {
                float dr=palette[i*3]-cr,dg=palette[i*3+1]-cg,db=palette[i*3+2]-cb;
                float d=2*dr*dr+4*dg*dg+3*db*db;
                if(d<best) {best=d;nearest[cell]=(short)i;}
            }
        }
        return (byte)nearest[cell];
    };
    std::vector<byte> out;levels=1;
    for(int lw=w,lh=h;lw>1||lh>1;++levels) {
        lw=std::max(1,lw/2);lh=std::max(1,lh/2);
        for(int y=0;y<lh;++y) for(int x=0;x<lw;++x) {
            int x0=x*w/lw,x1=std::max(x0+1,(x+1)*w/lw),y0=y*h/lh,y1=std::max(y0+1,(y+1)*h/lh);
            double rgb[3]={},coverage=0,mask=0;int count=0;
            for(int sy=y0;sy<y1;++sy) for(int sx=x0;sx<x1;++sx) {
                const byte *p=base+((size_t)sy*w+sx)*channels;
                for(int c=0;c<3;++c)rgb[c]+=palette[p[0]*3+c]*(double)p[1];
                coverage+=p[1];if(channels==4)mask+=p[2];++count;
            }
            const byte *middle=base+((size_t)((y0+y1)/2)*w+(x0+x1)/2)*channels;
            out.push_back(coverage>0?snap(float(rgb[0]/coverage),float(rgb[1]/coverage),float(rgb[2]/coverage)):middle[0]);
            out.push_back((byte)std::lround(coverage/count));
            if(channels==4) {out.push_back((byte)std::lround(mask/count));out.push_back(middle[3]);}
        }
    }
    return out;
}
// Channels: palette index, coverage, then the emissive mask (or sprite
// normal x) and, on walls and flats, monitor glass height (or sprite normal y).
// mipmaps: walls and flats also get palette mip levels.
Image upload(int w,int h,const byte *pixels,int left,int top,const byte *mask,const byte *normals,const byte *glass,bool mipmaps) {
    Image image; image.pixels.assign(pixels,pixels+(size_t)w*h*2); image.width=w; image.height=h; image.left=left; image.top=top;
    for(size_t i=0;i<(size_t)w*h;++i)if(pixels[2*i+1]!=255) {image.opaque=false;break;}
    std::vector<byte> packed;
    bool wide=mask||normals||glass;
    if(wide) {
        packed.resize((size_t)w*h*4);
        for(size_t i=0;i<(size_t)w*h;++i) {
            packed[4*i]=pixels[2*i];packed[4*i+1]=pixels[2*i+1];
            packed[4*i+2]=normals?normals[2*i]:mask?mask[i]:0;
            packed[4*i+3]=normals?normals[2*i+1]:glass?glass[i]:DOOM_GLASS_NONE;
        }
    }
    if(glass)image.screens.assign(glass,glass+(size_t)w*h); // Monitor glass, for shots (shot_lights.cpp).
    if(mask) {
        int lump=W_GetNumForName((char*)"PLAYPAL");
        if(W_LumpLength(lump)<768) I_Error((char*)"Invalid emissive palette");
        const byte *palette=(const byte*)W_CacheLumpNum(lump,PU_CACHE);
        double weight=0;
        for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
            size_t i=(size_t)y*w+x;float e=mask[i]/255.0f;
            weight+=e;image.emissionU+=(x+0.5f)*e;image.emissionV+=(y+0.5f)*e;
            for(int c=0;c<3;++c) image.emissionColor[c]+=palette[pixels[2*i]*3+c]/255.0f*e;
        }
        if(weight>0) {
            image.emission.assign(mask,mask+(size_t)w*h); // Where shots break the glow (shot_lights.cpp).
            image.emissionU/=weight;image.emissionV/=weight;
            float peak=*std::max_element(image.emissionColor.begin(),image.emissionColor.end());
            for(float &c:image.emissionColor)c=std::max(0.04f,c/std::max(peak,0.001f));
            image.emissionCoverage=(float)weight/(w*h);
            image.emissionWeight=std::clamp(image.emissionCoverage*8.0f,0.25f,1.0f);
        }
    }
    int channels=wide?4:2,levels=1;
    const byte *levelData=wide?packed.data():pixels;
    std::vector<byte> chain;
    if(mipmaps) {
        std::vector<byte> mips=paletteMips(w,h,levelData,channels,levels);
        chain.assign(levelData,levelData+(size_t)w*h*channels);chain.insert(chain.end(),mips.begin(),mips.end());
        levelData=chain.data();
    }
    image.texture=gpuCreateTexture(wide?GpuFormat::RGBA8:GpuFormat::RG8,w,h,levelData,w*channels,levels);
    if(!image.texture) I_Error((char*)"Could not allocate a GPU texture");
    return image;
}
// Screen glass heights, or empty when the texture has no screens; frame
// receives where each glass pixel sits on its screen (doom_screen_glass),
// and each readout texel where it sits on its readout (doom_screen_readouts;
// empty when there are neither). Switches keep their lamps still, so the
// lit state stays plain.
static std::vector<byte> screenGlass(const char *name,bool flat,int w,int h,const byte *pixels,std::vector<byte> &frame) {
    frame.clear();
    if(!Game_ScreenTexture(name,flat))return {};
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    std::vector<byte> glass((size_t)w*h);frame.assign((size_t)w*h*4,0);
    int screens=doom_screen_glass(w,h,pixels,palette,glass.data(),frame.data());
    bool switches=!strncmp(name,"SW1",3)||!strncmp(name,"SW2",3);
    int readouts=switches?0:doom_screen_readouts(w,h,pixels,palette,glass.data(),frame.data());
    if(!readouts&&!screens)frame.clear();
    if(!screens)return {};
    return glass;
}
// Uploads the screen frame beside the image the shader refracts through it.
static Image &withGlassFrame(Image &image,const std::vector<byte> &frame) {
    if(frame.empty())return image;
    image.glassFrame=gpuCreateTexture(GpuFormat::RGBA8,image.width,image.height,frame.data(),image.width*4);
    if(!image.glassFrame)I_Error((char*)"Could not allocate a GPU texture");
    return image;
}
// Bump relief for walls and flats (Image::relief), from the artwork's log
// luminance, so a dark texture's seams count as much as a bright one's (in
// CEIL3_5 they are a single palette step). R/G: the slope along u and v, 128
// level and 255 just under 45 degrees; B: how grainy the texels around are,
// for varied highlights. Seams and mortar get real depth: the height is
// averaged 11 texels along each axis before taking its slope across, so long
// straight bands stand out while blotches a few texels wide wash out instead
// of turning into craters. A weak per-texel Sobel adds only fine pitting.
// Repeats like the texture. tiles: finds the painted tiles of a wall or flat
// (painted_tiles.h) so each gets a crooked lie; the relief is then stored at
// 16 bits with the anchor offset in alpha (noPaintedTile: none), the other
// channels as at 8 bits.
using Tiler=std::vector<uint16_t>(*)(const std::vector<float> &,int,int);
// depth scales the broad bands (seams, streaks); smooth leaves no grain
// (glassy highlights) and halves the per-texel tilt.
static Image &withRelief(Image &image,Tiler tiles=nullptr,float depth=1,bool smooth=false) {
    int w=image.width,h=image.height;size_t count=(size_t)w*h;
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    std::vector<float> height(count),across(count),down(count),broad(count),grain(count),rough(count),scratch(count);
    for(size_t i=0;i<count;++i) {
        const byte *rgb=palette+image.pixels[2*i]*3;
        height[i]=std::log((0.299f*rgb[0]+0.587f*rgb[1]+0.114f*rgb[2])/255.0f+0.03f);
    }
    auto at=[&](const std::vector<float> &v,int x,int y) {return v[(size_t)((y%h+h)%h)*w+(x%w+w)%w];};
    // Separable filter: taps along x into scratch, then along y into to.
    auto filter=[&](const std::vector<float> &from,std::vector<float> &to,const std::vector<float> &alongX,const std::vector<float> &alongY) {
        int rx=(int)alongX.size()/2,ry=(int)alongY.size()/2;
        for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
            float sum=0;for(int k=-rx;k<=rx;++k)sum+=alongX[k+rx]*at(from,x+k,y);
            scratch[(size_t)y*w+x]=sum;
        }
        for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
            float sum=0;for(int k=-ry;k<=ry;++k)sum+=alongY[k+ry]*at(scratch,x,y+k);
            to[(size_t)y*w+x]=sum;
        }
    };
    const std::vector<float> line(11,1/11.0f),tent={0.25f,0.5f,0.25f},binomial={1/16.0f,4/16.0f,6/16.0f,4/16.0f,1/16.0f};
    filter(height,across,tent,line); // Vertical bands, for the slope along u.
    filter(height,down,line,tent);   // Horizontal bands, for the slope along v.
    filter(height,broad,binomial,binomial);
    for(size_t i=0;i<count;++i)grain[i]=std::fabs(height[i]-broad[i]);
    filter(grain,rough,binomial,binomial);
    // Faint seams: some artwork paints its seams only a few palette steps
    // off the surface (FLAT18, COMPSPAN), so they barely tilt. Where the
    // strongest bands (99th percentile) clearly stand out from the typical
    // texel, they are deepened toward seamDepth; grain and organic artwork
    // without straight seams keep their relief. Only bands well above the
    // typical one deepen, so averaged speckle inside a slab stays shallow
    // instead of turning into streaks.
    std::vector<float> bands(count);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)
        bands[(size_t)y*w+x]=std::hypot(at(across,x+1,y)-at(across,x-1,y),at(down,x,y+1)-at(down,x,y-1))*1.5f;
    std::vector<float> ranked=bands;
    std::nth_element(ranked.begin(),ranked.begin()+count/2,ranked.end());float typical=ranked[count/2];
    std::nth_element(ranked.begin(),ranked.begin()+count*99/100,ranked.end());float strongest=ranked[count*99/100];
    const float seamDepth=0.35f,deepest=3.0f,standout=4.0f;
    float gain=strongest>0&&strongest>=standout*typical?std::clamp(seamDepth/strongest,1.0f,deepest):1.0f;
    std::vector<byte> cells(count*4);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        float fx=(at(height,x+1,y-1)+2*at(height,x+1,y)+at(height,x+1,y+1)-at(height,x-1,y-1)-2*at(height,x-1,y)-at(height,x-1,y+1))*0.25f;
        float fy=(at(height,x-1,y+1)+2*at(height,x,y+1)+at(height,x+1,y+1)-at(height,x-1,y-1)-2*at(height,x,y-1)-at(height,x+1,y-1))*0.25f;
        float t=std::clamp((bands[(size_t)y*w+x]-2*typical)/(2*typical+1e-6f),0.0f,1.0f);
        float deepen=1+(gain-1)*t*t*(3-2*t);
        float fine=smooth?0.125f:0.25f;
        float sx=fine*fx+1.5f*deepen*depth*(at(across,x+1,y)-at(across,x-1,y));
        float sy=fine*fy+1.5f*deepen*depth*(at(down,x,y+1)-at(down,x,y-1));
        float soften=1/(1+std::sqrt(sx*sx+sy*sy)); // Steep edges ease toward 45 degrees.
        byte *cell=&cells[((size_t)y*w+x)*4];
        cell[0]=(byte)std::lround(128+127*sx*soften);cell[1]=(byte)std::lround(128+127*sy*soften);
        cell[2]=smooth?0:(byte)std::lround(std::clamp(rough[(size_t)y*w+x]/0.25f,0.0f,1.0f)*255);cell[3]=255;
    }
    std::vector<uint16_t> anchors=tiles?tiles(height,w,h):std::vector<uint16_t>();
    if(std::any_of(anchors.begin(),anchors.end(),[](uint16_t a){return a!=noPaintedTile;})) {
        std::vector<uint16_t> wide(count*4);
        for(size_t i=0;i<count;++i) {
            for(int c=0;c<3;++c)wide[i*4+c]=(uint16_t)(cells[i*4+c]*257);
            wide[i*4+3]=anchors[i];
        }
        image.relief=gpuCreateTexture(GpuFormat::RGBA16Uint,w,h,wide.data(),w*8);
    } else image.relief=gpuCreateTexture(GpuFormat::RGBA8,w,h,cells.data(),w*4);
    if(!image.relief)I_Error((char*)"Could not allocate a GPU texture");
    return image;
}
// Leaded stained glass (Image::leaded): its relief in place of withRelief's.
// The glowing panes are the shards, split by the dark lead between them
// (4-connected groups of glowing texels): each shard is flat glass leaning a
// little its own way, by a hash of its first texel, so a light glints off
// some shards and not their neighbors. The lead is a groove: its texels
// slope down from the panes on either side, so its edges catch the light.
// RGBA8 as withRelief's (no tiles); B (grain) 0: glass is smooth.
static Image &withLeadedRelief(Image &image) {
    int w=image.width,h=image.height;size_t count=(size_t)w*h;
    const std::vector<byte> &mask=image.emission;
    std::vector<int> shard(count,-1);std::vector<size_t> stack;
    auto pane=[&](int x,int y){x=(x%w+w)%w;y=(y%h+h)%h;return !mask.empty()&&mask[(size_t)y*w+x]>0;};
    for(size_t start=0;start<count;++start) {
        if(shard[start]>=0||mask.empty()||!mask[start])continue;
        shard[start]=(int)start;stack.assign(1,start);
        while(!stack.empty()) {
            size_t i=stack.back();stack.pop_back();int x=(int)(i%w),y=(int)(i/w);
            const int steps[4][2]={{1,0},{-1,0},{0,1},{0,-1}};
            for(const auto &s:steps) {
                int nx=x+s[0],ny=y+s[1];if(nx<0||ny<0||nx>=w||ny>=h)continue;
                size_t j=(size_t)ny*w+nx;
                if(shard[j]<0&&mask[j]) {shard[j]=(int)start;stack.push_back(j);}
            }
        }
    }
    std::vector<byte> cells(count*4);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        size_t i=(size_t)y*w+x;float sx=0,sy=0;
        if(shard[i]>=0) {
            uint32_t hash=(uint32_t)shard[i]*2654435761u;hash^=hash>>13;hash*=0x5bd1e995u;hash^=hash>>15;
            sx=((hash&1023)/1023.0f*2-1)*0.18f;sy=(((hash>>10)&1023)/1023.0f*2-1)*0.18f;
        } else {
            // Down from the panes: the slope points away from them.
            sx=0.45f*((float)pane(x-1,y)-(float)pane(x+1,y));sy=0.45f*((float)pane(x,y-1)-(float)pane(x,y+1));
        }
        byte *cell=&cells[i*4];
        cell[0]=(byte)std::lround(128+127*std::clamp(sx,-1.0f,1.0f));cell[1]=(byte)std::lround(128+127*std::clamp(sy,-1.0f,1.0f));
        cell[2]=0;cell[3]=255;
    }
    image.relief=gpuCreateTexture(GpuFormat::RGBA8,w,h,cells.data(),w*4);
    if(!image.relief)I_Error((char*)"Could not allocate a GPU texture");
    return image;
}
// Masks live beside graphics.cfg, or in the directory selected by -emissive.
static std::vector<byte> emissionMask(const char *name,bool flat,int w,int h,const byte *pixels) {
    std::vector<byte> mask((size_t)w*h,0);
    int paletteLump=W_GetNumForName((char*)"PLAYPAL");
    if(W_LumpLength(paletteLump)<768) I_Error((char*)"Invalid emissive palette");
    const byte *palette=(const byte*)W_CacheLumpNum(paletteLump,PU_CACHE);
    int kind=Game_GlowKind(name,flat);
    for(size_t i=0;i<mask.size();++i)
        mask[i]=doom_emissive_pixel(kind,palette+pixels[2*i]*3,pixels[2*i+1]);
    if(!flat && doom_switch_texture(name)) {
        char companion[9];strcpy(companion,name);
        companion[2]=name[2]=='1'?'2':'1';
        int other=R_CheckTextureNumForName(companion),ow=0,oh=0;
        if(other>=0)R_TextureDimensions(other,&ow,&oh);
        if(ow==w && oh==h) {
            std::vector<byte> paired((size_t)w*h*2);
            R_CopyIndexedTexture(other,paired.data());
            for(size_t i=0;i<mask.size();++i)
                mask[i]=pixels[2*i+1]&&paired[2*i+1]?
                    doom_switch_pixel(palette+pixels[2*i]*3,palette+paired[2*i]*3):0;
        }
    }
    std::string root=graphicsConfig;
    size_t slash=root.find_last_of("/\\");
    root=slash==std::string::npos?"Emissive":root.substr(0,slash+1)+"Emissive";
    int option=M_CheckParm((char*)"-emissive");
    if(option && option+1<myargc) root=myargv[option+1];
    while(root.size()>1&&(root.back()=='/'||root.back()=='\\'))root.pop_back();
    std::string file=root+(flat?"/flats/":"/walls/")+name+".png";
    if(FILE *probe=fopen(file.c_str(),"rb")) {
        fclose(probe);
        std::vector<float> values;
        if(!platformReadMask(file.c_str(),w,h,values)) {
            fprintf(stderr,"Emissive mask must be %dx%d: %s\n",w,h,file.c_str());
        } else {
            for(size_t i=0;i<mask.size();++i)
                mask[i]=pixels[i*2+1]?(byte)std::clamp(values[i]*255.0f,0.0f,255.0f):0;
            fprintf(stderr,"Loaded emissive mask: %s\n",file.c_str());
        }
    }
    return mask;
}
// Sprite lumps a living monster shows in its ordinary (not fullbright)
// frames: those its spawn, chase, pain and attack states lead through, read
// from the state table so DEHACKED monsters count. Death frames never get eyes.
static bool livingMonsterLump(int lump) {
    static std::vector<char> living;
    if(living.empty()) {
        living.assign(numlumps,0);
        for(const mobjinfo_t &info:mobjinfo) {
            if(!(info.flags&MF_COUNTKILL))continue;
            for(int start:{info.spawnstate,info.seestate,info.painstate,info.meleestate,info.missilestate})
                for(int s=start,steps=0;s>0&&s<NUMSTATES&&steps<64;s=states[s].nextstate,++steps) {
                    const state_t &state=states[s];int frame=state.frame&FF_FRAMEMASK;
                    if((state.frame&FF_FULLBRIGHT)||state.sprite<0||state.sprite>=numsprites||frame>=sprites[state.sprite].numframes)continue;
                    for(short rotation:sprites[state.sprite].spriteframes[frame].lump)
                        if(rotation>=0&&firstspritelump+rotation<numlumps)living[firstspritelump+rotation]=1;
                }
        }
    }
    return lump>=0&&lump<(int)living.size()&&living[lump];
}
// The 0.25 weight floor lets small fixtures (key-door strips, console
// screens) light their rooms. Computer and tech walls merely dotted with
// LEDs (TEKWALL*, COMPTALL, COMPWERD: under 0.6% glows) keep their glowing
// artwork but light only as much as those LEDs; with the floor E1M2's
// TEKWALL4 room went red from its few red LEDs. Stained glass glows over
// most of its area but is a window, not a lamp: it lights the room softly,
// most of its color reaching the floor as projected light (glass_light.cpp).
static Image &speckledPanel(Image &image,const char *name,bool flat) {
    int kind=Game_GlowKind(name,flat);
    if(image.emissionCoverage>0&&image.emissionCoverage<0.006f&&kind==DOOM_EMISSIVE_PANEL)
        image.emissionWeight=image.emissionCoverage*8.0f;
    if(kind==DOOM_EMISSIVE_GLASS) {image.emissionWeight*=0.2f;image.leaded=!image.emission.empty();}
    return image;
}
/* Texture keys: wall index >=0, WAD lump = -1-lump. */
Image &wallImage(int index) {
    auto found=images.find(index); if(found!=images.end()) return found->second;
    loadingTick();
    int w,h; R_TextureDimensions(index,&w,&h);
    if(w<1||h<1||w>8192||h>8192) I_Error((char*)"Unsupported texture dimensions");
    std::vector<byte> pixels((size_t)w*h*2); R_CopyIndexedTexture(index,pixels.data());
    char name[9]; R_TextureName(index,name);
    auto mask=emissionMask(name,false,w,h,pixels.data());
    bool glowing=std::any_of(mask.begin(),mask.end(),[](byte p){return p!=0;});
    std::vector<byte> frame;auto glass=screenGlass(name,false,w,h,pixels.data(),frame);
    // Masked walls (bars, fences) show what lies behind instead of tiles;
    // animated ones would change tiles every frame.
    int next;bool animated=P_PicAnimationNext(true,index,&next)>0;
    Image &image=speckledPanel(images.emplace(index,upload(w,h,pixels.data(),0,0,glowing?mask.data():nullptr,nullptr,
        glass.empty()?nullptr:glass.data(),true)).first->second,name,false);
    image.switchIndicator=doom_switch_texture(name);
    if(image.leaded)return withLeadedRelief(withGlassFrame(image,frame));
    return withRelief(withGlassFrame(image,frame),image.opaque&&!animated?wallTiles:nullptr);
}
Image &lumpImage(int lump,bool flat) {
    int key=-1-lump; auto found=images.find(key); if(found!=images.end()) return found->second;
    loadingTick();
    int size=W_LumpLength(lump);
    const byte *data=(const byte*)W_CacheLumpNum(lump,PU_CACHE);
    if(flat) {
        if(size<4096) I_Error((char*)"Invalid floor texture");
        std::vector<byte> pixels(64*64*2);
        for(int i=0;i<4096;++i) {pixels[2*i]=data[i];pixels[2*i+1]=255;}
        char name[9];memcpy(name,lumpinfo[lump].name,8);name[8]=0;
        auto mask=emissionMask(name,true,64,64,pixels.data());
        bool glowing=std::any_of(mask.begin(),mask.end(),[](byte p){return p!=0;});
        std::vector<byte> frame;auto glass=screenGlass(name,true,64,64,pixels.data(),frame);
        // Animated flats (liquids, lights) change their tiles every frame.
        int next;bool animated=P_PicAnimationNext(false,lump-firstflat,&next)>0;
        // Ice: its painted streaks cut grooves twice as deep, in a smooth,
        // glassy surface; no crooked tiles.
        if(Game_IceFlat(name))
            return withRelief(images.emplace(key,upload(64,64,pixels.data(),0,0,nullptr,nullptr,nullptr,true)).first->second,nullptr,2.0f,true);
        Image &result=withRelief(withGlassFrame(speckledPanel(images.emplace(key,upload(64,64,pixels.data(),0,0,glowing?mask.data():nullptr,nullptr,
            glass.empty()?nullptr:glass.data(),true)).first->second,name,true),frame),animated?nullptr:flatTiles);
        int kind=doom_emissive_kind(name,true);
        result.liquidEmitter=kind==DOOM_EMISSIVE_LAVA||kind==DOOM_EMISSIVE_SLIME;
        return result;
    }
    if(size<8) I_Error((char*)"Invalid sprite texture");
    const patch_t *patch=(const patch_t*)data;
    int w=SHORT(patch->width),h=SHORT(patch->height);
    if(w<1||h<1||w>8192||h>8192||8+(size_t)w*4>(size_t)size)
        I_Error((char*)"Invalid sprite dimensions");
    std::vector<byte> pixels((size_t)w*h*2);
    for(int x=0;x<w;++x) {
        size_t pos=(uint32_t)LONG(patch->columnofs[x]); int previous=-1;
        while(pos<(size_t)size && data[pos]!=255) {
            if(pos+4>(size_t)size||pos+4+data[pos+1]>(size_t)size) I_Error((char*)"Invalid sprite column");
            int top=data[pos]; if(top<=previous) top+=previous; previous=top;
            for(int y=0;y<data[pos+1];++y) if(top+y>=0&&top+y<h) {
                size_t pixel=((size_t)(top+y)*w+x)*2;
                pixels[pixel]=data[pos+3+y]; pixels[pixel+1]=255;
            }
            pos+=data[pos+1]+4;
        }
    }
    int paletteLump=W_GetNumForName((char*)"PLAYPAL");
    if(W_LumpLength(paletteLump)<768) I_Error((char*)"Invalid lighting palette");
    const byte *palette=(const byte*)W_CacheLumpNum(paletteLump,PU_CACHE);
    auto normals=spriteNormals(w,h,pixels.data());
    // Glowing eyes take normals no silhouette has: x all the way right
    // and y over halfway down, the glow in y's top quarter (eyeGlow).
    // Stock monsters' eyes are authored (sprite_eyes_stock.h); other
    // artwork is searched for them (sprite_eyes.h).
    if(livingMonsterLump(lump)) {
        char name[9];memcpy(name,lumpinfo[lump].name,8);name[8]=0;
        std::vector<byte> eyes;
        if(!doom_stock_eyes(name,w,h,pixels.data(),eyes))eyes=doom_sprite_eyes(w,h,pixels.data(),palette);
        for(size_t i=0;i<eyes.size();++i)if(eyes[i]) {normals[2*i]=255;normals[2*i+1]=(byte)(192+eyes[i]*63/255);}
    }
    Image image=upload(w,h,pixels.data(),SHORT(patch->leftoffset),SHORT(patch->topoffset),nullptr,normals.data());
    image.glowWeight=doom_sprite_glow(pixels.data(),(size_t)w*h,palette,image.glow.data());
    image.glowHeight=doom_sprite_glow_height(pixels.data(),w,h,palette,image.glow.data());
    return images.emplace(key,std::move(image)).first->second;
}
const std::array<float,3> &averageColor(Image &image) {
    if(image.averaged)return image.average;
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    double sum[3]={},count=0;
    for(size_t i=0;i+1<image.pixels.size();i+=2) if(image.pixels[i+1]) {
        for(int c=0;c<3;++c)sum[c]+=palette[image.pixels[i]*3+c]/255.0;
        ++count;
    }
    for(int c=0;c<3;++c)image.average[c]=count?(float)(sum[c]/count):0.5f;
    image.averaged=true;return image.average;
}
// Particles are chunky: opaque blocks of whole Doom pixels in WAD palette
// colors, taken from random texels of the art they come from.
std::array<float,3> paletteColor(const std::array<float,3> &color) {
    static std::vector<int16_t> nearest;
    if(nearest.empty())nearest.assign(32*32*32,-1);
    int cell[3];for(int c=0;c<3;++c)cell[c]=std::clamp((int)std::lround(color[c]*31),0,31);
    int16_t &index=nearest[(cell[0]*32+cell[1])*32+cell[2]];
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    if(index<0) {
        int best=1<<30;
        for(int i=0;i<256;++i) {
            int d=0;for(int c=0;c<3;++c){int e=palette[i*3+c]-cell[c]*255/31;d+=e*e;}
            if(d<best){best=d;index=(int16_t)i;}
        }
    }
    return {palette[index*3]/255.0f,palette[index*3+1]/255.0f,palette[index*3+2]/255.0f};
}
// The brightest opaque texel's color.
const std::array<float,3> &brightestColor(Image &image) {
    if(image.brightened)return image.brightest;
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    int best=-1,bestSum=-1;
    for(size_t i=0;i+1<image.pixels.size();i+=2) if(image.pixels[i+1]) {
        int index=image.pixels[i],sum=palette[index*3]+palette[index*3+1]+palette[index*3+2];
        if(sum>bestSum){bestSum=sum;best=index;}
    }
    image.brightest=best<0?averageColor(image):std::array<float,3>{palette[best*3]/255.0f,palette[best*3+1]/255.0f,palette[best*3+2]/255.0f};
    image.brightened=true;return image.brightest;
}
std::array<float,3> texelColor(Image &image) {
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    size_t count=image.pixels.size()/2;
    for(int tries=0;count&&tries<8;++tries) {
        size_t i=std::min(count-1,(size_t)randomRange(0,(float)count));
        if(image.pixels[2*i+1])return {palette[image.pixels[2*i]*3]/255.0f,palette[image.pixels[2*i]*3+1]/255.0f,palette[image.pixels[2*i]*3+2]/255.0f};
    }
    return averageColor(image);
}
void imagesShutdown() {images.clear();cloudTexture=nullptr;}
} // namespace doom3d
