#ifndef P_FLUID_H
#define P_FLUID_H
#include "p_mobj.h"
extern int p_fluidweapons, p_fluidmonsters;
int P_FluidWeaponsEnabled(void);
int P_FluidEnabled(void);
void P_FluidReset(void);
void P_FluidRemove(mobj_t *actor);
void P_FluidTick(mobj_t *actor);
int P_FluidChase(mobj_t *actor,actionf_p1 action);
fixed_t P_FluidMoveSpeed(mobj_t *actor);
fixed_t P_FluidFloatSpeed(mobj_t *actor);
#endif
