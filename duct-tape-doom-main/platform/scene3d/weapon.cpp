#include "../../assets/keyhands/images.h"
/* The relit weapon: its light probe of the surroundings, draws, and the
   crosshair. */
#include "internal.h"
#include "../weapon_transform.h"
#include "../weapon_switch.h"
#include "../mod_textures.h"
#include "../fluid_weapons.h"
#include "../fluid_gloves.h"
#include "../mod_soundbank.h"
extern "C" int I_StartNamedSound(const char*,int,int,int,int);
extern "C" {
#include "d_items.h"
#include "p_fluid.h"
#include "i_sound.h"
}

namespace doom3d {
static doomfluid::Weapons fluidAnimation;
static doomweapon::WeaponSwitch fluidSwitch;
static std::unique_ptr<doommod::TextureBank> fluidTextures;
static std::map<std::string,int> fluidImageKeys;
static bool fluidAlt=false,fluidReload=false;
static int fluidAmbientHandle=0;
static std::string fluidAmbientName;
void fluidWeaponAux(int alt,int reload){fluidAlt=alt!=0;fluidReload=reload!=0;}
static bool fluidEnabled() {return P_FluidWeaponsEnabled()!=0;}
static int playFluidSound(const std::string&event,int volume=127) {
    std::string sound="MRIntW/HQ/"+event;
    if(!ModSounds_Has(sound.c_str()))sound="MRIntW/"+event;
    if(event=="ShotgunPumpDown"&&!ModSounds_Has("MRIntW/HQ/ShotgunPumpDown"))sound="MRIntW/ShotgunPump";
    if(event=="PistolReload2"&&!ModSounds_Has(sound.c_str()))sound="MRIntW/PistolReload";
    if(ModSounds_Has(sound.c_str()))return I_StartNamedSound(sound.c_str(),event=="FistSwing"?38:volume,128,128,64);
    return 0;
}
static void initializeFluidTextures() {
    if(fluidTextures||!fluidEnabled())return;
    int arg=M_CheckParm((char*)"-fluidweapons");
    if(arg+1>=myargc)I_Error((char*)"-fluidweapons needs the main Fluid Weapons PK3");
    try {
        auto palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
        auto bank=std::make_unique<doommod::TextureBank>(palette,[](const std::string&name){
            if(name.size()>8)return std::vector<uint8_t>{};
            int lump=W_CheckNumForName((char*)name.c_str());if(lump<0)return std::vector<uint8_t>{};
            auto p=(const byte*)W_CacheLumpNum(lump,PU_CACHE);
            return std::vector<uint8_t>(p,p+W_LumpLength(lump));
        });
        bank->addArchive(myargv[arg+1]);
        fluidTextures=std::move(bank);
    }catch(const std::exception&e){I_Error((char*)"Fluid Weapons resources: %s",e.what());}
}
void fluidWeaponShot(mobj_t*origin,int weapon,int flash) {
    muzzleFlash(origin,weapon,flash);
    if(weapon==wp_missile)fluidRocketLaunch(origin);
    if(fluidEnabled()&&origin==players[displayplayer].mo)fluidAnimation.shot(weapon);
}
void fluidWeaponAttack(player_t*player) {
    if(fluidEnabled()&&player==&players[displayplayer]){
        fluidAnimation.punchDirection(player->cmd.angleturn,player->cmd.sidemove);
        fluidAnimation.attack((int)player->readyweapon);
    }
}
static std::array<doomweapon::Motion,NUMPSPRITES> weaponMotion;
static std::array<bool,NUMPSPRITES> weaponWasActive={};
static int weaponPlayer=-1,weaponType=-1;
void resetWeaponPositions() {
    if(fluidAmbientHandle)I_StopSound(fluidAmbientHandle);
    fluidAmbientHandle=0;fluidAmbientName.clear();
    for(auto &motion:weaponMotion)motion.reset();
    weaponWasActive.fill(false);weaponPlayer=weaponType=-1;
    fluidAnimation.reset((int)players[displayplayer].readyweapon);
    fluidSwitch.reset();
}
void snapshotWeaponPositions() {
    const player_t &player=players[displayplayer];
    bool changed=weaponPlayer!=displayplayer||weaponType!=(int)player.readyweapon;
    if(changed)resetWeaponPositions();
    weaponPlayer=displayplayer;weaponType=(int)player.readyweapon;
    if(fluidEnabled()) {
        if(changed){
            static const char*select[]={"FistSelect","PistolSelect","ShotgunSelect","ChaingunSelect","RocketSelect","PlasmaSelect","BFGSelect","SawUp","SSGSelect"};
            // The chainsaw's raise sound is already emitted by the Doom state.
            if(weaponType>=0&&weaponType<NUMWEAPONS&&weaponType!=wp_chainsaw)playFluidSound(select[weaponType]);
        }
        bool ready=player.health>0&&player.pendingweapon==wp_nochange&&player.psprites[ps_weapon].state==&states[weaponinfo[weaponType].readystate];
        fluidAnimation.input(fluidAlt,fluidReload,ready);
        fluidAnimation.movement(player.cmd.angleturn,tourpitch,units(player.bob),units(player.mo->momz),player.mo->z>player.mo->floorz,player.cmd.forwardmove||player.cmd.sidemove,leveltime);
        fluidAnimation.tick(weaponType);
        const auto state=player.psprites[ps_weapon].state;
        int switchPhase=state==&states[weaponinfo[weaponType].upstate]?1:
                        state==&states[weaponinfo[weaponType].downstate]?2:0;
        bool wasSwitching=fluidSwitch.active();
        auto switchEnd=wasSwitching&&weaponType==wp_chaingun?fluidSwitch.layers(1,settings.fluidBareHands):std::vector<doomfluid::Layer>{};
        fluidSwitch.tick(weaponType,switchPhase,player.health<=0||
            (!switchPhase&&(state!=&states[weaponinfo[weaponType].readystate]||fluidAlt||fluidReload)));
        if(wasSwitching&&!fluidSwitch.active())fluidAnimation.adoptSwitchPose(switchEnd);
        for(const auto&event:fluidAnimation.takeSounds())playFluidSound(event);
        std::string ambient=player.health>0?fluidAnimation.ambientSound():"";
        int ambientVolume=int(40*fluidAnimation.ambientLevel());
        if(ambientVolume<1||player.pendingweapon!=wp_nochange)ambient.clear();
        if(ambient!=fluidAmbientName){if(fluidAmbientHandle)I_StopSound(fluidAmbientHandle);fluidAmbientHandle=0;fluidAmbientName=ambient;}
        if(!ambient.empty()){
            if(!I_SoundIsPlaying(fluidAmbientHandle))fluidAmbientHandle=playFluidSound(ambient,ambientVolume);
            I_UpdateSoundParams(fluidAmbientHandle,ambientVolume,128,int(64+64*fluidAnimation.ambientLevel()));
        }
    }
    for(int i=0;i<NUMPSPRITES;++i) {
        const auto &psp=player.psprites[i];
        if(!psp.state) {weaponMotion[i].reset();weaponWasActive[i]=false;continue;}
        doomweapon::Transform now;now.x=units(psp.sx);now.y=units(psp.sy);
        weaponMotion[i].set(now,false);weaponMotion[i].beforeTick();weaponWasActive[i]=true;
    }
}
// Weapon frames to relight (weapon_lighting.h), keyed below the WAD lumps:
// the de-lit artwork with its normals in B/A, two transparent columns, then
// its gloss in R.
static Image &relitWeaponImage(int key,const Image &source) {
    auto found=images.find(key); if(found!=images.end()) return found->second;
    int w=source.width,h=source.height,stride=2*w+2;
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    auto maps=doom_weapon_delight(w,h,source.pixels.data(),palette);
    std::vector<byte> packed((size_t)stride*h*4,0);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        size_t i=(size_t)y*w+x;byte *art=&packed[((size_t)y*stride+x)*4];
        art[0]=maps.pixels[2*i];art[1]=maps.pixels[2*i+1];art[2]=maps.normals[2*i];art[3]=maps.normals[2*i+1];
        packed[((size_t)y*stride+w+2+x)*4]=maps.gloss[i];
    }
    Image image;image.pixels=std::move(maps.pixels);image.opaque=false;
    image.width=w;image.height=h;image.left=source.left;image.top=source.top;
    image.texture=gpuCreateTexture(GpuFormat::RGBA8,stride,h,packed.data(),stride*4);
    if(!image.texture) I_Error((char*)"Could not allocate a GPU texture");
    return images.emplace(key,std::move(image)).first->second;
}
static Image &pickupHandImage(int card,bool relit){
    int key=-4100000-card;
    auto found=images.find(key);
    if(found==images.end()){
        SDL_Surface *png=SDL_LoadPNG_IO(SDL_IOFromConstMem(keyhandData[card],keyhandSize[card]),true);
        SDL_Surface *rgba=png?SDL_ConvertSurface(png,SDL_PIXELFORMAT_RGBA32):nullptr;SDL_DestroySurface(png);
        if(!rgba)I_Error("Cannot decode key pickup hand");
        // Sample at weapon-sprite resolution, then use the game's palette and lighting.
        const int h=140,w=int(h*float(rgba->w)/rgba->h);
        std::vector<byte> pixels(w*h*2);const byte *pal=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){
            int sx=std::clamp(int((x+.5f)*rgba->w/w),5,rgba->w-1),sy=std::min(rgba->h-1,int((y+.5f)*rgba->h/h));
            const byte *c=(const byte*)rgba->pixels+sy*rgba->pitch+sx*4;int best=0,dist=INT_MAX;
            for(int n=0;n<256;++n){int d=0;for(int k=0;k<3;++k){int v=int(c[k])-pal[n*3+k];d+=v*v;}if(d<dist){dist=d;best=n;}}
            pixels[(y*w+x)*2]=byte(best);pixels[(y*w+x)*2+1]=c[3];
        }
        SDL_DestroySurface(rgba);found=images.emplace(key,upload(w,h,pixels.data())).first;
    }
    return relit?relitWeaponImage(key-100,found->second):found->second;
}
static Image &weaponImage(int lump) {return relitWeaponImage(INT_MIN/2-lump,lumpImage(lump,false));}
static Image &fluidImage(const std::string&name,bool relit) try {
    static std::array<float,7> gloveSettings{};
    std::array<float,7> current={(float)settings.fluidGloves,settings.fluidGloveRed,settings.fluidGloveGreen,settings.fluidGloveBlue,settings.fluidShadeRed,settings.fluidShadeGreen,settings.fluidShadeBlue};
    if(current!=gloveSettings) {
        // Free previous colored GPU frames; color changes cannot grow the cache.
        for(const auto&entry:fluidImageKeys){images.erase(entry.second);images.erase(entry.second-100000);}
        gloveSettings=current;
    }
    auto found=fluidImageKeys.find(name);
    int key=found==fluidImageKeys.end()?fluidImageKeys.emplace(name,-2000000-(int)fluidImageKeys.size()).first->second:found->second;
    auto cached=images.find(key);
    if(cached==images.end()) {
        const auto&source=fluidTextures->image(name);
        auto pixels=source.pixels;
        int translation=doomfluid::spriteTranslation(name);
        bool pistolPalette=translation==2;
        if(translation) {
            auto table=doomfluid::gloveTranslation((const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE),settings.fluidGloves,pistolPalette,
                {settings.fluidShadeRed,settings.fluidShadeGreen,settings.fluidShadeBlue},
                {settings.fluidGloveRed,settings.fluidGloveGreen,settings.fluidGloveBlue});
            for(size_t p=0;p<pixels.size();p+=2)pixels[p]=table[pixels[p]];
        }
        int left=0,top=0,right=source.width,bottom=source.height;
        if(!fluidTextures->noTrim(name)){
            left=source.width;top=source.height;right=bottom=0;
            for(int y=0;y<source.height;++y)for(int x=0;x<source.width;++x)if(pixels[(y*source.width+x)*2+1]){
                left=std::min(left,x);top=std::min(top,y);right=std::max(right,x+1);bottom=std::max(bottom,y+1);
            }
            if(right<=left||bottom<=top){left=top=0;right=source.width;bottom=source.height;}
        }
        int width=right-left,height=bottom-top;
        std::vector<byte> cropped(width*height*2);
        for(int y=0;y<height;++y)std::copy_n(pixels.data()+((y+top)*source.width+left)*2,width*2,cropped.data()+y*width*2);
        cached=images.emplace(key,upload(width,height,cropped.data(),source.left-left,source.top-top)).first;
    }
    return relit?relitWeaponImage(key-100000,cached->second):cached->second;
} catch(const std::exception&e) {
    I_Error((char*)"Fluid Weapons image %s: %s",name.c_str(),e.what());
    std::abort();
}
// What the relit weapon sees around it, in the view's frame: ahead, behind,
// right, left, up and down (rgb, w unused). Five rays a direction walk the
// bake map from the eye; each hit shows its surface's average color under
// its sector and baked light, plus its glow, as world.frag draws it. Eased
// over about a tenth of a second so single rays changing what they hit don't
// flicker. The shader lights the weapon with it as an ambient cube, and its
// glossy and grazing edges mirror it, so a bright wall ahead creeps over the
// weapon's outline even when no light reaches the player.
float weaponProbe[24]={};
static std::array<float,18> weaponProbeTarget={};
static uint64_t weaponProbeTime=0;static unsigned weaponProbeSerial=~0u;
static void updateWeaponProbe(const Uniforms &camera,sector_t *sector) {
    uint64_t now=SDL_GetTicksNS();
    bool fresh=weaponProbeSerial!=r_levelserial;
    weaponProbeSerial=r_levelserial;
    if(bakeMap.sectors.size()!=(size_t)numsectors)return;
    const float *axes[3]={camera.forward,camera.right,camera.up};
    float eye[3]={camera.eye[0],camera.eye[1],camera.eye[2]};
    const BakeSector &own=bakeMap.sectors[sector-sectors];
    eye[2]=std::clamp(eye[2],own.floor+1,std::max(own.floor+1,own.ceiling-1));
    bool glow=settings.emissive;
    auto lit=[&](const std::array<float,3> &albedo,const Image *image,int at,const uint8_t *baked,float out[3]) {
        float level=lighting(&sectors[at]),shown[3]={};
        if(baked) {for(int c=0;c<3;++c)shown[c]=baked[c]/255.0f*2;fillHeadroom(shown,level);}
        float cover=glow&&image&&image->emissionWeight>0?image->emissionCoverage:0;
        for(int c=0;c<3;++c)out[c]=albedo[c]*(level+shown[c])*(1-cover)+(cover>0?image->emissionColor[c]*cover*0.9f:0);
    };
    auto radiance=[&](const BakeHit &hit,float out[3]) {
        if(hit.kind==BakeHit::thing) {lit({0.4f,0.4f,0.4f},nullptr,hit.sector,nullptr,out);return;}
        if(hit.kind==BakeHit::wall) {
            const line_t &line=lines[hit.line];
            int side=line.frontsector==&sectors[hit.sector]?0:1;
            if(line.sidenum[side]<0) side^=1;
            const side_t &facing=sides[line.sidenum[side]];
            short texture=hit.part==1?facing.toptexture:hit.part==2?facing.bottomtexture:facing.midtexture;
            Image *image=texture>0?&wallImage(texturetranslation[texture]):nullptr;
            const uint8_t *baked=nullptr;
            const BakeStrip &strip=bakeStrips.size()>(size_t)hit.line*2+side?bakeStrips[(size_t)hit.line*2+side]:BakeStrip{};
            if(!wallBakeCells.empty()&&strip.x>=0) {
                float along=(hit.x-strip.ax)*strip.dx+(hit.y-strip.ay)*strip.dy;
                int column=std::clamp((int)std::floor(along*strip.scale),0,strip.width-1);
                int row=std::clamp((int)std::floor((hit.z-strip.low)*strip.scale),0,strip.height-1);
                baked=&wallBakeCells[4*((size_t)(strip.y+row)*atlasStride+strip.x+column)];
            }
            lit(image?averageColor(*image):std::array<float,3>{0.5f,0.5f,0.5f},image,hit.sector,baked,out);
            return;
        }
        bool ceiling=hit.kind==BakeHit::ceiling;
        int pic=ceiling?sectors[hit.sector].ceilingpic:sectors[hit.sector].floorpic;
        Image &image=lumpImage(firstflat+flattranslation[pic],true);
        const uint8_t *baked=nullptr;
        if(!flatLightCells.empty()) {
            int cx=std::clamp((int)std::floor((hit.x-mapOrigin[0])/mapCell),0,mapWidth-1);
            int cy=std::clamp((int)std::floor((hit.y-mapOrigin[1])/mapCell),0,mapHeight-1);
            baked=flatCell(ceiling?ceilingLight:floorLight,(size_t)cy*mapWidth+cx);
        }
        lit(averageColor(image),&image,hit.sector,baked,out);
    };
    std::array<float,18> target={};
    for(int axis=0;axis<6;++axis) {
        float sign=axis&1?-1.0f:1.0f;const float *main=axes[axis/2],*a=axes[(axis/2+1)%3],*b=axes[(axis/2+2)%3];
        // The axis and four rays tilted 35 degrees around it.
        static const float tilt[5][2]={{0,0},{0.7f,0},{-0.7f,0},{0,0.7f},{0,-0.7f}};
        for(const auto &t:tilt) {
            float direction[3],length=0,out[3]={};
            for(int c=0;c<3;++c){direction[c]=main[c]*sign+a[c]*t[0]+b[c]*t[1];length+=direction[c]*direction[c];}
            length=std::sqrt(length);for(float &c:direction)c/=length;
            BakeHit hit;
            if(traceRay(bakeMap,(int)(sector-sectors),eye[0],eye[1],eye[2],direction,2048,SkyCeiling::escape,&hit)||hit.kind==BakeHit::none) {
                // Open sky above, or nothing within reach: the sky's light, or the room's.
                float level=lighting(sector);
                for(int c=0;c<3;++c)out[c]=direction[2]>0.3f&&skyLevel>0?skyTint[c]*skyLevel:0.5f*level;
            } else radiance(hit,out);
            for(int c=0;c<3;++c)target[axis*3+c]+=out[c]/5;
        }
    }
    float seconds=fresh||!weaponProbeTime?1.0f:std::min(0.1f,(now-weaponProbeTime)/1e9f);
    weaponProbeTime=now;float blend=1-std::exp(-seconds/0.1f);
    for(int i=0;i<18;++i) {
        weaponProbeTarget[i]+=(target[i]-weaponProbeTarget[i])*blend;
        weaponProbe[(i/3)*4+i%3]=weaponProbeTarget[i];
    }
}
std::vector<SpriteDraw> weaponDraws(const Uniforms &camera) {
    std::vector<SpriteDraw> draws;
    player_t *player=&players[displayplayer];
    const int invisibility=player->powers[pw_invisibility];
    const bool invisible=invisibility>4*32||(invisibility&8);
    initializeFluidTextures();
    auto fluid=fluidEnabled()?fluidAnimation.layers(renderFraction,settings.fluidBareHands):std::vector<doomfluid::Layer>{};
    if(fluidEnabled()&&fluidSwitch.active()){
        fluid=fluidSwitch.layers(renderFraction,settings.fluidBareHands);
        // Invisible control frames must not fall back to vanilla artwork.
        if(fluid.empty())return {};
    }
    if(settings.weaponLighting&&camera.effects[2]==0&&player->mo)updateWeaponProbe(camera,player->mo->subsector->sector);
    for(int i=0;i<(fluid.empty()?NUMPSPRITES:(int)fluid.size());++i) {
        bool modded=!fluid.empty();
        auto psp=player->psprites[modded?0:i];if(!psp.state)continue;
        // A modded weapon can exist in an IWAD without its vanilla sprites
        // (for example the SSG in Doom I). Only consult those for vanilla art.
        spriteframe_t sf{};
        if(!modded)sf=sprites[psp.state->sprite].spriteframes[psp.state->frame&FF_FRAMEMASK];
        bool bright=modded?fluid[i].bright:psp.state->frame&FF_FULLBRIGHT;
        // MODE_RELIT on the screen: the weapon relit per pixel from its own
        // normals, its painted light taken out.
        // Fluid sprites already contain the intended material shading. Preserve it;
        // the normal sprite path still applies sector and dynamic world light.
        bool relit=settings.weaponLighting&&!bright&&!modded;
        int lump=firstspritelump+sf.lump[0];
        Image &image=modded?fluidImage(fluid[i].sprite,relit):(relit?weaponImage(lump):lumpImage(lump,false));
        doomweapon::Transform position;position.x=units(psp.sx);position.y=units(psp.sy);
        // Opt-in while the Fluid Weapons runtime is being ported. Only the
        // visual position changes; state timing, firing and ammo stay in C.
        if(modded||M_CheckParm((char*)"-weaponinterpolation")) {
            const int baseSprite=modded?0:i;
            bool continuous=weaponWasActive[baseSprite]&&weaponPlayer==displayplayer&&weaponType==(int)player->readyweapon&&tickTime;
            weaponMotion[baseSprite].set(position,continuous);
            position=weaponMotion[baseSprite].sample(renderFraction);
        }
        if(modded){position.x=0;if(position.y>=32&&position.y<49)position.y=32;}
        float x=position.x-image.left,y=viewwindowy+viewheight/2.0f-100+position.y-image.top;
        std::vector<Vertex> weapon;
        bool flipped=modded?fluid[i].flipX:sf.flip[0];
        float u0=flipped?image.width:0,u1=flipped?0:image.width;
        float light=bright?1.0f:sunLightAt(camera.eye[0],camera.eye[1],player->mo->subsector->sector,
                flowLightAt(camera.eye[0],camera.eye[1],player->mo->subsector->sector,
                seamLightAt(camera.eye[0],camera.eye[1],player->mo->subsector->sector,lighting(player->mo->subsector->sector))));
        screenQuad(weapon,x/160-1,1-y/100,image.width/160.0f,image.height/100.0f,u0,0,u1,image.height,light,
            MODE_SCREEN|MODE_SPRITE|(invisible?MODE_FUZZ:0u)|(relit?MODE_RELIT|(flipped?MODE_FLIPPED:0u):0u));
        if(modded) {
            const auto&source=fluidTextures->image(fluid[i].sprite);
            auto transform=fluid[i].transform;
            float w=image.width/source.scaleX,h=image.height/source.scaleY;
            transform.pivotX*=w;transform.pivotY*=h;
            transform.x+=position.x-image.left/source.scaleX;
            transform.y+=viewwindowy+viewheight/2.f-100+position.y-image.top/source.scaleY;
            if(!fluid[i].addWeapon){transform.x-=position.x;transform.y-=position.y;}
            // PSPF_MIRROR changes the rectangle's position; PSPF_FLIP changes
            // artwork, pivot and rotation independently.
            if(fluid[i].mirror)transform.x=320-transform.x-w;
            if(flipped){transform.pivotX=w-transform.pivotX;transform.rotation=-transform.rotation;}
            for(auto&v:weapon) {
                float localX=(v.x+1)*160-x,localY=(1-v.y)*100-y;
                auto point=doomweapon::point(transform,localX/source.scaleX,localY/source.scaleY);
                v.x=point[0]/160-1;v.y=1-point[1]/100;
                v.sunU=-2;v.sunV=fluid[i].alpha;
            }
        }
        // The weapon takes a restrained, capped share of the light around the
        // eye so nearby lamps tint it without washing out the artwork.
        if(!bright) {
            StaticSample statics;
            auto baked=bakedLightAt(camera.eye[0],camera.eye[1],camera.eye[2],player->mo->subsector->sector,&statics);
            for(float &c:baked)c=std::min(0.5f,c*0.4f);
            unsigned tint=packedLampTint(statics,light);
            for(auto &vertex:weapon) {vertex.red=baked[0];vertex.green=baked[1];vertex.blue=baked[2];vertex.lampTint=tint;}
            if(relit) {
                // Lights that reach the weapon, which the shader places about
                // ten units ahead of the eye; the flashlight only tints it.
                // With bake-only lights the static light's direction comes
                // packed as for sprites.
                float low[3],high[3];unsigned mask[2]={},packedStatics=0;
                for(int c=0;c<3;++c) {
                    float at=camera.eye[c]+camera.forward[c]*10-camera.up[c]*5;
                    low[c]=at-12;high[c]=at+12;
                }
                for(unsigned n=0;n<flashes.count;++n) {
                    const auto &light=flashes.lights[n];
                    if(!fogOnly[n]&&light.direction[3]<=0&&doom_flash_reaches_bounds(&light,low,high))mask[n/32]|=1u<<(n%32);
                }
                float towards=std::sqrt(statics.towards[0]*statics.towards[0]+statics.towards[1]*statics.towards[1]+statics.towards[2]*statics.towards[2]);
                if(bakeOnlyActive&&statics.amount>0&&towards>1e-6f) {
                    for(int c=0;c<3;++c)packedStatics|=(unsigned)std::lround((statics.towards[c]/towards*0.5f+0.5f)*255)<<(8*c);
                    packedStatics|=(unsigned)std::lround(std::clamp(towards/statics.amount,0.0f,1.0f)*255)<<24;
                }
                for(auto &vertex:weapon) {std::copy(mask,mask+2,vertex.lightMask);vertex.statics=packedStatics;}
            }
        }
        draws.push_back({&image,std::move(weapon),!invisible&&modded&&fluid[i].additive});
    }
    // The pickup hand is a real weapon layer, before HUD and post-processing.
    for(auto &draw:draws)for(auto &v:draw.vertices)v.y-=.85f*keyWeaponDrop;
    if(keyHandVisible>0&&keyHandIndex>=0&&keyHandIndex<6&&player->mo){
        bool relit=false; // Preserve painted shading, matching Fluid weapon sprites.
        Image &image=pickupHandImage(keyHandIndex,relit);
        std::vector<Vertex> hand;
        float h=1.05f,w=h*image.width/image.height*.625f;
        float light=sunLightAt(camera.eye[0],camera.eye[1],player->mo->subsector->sector,
            flowLightAt(camera.eye[0],camera.eye[1],player->mo->subsector->sector,
            seamLightAt(camera.eye[0],camera.eye[1],player->mo->subsector->sector,lighting(player->mo->subsector->sector))));
        screenQuad(hand,.08f-w*.5f,1-(viewwindowy+viewheight)/100.f+h*keyHandVisible,w,h,0,0,image.width,image.height,light,
            MODE_SCREEN|MODE_SPRITE|(invisible?MODE_FUZZ:0u)|(relit?MODE_RELIT:0u));
        StaticSample statics;auto baked=bakedLightAt(camera.eye[0],camera.eye[1],camera.eye[2],player->mo->subsector->sector,&statics);
        unsigned tint=packedLampTint(statics,light);unsigned mask[2]={};
        float low[3],high[3];for(int c=0;c<3;++c){float at=camera.eye[c]+camera.forward[c]*10-camera.up[c]*5;low[c]=at-12;high[c]=at+12;}
        for(unsigned n=0;n<flashes.count;++n)if(!fogOnly[n]&&flashes.lights[n].direction[3]<=0&&doom_flash_reaches_bounds(&flashes.lights[n],low,high))mask[n/32]|=1u<<(n%32);
        for(auto &v:hand){v.red=std::min(.5f,baked[0]*.4f);v.green=std::min(.5f,baked[1]*.4f);v.blue=std::min(.5f,baked[2]*.4f);v.lampTint=tint;std::copy(mask,mask+2,v.lightMask);}
        draws.push_back({&image,std::move(hand),false});
    }
    // Tilt the complete held weapon (including flashes and pickup hands) about
    // the bottom of the view. HUD and crosshair remain upright.
    if(std::abs(cornerPeekRoll)>.0001f){
        float c=std::cos(cornerPeekRoll*.4f),s=std::sin(cornerPeekRoll*.4f);
        float bottom=1-(viewwindowy+viewheight)/100.f;
        for(auto &draw:draws)for(auto &v:draw.vertices){
            float x=v.x*1.6f,y=v.y-bottom;
            v.x=(x*c+y*s)/1.6f;v.y=bottom-x*s+y*c;
        }
    }
    return draws;
}
std::vector<Vertex> crosshairVertices(float worldW,float worldH) {
    std::vector<Vertex> cross;
    if(!worldPending||!settings.crosshair||menuactive)return cross;
    float px=2/worldW,py=2/worldH;
    screenQuad(cross,-7*px,py,5*px,2*py,0,0,1,1);screenQuad(cross,2*px,py,5*px,2*py,0,0,1,1);
    screenQuad(cross,-px,7*py,2*px,5*py,0,0,1,1);screenQuad(cross,-px,-2*py,2*px,5*py,0,0,1,1);
    for(auto &v:cross){v.x+=cornerPeekAim[0];v.y+=cornerPeekAim[1];}
    return cross;
}
} // namespace doom3d





