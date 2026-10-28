// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//	Play functions, animation, global header.
//
//-----------------------------------------------------------------------------


#ifndef __P_LOCAL__
#define __P_LOCAL__

#ifndef __R_LOCAL__
#include "r_local.h"
#endif

#define FLOATSPEED		(FRACUNIT*4)


#define MAXHEALTH		100
#define VIEWHEIGHT		(41*FRACUNIT)

// mapblocks are used to check movement
// against lines and things
#define MAPBLOCKUNITS	128
#define MAPBLOCKSIZE	(MAPBLOCKUNITS*FRACUNIT)
#define MAPBLOCKSHIFT	(FRACBITS+7)
#define MAPBMASK		(MAPBLOCKSIZE-1)
#define MAPBTOFRAC		(MAPBLOCKSHIFT-FRACBITS)


// player radius for movement checking
#define PLAYERRADIUS	16*FRACUNIT

// MAXRADIUS is for precalculated sector block boxes
// the spider demon is larger,
// but we do not have any moving sectors nearby
#define MAXRADIUS		32*FRACUNIT

#define GRAVITY		FRACUNIT
#define MAXMOVE		(30*FRACUNIT)

#define USERANGE		(64*FRACUNIT)
#define MELEERANGE		(64*FRACUNIT)
#define MISSILERANGE	(32*64*FRACUNIT)

// follow a player exlusively for 3 seconds
#define	BASETHRESHOLD	 	100



//
// P_TICK
//

// both the head and tail of the thinker list
extern	thinker_t	thinkercap;	


void P_InitThinkers (void);
void P_AddThinker (thinker_t* thinker);
void P_RemoveThinker (thinker_t* thinker);


//
// P_PSPR
//
void P_SetupPsprites (player_t* curplayer);
void P_MovePsprites (player_t* curplayer);
void P_DropWeapon (player_t* player);


//
// P_USER
//
void	P_PlayerThink (player_t* player);
boolean P_UseResponsiveMovement (void);
boolean P_DebugAllowed (void);
boolean P_Flying (void);
boolean P_Unseen (mobj_t* mo);
void P_SetCheat (player_t* player, boolean* setting, boolean on);
void P_SetTour (player_t* player, boolean on);
// View pitch in radians, written by the 3D renderer; flight follows it.
boolean P_UseUZMovement(void);
extern float tourpitch;
// Mouselook is on (3D renderer): a player's shot follows tourpitch
// (P_BulletSlope).
extern boolean freeaim;
// With freeaim: a shot autoaims at a monster in front as in vanilla, and
// follows tourpitch only with none.
extern boolean classicaim;
// The next P_LineAttack is such a shot: it starts at the eye and also stops
// on floors and ceilings. P_LineAttack clears it.
extern void (*P_ShotTraced)(mobj_t *shooter,mobj_t *hit,fixed_t x1,fixed_t y1,fixed_t z1,fixed_t x2,fixed_t y2,fixed_t z2);
extern boolean la_freeaim;
// Where each hitscan shot went: from the shooter to where it stopped, and
// what it hit (NULL for walls, floors, the sky or the end of its range).
// For sounds that follow the bullet, such as a near miss whizzing past.
extern void (*P_ShotTraced)(mobj_t *shooter, mobj_t *hit, fixed_t x1, fixed_t y1, fixed_t z1,
                            fixed_t x2, fixed_t y2, fixed_t z2);


//
// P_MOBJ
//
#define ONFLOORZ		MININT
#define ONCEILINGZ		MAXINT

// Time interval for item respawning.
#define ITEMQUESIZE		128

extern mapthing_t	itemrespawnque[ITEMQUESIZE];
extern int		itemrespawntime[ITEMQUESIZE];
extern int		iquehead;
extern int		iquetail;


void P_RespawnSpecials (void);

mobj_t*
P_SpawnMobj
( fixed_t	x,
  fixed_t	y,
  fixed_t	z,
  mobjtype_t	type );

void 	P_RemoveMobj (mobj_t* th);
boolean	P_SetMobjState (mobj_t* mobj, statenum_t state);
void 	P_MobjThinker (mobj_t* mobj);

void	P_SpawnPuff (fixed_t x, fixed_t y, fixed_t z);
void 	P_SpawnBlood (fixed_t x, fixed_t y, fixed_t z, int damage);
mobj_t* P_SpawnMissile (mobj_t* source, mobj_t* dest, mobjtype_t type);
void	P_SpawnPlayerMissile (mobj_t* source, mobjtype_t type);


//
// P_ENEMY
//
void P_NoiseAlert (mobj_t* target, mobj_t* emmiter);


//
// P_MAPUTL
//
typedef struct
{
    fixed_t	x;
    fixed_t	y;
    fixed_t	dx;
    fixed_t	dy;
    
} divline_t;

typedef struct
{
    fixed_t	frac;		// along trace line
    boolean	isaline;
    union {
	mobj_t*	thing;
	line_t*	line;
    }			d;
} intercept_t;

// Grows on demand; vanilla's 128 entries overflowed into other globals.
extern intercept_t*	intercepts;
extern intercept_t*	intercept_p;
void P_CheckIntercepts (void);

typedef boolean (*traverser_t) (intercept_t *in);

fixed_t P_AproxDistance (fixed_t dx, fixed_t dy);
int 	P_PointOnLineSide (fixed_t x, fixed_t y, line_t* line);
int 	P_PointOnDivlineSide (fixed_t x, fixed_t y, divline_t* line);
void 	P_MakeDivline (line_t* li, divline_t* dl);
fixed_t P_InterceptVector (divline_t* v2, divline_t* v1);
int 	P_BoxOnLineSide (fixed_t* tmbox, line_t* ld);

extern fixed_t		opentop;
extern fixed_t 		openbottom;
extern fixed_t		openrange;
extern fixed_t		lowfloor;

void 	P_LineOpening (line_t* linedef);

boolean P_BlockLinesIterator (int x, int y, boolean(*func)(line_t*) );
boolean P_BlockThingsIterator (int x, int y, boolean(*func)(mobj_t*) );

#define PT_ADDLINES		1
#define PT_ADDTHINGS	2
#define PT_EARLYOUT		4

extern divline_t	trace;

boolean
P_PathTraverse
( fixed_t	x1,
  fixed_t	y1,
  fixed_t	x2,
  fixed_t	y2,
  int		flags,
  boolean	(*trav) (intercept_t *));

void P_UnsetThingPosition (mobj_t* thing);
void P_SetThingPosition (mobj_t* thing);


//
// P_MAP
//

// If "floatok" true, move would be ok
// if within "tmfloorz - tmceilingz".
extern boolean		floatok;
extern fixed_t		tmfloorz;
extern fixed_t		tmceilingz;


extern	line_t*		ceilingline;

boolean P_CheckPosition (mobj_t *thing, fixed_t x, fixed_t y);
boolean P_TryMove (mobj_t* thing, fixed_t x, fixed_t y);
boolean P_TeleportMove (mobj_t* thing, fixed_t x, fixed_t y);
void	P_SlideMove (mobj_t* mo);
boolean P_CheckSight (mobj_t* t1, mobj_t* t2);
void 	P_UseLines (player_t* player);

boolean P_ChangeSector (sector_t* sector, boolean crunch);

extern mobj_t*	linetarget;	// who got hit (or NULL)

fixed_t
P_AimLineAttack
( mobj_t*	t1,
  angle_t	angle,
  fixed_t	distance );

void
P_LineAttack
( mobj_t*	t1,
  angle_t	angle,
  fixed_t	distance,
  fixed_t	slope,
  int		damage );

void
P_RadiusAttack
( mobj_t*	spot,
  mobj_t*	source,
  int		damage );



//
// P_SETUP
//
extern byte*		rejectmatrix;	// for fast sight rejection
extern int32_t*		blockmaplump;	// offsets in blockmap are from here
extern int32_t*		blockmap;
extern int		bmapwidth;
extern int		bmapheight;	// in mapblocks
extern fixed_t		bmaporgx;
extern fixed_t		bmaporgy;	// origin of block map
extern mobj_t**		blocklinks;	// for thing chains



//
// P_INTER
//
extern int		maxammo[NUMAMMO];
extern int		clipammo[NUMAMMO];

void
P_TouchSpecialThing
( mobj_t*	special,
  mobj_t*	toucher );

void
P_DamageMobj
( mobj_t*	target,
  mobj_t*	inflictor,
  mobj_t*	source,
  int		damage );


//
// P_SPEC
//
#include "p_spec.h"


/* Optional single-player obstacle navigation. */
extern int p_persistentpursuit,p_aggressiveai,p_advancedtactics;
void P_SetAdvancedTactics(int enabled);
boolean P_TacticsEnabled(void);
boolean P_TacticsMove(mobj_t *actor);
boolean P_TacticsHold(mobj_t *actor);
boolean P_TacticsCanFire(mobj_t *actor);
boolean P_TacticsAim(mobj_t *actor,mobj_t *aim);
void P_TacticsTick(mobj_t *actor);
void P_TacticsFired(mobj_t *actor);
void P_TacticsRemove(mobj_t *actor);
boolean P_TacticsAttackBlocked(mobj_t *actor);
void P_TacticsChargeBlocked(mobj_t *actor);
angle_t P_TacticsFatSpread(mobj_t *actor);
void P_SetAggressiveAI(int enabled);
boolean P_AggressiveEnabled(void);
int P_AggressiveStateTics(mobj_t *actor,statenum_t state,int tics);
void P_AggressiveContact(mobj_t *victim,mobj_t *inflictor,mobj_t *source);
void P_AggressiveDamage(mobj_t *victim,mobj_t *inflictor,mobj_t *source,int damage);
void P_AggressiveMissile(mobj_t *missile,mobj_t *source);
void P_AggressiveImpact(mobj_t *missile);
void P_AggressiveRemove(mobj_t *actor);
boolean P_AggressiveBarrageAim(mobj_t *actor,mobj_t *aim);
boolean P_PersistentPursuit(mobj_t *actor);

extern int (*P_PeekOriginHook)(mobj_t*,fixed_t*,fixed_t*,fixed_t*);
boolean P_PeekShotOrigin(mobj_t *actor, fixed_t *x, fixed_t *y, fixed_t *z);
int P_RomeroStick(mobj_t *mo,float nx,float ny,float nz);
#endif	// __P_LOCAL__
//-----------------------------------------------------------------------------
//
// $Log:$
//
//-----------------------------------------------------------------------------




void P_RomeroDrunk(mobj_t *source);
