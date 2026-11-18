/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "g_local.h"
#include "g_tdm_curl.h"

game_locals_t	game;
level_locals_t	level;
game_import_t	gi;
game_export_t	globals;
spawn_temp_t	st;

int	sm_meat_index;
int	snd_fry;
int meansOfDeath;

edict_t		*g_edicts;
int			g_max_clients;

//cvar_t	*deathmatch;
//cvar_t	*coop;
cvar_t	*dmflags;
//cvar_t	*skill;
cvar_t	*fraglimit;
cvar_t	*timelimit;
cvar_t	*password;
cvar_t	*spectator_password;
cvar_t	*maxclients;
//cvar_t	*maxspectators;
cvar_t	*maxentities;
cvar_t	*g_select_empty;
cvar_t	*dedicated;

cvar_t	*filterban;

cvar_t	*sv_maxvelocity;
cvar_t	*sv_gravity;

cvar_t	*sv_rollspeed;
cvar_t	*sv_rollangle;
cvar_t	*gun_x;
cvar_t	*gun_y;
cvar_t	*gun_z;

cvar_t	*run_pitch;
cvar_t	*run_roll;
cvar_t	*bob_up;
cvar_t	*bob_pitch;
cvar_t	*bob_roll;

cvar_t	*sv_cheats;

cvar_t	*flood_msgs;
cvar_t	*flood_persecond;
cvar_t	*flood_waitdelay;

cvar_t	*flood_waves;
cvar_t	*flood_waves_perminute;
cvar_t	*flood_waves_waitdelay;

cvar_t	*sv_maplist;

cvar_t	*g_team_a_name;
cvar_t	*g_team_b_name;
cvar_t	*g_locked_names;

cvar_t	*g_team_a_skin;
cvar_t	*g_team_b_skin;
cvar_t	*g_locked_skins;

cvar_t	*g_admin_password;
cvar_t	*g_admin_vote_decide;
cvar_t	*g_match_time;
cvar_t	*g_match_countdown;
cvar_t	*g_vote_time;
cvar_t	*g_vote_mask;
cvar_t	*g_intermission_time;
cvar_t	*g_force_screenshot;
cvar_t	*g_force_record;

cvar_t	*g_tdmflags;
cvar_t	*g_itdmflags;
cvar_t	*g_1v1flags;

cvar_t	*g_itemflags;
cvar_t	*g_powerupflags;

cvar_t	*g_tdm_allow_pick;

cvar_t	*g_fast_weap_switch;
cvar_t	*g_teleporter_nofreeze;
cvar_t	*g_overtime;
cvar_t	*g_tie_mode;
cvar_t	*g_crushdiag;
cvar_t	*g_gamemode;
cvar_t	*g_respawn_time;
cvar_t	*g_max_timeout;
cvar_t	*g_1v1_timeout;
cvar_t	*g_chat_mode;
cvar_t	*g_idle_time;

cvar_t	*g_http_enabled;
cvar_t	*g_http_bind;
cvar_t	*g_http_proxy;
cvar_t	*g_http_debug;

cvar_t	*g_http_path;
cvar_t	*g_http_domain;

cvar_t	*g_debug_spawns;

cvar_t	*g_max_players_per_team;

cvar_t	*g_maplistfile;
cvar_t	*g_motd_message;

cvar_t	*g_bugs;

cvar_t	*g_allow_name_change_during_match;

cvar_t	*g_allow_vote_config;

cvar_t	*g_command_mask;

cvar_t	*g_auto_rejoin_match;
cvar_t	*g_auto_rejoin_map;

cvar_t	*g_1v1_spawn_mode;
cvar_t	*g_tdm_spawn_mode;

void SpawnEntities (const char *mapname, const char *entities, const char *spawnpoint);
void ClientThink (edict_t *ent, usercmd_t *cmd);
qboolean ClientConnect (edict_t *ent, char *userinfo);
void ClientUserinfoChanged (edict_t *ent, char *userinfo);
void ClientDisconnect (edict_t *ent);
void ClientBegin (edict_t *ent);
void ClientCommand (edict_t *ent);
void RunEntity (edict_t *ent);
void DummyWrite (const char *filename, qboolean autosave);
void DummyRead (const char *filename);
void InitGame (void);
void G_RunFrame (void);


//===================================================================


void ShutdownGame (void)
{
	gi.dprintf ("==== ShutdownGame ====\n");

	HTTP_Shutdown();

	gi.cvar_forceset ("g_features", "0");

	gi.FreeTags (TAG_LEVEL);
	gi.FreeTags (TAG_GAME);
}


/*
=================
GetGameAPI

Returns a pointer to the structure with all entry points
and global variables
=================
*/
DLL_EXPORT game_export_t *GetGameAPI (game_import_t *import)
{
	gi = *import;

	globals.apiversion = GAME_API_VERSION;
	globals.Init = InitGame;
	globals.Shutdown = ShutdownGame;
	globals.SpawnEntities = SpawnEntities;

	globals.WriteGame = DummyWrite;
	globals.ReadGame = DummyRead;
	globals.WriteLevel = DummyRead;
	globals.ReadLevel = DummyRead;

	globals.ClientThink = ClientThink;
	globals.ClientConnect = ClientConnect;
	globals.ClientUserinfoChanged = ClientUserinfoChanged;
	globals.ClientDisconnect = ClientDisconnect;
	globals.ClientBegin = ClientBegin;
	globals.ClientCommand = ClientCommand;

	globals.RunFrame = G_RunFrame;

	globals.ServerCommand = ServerCommand;

	globals.edict_size = sizeof(edict_t);

	return &globals;
}

/*
=================
GetGameAPIEx

opentdm-60hz: the Q2PRO extended game API (inc/shared/gameext.h, version
3), used for one callback: the display of the weapon projectiles between
their 100 ms grid steps (G_GridProjectile) to clients faster than 10 Hz.
The engine gives a client's frame divisor through its private extension
OPENTDM60_FRAMEDIV_V1; without it (another engine) projectiles go out as
they are, as from a 10 Hz server. Every other entry point is NULL.
=================
*/
#define OPENTDM60_FRAMEDIV_V1	"OPENTDM60_FRAMEDIV_V1"

typedef struct
{
	int		(*ClientFrameDiv) (edict_t *ent);	// world frames per frame sent to it; 0: not in game
} opentdm60_framediv_v1_t;

typedef struct
{
	uint32	apiversion;
	uint32	structsize;
	void	*local_sound;
	void	*get_configstring;
	void	*clip;
	void	*inVIS;
	void	*(*GetExtension) (const char *name);
	void	*TagRealloc;
} q2pro_import_ex_t;

typedef struct
{
	uint32	apiversion;
	uint32	structsize;
	void		*(*GetExtension) (const char *name);
	qboolean	(*CanSave) (void);
	void		(*PrepFrame) (void);
	void		(*RestartFilesystem) (void);
	qboolean	(*CustomizeEntityToClient) (edict_t *client, edict_t *ent, entity_state_t *temp);
	qboolean	(*EntityVisibleToClient) (edict_t *client, edict_t *ent);
} q2pro_export_ex_t;

static const opentdm60_framediv_v1_t	*framediv_api;
static q2pro_export_ex_t				globals_ex;

/*
A client of `d` world frames per frame (d < 6 at 60 Hz) gets the projectile
at the fraction (j + d) / 6 of its last grid step, j world frames after
it, and the fraction j / 6 as old_origin: drawn between them, it is where
a 10 Hz client draws it at the same time (100 ms from the old to the new
grid point). Its angles turn by the same fraction of the step (the
shorter way round: a grenade spins 30 degrees per step on each axis).
Collision, touches and damage use the grid state only.
*/
static qboolean G_CustomizeEntityToClient (edict_t *client, edict_t *ent, entity_state_t *temp)
{
	int		div, d, j, i;
	float	f0, f1, da;

	if (!ent->grid_move || ent->grid_frame < 0 || !framediv_api)
		return false;

	div = SECS_TO_FRAMES(0.1f);
	d = framediv_api->ClientFrameDiv (client);
	if (d < 1 || d >= div)
		return false;	// the grid state itself, as from a 10 Hz server

	// the engine asks after G_RunFrame: the step of this frame is j = 0
	j = level.realframenum - 1 - ent->grid_frame;
	if (j < 0)
		j = 0;
	f1 = (float)(j + d < div ? j + d : div) / div;
	f0 = (float)(j < div ? j : div) / div;

	*temp = ent->s;
	for (i = 0; i < 3; i++)
	{
		temp->origin[i] = ent->grid_from[i] + (ent->s.origin[i] - ent->grid_from[i]) * f1;
		temp->old_origin[i] = ent->grid_from[i] + (ent->s.origin[i] - ent->grid_from[i]) * f0;
		da = ent->s.angles[i] - ent->grid_from_angles[i];
		da -= 360.0f * floorf ((da + 180.0f) / 360.0f);
		temp->angles[i] = ent->grid_from_angles[i] + da * f1;
	}
	return true;
}

DLL_EXPORT const q2pro_export_ex_t *GetGameAPIEx (const q2pro_import_ex_t *import)
{
	framediv_api = NULL;
	if (import && import->apiversion >= 1 && import->GetExtension &&
		import->structsize >= offsetof (q2pro_import_ex_t, GetExtension) + sizeof (import->GetExtension))
		framediv_api = import->GetExtension (OPENTDM60_FRAMEDIV_V1);

	memset (&globals_ex, 0, sizeof (globals_ex));
	globals_ex.apiversion = 3;
	globals_ex.structsize = sizeof (globals_ex);
	if (framediv_api && framediv_api->ClientFrameDiv)
		globals_ex.CustomizeEntityToClient = G_CustomizeEntityToClient;
	else
		framediv_api = NULL;
	return &globals_ex;
}

#ifndef GAME_HARD_LINKED
// this is only here so the functions in q_shared.c and q_shwin.c can link
void Sys_Error (const char *error, ...)
{
	va_list		argptr;
	char		text[1024];

	va_start (argptr, error);
	vsnprintf (text, sizeof(text)-1, error, argptr);
	va_end (argptr);

	gi.error ("%s", text);
}

void Com_Printf (const char *msg, int level, ...)
{
	va_list		argptr;
	char		text[1024];

	va_start (argptr, level);
	vsnprintf (text, sizeof(text)-1, msg, argptr);
	va_end (argptr);

	gi.dprintf ("%s", text);
}

#endif

//======================================================================


/*
=================
ClientEndServerFrames
=================
*/
void ClientEndServerFrames (void)
{
	int		i;
	edict_t	*ent;

	//run now to copy results out to spectators
	for (i=0 ; i < game.maxclients; i++)
	{
		ent = g_edicts + 1 + i;

		if (!ent->inuse || !ent->client)
			continue;

		if (ent->client->chase_mode == CHASE_LOCK)
		{
			UpdateLockCam (ent);
			continue;
		}
		else if (!ent->client->chase_target)
			continue;

		//this copies before the stats
		UpdateChaseCam (ent);
	}

	// calc the player views now that all pushing
	// and damage has been added
	for (i=0 ; i < game.maxclients; i++)
	{
		ent = g_edicts + 1 + i;
		if (!ent->inuse || !ent->client)
			continue;
		ClientEndServerFrame (ent);
	}


	//run now to copy results out to spectators
	for (i=0 ; i < game.maxclients; i++)
	{
		ent = g_edicts + 1 + i;

		if (!ent->inuse || !ent->client || !ent->client->chase_target)
			continue;

		//this copies after the stats
		G_SetSpectatorStats (ent);
	}
}

/*
=================
CreateTargetChangeLevel

Returns the created target changelevel
=================
*/
edict_t *CreateTargetChangeLevel(char *map)
{
	edict_t *ent;

	ent = G_Spawn ();
	ent->classname = "target_changelevel";
	Com_sprintf(level.nextmap, sizeof(level.nextmap), "%s", map);
	ent->map = level.nextmap;
	return ent;
}

/*
=================
EndDMLevel

The timelimit or fraglimit has been exceeded
=================
*/
void EndDMLevel (void)
{
	edict_t		*ent;
	char *s, *t, *f;
	static const char *seps = " ,\n\r";

	// stay on same level flag
	if ((int)dmflags->value & DF_SAME_LEVEL)
	{
		BeginIntermission (CreateTargetChangeLevel (level.mapname) );
		return;
	}

	// see if it's in the map list
	if (*sv_maplist->string)
	{
		s = strdup(sv_maplist->string);
		f = NULL;
		t = strtok(s, seps);
		while (t != NULL)
		{
			if (Q_stricmp(t, level.mapname) == 0)
			{
				// it's in the list, go to the next one
				t = strtok(NULL, seps);
				if (t == NULL)
				{
					// end of list, go to first one
					if (f == NULL) // there isn't a first one, same level
						BeginIntermission (CreateTargetChangeLevel (level.mapname) );
					else
						BeginIntermission (CreateTargetChangeLevel (f) );
				}
				else
					BeginIntermission (CreateTargetChangeLevel (t) );
				free(s);
				return;
			}
			if (!f)
				f = t;
			t = strtok(NULL, seps);
		}
		free(s);
	}

	if (level.nextmap[0]) // go to a specific map
		BeginIntermission (CreateTargetChangeLevel (level.nextmap) );
	else
	{
		// search for a changelevel
		ent = G_Find (NULL, FOFS(classname), "target_changelevel");
		if (!ent)
		{
			// the map designer didn't include a changelevel,
			// so create a fake ent that goes back to the same level
			BeginIntermission (CreateTargetChangeLevel (level.mapname) );
			return;
		}
		BeginIntermission (ent);
	}
}

/*
=================
CheckDMRules
=================
*/
void CheckDMRules (void)
{
	int			i;
	gclient_t	*cl;

	if (tdm_match_status == MM_SCOREBOARD)
		return;

	if (timelimit->value)
	{
		if (level.framenum >= SECS_TO_FRAMES(timelimit->value * 600))
		{
			gi.bprintf (PRINT_HIGH, "Timelimit hit.\n");
			EndDMLevel ();
			return;
		}
	}

	if (fraglimit->value)
	{
		for (i=0 ; i < game.maxclients; i++)
		{
			cl = game.clients + i;
			if (!g_edicts[i+1].inuse)
				continue;

			if (cl->resp.score >= fraglimit->value)
			{
				gi.bprintf (PRINT_HIGH, "Fraglimit hit.\n");
				EndDMLevel ();
				return;
			}
		}
	}
}


/*
=============
ExitLevel
=============
*/
void ExitLevel (void)
{
	/*int		i;
	edict_t	*ent;*/
	char	command [256];

	if (level.changemap)
	{
		Com_sprintf (command, sizeof(command), "gamemap \"%s\"\n", level.changemap);
		gi.AddCommandString (command);
		level.exitintermission = 2;
	}
	else
		level.exitintermission = 0;
	
	level.intermissionframe = 0;
	ClientEndServerFrames ();

	// clear some things before going to next level
	/*for (i=0 ; i < game.maxclients; i++)
	{
		ent = g_edicts + 1 + i;
		if (!ent->inuse)
			continue;
		if (ent->health > ent->max_health)
			ent->health = ent->max_health;
	}*/

	//this should be called implicitly on new map, check
	//TDM_ResetGameState ();
}

/*
================
G_RunFrame

Advances the world by 0.1 seconds
================
*/
void G_RunFrame (void)
{
	int		i;
	edict_t	*ent;

	//FIXME: shouldn't we increment framenum after the game has run, in preparation for the next frame?
	//otherwise usercmds that arrive after we return will still use the old framenum, which seems wrong.
	//level.realframenum++;

	// opentdm-60hz: a timeout freezes the game clock and the world on the
	// next grid frame, as on the next tick at 10 Hz (players and weapons
	// stop at once, as at 10 Hz); the resume is on a grid frame too
	// (TDM_CheckTimes), so the game's grid frames stay on FRAMESYNC
	if (tdm_match_status != MM_TIMEOUT)
		level.timeout_frozen = false;
	else if (FRAMESYNC)
	{
		// the frozen tick's thinks and things in flight (rework 4)
		if (!level.timeout_frozen)
			G_FreezeFrame ();
		level.timeout_frozen = true;
	}

	if (!GAME_FROZEN)
	{
		//level.framenum++;

		// exit intermissions

		if (level.exitintermission)
		{
			if (level.exitintermission == 2)
			{
				//if we got here, the map change from ExitLevel didn't work
				gi.bprintf (PRINT_CHAT, "ERROR: Map '%s' was not found on the server.\n", level.changemap);
				level.exitintermission = 0;
				TDM_ResetGameState ();
			}
			else
			{
				ExitLevel ();
				// opentdm-60hz: the engine counts this frame: keep the frame counters
				// in step with it (a failed map change goes on on this map, and the
				// grid frames must stay the engine's key frames)
				level.realframenum++;
				level.framenum++;
				return;
			}
		}

		//
		// treat each object in turn
		// even the world gets a chance to think
		//
		ent = &g_edicts[0];
		for (i=0 ; i<globals.num_edicts ; i++, ent++)
		{
			if (!ent->inuse)
				continue;

			level.current_entity = ent;

			VectorCopy (ent->old_origin, ent->s.old_origin);

			// if the ground entity moved, make sure we are still on it
			if ((ent->groundentity) && (ent->groundentity->linkcount != ent->groundentity_linkcount))
			{
				// opentdm-60hz (rework 7): a thing lying on a moving part (an item,
				// a body) too: its toss physics lands it again only on its 100 ms
				// step (the 10 Hz ballistics), so between grid frames it lost the
				// ride and the part moved away under it (at 10 Hz every frame is
				// that step: the original code). Only on a moving part: a thing
				// lying on a player falls on him again when he moves, as before.
				if (ent->client || (SECS_TO_FRAMES(0.1f) > 1 &&
					(ent->movetype == MOVETYPE_TOSS || ent->movetype == MOVETYPE_BOUNCE) &&
					(ent->groundentity->movetype == MOVETYPE_PUSH || ent->groundentity->movetype == MOVETYPE_STOP)))
				{
					vec3_t	end;
					trace_t	trace;

					// A pusher can carry a client without a new usercmd. Recheck
					// actual support, also after a team push was rolled back.
					// (q2trek/opentdm e5e5e4a)
					VectorCopy (ent->s.origin, end);
					end[2] -= 0.25f;
					trace = gi.trace (ent->s.origin, ent->mins, ent->maxs, end, ent,
						!ent->client ? (ent->clipmask ? ent->clipmask : MASK_SOLID)
						: ent->health > 0 ? MASK_PLAYERSOLID : MASK_DEADSOLID);
					if (!trace.startsolid && !trace.allsolid
						&& trace.ent == ent->groundentity
						&& trace.plane.normal[2] >= 0.7f)
					{
						ent->groundentity_linkcount = ent->groundentity->linkcount;
					}
					else
					{
						ent->groundentity = NULL;
					}
				}
				else
				{
					ent->groundentity = NULL;
				}
			}

			if (i > 0 && i <= game.maxclients)
			{
				ClientBeginServerFrame (ent);
				continue;
			}

			// opentdm-60hz: weapon projectiles run on the 100 ms grid frames only
			if (ent->grid_move)
			{
				if (FRAMESYNC)
					G_RunGridEntity (ent);
				continue;
			}

			G_RunEntity (ent);
		}
	}

	HTTP_RunDownloads ();

	TDM_UpdateConfigStrings (false);

	TDM_CheckTimes ();
	
	// see if it is time to end a deathmatch
	//CheckDMRules ();

	// build the playerstate_t structures for all players
	ClientEndServerFrames ();

	level.realframenum++;

	if (!GAME_FROZEN)
	{
		// save old_origins for next frame
		ent = &g_edicts[0];
		for (i=0 ; i<globals.num_edicts ; i++, ent++)
		{
			if (ent->inuse)
				VectorCopy (ent->s.origin, ent->old_origin);
		}

		level.framenum++;
	}
}
