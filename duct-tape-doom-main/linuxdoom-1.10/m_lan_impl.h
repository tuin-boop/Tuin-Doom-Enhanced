/* In-game LAN lobby: keyboard navigation, no external window. */
#include "i_lan.h"
int lan_menu,lan_choice,lan_edit,lan_browse,lan_styled,lan_character;
static char lan_address[16]="192.168.1.";
void M_LanStarted(void){lan_menu=lan_edit=lan_browse=lan_character=0;M_ClearMenus();}
static void M_Multiplayer(int choice){
    if(netgame&&!lan_session){M_StartMessage("ALREADY IN A NETWORK GAME",NULL,false);return;}
    if(demorecording||arcade_active||director_watch||director_edit){M_StartMessage("END THE CURRENT SPECIAL MODE FIRST",NULL,false);return;}
    lan_menu=1;lan_choice=0;lan_edit=lan_browse=lan_character=0;menuactive=true;
    if(!G_MapExists(lan_episode,lan_map)){lan_episode=gameepisode;lan_map=gamemap;}
}
void M_OpenLanMenu(void){M_StartControlPanel();M_Multiplayer(0);}
void M_LanRow(int row,char *text,int size){
    const char *mode[]={"COOPERATIVE","DEATHMATCH","ALT DEATHMATCH"};
    const char *skill[]={"VERY EASY","EASY","NORMAL","ULTRA-VIOLENCE","NIGHTMARE"};
    text[0]=0;
    if(lan_character){const char *colors[]={"Green","Gray","Brown","Red"};if(row==0)snprintf(text,size,"Suit colour: %s",colors[lan_color&3]);if(row==1)snprintf(text,size,"Back to multiplayer");return;}
    if(lan_browse){
        int i=(lan_choice/8)*8+row;if(i>=lan_game_count)return;
        lan_game_t *g=&lan_games[i];
        snprintf(text,size,"%.22s  %d/4  %s",g->name,g->players,!g->compatible?"MISMATCH":g->players==4?"FULL":g->running?"PLAYING":"LOBBY");return;
    }
    if(row==0)snprintf(text,size,"Host new game");
    if(row==1)snprintf(text,size,"Browse LAN games");
    if(row==2)snprintf(text,size,"Join by IP: %s%s",lan_address,lan_edit?"_":"");
    if(row==3){if(gamemode==commercial)snprintf(text,size,"Map: MAP%02d",lan_map);else snprintf(text,size,"Map: E%dM%d",lan_episode,lan_map);}
    if(row==4)snprintf(text,size,"Skill: %s",skill[lan_skill]);
    if(row==5)snprintf(text,size,"Mode: %s",mode[lan_mode]);
    if(row==7)snprintf(text,size,"Co-op friendly fire: %s",lan_friendlyfire?"On":"Off");
    if(row==8)snprintf(text,size,"Character / suit colour");
    if(row==6)snprintf(text,size,lan_session?"Match live / %d connected":"Start match / %d players",lan_count);
}
static void lan_draw(void){
    if(!lan_menu||lan_styled)return;
    char text[100];arcade_ink=0;
    arcade_box(10,12,300,178,0);arcade_box(10,12,300,2,176);
    arcade_font(24,23,lan_browse?"BROWSE LAN GAMES":"MULTIPLAYER / LAN",1);
    for(int i=0;i<(lan_character?2:lan_browse?8:9);i++){
        if(i==(lan_browse?lan_choice%8:lan_choice))arcade_box(21,38+i*12,278,12,24);
        M_LanRow(i,text,sizeof(text));arcade_font(25,40+i*12,text,1);
    }
    arcade_font(22,151,lan_status,1);
    arcade_font(22,165,"ARROWS: SELECT  ENTER: CHOOSE",1);
    arcade_font(22,178,lan_browse?"R: REFRESH / ESC: BACK":"JOIN ANYTIME / ESC: BACK",1);
}
static int lan_input(event_t *ev){
    if(!lan_menu)return 0;
    if(ev->type!=ev_keydown)return 1;
    int key=ev->data1;size_t n=strlen(lan_address);
    if(lan_character){
        if(key==KEY_ESCAPE||(key==KEY_ENTER&&lan_choice==1)){lan_character=0;lan_choice=8;return 1;}
        if(key==KEY_UPARROW||key==KEY_DOWNARROW)lan_choice=1-lan_choice;
        if(lan_choice==0&&(key==KEY_LEFTARROW||key==KEY_RIGHTARROW||key==KEY_ENTER))lan_color=(lan_color+(key==KEY_LEFTARROW?3:1))%4;
        return 1;
    }
    if(lan_browse){
        if(lan_choice>=lan_game_count)lan_choice=lan_game_count?lan_game_count-1:0;
        if(key==KEY_ESCAPE){lan_browse=0;lan_choice=1;I_LanClose();return 1;}
        if(key=='r'){I_LanBrowse();lan_choice=0;return 1;}
        if(lan_game_count){
            if(key==KEY_UPARROW)lan_choice=(lan_choice+lan_game_count-1)%lan_game_count;
            if(key==KEY_DOWNARROW)lan_choice=(lan_choice+1)%lan_game_count;
            if(key==KEY_ENTER){
                lan_game_t *g=&lan_games[lan_choice];
                if(!g->compatible)strcpy(lan_status,"USE THE SAME BUILD, IWAD AND ADD-ONS");
                else if(g->players>=4)strcpy(lan_status,"THIS GAME IS FULL");
                else{strcpy(lan_address,g->address);lan_browse=0;lan_choice=2;I_LanOpen(lan_address);}
            }
        }
        return 1;
    }
    if(lan_edit){
        if(key==KEY_ESCAPE){lan_edit=0;return 1;}
        if(key==KEY_ENTER){lan_edit=0;I_LanOpen(lan_address);return 1;}
        if(key==KEY_BACKSPACE){if(n)lan_address[n-1]=0;return 1;}
        if(n<15&&((key>='0'&&key<='9')||key=='.')){lan_address[n]=(char)key;lan_address[n+1]=0;}return 1;
    }
    if(key==KEY_ESCAPE){if(!lan_session)I_LanClose();lan_menu=0;M_ClearMenus();return 1;}
    if(key==KEY_UPARROW)lan_choice=(lan_choice+8)%9;
    if(key==KEY_DOWNARROW)lan_choice=(lan_choice+1)%9;
    if((key==KEY_LEFTARROW||key==KEY_RIGHTARROW)&&!lan_lobby&&!lan_session){
        int step=key==KEY_RIGHTARROW?1:-1;
        if(lan_choice==3)G_StepMap(step,&lan_episode,&lan_map);
        if(lan_choice==4)lan_skill=(lan_skill+step+5)%5;
        if(lan_choice==5)lan_mode=(lan_mode+step+3)%3;
    }
    if(lan_choice==7&&!lan_session&&(!lan_lobby||lan_host)&&(key==KEY_LEFTARROW||key==KEY_RIGHTARROW||key==KEY_ENTER))lan_friendlyfire=!lan_friendlyfire;
    if(key==KEY_ENTER){
        if(lan_choice==8){lan_character=1;lan_choice=0;return 1;}
        if(lan_choice==0&&!lan_lobby&&!lan_session)I_LanOpen(NULL);
        if(lan_choice==1&&!lan_lobby&&!lan_session){lan_browse=1;lan_choice=0;I_LanBrowse();return 1;}
        if(lan_choice==2&&!lan_lobby&&!lan_session)lan_edit=1;
        if(lan_choice==6)I_LanStart();
    }
    return 1;
}

void M_LanKey(int key){event_t e={ev_keydown,0,0,0};e.data1=key;lan_input(&e);}
