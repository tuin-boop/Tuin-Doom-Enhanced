#include "p_arena.h"
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
//	Archiving: SaveGame I/O.
//	Thinker, Ticker.
//
//-----------------------------------------------------------------------------

static const char
rcsid[] = "$Id: p_tick.c,v 1.4 1997/02/03 16:47:55 b1 Exp $";

#include <time.h>
#include <stdio.h>
#include "m_argv.h"
#include "z_zone.h"
#include "p_local.h"
#include "p_solid.h"

#include "doomstat.h"
#include "r_state.h"


int	leveltime;
int r_photoauto=0,r_photomask=31,r_photoscope=0,r_photoevent=0,r_photocooldown=0;
mobj_t *r_photoeventsubject=0;
static unsigned photoEventLevel;
void P_PhotoEvent(int event,mobj_t *subject){
    if(photoEventLevel!=r_levelserial){photoEventLevel=r_levelserial;r_photoevent=0;r_photoeventsubject=0;r_photocooldown=0;}
    if(!r_photoauto||!(r_photomask&event)||r_photoactive||leveltime<r_photocooldown||
       gamestate!=GS_LEVEL||menuactive||paused||netgame||demorecording||demoplayback||!subject)return;
    if(event!=8){
        if(!subject->player&&!(subject->info->flags&MF_COUNTKILL)&&subject->type!=MT_SKULL)return;
        if((r_photoscope==1&&!subject->player)||(r_photoscope==2&&subject->player))return;
    }
    mobj_t *p=players[displayplayer].mo;if(!p)return;
    double dx=((double)subject->x-p->x)/FRACUNIT,dy=((double)subject->y-p->y)/FRACUNIT;
    if(dx*dx+dy*dy>2048.0*2048.0)return;
    /* Prefer deaths/explosions over incidental firing or damage in this tick. */
    if(event>=r_photoevent){r_photoevent=event;r_photoeventsubject=subject;}
}


//
// THINKERS
// All thinkers should be allocated by Z_Malloc
// so they can be operated on uniformly.
// The actual structures will vary in size,
// but the first element must be thinker_t.
//



// Both the head and tail of the thinker list.
thinker_t	thinkercap;


//
// P_InitThinkers
//
void P_InitThinkers (void)
{
    thinkercap.prev = thinkercap.next  = &thinkercap;
}




//
// P_AddThinker
// Adds a new thinker at the end of the list.
//
void P_AddThinker (thinker_t* thinker)
{
    thinkercap.prev->next = thinker;
    thinker->next = &thinkercap;
    thinker->prev = thinkercap.prev;
    thinkercap.prev = thinker;
}



//
// P_RemoveThinker
// Deallocation is lazy -- it will not actually be freed
// until its thinking turn comes up.
//
void P_RemoveThinker (thinker_t* thinker)
{
  // FIXME: NOP.
  thinker->function.acv = (actionf_v)(-1);
}



//
// P_AllocateThinker
// Allocates memory and adds a new thinker at the end of the list.
//
void P_AllocateThinker (thinker_t*	thinker)
{
}



//
// P_RunThinkers
//
void P_RunThinkers (void)
{
    thinker_t*	currentthinker;

    const int profileSteps=M_CheckParm("-profilegamesteps");
    currentthinker = thinkercap.next;
    while (currentthinker != &thinkercap)
    {
	if ( currentthinker->function.acv == (actionf_v)(-1) )
	{
	    // time to remove it
	    currentthinker->next->prev = currentthinker->prev;
	    currentthinker->prev->next = currentthinker->next;
	    Z_Free (currentthinker);
	}
	else
	{
	    if (currentthinker->function.acp1)
		{
            clock_t begin=profileSteps?clock():0;
            int stateBefore=(profileSteps&&currentthinker->function.acp1==(actionf_p1)P_MobjThinker)?(int)(((mobj_t*)currentthinker)->state-states):-1;
            currentthinker->function.acp1 (currentthinker);
            if(profileSteps&&clock()-begin>CLOCKS_PER_SEC*.005){
                int type=-1;
                if(currentthinker->function.acp1==(actionf_p1)P_MobjThinker)type=((mobj_t*)currentthinker)->type;
                printf("THINK tic %d type %d state %d %.3f ms\n",gametic,type,stateBefore,1000.0*(clock()-begin)/CLOCKS_PER_SEC);
            }
        }
	}
	currentthinker = currentthinker->next;
    }
}



//
// P_Ticker
//

void P_Ticker (void)
{
    int		i;

    if(r_photoactive&&!netgame){
        if(r_photosteps<=0)return;
        --r_photosteps;
        /* Advance the world, without replaying held movement/fire commands. */
        for(i=0;i<MAXPLAYERS;++i)if(playeringame[i]){
            players[i].cmd.forwardmove=players[i].cmd.sidemove=0;
            players[i].cmd.angleturn=players[i].cmd.buttons=0;
        }
    }
    static unsigned wheelTicks;
    if(!netgame&&!demorecording&&!demoplayback&&!menuactive&&!paused&&R_WheelSlowMotion&&R_WheelSlowMotion()){
        if(++wheelTicks%20)return; /* 5% simulation speed; input/render remain responsive. */
    }else wheelTicks=0;

    // run the tic
    if (paused)
	return;
		
    // pause if in menu and at least one tic has been run
    if ( !netgame
	 && menuactive
	 && !demoplayback
	 && players[consoleplayer].viewz != 1)
    {
	return;
    }
    
		
    if (R_BeforeTic) R_BeforeTic();
    for (i=0 ; i<MAXPLAYERS ; i++)
	if (playeringame[i])
	    P_PlayerThink (&players[i]);
			
    P_ArenaTick();
    SV_Tick();
    P_RunThinkers ();
    P_UpdateSpecials ();
    P_RespawnSpecials ();

    // for par times
    leveltime++;
    if(r_photoevent&&!r_photoactive&&r_photoauto)r_photoactive=2; /* Open after the completed simulation tick. */
}
