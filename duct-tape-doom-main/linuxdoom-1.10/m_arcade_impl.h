#include "../assets/arcade/podium.h"
#include "../assets/arcade/tinybaby.h"
extern int p_aggressiveai,p_advancedtactics,p_persistentpursuit;
boolean P_AggressiveEnabled(void);
boolean P_TacticsEnabled(void);
boolean P_PersistentPursuit(mobj_t*);
#include "p_fluid.h"

/* Arcade mode lives with the menu so it shares Doom's input and font renderer. */
typedef struct {char name[9];int episode,map;unsigned hash;} arcade_map_t;
typedef struct {unsigned hash;char map[9],name[13];int score,won,ticks;} arcade_record_t;
static arcade_map_t arcade_maps[180];
static arcade_record_t arcade_records[18000];
static int arcade_count,arcade_records_count,arcade_selected,arcade_screen,arcade_pending,arcade_scroll;
static int arcade_elapsed,arcade_timebonus;
int arcade_active;
int arcade_podium_hires;
int Arcade_PodiumRect(float *rect){
    if(!menuactive||arcade_screen!=1||!arcade_count)return 0;
    rect[0]=80+151*.5f;rect[1]=50+85*.5f;
    rect[2]=156*.5f;rect[3]=73*.5f;return 1;
}
static int arcade_score,arcade_display,arcade_delta,arcade_pulse,arcade_clock;
static int arcade_damage,arcade_hits,arcade_shots,arcade_kills,arcade_items,arcade_secrets,arcade_won,arcade_ranked;
static char arcade_name[13]="PLAYER",arcade_event[32],arcade_error[64];
/* Transient projectile attribution; no save-format or mobj layout changes. */
typedef struct arcade_projectile_s {mobj_t *actor; int hit; struct arcade_projectile_s *next;} arcade_projectile_t;
static arcade_projectile_t *arcade_projectiles[256];
static int arcade_shot_open,arcade_shot_hit;
static unsigned arcade_bucket(mobj_t *p){return ((size_t)p>>4)&255;}
void Arcade_RemoveProjectile(mobj_t *p){
    arcade_projectile_t **at=&arcade_projectiles[arcade_bucket(p)];
    while(*at){if((*at)->actor==p){arcade_projectile_t *old=*at;*at=old->next;free(old);return;}at=&(*at)->next;}
}
static void arcade_clear_shots(void){
    for(int i=0;i<256;++i)while(arcade_projectiles[i])Arcade_RemoveProjectile(arcade_projectiles[i]->actor);
    arcade_shot_open=arcade_shot_hit=0;
}
void Arcade_TrackProjectile(mobj_t *p){
    if(!arcade_active||!p->target||p->target->player!=&players[consoleplayer])return;
    Arcade_RemoveProjectile(p);
    arcade_projectile_t *entry=malloc(sizeof(*entry));if(!entry){arcade_ranked=0;return;}
    unsigned b=arcade_bucket(p);entry->actor=p;entry->hit=0;entry->next=arcade_projectiles[b];arcade_projectiles[b]=entry;
}
void Arcade_EndShot(void){arcade_shot_open=0;}
void Arcade_Impact(mobj_t *inflictor,mobj_t *source,mobj_t *target){
    if(!arcade_active||!source||source->player!=&players[consoleplayer]||!target||target->health<=0||!(target->flags&MF_COUNTKILL))return;
    if(inflictor&&inflictor!=source){
        for(arcade_projectile_t *p=arcade_projectiles[arcade_bucket(inflictor)];p;p=p->next)
            if(p->actor==inflictor){if(!p->hit){p->hit=1;++arcade_hits;}return;}
    }else if(arcade_shot_open&&!arcade_shot_hit){arcade_shot_hit=1;++arcade_hits;}
}
static unsigned arcade_hash(const char *name){
    unsigned h=2166136261u;int at=W_CheckNumForName((char*)name);
    /* Fingerprint the effective map data, including things and sectors. */
    for(int l=at+1;l<=at+10&&l<numlumps;++l){
        const unsigned char *data=W_CacheLumpNum(l,PU_CACHE);int length=W_LumpLength(l);
        for(int i=0;i<length;++i)h=(h^data[i])*16777619u;
    }
    return h;
}
static const char *arcade_file(void){const char *p=getenv("DOOM_ARCADE_SCORE_FILE");return p&&*p?p:"arcade-scores-v2.txt";}
static void arcade_load(void){
    FILE *f=fopen(arcade_file(),"r");arcade_records_count=0;
    if(!f)return;
    char line[256];
    while(fgets(line,sizeof(line),f)){arcade_record_t r;r.ticks=-1;
        if(sscanf(line,"%x %8s %12s %d %d %d",&r.hash,r.map,r.name,&r.score,&r.won,&r.ticks)<5)continue;
        /* Every line is a separate run, even when the player name repeats.
           Keep the top 100 runs per map; the append-only file retains all runs. */
        if(r.score>=0){
            int count=0,worst=-1;
            for(int i=0;i<arcade_records_count;++i)if(r.hash==arcade_records[i].hash&&!strcmp(r.map,arcade_records[i].map)){
                ++count;if(worst<0||arcade_records[i].score<arcade_records[worst].score)worst=i;
            }
            if(count>=100){if(r.score>arcade_records[worst].score)arcade_records[worst]=r;}
            else if(arcade_records_count<18000)arcade_records[arcade_records_count++]=r;
        }
    }fclose(f);
}
static int arcade_place(int map,int rank){
    int top[100];for(int i=0;i<100;++i)top[i]=-1;
    if(rank<0||rank>=100)return -1;
    for(int i=0;i<arcade_records_count;++i){
        if(arcade_records[i].hash!=arcade_maps[map].hash||strcmp(arcade_records[i].map,arcade_maps[map].name))continue;
        for(int j=0;j<100;++j)if(top[j]<0||arcade_records[i].score>arcade_records[top[j]].score){
            for(int k=99;k>j;--k)top[k]=top[k-1];top[j]=i;break;
        }
    }return top[rank];
}
static int arcade_best(int map){return arcade_place(map,0);}
void Arcade_Cancel(void){arcade_active=arcade_pending=0;arcade_screen=0;arcade_clear_shots();}
void Arcade_NewGame(void){if(!arcade_pending)Arcade_Cancel();else arcade_pending=0;}
void Arcade_Open(int choice){
    (void)choice;Arcade_Cancel();arcade_count=0;arcade_load();arcade_error[0]=0;
    for(int e=1;e<=9;++e)for(int m=1;m<=99;++m){char name[9];
        if(gamemode==commercial){if(e!=1)continue;snprintf(name,sizeof(name),"MAP%02d",m);}
        else {if(m>9||(gamemode==shareware&&e>1))continue;snprintf(name,sizeof(name),"E%dM%d",e,m);}
        int at=W_CheckNumForName(name);
        if(at<0||at+10>=numlumps||strncmp(lumpinfo[at+1].name,"THINGS",6))continue;
        arcade_map_t *map=&arcade_maps[arcade_count++];strcpy(map->name,name);map->episode=e;map->map=m;map->hash=arcade_hash(name);
    }
    if(arcade_selected>=arcade_count)arcade_selected=0;
    arcade_screen=1;menuactive=true;currentMenu=&MainDef;
}
static void arcade_add(int points,const char *event){
    if(!arcade_active)return;
    if(points>0&&arcade_score>999999999-points)points=999999999-arcade_score;
    arcade_score+=points;if(arcade_score<0)arcade_score=0;
    arcade_delta=points;arcade_pulse=24;snprintf(arcade_event,sizeof(arcade_event),"%s",event);
}
void Arcade_Damage(mobj_t *source,mobj_t *target,int damage){
    if(!arcade_active||!source||source->player!=&players[consoleplayer]||target->player||!(target->flags&MF_COUNTKILL)||target->health<=0||damage<=0)return;
    int dealt=damage<target->health?damage:target->health;arcade_damage+=dealt;arcade_add((dealt+3)/4,"HIT");
}
void Arcade_Kill(mobj_t *source,mobj_t *target){
    if(arcade_active&&source&&source->player==&players[consoleplayer]&&(target->flags&MF_COUNTKILL)){
        ++arcade_kills;arcade_add(100+target->info->spawnhealth,"MONSTER DOWN");
    }
}
void Arcade_Pickup(player_t *player,mobj_t *item){
    if(!arcade_active||player!=&players[consoleplayer])return;
    ++arcade_items;arcade_add((item->flags&MF_COUNTITEM)?100:25,"PICKUP");
}
void Arcade_Shot(player_t *player){
    if(!arcade_active||player!=&players[consoleplayer])return;
    ++arcade_shots;arcade_shot_open=1;arcade_shot_hit=0;
}
static int arcade_efficiency(void){return arcade_shots?(int)((long long)arcade_hits*100/arcade_shots):0;}
static int arcade_time_bonus(int ticks){
    if(ticks<=5*60*TICRATE)return 3000;
    if(ticks<=10*60*TICRATE)return 1500;
    if(ticks<=15*60*TICRATE)return 500;
    return 0;
}
static const char *arcade_medal(int bonus){return bonus==3000?"GOLD":bonus==1500?"SILVER":bonus==500?"BRONZE":"NONE";}
static void arcade_time_text(char *out,int size,int ticks){
    if(ticks<0)snprintf(out,size,"--:--");
    else {int secs=ticks/TICRATE;snprintf(out,size,"%02d:%02d",secs/60,secs%60);}
}
int Arcade_Finish(int won){
    if(!arcade_active)return 0;
    arcade_elapsed=leveltime;arcade_timebonus=won?arcade_time_bonus(arcade_elapsed):0;
    if(won)arcade_add(1000+arcade_efficiency()*10+arcade_timebonus,"LEVEL CLEAR BONUS");
    arcade_active=0;arcade_clear_shots();arcade_won=won;arcade_display=arcade_score;arcade_screen=2;
    arcade_error[0]=0;arcade_name[0]=0;menuactive=true;currentMenu=&MainDef;gameaction=ga_nothing;return 1;
}
static void arcade_tick(void){
    ++arcade_clock;if(arcade_pulse>0)--arcade_pulse;
    int difference=arcade_score-arcade_display;
    if(difference)arcade_display+=difference>0?(difference+5)/6:(difference-5)/6;
    if(!arcade_active||arcade_pending||gamestate!=GS_LEVEL)return;
    player_t *p=&players[consoleplayer];
    if(p->cheats||gameskill!=sk_hard||netgame||demoplayback||demorecording)arcade_ranked=0;
    if(p->secretcount>arcade_secrets){arcade_add((p->secretcount-arcade_secrets)*500,"SECRET FOUND");arcade_secrets=p->secretcount;}
    if(p->health<=0)Arcade_Finish(0);
}
/* True-colour rank lettering supplements the classic palette fallback. */
unsigned arcade_rank_pixels[SCREENWIDTH*SCREENHEIGHT];
static unsigned arcade_rank_tint;
static void arcade_box(int x,int y,int w,int h,int color){
    /* Keep arcade displays at half size. Transform shared edges together
       so the artwork, text and panels remain aligned. Preserve fine strokes. */
    if(arcade_screen || (arcade_active && !menuactive)){
        int top=arcade_screen?50:1;
        int right=80+(x+w)/2,bottom=top+(y+h)/2;
        x=80+x/2;y=top+y/2;
        w=right-x;h=bottom-y;if(w<1)w=1;if(h<1)h=1;
    }
    for(int yy=y;yy<y+h&&yy<SCREENHEIGHT;++yy)if(yy>=0)
        for(int xx=x;xx<x+w&&xx<SCREENWIDTH;++xx)if(xx>=0){int at=yy*SCREENWIDTH+xx;screens[0][at]=(byte)color;arcade_rank_pixels[at]=arcade_rank_tint;if(v_overlayactive)v_overlaymask[at]=color?255:235;}
    V_MarkRect(x,y,w,h);
}
int Arcade_MenuOpen(void){return arcade_screen!=0;}
static int arcade_ink=0;
static byte arcade_colors[8],arcade_shades[8][16];
static void arcade_init_colors(void){
    static int ready=0;if(ready)return;ready=1;
    static const int colors[8][3]={{255,80,70},{255,170,45},{255,235,80},{70,255,125},{85,155,255},{130,110,255},{235,100,255},{190,100,255}};
    const byte *pal=W_CacheLumpName("PLAYPAL",PU_CACHE);
    for(int n=0;n<8;++n)for(int shade=0;shade<16;++shade){int best=200000;for(int i=0;i<256;++i){int d=0;for(int c=0;c<3;++c){int x=pal[i*3+c]-colors[n][c]*(16-shade)/16;d+=x*x;}if(d<best){best=d;arcade_shades[n][shade]=(byte)i;}}arcade_colors[n]=arcade_shades[n][0];}
}
static int arcade_width(const char *text){
    int width=0;for(;*text;++text){unsigned c=(unsigned char)*text;if(c<32||c>126)c='?';width+=baby_widths[c-32];}return width;
}
static void arcade_font(int x,int y,const char *text,int scale){
    arcade_init_colors();
    for(int pass=0;pass<2;++pass){int cursor=x;
        for(const char *t=text;*t;++t){unsigned c=(unsigned char)*t;if(c<32||c>126)c='?';int w=baby_widths[c-32],index=(int)(t-text);
            for(int yy=0;yy<6;++yy)for(int xx=0;xx<w;++xx){
                int value=baby_pixels[baby_offsets[c-32]+yy*w+xx];if(!value)continue;int color=0;
                if(pass){if(!arcade_ink)color=value==2?4:82;
                    else color=arcade_shades[arcade_ink==1?(index+arcade_clock/8)%7:arcade_ink==2?7:arcade_ink==4?1:4][value==2?0:3];}
                arcade_rank_tint=0;
                if(pass&&arcade_ink>=1&&arcade_ink<=3)arcade_rank_tint=((unsigned)arcade_ink<<24)|value;
                arcade_box(cursor+xx*scale+(pass?0:1),y+yy*scale+(pass?0:1),scale,scale,color);
            }cursor+=w*scale;
        }
    }
    arcade_rank_tint=0;
}
/* Tilted main-menu signature, sharing the smooth true-colour rank pass. */
int Menu_BrandOpen(void){return menuactive && currentMenu==&MainDef && !messageToPrint && !arcade_screen;}
static void menu_brand(void){
    const char *text="TuinDoomEnhanced";
    unsigned char glyphs[160*6]={0};int width=0;
    arcade_init_colors();
    for(const char *t=text;*t;++t){unsigned c=(unsigned char)*t;int w=baby_widths[c-32];
        for(int y=0;y<6;++y)for(int x=0;x<w;++x)glyphs[y*160+width+x]=baby_pixels[baby_offsets[c-32]+y*w+x];
        width+=w;
    }
    /* Inverse-map a 12-degree upward tilt; shadow stays outside the letters. */
    for(int pass=0;pass<2;++pass)for(int y=20;y<72;++y)for(int x=184;x<318;++x){
        float dx=x-200.f-(pass?0:1),dy=y-58.f-(pass?0:1);
        int u=(int)floorf((dx*.978148f-dy*.207912f)/1.15f);
        int v=(int)floorf((dx*.207912f+dy*.978148f)/1.15f);
        if(u<0||u>=width||v<0||v>=6)continue;
        int value=glyphs[v*160+u];if(!value)continue;
        arcade_rank_tint=pass?(1u<<24)|value:0;
        arcade_box(x,y,1,1,pass?arcade_shades[(u/6)%7][value==2?0:3]:0);
    }
    arcade_rank_tint=0;
}
static void arcade_text(int x,int y,const char *s){int scale=arcade_width(s)*2<=304-x?2:1;arcade_font(x,y,s,scale);}
static void arcade_center(int y,const char *s){int scale=arcade_width(s)*2<=288?2:1;arcade_font(160-arcade_width(s)*scale/2,y,s,scale);}
static void arcade_small(int center,int y,const char *s){char label[32];snprintf(label,sizeof(label),"%s",s);while(arcade_width(label)*2>48&&strlen(label)>1)label[strlen(label)-1]=0;arcade_font(center-arcade_width(label),y,label,2);}
static void arcade_draw(void){
    char text[96];
    memset(arcade_rank_pixels,0,sizeof(arcade_rank_pixels));arcade_rank_tint=0;
    if(!arcade_screen){
        if(!arcade_active||menuactive)return;
        snprintf(text,sizeof(text),"%08d",arcade_display);
        arcade_box(110,2,100,44,0);arcade_box(110,2,100,1,arcade_pulse?231:176);
        arcade_font(114,4,"SCORE",2);char timer[16];arcade_time_text(timer,sizeof(timer),leveltime);arcade_font(166,4,timer,2);arcade_center(18,text);
        if(arcade_pulse){snprintf(text,sizeof(text),"%+d %s",arcade_delta,arcade_event);arcade_font(160-arcade_width(text),32-(24-arcade_pulse)/12,text,2);}
        return;
    }
    arcade_init_colors();inhelpscreens=true;arcade_ink=0;
    arcade_box(8,6,304,188,0);arcade_box(8,6,304,2,176);arcade_box(8,192,304,2,176);
    // Cabinet rails and a travelling orange highlight.
    arcade_box(8,8,1,184,107);arcade_box(311,8,1,184,107);
    arcade_box(8+(arcade_clock*2)%270,6,32,2,arcade_colors[1]);
    arcade_ink=4;arcade_center(14,"ARCADE // HALL OF FAME");arcade_ink=0;
    arcade_center(27,"ULTRA-VIOLENCE / PISTOL START");
    arcade_box(16,40,288,1,107);
    if(arcade_screen==1){
        arcade_text(20,46,"LEVEL");arcade_text(91,46,"BEST");
        int first=arcade_selected-3;if(first<0)first=0;if(first>arcade_count-7&&arcade_count>7)first=arcade_count-7;
        for(int row=0;row<7&&first+row<arcade_count;++row){int index=first+row,y=61+row*14,record=arcade_best(index);
            arcade_box(16,y-3,128,13,index==arcade_selected?105:110);
            if(index==arcade_selected){arcade_box(16,y-3,2,13,arcade_colors[1]);arcade_ink=4;}else arcade_ink=0;
            arcade_text(21,y,arcade_maps[index].name);
            snprintf(text,sizeof(text),"%08d",record>=0?arcade_records[record].score:0);arcade_text(77,y,text);
        }
        arcade_ink=0;arcade_box(147,45,1,113,107);
        if(arcade_count){
            snprintf(text,sizeof(text),"%s TOP 3",arcade_maps[arcade_selected].name);arcade_text(180,44,text);
            /* Fit the complete artwork into the space below the labels.
               Keep its aspect ratio; names never mask a marine or a medal. */
            const int boxX=151,boxY=85,boxW=156,boxH=73;
            int artW=boxW,artH=artW*104/156;
            if(artH>boxH){artH=boxH;artW=artH*156/104;}
            int artX=boxX+(boxW-artW)/2,artY=boxY+(boxH-artH)/2;
            if(!arcade_podium_hires)for(int yy=0;yy<artH;++yy)for(int xx=0;xx<artW;++xx){
                int c=arcade_podium[(yy*104/artH)*156+xx*156/artW];
                if(c<256)arcade_box(artX+xx,artY+yy,1,1,c);
            }
            /* Names are aligned in silver / gold / bronze order above the art. */
            const int centers[3]={230,179,281};
            for(int place=0;place<3;++place){int record=arcade_place(arcade_selected,place),cx=centers[place];
                static const int floatY[10]={0,0,-1,-1,-2,-2,-2,-1,-1,0};
                arcade_ink=record>=0?place+1:0;
                arcade_small(cx,58+(record>=0&&place==0?floatY[(arcade_clock/8)%10]:0),record>=0?arcade_records[record].name:"---");
                snprintf(text,sizeof(text),"%d",record>=0?arcade_records[record].score:0);arcade_ink=0;arcade_small(cx,71,text);
            }
        }else arcade_center(87,"NO PLAYABLE MAPS FOUND");
        arcade_ink=0;arcade_box(16,161,288,1,107);
        snprintf(text,sizeof(text),"%02d / %02d LEVELS",arcade_count?arcade_selected+1:0,arcade_count);arcade_text(20,166,text);
        arcade_ink=4;arcade_font(177,166,"ENTER: SCORES",2);arcade_ink=0;
        arcade_center(181,"UP/DOWN: SCROLL   ESC: BACK");
    }else if(arcade_screen==3){
        snprintf(text,sizeof(text),"%s LEADERBOARD",arcade_maps[arcade_selected].name);arcade_ink=4;arcade_center(45,text);arcade_ink=0;
        arcade_font(22,58,"#",2);arcade_font(44,58,"PLAYER",2);arcade_font(135,58,"SCORE",2);arcade_font(211,58,"TIME",2);arcade_font(258,58,"END",2);
        for(int row=0;row<6;++row){int rank=arcade_scroll+row,r=arcade_place(arcade_selected,rank),y=73+row*13;if(r<0)break;
            arcade_record_t *record=&arcade_records[r];arcade_box(17,y-1,286,12,row%2?110:108);
            arcade_ink=rank<3?rank+1:0;snprintf(text,sizeof(text),"%02d",rank+1);arcade_font(22,y,text,2);arcade_font(44,y,record->name,2);
            arcade_ink=0;snprintf(text,sizeof(text),"%d",record->score);arcade_font(135,y,text,2);
            arcade_time_text(text,sizeof(text),record->ticks);arcade_font(211,y,text,2);
            arcade_font(258,y,record->won?(record->ticks<0?"CLEAR":arcade_medal(arcade_time_bonus(record->ticks))):"DIED",2);
        }
        if(arcade_best(arcade_selected)<0)arcade_center(99,"BE THE FIRST TO SET A SCORE");
        arcade_center(153,"5/10/15 MIN: +3000/1500/500");
        /* Completion-only bonuses are shown on the results screen. */
        arcade_ink=4;arcade_center(167,"ENTER: START RUN");arcade_ink=0;arcade_center(180,"UP/DOWN: SCORES  ESC: LEVELS");
    }else{
        arcade_ink=4;arcade_center(46,arcade_won?"LEVEL CLEARED":"GAME OVER");arcade_ink=0;
        snprintf(text,sizeof(text),"SCORE  %08d",arcade_score);arcade_center(64,text);
        snprintf(text,sizeof(text),"KILLS %d   PICKUPS %d",arcade_kills,arcade_items);arcade_center(79,text);
        snprintf(text,sizeof(text),"ACCURACY %d%%   MISSES %d",arcade_efficiency(),arcade_shots-arcade_hits);arcade_center(92,text);
        snprintf(text,sizeof(text),"SECRETS %d   SHOTS %d",arcade_secrets,arcade_shots);arcade_center(105,text);
        char elapsed[16];arcade_time_text(elapsed,sizeof(elapsed),arcade_elapsed);
        snprintf(text,sizeof(text),"TIME %s / %s +%d",elapsed,arcade_medal(arcade_timebonus),arcade_timebonus);arcade_center(118,text);
        arcade_center(131,arcade_ranked?"ENTER YOUR NAME":"PRACTICE / NOT RANKED");
        snprintf(text,sizeof(text),"%s%s",arcade_name,(arcade_clock/12)%2?"_":" ");arcade_center(145,text);
        arcade_center(158,"ENTER: SAVE AND RETURN");arcade_center(173,"ESC: RETURN WITHOUT SAVING");
        if(arcade_error[0])arcade_center(186,arcade_error);
    }
}
/* Reuse the actual bevelled menu artwork, not an enlarged HUD font.
   Columns are from Doom's menu patches; the IWAD supplies the pixels. */
static void arcade_main_label(int x,int y){
    static const struct {const char *lump;int first,last,small;} letters[]={
        {"M_HURT",0,16,0},{"M_RDTHIS",94,99,0},{"M_NGAME",51,68,1},{"M_RDTHIS",80,94,0},
        {NULL,0,6,0},{"M_RDTHIS",99,113,0},{"M_EPISOD",35,48,0},{"M_OPTION",0,17,1},
        {"M_RDTHIS",0,15,1},{"M_RDTHIS",15,29,0},{"M_RDTHIS",99,113,0}};
    for(int i=0;i<11;++i){
        if(!letters[i].lump){x+=6;continue;}
        const byte *p=W_CacheLumpName((char*)letters[i].lump,PU_CACHE);int w=p[0]|p[1]<<8;
        for(int col=letters[i].first;col<letters[i].last&&col<w;++col){
            const byte *o=p+8+col*4;unsigned off=(unsigned)o[0]|(unsigned)o[1]<<8|(unsigned)o[2]<<16|(unsigned)o[3]<<24;
            for(const byte *post=p+off;post[0]!=255;post+=post[1]+4)for(int row=0;row<post[1];++row){
                int yy=post[0]+row;if(letters[i].small)yy=4+yy*11/15;
                arcade_box(x+col-letters[i].first,y+yy,1,1,post[3+row]);
            }
        }x+=letters[i].last-letters[i].first;
    }
}
static int arcade_key(int ch){
    if(!arcade_screen)return 0;
    if(arcade_screen==1){
        if(ch==KEY_ESCAPE){arcade_screen=0;return 1;}
        if(arcade_count&&(ch==KEY_UPARROW||ch==KEY_DOWNARROW)){arcade_selected=(arcade_selected+arcade_count+(ch==KEY_UPARROW?-1:1))%arcade_count;S_StartSound(NULL,sfx_pstop);}
        if(arcade_count&&(ch==KEY_LEFTARROW||ch==KEY_RIGHTARROW)){arcade_selected+=ch==KEY_LEFTARROW?-7:7;if(arcade_selected<0)arcade_selected=0;if(arcade_selected>=arcade_count)arcade_selected=arcade_count-1;}
        if(ch==KEY_ENTER&&arcade_count){arcade_screen=3;arcade_scroll=0;S_StartSound(NULL,sfx_swtchn);}return 1;
    }
    if(arcade_screen==3){
        if(ch==KEY_ESCAPE){arcade_screen=1;return 1;}
        if(ch==KEY_UPARROW&&arcade_scroll>0)--arcade_scroll;
        if(ch==KEY_DOWNARROW&&arcade_place(arcade_selected,arcade_scroll+6)>=0)++arcade_scroll;
        if(ch==KEY_ENTER&&!netgame){
            arcade_score=arcade_display=arcade_damage=arcade_hits=arcade_shots=arcade_kills=arcade_items=arcade_secrets=0;
            arcade_ranked=1;arcade_active=arcade_pending=1;arcade_screen=0;arcade_pulse=0;
            players[consoleplayer].cheats=0;respawnparm=fastparm=nomonsters=false;
            S_StartSound(NULL,sfx_swtchn);
            G_DeferedInitNew(sk_hard,arcade_maps[arcade_selected].episode,arcade_maps[arcade_selected].map);M_ClearMenus();
        }return 1;
    }
    if(ch==KEY_BACKSPACE){size_t n=strlen(arcade_name);if(n)arcade_name[n-1]=0;return 1;}
    if(ch==KEY_ESCAPE){Arcade_Open(0);arcade_screen=3;arcade_scroll=0;return 1;}
    if(ch==KEY_ENTER){
        if(arcade_ranked){
            FILE *f=fopen(arcade_file(),"a");
            if(!f){strcpy(arcade_error,"COULD NOT SAVE SCORE");return 1;}
            arcade_map_t *m=&arcade_maps[arcade_selected];
            int ok=fprintf(f,"%08x %s %s %d %d %d\n",m->hash,m->name,arcade_name[0]?arcade_name:"PLAYER",arcade_score,arcade_won,arcade_elapsed)>0;
            if(fclose(f)!=0)ok=0;
            if(!ok){strcpy(arcade_error,"COULD NOT SAVE SCORE");return 1;}
        }S_StartSound(NULL,sfx_itemup);Arcade_Open(0);arcade_screen=3;arcade_scroll=0;return 1;
    }
    if(ch>=32&&ch<127&&(isalnum(ch)||ch=='_'||ch=='-')){size_t n=strlen(arcade_name);if(n<10){arcade_name[n]=(char)toupper(ch);arcade_name[n+1]=0;}}
    return 1;
}

/* Deterministic smoke driver: only called by the test executable. */
int M_ArcadeTest(int step){
    if(step==0){Arcade_Open(0);return arcade_count;}
    if(step==1){if(arcade_screen==1)arcade_key(KEY_ENTER);arcade_key(KEY_ENTER);return arcade_active&&arcade_pending;}
    if(step==2)return arcade_active&&gameskill==sk_hard&&players[consoleplayer].readyweapon==wp_pistol&&players[consoleplayer].ammo[am_clip]==50&&!players[consoleplayer].weaponowned[wp_shotgun];
    if(step==3){int before=arcade_score;mobj_t monster;memset(&monster,0,sizeof(monster));monster.flags=MF_COUNTKILL;monster.health=20;monster.info=&mobjinfo[MT_POSSESSED];
        Arcade_Shot(&players[consoleplayer]);Arcade_Impact(players[consoleplayer].mo,players[consoleplayer].mo,&monster);Arcade_Impact(players[consoleplayer].mo,players[consoleplayer].mo,&monster);Arcade_EndShot();Arcade_Damage(players[consoleplayer].mo,&monster,100);Arcade_Kill(players[consoleplayer].mo,&monster);Arcade_Pickup(&players[consoleplayer],&monster);
        return arcade_score==before+150&&arcade_damage==20&&arcade_kills==1&&arcade_items==1&&arcade_shots==1&&arcade_hits==1&&arcade_efficiency()==100;}
    if(step==9){
        int a=p_aggressiveai,t=p_advancedtactics,p=p_persistentpursuit,f=p_fluidmonsters;
        p_aggressiveai=p_advancedtactics=p_persistentpursuit=p_fluidmonsters=1;
        int ok=!P_AggressiveEnabled()&&!P_TacticsEnabled()&&!P_FluidEnabled()&&!P_PersistentPursuit(NULL);
        p_aggressiveai=a;p_advancedtactics=t;p_persistentpursuit=p;p_fluidmonsters=f;
        mobj_t target,missile;memset(&target,0,sizeof(target));memset(&missile,0,sizeof(missile));target.flags=MF_COUNTKILL;target.health=100;missile.target=players[consoleplayer].mo;
        int score=arcade_score,hits=arcade_hits,shots=arcade_shots;
        Arcade_Shot(&players[consoleplayer]);Arcade_EndShot(); /* miss */
        Arcade_Shot(&players[consoleplayer]);Arcade_TrackProjectile(&missile);Arcade_EndShot();
        Arcade_Impact(&missile,missile.target,&target);Arcade_Impact(&missile,missile.target,&target);
        ok=ok&&arcade_score==score&&arcade_hits==hits+1&&arcade_shots==shots+2;
        Arcade_RemoveProjectile(&missile);Arcade_Impact(&missile,missile.target,&target);
        ok=ok&&arcade_hits==hits+1;
        arcade_hits=hits;arcade_shots=shots;
        int secrets=players[consoleplayer].secretcount,tracked=arcade_secrets;
        players[consoleplayer].secretcount=tracked+1;arcade_tick();
        ok=ok&&arcade_score==score+500&&arcade_secrets==tracked+1;
        players[consoleplayer].secretcount=secrets;arcade_secrets=tracked;arcade_score=score;
        return ok;
    }
    if(step==11){arcade_key(KEY_ENTER);if(arcade_place(arcade_selected,6)>=0){arcade_key(KEY_DOWNARROW);if(arcade_scroll!=1)return 0;arcade_key(KEY_UPARROW);if(arcade_scroll!=0)return 0;}return arcade_screen==3&&!arcade_active;}
    if(step==12)return arcade_time_bonus(5*60*TICRATE)==3000&&arcade_time_bonus(5*60*TICRATE+1)==1500&&arcade_time_bonus(10*60*TICRATE)==1500&&arcade_time_bonus(10*60*TICRATE+1)==500&&arcade_time_bonus(15*60*TICRATE)==500&&arcade_time_bonus(15*60*TICRATE+1)==0;
    if(step==10){Arcade_Cancel();M_StartControlPanel();return 1;}
    if(step==4)return arcade_screen==2&&!arcade_active&&arcade_won;
    if(step==5){
        int before=arcade_records_count;
        arcade_key('T');arcade_key('E');arcade_key('S');arcade_key('T');arcade_key(KEY_ENTER);
        int found=0;for(int i=0;i<arcade_records_count;++i)
            if(!strcmp(arcade_records[i].name,"TEST")&&arcade_records[i].score==arcade_score&&arcade_records[i].ticks==arcade_elapsed)found=1;
        return arcade_screen==3&&found&&arcade_records_count==before+1;
    }
    if(step==6){arcade_key(KEY_ENTER);return arcade_active;}
    if(step==7)return arcade_screen==2&&!arcade_active&&!arcade_won&&arcade_timebonus==0;
    if(step==8){Arcade_Cancel();return !arcade_active&&!arcade_screen;}
    return 0;
}
