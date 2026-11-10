#ifndef DOOM_KEY_NOTICE_H
#define DOOM_KEY_NOTICE_H
#include "../completion_notices.h"
extern "C" {
#include "../../linuxdoom-1.10/p_notices.h"
extern unsigned p_keypickup_serial[MAXPLAYERS];
extern int p_keypickup_card[MAXPLAYERS],p_keypickup_tic[MAXPLAYERS];
}
#include "notice_catalog.h"
namespace doom3d {
// Shared presentation for keys and level milestones.
static void drawNotice(std::vector<unsigned>& out,const std::string& title,
                       const std::string& subtitle,unsigned color,float t,float centerY,int style=0,bool left=false){
    // Use the IWAD's actual Doom message lettering, including its shaded bevel.
    auto glyph=[&](char c)->const Image* {
        if(c==' ')return nullptr;
        char name[9];snprintf(name,sizeof(name),"STCFN%03d",int((unsigned char)c));
        int lump=W_CheckNumForName(name);return lump<0?nullptr:&lumpImage(lump,false);
    };
    auto textWidth=[&](const std::string& value){
        int result=0;for(char c:value){const Image* img=glyph(c);result+=img?img->width:4;}return result;
    };

    int width=std::max(textWidth(title),textWidth(subtitle))+8;
    constexpr int height=28;
    std::vector<unsigned> art(width*height,0);
    const byte* palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    auto put=[&](int x,int y,unsigned c){if(x>=0&&x<width&&y>=0&&y<height)art[y*width+x]=c;};
    auto text=[&](const std::string& value,int y,unsigned color){
        int x=(width-textWidth(value))/2;
        for(char c:value){const Image* img=glyph(c);if(!img){x+=4;continue;}
            for(int yy=0;yy<img->height;++yy)for(int xx=0;xx<img->width;++xx){
                size_t at=(yy*img->width+xx)*2;
                if(at+1>=img->pixels.size()||!img->pixels[at+1])continue;
                const byte* rgb=palette+img->pixels[at]*3;
                unsigned shade=std::max({rgb[0],rgb[1],rgb[2]});
                unsigned tinted=(((color>>16)&255)*shade/255)<<16
                    |(((color>>8)&255)*shade/255)<<8|(color&255)*shade/255;
                put(x+xx,y+yy,(unsigned(img->pixels[at+1])<<24)|tinted);
            }
            x+=img->width;
        }
    };

    text(title,2,color);text(subtitle,17,0xdce4ee);
    float enter=std::clamp(t/.42f,0.f,1.f),exit=std::clamp((t-2.55f)/.65f,0.f,1.f);
    float ease=1-std::pow(1-enter,3.f),fade=enter*(1-exit*exit);
    // A short accent grows from the center; a highlight travels along it once.
    int line=int(26*ease);
    for(int x=width/2-line;x<=width/2+line;++x)put(x,13,0x90000000|color);
    if(t<.65f)put(width/2-line+int(2*line*std::clamp(t/.65f,0.f,1.f)),13,0xffffffff);
    float angle=((style==2?-12:-6)*(1-ease)+3*exit)*.01745329252f,c=std::cos(angle),s=std::sin(angle);
    float scale=1+.06f*std::sin(enter*3.14159265f),cx=SCREENWIDTH-8-(width+1)/2+42*(1-ease)+14*exit,cy=centerY-3*std::sin(enter*3.14159265f);
    if(left)cx=8+(width+1)/2-42*(1-ease)-14*exit;
    if(style==2&&t<.28f){cx+=std::sin(t*90)*2*(1-t/.28f);cy+=std::cos(t*70)*(1-t/.28f);}
    if(style==1)scale+=.025f*std::sin(t*12)*std::exp(-t*2);
    if(style==3&&t<.3f)fade*=.65f+.35f*std::abs(std::sin(t*55));
    for(int y=std::max(0,int(centerY)-30);y<std::min(SCREENHEIGHT,int(centerY)+31);++y)for(int x=0;x<SCREENWIDTH;++x){
        float dx=(x-cx)/scale,dy=(y-cy)*1.2f/scale;
        float u=dx*c+dy*s+width/2.f,v=(-dx*s+dy*c)/1.2f+height/2.f;
        int ix=int(std::round(u)),iy=int(std::round(v));
        if(ix<0||ix>=width||iy<0||iy>=height)continue;
        unsigned sample=art[iy*width+ix];
        float alpha=(sample>>24)/255.f*fade;
        float r=((sample>>16)&255)*alpha,g=((sample>>8)&255)*alpha,b=(sample&255)*alpha;
        if(alpha<=0)continue;
        unsigned old=out[y*SCREENWIDTH+x];float oa=(old>>24)/255.f*(1-alpha),total=alpha+oa;
        out[y*SCREENWIDTH+x]=(unsigned(std::min(255.f,total*255))<<24)|
            (unsigned((r+((old>>16)&255)*oa)/total)<<16)|(unsigned((g+((old>>8)&255)*oa)/total)<<8)|unsigned((b+(old&255)*oa)/total);
    }
}
// Presentation follows game time, so pausing also pauses the entrance/fade.
static void keyNotice(std::vector<unsigned>& out){
    struct Notice {std::string title,subtitle;unsigned color;int start=-1;int style=0;int priority=0;bool left=false;};
    static std::vector<Notice> notices;
    static doomnotice::Completion completion;
    static unsigned serial=0,level=~0u;
    static int viewer=-1,lastTime=-1,clock=0,lastTick=-1;
    if(gamestate!=GS_LEVEL&&gamestate!=GS_INTERMISSION&&gamestate!=GS_FINALE){notices.clear();completion.reset();lastTime=-1;return;}
    if(level!=r_levelserial||viewer!=displayplayer||leveltime<lastTime){
        level=r_levelserial;viewer=displayplayer;serial=p_keypickup_serial[viewer];
        notices.clear();completion.reset();
    }
    lastTime=leveltime;
    if(lastTick>=0&&!paused&&!menuactive)clock+=std::clamp(gametic-lastTick,0,4);
    lastTick=gametic;
    if(serial!=p_keypickup_serial[viewer]){
        serial=p_keypickup_serial[viewer];int key=p_keypickup_card[viewer];
        const unsigned colors[]={0x819cff,0xffdb64,0xff7666};
        const char* names[]={"BLUE","YELLOW","RED"};
        if(key>=0&&key<NUMCARDS)notices.push_back({std::string(names[key%3])+(key<3?" KEYCARD ACQUIRED":" SKULL KEY ACQUIRED"),std::array<const char*,3>{"A NEW PATH OPENS","ACCESS GRANTED","ANOTHER DOOR AWAITS"}[serial%3],colors[key%3]});
    }
    std::array<int,3> counts{};
    for(int i=0;i<MAXPLAYERS;++i)if(playeringame[i]){
        counts[0]+=players[i].killcount;counts[1]+=players[i].secretcount;counts[2]+=players[i].itemcount;
    }
    unsigned earned=gamestate==GS_LEVEL?completion.update(counts,{totalkills,totalsecret,totalitems}):0;
    for(int n=0;n<4;++n)if(earned&(1u<<n))P_Notice(viewer,N_KILLS+n,0);
    noticeevent_t event;
    while(P_PopNotice(viewer,&event)){
        if(event.kind==N_SECRET&&(earned&2))continue;
        const auto& text=noticeText[event.kind];
        std::string title=text.titles[event.variant],subtitle=text.subtitle;
        unsigned color=text.color;
        if(event.kind==N_WEAPON&&event.detail>=0&&event.detail<NUMWEAPONS){
            const std::string weapon=noticeWeapons[event.detail];
            title=event.variant==0?weapon+" ACQUIRED":event.variant==1?"NEW WEAPON: "+weapon:weapon+" READY";
            subtitle=event.detail==wp_bfg?std::array<const char*,3>{"BIG GUN. BIG PROBLEMS.","LET HELL TURN GREEN","MAKE EVERY CELL COUNT"}[event.variant]:text.titles[event.variant];
            color=weaponColors[event.detail];
        }
        int priority=event.kind==N_CYBER||event.kind==N_SPIDER||event.kind==N_PERFECT?2:event.kind==N_EXIT?1:0;
        notices.push_back({title,subtitle,color,-1,text.style,priority,event.kind==N_WEAPON});
    }
    // Major awards precede queued pickups without restarting visible notices.
    std::stable_sort(notices.begin(),notices.end(),[](const Notice&a,const Notice&b){
        if((a.start>=0)!=(b.start>=0))return a.start>=0;
        return a.start<0&&a.priority>b.priority;
    });
    notices.erase(std::remove_if(notices.begin(),notices.end(),[&](const Notice& n){return n.start>=0&&clock-n.start>=112;}),notices.end());
    // Keep overflow queued until a visible slot is free. This also prevents a
    // key pickup from replacing a completion message on the same frame.
    int count=std::min(5,int(notices.size()));
    for(int i=0;i<count;++i){
        auto& n=notices[i];if(n.start<0)n.start=clock;
        if(menuactive||automapactive)continue;
        float t=(clock-n.start+(paused?0:renderFraction))/35.f;
        if(M_CheckParm((char*)"-test-special-notices")||M_CheckParm((char*)"-test-completion-notices"))t=.8f;
        float centerY=94-(count-1)*14+i*28;
        drawNotice(out,n.title,n.subtitle,n.color,t,centerY,n.style,n.left);
    }
}
}
#endif
