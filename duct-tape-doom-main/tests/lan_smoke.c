/* Two or more processes exercise the real Doom loop and LAN transport. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "doomdef.h"
#include "doomstat.h"
#include "d_main.h"
#include "d_event.h"
#include "m_argv.h"
#include "i_system.h"
#include "p_local.h"
#include "r_state.h"
#include "p_fluid.h"
#include "i_lan.h"
#include "m_menu.h"
#include "v_video.h"
#include "w_wad.h"
#include "z_zone.h"
extern int prndindex;
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
static FILE *traceFile;
static int previous=-1;
static void featureProbe(void){
 static int done;if(done||!getenv("LAN_FEATURES")||!lan_session)return;done=1;
 if(!playeringame[1]||!players[1].mo){done=0;return;}
 mobj_t *target=players[1].mo,*source=players[0].mo;
 mobj_t savedMob=*target;player_t savedPlayer=players[1];
 int savedRng=prndindex,savedRule=lan_friendlyfire;
 target->health=players[1].health=100;players[1].armortype=0;players[1].cheats=0;players[1].powers[pw_invulnerability]=0;
 target->momx=target->momy=target->momz=0;
 lan_friendlyfire=0;P_DamageMobj(target,source,source,10);
 if(target->health!=100||players[1].health!=100||target->momx||target->momy)I_Error("Friendly fire off failed");
 lan_friendlyfire=1;P_DamageMobj(target,source,source,10);
 if(target->health!=90||players[1].health!=90)I_Error("Friendly fire on failed");
 lan_friendlyfire=0;P_DamageMobj(target,target,target,10);
 if(target->health!=80)I_Error("Self damage incorrectly blocked");
 *target=savedMob;players[1]=savedPlayer;lan_friendlyfire=savedRule;prndindex=savedRng;
 printf("PASS friendly fire off/on and self damage\n");
}
static boolean replacePuff;
static boolean testImpact(sector_t *s,boolean ceiling,fixed_t x,fixed_t y,fixed_t z){return replacePuff;}
static void impactTest(void){
 static int done;if(done)return;done=1;
 boolean (*saved)(sector_t*,boolean,fixed_t,fixed_t,fixed_t)=R_PlaneImpact;
 int rng=prndindex,results[2],counts[2];
 R_PlaneImpact=testImpact;
 for(int mode=0;mode<2;mode++){
  thinker_t *tail=thinkercap.prev;replacePuff=mode;prndindex=rng;
  P_LineAttack(players[0].mo,players[0].mo->angle,1024*FRACUNIT,-FRACUNIT*4,5);
  results[mode]=prndindex;counts[mode]=0;
  while(tail->next!=&thinkercap){thinker_t *t=tail->next;counts[mode]++;P_RemoveMobj((mobj_t*)t);/* delayed deletion: unlink and free this test-only puff now */t->prev->next=t->next;t->next->prev=t->prev;Z_Free(t);}
 }
 prndindex=rng;R_PlaneImpact=saved;
 if(results[0]!=results[1]||counts[0]!=counts[1]||counts[0]==0)I_Error("LAN impact presentation altered simulation");
 printf("PASS LAN floor effects preserve RNG and thinker count (%d)\n",counts[0]);
}
static uint32_t mix(uint32_t h,int n){return (h^(uint32_t)n)*16777619u;}
static void key(int type,int code){event_t e;memset(&e,0,sizeof(e));e.type=type;e.data1=code;D_PostEvent(&e);}
static void menuKey(int code){event_t e={ev_keydown,0,0,0};e.data1=code;M_Responder(&e);}
static void captureMenu(void){
 const char *path=getenv("LAN_MENU_CAPTURE");if(!path)return;
 if(getenv("LAN_BROWSER")){
  extern void I_Render3DSettingsDraw(unsigned*);unsigned *pixels=calloc(640*400,sizeof(unsigned));I_Render3DSettingsDraw(pixels);
  FILE *f=fopen(path,"wb");if(!f)I_Error("Cannot capture styled LAN menu");fprintf(f,"P6\n640 400\n255\n");
  for(int i=0;i<640*400;i++){unsigned c=pixels[i];unsigned char rgb[3]={c>>16,c>>8,c};fwrite(rgb,1,3,f);}fclose(f);free(pixels);return;
 }
 M_Drawer();const byte *pal=W_CacheLumpName("PLAYPAL",PU_CACHE);FILE *f=fopen(path,"wb");if(!f)I_Error("Cannot capture LAN menu");
 fprintf(f,"P6\n%d %d\n255\n",SCREENWIDTH,SCREENHEIGHT);
 for(int i=0;i<SCREENWIDTH*SCREENHEIGHT;i++)fwrite(pal+screens[0][i]*3,1,3,f);
 fclose(f);
}
void I_TestFrame(void){
 if(getenv("LAN_INGAME")){
  static int opened,requested;
  if(!opened&&gamestate==GS_LEVEL&&(!getenv("LAN_LATE")||!atoi(getenv("LAN_ROLE"))||I_GetTime()>70*atoi(getenv("LAN_ROLE")))){
   extern void M_OpenLanMenu(void);opened=1;M_OpenLanMenu();
   if(getenv("LAN_FEATURES")){
    menuKey(KEY_UPARROW);menuKey(KEY_ENTER);
    for(int c=0;c<atoi(getenv("LAN_ROLE"));c++)menuKey(KEY_RIGHTARROW);
    if(lan_color!=atoi(getenv("LAN_ROLE")))I_Error("Character menu colour failed");
    menuKey(KEY_ESCAPE);menuKey(KEY_DOWNARROW);
   }
   captureMenu();
   if(atoi(getenv("LAN_ROLE"))&&getenv("LAN_BROWSER")){menuKey(KEY_DOWNARROW);menuKey(KEY_ENTER);}
   else if(atoi(getenv("LAN_ROLE"))){menuKey(KEY_DOWNARROW);menuKey(KEY_DOWNARROW);menuKey(KEY_ENTER);for(int i=0;i<16;i++)menuKey(KEY_BACKSPACE);for(const char *s="127.0.0.1";*s;s++)menuKey(*s);menuKey(KEY_ENTER);}
   else menuKey(KEY_ENTER);
   if(!lan_lobby&&!lan_browse)I_Error("In-game lobby: %s",lan_status);
  }
  if(lan_browse&&lan_game_count){
   if(!lan_games[0].compatible)I_Error("Discovery content match failed");
   if(getenv("LAN_LATE")&&!lan_games[0].running)I_Error("Expected a running match in browser");
   printf("PASS discovered %s / %s / running=%d\n",lan_games[0].name,lan_games[0].address,lan_games[0].running);
   captureMenu();menuKey(KEY_ENTER);
  }
  if(lan_host&&!requested&&(getenv("LAN_LATE")||lan_count==atoi(getenv("LAN_COUNT")))){requested=1;for(int i=0;i<6;i++)menuKey(KEY_DOWNARROW);menuKey(KEY_ENTER);}
  if(!netgame)return;
 }
 if(gamestate!=GS_LEVEL||!players[consoleplayer].mo||leveltime==previous)return;
 previous=leveltime;
 if(!lan_session)impactTest();
 if(lan_session&&getenv("LAN_FEATURES")){
  if(!lan_catchingup&&leveltime>350)featureProbe();
  freeaim=true;tourpitch=(atoi(getenv("LAN_ROLE"))%2?-.35f:.25f);
  static int photoTest,wheelTest;
  extern int I_PhotoKey(int);extern void I_Render3DWheelKey(int);extern void I_Render3DWheelMove(float,float);
  if(!photoTest&&!lan_catchingup&&leveltime>=400){photoTest=leveltime;I_PhotoKey(SDL_SCANCODE_F8);if(!r_photoactive)I_Error("Live photo did not open");}
  if(photoTest>0&&leveltime>=photoTest+20){I_PhotoKey(SDL_SCANCODE_F8);photoTest=-1;printf("PASS live photo world advances\n");}
  if(!wheelTest&&!lan_catchingup&&leveltime>=440){wheelTest=leveltime;I_Render3DWheelKey(1);I_Render3DWheelMove(0,-160);}
  if(wheelTest>0&&leveltime>=wheelTest+20){I_Render3DWheelKey(0);wheelTest=-1;if(!lan_wheelserial)I_Error("Wheel did not request synchronized weapon");printf("PASS live wheel world advances\n");}
 }
 uint32_t h=2166136261u;thinker_t *it;
 for(int i=0;i<MAXPLAYERS;i++)if(playeringame[i]){h=mix(h,players[i].health);h=mix(h,players[i].ammo[0]);}
 for(it=thinkercap.next;it!=&thinkercap;it=it->next)if(it->function.acp1==(actionf_p1)P_MobjThinker){mobj_t *m=(mobj_t*)it;h=mix(h,m->type);h=mix(h,m->x);h=mix(h,m->y);h=mix(h,m->z);h=mix(h,m->angle);h=mix(h,m->health);h=mix(h,m->tics);}
 fprintf(traceFile,"%d %08x\n",leveltime,h);fflush(traceFile);
 if(leveltime<20){key(ev_keydown,KEY_UPARROW);key(ev_keydown,KEY_RCTRL);}
 if(leveltime>100){key(ev_keyup,KEY_UPARROW);key(ev_keydown,consoleplayer%2?KEY_LEFTARROW:KEY_RIGHTARROW);}
 if(leveltime>=(getenv("LAN_LONG")?2100:(getenv("LAN_LATE")?700:350))){if(lan_session){int active=0;for(int i=0;i<4;i++)active+=playeringame[i]!=0;if(active!=atoi(getenv("LAN_COUNT"))||lan_catchingup)I_Error("Late join did not activate all players");if(getenv("LAN_FEATURES")){for(int i=0;i<active;i++)if(lan_colors[i]!=i||lan_pitch[i]!=(i%2?-3499:2500)&&lan_pitch[i]!=(i%2?-3500:2500))I_Error("Player colour or pitch not synchronized");if(players[consoleplayer].readyweapon!=wp_fist)I_Error("Wheel weapon choice not applied");printf("PASS synchronized aim, character colours and wheel selection\n");}printf("PASS native LAN: %d active players, local slot %d, tic %d\n",active,consoleplayer,gametic);}
 fclose(traceFile);traceFile=NULL;I_Quit();}
}
int main(int argc,char **argv){
 const char *output=getenv("LAN_TRACE");if(!output)return 2;traceFile=fopen(output,"w");if(!traceFile)return 2;
 SDL_SetHint(SDL_HINT_AUDIO_DRIVER,"dummy");if(!getenv("LAN_GPU"))SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"dummy");
 if(!SDL_Init(SDL_INIT_VIDEO))return 3;
 myargc=argc;myargv=argv;D_DoomMain();return 0;
}



