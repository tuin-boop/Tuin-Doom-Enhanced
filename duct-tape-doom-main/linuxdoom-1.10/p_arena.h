#ifndef P_ARENA_H
#define P_ARENA_H
#include "p_local.h"
#define MF_ARENA 0x10000000
#define MF_ARENAOLD 0x20000000
extern int p_keyarena,p_arenaradius,p_monsterrecovery;
extern void (*R_ArenaAnnouncement)(int safe);
void P_ArenaTick(void);
void P_ArenaForgetMonster(mobj_t *monster);
void P_ArenaMoveResult(mobj_t *monster,int moved);
int P_ArenaWave(void);
void P_ArenaKey(player_t *player);
int P_ArenaBlocks(mobj_t *thing,fixed_t x,fixed_t y);
int P_ArenaView(float *x,float *y,float *radius,int *remaining);
int P_ArenaSave(unsigned char *data);
int P_ArenaLoad(const unsigned char *data,int length);
#endif
