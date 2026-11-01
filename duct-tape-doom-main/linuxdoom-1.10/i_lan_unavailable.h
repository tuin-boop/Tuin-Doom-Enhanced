/* Backends without the SDL LAN transport retain a complete single-player link. */
#include "i_lan.h"
int lan_lobby,lan_host,lan_count,lan_pending,lan_session,lan_catchingup,lan_slot;
int lan_episode=1,lan_map=1,lan_skill=3,lan_mode;
int lan_friendlyfire=0,lan_color=0,lan_wheelweapon=0;
uint32_t lan_wheelserial;
int16_t lan_pitch[4];uint8_t lan_colors[4];
char lan_status[80]="LAN REQUIRES THE SDL BUILD";
int I_LanOpen(const char *address){(void)address;return 0;}
void I_LanClose(void){}
void I_LanPoll(void){}
void I_LanStart(void){}
void I_LanActivate(void){}

int I_LanGetFrame(int t,lan_frame_t *f){return 0;}
void I_LanInput(const ticcmd_t *c){}
unsigned I_LanMask(void){return 1;}
void I_LanHostCommands(ticcmd_t *c){}
void I_LanHostInputs(lan_input_t *c){}
void I_LanRecord(const lan_frame_t *f){}
void I_LanCaughtUp(int t){}

lan_game_t lan_games[16];int lan_game_count;void I_LanBrowse(void){}
