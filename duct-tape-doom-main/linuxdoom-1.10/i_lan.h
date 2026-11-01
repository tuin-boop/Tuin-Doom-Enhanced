#ifndef I_LAN_H
#define I_LAN_H
#include "d_ticcmd.h"
#include <stdint.h>
typedef struct {ticcmd_t cmd;int16_t pitch;uint8_t color,weapon;uint32_t weaponserial;} lan_input_t;
typedef struct {lan_input_t inputs[4];uint32_t hash;unsigned char mask;} lan_frame_t;
extern int lan_friendlyfire,lan_color;
extern int16_t lan_pitch[4];
extern uint8_t lan_colors[4];
extern int lan_wheelweapon;
extern uint32_t lan_wheelserial;
void I_LanHostInputs(lan_input_t *inputs);
extern int lan_session,lan_catchingup,lan_slot;
int I_LanGetFrame(int tic,lan_frame_t *frame);
void I_LanInput(const ticcmd_t *cmd);
unsigned I_LanMask(void);
void I_LanHostCommands(ticcmd_t *cmds);
void I_LanRecord(const lan_frame_t *frame);
void I_LanCaughtUp(int tic);
/* Bounded, nonblocking in-game lobby. Gameplay still uses Doom lockstep. */
extern int lan_lobby, lan_host, lan_count, lan_pending;
extern int lan_episode, lan_map, lan_skill, lan_mode;
extern char lan_status[80];
int I_LanOpen(const char *address);
void I_LanClose(void);
void I_LanPoll(void);
void I_LanStart(void);
void I_LanActivate(void);
typedef struct {uint64_t id;char address[16],name[32];int players,running,episode,map,skill,mode,compatible,seen;} lan_game_t;
extern lan_game_t lan_games[16];
extern int lan_game_count;
void I_LanBrowse(void);
extern int lan_menu,lan_choice,lan_edit,lan_browse,lan_styled,lan_character;
void M_LanKey(int key);
void M_LanRow(int row,char *text,int size);
#endif
