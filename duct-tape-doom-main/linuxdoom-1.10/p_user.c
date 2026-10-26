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
// $Log:$
//
// DESCRIPTION:
//	Player related stuff.
//	Bobbing POV/weapon, movement.
//	Pending weapon.
//
//-----------------------------------------------------------------------------


static const char
rcsid[] = "$Id: p_user.c,v 1.3 1997/01/28 22:08:29 b1 Exp $";


#include "p_notices.h"
#include "doomdef.h"
#include "d_event.h"

#include "p_local.h"

#include "doomstat.h"

#include <math.h>



// Index of the special effects (INVUL inverse) map.
#define INVERSECOLORMAP		32


//
// Movement.
//

// 16 pixels of bob
#define MAXBOB	0x100000	

boolean		onground;

// Demos store input only, so recording/playback must retain original physics.
boolean P_UseUZMovement(void) { return uzmovement && !demoplayback && !demorecording && !netgame; }
boolean P_UseResponsiveMovement (void)
{
    return !classicmovement && !P_UseUZMovement() && !demoplayback && !demorecording && !netgame;
}


//
// Renderer debugging cheats (F5 menu). Flying follows the view direction
// (E/Q rise and sink); god mode and flight also ignore special sectors so
// damaging and exit floors cannot end the visit. The settings outlive map
// changes, unlike player->cheats.
//
extern boolean	gamekeydown[];
extern int	key_speed;

float		tourpitch;
boolean		freeaim;
boolean		classicaim;

boolean P_DebugAllowed (void)
{
    return !demoplayback && !demorecording && !netgame;
}

boolean P_Flying (void)
{
    return debugfly && P_DebugAllowed ();
}

// "No target": monsters neither see nor hear the player.
boolean P_Unseen (mobj_t* mo)
{
    return mo && mo->player && debugnotarget && P_DebugAllowed ();
}

void P_SetCheat (player_t* player, boolean* setting, boolean on)
{
    int		bit = setting == &debugnoclip ? CF_NOCLIP
		    : setting == &debuggod ? CF_GODMODE
		    : setting == &debugfly ? CF_NOMOMENTUM : 0;

    *setting = on;
    if (on)
	player->cheats |= bit;
    else
	player->cheats &= ~bit;
    if (setting == &debuggod && on)
    {
	player->health = 100;
	if (player->mo)
	    player->mo->health = 100;
    }
    if (setting == &debugfly && !on && player->mo)
	player->mo->flags &= ~MF_NOGRAVITY;
}

void P_SetTour (player_t* player, boolean on)
{
    P_SetCheat (player, &debugfly, on);
    P_SetCheat (player, &debugnoclip, on);
    P_SetCheat (player, &debuggod, on);
    debugnomonsters = on;
}

static void P_RemoveMonsters (void)
{
    thinker_t*	th;
    mobj_t*	mo;

    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	if (th->function.acp1 != (actionf_p1)P_MobjThinker)
	    continue;
	mo = (mobj_t *)th;
	if (!mo->player && (mo->type == MT_SKULL || (mo->flags & MF_COUNTKILL)))
	    P_RemoveMobj (mo);
    }
}

static void P_FlyMove (player_t* player)
{
    ticcmd_t*	cmd = &player->cmd;
    mobj_t*	mo = player->mo;
    fixed_t	speed = FRACUNIT*2/5;
    fixed_t	forward = cmd->forwardmove*speed;
    fixed_t	side = cmd->sidemove*speed;
    fixed_t	flat = (fixed_t)(cos (tourpitch)*FRACUNIT);
    fixed_t	rise = (fixed_t)(sin (tourpitch)*FRACUNIT);
    angle_t	a = mo->angle >> ANGLETOFINESHIFT;
    angle_t	s = (mo->angle-ANG90) >> ANGLETOFINESHIFT;
    fixed_t	climb = gamekeydown[key_speed] ? 20*FRACUNIT : 10*FRACUNIT;

    forward = FixedMul (forward, flat);
    mo->momx = FixedMul (forward, finecosine[a]) + FixedMul (side, finecosine[s]);
    mo->momy = FixedMul (forward, finesine[a]) + FixedMul (side, finesine[s]);
    mo->momz = FixedMul (cmd->forwardmove*speed, rise);
    if (gamekeydown['e'])
	mo->momz += climb;
    if (gamekeydown['q'])
	mo->momz -= climb;
}


//
// P_Thrust
// Moves the given origin along a given angle.
//
void
P_Thrust
( player_t*	player,
  angle_t	angle,
  fixed_t	move ) 
{
    angle >>= ANGLETOFINESHIFT;
    
    player->mo->momx += FixedMul(move,finecosine[angle]); 
    player->mo->momy += FixedMul(move,finesine[angle]);
}




//
// P_CalcHeight
// Calculate the walking / running height adjustment
//
void P_CalcHeight (player_t* player) 
{
    int		angle;
    fixed_t	bob;
    
    // Regular movement bobbing
    // (needs to be calculated for gun swing
    // even if not on ground)
    // OPTIMIZE: tablify angle
    // Note: a LUT allows for effects
    //  like a ramp with low health.
    player->bob =
	FixedMul (player->mo->momx, player->mo->momx)
	+ FixedMul (player->mo->momy,player->mo->momy);
    
    player->bob >>= 2;

    if (player->bob>MAXBOB)
	player->bob = MAXBOB;

    if (P_UseResponsiveMovement ())
        player->bob /= 2;

    if ((player->cheats & CF_NOMOMENTUM) || !onground)
    {
	player->viewz = player->mo->z + VIEWHEIGHT;

	if (player->viewz > player->mo->ceilingz-4*FRACUNIT)
	    player->viewz = player->mo->ceilingz-4*FRACUNIT;

	player->viewz = player->mo->z + player->viewheight;
	return;
    }
		
    angle = (FINEANGLES/20*leveltime)&FINEMASK;
    bob = FixedMul ( player->bob/2, finesine[angle]);

    
    if(P_UseUZMovement())bob /= 4; /* User UZDoom movebob=0.25. */
    // move viewheight
    if (player->playerstate == PST_LIVE)
    {
	player->viewheight += player->deltaviewheight;

	if (player->viewheight > VIEWHEIGHT)
	{
	    player->viewheight = VIEWHEIGHT;
	    player->deltaviewheight = 0;
	}

	if (player->viewheight < VIEWHEIGHT/2)
	{
	    player->viewheight = VIEWHEIGHT/2;
	    if (player->deltaviewheight <= 0)
		player->deltaviewheight = 1;
	}
	
	if (player->deltaviewheight)	
	{
	    player->deltaviewheight += FRACUNIT/4;
	    if (!player->deltaviewheight)
		player->deltaviewheight = 1;
	}
    }
    player->viewz = player->mo->z + player->viewheight + bob;

    if (player->viewz > player->mo->ceilingz-4*FRACUNIT)
	player->viewz = player->mo->ceilingz-4*FRACUNIT;
}



//
// P_MovePlayer
//
void P_MovePlayer (player_t* player)
{
    ticcmd_t*		cmd;
    // Match original terminal speed with 3/4 friction: (1-3/4)/(1-29/32).
    int thrust = P_UseResponsiveMovement () ? 2048*8/3 : 2048;
	
    cmd = &player->cmd;
	
    player->mo->angle += (cmd->angleturn<<16);

    if (P_Flying ())
    {
	onground = false;
	P_FlyMove (player);
	return;
    }

    // Do not let the player control movement
    //  if not onground.
    onground = (player->mo->z <= player->mo->floorz);
	
    if(P_UseUZMovement()) {
        if(onground && player->mo->momz==0 && gamekeydown[' '] && player->mo->ceilingz-player->mo->z>player->mo->height)player->mo->momz=8*FRACUNIT;
        if(!onground)thrust=8; /* 2048 * default aircontrol (1/256). */
    }
    if (cmd->forwardmove && (onground || P_UseUZMovement()))
	P_Thrust (player, player->mo->angle, cmd->forwardmove*thrust);
    
    if (cmd->sidemove && (onground || P_UseUZMovement()))
	P_Thrust (player, player->mo->angle-ANG90, cmd->sidemove*thrust);

    if ( (cmd->forwardmove || cmd->sidemove) 
	 && player->mo->state == &states[S_PLAY] )
    {
	P_SetMobjState (player->mo, S_PLAY_RUN1);
    }
}	



//
// P_DeathThink
// Fall on your face when dying.
// Decrease POV height to floor height.
//
#define ANG5   	(ANG90/18)

void P_DeathThink (player_t* player)
{
    angle_t		angle;
    angle_t		delta;

    P_MovePsprites (player);
	
    // fall to the ground
    if (player->viewheight > 6*FRACUNIT)
	player->viewheight -= FRACUNIT;

    if (player->viewheight < 6*FRACUNIT)
	player->viewheight = 6*FRACUNIT;

    player->deltaviewheight = 0;
    onground = (player->mo->z <= player->mo->floorz);
    P_CalcHeight (player);
	
    if (player->attacker && player->attacker != player->mo)
    {
	angle = R_PointToAngle2 (player->mo->x,
				 player->mo->y,
				 player->attacker->x,
				 player->attacker->y);
	
	delta = angle - player->mo->angle;
	
	if (delta < ANG5 || delta > (unsigned)-ANG5)
	{
	    // Looking at killer,
	    //  so fade damage flash down.
	    player->mo->angle = angle;

	    if (player->damagecount)
		player->damagecount--;
	}
	else if (delta < ANG180)
	    player->mo->angle += ANG5;
	else
	    player->mo->angle -= ANG5;
    }
    else if (player->damagecount)
	player->damagecount--;
	

    if (player->cmd.buttons & BT_USE)
	player->playerstate = PST_REBORN;
}



//
// P_PlayerThink
//
void P_PlayerThink (player_t* player)
{
    P_NoticeTick(player);
    ticcmd_t*		cmd;
    weapontype_t	newweapon;
	
    if (P_DebugAllowed ())
    {
	if (debugnoclip)
	    player->cheats |= CF_NOCLIP;
	if (debuggod)
	    player->cheats |= CF_GODMODE;
	if (debugfly)
	{
	    player->cheats |= CF_NOMOMENTUM;
	    player->mo->flags |= MF_NOGRAVITY;
	}
	if (debugnomonsters)
	    P_RemoveMonsters ();
    }

    // fixme: do this in the cheat code
    if (player->cheats & CF_NOCLIP)
	player->mo->flags |= MF_NOCLIP;
    else
	player->mo->flags &= ~MF_NOCLIP;
    
    // chain saw run forward
    cmd = &player->cmd;
    if (player->mo->flags & MF_JUSTATTACKED)
    {
	cmd->angleturn = 0;
	cmd->forwardmove = 0xc800/512;
	cmd->sidemove = 0;
	player->mo->flags &= ~MF_JUSTATTACKED;
    }
			
	
    if (player->playerstate == PST_DEAD)
    {
	P_DeathThink (player);
	return;
    }
    
    // Move around.
    // Reactiontime is used to prevent movement
    //  for a bit after a teleport.
    if (player->mo->reactiontime)
	player->mo->reactiontime--;
    else
	P_MovePlayer (player);
    
    P_CalcHeight (player);

    if (player->mo->subsector->sector->special
	&& !((debugfly || debuggod) && P_DebugAllowed ()))
	P_PlayerInSpecialSector (player);
    
    // Check for weapon change.

    // A special event has no other buttons.
    if (cmd->buttons & BT_SPECIAL)
	cmd->buttons = 0;			
		
    if (cmd->buttons & BT_CHANGE)
    {
	// The actual changing of the weapon is done
	//  when the weapon psprite can do it
	//  (read: not in the middle of an attack).
	newweapon = (cmd->buttons&BT_WEAPONMASK)>>BT_WEAPONSHIFT;
	
	if (newweapon == wp_fist
	    && player->weaponowned[wp_chainsaw]
	    && !(player->readyweapon == wp_chainsaw
		 && (player->powers[pw_strength] || (!demoplayback && !demorecording))))
	{
	    newweapon = wp_chainsaw;
	}
	
	if ( (gamemode == commercial)
	    && newweapon == wp_shotgun 
	    && player->weaponowned[wp_supershotgun]
	    && player->readyweapon != wp_supershotgun)
	{
	    newweapon = wp_supershotgun;
	}
	

	if (player->weaponowned[newweapon]
	    && newweapon != player->readyweapon)
	{
	    // Do not go to plasma or BFG in shareware,
	    //  even if cheated.
	    if ((newweapon != wp_plasma
		 && newweapon != wp_bfg)
		|| (gamemode != shareware) )
	    {
		player->pendingweapon = newweapon;
	    }
	}
    }
    
    // check for use
    if (cmd->buttons & BT_USE)
    {
	if (!player->usedown)
	{
	    P_UseLines (player);
	    player->usedown = true;
	}
    }
    else
	player->usedown = false;
    
    // cycle psprites
    P_MovePsprites (player);
    
    // Counters, time dependend power ups.

    // Strength counts up to diminish fade.
    if (player->powers[pw_strength])
	player->powers[pw_strength]++;	
		
    if (player->powers[pw_invulnerability])
	player->powers[pw_invulnerability]--;

    if (player->powers[pw_invisibility])
	if (! --player->powers[pw_invisibility] )
	    player->mo->flags &= ~MF_SHADOW;
			
    if (player->powers[pw_infrared])
	player->powers[pw_infrared]--;
		
    if (player->powers[pw_ironfeet])
	player->powers[pw_ironfeet]--;
		
    if (player->damagecount)
	player->damagecount--;
		
    if (player->bonuscount)
	player->bonuscount--;

    
    // Handling colormaps.
    if (player->powers[pw_invulnerability])
    {
	if (player->powers[pw_invulnerability] > 4*32
	    || (player->powers[pw_invulnerability]&8) )
	    player->fixedcolormap = INVERSECOLORMAP;
	else
	    player->fixedcolormap = 0;
    }
    else if (player->powers[pw_infrared])	
    {
	if (player->powers[pw_infrared] > 4*32
	    || (player->powers[pw_infrared]&8) )
	{
	    // almost full bright
	    player->fixedcolormap = 1;
	}
	else
	    player->fixedcolormap = 0;
    }
    else
	player->fixedcolormap = 0;
}



