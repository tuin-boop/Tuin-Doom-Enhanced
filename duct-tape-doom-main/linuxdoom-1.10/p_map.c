#include <stdint.h>
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
//	Movement, collision handling.
//	Shooting and aiming.
//
//-----------------------------------------------------------------------------

static const char
rcsid[] = "$Id: p_map.c,v 1.5 1997/02/03 22:45:11 b1 Exp $";

#include <stdlib.h>

#include "m_bbox.h"
#include "m_random.h"
#include "i_system.h"

#include "doomdef.h"
#include "p_local.h"
#include "p_solid.h"
#include "p_digwall.h"

#include "s_sound.h"

// State.
#include "doomstat.h"
#include "r_state.h"
// Data.
#include "sounds.h"
#include "d_deh.h"


fixed_t		tmbbox[4];
mobj_t*		tmthing;
int		tmflags;
fixed_t		tmx;
fixed_t		tmy;


// If "floatok" true, move would be ok
// if within "tmfloorz - tmceilingz".
boolean		floatok;

fixed_t		tmfloorz;
fixed_t		tmceilingz;
fixed_t		tmdropoffz;

// keep track of the line that lowers the ceiling,
// so missiles don't explode against sky hack walls
line_t*		ceilingline;
int p_missile_hit_actor=0;

// keep track of special lines as they are hit,
// but don't process them until the move is proven valid
// Grows on demand; vanilla's 8 entries overflowed into other globals.
line_t**	spechit;
int		numspechit;
static int	maxspechit;



//
// TELEPORT MOVE
// 

//
// PIT_StompThing
//
boolean PIT_StompThing (mobj_t* thing)
{
    fixed_t	blockdist;
		
    if (!(thing->flags & MF_SHOOTABLE) )
	return true;
		
    blockdist = thing->radius + tmthing->radius;
    
    if ( abs(thing->x - tmx) >= blockdist
	 || abs(thing->y - tmy) >= blockdist )
    {
	// didn't hit it
	return true;
    }
    
    // don't clip against self
    if (thing == tmthing)
	return true;
    
    // monsters don't stomp things except on boss level
    if ( !tmthing->player && gamemap != 30)
	return false;	
		
    P_DamageMobj (thing, tmthing, tmthing, 10000);
	
    return true;
}


//
// P_TeleportMove
//
boolean
P_TeleportMove
( mobj_t*	thing,
  fixed_t	x,
  fixed_t	y )
{
    int			xl;
    int			xh;
    int			yl;
    int			yh;
    int			bx;
    int			by;
    
    subsector_t*	newsubsec;
    
    // kill anything occupying the position
    tmthing = thing;
    tmflags = thing->flags;
	
    tmx = x;
    tmy = y;
	
    tmbbox[BOXTOP] = y + tmthing->radius;
    tmbbox[BOXBOTTOM] = y - tmthing->radius;
    tmbbox[BOXRIGHT] = x + tmthing->radius;
    tmbbox[BOXLEFT] = x - tmthing->radius;

    newsubsec = R_PointInSubsector (x,y);
    ceilingline = NULL;
    p_missile_hit_actor=0;
    
    // The base floor/ceiling is from the subsector
    // that contains the point.
    // Any contacted lines the step closer together
    // will adjust them.
    tmfloorz = tmdropoffz = newsubsec->sector->floorheight;
    tmceilingz = newsubsec->sector->ceilingheight;
			
    validcount++;
    numspechit = 0;
    
    // stomp on any things contacted
    xl = (tmbbox[BOXLEFT] - bmaporgx - MAXRADIUS)>>MAPBLOCKSHIFT;
    xh = (tmbbox[BOXRIGHT] - bmaporgx + MAXRADIUS)>>MAPBLOCKSHIFT;
    yl = (tmbbox[BOXBOTTOM] - bmaporgy - MAXRADIUS)>>MAPBLOCKSHIFT;
    yh = (tmbbox[BOXTOP] - bmaporgy + MAXRADIUS)>>MAPBLOCKSHIFT;

    for (bx=xl ; bx<=xh ; bx++)
	for (by=yl ; by<=yh ; by++)
	    if (!P_BlockThingsIterator(bx,by,PIT_StompThing))
		return false;
    
    // the move is ok,
    // so link the thing into its new position
    P_UnsetThingPosition (thing);

    thing->floorz = tmfloorz;
    thing->ceilingz = tmceilingz;	
    thing->x = x;
    thing->y = y;

    P_SetThingPosition (thing);
	
    return true;
}


//
// MOVEMENT ITERATOR FUNCTIONS
//


//
// PIT_CheckLine
// Adjusts tmfloorz and tmceilingz as lines are contacted
//
static boolean P_EscapeSpawnWall(line_t *ld)
{
    if(demoplayback||demorecording||netgame||!(tmthing->flags&MF_COUNTKILL)
       ||(tmthing->flags&(MF_MISSILE|MF_SKULLFLY))||tmthing->health<=0)return false;
    fixed_t box[4];
    box[BOXLEFT]=tmthing->x-tmthing->radius;box[BOXRIGHT]=tmthing->x+tmthing->radius;
    box[BOXBOTTOM]=tmthing->y-tmthing->radius;box[BOXTOP]=tmthing->y+tmthing->radius;
    if(box[BOXRIGHT]<=ld->bbox[BOXLEFT]||box[BOXLEFT]>=ld->bbox[BOXRIGHT]
       ||box[BOXTOP]<=ld->bbox[BOXBOTTOM]||box[BOXBOTTOM]>=ld->bbox[BOXTOP]
       ||P_BoxOnLineSide(box,ld)!=-1)return false;
    double old=((double)tmthing->x-ld->v1->x)*ld->dy-((double)tmthing->y-ld->v1->y)*ld->dx;
    double next=((double)tmx-ld->v1->x)*ld->dy-((double)tmy-ld->v1->y)*ld->dx;
    // Never cross the line, only retreat from an already intersecting wall.
    return (old>0&&next>old)||(old<0&&next<old);
}

static double P_LedgeDistanceSquared(line_t *line, fixed_t x, fixed_t y)
{
    double dx=line->dx,dy=line->dy;
    double px=(double)x-line->v1->x,py=(double)y-line->v1->y;
    double length=dx*dx+dy*dy;
    double t=length>0?(px*dx+py*dy)/length:0;
    if(t<0)t=0;else if(t>1)t=1;
    px-=t*dx;py-=t*dy;
    return px*px+py*py;
}

// A large monster can overlap several elevated tiers after falling. The
// adjacent sectors of a distant edge need not include its actual lower room.
// Permit only increasing separation from an already overlapped finite edge.
static boolean P_EscapeLedge(line_t *line)
{
    fixed_t box[4];
    sector_t *room=tmthing->subsector->sector;
    sector_t *dest=R_PointInSubsector(tmx,tmy)->sector;
    if(demoplayback||demorecording||netgame||!(tmthing->flags&MF_COUNTKILL)
       ||(tmthing->flags&(MF_MISSILE|MF_SKULLFLY))||tmthing->health<=0)return false;
    if(room->floorheight>tmthing->z||dest->floorheight>tmthing->z
       ||room->ceilingheight-tmthing->z<tmthing->height
       ||dest->ceilingheight-tmthing->z<tmthing->height)return false;
    box[BOXLEFT]=tmthing->x-tmthing->radius;box[BOXRIGHT]=tmthing->x+tmthing->radius;
    box[BOXBOTTOM]=tmthing->y-tmthing->radius;box[BOXTOP]=tmthing->y+tmthing->radius;
    if(box[BOXRIGHT]<=line->bbox[BOXLEFT]||box[BOXLEFT]>=line->bbox[BOXRIGHT]
       ||box[BOXTOP]<=line->bbox[BOXBOTTOM]||box[BOXBOTTOM]>=line->bbox[BOXTOP]
       ||P_BoxOnLineSide(box,line)!=-1)return false;
    if(P_PointOnLineSide(tmthing->x,tmthing->y,line)!=P_PointOnLineSide(tmx,tmy,line))return false;
    return P_LedgeDistanceSquared(line,tmx,tmy)>
           P_LedgeDistanceSquared(line,tmthing->x,tmthing->y);
}
boolean PIT_CheckLine (line_t* ld)
{
    if (tmbbox[BOXRIGHT] <= ld->bbox[BOXLEFT]
	|| tmbbox[BOXLEFT] >= ld->bbox[BOXRIGHT]
	|| tmbbox[BOXTOP] <= ld->bbox[BOXBOTTOM]
	|| tmbbox[BOXBOTTOM] >= ld->bbox[BOXTOP] )
	return true;

    if (P_BoxOnLineSide (tmbbox, ld) != -1)
	return true;
    if(SV_MapPosition(tmthing,tmx,tmy)){
        // Preserve deliberate invisible barriers and monster-only fences.
        // Physical solid walls are tested against the edited volume.
        if(!(tmthing->flags&MF_MISSILE)&&ld->backsector){
            if((ld->flags&ML_BLOCKMONSTERS)&&!tmthing->player)return false;
            if((ld->flags&ML_BLOCKING)&&
               ld->frontsector->floorheight==ld->backsector->floorheight&&
               ld->frontsector->ceilingheight==ld->backsector->ceilingheight)return false;
        }
        goto solid_special;
    }
		
    if(P_GlobalDigEnabled()){
        for(int side=0;side<2;++side)if(ld->sidenum[side]>=0&&(P_GlobalDigFit((int)(ld-lines),side,tmthing,tmx,tmy)||P_GlobalDigRetreat((int)(ld-lines),side,tmthing,tmx,tmy)))return true;
    }
    // A line has been hit
    
    // The moving thing's destination position will cross
    // the given line.
    // If this should not be allowed, return false.
    // If the line is special, keep track of it
    // to process later if the move is proven ok.
    // NOTE: specials are NOT sorted by order,
    // so two special lines that are only 8 pixels apart
    // could be crossed in either order.
    
    if (!ld->backsector)
	return P_EscapeSpawnWall(ld);	// one sided line
		
    if (!(tmthing->flags & MF_MISSILE) )
    {
	if ( ld->flags & ML_BLOCKING )
	    return P_EscapeSpawnWall(ld);	// explicitly blocking everything

	if ( !tmthing->player && ld->flags & ML_BLOCKMONSTERS )
	    return P_EscapeSpawnWall(ld);	// block monsters only
    }

    // set openrange, opentop, openbottom
    P_LineOpening (ld);	
	
    // A monster can fall beside a ledge with its radius still overlapping
    // the upper sector. Let it retreat on the low side instead of requiring
    // a step up onto that ledge for every attempted direction.
    if(openbottom-tmthing->z>24*FRACUNIT&&P_EscapeLedge(ld))goto solid_special;

    // adjust floor / ceiling heights
    if (opentop < tmceilingz)
    {
	tmceilingz = opentop;
	ceilingline = ld;
    }

    if (openbottom > tmfloorz)
	tmfloorz = openbottom;	

    if (lowfloor < tmdropoffz)
	tmdropoffz = lowfloor;
		
solid_special:
    // if contacted a special line, add it to the list
    if (ld->special)
    {
	if (numspechit == maxspechit)
	{
	    maxspechit = maxspechit ? maxspechit * 2 : 8;
	    spechit = realloc (spechit, maxspechit * sizeof(*spechit));
	    if (!spechit)
		I_Error ("PIT_CheckLine: out of memory");
	}
	spechit[numspechit] = ld;
	numspechit++;
    }

    return true;
}

//
// PIT_CheckThing
//
boolean PIT_CheckThing (mobj_t* thing)
{
    fixed_t		blockdist;
    boolean		solid;
    int			damage;
		
    if (!(thing->flags & (MF_SOLID|MF_SPECIAL|MF_SHOOTABLE) ))
	return true;
    
    blockdist = thing->radius + tmthing->radius;

    if ( abs(thing->x - tmx) >= blockdist
	 || abs(thing->y - tmy) >= blockdist )
    {
	// didn't hit it
	return true;	
    }
    
    // don't clip against self
    if (thing == tmthing)
	return true;
    
    // check for skulls slamming into things
    if (tmthing->flags & MF_SKULLFLY)
    {
	if(thing!=tmthing->target)P_TacticsChargeBlocked(tmthing);
	damage = ((P_Random()%8)+1)*tmthing->info->damage;
	
	P_DamageMobj (thing, tmthing, tmthing, damage);
	
	tmthing->flags &= ~MF_SKULLFLY;
	tmthing->momx = tmthing->momy = tmthing->momz = 0;
	
	P_SetMobjState (tmthing, tmthing->info->spawnstate);
	
	return false;		// stop moving
    }

    
    // missiles can hit other things
    if (tmthing->flags & MF_MISSILE)
    {
	// see if it went over / under
	if (tmthing->z > thing->z + thing->height)
	    return true;		// overhead
	if (tmthing->z+tmthing->height < thing->z)
	    return true;		// underneath
		
	if (tmthing->target && (
	    tmthing->target->type == thing->type || 
	    (tmthing->target->type == MT_KNIGHT && thing->type == MT_BRUISER)||
	    (tmthing->target->type == MT_BRUISER && thing->type == MT_KNIGHT) ) )
	{
	    // Don't hit same species as originator.
	    if (thing == tmthing->target)
		return true;

	    // A DEHACKED patch can let a species fight itself.
	    if (thing->type != MT_PLAYER && !deh_species_infighting)
	    {
		// Explode, but do no damage.
                p_missile_hit_actor=1;
		// Let players missile other players.
		return false;
	    }
	}
	
	if (! (thing->flags & MF_SHOOTABLE) )
	{
	    // didn't do any damage
            if(thing->flags&MF_SOLID)p_missile_hit_actor=1;
	    return !(thing->flags & MF_SOLID);	
	}
	
	// Record the actual actor impact for nearby surface blast damage.
        p_missile_hit_actor=1;
        P_GlobalDigMonsterContact(tmthing,tmx,tmy);
        SV_MonsterContact(tmthing,thing);
	// damage / explode
	damage = ((P_Random()%8)+1)*tmthing->info->damage;
	P_DamageMobj (thing, tmthing, tmthing->target, damage);

	// don't traverse any more
	return false;				
    }
    
    // check for special pickup
    if (thing->flags & MF_SPECIAL)
    {
	solid = thing->flags&MF_SOLID;
	if (tmflags&MF_PICKUP)
	{
	    // can remove thing
	    P_TouchSpecialThing (thing, tmthing);
	}
	return !solid;
    }
	
    /* Tightly packed map spawns may already overlap. Permit only steps
       that reduce that overlap; Fluid's small steps otherwise never escape.
       New collisions, player movement, missiles and demo rules stay intact. */
    if (!demoplayback && !demorecording && !netgame
        && (tmthing->flags & MF_COUNTKILL) && (thing->flags & MF_COUNTKILL)
        && tmthing->health > 0 && thing->health > 0)
    {
        int64_t ox = llabs((int64_t)thing->x-tmthing->x);
        int64_t oy = llabs((int64_t)thing->y-tmthing->y);
        int64_t nx = llabs((int64_t)thing->x-tmx);
        int64_t ny = llabs((int64_t)thing->y-tmy);
        if (ox < blockdist && oy < blockdist
            && (nx > ny ? nx : ny) > (ox > oy ? ox : oy))
            return true;
    }
    return !(thing->flags & MF_SOLID);
}


//
// MOVEMENT CLIPPING
//

//
// P_CheckPosition
// This is purely informative, nothing is modified
// (except things picked up).
// 
// in:
//  a mobj_t (can be valid or invalid)
//  a position to be checked
//   (doesn't need to be related to the mobj_t->x,y)
//
// during:
//  special things are touched if MF_PICKUP
//  early out on solid lines?
//
// out:
//  newsubsec
//  floorz
//  ceilingz
//  tmdropoffz
//   the lowest point contacted
//   (monsters won't move to a dropoff)
//  speciallines[]
//  numspeciallines
//
boolean
P_CheckPosition
( mobj_t*	thing,
  fixed_t	x,
  fixed_t	y )
{
    int			xl;
    int			xh;
    int			yl;
    int			yh;
    int			bx;
    int			by;
    subsector_t*	newsubsec;

    tmthing = thing;
    tmflags = thing->flags;
	
    tmx = x;
    tmy = y;
	
    tmbbox[BOXTOP] = y + tmthing->radius;
    tmbbox[BOXBOTTOM] = y - tmthing->radius;
    tmbbox[BOXRIGHT] = x + tmthing->radius;
    tmbbox[BOXLEFT] = x - tmthing->radius;

    newsubsec = R_PointInSubsector (x,y);
    ceilingline = NULL;
    p_missile_hit_actor=0;
    
    // The base floor / ceiling is from the subsector
    // that contains the point.
    // Any contacted lines the step closer together
    // will adjust them.
    tmfloorz = tmdropoffz = newsubsec->sector->floorheight;
    tmceilingz = newsubsec->sector->ceilingheight;
			
    validcount++;
    numspechit = 0;

    if ( tmflags & MF_NOCLIP )
	return true;
    
    if(SV_Active()){
        if(!SV_Position(thing,x,y,&tmfloorz,&tmceilingz))return false;
        tmdropoffz=tmfloorz;
    }else{
    if(P_GlobalDigBreachSupportRoom((int)(newsubsec->sector-sectors))>=0){
        float floor=tmfloorz/(float)FRACUNIT,ceiling=tmceilingz/(float)FRACUNIT;
        P_GlobalDigBreachRoomBounds((int)(newsubsec->sector-sectors),&floor,&ceiling);
        tmfloorz=tmdropoffz=(fixed_t)(floor*FRACUNIT);tmceilingz=(fixed_t)(ceiling*FRACUNIT);
    }
    {int owner=-1;
        if(!P_GlobalDigPosition(thing,x,y,&owner))return false;
        if(owner>=0){
            float floor=sectors[owner].floorheight/(float)FRACUNIT,ceiling=sectors[owner].ceilingheight/(float)FRACUNIT;
            P_GlobalDigBreachRoomBounds(owner,&floor,&ceiling);
            tmfloorz=tmdropoffz=(fixed_t)(floor*FRACUNIT);tmceilingz=(fixed_t)(ceiling*FRACUNIT);
        }
    }
    P_GlobalDigFlatHeights(thing,x,y,(int)(newsubsec->sector-sectors),&tmfloorz,&tmceilingz);
    tmfloorz=P_GlobalDigBreachStepFloor(thing,x,y,tmfloorz);
    tmdropoffz=tmfloorz;
    }
    // Check things first, possibly picking things up.
    // The bounding box is extended by MAXRADIUS
    // because mobj_ts are grouped into mapblocks
    // based on their origin point, and can overlap
    // into adjacent blocks by up to MAXRADIUS units.
    xl = (tmbbox[BOXLEFT] - bmaporgx - MAXRADIUS)>>MAPBLOCKSHIFT;
    xh = (tmbbox[BOXRIGHT] - bmaporgx + MAXRADIUS)>>MAPBLOCKSHIFT;
    yl = (tmbbox[BOXBOTTOM] - bmaporgy - MAXRADIUS)>>MAPBLOCKSHIFT;
    yh = (tmbbox[BOXTOP] - bmaporgy + MAXRADIUS)>>MAPBLOCKSHIFT;

    for (bx=xl ; bx<=xh ; bx++)
	for (by=yl ; by<=yh ; by++)
	    if (!P_BlockThingsIterator(bx,by,PIT_CheckThing))
		return false;
    
    // check lines
    xl = (tmbbox[BOXLEFT] - bmaporgx)>>MAPBLOCKSHIFT;
    xh = (tmbbox[BOXRIGHT] - bmaporgx)>>MAPBLOCKSHIFT;
    yl = (tmbbox[BOXBOTTOM] - bmaporgy)>>MAPBLOCKSHIFT;
    yh = (tmbbox[BOXTOP] - bmaporgy)>>MAPBLOCKSHIFT;

    for (bx=xl ; bx<=xh ; bx++)
	for (by=yl ; by<=yh ; by++)
	    if (!P_BlockLinesIterator (bx,by,PIT_CheckLine))
		return false;

    return true;
}


// A descending lift can leave an existing monster overlapping a drop-off.
// Allow steps farther onto the high side, never steps toward or off the ledge.
// This checks geometry only and leaves P_CheckPosition's collision state intact.
boolean P_RetreatFromDropoff(mobj_t *thing, fixed_t x, fixed_t y)
{
    fixed_t oldbox[4];
    sector_t *sector=thing->subsector->sector;
    boolean retreat=false;
    int i;
    if(demoplayback||demorecording||netgame||!(thing->flags&MF_COUNTKILL)
       ||thing->health<=0||tmfloorz!=thing->floorz||thing->z<tmfloorz)return false;
    oldbox[BOXLEFT]=thing->x-thing->radius;oldbox[BOXRIGHT]=thing->x+thing->radius;
    oldbox[BOXBOTTOM]=thing->y-thing->radius;oldbox[BOXTOP]=thing->y+thing->radius;
    for(i=0;i<sector->linecount;++i){
        line_t *line=sector->lines[i];
        fixed_t front,back;int highside;
        double olddistance,newdistance;
        if(!line->frontsector||!line->backsector)continue;
        front=line->frontsector->floorheight;back=line->backsector->floorheight;
        if(abs(front-back)<=24*FRACUNIT||P_BoxOnLineSide(tmbbox,line)!=-1)continue;
        highside=front>back?0:1;
        if(P_BoxOnLineSide(oldbox,line)!=-1||P_PointOnLineSide(thing->x,thing->y,line)!=highside
           ||P_PointOnLineSide(x,y,line)!=highside)return false;
        olddistance=((double)thing->x-line->v1->x)*line->dy-((double)thing->y-line->v1->y)*line->dx;
        newdistance=((double)x-line->v1->x)*line->dy-((double)y-line->v1->y)*line->dx;
        if(newdistance*newdistance<olddistance*olddistance)return false;
        if(newdistance*newdistance>olddistance*olddistance)retreat=true;
    }
    return retreat;
}

//
// P_TryMove
// Attempt to move to a new position,
// crossing special lines unless MF_TELEPORT is set.
//
boolean
P_TryMove
( mobj_t*	thing,
  fixed_t	x,
  fixed_t	y )
{
    fixed_t	oldx;
    fixed_t	oldy;
    int		side;
    int		oldside;
    line_t*	ld;

    floatok = false;
    if (!P_CheckPosition (thing, x, y))
	return false;		// solid wall or thing
    
    if ( !(thing->flags & MF_NOCLIP) )
    {
	if (tmceilingz - tmfloorz < thing->height)
	    return false;	// doesn't fit

	floatok = true;
	
	if ( !(thing->flags&MF_TELEPORT) 
	     &&tmceilingz - thing->z < thing->height)
	    return false;	// mobj must lower itself to fit

	if ( !(thing->flags&MF_TELEPORT)
	     && tmfloorz - thing->z > 24*FRACUNIT )
	    return false;	// too big a step up

	if ( !(thing->flags&(MF_DROPOFF|MF_FLOAT))
	     && tmfloorz - tmdropoffz > 24*FRACUNIT
             && !P_RetreatFromDropoff(thing,x,y) )
	    return false;	// don't stand over a dropoff
    }
    
    // the move is ok,
    // so link the thing into its new position
    P_UnsetThingPosition (thing);

    oldx = thing->x;
    oldy = thing->y;
    thing->floorz = tmfloorz;
    thing->ceilingz = tmceilingz;	
    thing->x = x;
    thing->y = y;

    P_SetThingPosition (thing);
    
    // if any special lines were hit, do the effect
    if (! (thing->flags&(MF_TELEPORT|MF_NOCLIP)) )
    {
	while (numspechit--)
	{
	    // see if the line was crossed
	    ld = spechit[numspechit];
	    side = P_PointOnLineSide (thing->x, thing->y, ld);
	    oldside = P_PointOnLineSide (oldx, oldy, ld);
	    if (side != oldside)
	    {
		if (ld->special)
		    P_CrossSpecialLine (ld-lines, oldside, thing);
	    }
	}
    }

    return true;
}


//
// P_ThingHeightClip
// Takes a valid thing and adjusts the thing->floorz,
// thing->ceilingz, and possibly thing->z.
// This is called for all nearby monsters
// whenever a sector changes height.
// If the thing doesn't fit,
// the z will be set to the lowest value
// and false will be returned.
//
boolean P_ThingHeightClip (mobj_t* thing)
{
    boolean		onfloor;
	
    onfloor = (thing->z == thing->floorz);
    if(SV_MapPosition(thing,thing->x,thing->y)){
        fixed_t floor,ceiling;
        if(SV_Position(thing,thing->x,thing->y,&floor,&ceiling)==0)return false;
    }
	
    P_CheckPosition (thing, thing->x, thing->y);	
    // what about stranding a monster partially off an edge?
	
    thing->floorz = tmfloorz;
    thing->ceilingz = tmceilingz;
	
    if (onfloor)
    {
	// walking monsters rise and fall with the floor
	thing->z = thing->floorz;
    }
    else
    {
	// don't adjust a floating monster unless forced to
	if (thing->z+thing->height > thing->ceilingz)
	    thing->z = thing->ceilingz - thing->height;
    }
	
    if (thing->ceilingz - thing->floorz < thing->height)
	return false;
		
    return true;
}



//
// SLIDE MOVE
// Allows the player to slide along any angled walls.
//
fixed_t		bestslidefrac;
fixed_t		secondslidefrac;

line_t*		bestslideline;
line_t*		secondslideline;

mobj_t*		slidemo;

fixed_t		tmxmove;
fixed_t		tmymove;



//
// P_HitSlideLine
// Adjusts the xmove / ymove
// so that the next move will slide along the wall.
//
void P_HitSlideLine (line_t* ld)
{
    int			side;

    angle_t		lineangle;
    angle_t		moveangle;
    angle_t		deltaangle;
    
    fixed_t		movelen;
    fixed_t		newlen;
	
	
    if (ld->slopetype == ST_HORIZONTAL)
    {
	tmymove = 0;
	return;
    }
    
    if (ld->slopetype == ST_VERTICAL)
    {
	tmxmove = 0;
	return;
    }
	
    side = P_PointOnLineSide (slidemo->x, slidemo->y, ld);
	
    lineangle = R_PointToAngle2 (0,0, ld->dx, ld->dy);

    if (side == 1)
	lineangle += ANG180;

    moveangle = R_PointToAngle2 (0,0, tmxmove, tmymove);
    deltaangle = moveangle-lineangle;

    if (deltaangle > ANG180)
	deltaangle += ANG180;
    //	I_Error ("SlideLine: ang>ANG180");

    lineangle >>= ANGLETOFINESHIFT;
    deltaangle >>= ANGLETOFINESHIFT;
	
    movelen = P_AproxDistance (tmxmove, tmymove);
    newlen = FixedMul (movelen, finecosine[deltaangle]);

    tmxmove = FixedMul (newlen, finecosine[lineangle]);	
    tmymove = FixedMul (newlen, finesine[lineangle]);	
}


//
// PTR_SlideTraverse
//
boolean PTR_SlideTraverse (intercept_t* in)
{
    line_t*	li;
	
    if (!in->isaline)
	I_Error ("PTR_SlideTraverse: not a line?");
		
    li = in->d.line;
    
    if ( ! (li->flags & ML_TWOSIDED) )
    {
	if (P_PointOnLineSide (slidemo->x, slidemo->y, li))
	{
	    // don't hit the back side
	    return true;		
	}
	goto isblocking;
    }

    // set openrange, opentop, openbottom
    P_LineOpening (li);
    
    if (openrange < slidemo->height)
	goto isblocking;		// doesn't fit
		
    if (opentop - slidemo->z < slidemo->height)
	goto isblocking;		// mobj is too high

    if (openbottom - slidemo->z > 24*FRACUNIT )
	goto isblocking;		// too big a step up

    // this line doesn't block movement
    return true;		
	
    // the line does block movement,
    // see if it is closer than best so far
  isblocking:		
    if (in->frac < bestslidefrac)
    {
	secondslidefrac = bestslidefrac;
	secondslideline = bestslideline;
	bestslidefrac = in->frac;
	bestslideline = li;
    }
	
    return false;	// stop
}



//
// P_SlideMove
// The momx / momy move is bad, so try to slide
// along a wall.
// Find the first line hit, move flush to it,
// and slide along it
//
// This is a kludgy mess.
//
void P_SlideMove (mobj_t* mo)
{
    if(SV_Slide(mo))return;
    fixed_t		leadx;
    fixed_t		leady;
    fixed_t		trailx;
    fixed_t		traily;
    fixed_t		newx;
    fixed_t		newy;
    int			hitcount;
		
    slidemo = mo;
    hitcount = 0;
    
  retry:
    if (++hitcount == 3)
	goto stairstep;		// don't loop forever

    
    // trace along the three leading corners
    if (mo->momx > 0)
    {
	leadx = mo->x + mo->radius;
	trailx = mo->x - mo->radius;
    }
    else
    {
	leadx = mo->x - mo->radius;
	trailx = mo->x + mo->radius;
    }
	
    if (mo->momy > 0)
    {
	leady = mo->y + mo->radius;
	traily = mo->y - mo->radius;
    }
    else
    {
	leady = mo->y - mo->radius;
	traily = mo->y + mo->radius;
    }
		
    bestslidefrac = FRACUNIT+1;
	
    P_PathTraverse ( leadx, leady, leadx+mo->momx, leady+mo->momy,
		     PT_ADDLINES, PTR_SlideTraverse );
    P_PathTraverse ( trailx, leady, trailx+mo->momx, leady+mo->momy,
		     PT_ADDLINES, PTR_SlideTraverse );
    P_PathTraverse ( leadx, traily, leadx+mo->momx, traily+mo->momy,
		     PT_ADDLINES, PTR_SlideTraverse );
    
    // move up to the wall
    if (bestslidefrac == FRACUNIT+1)
    {
	// the move most have hit the middle, so stairstep
      stairstep:
	if (!P_TryMove (mo, mo->x, mo->y + mo->momy))
	    P_TryMove (mo, mo->x + mo->momx, mo->y);
	return;
    }

    // fudge a bit to make sure it doesn't hit
    bestslidefrac -= 0x800;	
    if (bestslidefrac > 0)
    {
	newx = FixedMul (mo->momx, bestslidefrac);
	newy = FixedMul (mo->momy, bestslidefrac);
	
	if (!P_TryMove (mo, mo->x+newx, mo->y+newy))
	    goto stairstep;
    }
    
    // Now continue along the wall.
    // First calculate remainder.
    bestslidefrac = FRACUNIT-(bestslidefrac+0x800);
    
    if (bestslidefrac > FRACUNIT)
	bestslidefrac = FRACUNIT;
    
    if (bestslidefrac <= 0)
	return;
    
    tmxmove = FixedMul (mo->momx, bestslidefrac);
    tmymove = FixedMul (mo->momy, bestslidefrac);

    P_HitSlideLine (bestslideline);	// clip the moves

    mo->momx = tmxmove;
    mo->momy = tmymove;
		
    if (!P_TryMove (mo, mo->x+tmxmove, mo->y+tmymove))
    {
	goto retry;
    }
}


//
// P_LineAttack
//
mobj_t*		linetarget;	// who got hit (or NULL)
mobj_t*		shootthing;

// Height if not aiming up or down
// ???: use slope for monsters?
fixed_t		shootz;	

int		la_damage;
fixed_t		attackrange;

fixed_t		aimslope;

// slopes to top and bottom of target
extern fixed_t	topslope;
extern fixed_t	bottomslope;	


//
// PTR_AimTraverse
// Sets linetaget and aimslope when a target is aimed at.
//
boolean
PTR_AimTraverse (intercept_t* in)
{
    line_t*		li;
    mobj_t*		th;
    fixed_t		slope;
    fixed_t		thingtopslope;
    fixed_t		thingbottomslope;
    fixed_t		dist;
		
    if (in->isaline)
    {
	li = in->d.line;
        if(P_GlobalDigBreachRay((int)(li-lines),
            (trace.x+FixedMul(trace.dx,in->frac))/(float)FRACUNIT,
            (trace.y+FixedMul(trace.dy,in->frac))/(float)FRACUNIT,shootz/(float)FRACUNIT,
            trace.dx/(float)FRACUNIT,trace.dy/(float)FRACUNIT,0)){
            bottomslope=0;topslope=1;return true;
        }
	if ( !(li->flags & ML_TWOSIDED) )
	    return false;		// stop
	
	// Crosses a two sided line.
	// A two sided line will restrict
	// the possible target ranges.
	P_LineOpening (li);
	
	if (openbottom >= opentop)
	    return false;		// stop
	
	dist = FixedMul (attackrange, in->frac);

	if (li->frontsector->floorheight != li->backsector->floorheight)
	{
	    slope = FixedDiv (openbottom - shootz , dist);
	    if (slope > bottomslope)
		bottomslope = slope;
	}
		
	if (li->frontsector->ceilingheight != li->backsector->ceilingheight)
	{
	    slope = FixedDiv (opentop - shootz , dist);
	    if (slope < topslope)
		topslope = slope;
	}
		
	if (topslope <= bottomslope)
	    return false;		// stop
			
	return true;			// shot continues
    }
    
    // shoot a thing
    th = in->d.thing;
    if (th == shootthing)
	return true;			// can't shoot self
    
    if (!(th->flags&MF_SHOOTABLE))
	return true;			// corpse or something

    // check angles to see if the thing can be aimed at
    dist = FixedMul (attackrange, in->frac);
    thingtopslope = FixedDiv (th->z+th->height - shootz , dist);

    if (thingtopslope < bottomslope)
	return true;			// shot over the thing

    thingbottomslope = FixedDiv (th->z - shootz, dist);

    if (thingbottomslope > topslope)
	return true;			// shot under the thing
    
    // this thing can be hit!
    if (thingtopslope > topslope)
	thingtopslope = topslope;
    
    if (thingbottomslope < bottomslope)
	thingbottomslope = bottomslope;

    aimslope = (thingtopslope+thingbottomslope)/2;
    linetarget = th;

    return false;			// don't go any farther
}


//
// PTR_ShootPlane
// A free-aimed shot (la_freeaim) also stops on floors and ceilings: before
// each line or thing, and at the end of its range, it checks whether it
// already left la_sector, the sector it is passing through, vertically.
// Returns false when it did.
//
void (*P_ShotTraced)(mobj_t *shooter,mobj_t *hit,fixed_t x1,fixed_t y1,fixed_t z1,fixed_t x2,fixed_t y2,fixed_t z2);
static fixed_t la_endfrac;
static mobj_t *la_hit;
boolean		la_freeaim;
static sector_t* la_sector;

// Where the shot stopped, as a fraction of attackrange, and what it hit.
void (*P_ShotTraced)(mobj_t *shooter, mobj_t *hit, fixed_t x1, fixed_t y1, fixed_t z1,
                     fixed_t x2, fixed_t y2, fixed_t z2);
static fixed_t	la_endfrac;
static mobj_t*	la_hit;

static boolean PTR_ShootPlane (fixed_t frac)
{
    if(SV_Active())return true; // The shared mesh already bounds this shot.
    fixed_t	z = shootz + FixedMul (aimslope, FixedMul (frac, attackrange));
    fixed_t	plane;
    fixed_t	dist;
    fixed_t	x;
    fixed_t	y;
    boolean	ceiling;

    if (z < la_sector->floorheight)
	plane = la_sector->floorheight, ceiling = false;
    else if (z > la_sector->ceilingheight)
	plane = la_sector->ceilingheight, ceiling = true;
    else
	return true;

    // don't shoot the sky!
    if ((ceiling ? la_sector->ceilingpic : la_sector->floorpic) == skyflatnum)
	return false;

    dist = aimslope ? FixedDiv (plane - shootz, aimslope) : 0;
    if (dist < 0)
	dist = 0;
    if (dist < FixedMul (frac, attackrange))
	frac = FixedDiv (dist, attackrange);
    la_endfrac=frac;
    x = trace.x + FixedMul (trace.dx, frac);
    y = trace.y + FixedMul (trace.dy, frac);
    z = ceiling ? plane - 4*FRACUNIT : plane;
    la_endfrac = frac;

    if(la_damage>0)P_GlobalDigBulletFlat(shootthing,(int)(la_sector-sectors),ceiling,x,y);
    if (!R_PlaneImpact || !R_PlaneImpact (la_sector, ceiling, x, y, z))
	P_SpawnPuff (x, y, z);
    return false;
}


//
// PTR_ShootTraverse
//
static fixed_t solidFraction;
static float solidPoint[6];
static int solidOwner;
boolean PTR_ShootTraverse (intercept_t* in)
{
    fixed_t		x;
    fixed_t		y;
    fixed_t		z;
    fixed_t		frac;
    
    line_t*		li;
    
    mobj_t*		th;

    fixed_t		slope;
    fixed_t		dist;
    fixed_t		thingtopslope;
    fixed_t		thingbottomslope;
		
    if(SV_Active()&&in->frac>=solidFraction&&solidFraction<FRACUNIT){
        if(SV_MapMode()&&in->isaline&&R_WallImpact&&
           FixedMul(in->frac-solidFraction,attackrange)<=2*FRACUNIT)
            R_WallImpact(in->d.line,P_PointOnLineSide(trace.x,trace.y,in->d.line),
                trace.x+FixedMul(trace.dx,in->frac),
                trace.y+FixedMul(trace.dy,in->frac),
                shootz+FixedMul(aimslope,FixedMul(attackrange,in->frac)));
        if(SV_MapMode()&&in->isaline&&in->d.line->special&&
           FixedMul(in->frac-solidFraction,attackrange)<=2*FRACUNIT)
            P_ShootSpecialLine(shootthing,in->d.line);
        la_endfrac=solidFraction;return false;
    }
    if (la_freeaim && !PTR_ShootPlane (in->frac))
	return false;
    la_endfrac = in->frac;

    la_endfrac=in->frac;
    if (in->isaline)
    {
	li = in->d.line;
	
	if (li->special)
	    P_ShootSpecialLine (shootthing, li);
        if(SV_MapMode()){
            // Glass is a gameplay effect on an open portal, not part of the
            // solid volume. Keep the original crossing callback.
            if(R_ShotPassed)R_ShotPassed(li,
                trace.x+FixedMul(trace.dx,in->frac),
                trace.y+FixedMul(trace.dy,in->frac),
                shootz+FixedMul(aimslope,FixedMul(attackrange,in->frac)),
                trace.dx,trace.dy);
            return true;
        }
        dist=FixedMul(attackrange,in->frac);
        if(P_GlobalDigBreachRay((int)(li-lines),
            (trace.x+FixedMul(trace.dx,in->frac))/(float)FRACUNIT,
            (trace.y+FixedMul(trace.dy,in->frac))/(float)FRACUNIT,
            (shootz+FixedMul(aimslope,dist))/(float)FRACUNIT,
            trace.dx/(float)FRACUNIT,trace.dy/(float)FRACUNIT,
            FixedMul(aimslope,attackrange)/(float)FRACUNIT)){
            if(la_freeaim){int room=P_GlobalDigBreachSector((int)(li-lines),0,0,trace.dx/(float)FRACUNIT,trace.dy/(float)FRACUNIT);if(room>=0)la_sector=&sectors[room];}
            return true;
        }


	if ( !(li->flags & ML_TWOSIDED) )
	    goto hitline;
	
	// crosses a two sided line
	P_LineOpening (li);
		
	dist = FixedMul (attackrange, in->frac);

	if (li->frontsector->floorheight != li->backsector->floorheight)
	{
	    slope = FixedDiv (openbottom - shootz , dist);
	    if (slope > aimslope)
		goto hitline;
	}
		
	if (li->frontsector->ceilingheight != li->backsector->ceilingheight)
	{
	    slope = FixedDiv (opentop - shootz , dist);
	    if (slope < aimslope)
		goto hitline;
	}

	// shot continues
	if (R_ShotPassed)
	    R_ShotPassed (li, trace.x + FixedMul (trace.dx, in->frac),
			  trace.y + FixedMul (trace.dy, in->frac),
			  shootz + FixedMul (aimslope, dist), trace.dx, trace.dy);
	if (la_freeaim)
	    la_sector = P_PointOnLineSide (trace.x, trace.y, li)
		? li->frontsector : li->backsector;
	return true;
	
	
	// hit line
      hitline:
	// position a bit closer
	frac = in->frac - FixedDiv (4*FRACUNIT,attackrange);
	x = trace.x + FixedMul (trace.dx, frac);
	y = trace.y + FixedMul (trace.dy, frac);
	z = shootz + FixedMul (aimslope, FixedMul(frac, attackrange));

	if (li->frontsector->ceilingpic == skyflatnum)
	{
	    // don't shoot the sky!
	    if (z > li->frontsector->ceilingheight)
		return false;
	    
	    // it's a sky hack wall
	    if	(li->backsector && li->backsector->ceilingpic == skyflatnum)
		return false;		
	}

        if(la_damage>0)P_GlobalDigBulletWall(shootthing,(int)(li-lines),P_PointOnLineSide(trace.x,trace.y,li),
            trace.x+FixedMul(trace.dx,in->frac),trace.y+FixedMul(trace.dy,in->frac),
            shootz+FixedMul(aimslope,FixedMul(in->frac,attackrange)));
	// Spawn bullet puffs.
	P_SpawnPuff (x,y,z);
	if (R_WallImpact)
	    R_WallImpact (li, P_PointOnLineSide (trace.x, trace.y, li), x, y, z);
	
	// don't go any farther
	return false;	
    }
    
    // shoot a thing
    th = in->d.thing;
    if (th == shootthing)
	return true;		// can't shoot self
    
    if (!(th->flags&MF_SHOOTABLE))
	return true;		// corpse or something
		
    // check angles to see if the thing can be aimed at
    dist = FixedMul (attackrange, in->frac);
    thingtopslope = FixedDiv (th->z+th->height - shootz , dist);

    if (thingtopslope < aimslope)
	return true;		// shot over the thing

    thingbottomslope = FixedDiv (th->z - shootz, dist);

    if (thingbottomslope > aimslope)
	return true;		// shot under the thing

    
    // hit thing
    // position a bit closer
    frac = in->frac - FixedDiv (10*FRACUNIT,attackrange);
    la_hit = th;

    x = trace.x + FixedMul (trace.dx, frac);
    y = trace.y + FixedMul (trace.dy, frac);
    z = shootz + FixedMul (aimslope, FixedMul(frac, attackrange));

    // Spawn bullet puffs or blod spots,
    // depending on target type.
    if (in->d.thing->flags & MF_NOBLOOD)
	P_SpawnPuff (x,y,z);
    else
    {
	P_SpawnBlood (x,y,z, la_damage);
	if (R_BloodHit)
	    R_BloodHit (th, shootthing, x, y, z, trace.dx, trace.dy, aimslope, la_damage);
    }

    la_hit=th;
    if (la_damage)
	P_DamageMobj (th, shootthing, shootthing, la_damage);

    // don't go any farther
    return false;
	
}


//
// P_AimLineAttack
//
int (*P_PeekOriginHook)(mobj_t*,fixed_t*,fixed_t*,fixed_t*)=NULL;
boolean P_PeekShotOrigin(mobj_t *actor, fixed_t *x, fixed_t *y, fixed_t *z)
{
    return P_PeekOriginHook && P_PeekOriginHook(actor,x,y,z);
}

fixed_t
P_AimLineAttack
( mobj_t*	t1,
  angle_t	angle,
  fixed_t	distance )
{
    fixed_t x1=t1->x,y1=t1->y;
    fixed_t	x2;
    fixed_t	y2;
	
    angle >>= ANGLETOFINESHIFT;
    shootthing = t1;
    
    x2 = x1 + (distance>>FRACBITS)*finecosine[angle];
    y2 = y1 + (distance>>FRACBITS)*finesine[angle];
    shootz = t1->z + (t1->height>>1) + 8*FRACUNIT;

    P_PeekShotOrigin(t1,&x1,&y1,&shootz);
    x2=x1+(distance>>FRACBITS)*finecosine[angle];
    y2=y1+(distance>>FRACBITS)*finesine[angle];

    // can't shoot outside view angles
    topslope = 100*FRACUNIT/160;	
    bottomslope = -100*FRACUNIT/160;
    
    attackrange = distance;
    linetarget = NULL;
	
    P_PathTraverse ( x1, y1,
		     x2, y2,
		     PT_ADDLINES|PT_ADDTHINGS,
		     PTR_AimTraverse );
		
    if (linetarget)
	return aimslope;

    return 0;
}
 

//
// P_LineAttack
// If damage == 0, it is just a test trace
// that will leave linetarget set.
//
void
P_LineAttack
( mobj_t*	t1,
  angle_t	angle,
  fixed_t	distance,
  fixed_t	slope,
  int		damage )
{
    fixed_t x1=t1->x,y1=t1->y;
    fixed_t	x2;
    fixed_t	y2;
	
    angle >>= ANGLETOFINESHIFT;
    shootthing = t1;
    la_damage = damage;
    x2 = x1 + (distance>>FRACBITS)*finecosine[angle];
    y2 = y1 + (distance>>FRACBITS)*finesine[angle];
    shootz = t1->z + (t1->height>>1) + 8*FRACUNIT;
    attackrange = distance;
    aimslope = slope;
    if (la_freeaim)
    {
	// From the eye, where the crosshair sits.
	shootz = t1->player->viewz;
	la_sector = t1->subsector->sector;
    }
    if(P_PeekShotOrigin(t1,&x1,&y1,&shootz)&&la_freeaim)la_sector=R_PointInSubsector(x1,y1)->sector;
    x2=x1+(distance>>FRACBITS)*finecosine[angle];
    y2=y1+(distance>>FRACBITS)*finesine[angle];
    la_hit = NULL;
		
    solidFraction=FRACUNIT;
    if(SV_Active())solidFraction=(fixed_t)(FRACUNIT*SV_Trace(x1/(float)FRACUNIT,y1/(float)FRACUNIT,shootz/(float)FRACUNIT,
        (x2-x1)/(float)FRACUNIT,(y2-y1)/(float)FRACUNIT,FixedMul(slope,distance)/(float)FRACUNIT,solidPoint,&solidOwner));
    la_hit=NULL;la_endfrac=FRACUNIT;
    if (P_PathTraverse ( x1, y1,
			 x2, y2,
			 PT_ADDLINES|PT_ADDTHINGS,
			 PTR_ShootTraverse ))
    {la_endfrac=FRACUNIT;if(la_freeaim)PTR_ShootPlane(FRACUNIT);}
    if(SV_Active()&&!la_hit&&solidFraction<FRACUNIT&&la_endfrac>=solidFraction){
        la_endfrac=solidFraction;
        if(damage>0){
            if(!SV_MapMode()||(t1->player?p_player_surface_damage:p_monster_surface_damage))SV_Impact(solidPoint,solidOwner,2.f);
            fixed_t hx=(fixed_t)(solidPoint[0]*FRACUNIT),hy=(fixed_t)(solidPoint[1]*FRACUNIT),hz=(fixed_t)(solidPoint[2]*FRACUNIT);
            sector_t *room=R_PointInSubsector(hx,hy)->sector;
            // Original sector floors still need the liquid/flat callback in
            // solid-world mode. Rubble and excavated surfaces use the mesh hit.
            boolean floorHit=solidPoint[5]>.5f&&abs(hz-room->floorheight)<=2*FRACUNIT;
            boolean handled=false;
            if(floorHit&&R_PlaneImpact)handled=R_PlaneImpact(room,false,hx,hy,hz);
            else if(R_SolidBulletImpact)R_SolidBulletImpact(solidPoint[0],solidPoint[1],solidPoint[2],solidPoint[3],solidPoint[4],solidPoint[5]);
            if(!handled)P_SpawnPuff(hx,hy,hz);
        }
    }
    la_freeaim = false;
    if(P_ShotTraced&&damage)P_ShotTraced(t1,la_hit,x1,y1,shootz,x1+FixedMul(x2-x1,la_endfrac),y1+FixedMul(y2-y1,la_endfrac),shootz+FixedMul(aimslope,FixedMul(la_endfrac,attackrange)));
}
 


//
// USE LINES
//
mobj_t*		usething;

boolean	PTR_UseTraverse (intercept_t* in)
{
    int		side;
	
    if (!in->d.line->special)
    {
	P_LineOpening (in->d.line);
	if (openrange <= 0)
	{
	    S_StartSound (usething, sfx_noway);
	    
	    // can't use through a wall
	    return false;	
	}
	// not a special line, but keep checking
	return true ;		
    }
	
    side = 0;
    if (P_PointOnLineSide (usething->x, usething->y, in->d.line) == 1)
	side = 1;
    
    //	return false;		// don't use back side
	
    P_UseSpecialLine (usething, in->d.line, side);

    // can't use for than one special line in a row
    return false;
}


//
// P_UseLines
// Looks for special lines in front of the player to activate.
//
void P_UseLines (player_t*	player) 
{
    if(SV_Active()&&!SV_MapMode()){SV_Use(player);return;}
    int		angle;
    fixed_t	x1;
    fixed_t	y1;
    fixed_t	x2;
    fixed_t	y2;
	
    usething = player->mo;
		
    angle = player->mo->angle >> ANGLETOFINESHIFT;

    x1 = player->mo->x;
    y1 = player->mo->y;
    x2 = x1 + (USERANGE>>FRACBITS)*finecosine[angle];
    y2 = y1 + (USERANGE>>FRACBITS)*finesine[angle];
	
    P_PathTraverse ( x1, y1, x2, y2, PT_ADDLINES, PTR_UseTraverse );
}


//
// RADIUS ATTACK
//
mobj_t*		bombsource;
mobj_t*		bombspot;
int		bombdamage;


//
// PIT_RadiusAttack
// "bombsource" is the creature
// that caused the explosion at "bombspot".
//
boolean PIT_RadiusAttack (mobj_t* thing)
{
    fixed_t	dx;
    fixed_t	dy;
    fixed_t	dist;
	
    if (!(thing->flags & MF_SHOOTABLE) )
	return true;

    // Boss spider and cyborg
    // take no damage from concussion.
    if (thing->type == MT_CYBORG
	|| thing->type == MT_SPIDER)
	return true;	
		
    dx = abs(thing->x - bombspot->x);
    dy = abs(thing->y - bombspot->y);
    
    dist = dx>dy ? dx : dy;
    dist = (dist - thing->radius) >> FRACBITS;

    if (dist < 0)
	dist = 0;

    if (dist >= bombdamage)
	return true;	// out of range

    if ( P_CheckSight (thing, bombspot) )
    {
	// must be in direct path
	P_DamageMobj (thing, bombspot, bombsource, bombdamage - dist);
    }
    
    return true;
}


//
// P_RadiusAttack
// Source is the creature that caused the explosion at spot.
//
void
P_RadiusAttack
( mobj_t*	spot,
  mobj_t*	source,
  int		damage )
{
    int		x;
    int		y;
    
    int		xl;
    int		xh;
    int		yl;
    int		yh;
    
    fixed_t	dist;
	
    dist = (damage+MAXRADIUS)<<FRACBITS;
    yh = (spot->y + dist - bmaporgy)>>MAPBLOCKSHIFT;
    yl = (spot->y - dist - bmaporgy)>>MAPBLOCKSHIFT;
    xh = (spot->x + dist - bmaporgx)>>MAPBLOCKSHIFT;
    xl = (spot->x - dist - bmaporgx)>>MAPBLOCKSHIFT;
    bombspot = spot;
    bombsource = source;
    bombdamage = damage;
    SV_Blast(spot,damage);
	
    for (y=yl ; y<=yh ; y++)
	for (x=xl ; x<=xh ; x++)
	    P_BlockThingsIterator (x, y, PIT_RadiusAttack );
}



//
// SECTOR HEIGHT CHANGING
// After modifying a sectors floor or ceiling height,
// call this routine to adjust the positions
// of all things that touch the sector.
//
// If anything doesn't fit anymore, true will be returned.
// If crunch is true, they will take damage
//  as they are being crushed.
// If Crunch is false, you should set the sector height back
//  the way it was and call P_ChangeSector again
//  to undo the changes.
//
boolean		crushchange;
boolean		nofit;


//
// PIT_ChangeSector
//
boolean PIT_ChangeSector (mobj_t*	thing)
{
    mobj_t*	mo;
	
    if (P_ThingHeightClip (thing))
    {
	// keep checking
	return true;
    }
    

    // crunch bodies to giblets
    if (thing->health <= 0)
    {
	P_SetMobjState (thing, S_GIBS);

	thing->flags &= ~MF_SOLID;
	thing->height = 0;
	thing->radius = 0;

	// keep checking
	return true;		
    }

    // crunch dropped items
    if (thing->flags & MF_DROPPED)
    {
	P_RemoveMobj (thing);
	
	// keep checking
	return true;		
    }

    if (! (thing->flags & MF_SHOOTABLE) )
    {
	// assume it is bloody gibs or something
	return true;			
    }
    
    nofit = true;

    if (crushchange && !(leveltime&3) )
    {
	P_DamageMobj(thing,NULL,NULL,10);

	// spray blood in a random direction
	mo = P_SpawnMobj (thing->x,
			  thing->y,
			  thing->z + thing->height/2, MT_BLOOD);
	
	mo->momx = (P_Random() - P_Random ())<<12;
	mo->momy = (P_Random() - P_Random ())<<12;
    }

    // keep checking (crush other things)	
    return true;	
}



//
// P_ChangeSector
//
boolean
P_ChangeSector
( sector_t*	sector,
  boolean	crunch )
{
    int		x;
    int		y;
    SV_SectorChanged(sector);
	
    nofit = false;
    crushchange = crunch;
	
    // re-check heights for all things near the moving sector
    for (x=sector->blockbox[BOXLEFT] ; x<= sector->blockbox[BOXRIGHT] ; x++)
	for (y=sector->blockbox[BOXBOTTOM];y<= sector->blockbox[BOXTOP] ; y++)
	    P_BlockThingsIterator (x, y, PIT_ChangeSector);
	
	
    return nofit;
}

