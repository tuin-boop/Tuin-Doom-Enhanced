#ifndef DOOM_WEAPON_WHEEL_H
#define DOOM_WEAPON_WHEEL_H
#include "../../assets/weaponwheel/red_alt.h"
namespace doom3d {
static bool wheelActive=false;
static int wheelSelected=0,wheelLevel=0;
static float wheelX=0,wheelY=0;
static bool wheelAllowed(){return worldPending&&gamestate==GS_LEVEL&&!menuactive&&!paused&&!demoplayback&&!demorecording&&!netgame&&players[consoleplayer].health>0;}
static bool wheelOwned(int slot){return slot>=0&&slot<NUMWEAPONS&&players[consoleplayer].weaponowned[slot]&&!(gamemode==shareware&&(slot==wp_plasma||slot==wp_bfg));}
static void wheelDraw(std::vector<unsigned>& out){
    if(!wheelActive)return;
    if(!wheelAllowed()||wheelLevel!=r_levelserial){wheelActive=false;return;}
    const char* names[]={"FISTS","PISTOL","SHOTGUN","CHAINGUN","ROCKET","PLASMA","BFG","SAW","SSG"};
    const char* alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    const char* glyphs[]={"010101111101101","110101110101110","111100100100111","110101101101110","111100110100111","111100110100100","111100101101111","101101111101101","111010010010111","001001001101111","101101110101101","100100100100111","101111111101101","110101101101101","111101101101111","111101111100100","111101101111001","110101110101101","111100111001111","111010010010010","101101101101111","101101101101010","101101111111101","101101010101101","101101010010010","111001010100111"};
    auto pixel=[&](int x,int y,unsigned c){if(x>=0&&x<320&&y>=0&&y<200)out[y*320+x]=c;};
    auto text=[&](const char* value,int x,int y,unsigned c){for(;*value;++value,x+=4){const char* at=strchr(alphabet,*value);if(!at)continue;const char* bits=glyphs[at-alphabet];for(int yy=0;yy<5;++yy)for(int xx=0;xx<3;++xx)if(bits[yy*3+xx]=='1')pixel(x+xx,y+yy,c);}};
    for(int y=22;y<178;++y)for(int x=65;x<255;++x){float dx=x-160.f,dy=(y-100.f)*1.2f;float radius=std::sqrt(dx*dx+dy*dy);if(radius>88||radius<35)continue;float angle=std::atan2(dy,dx)+1.570796327f+3.141592654f/9;int slot=int(std::floor(angle/(6.283185307f/9)+9))%9;pixel(x,y,slot==wheelSelected?(wheelOwned(slot)?0xe03b697a:0xe0533434):0xd0142028);}
    const byte* palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    for(int slot=0;slot<9;++slot){
        float a=slot*6.283185307f/9-1.570796327f;
        int cx=int(160+std::cos(a)*65),cy=int(100+std::sin(a)*65/1.2f);
        bool owned=wheelOwned(slot);
        const unsigned char* patch=wheelArt[owned&&slot==wheelSelected?1:0][slot];
        auto u16=[](const unsigned char* p){return unsigned(p[0])|(unsigned(p[1])<<8);};
        auto u32=[](const unsigned char* p){return unsigned(p[0])|(unsigned(p[1])<<8)|(unsigned(p[2])<<16)|(unsigned(p[3])<<24);};
        int width=u16(patch),height=u16(patch+2);
        float scale=std::min(1.f,std::min(42.f/width,23.f/height));
        int dw=std::max(1,int(width*scale)),dh=std::max(1,int(height*scale));
        int left=cx-dw/2,top=cy-4-dh/2;
        // Original Doom column posts preserve the WAD's transparent silhouette.
        for(int xx=0;xx<dw;++xx){
            int sx=xx*width/dw;
            const unsigned char* post=patch+u32(patch+8+sx*4);
            while(post[0]!=255){
                for(int yy=0;yy<dh;++yy){
                    int sy=yy*height/dh;
                    if(sy<post[0]||sy>=post[0]+post[1])continue;
                    int index=post[3+sy-post[0]];
                    unsigned r=palette[index*3],g=palette[index*3+1],b=palette[index*3+2];
                    if(!owned){r=r*35/100;g=g*35/100;b=b*35/100;}
                    pixel(left+xx,top+yy,0xff000000|(r<<16)|(g<<8)|b);
                }
                post+=post[1]+4;
            }
        }
        text(names[slot],cx-int(strlen(names[slot]))*2,cy+10,owned?0xffedf3f3:0xff718087);
    }
    const char* selected=wheelOwned(wheelSelected)?names[wheelSelected]:"UNAVAILABLE";
    text(selected,160-int(strlen(selected))*2,95,0xffffffff);
    text("RELEASE Q",142,106,0xff9acfdc);
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x)pixel(int(160+wheelX)+x,int(100+wheelY)+y,0xffffc580);
}
}
#endif
