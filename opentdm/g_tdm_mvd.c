/*
Automatic server MVD of every started match (opentdm-60hz).

Ported from the OpenTDM-X human/bot match recorder (otx_mvd.c at
9c0749f9b) for matches of people: no bot roster, no styles, no metadata.
The engine of the same delivery owns the file (q2proded src/server/mvd.c):
the module only sends private requests through gi.AddCommandString, which
the engine takes before its command buffer, so no console/rcon command can
fake them:

  mvdmatchrecord "<name>"   open demos/<name>.mvd2 at the next MVD frame
                            (exclusive create, the "Fight!" of this frame
                            included, plain .mvd2 like the bot recorder)
  mvdmatchname "<name>"     final name, applied when the file is closed
  mvdmatchstop              close after the current MVD frame

sv_mvd_autorecord_matches 1 enables it (default 0). With it on the engine
ignores mvdrecord/mvdstop/mvdstuff from the console and rcon. Countdown and
warmup make no file; timeout and overtime continue the same file.

Names: <date>_<time>_<map>_<team A>-vs-<team B>_<serial> while recording,
renamed at the end of the match to ..._<score A>-<score B>[_forfeit]_<serial>.
A side is its player's name, or its team name and player count. The order
of the sides is fixed at "Fight!". A match that ends by a reset, a vote
abort or a map change keeps the name without a score.
*/

#include "g_local.h"

static cvar_t	*mvd_autorecord;
static qboolean	mvd_requested;
static unsigned	mvd_serial;
static unsigned	mvd_match_serial;
static char		mvd_stem[192];

#define SIDE_MAX	40

void TDM_MvdInit (void)
{
	mvd_autorecord = gi.cvar ("sv_mvd_autorecord_matches", "0", 0);
	mvd_requested = false;
	mvd_serial = 0;
	mvd_match_serial = 0;
	mvd_stem[0] = 0;
}

/* Names can contain Quake colour bits and filesystem/command characters:
 * keep readable ASCII, drop the colour bit, replace everything else. */
static void filename_part (char *out, size_t size, const char *text, qboolean lower)
{
	size_t	n = 0;

	for (; *text && n + 1 < size; text++)
	{
		unsigned	c = (unsigned char)*text & 127;

		if (lower && c >= 'A' && c <= 'Z')
			c += 'a' - 'A';
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') || c == '-' || c == '_' || c == '[' || c == ']'))
			c = '_';
		if (c == '_' && n && out[n-1] == '_')
			continue;
		out[n++] = (char)c;
	}
	if (!n && size > 1)
		out[n++] = '_';
	out[n] = 0;
}

static void side_label (char *out, size_t size, int team)
{
	edict_t	*ent, *only = NULL;
	int		count = 0;
	char	part[SIDE_MAX + 1];

	for (ent = g_edicts + 1; ent <= g_edicts + game.maxclients; ent++)
	{
		if (!ent->inuse || !ent->client || ent->client->pers.mvdclient || ent->client->pers.team != team)
			continue;
		only = ent;
		count++;
	}

	if (count == 1)
	{
		filename_part (part, sizeof(part), only->client->pers.netname, false);
		Q_strncpy (out, part, size - 1);
	}
	else
	{
		filename_part (part, sizeof(part) - 4, teaminfo[team].name, false);
		Com_sprintf (out, size, "%s-%dp", part, count);
	}
}

void TDM_MvdEndMatch (const char *outcome)
{
	char	filename[256], command[300];

	if (!mvd_requested)
		return;

	/* outcome NULL: a reset/abort, the file keeps its name without a score */
	if (outcome)
	{
		Com_sprintf (filename, sizeof(filename), "%s_%d-%d%s%s_%03u", mvd_stem,
			teaminfo[TEAM_A].score, teaminfo[TEAM_B].score, outcome[0] ? "_" : "", outcome, mvd_match_serial);
		Com_sprintf (command, sizeof(command), "mvdmatchname \"%s\"\n", filename);
		gi.AddCommandString (command);
	}
	gi.AddCommandString ("mvdmatchstop\n");

	mvd_requested = false;
	mvd_stem[0] = 0;
}

void TDM_MvdBeginMatch (void)
{
	char		side[2][SIDE_MAX + 8], map[64], filename[256], command[300];
	time_t		now;
	struct tm	*tm;
	cvar_t		*enabled, *capability;

	if (!mvd_autorecord || mvd_autorecord->value <= 0)
		return;

	if (mvd_requested)
		TDM_MvdEndMatch (NULL);

	enabled = gi.cvar ("sv_mvd_enable", NULL, 0);
	capability = gi.cvar ("sv_mvd_match_recording", NULL, 0);
	if (!enabled || enabled->value <= 0 || !capability)
	{
		gi.dprintf ("OpenTDM: automatic match MVD unavailable: needs the paired q2proded "
			"and sv_mvd_enable set before the map loads.\n");
		return;
	}

	side_label (side[0], sizeof(side[0]), TEAM_A);
	side_label (side[1], sizeof(side[1]), TEAM_B);
	filename_part (map, sizeof(map), level.mapname, true);

	now = time (NULL);
	tm = localtime (&now);
	if (!tm)
		return;

	Com_sprintf (mvd_stem, sizeof(mvd_stem), "%04d-%02d-%02d_%02d-%02d-%02d_%s_%s-vs-%s",
		tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec,
		map, side[0], side[1]);
	mvd_match_serial = ++mvd_serial;

	Com_sprintf (filename, sizeof(filename), "%s_%03u", mvd_stem, mvd_match_serial);
	Com_sprintf (command, sizeof(command), "mvdmatchrecord \"%s\"\n", filename);
	gi.AddCommandString (command);
	mvd_requested = true;
}

void TDM_MvdCheck (void)
{
	// turning the setting off during a match closes its file
	if (mvd_requested && (!mvd_autorecord || mvd_autorecord->value <= 0))
		TDM_MvdEndMatch (NULL);
}
