/* Wall-camera setup and single-player demo sessions. */
#include <time.h>
#include <math.h>
#include <errno.h>
#ifdef _WIN32
#include <io.h>
#else
#include <dirent.h>
#endif
extern unsigned r_levelserial;
typedef struct {float x,y,z;} director_camera_t;
director_camera_t director_cameras[32];
int director_count,director_edit,director_watch,director_menu;
extern int director_demo;
static unsigned director_hash;
static int director_choice,director_mouse,director_recording,director_stop_confirm;
static char director_status[80]="PLACE CAMERAS FIRST",director_last[32];
extern int I_DirectorPlace(float*);
extern int I_TestDirectorClosePlacement(void);
extern int I_TestDirectorNoBob(void);
extern void G_DirectorRecord(const char*);
extern void G_DirectorPlay(const char*);
static unsigned director_map_hash(void){char map[9];if(gamemode==commercial)snprintf(map,9,"MAP%02d",gamemap);else snprintf(map,9,"E%dM%d",gameepisode,gamemap);return arcade_hash(map);}
static int director_write_cameras(const char *path){
    FILE *f=fopen(path,"w");if(!f)return 0;
    int ok=fprintf(f,"TDC1 %x %d\n",director_hash,director_count)>0;
    for(int i=0;i<director_count;++i)if(fprintf(f,"%.9g %.9g %.9g\n",director_cameras[i].x,director_cameras[i].y,director_cameras[i].z)<0)ok=0;
    if(fclose(f))ok=0;return ok;
}
static int director_read_cameras(const char *path){
    FILE *f=fopen(path,"r");if(!f)return 0;unsigned hash;int count;
    if(fscanf(f,"TDC1 %x %d",&hash,&count)!=2||hash!=director_hash||count<0||count>32){fclose(f);return 0;}
    director_camera_t cameras[32];
    for(int i=0;i<count;++i)if(fscanf(f,"%f %f %f",&cameras[i].x,&cameras[i].y,&cameras[i].z)!=3||!isfinite(cameras[i].x)||!isfinite(cameras[i].y)||!isfinite(cameras[i].z)){fclose(f);return 0;}
    fclose(f);memcpy(director_cameras,cameras,count*sizeof(*cameras));director_count=count;return 1;
}
static void director_save(void){char path[48];snprintf(path,sizeof(path),"director-%08x.cams",director_hash);if(!director_write_cameras(path))strcpy(director_status,"CAMERA SAVE FAILED");}
static char director_takes[512][32];
static int director_take_count,director_take_choice,director_browse,director_delete_confirm;
static void director_add_take(const char *name){
    if(director_take_count>=512||strlen(name)>=32)return;
    char path[48];snprintf(path,sizeof(path),"%s",name);
    char *dot=strrchr(path,'.');if(!dot||strcmp(dot,".lmp"))return;strcpy(dot,".cams");
    FILE *f=fopen(path,"r");unsigned hash;int count;
    int ok=f&&fscanf(f,"TDC1 %x %d",&hash,&count)==2&&hash==director_hash&&count>0&&count<=32;
    if(f)fclose(f);if(ok)strcpy(director_takes[director_take_count++],name);
}
static int director_take_compare(const void *a,const void *b){return strcmp((const char*)b,(const char*)a);}
static void director_scan(void){
    director_take_count=director_take_choice=director_delete_confirm=0;
#ifdef _WIN32
    struct _finddata_t data;intptr_t handle=_findfirst("take_*.lmp",&data);
    if(handle!=-1){do{director_add_take(data.name);}while(_findnext(handle,&data)==0);_findclose(handle);}
#else
    DIR *dir=opendir(".");struct dirent *entry;
    if(dir){while((entry=readdir(dir)))if(!strncmp(entry->d_name,"take_",5))director_add_take(entry->d_name);closedir(dir);}
#endif
    qsort(director_takes,director_take_count,sizeof(director_takes[0]),director_take_compare);
}
static void director_delete_selected(void){
    director_delete_confirm=0;
    if(director_demo||director_take_choice<0||director_take_choice>=director_take_count)return;
    char name[32],path[48];strcpy(name,director_takes[director_take_choice]);
    if(remove(name)!=0){strcpy(director_status,"TAKE DELETE FAILED");return;}
    snprintf(path,sizeof(path),"%s",name);strcpy(strrchr(path,'.'),".cams");
    int sidecar_ok=remove(path)==0||errno==ENOENT;
    if(!strcmp(director_last,name))director_last[0]=0;
    FILE *f=fopen("director-last.txt","r");char last[32]={0};
    if(f){fscanf(f,"%31s",last);fclose(f);if(!strcmp(last,name))remove("director-last.txt");}
    int selected=director_take_choice;director_scan();
    director_take_choice=selected<director_take_count?selected:director_take_count?director_take_count-1:0;
    strcpy(director_status,sidecar_ok?"TAKE DELETED":"TAKE DELETED / CAM FILE LEFT");
}
static void director_play_selected(void){
    if(!director_take_count)return;
    strcpy(director_last,director_takes[director_take_choice]);
    // Takes record gameplay; cameras are the editable shot layout for this map.
    // Never replace it with the older layout captured in the take sidecar.
    if(!director_count){strcpy(director_status,"PLACE CAMERAS FIRST");director_browse=0;return;}
    director_watch=1;director_edit=director_menu=director_browse=0;menuactive=false;
    G_DirectorPlay(director_last);
}
static void director_open(void){
    if(gamestate!=GS_LEVEL||netgame||arcade_active||arcade_screen){players[consoleplayer].message="CAMERA DIRECTOR: SINGLE PLAYER LEVELS ONLY";return;}
    unsigned hash=director_map_hash();if(hash!=director_hash){director_hash=hash;director_count=0;director_edit=director_watch=0;char path[48];snprintf(path,sizeof(path),"director-%08x.cams",hash);director_read_cameras(path);}
    director_menu=1;director_stop_confirm=0;director_browse=director_delete_confirm=0;menuactive=true;director_choice=0;
}
void Director_DemoEnded(int saved){
    director_recording=director_watch=director_edit=0;
    strcpy(director_status,saved?"TAKE READY FOR PLAYBACK":"COULD NOT SAVE TAKE");director_menu=director_browse=0;menuactive=false;
}
void Director_LevelExit(void){
    director_watch=director_edit=director_menu=director_browse=0;
    D_StartTitle();M_StartControlPanel();M_SetupNextMenu(&MainDef);
}
static void director_place(void){
    if(director_count>=32){strcpy(director_status,"MAXIMUM 32 CAMERAS");return;}
    float at[3];if(!I_DirectorPlace(at)){players[consoleplayer].message="AIM AT A SOLID WALL WITHIN 1024 UNITS";return;}
    director_cameras[director_count++]=(director_camera_t){at[0],at[1],at[2]};director_save();
    players[consoleplayer].message="GREEN CAMERA PLACED - F9 WHEN READY";S_StartSound(NULL,sfx_swtchn);
}
static void director_action(void){
    if(director_choice==0&&(demorecording||demoplayback)){strcpy(director_status,"STOP THE TAKE FIRST");return;}
    if(director_choice==0){director_edit=!director_edit;director_watch=0;director_menu=0;menuactive=false;return;}
    if(director_choice==1){if(director_count){--director_count;director_save();}return;}
    if(director_choice==2){
        if(demorecording||demoplayback){strcpy(director_status,"STOP THE CURRENT TAKE FIRST");return;}
        if(!director_count){strcpy(director_status,"PLACE AT LEAST ONE CAMERA");return;}
        char base[24],path[48];unsigned stamp=(unsigned)time(NULL);FILE *f;
        do{snprintf(base,sizeof(base),"take_%u",stamp++);snprintf(path,sizeof(path),"%s.lmp",base);f=fopen(path,"rb");if(f)fclose(f);}while(f);
        snprintf(path,sizeof(path),"%s.cams",base);if(!director_write_cameras(path)){strcpy(director_status,"COULD NOT SAVE CAMERAS");return;}
        snprintf(director_last,sizeof(director_last),"%s.lmp",base);f=fopen("director-last.txt","w");if(f){fprintf(f,"%s",director_last);fclose(f);}
        director_recording=1;director_edit=director_watch=director_menu=0;menuactive=false;G_DirectorRecord(base);return;
    }
    if(director_choice==3){if(director_demo&&demorecording){director_stop_confirm=1;return;}if(director_demo)G_CheckDemoStatus();director_watch=director_edit=director_menu=director_browse=0;menuactive=false;return;}
    if(director_choice==4){
        if(demorecording||demoplayback){strcpy(director_status,"STOP THE TAKE FIRST");return;}
        director_scan();director_browse=1;return;
    }
    if(director_choice==6){director_save();strcpy(director_status,"CAMERAS SAVED FOR THIS MAP");return;}
    if(director_choice==7){
        if(director_demo){strcpy(director_status,"STOP THE TAKE FIRST");return;}
        director_count=director_watch=director_edit=0;director_save();
        strcpy(director_status,"ALL CAMERAS CLEARED");return;
    }
    if(director_choice==5){director_watch=!director_watch;director_menu=0;menuactive=false;return;}
    director_menu=0;menuactive=false;
}
static int director_input(event_t *ev){
    if(ev->type==ev_keydown&&ev->data1==KEY_F9){if(director_menu){director_menu=0;menuactive=false;}else director_open();return 1;}
    if(director_menu&&director_stop_confirm){
        if(ev->type==ev_keydown){
            if(ev->data1=='y'||ev->data1=='Y'){
                director_stop_confirm=0;if(director_demo)G_CheckDemoStatus();
                director_watch=director_edit=director_menu=0;menuactive=false;
            }else if(ev->data1=='n'||ev->data1=='N'||ev->data1==KEY_ESCAPE)director_stop_confirm=0;
        }return 1;
    }
    if(director_menu&&director_browse){
        if(ev->type==ev_keydown){
            if(director_delete_confirm){
                if(ev->data1=='y'||ev->data1=='Y')director_delete_selected();
                else if(ev->data1=='n'||ev->data1=='N'||ev->data1==KEY_ESCAPE)director_delete_confirm=0;
                return 1;
            }
            if((ev->data1=='d'||ev->data1=='D')&&director_take_count){director_delete_confirm=1;return 1;}
            if(ev->data1==KEY_ESCAPE)director_browse=0;
            if(ev->data1==KEY_UPARROW&&director_take_choice>0)--director_take_choice;
            if(ev->data1==KEY_DOWNARROW&&director_take_choice+1<director_take_count)++director_take_choice;
            if(ev->data1==KEY_ENTER)director_play_selected();
        }return 1;
    }
    if(director_menu){if(ev->type==ev_keydown){int k=ev->data1;
        if(k==KEY_ESCAPE){director_menu=0;menuactive=false;}
        if(k==KEY_UPARROW)director_choice=(director_choice+8)%9;
        if(k==KEY_DOWNARROW)director_choice=(director_choice+1)%9;
        if(k==KEY_ENTER)director_action();}return 1;}
    if(director_edit&&ev->type==ev_mouse){int pressed=ev->data1&1;if(pressed&&!director_mouse)director_place();director_mouse=pressed;if(pressed)return 1;}
    return 0;
}
static void director_draw(void){
    if(director_demo&&gamestate==GS_LEVEL&&!director_menu){arcade_ink=4;arcade_font(8,8,demorecording?"REC / F9: STOP":"REPLAY / F9: CAMERAS",1);arcade_ink=0;}
    if(director_edit&&!director_menu){arcade_ink=0;arcade_font(8,8,"CAMERA SETUP: CLICK WALL / F9: MENU",1);}
    if(!director_menu)return;
    int old=arcade_screen;arcade_screen=4;arcade_ink=0;inhelpscreens=true;
    arcade_box(8,6,304,188,0);arcade_box(8,6,304,2,112);arcade_box(8,192,304,2,112);
    arcade_ink=4;arcade_center(15,"CAMERA DIRECTOR");arcade_ink=0;
    if(director_stop_confirm){
        arcade_ink=4;arcade_center(65,"STOP RECORDING?");
        arcade_ink=0;arcade_center(89,"THIS ENDS AND SAVES THE TAKE");
        arcade_center(111,"YOU CAN KEEP PLAYING");
        arcade_center(155,"Y: STOP / N: CANCEL");
        arcade_screen=old;return;
    }
    char text[80];snprintf(text,sizeof(text),"%d / 32 WALL CAMERAS",director_count);arcade_center(31,text);
    if(director_browse){
        char heading[48];snprintf(heading,sizeof(heading),"%d TAKES / THIS LEVEL",director_take_count);arcade_center(46,heading);
        int first=(director_take_choice/6)*6;
        for(int i=first;i<director_take_count&&i<first+6;++i){
            int y=64+(i-first)*16;if(i==director_take_choice)arcade_box(16,y-2,288,15,108);
            arcade_ink=i==director_take_choice?4:0;
            unsigned stamp=0;sscanf(director_takes[i],"take_%u",&stamp);
            time_t when=stamp;struct tm *date=localtime(&when);char label[48];
            if(date)strftime(label,sizeof(label),"%d %b %y %H:%M:%S",date);else snprintf(label,sizeof(label),"TAKE %d",i+1);
            arcade_center(y,label);
        }
        arcade_ink=0;if(!director_take_count)arcade_center(95,"NO TAKES RECORDED YET");
        if(director_delete_confirm){
            arcade_ink=4;arcade_center(165,"DELETE SELECTED TAKE?");
            arcade_ink=0;arcade_center(180,"Y: DELETE / N: CANCEL");
        }else{
            arcade_center(165,"ENTER: PLAY / D: DELETE");
            arcade_center(180,"ESC: BACK");
        }
        arcade_screen=old;return;
    }
    const char *labels[]={"PLACE CAMERAS","REMOVE LAST CAMERA","RECORD NEW TAKE","STOP TAKE / CAMERA","TAKES ON THIS LEVEL","TOGGLE CAMERA VIEW","SAVE CAMERA SETUP","CLEAR ALL CAMERAS","BACK TO GAME"};
    for(int i=0;i<9;++i){int y=47+i*13;if(i==director_choice)arcade_box(16,y-1,288,13,108);arcade_ink=i==director_choice?4:0;arcade_center(y,labels[i]);}
    arcade_ink=0;arcade_center(166,"RECORD RESTARTS THIS LEVEL");arcade_center(180,director_status);arcade_screen=old;
}
static void director_tick(void){
    static unsigned serial=~0u;
    if(gamestate==GS_LEVEL&&serial!=r_levelserial){
        serial=r_levelserial;
        if(!director_demo&&!director_recording){
            director_edit=director_watch=director_mouse=0;
            director_hash=director_map_hash();director_count=0;
            char path[48];snprintf(path,sizeof(path),"director-%08x.cams",director_hash);director_read_cameras(path);
        }
    }

    if(director_recording&&director_demo&&demorecording&&(gamestate!=GS_LEVEL||players[consoleplayer].health<=0))G_CheckDemoStatus();
}

int M_DirectorTest(int step){
    if(step==0){director_open();director_count=0;angle_t angle=players[0].mo->angle;
        for(int i=0;i<8&&!director_count;++i){players[0].mo->angle=i*ANG45;director_place();}
        int close=director_count&&I_TestDirectorClosePlacement();
        players[0].mo->angle=angle;return director_count==1&&director_menu&&close&&I_TestDirectorNoBob();
    }
    if(step==1){
        director_edit=1;director_menu=0;menuactive=false;
        double dx=director_cameras[0].x-(double)players[0].mo->x/FRACUNIT;
        double dy=director_cameras[0].y-(double)players[0].mo->y/FRACUNIT;
        double a=atan2(dy,dx);if(a<0)a+=6.283185307179586;
        players[0].mo->angle=(angle_t)(a*(4294967296.0/6.283185307179586));
        return 1;
    }
    if(step==2){director_save();director_count=0;director_hash=0;director_open();if(director_count!=1)return 0;director_choice=2;director_action();return director_recording;}
    if(step==3){
        director_menu=1;menuactive=true;director_choice=3;director_action();
        if(!director_stop_confirm||!demorecording)return 0;
        event_t ev={ev_keydown,'n',0,0};director_input(&ev);
        if(director_stop_confirm||!demorecording)return 0;
        director_action();ev.data1='y';director_input(&ev);
        return !director_menu&&!director_watch&&!demorecording;
    }
    if(step==4){director_cameras[1]=director_cameras[0];director_cameras[1].x+=1;director_count=2;director_save();director_choice=4;director_action();if(!director_browse||!director_take_count)return 0;director_play_selected();return director_watch&&!director_menu&&director_count==2&&director_cameras[1].x==director_cameras[0].x+1;}
    if(step==5){
        if(director_demo||demoplayback||director_menu||director_watch||director_count!=2)return 0;
        director_camera_t saved=director_cameras[0];
        director_choice=7;director_action();
        char path[48];snprintf(path,sizeof(path),"director-%08x.cams",director_hash);
        director_count=1;if(!director_read_cameras(path)||director_count)return 0;
        director_cameras[0]=saved;director_count=1;director_choice=6;director_action();
        director_count=0;if(!director_read_cameras(path)||director_count!=1)return 0;
        director_open();director_choice=4;director_action();return director_browse&&director_take_count>0;
    }
    if(step==6){
        int found=-1;for(int i=0;i<director_take_count;++i)if(!strcmp(director_takes[i],director_last))found=i;
        if(found<0)return 0;director_take_choice=found;
        char name[32],path[48];strcpy(name,director_last);
        event_t ev={ev_keydown,'d',0,0};director_input(&ev);if(!director_delete_confirm)return 0;
        ev.data1=KEY_ESCAPE;director_input(&ev);if(director_delete_confirm)return 0;
        FILE *f=fopen(name,"rb");if(!f)return 0;fclose(f);
        int count=director_take_count;ev.data1='d';director_input(&ev);ev.data1='y';director_input(&ev);
        f=fopen(name,"rb");if(f){fclose(f);return 0;}
        snprintf(path,sizeof(path),"%s",name);strcpy(strrchr(path,'.'),".cams");
        f=fopen(path,"rb");if(f){fclose(f);return 0;}
        return director_take_count==count-1&&director_count==1;
    }
    return 0;
}
