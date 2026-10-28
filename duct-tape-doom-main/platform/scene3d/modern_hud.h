// Full-view HUD drawn into the existing transparent overlay. No world renderer changes.
#ifndef DOOM_MODERN_HUD_H
#define DOOM_MODERN_HUD_H
extern "C" { int ST_FaceIndex(void); int Arcade_MenuOpen(void); extern int arcade_active; }
extern "C" { const unsigned char *ST_HelmetFace(int index); }
namespace doom3d {
inline void modernHud(std::vector<unsigned>& out) {
    if(!worldPending||menuactive||automapactive||Arcade_MenuOpen())return;
    std::vector<unsigned> hud(SCREENWIDTH*SCREENHEIGHT,0);
    const auto& p=players[displayplayer];
    auto rect=[&](int x,int y,int w,int h,unsigned color){
        for(int yy=std::max(0,y);yy<std::min(SCREENHEIGHT,y+h);++yy)
            for(int xx=std::max(0,x);xx<std::min(SCREENWIDTH,x+w);++xx)hud[yy*SCREENWIDTH+xx]=color;
    };
    // Compact, neutral 3x5 lettering; values use twice the label size.
    const char* alphabet="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-/";
    const char* glyphs[]={"111101101101111","010110010010111","111001111100111","111001111001111","101101111001001","111100111001111","111100111101111","111001010010010","111101111101111","111101111001111",
        "010101111101101","110101110101110","111100100100111","110101101101110","111100110100111","111100110100100","111100101101111","101101111101101","111010010010111","001001001101111","101101110101101","100100100100111","101111111101101","110101101101101","111101101101111","111101111100100","111101101111001","110101110101101","111100111001111","111010010010010","101101101101111","101101101101010","101101111111101","101101010101101","101101010010010","111001010100111","000000111000000","001001010100100"};
    struct Label {std::string value;int x,y,size;unsigned color;bool fixed=false;};
    std::vector<Label> labels;
    auto text=[&](const std::string& value,int x,int y,int size,unsigned color){labels.push_back({value,x,y,size,color});};
    auto art=[&](const char* name,int x,int y,int maxW,int maxH){
        int lump=W_CheckNumForName((char*)name);if(lump<0)return;
        const auto& img=lumpImage(lump,false);if(img.width<=0||img.height<=0)return;
        const byte* palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
        float scale=std::min(float(maxW)/img.width,float(maxH)/img.height);
        int w=std::max(1,int(img.width*scale)),h=std::max(1,int(img.height*scale));
        for(int yy=0;yy<h;++yy)for(int xx=0;xx<w;++xx){int sx=std::min(img.width-1,int(xx/scale)),sy=std::min(img.height-1,int(yy/scale));size_t at=(sy*img.width+sx)*2;
            if(at+1<img.pixels.size()&&img.pixels[at+1]){const byte* rgb=palette+img.pixels[at]*3;rect(x+xx,y+yy,1,1,(unsigned(img.pixels[at+1])<<24)|(unsigned(gammatable[usegamma][rgb[0]])<<16)|(unsigned(gammatable[usegamma][rgb[1]])<<8)|gammatable[usegamma][rgb[2]]);}}
    };
    const bool tactical=settings.hudStyle==2;
    const unsigned white=0xffedf3f3,muted=0xff9aafb7,red=p.health<=25?0xffff5d4c:0xffeead72,blue=0xff6ed6dc,back=0xc0142028;
    int hx=12,ax=tactical?76:88;
    if(settings.hudFace==1){hx=52;ax=tactical?100:98;}
    rect(hx-4,164,tactical?42:40,31,back);rect(ax-4,164,40,31,back);rect(252,164,60,31,back);
    text("HEALTH",hx,167,1,muted);text(std::to_string(std::max(0,p.health)),hx,176,2,red);
    text("ARMOR",ax,167,1,muted);text(std::to_string(std::max(0,p.armorpoints)),ax,176,2,blue);
    auto bar=[&](int x,int value,unsigned color){rect(x,190,32,2,0xff344650);rect(x,190,std::clamp(value,0,100)*32/100,2,color);if(value>100)rect(x,193,std::min(value-100,100)*32/100,1,color);};
    bar(hx,p.health,red);bar(ax,p.armorpoints,blue);
    int ammo=weaponinfo[p.readyweapon].ammo;
    text("AMMO",256,167,1,muted);
    if(ammo>=0&&ammo<NUMAMMO){text(std::to_string(p.ammo[ammo]),256,176,2,white);text("/"+std::to_string(p.maxammo[ammo]),282,188,1,muted);}
    else text(p.readyweapon==wp_chainsaw?"SAW":"FIST",256,177,1,white);
    for(int key=0;key<NUMCARDS;++key)if(p.cards[key]){char name[9];snprintf(name,sizeof(name),"STKEYS%d",key);art(name,256+(key%3)*18,151-(key/3)*9,14,8);}
    // Fixed top-right statistics, independent of the bottom-panel scale/dock.
    const char* statNames[]={"KILLS","SECRETS","ITEMS"};
    int counts[]={0,0,0},totals[]={totalkills,totalsecret,totalitems};
    for(int i=0;i<MAXPLAYERS;++i)if(playeringame[i]){counts[0]+=players[i].killcount;counts[1]+=players[i].secretcount;counts[2]+=players[i].itemcount;}
    for(int row=0;row<3;++row){
        std::string value=std::string(statNames[row])+" "+std::to_string(counts[row])+"/"+std::to_string(totals[row]);
        unsigned color=totals[row]>0&&counts[row]>=totals[row]?0xff83edb0:0xffedf3f3;
        labels.push_back({value,312-int(value.size())*4,4+row*7,1,color,true});
    }
    if(tactical){int sec=leveltime/35;char timer[20];snprintf(timer,sizeof(timer),"%02d-%02d",sec/60,sec%60);labels.push_back({timer,12,4,1,white,true});}
    if(settings.hudFace){
        int face=std::clamp(ST_FaceIndex(),0,41),pain=face/8,variant=face%8;char name[9];
        if(face==40)snprintf(name,sizeof(name),"STFGOD0");else if(face==41)snprintf(name,sizeof(name),"STFDEAD0");
        else if(variant<3)snprintf(name,sizeof(name),"STFST%d%d",pain,variant);
        else if(variant<5)snprintf(name,sizeof(name),variant==3?"STFTR%d0":"STFTL%d0",pain);
        else snprintf(name,sizeof(name),variant==5?"STFOUCH%d":variant==6?"STFEVL%d":"STFKILL%d",pain);
        int x=settings.hudFace==2?146:10;
        const unsigned char* helmet=ST_HelmetFace(face);
        rect(x-(helmet?4:2),157,helmet?36:32,38,back);rect(x-(helmet?4:2),157,helmet?36:32,1,red);
        if(const unsigned char* patch=helmet){
            int width=patch[0]|patch[1]<<8,height=patch[2]|patch[3]<<8;
            const byte* pal=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
            // Preserve every source pixel: shrinking 32 to 28 drops eye rows/columns.
            int w=width,h=height;
            for(int xx=0;xx<w;++xx){int sx=xx*width/w;const auto* p=patch+8+sx*4;
                const auto* post=patch+(unsigned(p[0])|unsigned(p[1])<<8|unsigned(p[2])<<16|unsigned(p[3])<<24);
                while(post[0]!=255){for(int yy=0;yy<h;++yy){int sy=yy*height/h;if(sy<post[0]||sy>=post[0]+post[1])continue;
                    auto rgb=pal+post[3+sy-post[0]]*3;rect(x+(28-w)/2+xx,161+yy,1,1,0xff000000|unsigned(gammatable[usegamma][rgb[0]])<<16|unsigned(gammatable[usegamma][rgb[1]])<<8|gammatable[usegamma][rgb[2]]);}post+=post[1]+4;}
            }
        }else art(name,x,161,28,32);
    }
    if(!settings.hudStyle){
        std::fill(hud.begin(),hud.end(),0);
        labels.erase(std::remove_if(labels.begin(),labels.end(),[](const Label& l){return !l.fixed;}),labels.end());
    }
    // Scale each panel about its dock, so reducing size does not drag the
    // corner HUD toward the weapon. Centered layout moves the docks inward.
    const float scale=std::clamp(settings.hudScale/100.f,.5f,1.f);
    for(int group=0;group<3;++group)for(int y=0;y<SCREENHEIGHT;++y)for(int x=0;x<SCREENWIDTH;++x){
        bool topStats=y<30;
        float sx=x,sy=y;
        // Inverse mapping leaves no gaps when enlarging and samples crisply
        // when shrinking. The panel ranges do not overlap at any valid size.
        float anchor=group==0?8.f:group==1?160.f:312.f;
        // Leave a dedicated gap for the center face even at full size.
        float destination=settings.hudLayout==1?(group==0?18.f:group==1?160.f:242.f):anchor;
        sx=anchor+(x-destination)/scale;
        if(settings.hudLayout==2){
            if(y>=30&&y<100){sy=140+(y-38)/scale;topStats=false;}
            else {sy=10+(y-75)/scale;topStats=true;}
        }else sy=topStats?10+(y-10)/scale:195+(y-195)/scale;
        int ix=int(std::floor(sx)),iy=int(std::floor(sy));
        if(ix<0||ix>=SCREENWIDTH||iy<0||iy>=SCREENHEIGHT)continue;
        if((ix<136?0:ix<202?1:2)!=group)continue;
        if((topStats&&iy>=30)||(!topStats&&iy<140))continue;
        unsigned color=hud[iy*SCREENWIDTH+ix];
        if(scale<1.f){
            // Area filtering preserves thin glyph strokes at fractional sizes.
            // Point sampling discarded entire columns of the 3x5 lettering.
            float edgeX=sx+1.f/scale,edgeY=sy+1.f/scale;
            float alpha=0,redSum=0,greenSum=0,blueSum=0;
            for(int yy=iy;yy<int(std::ceil(edgeY))&&yy<SCREENHEIGHT;++yy)
                for(int xx=ix;xx<int(std::ceil(edgeX))&&xx<SCREENWIDTH;++xx){
                    if((xx<136?0:xx<202?1:2)!=group)continue;
                    float weight=(std::min(edgeX,float(xx+1))-std::max(sx,float(xx)))
                        *(std::min(edgeY,float(yy+1))-std::max(sy,float(yy)));
                    unsigned sample=hud[yy*SCREENWIDTH+xx];
                    float a=float(sample>>24)*weight;
                    alpha+=a;redSum+=float((sample>>16)&255)*a;
                    greenSum+=float((sample>>8)&255)*a;blueSum+=float(sample&255)*a;
                }
            color=alpha>0?(unsigned(std::clamp(alpha*scale*scale,0.f,255.f))<<24)
                |(unsigned(redSum/alpha)<<16)|(unsigned(greenSum/alpha)<<8)|unsigned(blueSum/alpha):0;
        }
        if(color>>24)out[y*SCREENWIDTH+x]=color;
    }
    // Rasterize lettering after layout, at whole pixel sizes. Scaling the
    // panels must never erase strokes from numbers or small labels.
    for(int yy=1;yy<25;++yy)for(int xx=222;xx<315;++xx){
        out[yy*SCREENWIDTH+xx]=0xb010141a;
    }
    for(const auto& label:labels){
        int group=label.x<136?0:label.x<202?1:2;
        float anchor=group==0?8.f:group==1?160.f:312.f;
        float destination=settings.hudLayout==1?(group==0?18.f:group==1?160.f:242.f):anchor;
        int x=int(std::round(destination+(label.x-anchor)*scale));
        auto mappedY=[&](int source){return int(std::round(settings.hudLayout==2
            ?(source<30?16+60*scale+(source-10)*scale:38+(source-140)*scale)
            :(source<30?10+(source-10)*scale:195+(source-195)*scale)));};
        int y=mappedY(label.y);
        int size=std::max(1,int(std::round(label.size*scale)));
        if(label.y>=176&&label.y<=177)y=std::max(y,mappedY(167)+6);
        if(label.y==188)y=std::min(y,mappedY(195)-5);
        if(label.y==18)y=std::max(y,mappedY(10)+6);
        if(label.fixed){x=label.x;y=label.y;size=label.size;}
        std::string value=label.value;
        if(scale<.7f){if(value=="HEALTH")value="HP";else if(value=="ARMOR")value="AR";}
        for(char c:value){
            const char* found=strchr(alphabet,c);
            if(found){const char* bits=glyphs[found-alphabet];
                for(int row=0;row<5;++row)for(int col=0;col<3;++col)if(bits[row*3+col]=='1')
                    for(int yy=0;yy<size;++yy)for(int xx=0;xx<size;++xx){
                        int px=x+col*size+xx,py=y+row*size+yy;
                        if(px>=0&&px<SCREENWIDTH&&py>=0&&py<SCREENHEIGHT)out[py*SCREENWIDTH+px]=label.color;
                    }
            }
            x+=4*size;
        }
    }
}
}
#endif
