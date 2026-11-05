/*
===========================================================================
JK_Shinobi - procedural Orochimaru maze (runtime assembly of prefab blocks + its NPCs).

A maze map (worldspawn "shinobi_maze" "1", built by tools/maze/build.sh) is an empty sealed box. At load
Shinobi_MazeSpawn picks one of the layouts the generator wrote (maps/mz/layouts/NNN.txt, tools/maze/layout.py)
and spawns from it:
  b  misc_bsp instances of the blocks (maps/mz/<block>.bsp, each with its own baked light)
  w  nav waypoints with their links (the NPC navigator is rebuilt every load: no .nav cache for mazes)
  s  player spawns, i  items, n  NPC slots (patrol / guard / ambush / boss) with their depth from the start
Once the nav paths are calculated the "director" fills shinobi_maze_npcs slots with Sound ninjas (deeper
slots are likelier, the boss slot always gets the keeper) and Shinobi_MazeFrame drives them on top of the
stock reborn AI:
  patrol   wanders the waypoint graph (BS_WANDER), shorter look-around stops than stock
  guard    stands at its post (stock idle)
  ambush   clings to the ceiling (stock JSF_AMBUSH) and drops on a player passing below
  boss     guard, tougher NPC
  spotting a player: in view (140 deg) with a clear line within 1400, or within 260 any way, or within 380 of
           a running / attacking one -> attack (BS_DEFAULT: the stock Jedi combat AI, it chases along the nav
           graph) and alert the others within 900 that see it (or within 400)
  losing him for 8 s -> search: wander from where it is for 15 s, then back to patrolling
shinobi_maze_layout (-1 random), shinobi_maze_npcs, shinobi_maze_debug (state log to the console).
===========================================================================
*/
#include "g_local.h"
#include "b_local.h"
#include "w_saber.h"

extern void AddSpawnField( char *field, char *value );
extern void G_SpawnGEntityFromSpawnVars( qboolean inSubBSP );
extern gentity_t *NPC_Spawn_Do( gentity_t *ent );
extern void Jedi_Ambush( gentity_t *self );
extern qboolean Jedi_WaitingAmbush( gentity_t *self );

#define MZ_MAX_SLOTS	192
#define MZ_MAX_NPCS		48
#define MZ_MAX_EDGES	1024
#define MZ_BUF			65536

enum { MZR_PATROL, MZR_GUARD, MZR_AMBUSH, MZR_BOSS };
static const char *mzRoleNames[] = { "patrol", "guard", "ambush", "boss" };
enum { MZS_IDLE, MZS_FIGHT, MZS_SEARCH };
static const char *mzStateNames[] = { "idle", "fight", "search" };

typedef struct { int role; vec3_t org; float yaw; int depth; } mzSlot_t;
typedef struct {
	int		ent, role, state;
	qboolean	ready;			// NPC_Begin has run (health set): the maze AI takes over
	int		lastSeen, searchUntil, nextThink, spawnTime;
	vec3_t	home, lastPos;
	float	moved;					// distance walked since the last summary (shinobi_maze_debug)
} mzNpc_t;

static struct {
	qboolean	active, npcsSpawned, navReady;
	int			layout, blocks, caps, waypoints, spawns, items;
	mzSlot_t	slots[MZ_MAX_SLOTS]; int numSlots;
	mzNpc_t		npcs[MZ_MAX_NPCS]; int numNpcs;
	vec3_t		wp[512]; int numWp;
	int			edges[MZ_MAX_EDGES][2]; int numEdges;
	int			nextSummary;
} mz;

static char mzBuf[MZ_BUF];

qboolean Shinobi_MazeMapName( const char *mapname ) {
	return ( Q_stristr( mapname, "_maze" ) != NULL ) ? qtrue : qfalse;
}

qboolean Shinobi_MazeActive( void ) {
	return mz.active;
}

static void MZ_Spawn( const char **kv ) {
	char key[64], val[256];
	level.numSpawnVars = 0;
	level.numSpawnVarChars = 0;
	for ( ; kv[0]; kv += 2 ) {
		Q_strncpyz( key, kv[0], sizeof( key ) );
		Q_strncpyz( val, kv[1], sizeof( val ) );
		AddSpawnField( key, val );
	}
	G_SpawnGEntityFromSpawnVars( qfalse );
}

static int MZ_ReadFile( const char *path, char *buf, int size ) {
	fileHandle_t f;
	int len = trap->FS_Open( path, &f, FS_READ );
	if ( !f )
		return -1;
	if ( len >= size )
		len = size - 1;
	trap->FS_Read( buf, len, f );
	trap->FS_Close( f );
	buf[len] = 0;
	return len;
}

static int MZ_Role( const char *s ) {
	int i;
	for ( i = 0; i < (int)ARRAY_LEN( mzRoleNames ); i++ )
		if ( !Q_stricmp( s, mzRoleNames[i] ) )
			return i;
	return MZR_GUARD;
}

/* called at the end of the main map's entity spawning (G_SpawnEntitiesFromString) */
void Shinobi_MazeSpawn( void ) {
	char path[MAX_QPATH], *line, *next;
	int count, len, idx;

	memset( &mz, 0, sizeof( mz ) );
	if ( !Shinobi_MazeMapName( level.rawmapname ) )
		return;
	if ( MZ_ReadFile( "maps/mz/layouts/count.txt", mzBuf, MZ_BUF ) <= 0 || ( count = atoi( mzBuf ) ) <= 0 ) {
		Com_Printf( S_COLOR_RED "[maze] no layouts (maps/mz/layouts/count.txt)\n" );
		return;
	}
	idx = shinobi_maze_layout.integer >= 0 ? shinobi_maze_layout.integer % count : Q_irand( 0, count - 1 );
	Com_sprintf( path, sizeof( path ), "maps/mz/layouts/%03d.txt", idx );
	len = MZ_ReadFile( path, mzBuf, MZ_BUF );
	if ( len <= 0 ) {
		Com_Printf( S_COLOR_RED "[maze] can't read %s\n", path );
		return;
	}
	mz.active = qtrue;
	mz.layout = idx;

	for ( line = mzBuf; line && *line; line = next ) {
		char a[64], b[64], c[64], d[64], e[64], f[64];
		int n;
		next = strchr( line, '\n' );
		if ( next )
			*next++ = 0;
		if ( line[0] == 'b' && line[1] == ' ' ) {			// block: b name x y yaw depth
			char model[MAX_QPATH], org[64], ang[16];
			if ( sscanf( line + 2, "%63s %63s %63s %63s", a, b, c, d ) < 4 )
				continue;
			Com_sprintf( model, sizeof( model ), "mz/%s", a );
			Com_sprintf( org, sizeof( org ), "%s %s 0", b, c );
			Q_strncpyz( ang, d, sizeof( ang ) );
			{
				const char *kv[] = { "classname", "misc_bsp", "bspmodel", model, "origin", org, "angle", ang, NULL };
				MZ_Spawn( kv );
			}
			if ( !Q_stricmp( a, "cap" ) ) mz.caps++; else mz.blocks++;
		}
		else if ( line[0] == 'w' && line[1] == ' ' ) {		// waypoint: w id x y z links...
			char tn[32], org[64], t[4][32];
			int links[4] = { -1, -1, -1, -1 }, id, i;
			const char *kv[16]; int nkv = 0;
			n = sscanf( line + 2, "%d %63s %63s %63s %d %d %d %d", &id, a, b, c, &links[0], &links[1], &links[2], &links[3] );
			if ( n < 4 )
				continue;
			Com_sprintf( tn, sizeof( tn ), "mzw%d", id );
			Com_sprintf( org, sizeof( org ), "%s %s %s", a, b, c );
			kv[nkv++] = "classname"; kv[nkv++] = "waypoint";
			kv[nkv++] = "origin"; kv[nkv++] = org;
			kv[nkv++] = "targetname"; kv[nkv++] = tn;
			for ( i = 0; i < n - 4 && i < 4; i++ ) {
				static const char *keys[] = { "target", "target2", "target3", "target4" };
				Com_sprintf( t[i], sizeof( t[i] ), "mzw%d", links[i] );
				kv[nkv++] = keys[i]; kv[nkv++] = t[i];
				if ( mz.numEdges < MZ_MAX_EDGES ) {
					mz.edges[mz.numEdges][0] = id; mz.edges[mz.numEdges][1] = links[i]; mz.numEdges++;
				}
			}
			kv[nkv] = NULL;
			if ( id >= 0 && id < (int)ARRAY_LEN( mz.wp ) ) {
				VectorSet( mz.wp[id], atof( a ), atof( b ), atof( c ) );
				if ( id + 1 > mz.numWp ) mz.numWp = id + 1;
			}
			MZ_Spawn( kv );
			mz.waypoints++;
		}
		else if ( line[0] == 's' && line[1] == ' ' ) {		// player spawn: s x y z yaw
			char org[64];
			if ( sscanf( line + 2, "%63s %63s %63s %63s", a, b, c, d ) < 4 )
				continue;
			Com_sprintf( org, sizeof( org ), "%s %s %s", a, b, c );
			{
				const char *kv[] = { "classname", "info_player_deathmatch", "origin", org, "angle", d, NULL };
				MZ_Spawn( kv );
			}
			mz.spawns++;
		}
		else if ( line[0] == 'i' && line[1] == ' ' ) {		// item: i classname x y z
			char org[64];
			if ( sscanf( line + 2, "%63s %63s %63s %63s", a, b, c, d ) < 4 )
				continue;
			Com_sprintf( org, sizeof( org ), "%s %s %s", b, c, d );
			{
				const char *kv[] = { "classname", a, "origin", org, NULL };
				MZ_Spawn( kv );
			}
			mz.items++;
		}
		else if ( line[0] == 'n' && line[1] == ' ' ) {		// NPC slot: n role x y z yaw depth
			if ( sscanf( line + 2, "%63s %63s %63s %63s %63s %63s", a, b, c, d, e, f ) < 6 || mz.numSlots >= MZ_MAX_SLOTS )
				continue;
			mz.slots[mz.numSlots].role = MZ_Role( a );
			VectorSet( mz.slots[mz.numSlots].org, atof( b ), atof( c ), atof( d ) );
			mz.slots[mz.numSlots].yaw = atof( e );
			mz.slots[mz.numSlots].depth = atoi( f );
			mz.numSlots++;
		}
	}
	Com_Printf( "[maze] layout %d: %d blocks, %d caps, %d waypoints, %d edges, %d spawns, %d items, %d NPC slots\n",
		mz.layout, mz.blocks, mz.caps, mz.waypoints, mz.numEdges, mz.spawns, mz.items, mz.numSlots );
}

/* self-check: every nav edge should be walkable (a player-sized box swept a step above the floor) */
static void MZ_CheckEdges( void ) {
	int i, blocked = 0;
	vec3_t mins = { -15, -15, -6 }, maxs = { 15, 15, 40 };
	trace_t tr;
	for ( i = 0; i < mz.numEdges; i++ ) {
		vec3_t a, b;
		VectorCopy( mz.wp[mz.edges[i][0]], a ); VectorCopy( mz.wp[mz.edges[i][1]], b );
		a[2] += 8; b[2] += 8;
		trap->Trace( &tr, a, mins, maxs, b, ENTITYNUM_NONE, MASK_PLAYERSOLID, qfalse, 0, 0 );
		if ( tr.fraction < 1.0f || tr.startsolid ) {
			blocked++;
			if ( blocked <= 8 )
				Com_Printf( S_COLOR_YELLOW "[maze] edge %d-%d blocked at %s (%s)\n", mz.edges[i][0], mz.edges[i][1],
					vtos( tr.endpos ), tr.startsolid ? "start solid" : "hit" );
		}
	}
	Com_Printf( "[maze] nav check: %d edges, %d blocked\n", mz.numEdges, blocked );
}

/* g_main.c: the nav paths were calculated (the NPCs can be spawned now) */
void Shinobi_MazeNavReady( void ) {
	if ( !mz.active )
		return;
	mz.navReady = qtrue;
	MZ_CheckEdges();
}

static gentity_t *MZ_SpawnNPC( mzSlot_t *s ) {
	gentity_t *spawner = G_Spawn(), *npc;
	if ( !spawner )
		return NULL;
	G_SetOrigin( spawner, s->org );
	VectorCopy( s->org, spawner->s.origin );
	spawner->s.angles[YAW] = s->yaw;
	spawner->NPC_type = G_NewString( s->role == MZR_BOSS ? "oto_jonin" : "oto_ninja" );
	spawner->count = 1;
	spawner->spawnflags = ( s->role == MZR_AMBUSH ) ? JSF_AMBUSH : 0;
	spawner->think = G_FreeEntity;
	spawner->nextthink = level.time + FRAMETIME;
	trap->LinkEntity( (sharedEntity_t *)spawner );
	npc = NPC_Spawn_Do( spawner );
	if ( !npc || !npc->client || !npc->NPC )
		return NULL;
	npc->client->playerTeam = npc->s.teamowner = NPCTEAM_ENEMY;
	npc->client->enemyTeam = NPCTEAM_PLAYER;
	return npc;
}

static void MZ_Idle( mzNpc_t *m, gentity_t *npc ) {
	m->state = MZS_IDLE;
	if ( m->role == MZR_PATROL ) {
		npc->NPC->behaviorState = BS_WANDER;
		npc->NPC->investigateDebounceTime = level.time;		// "done waiting": pick the next waypoint now
	}
	else {
		npc->NPC->behaviorState = BS_DEFAULT;
	}
}

static void MZ_Director( void ) {
	int want = shinobi_maze_npcs.integer, i, tries;
	qboolean used[MZ_MAX_SLOTS] = { 0 };
	int byRole[4] = { 0 };

	if ( want > MZ_MAX_NPCS ) want = MZ_MAX_NPCS;
	// the keeper first
	for ( i = 0; i < mz.numSlots; i++ ) {
		if ( mz.slots[i].role == MZR_BOSS && mz.numNpcs < MZ_MAX_NPCS ) {
			gentity_t *npc = MZ_SpawnNPC( &mz.slots[i] );
			used[i] = qtrue;
			if ( npc ) {
				mzNpc_t *m = &mz.npcs[mz.numNpcs++];
				memset( m, 0, sizeof( *m ) );
				m->ent = npc->s.number; m->role = MZR_BOSS; m->spawnTime = level.time;
				VectorCopy( npc->r.currentOrigin, m->home ); VectorCopy( npc->r.currentOrigin, m->lastPos );
				byRole[MZR_BOSS]++;
			}
		}
	}
	// the rest: weighted by depth (nobody in the start room or right next to it), at most 3 ambushers
	for ( tries = 0; mz.numNpcs < want && tries < 400; tries++ ) {
		int total = 0, r, pick = -1;
		for ( i = 0; i < mz.numSlots; i++ )
			if ( !used[i] && mz.slots[i].depth >= 2 && !( mz.slots[i].role == MZR_AMBUSH && byRole[MZR_AMBUSH] >= 3 ) )
				total += 1 + mz.slots[i].depth;
		if ( !total )
			break;
		r = Q_irand( 0, total - 1 );
		for ( i = 0; i < mz.numSlots; i++ ) {
			if ( used[i] || mz.slots[i].depth < 2 || ( mz.slots[i].role == MZR_AMBUSH && byRole[MZR_AMBUSH] >= 3 ) )
				continue;
			r -= 1 + mz.slots[i].depth;
			if ( r < 0 ) { pick = i; break; }
		}
		if ( pick < 0 )
			break;
		used[pick] = qtrue;
		{
			gentity_t *npc = MZ_SpawnNPC( &mz.slots[pick] );
			if ( npc ) {
				mzNpc_t *m = &mz.npcs[mz.numNpcs++];
				memset( m, 0, sizeof( *m ) );
				m->ent = npc->s.number; m->role = mz.slots[pick].role; m->spawnTime = level.time;
				VectorCopy( npc->r.currentOrigin, m->home ); VectorCopy( npc->r.currentOrigin, m->lastPos );
				byRole[m->role]++;
			}
		}
	}
	Com_Printf( "[maze] %d Sound ninjas: %d patrol, %d guard, %d ambush, %d boss\n", mz.numNpcs,
		byRole[MZR_PATROL], byRole[MZR_GUARD], byRole[MZR_AMBUSH], byRole[MZR_BOSS] );
}

static qboolean MZ_ValidTarget( gentity_t *p ) {
	return p && p->inuse && p->client && p->client->pers.connected == CON_CONNECTED
		&& p->client->sess.sessionTeam != TEAM_SPECTATOR && p->health > 0 && !( p->flags & FL_NOTARGET )
		&& !( p->client->ps.pm_flags & PMF_FOLLOW );
}

/* can npc notice player p right now? */
static qboolean MZ_Notices( mzNpc_t *m, gentity_t *npc, gentity_t *p ) {
	vec3_t d, ang;
	float dist, dyaw;
	VectorSubtract( p->r.currentOrigin, npc->r.currentOrigin, d );
	dist = VectorLength( d );
	if ( dist > 1400 )
		return qfalse;
	if ( m->role == MZR_AMBUSH && Jedi_WaitingAmbush( npc ) ) {		// hanging: only someone right below
		return ( d[2] < 0 && sqrt( d[0] * d[0] + d[1] * d[1] ) < 300 && G_ClearLOS4( npc, p ) ) ? qtrue : qfalse;
	}
	if ( dist < 260 )
		return qtrue;
	if ( dist < 380 && ( VectorLength( p->client->ps.velocity ) > 200 || p->client->ps.weaponTime > 0 ) )
		return qtrue;			// heard him: running or fighting close by, even round a corner
	vectoangles( d, ang );
	dyaw = fabs( AngleDelta( ang[YAW], npc->client->ps.viewangles[YAW] ) );
	if ( dyaw > 70 )
		return qfalse;
	return G_ClearLOS4( npc, p );
}

static void MZ_Attack( mzNpc_t *m, gentity_t *npc, gentity_t *p, const char *why ) {
	if ( m->role == MZR_AMBUSH && Jedi_WaitingAmbush( npc ) )
		Jedi_Ambush( npc );
	G_SetEnemy( npc, p );
	npc->NPC->behaviorState = BS_DEFAULT;
	m->state = MZS_FIGHT;
	m->lastSeen = level.time;
	if ( shinobi_maze_debug.integer )
		Com_Printf( "[maze] npc %d (%s) %s %s at %.0f\n", m->ent, mzRoleNames[m->role], why, p->client->pers.netname,
			Distance( p->r.currentOrigin, npc->r.currentOrigin ) );
}

void Shinobi_MazeFrame( void ) {
	int i, j;
	if ( !mz.active )
		return;
	if ( !mz.npcsSpawned ) {
		if ( mz.navReady && level.time > 3000 ) {
			mz.npcsSpawned = qtrue;
			MZ_Director();
		}
		return;
	}
	for ( i = 0; i < mz.numNpcs; i++ ) {
		mzNpc_t *m = &mz.npcs[i];
		gentity_t *npc = &g_entities[m->ent];
		if ( m->ent < 0 )
			continue;
		if ( !m->ready ) {		// a freshly spawned NPC has no health until its NPC_Begin think
			if ( !npc->inuse ) { m->ent = -1; continue; }
			if ( level.time < m->spawnTime + 600 || !npc->client || !npc->NPC || npc->health <= 0 ) {
				if ( level.time > m->spawnTime + 5000 && npc->health <= 0 ) {
					Com_Printf( S_COLOR_YELLOW "[maze] npc %d (%s) never came alive\n", m->ent, mzRoleNames[m->role] );
					m->ent = -1;
				}
				continue;
			}
			m->ready = qtrue;
			npc->client->playerTeam = npc->s.teamowner = NPCTEAM_ENEMY;
			npc->client->enemyTeam = NPCTEAM_PLAYER;
			VectorCopy( npc->r.currentOrigin, m->home ); VectorCopy( npc->r.currentOrigin, m->lastPos );
			MZ_Idle( m, npc );
			if ( shinobi_maze_debug.integer )
				Com_Printf( "[maze] npc %d (%s) ready at %s, %d hp\n", m->ent, mzRoleNames[m->role], vtos( npc->r.currentOrigin ), npc->health );
		}
		if ( !npc->inuse || !npc->client || !npc->NPC || npc->health <= 0 ) {
			if ( shinobi_maze_debug.integer )
				Com_Printf( "[maze] npc %d (%s) down\n", m->ent, mzRoleNames[m->role] );
			m->ent = -1;
			continue;
		}
		m->moved += Distance( npc->r.currentOrigin, m->lastPos );
		VectorCopy( npc->r.currentOrigin, m->lastPos );
		if ( m->nextThink > level.time )
			continue;
		m->nextThink = level.time + 200;

		if ( npc->enemy ) {
			if ( !MZ_ValidTarget( npc->enemy ) ) {
				G_ClearEnemy( npc );
			}
			else {
				if ( m->state != MZS_FIGHT ) { m->state = MZS_FIGHT; m->lastSeen = level.time; }
				npc->NPC->behaviorState = BS_DEFAULT;
				if ( G_ClearLOS4( npc, npc->enemy ) )
					m->lastSeen = level.time;
				else if ( level.time - m->lastSeen > 8000 ) {	// lost him: search around here
					if ( shinobi_maze_debug.integer )
						Com_Printf( "[maze] npc %d (%s) lost %s, searching\n", m->ent, mzRoleNames[m->role], npc->enemy->client->pers.netname );
					G_ClearEnemy( npc );
					m->state = MZS_SEARCH;
					m->searchUntil = level.time + 15000;
					npc->NPC->behaviorState = BS_WANDER;
					npc->NPC->investigateDebounceTime = level.time;
				}
				continue;
			}
		}
		if ( m->state == MZS_FIGHT )
			MZ_Idle( m, npc );
		if ( m->state == MZS_SEARCH && level.time > m->searchUntil ) {
			m->role = ( m->role == MZR_AMBUSH ) ? MZR_PATROL : m->role;		// a dropped ambusher can't hang again
			MZ_Idle( m, npc );
			if ( m->role != MZR_PATROL ) {			// guards walk back to their post
				npc->NPC->behaviorState = BS_WANDER;
				npc->NPC->investigateDebounceTime = level.time;
			}
		}
		// patrol pace: stock wander waits 3-10 s at every waypoint
		if ( npc->NPC->behaviorState == BS_WANDER && npc->NPC->investigateDebounceTime > level.time + 2500 )
			npc->NPC->investigateDebounceTime = level.time + Q_irand( 600, 2500 );
		// look for players
		for ( j = 0; j < MAX_CLIENTS; j++ ) {
			gentity_t *p = &g_entities[j];
			if ( !MZ_ValidTarget( p ) || !MZ_Notices( m, npc, p ) )
				continue;
			MZ_Attack( m, npc, p, "spotted" );
			{	// shout: the others that see this one (or are close) join in
				int k;
				for ( k = 0; k < mz.numNpcs; k++ ) {
					mzNpc_t *o = &mz.npcs[k];
					gentity_t *on = &g_entities[o->ent > 0 ? o->ent : 0];
					float dd;
					if ( o == m || o->ent < 0 || !on->inuse || !on->NPC || on->enemy || on->health <= 0 )
						continue;
					dd = Distance( on->r.currentOrigin, npc->r.currentOrigin );
					if ( dd < 400 || ( dd < 900 && G_ClearLOS4( on, npc ) ) )
						MZ_Attack( o, on, p, "alerted, attacks" );
				}
			}
			break;
		}
	}
	if ( shinobi_maze_debug.integer && level.time > mz.nextSummary ) {
		int st[3] = { 0 }, alive = 0; float walked = 0;
		mz.nextSummary = level.time + 5000;
		for ( i = 0; i < mz.numNpcs; i++ ) {
			if ( mz.npcs[i].ent < 0 ) continue;
			alive++; st[mz.npcs[i].state]++; walked += mz.npcs[i].moved; mz.npcs[i].moved = 0;
		}
		Com_Printf( "[maze] t %d: %d alive (%d idle, %d fight, %d search), walked %.0f u in 5 s\n",
			level.time / 1000, alive, st[0], st[1], st[2], walked );
	}
	(void)mzStateNames;
}

/* cheat "maze_goto [role|n]": put the player ~220 u from a living maze NPC (the n-th, or the first of that
   role), on clear floor, facing it - for the auto-test and for looking at the AI */
void Cmd_MazeGoto_f( gentity_t *ent ) {
	char arg[32];
	int i, n = 0, want = 0, role = -1, d;
	vec3_t mins = { -15, -15, -24 }, maxs = { 15, 15, 40 };
	if ( !mz.active ) {
		trap->SendServerCommand( ent - g_entities, "print \"not a maze map\n\"" );
		return;
	}
	trap->Argv( 1, arg, sizeof( arg ) );
	if ( arg[0] >= '0' && arg[0] <= '9' ) want = atoi( arg );
	else if ( arg[0] ) role = MZ_Role( arg );
	for ( i = 0; i < mz.numNpcs; i++ ) {
		mzNpc_t *m = &mz.npcs[i];
		gentity_t *npc = m->ent >= 0 ? &g_entities[m->ent] : NULL;
		if ( !npc || !m->ready || !npc->inuse || npc->health <= 0 || ( role >= 0 && m->role != role ) )
			continue;
		if ( n++ < want )
			continue;
		for ( d = 0; d < 8; d++ ) {
			vec3_t spot, down, ang, dir;
			trace_t tr;
			float a = DEG2RAD( d * 45 );
			VectorSet( spot, npc->r.currentOrigin[0] + cos( a ) * 220, npc->r.currentOrigin[1] + sin( a ) * 220, npc->r.currentOrigin[2] + 8 );
			trap->Trace( &tr, npc->r.currentOrigin, mins, maxs, spot, npc->s.number, MASK_PLAYERSOLID, qfalse, 0, 0 );
			if ( tr.fraction < 1.0f || tr.startsolid )
				continue;
			VectorCopy( spot, down ); down[2] -= 128;
			trap->Trace( &tr, spot, mins, maxs, down, ENTITYNUM_NONE, MASK_PLAYERSOLID, qfalse, 0, 0 );
			if ( tr.fraction >= 1.0f || tr.startsolid )
				continue;
			VectorSubtract( npc->r.currentOrigin, spot, dir );
			vectoangles( dir, ang ); ang[PITCH] = 0; ang[ROLL] = 0;
			TeleportPlayer( ent, tr.endpos, ang );
			Com_Printf( "[maze] goto npc %d (%s, %s) from %s\n", m->ent, mzRoleNames[m->role], mzStateNames[m->state], vtos( tr.endpos ) );
			return;
		}
	}
	trap->SendServerCommand( ent - g_entities, "print \"no such maze NPC with room around it\n\"" );
}
