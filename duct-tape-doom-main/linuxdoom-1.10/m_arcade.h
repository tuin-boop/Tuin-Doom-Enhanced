#ifndef M_ARCADE_H
#define M_ARCADE_H
#include "doomdef.h"
#include "d_player.h"
extern int arcade_active;
int Arcade_MenuOpen(void);
void Arcade_Open(int choice);
void Arcade_NewGame(void);
void Arcade_Cancel(void);
int Arcade_Finish(int won);
void Arcade_Damage(mobj_t *source,mobj_t *target,int damage);
void Arcade_Impact(mobj_t *inflictor,mobj_t *source,mobj_t *target);
void Arcade_TrackProjectile(mobj_t *projectile);
void Arcade_RemoveProjectile(mobj_t *projectile);
void Arcade_EndShot(void);
void Arcade_Kill(mobj_t *source,mobj_t *target);
void Arcade_Pickup(player_t *player,mobj_t *item);
void Arcade_Shot(player_t *player);
#endif
