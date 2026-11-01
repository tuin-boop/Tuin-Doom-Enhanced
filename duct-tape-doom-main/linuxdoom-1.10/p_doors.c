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
// DESCRIPTION: Door animation code (opening/closing)
//
//-----------------------------------------------------------------------------

static const char
rcsid[] = "$Id: p_doors.c,v 1.4 1997/02/03 16:47:53 b1 Exp $";


#include "z_zone.h"
#include "doomdef.h"
#include "p_local.h"

#include "s_sound.h"


// State.
#include "doomstat.h"
#include "r_state.h"

// Data.
#include "dstrings.h"
#include "sounds.h"
#include "d_deh.h"

#if 0
//
// Sliding door frame information
//
slidename_t	slideFrameNames[MAXSLIDEDOORS] =
{
    {"GDOORF1","GDOORF2","GDOORF3","GDOORF4",	// front
     "GDOORB1","GDOORB2","GDOORB3","GDOORB4"},	// back
	 
    {"\0","\0","\0","\0"}
};
#endif


//
// VERTICAL DOORS
//

//
// T_VerticalDoor
//
void T_VerticalDoor (vldoor_t* door)
{
    result_e	res;
    fixed_t oldheight = door->sector->ceilingheight;
	
    switch(door->direction)
    {
      case 0:
	// WAITING
	if (!--door->topcountdown)
	{
	    switch(door->type)
	    {
	      case blazeRaise:
		door->direction = -1; // time to go back down
		S_StartSound((mobj_t *)&door->sector->soundorg,
			     sfx_bdcls);
		break;
		
	      case normal:
		door->direction = -1; // time to go back down
		S_StartSound((mobj_t *)&door->sector->soundorg,
			     sfx_dorcls);
		break;
		
	      case close30ThenOpen:
		door->direction = 1;
		S_StartSound((mobj_t *)&door->sector->soundorg,
			     sfx_doropn);
		break;
		
	      default:
		break;
	    }
	}
	break;
	
      case 2:
	//  INITIAL WAIT
	if (!--door->topcountdown)
	{
	    switch(door->type)
	    {
	      case raiseIn5Mins:
		door->direction = 1;
		door->type = normal;
		S_StartSound((mobj_t *)&door->sector->soundorg,
			     sfx_doropn);
		break;
		
	      default:
		break;
	    }
	}
	break;
	
      case -1:
	// DOWN
	res = T_MovePlane(door->sector,
			  door->speed,
			  door->sector->floorheight,
			  false,1,door->direction);

	if (res == pastdest)
	{
	    switch(door->type)
	    {
	      case blazeRaise:
	      case blazeClose:
		door->sector->specialdata = NULL;
		P_RemoveThinker (&door->thinker);  // unlink and free
		S_StartSound((mobj_t *)&door->sector->soundorg,
			     sfx_bdcls);
		break;
		
	      case normal:
	      case close:
		door->sector->specialdata = NULL;
		P_RemoveThinker (&door->thinker);  // unlink and free
		break;
		
	      case close30ThenOpen:
		door->direction = 0;
		door->topcountdown = 35*30;
		break;
		
	      default:
		break;
	    }
	}
	else if (res == crushed)
	{
	    switch(door->type)
	    {
	      case blazeClose:
	      case close:		// DO NOT GO BACK UP!
		break;
		
	      default:
		door->direction = 1;
		S_StartSound((mobj_t *)&door->sector->soundorg,
			     sfx_doropn);
		break;
	    }
	}
	break;
	
      case 1:
	// UP
	res = T_MovePlane(door->sector,
			  door->speed,
			  door->topheight,
			  false,1,door->direction);
    if (R_DoorMotion && door->sector->ceilingheight > oldheight)
        R_DoorMotion(door->sector);
	
	if (res == pastdest)
	{
	    switch(door->type)
	    {
	      case blazeRaise:
	      case normal:
		door->direction = 0; // wait at top
		door->topcountdown = door->topwait;
		break;
		
	      case close30ThenOpen:
	      case blazeOpen:
	      case open:
		door->sector->specialdata = NULL;
		P_RemoveThinker (&door->thinker);  // unlink and free
		break;
		
	      default:
		break;
	    }
	}
	break;
    }
}


//
// EV_DoLockedDoor
// Move a locked door up/down
//

int
EV_DoLockedDoor
( line_t*	line,
  vldoor_e	type,
  mobj_t*	thing )
{
    player_t*	p;
	
    p = thing->player;
	
    if (!p)
	return 0;
		
    switch(line->special)
    {
      case 99:	// Blue Lock
      case 133:
	if ( !p )
	    return 0;
	if (!p->cards[it_bluecard] && !p->cards[it_blueskull])
	{
	    p->message = DEH_String (PD_BLUEO);
	    S_StartSound(NULL,sfx_oof);
	    return 0;
	}
	break;
	
      case 134: // Red Lock
      case 135:
	if ( !p )
	    return 0;
	if (!p->cards[it_redcard] && !p->cards[it_redskull])
	{
	    p->message = DEH_String (PD_REDO);
	    S_StartSound(NULL,sfx_oof);
	    return 0;
	}
	break;
	
      case 136:	// Yellow Lock
      case 137:
	if ( !p )
	    return 0;
	if (!p->cards[it_yellowcard] &&
	    !p->cards[it_yellowskull])
	{
	    p->message = DEH_String (PD_YELLOWO);
	    S_StartSound(NULL,sfx_oof);
	    return 0;
	}
	break;	
    }

    return EV_DoDoor(line,type);
}


int
EV_DoDoor
( line_t*	line,
  vldoor_e	type )
{
    int		secnum,rtn;
    sector_t*	sec;
    vldoor_t*	door;
	
    secnum = -1;
    rtn = 0;
    
    while ((secnum = P_FindSectorFromLineTag(line,secnum)) >= 0)
    {
	sec = &sectors[secnum];
	if (sec->specialdata || sec->rocketdoorhits >= 3)
	    continue;
		
	
	// new door thinker
	rtn = 1;
	door = Z_Malloc (sizeof(*door), PU_LEVSPEC, 0);
	P_AddThinker (&door->thinker);
	sec->specialdata = door;

	door->thinker.function.acp1 = (actionf_p1) T_VerticalDoor;
	door->sector = sec;
	door->type = type;
	door->topwait = VDOORWAIT;
	door->speed = VDOORSPEED;
		
	switch(type)
	{
	  case blazeClose:
	    door->topheight = P_FindLowestCeilingSurrounding(sec);
	    door->topheight -= 4*FRACUNIT;
	    door->direction = -1;
	    door->speed = VDOORSPEED * 4;
	    S_StartSound((mobj_t *)&door->sector->soundorg,
			 sfx_bdcls);
	    break;
	    
	  case close:
	    door->topheight = P_FindLowestCeilingSurrounding(sec);
	    door->topheight -= 4*FRACUNIT;
	    door->direction = -1;
	    S_StartSound((mobj_t *)&door->sector->soundorg,
			 sfx_dorcls);
	    break;
	    
	  case close30ThenOpen:
	    door->topheight = sec->ceilingheight;
	    door->direction = -1;
	    S_StartSound((mobj_t *)&door->sector->soundorg,
			 sfx_dorcls);
	    break;
	    
	  case blazeRaise:
	  case blazeOpen:
	    door->direction = 1;
	    door->topheight = P_FindLowestCeilingSurrounding(sec);
	    door->topheight -= 4*FRACUNIT;
	    door->speed = VDOORSPEED * 4;
	    if (door->topheight != sec->ceilingheight)
		S_StartSound((mobj_t *)&door->sector->soundorg,
			     sfx_bdopn);
	    break;
	    
	  case normal:
	  case open:
	    door->direction = 1;
	    door->topheight = P_FindLowestCeilingSurrounding(sec);
	    door->topheight -= 4*FRACUNIT;
	    if (door->topheight != sec->ceilingheight)
		S_StartSound((mobj_t *)&door->sector->soundorg,
			     sfx_doropn);
	    break;
	    
	  default:
	    break;
	}
		
    }
    return rtn;
}


//
// EV_VerticalDoor : open a door manually, no tag value
//
void
EV_VerticalDoor
( line_t*	line,
  mobj_t*	thing )
{
    player_t*	player;
    int		secnum;
    sector_t*	sec;
    vldoor_t*	door;
    int		side;
	
    if(line->backsector && line->backsector->rocketdoorhits >= 3)return;
    side = 0;	// only front sides can be used

    //	Check for locks
    player = thing->player;
		
    switch(line->special)
    {
      case 26: // Blue Lock
      case 32:
	if ( !player )
	    return;
	
	if (!player->cards[it_bluecard] && !player->cards[it_blueskull])
	{
	    player->message = DEH_String (PD_BLUEK);
	    S_StartSound(NULL,sfx_oof);
	    return;
	}
	break;
	
      case 27: // Yellow Lock
      case 34:
	if ( !player )
	    return;
	
	if (!player->cards[it_yellowcard] &&
	    !player->cards[it_yellowskull])
	{
	    player->message = DEH_String (PD_YELLOWK);
	    S_StartSound(NULL,sfx_oof);
	    return;
	}
	break;
	
      case 28: // Red Lock
      case 33:
	if ( !player )
	    return;
	
	if (!player->cards[it_redcard] && !player->cards[it_redskull])
	{
	    player->message = DEH_String (PD_REDK);
	    S_StartSound(NULL,sfx_oof);
	    return;
	}
	break;
    }
	
    // if the sector has an active thinker, use it
    sec = sides[ line->sidenum[side^1]] .sector;
    secnum = sec-sectors;
    if (sec->rocketdoorhits >= 3) return;

    if (sec->specialdata)
    {
	door = sec->specialdata;
	switch(line->special)
	{
	  case	1: // ONLY FOR "RAISE" DOORS, NOT "OPEN"s
	  case	26:
	  case	27:
	  case	28:
	  case	117:
	    if (door->direction == -1)
		door->direction = 1;	// go back up
	    else
	    {
		if (!thing->player)
		    return;		// JDC: bad guys never close doors
		
		door->direction = -1;	// start going down immediately
	    }
	    return;
	}
    }
	
    // for proper sound
    switch(line->special)
    {
      case 117:	// BLAZING DOOR RAISE
      case 118:	// BLAZING DOOR OPEN
	S_StartSound((mobj_t *)&sec->soundorg,sfx_bdopn);
	break;
	
      case 1:	// NORMAL DOOR SOUND
      case 31:
	S_StartSound((mobj_t *)&sec->soundorg,sfx_doropn);
	break;
	
      default:	// LOCKED DOOR SOUND
	S_StartSound((mobj_t *)&sec->soundorg,sfx_doropn);
	break;
    }
	
    
    // new door thinker
    door = Z_Malloc (sizeof(*door), PU_LEVSPEC, 0);
    P_AddThinker (&door->thinker);
    sec->specialdata = door;
    door->thinker.function.acp1 = (actionf_p1) T_VerticalDoor;
    door->sector = sec;
    door->direction = 1;
    door->speed = VDOORSPEED;
    door->topwait = VDOORWAIT;

    switch(line->special)
    {
      case 1:
      case 26:
      case 27:
      case 28:
	door->type = normal;
	break;
	
      case 31:
      case 32:
      case 33:
      case 34:
	door->type = open;
	line->special = 0;
	break;
	
      case 117:	// blazing door raise
	door->type = blazeRaise;
	door->speed = VDOORSPEED*4;
	break;
      case 118:	// blazing door open
	door->type = blazeOpen;
	line->special = 0;
	door->speed = VDOORSPEED*4;
	break;
    }
    
    // find the top and bottom of the movement range
    door->topheight = P_FindLowestCeilingSurrounding(sec);
    door->topheight -= 4*FRACUNIT;
}


//
// Spawn a door that closes after 30 seconds
//
void P_SpawnDoorCloseIn30 (sector_t* sec)
{
    vldoor_t*	door;
	
    door = Z_Malloc ( sizeof(*door), PU_LEVSPEC, 0);

    P_AddThinker (&door->thinker);

    sec->specialdata = door;
    sec->special = 0;

    door->thinker.function.acp1 = (actionf_p1)T_VerticalDoor;
    door->sector = sec;
    door->direction = 0;
    door->type = normal;
    door->speed = VDOORSPEED;
    door->topcountdown = 30 * 35;
}

//
// Spawn a door that opens after 5 minutes
//
void
P_SpawnDoorRaiseIn5Mins
( sector_t*	sec,
  int		secnum )
{
    vldoor_t*	door;
	
    door = Z_Malloc ( sizeof(*door), PU_LEVSPEC, 0);
    
    P_AddThinker (&door->thinker);

    sec->specialdata = door;
    sec->special = 0;

    door->thinker.function.acp1 = (actionf_p1)T_VerticalDoor;
    door->sector = sec;
    door->direction = 2;
    door->type = raiseIn5Mins;
    door->speed = VDOORSPEED;
    door->topheight = P_FindLowestCeilingSurrounding(sec);
    door->topheight -= 4*FRACUNIT;
    door->topwait = VDOORWAIT;
    door->topcountdown = 5 * 60 * 35;
}



// UNUSED
// Separate into p_slidoor.c?

#if 0		// ABANDONED TO THE MISTS OF TIME!!!
//
// EV_SlidingDoor : slide a door horizontally
// (animate midtexture, then set noblocking line)
//


slideframe_t slideFrames[MAXSLIDEDOORS];

void P_InitSlidingDoorFrames(void)
{
    int		i;
    int		f1;
    int		f2;
    int		f3;
    int		f4;
	
    // DOOM II ONLY...
    if ( gamemode != commercial)
	return;
	
    for (i = 0;i < MAXSLIDEDOORS; i++)
    {
	if (!slideFrameNames[i].frontFrame1[0])
	    break;
			
	f1 = R_TextureNumForName(slideFrameNames[i].frontFrame1);
	f2 = R_TextureNumForName(slideFrameNames[i].frontFrame2);
	f3 = R_TextureNumForName(slideFrameNames[i].frontFrame3);
	f4 = R_TextureNumForName(slideFrameNames[i].frontFrame4);

	slideFrames[i].frontFrames[0] = f1;
	slideFrames[i].frontFrames[1] = f2;
	slideFrames[i].frontFrames[2] = f3;
	slideFrames[i].frontFrames[3] = f4;
		
	f1 = R_TextureNumForName(slideFrameNames[i].backFrame1);
	f2 = R_TextureNumForName(slideFrameNames[i].backFrame2);
	f3 = R_TextureNumForName(slideFrameNames[i].backFrame3);
	f4 = R_TextureNumForName(slideFrameNames[i].backFrame4);

	slideFrames[i].backFrames[0] = f1;
	slideFrames[i].backFrames[1] = f2;
	slideFrames[i].backFrames[2] = f3;
	slideFrames[i].backFrames[3] = f4;
    }
}


//
// Return index into "slideFrames" array
// for which door type to use
//
int P_FindSlidingDoorType(line_t*	line)
{
    int		i;
    int		val;
	
    for (i = 0;i < MAXSLIDEDOORS;i++)
    {
	val = sides[line->sidenum[0]].midtexture;
	if (val == slideFrames[i].frontFrames[0])
	    return i;
    }
	
    return -1;
}

void T_SlidingDoor (slidedoor_t*	door)
{
    switch(door->status)
    {
      case sd_opening:
	if (!door->timer--)
	{
	    if (++door->frame == SNUMFRAMES)
	    {
		// IF DOOR IS DONE OPENING...
		sides[door->line->sidenum[0]].midtexture = 0;
		sides[door->line->sidenum[1]].midtexture = 0;
		door->line->flags &= ML_BLOCKING^0xff;
					
		if (door->type == sdt_openOnly)
		{
		    door->frontsector->specialdata = NULL;
		    P_RemoveThinker (&door->thinker);
		    break;
		}
					
		door->timer = SDOORWAIT;
		door->status = sd_waiting;
	    }
	    else
	    {
		// IF DOOR NEEDS TO ANIMATE TO NEXT FRAME...
		door->timer = SWAITTICS;
					
		sides[door->line->sidenum[0]].midtexture =
		    slideFrames[door->whichDoorIndex].
		    frontFrames[door->frame];
		sides[door->line->sidenum[1]].midtexture =
		    slideFrames[door->whichDoorIndex].
		    backFrames[door->frame];
	    }
	}
	break;
			
      case sd_waiting:
	// IF DOOR IS DONE WAITING...
	if (!door->timer--)
	{
	    // CAN DOOR CLOSE?
	    if (door->frontsector->thinglist != NULL ||
		door->backsector->thinglist != NULL)
	    {
		door->timer = SDOORWAIT;
		break;
	    }

	    //door->frame = SNUMFRAMES-1;
	    door->status = sd_closing;
	    door->timer = SWAITTICS;
	}
	break;
			
      case sd_closing:
	if (!door->timer--)
	{
	    if (--door->frame < 0)
	    {
		// IF DOOR IS DONE CLOSING...
		door->line->flags |= ML_BLOCKING;
		door->frontsector->specialdata = NULL;
		P_RemoveThinker (&door->thinker);
		break;
	    }
	    else
	    {
		// IF DOOR NEEDS TO ANIMATE TO NEXT FRAME...
		door->timer = SWAITTICS;
					
		sides[door->line->sidenum[0]].midtexture =
		    slideFrames[door->whichDoorIndex].
		    frontFrames[door->frame];
		sides[door->line->sidenum[1]].midtexture =
		    slideFrames[door->whichDoorIndex].
		    backFrames[door->frame];
	    }
	}
	break;
    }
}



void
EV_SlidingDoor
( line_t*	line,
  mobj_t*	thing )
{
    sector_t*		sec;
    slidedoor_t*	door;
	
    // DOOM II ONLY...
    if (gamemode != commercial)
	return;
    
    // Make sure door isn't already being animated
    sec = line->frontsector;
    door = NULL;
    if (sec->specialdata)
    {
	if (!thing->player)
	    return;
			
	door = sec->specialdata;
	if (door->type == sdt_openAndClose)
	{
	    if (door->status == sd_waiting)
		door->status = sd_closing;
	}
	else
	    return;
    }
    
    // Init sliding door vars
    if (!door)
    {
	door = Z_Malloc (sizeof(*door), PU_LEVSPEC, 0);
	P_AddThinker (&door->thinker);
	sec->specialdata = door;
		
	door->type = sdt_openAndClose;
	door->status = sd_opening;
	door->whichDoorIndex = P_FindSlidingDoorType(line);

	if (door->whichDoorIndex < 0)
	    I_Error("EV_SlidingDoor: Can't use texture for sliding door!");
			
	door->frontsector = sec;
	door->backsector = line->backsector;
	door->thinker.function = T_SlidingDoor;
	door->timer = SWAITTICS;
	door->frame = 0;
	door->line = line;
    }
}
#endif

/* Deliberately conservative prototype: a door must have a manual opener,
   no sector tag, and no other boundary action. Never consume Doom's RNG. */
extern int p_breakablewalls;
int p_keydoorshield=1,p_switchdoorshield=1;
/* 1 manual, 2 remote, 3 blue, 4 red, 5 yellow. Unknown tagged movers stay intact. */
static int P_DoorActionKind(int special,int manual)
{
    if(manual)switch(special){
        case 1:case 31:case 117:case 118:return 1;
        case 26:case 32:return 3;case 28:case 33:return 4;case 27:case 34:return 5;
        default:return 0;
    }
    switch(special){
        case 99:case 133:return 3;case 134:case 135:return 4;case 136:case 137:return 5;
        case 2:case 3:case 4:case 16:case 29:case 42:case 46:case 50:case 61:case 63:
        case 75:case 76:case 86:case 90:case 103:case 105:case 106:case 107:case 108:
        case 109:case 110:case 111:case 112:case 113:case 114:case 115:case 116:return 2;
        default:return 0;
    }
}
static int P_BlowoutDoorKind(sector_t *sec)
{
    int i,kind=0,k;
    for(i=0;i<sec->linecount;++i){line_t *l=sec->lines[i];
        if(l->backsector!=sec)continue;
        k=P_DoorActionKind(l->special,1);if(k>kind)kind=k;
    }
    if(sec->tag)for(i=0;i<numlines;++i){line_t *l=&lines[i];
        if(l->tag!=sec->tag||!l->special)continue;
        k=P_DoorActionKind(l->special,0);if(!k)return 0;if(k>kind)kind=k;
    }
    return kind;
}
/* Guard the owning solid, including blast fallback and impacts inside old craters. */
int P_DoorShieldImpact(int sector, fixed_t x, fixed_t y, fixed_t z)
{
    sector_t *sec; int kind,i; line_t *best=0; int side=0;
    double nearest=1e30;
    if(!p_breakablewalls||sector<0||sector>=numsectors)return 0;
    sec=&sectors[sector];
    if(sec->rocketdoorhits>=3)return 0;
    kind=P_BlowoutDoorKind(sec);
    if(!((kind>=3&&p_keydoorshield)||(kind==2&&p_switchdoorshield)))return 0;
    if(z<sec->ceilingheight)return 0;
    for(i=0;i<sec->linecount;++i){
        line_t *l=sec->lines[i]; sector_t *other;
        double ax,ay,dx,dy,t,d;
        if(!l->frontsector||!l->backsector)continue;
        other=l->frontsector==sec?l->backsector:l->frontsector;
        if(other==sec||other->ceilingheight<=sec->ceilingheight)continue;
        ax=l->v1->x/(double)FRACUNIT;ay=l->v1->y/(double)FRACUNIT;
        dx=l->dx/(double)FRACUNIT;dy=l->dy/(double)FRACUNIT;
        t=((x/(double)FRACUNIT-ax)*dx+(y/(double)FRACUNIT-ay)*dy)/(dx*dx+dy*dy+1e-9);
        if(t<0)t=0;if(t>1)t=1;
        d=(x/(double)FRACUNIT-ax-dx*t)*(x/(double)FRACUNIT-ax-dx*t)+(y/(double)FRACUNIT-ay-dy*t)*(y/(double)FRACUNIT-ay-dy*t);
        if(d<nearest){nearest=d;best=l;side=l->frontsector==sec;}
    }
    if(best&&R_DoorDamage)R_DoorDamage(best,side,x,y,z,-kind);
    return 1;
}
/* Reject cuts whose bounding sphere overlaps the protected frame band.
   Search geometry, not BSP adjacency: excavated walls can be outside their
   original air sector, and a thin vestibule must not interrupt protection. */
int P_DoorShieldVolume(fixed_t x,fixed_t y,fixed_t z,float reach)
{
    int i;double px=x/(double)FRACUNIT,py=y/(double)FRACUNIT,pz=z/(double)FRACUNIT;
    double band=128+(reach>0?reach:0);
    if(!p_breakablewalls||(!p_keydoorshield&&!p_switchdoorshield))return 0;
    for(i=0;i<numlines;++i){
        line_t *l=&lines[i];sector_t *door,*room;
        double ax,ay,dx,dy,t,ex,ey;
        if(!l->frontsector||!l->backsector||l->frontsector==l->backsector)continue;
        door=l->backsector;room=l->frontsector;
        if(door->ceilingheight>room->ceilingheight){sector_t *swap=door;door=room;room=swap;}
        if(door->ceilingheight>=room->ceilingheight)continue;
        if(pz<door->ceilingheight/(double)FRACUNIT-band||pz>room->ceilingheight/(double)FRACUNIT+band)continue;
        ax=l->v1->x/(double)FRACUNIT;ay=l->v1->y/(double)FRACUNIT;
        dx=l->dx/(double)FRACUNIT;dy=l->dy/(double)FRACUNIT;
        /* Cheap bounds before classification, which may scan remote triggers. */
        if(px<(dx<0?ax+dx:ax)-band||px>(dx>0?ax+dx:ax)+band||
           py<(dy<0?ay+dy:ay)-band||py>(dy>0?ay+dy:ay)+band)continue;
        t=((px-ax)*dx+(py-ay)*dy)/(dx*dx+dy*dy+1e-9);
        if(t<0)t=0;if(t>1)t=1;
        ex=px-ax-dx*t;ey=py-ay-dy*t;
        if(ex*ex+ey*ey<=band*band&&P_DoorShieldImpact(door-sectors,x,y,z<door->ceilingheight?door->ceilingheight:z))return 1;
    }
    return 0;
}
int P_DoorShieldNearby(fixed_t x,fixed_t y,fixed_t z)
{
    return P_DoorShieldVolume(x,y,z,0);
}
int p_destructibledoors = 0;
int P_RocketDoorImpact(mobj_t *rocket, line_t *line)
{
    sector_t *sec, *front;
    vldoor_t *door;
    fixed_t top;
    int side, kind;
    if (!p_breakablewalls || !p_destructibledoors || demoplayback || demorecording || netgame ||
        !rocket || rocket->type != MT_ROCKET || !rocket->target ||
        !rocket->target->player || !line || !line->frontsector || !line->backsector)
        return 0;
    side = P_PointOnLineSide(rocket->x, rocket->y, line);
    sec = side ? line->frontsector : line->backsector;
    front = side ? line->backsector : line->frontsector;
    if (sec->special || sec->rocketdoorhits >= 3 ||
        sec->ceilingheight >= front->ceilingheight ||
        rocket->z + rocket->height <= sec->ceilingheight) return 0;
    kind = P_BlowoutDoorKind(sec);
    if (!kind) return 0;
    if ((kind>=3 && p_keydoorshield) || (kind==2 && p_switchdoorshield)) {
        if(R_DoorDamage)R_DoorDamage(line,side,rocket->x,rocket->y,rocket->z+rocket->height/2,-kind);
        return 1; /* Block excavation before it damages the shielded face. */
    }
    door = (vldoor_t *)sec->specialdata;
    if (door && door->thinker.function.acp1 != (actionf_p1)T_VerticalDoor) return 0;
    top = P_FindLowestCeilingSurrounding(sec) - 4*FRACUNIT;
    if (top <= sec->floorheight) return 0;
    ++sec->rocketdoorhits;
    if (R_DoorDamage) R_DoorDamage(line,side,rocket->x,rocket->y,
                                  rocket->z+rocket->height/2,sec->rocketdoorhits == 3);
    if (sec->rocketdoorhits < 3) return 0;
    if (!door)
    {
        door = Z_Malloc(sizeof(*door), PU_LEVSPEC, 0);
        sec->specialdata = door;
        door->sector = sec;
        P_AddThinker(&door->thinker);
        door->thinker.function.acp1 = (actionf_p1)T_VerticalDoor;
    }
    door->type = open;
    door->direction = 1;
    door->speed = top-sec->ceilingheight+FRACUNIT; /* Clear in one tic, not a sliding opening. */
    door->topheight = top;
    door->topwait = VDOORWAIT;
    door->topcountdown = 0;
    return 1;
}

/* Append-only save extension; old saves leave the zero-initialized health alone. */
int P_SaveDoorDamage(unsigned char *data)
{
    int i;
    data[0]='D';data[1]='O';data[2]='R';data[3]=1;
    for(i=0;i<4;++i)data[4+i]=(unsigned int)numsectors>>(8*i);
    for(i=0;i<numsectors;++i)data[8+i]=sectors[i].rocketdoorhits;
    return 8+numsectors;
}
void P_LoadDoorDamage(const unsigned char *data, int length)
{
    unsigned int count=0;int i;
    if(length<8 || data[0]!='D' || data[1]!='O' || data[2]!='R' || data[3]!=1)return;
    for(i=0;i<4;++i)count|=(unsigned int)data[4+i]<<(8*i);
    if(count!=(unsigned int)numsectors || length-8<numsectors)return;
    for(i=0;i<numsectors;++i)if(data[8+i]>3)return;
    for(i=0;i<numsectors;++i)sectors[i].rocketdoorhits=data[8+i];
}
