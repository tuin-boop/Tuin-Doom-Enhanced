#ifndef DOOM_TOASTY_H
#define DOOM_TOASTY_H
#include "../../assets/toasty/portrait.h"
#include "../../assets/toasty/romero.h"
extern "C" {
extern unsigned p_toasty_serial,p_romero_serial;
extern int p_toasty_tic,p_toasty_preview,snd_SfxVolume;
int I_StartNamedSound(const char*,int,int,int,int);
void I_StopSound(int);
}
namespace doom3d {
GpuTextureRef toastyTexture,romeroTexture;
float romeroAge=-1,romeroAspect=1;
int romeroCorner=1;
static void romeroNotice() {
 static unsigned seen=0,level=~0u;static int start=-1000,handle=0;
 romeroAge=-1;
 if(level!=r_levelserial||!worldPending||gamestate!=GS_LEVEL){level=r_levelserial;seen=p_romero_serial;start=-1000;return;}
 if(seen!=p_romero_serial){seen=p_romero_serial;start=leveltime;
  romeroCorner=(SDL_GetTicksNS()&1)?1:-1;
  if(handle)I_StopSound(handle);
  handle=I_StartNamedSound("DuctTape/Toasty",snd_SfxVolume,128,128,100);
  if(!romeroTexture){SDL_Surface *png=SDL_LoadPNG_IO(SDL_IOFromConstMem(romero_png,sizeof(romero_png)),true);
   SDL_Surface *rgba=png?SDL_ConvertSurface(png,SDL_PIXELFORMAT_RGBA32):nullptr;SDL_DestroySurface(png);
   if(rgba){romeroTexture=gpuCreateTexture(GpuFormat::RGBA8,rgba->w,rgba->h,rgba->pixels,rgba->pitch);romeroAspect=float(rgba->w)/rgba->h;SDL_DestroySurface(rgba);}
  }
 }
 float age=(leveltime-start+renderFraction)/35.f;
 if(age>=0&&age<1.25f)romeroAge=age;
}

float toastyVisible=0,toastyAspect=1;
static void toastyNotice() {
    toastyVisible=0;
    if(worldPending && !toastyTexture) {
        SDL_Surface *png=SDL_LoadPNG_IO(SDL_IOFromConstMem(toasty_png,sizeof(toasty_png)),true);
        SDL_Surface *rgba=png?SDL_ConvertSurface(png,SDL_PIXELFORMAT_RGBA32):nullptr;
        SDL_DestroySurface(png);
        if(rgba) {
            toastyTexture=gpuCreateTexture(GpuFormat::RGBA8,rgba->w,rgba->h,rgba->pixels,rgba->pitch);
            toastyAspect=float(rgba->w)/rgba->h;
            SDL_DestroySurface(rgba);
        }
    }
    static unsigned serial=0,level=~0u;
    static int start=-1000,handle=0;
    if(level!=r_levelserial || !worldPending || gamestate!=GS_LEVEL || menuactive) {
        level=r_levelserial;serial=p_toasty_serial;start=-1000;
        p_toasty_preview=0;
        if(handle){I_StopSound(handle);handle=0;}
        return;
    }
    if(serial!=p_toasty_serial) {
        serial=p_toasty_serial;
        // A multi-kill must not stack voices or hold the portrait onscreen.
        if((p_toasty_preview || leveltime-start>38) && leveltime-p_toasty_tic>=0 && leveltime-p_toasty_tic<37) {
            if(p_toasty_preview && handle)I_StopSound(handle);
            start=p_toasty_tic;
            handle=I_StartNamedSound("DuctTape/Toasty",snd_SfxVolume,128,128,100);
        }
        p_toasty_preview=0;
    }
    float t=(leveltime-start+renderFraction)/35.f;
    if(t<0 || t>=1.06f || automapactive)return;
    float rise=std::clamp(t/.18f,0.f,1.f);
    float fall=std::clamp((t-.9f)/.16f,0.f,1.f);
    toastyVisible=(1-std::pow(1-rise,3.f))*(1-fall*fall);
}
}
#endif
