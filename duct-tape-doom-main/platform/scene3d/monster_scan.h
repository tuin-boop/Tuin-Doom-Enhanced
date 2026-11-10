#pragma once
#include "../monster_scan.h"
#include "../../assets/arcade/tinybaby.h"
#include "../../assets/arcade/pda_cursor.h"
#include "../../assets/arcade/pda_equipment.h"
namespace doom3d {
std::vector<VoxelInstance> scanVoxels;
std::map<int,std::vector<Vertex>> scanSprites;
Uniforms scanCamera{};
float scanPopupAlpha=1,scanPopupSlide=0;
bool scanVisible=false,pdaActive=false;
float scanRect[4]={242,57,68,55},pdaViewport[4]={0,0,320,200},pdaCursor[2]={160,100};
GpuTextureRef pdaCursorTexture,scanPanelTexture;
bool scanPanelVisible=false;
static doomscan::Discovery<NUMMOBJTYPES+2> scanDiscovery;
static std::array<bool,NUMMOBJTYPES+2> pdaKnown{};
static int pdaGame=-1,pdaSelected=MT_PLAYER,pdaCategory=0,pdaScroll=0;
static unsigned pdaLevel=0;
static bool pdaDragging=false,pdaPaused=false;
static float pdaAngle=0;
struct ScanInfo {const char *name,*threat,*attack;int rating;};
static ScanInfo scanInfo(int type){
 switch(type){
 case MT_POSSESSED:return {"ZOMBIEMAN","LOW","RIFLE FIRE",1};
 case MT_SHOTGUY:return {"SHOTGUN GUY","MODERATE","SHOTGUN",2};
 case MT_CHAINGUY:return {"CHAINGUNNER","HIGH","RAPID FIRE",3};
 case MT_TROOP:return {"IMP","MODERATE","FIREBALLS",2};
 case MT_SERGEANT:return {"DEMON","MODERATE","MELEE",2};
 case MT_SHADOWS:return {"SPECTRE","HIGH","CLOAK / MELEE",3};
 case MT_HEAD:return {"CACODEMON","HIGH","AIR / PLASMA",3};
 case MT_SKULL:return {"LOST SOUL","MODERATE","CHARGE",2};
 case MT_KNIGHT:return {"HELL KNIGHT","HIGH","HELLFIRE",3};
 case MT_BRUISER:return {"BARON OF HELL","SEVERE","HELLFIRE",4};
 case MT_UNDEAD:return {"REVENANT","SEVERE","SEEKER ROCKET",4};
 case MT_FATSO:return {"MANCUBUS","SEVERE","FIRE VOLLEY",4};
 case MT_PAIN:return {"PAIN ELEMENTAL","SEVERE","SOUL SPAWNER",4};
 case MT_VILE:return {"ARCH-VILE","EXTREME","FIRE / REVIVE",5};
 case MT_BABY:return {"ARACHNOTRON","SEVERE","PLASMA",4};
 case MT_CYBORG:return {"CYBERDEMON","CRITICAL","ROCKET VOLLEY",6};
 case MT_SPIDER:return {"MASTERMIND","CRITICAL","CHAINGUN",6};
 case MT_WOLFSS:return {"WOLF SOLDIER","MODERATE","RAPID FIRE",2};
 default:return {"UNKNOWN ENTITY","UNKNOWN","UNCLASSIFIED",3};
 }
}

static const char* pdaMonsterLore(int type){switch(type){
case MT_POSSESSED:return "Once a human soldier, now serving the invasion. Familiar weapons remain deadly in corrupted hands.";
case MT_SHOTGUY:return "A fallen security trooper with a shotgun. His human reflexes are gone; his close-range firepower is not.";
case MT_CHAINGUY:return "A corrupted heavy gunner. Once the barrels spin, exposed corridors become killing grounds.";
case MT_TROOP:return "A clawed hunter that hurls burning projectiles. At close range, it abandons fire for teeth and talons.";
case MT_SERGEANT:return "A muscular predator built for the charge. It has no ranged attack, but little patience for distance.";
case MT_SHADOWS:return "A demon concealed by a wavering outline. Watch for movement in the dark before its jaws find you.";
case MT_HEAD:return "A floating, one-eyed horror. It drifts over obstacles and spits plasma from its enormous mouth.";
case MT_SKULL:return "A burning skull driven by a single impulse: charge. Its small size makes crowded rooms dangerous.";
case MT_KNIGHT:return "A towering hellspawn with burning green projectiles. Less resilient than a baron, but no less hostile.";
case MT_BRUISER:return "One of Hell's toughest bruisers. Its imposing frame conceals the speed of its lethal green attacks.";
case MT_UNDEAD:return "An armored skeleton fitted with missile launchers. Some rockets pursue their target around cover.";
case MT_FATSO:return "A walking weapons platform. Twin cannons sweep the battlefield with spreading volleys of fire.";
case MT_PAIN:return "A floating incubator for Lost Souls. Every moment it survives gives the invasion another attacker.";
case MT_VILE:return "A fire-wielding nightmare that raises fallen monsters. Break its line of sight before the flames erupt.";
case MT_BABY:return "A brain mounted on mechanical legs. Its plasma cannon turns open ground into a dangerous crossing.";
case MT_CYBORG:return "Demonic muscle fused with machinery. Its rocket volleys demand cover, distance, and constant movement.";
case MT_SPIDER:return "A colossal brain carried by a metal chassis. Its chaingun can punish even a brief moment in the open.";
case MT_WOLFSS:return "An armed soldier from a very different battlefield. Rapid fire makes this strange encounter dangerous.";
default:return "An unidentified hostile presence. Observe its attacks and keep an escape route open.";}}
struct PdaInfo {const char *name,*category,*description;int group;};
static PdaInfo pdaInfo(int type){
 if(type==NUMMOBJTYPES)return {"PISTOL","WEAPON","Your standard sidearm. Uses bullets. Controlled shots help conserve ammunition.",3};
 if(type==NUMMOBJTYPES+1)return {"FISTS","WEAPON","Close-range melee. Berserk greatly increases the force of each punch.",3};
 if(type==MT_PLAYER)return {"DOOMGUY","PERSONNEL","A lone marine on the front line of the invasion. When the bases fell silent, you kept fighting.",1};
 if(type==MT_BARREL)return {"EXPLOSIVE BARREL","OBJECT","Abandoned industrial waste, still dangerously volatile. One blast can turn a storage area into a chain reaction.",5};
 if(type<0||type>=NUMMOBJTYPES)return {"UNKNOWN","OBJECT","Unidentified object.",5};
 if((mobjinfo[type].flags&MF_COUNTKILL)||type==MT_SKULL){auto s=scanInfo(type);return {s.name,"MONSTER",pdaMonsterLore(type),2};}
 switch(type){
 case MT_MISC0:return {"GREEN ARMOR","ITEM","Basic armor. Absorbs part of incoming damage until depleted.",4};
 case MT_MISC1:return {"BLUE ARMOR","ITEM","Heavy armor. Provides stronger protection against incoming damage.",4};
 case MT_MISC2:return {"HEALTH BONUS","ITEM","A small health boost. Can raise health above its normal limit.",4};
 case MT_MISC3:return {"ARMOR BONUS","ITEM","Adds a little armor, even beyond the normal armor pickup limit.",4};
 case MT_MISC10:return {"STIMPACK","ITEM","Restores a small amount of health.",4};
 case MT_MISC11:return {"MEDIKIT","ITEM","Restores more health than a stimpack. Save it until needed.",4};
 case MT_MISC12:return {"SOULSPHERE","POWERUP","An otherworldly sphere pulsing with stolen vitality. Its power can push your health beyond human limits.",6};
 case MT_MEGA:return {"MEGASPHERE","POWERUP","Restores health and armor to 200 percent.",6};
 case MT_INV:return {"INVULNERABILITY","POWERUP","Temporary protection from most damage. Use the limited time wisely.",6};
 case MT_INS:return {"PARTIAL INVISIBILITY","POWERUP","Disrupts enemy aim for a limited time.",6};
 case MT_MISC13:return {"BERSERK","POWERUP","Restores health and greatly strengthens fist attacks for this level.",6};
 case MT_MISC14:return {"RADIATION SUIT","POWERUP","Temporary protection from hazardous floors.",6};
 case MT_MISC15:return {"COMPUTER MAP","POWERUP","Reveals unexplored map geometry on the automap.",6};
 case MT_MISC16:return {"LIGHT AMPLIFIER","POWERUP","Temporarily brightens dark areas.",6};
 case MT_CLIP:return {"BULLET CLIP","AMMO","Bullets for the pistol and chaingun.",4};
 case MT_MISC17:return {"BULLET BOX","AMMO","A larger supply of bullets.",4};
 case MT_MISC18:return {"ROCKET","AMMO","Explosive ammunition for the rocket launcher.",4};
 case MT_MISC19:return {"ROCKET BOX","AMMO","A supply of rockets. Watch your distance from explosions.",4};
 case MT_MISC20:return {"ENERGY CELL","AMMO","Energy ammunition shared by plasma and BFG weapons.",4};
 case MT_MISC21:return {"CELL PACK","AMMO","A large reserve of energy ammunition.",4};
 case MT_MISC22:return {"SHELLS","AMMO","Shells for both shotguns.",4};
 case MT_MISC23:return {"SHELL BOX","AMMO","A larger reserve of shotgun shells.",4};
 case MT_MISC24:return {"BACKPACK","ITEM","Expands ammunition capacity and supplies ammunition.",4};
 case MT_SHOTGUN:return {"SHOTGUN","WEAPON","A rugged workhorse in the fallen bases. Its pellet spread rewards close encounters and a steady aim.",3};
 case MT_SUPERSHOTGUN:return {"SUPER SHOTGUN","WEAPON","Two shells per blast. Devastating at close range, with a slower reload.",3};
 case MT_CHAINGUN:return {"CHAINGUN","WEAPON","Sustained bullet fire. Short bursts help control distant targets.",3};
 case MT_MISC25:return {"BFG 9000","WEAPON","The BFG 9000 turns stored energy into overwhelming force. Its blast and follow-up strikes can tear through a crowd.",3};
 case MT_MISC26:return {"CHAINSAW","WEAPON","An industrial tool repurposed for survival. The screaming chain needs no ammunition, only dangerously close contact.",3};
 case MT_MISC27:return {"ROCKET LAUNCHER","WEAPON","Portable demolition with a military purpose. The blast punishes clustered enemies and careless operators alike.",3};
 case MT_MISC28:return {"PLASMA RIFLE","WEAPON","A stream of concentrated energy from UAC hardware. Sustained fire can overwhelm even the invasion's toughest fighters.",3};
 case MT_MISC4:return {"BLUE KEYCARD","KEY","Opens doors requiring the blue key.",4};
 case MT_MISC5:return {"RED KEYCARD","KEY","Opens doors requiring the red key.",4};
 case MT_MISC6:return {"YELLOW KEYCARD","KEY","Opens doors requiring the yellow key.",4};
 case MT_MISC7:return {"YELLOW SKULL","KEY","A skull key for yellow locks.",4};
 case MT_MISC8:return {"RED SKULL","KEY","A skull key for red locks.",4};
 case MT_MISC9:return {"BLUE SKULL","KEY","A skull key for blue locks.",4};
 default:return {"FIELD PICKUP","ITEM","Collected equipment.",4};
 }
}
static void pdaSave(){if(M_CheckParm((char*)"-rendercheck"))return;char path[64];snprintf(path,sizeof(path),"pda-discoveries-%d.dat",pdaGame);FILE *f=fopen(path,"wb");if(!f)return;fputs("TUINPDA1\n",f);for(int i=0;i<NUMMOBJTYPES+2;++i)if(pdaKnown[i])fprintf(f,"%d\n",i);fclose(f);}
static void pdaSync(){
 if(pdaGame==int(gamemission))return;pdaGame=int(gamemission);pdaKnown.fill(false);pdaKnown[MT_PLAYER]=true;pdaKnown[NUMMOBJTYPES]=true;pdaKnown[NUMMOBJTYPES+1]=true;
 char path[64];snprintf(path,sizeof(path),"pda-discoveries-%d.dat",pdaGame);FILE *f=fopen(path,"rb");if(f){char tag[32];if(fgets(tag,sizeof(tag),f)&&!strcmp(tag,"TUINPDA1\n")){int id;for(int n=0;n<NUMMOBJTYPES+2&&fscanf(f,"%d",&id)==1;++n)if(id>=0&&id<NUMMOBJTYPES+2)pdaKnown[id]=true;}fclose(f);}
}
static void pdaDiscover(int type,bool popup){pdaSync();if(type<0||type>=NUMMOBJTYPES)return;if(!pdaKnown[type]){pdaKnown[type]=true;pdaSave();}scanDiscovery.sync(r_levelserial,displayplayer,leveltime);if(popup)scanDiscovery.hit(type,settings.monsterScanner!=0);}
void monsterScanHit(mobj_t *target,mobj_t *source,int damage){
 if(!target||!source||damage<=0||target->health<=0||target->player||source->player!=&players[displayplayer])return;
 if((target->flags&MF_COUNTKILL)||target->type==MT_SKULL||target->type==MT_BARREL)pdaDiscover(target->type,true);
}
static void pdaPickup(player_t *player,mobj_t *item){if(player==&players[consoleplayer]&&item)pdaDiscover(item->type,pdaInfo(item->type).group==3);}
static void pdaClose(){pdaActive=false;pdaDragging=false;if(pdaPaused){paused=false;pdaPaused=false;}}
static void pdaToggle(){
 if(pdaActive){pdaClose();return;}
 // Input arrives before the renderer marks this frame as having a world.
 if(!settings.pda||gamestate!=GS_LEVEL||menuactive||paused||r_photoactive||demoplayback||demorecording||director_watch)return;
 pdaSync();pdaActive=true;pdaLevel=r_levelserial;wheelActive=false;pdaAngle=0;pdaDragging=false;
 if(!netgame){paused=true;pdaPaused=true;}
}
static std::vector<int> pdaList(){std::vector<int> v;for(int i=0;i<NUMMOBJTYPES+2;++i)if(pdaKnown[i]&&(!pdaCategory||pdaInfo(i).group==pdaCategory))v.push_back(i);return v;}
static void pdaPixel(std::vector<unsigned>&out,int x,int y,unsigned color){if(x>=0&&x<320&&y>=0&&y<200)out[y*320+x]=color;}
static void pdaRect(std::vector<unsigned>&out,int x,int y,int w,int h,unsigned c){for(int yy=y;yy<y+h;++yy)for(int xx=x;xx<x+w;++xx)pdaPixel(out,xx,yy,c);}
static void pdaText(std::vector<unsigned>&out,int x,int y,const char* s,unsigned color,int maxX=315){
 for(;*s;++s){int c=(unsigned char)*s-32;if(c<0||c>=95)continue;int w=baby_widths[c];if(x+w>maxX)return;for(int yy=0;yy<6;++yy)for(int xx=0;xx<w;++xx){int a=baby_pixels[baby_offsets[c]+yy*w+xx];if(a){unsigned k=a==2?255:150;unsigned tint=(((color>>16)&255)*k/255)<<16|(((color>>8)&255)*k/255)<<8|(color&255)*k/255;pdaPixel(out,x+xx,y+yy,0xff000000|tint);}}x+=w;}
}
static void pdaWrap(std::vector<unsigned>&out,int x,int y,int width,const char *s){std::string line,word;auto flush=[&](){pdaText(out,x,y,line.c_str(),0xffb9d1c7,x+width);y+=9;line.clear();};for(;;++s){if(*s==' '||!*s){if((line.size()+word.size()+1)*4>size_t(width)&&!line.empty())flush();if(!line.empty())line+=' ';line+=word;word.clear();if(!*s){flush();break;}}else word+=*s;}}
// Supplied artwork for entries without a world pickup model. Preserve alpha and aspect ratio.
static void pdaEquipmentArt(std::vector<unsigned>&out,int type){
 const unsigned *pixels=type==NUMMOBJTYPES?pdaPistolPixels:pdaFistsPixels;
 int w=type==NUMMOBJTYPES?pdaPistolWidth:pdaFistsWidth,h=type==NUMMOBJTYPES?pdaPistolHeight:pdaFistsHeight;
 int left=w,top=h,right=0,bottom=0;
 for(int y=0;y<h;++y)for(int x=0;x<w;++x)if(pixels[y*w+x]>>24){left=std::min(left,x);top=std::min(top,y);right=std::max(right,x+1);bottom=std::max(bottom,y+1);}
 if(right<=left||bottom<=top)return;
 float scale=std::min(90.f/(right-left),61.f/(bottom-top));int dw=std::max(1,int((right-left)*scale)),dh=std::max(1,int((bottom-top)*scale));
 int ox=172+(100-dw)/2,oy=69+(69-dh)/2;
 for(int y=0;y<dh;++y)for(int x=0;x<dw;++x){unsigned c=pixels[(top+y*(bottom-top)/dh)*w+left+x*(right-left)/dw];unsigned a=c>>24;if(!a)continue;unsigned bg=out[(oy+y)*320+ox+x],rgb=0;for(int shift=0;shift<24;shift+=8)rgb|=(( ((c>>shift)&255)*a+((bg>>shift)&255)*(255-a))/255)<<shift;pdaPixel(out,ox+x,oy+y,0xff000000u|rgb);}
}
static void scanPreview(int type,bool normal,float angle){
 scanCamera={};scanCamera.eye[1]=-200;scanCamera.right[0]=1;scanCamera.forward[1]=1;scanCamera.up[2]=1;
 scanCamera.projection[0]=1.7f;scanCamera.projection[1]=2.05f;scanCamera.projection[2]=1;scanCamera.projection[3]=2048;scanCamera.effects[0]=float(SDL_GetTicks()/1000.0);
 int state=type>=NUMMOBJTYPES?S_NULL:mobjinfo[type].spawnstate;int sprite,frame=0;if(type==NUMMOBJTYPES)sprite=SPR_PISG;else if(type==NUMMOBJTYPES+1)sprite=SPR_PUNG;else{if(state<=0||state>=NUMSTATES)return;sprite=states[state].sprite;frame=states[state].frame&FF_FRAMEMASK;}
 if(sprite<0||sprite>=numsprites||frame>=sprites[sprite].numframes)return;
 const unsigned shadow=normal&&type<NUMMOBJTYPES&&(mobjinfo[type].flags&MF_SHADOW)?MODE_FUZZ:0;
 unsigned mode=MODE_SPRITE|0x40000000u|(normal?0x20000000u:0)|shadow;
 if(prepareVoxelScan(sprite,frame,angle)){if(normal)scanVoxels.back().shading[0]|=0x20000000u|shadow;}
 else {auto &sf=sprites[sprite].spriteframes[frame];int rot=sf.rotate?(int(angle*4/doomPi)%8+8)%8:0;int lump=firstspritelump+sf.lump[rot];auto &im=lumpImage(lump,false);
 scanCamera.eye[1]=-std::max(im.width,im.height)*1.2f-10;float w=im.width*.5f,h=im.height*.5f,u0=sf.flip[rot]?float(im.width):0,u1=sf.flip[rot]?0:float(im.width);
 quad(scanSprites[-1-lump],Vertex{-w,0,h,u0,0,1,mode},Vertex{w,0,h,u1,0,1,mode},Vertex{w,0,-h,u1,float(im.height),1,mode},Vertex{-w,0,-h,u0,float(im.height),1,mode});}
 scanVisible=true;
}
static void monsterScanPanel(std::vector<unsigned>&overlay){
 scanPanelVisible=false;
 scanVisible=false;scanVoxels.clear();scanSprites.clear();pdaSync();
 if(worldPending&&gamestate==GS_LEVEL){const int types[NUMWEAPONS]={-1,-1,MT_SHOTGUN,MT_CHAINGUN,MT_MISC27,MT_MISC28,MT_MISC25,MT_MISC26,MT_SUPERSHOTGUN};for(int w=0;w<NUMWEAPONS;++w)if(types[w]>=0&&players[consoleplayer].weaponowned[w]&&!pdaKnown[types[w]])pdaDiscover(types[w],true);}
 scanDiscovery.sync(r_levelserial,displayplayer,leveltime);int type=scanDiscovery.update(leveltime,settings.monsterScanner!=0);
 int test=M_CheckParm((char*)"-renderscan");if(test&&test+1<myargc&&settings.monsterScanner)type=std::clamp(atoi(myargv[test+1]),0,NUMMOBJTYPES-1);
 if(pdaActive||type<0||!worldPending||menuactive||automapactive||r_photoactive||director_watch||(director_demo&&demoplayback))return;
 std::vector<unsigned> out(320*200,0);
 auto info=pdaInfo(type);auto threat=scanInfo(type);
 float age=test?14.f:float(leveltime-scanDiscovery.start);
 float enter=std::clamp(age/7.f,0.f,1.f);enter=1-(1-enter)*(1-enter)*(1-enter);
 scanPopupSlide=(1-enter)*120.f;scanPopupAlpha=std::clamp((126.f-age)/14.f,0.f,1.f);
 pdaRect(out,200,76,116,42,0xf0091712);pdaRect(out,200,76,2,42,0xff50ee98);
 scanRect[0]=205;scanRect[1]=81;scanRect[2]=38;scanRect[3]=32;scanPreview(type,false,float(SDL_GetTicks()/1000.0)*.9f);
 pdaText(out,247,82,info.group==3?"NEW WEAPON":"NEW CONTACT",0xff70ffae,314);
 pdaText(out,247,92,info.name,0xffe0f7eb,314);
 char detail[80];snprintf(detail,sizeof(detail),info.group==2?"THREAT: %s":"%s",info.group==2?threat.threat:info.category);
 pdaText(out,247,104,detail,0xffa6cdb8,314);
 if(!scanPanelTexture)scanPanelTexture=gpuCreateTexture(GpuFormat::RGBA8,320,200,out.data(),320*4);
 else gpuUpdateTexture(scanPanelTexture,GpuFormat::RGBA8,{{0,0,320,200}},out.data(),320*4);
 scanPanelVisible=true;
}
static void pdaRunChecks();
static void pdaDraw(std::vector<unsigned>&out){
 if(worldPending&&gamestate==GS_LEVEL)pdaRunChecks();
 if(M_CheckParm((char*)"-renderpda")&&worldPending){pdaActive=true;pdaLevel=r_levelserial;int a=M_CheckParm((char*)"-renderpda");if(a+1<myargc){pdaSelected=std::clamp(atoi(myargv[a+1]),0,NUMMOBJTYPES+1);pdaKnown[pdaSelected]=true;}}
 if(!pdaActive)return;if(!settings.pda||gamestate!=GS_LEVEL||pdaLevel!=r_levelserial||menuactive){pdaClose();return;}
 scanVoxels.clear();scanSprites.clear();scanVisible=false;
 pdaRect(out,10,14,300,174,0xff081411);pdaRect(out,10,14,300,2,0xff48d99b);
 pdaText(out,18,21,"UAC // FIELD DATABASE",0xff8cffe0);pdaText(out,260,21,"P / ESC: EXIT",0xff7ab49e);
 const char *cats[]={"ALL","YOU","MONSTERS","WEAPONS","ITEMS","OBJECTS","POWERUPS"};int cx=18;for(int i=0;i<7;++i){int w=int(strlen(cats[i]))*4+7;pdaRect(out,cx,33,w,11,i==pdaCategory?0xff246750:0xff11271e);pdaText(out,cx+3,36,cats[i],0xffc0ebd8);cx+=w+2;}
 auto list=pdaList();if(std::find(list.begin(),list.end(),pdaSelected)==list.end())pdaSelected=list.empty()?-1:list.front();pdaScroll=std::clamp(pdaScroll,0,std::max(0,int(list.size())-12));
 pdaRect(out,18,49,112,126,0xff0c2019);for(int row=0;row<12&&row+pdaScroll<int(list.size());++row){int id=list[row+pdaScroll],y=51+row*10;if(id==pdaSelected)pdaRect(out,19,y-1,110,10,0xff235641);pdaText(out,22,y+1,pdaInfo(id).name,id==pdaSelected?0xffb4ffd9:0xff96b5a6,128);}
 if(list.empty())pdaText(out,23,60,"NO DISCOVERIES YET",0xff739387,128);
 if(pdaSelected>=0){auto info=pdaInfo(pdaSelected);pdaText(out,141,50,info.name,0xffccffe6,303);pdaText(out,141,59,info.category,0xff6aab8e,303);
 scanRect[0]=172;scanRect[1]=69;scanRect[2]=100;scanRect[3]=69;if(pdaSelected>=NUMMOBJTYPES)pdaEquipmentArt(out,pdaSelected);else scanPreview(pdaSelected,true,pdaAngle);
 if(info.group==2){char details[180];auto m=scanInfo(pdaSelected);snprintf(details,sizeof(details),"%s / HP %d",m.threat,mobjinfo[pdaSelected].spawnhealth);pdaText(out,141,139,details,0xff82b99e,303);pdaWrap(out,141,148,158,info.description);}else pdaWrap(out,141,143,158,info.description);
 }
 pdaText(out,18,179,"SCROLL: LIST   CLICK: SELECT   DRAG PREVIEW: ROTATE",0xff75af97,303);
 if(!pdaCursorTexture)pdaCursorTexture=gpuCreateTexture(GpuFormat::RGBA8,pdaCursorWidth,pdaCursorHeight,pdaCursorPixels,pdaCursorWidth*4);
}
static void pdaKey(int key){if(key==SDL_SCANCODE_P||key==SDL_SCANCODE_ESCAPE){pdaClose();return;}auto list=pdaList();if(list.empty())return;auto it=std::find(list.begin(),list.end(),pdaSelected);int row=it==list.end()?0:int(it-list.begin());if(key==SDL_SCANCODE_UP)--row;if(key==SDL_SCANCODE_DOWN)++row;row=std::clamp(row,0,int(list.size())-1);pdaSelected=list[row];if(row<pdaScroll)pdaScroll=row;if(row>=pdaScroll+12)pdaScroll=row-11;}
static void pdaMouse(float x,float y,int action){float previous=pdaCursor[0];pdaCursor[0]=std::clamp((x-pdaViewport[0])*320/pdaViewport[2],0.f,319.f);pdaCursor[1]=std::clamp((y-pdaViewport[1])*200/pdaViewport[3],0.f,199.f);if(action==2){pdaDragging=false;return;}if(action==0){if(pdaDragging)pdaAngle+=(pdaCursor[0]-previous)*.045f;return;}
 x=pdaCursor[0];y=pdaCursor[1];if(x>=260&&y<30){pdaClose();return;}if(y>=33&&y<45){const char *cats[]={"ALL","YOU","MONSTERS","WEAPONS","ITEMS","OBJECTS","POWERUPS"};int cx=18;for(int i=0;i<7;++i){int w=int(strlen(cats[i]))*4+7;if(x>=cx&&x<cx+w){pdaCategory=i;pdaScroll=0;pdaAngle=0;return;}cx+=w+2;}}
 if(x>=18&&x<130&&y>=50&&y<171){auto list=pdaList();int row=(int(y)-50)/10+pdaScroll;if(row>=0&&row<int(list.size())){pdaSelected=list[row];pdaAngle=0;}}
 if(x>=scanRect[0]&&x<scanRect[0]+scanRect[2]&&y>=scanRect[1]&&y<scanRect[1]+scanRect[3])pdaDragging=true;
}
// Bounded integration checks run only with the explicit render-test flag.
static void pdaRunChecks(){
 static bool ran=false;if(ran||!M_CheckParm((char*)"-pdacheck")||!M_CheckParm((char*)"-rendercheck"))return;ran=true;
 auto known=pdaKnown;auto discovery=scanDiscovery;int popup=settings.monsterScanner,enabled=settings.pda;auto wasNet=netgame;
 auto check=[](bool ok,const char *why){if(!ok)I_Error("PDA regression: %s",why);};
 settings.pda=1;settings.monsterScanner=0;pdaKnown.fill(false);pdaKnown[MT_PLAYER]=true;scanDiscovery={};
 mobj_t target={},source={},item={};source.player=&players[displayplayer];target.type=MT_TROOP;target.health=60;target.flags=MF_COUNTKILL;
 monsterScanHit(&target,&source,1);check(pdaKnown[MT_TROOP],"first player hit unlocks monster");check(scanDiscovery.pending.empty(),"default-off popup stays hidden");
 check(!pdaKnown[MT_CYBORG],"unencountered monster stays hidden");
 target.type=MT_BARREL;target.flags=0;monsterScanHit(&target,&source,1);check(pdaKnown[MT_BARREL],"barrel unlocks");
 settings.monsterScanner=1;item.type=MT_SHOTGUN;pdaPickup(&players[consoleplayer],&item);pdaPickup(&players[consoleplayer],&item);check(pdaKnown[MT_SHOTGUN]&&scanDiscovery.pending.size()==1,"weapon unlock queues only once");
 item.type=MT_MISC3;pdaPickup(&players[consoleplayer],&item);check(pdaKnown[MT_MISC3],"armor bonus unlocks");
 item.type=MT_INV;pdaPickup(&players[consoleplayer],&item);check(pdaKnown[MT_INV],"powerup unlocks");
 pdaCategory=3;auto list=pdaList();check(list.size()==1&&list[0]==MT_SHOTGUN,"category only shows discovered weapons");
 bool hadWorld=worldPending;worldPending=false;
 netgame=false;pdaToggle();check(pdaActive&&paused,"single-player opens before world rendering");
 scanRect[0]=172;scanRect[1]=69;scanRect[2]=100;scanRect[3]=69;
 auto mouse=[](float x,float y,int action){pdaMouse(pdaViewport[0]+x*pdaViewport[2]/320,pdaViewport[1]+y*pdaViewport[3]/200,action);};
 mouse(200,90,1);float angle=pdaAngle;mouse(220,90,0);mouse(220,90,2);check(pdaAngle>angle&&!pdaDragging,"left drag rotates and release ends drag");
 pdaKey(SDL_SCANCODE_ESCAPE);check(!pdaActive&&!paused,"escape restores single-player");
 I_Render3DWheelKey(1);I_Render3DWheelMove(340,0);I_Render3DWheelKey(0);check(pdaActive&&paused&&!wheelActive,"weapon wheel opens PDA before rendering");pdaClose();
 netgame=true;pdaToggle();check(pdaActive&&!paused,"multiplayer remains live");pdaClose();netgame=wasNet;worldPending=hadWorld;
 pdaKnown=known;scanDiscovery=discovery;settings.monsterScanner=popup;settings.pda=enabled;pdaCategory=0;pdaSelected=MT_PLAYER;pdaScroll=0;pdaAngle=0;
 fprintf(stderr,"PDA integration: 13 checks passed (discovery, hidden entries, pickups, rotation, pause and live multiplayer).\n");
}

}
