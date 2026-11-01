/*
===========================================================================
JK_Shinobi - Kage Bunshin no Jutsu (shadow clones).
===========================================================================
*/
#include "g_shinobi_local.h"

/*
===========================================================================
Kage Bunshin no Jutsu (shadow clones).

Clones are NPCs with the Jedi AI, the owner's model and saber, 1 health.
They follow the owner, pick the nearest hostile themselves (see
Shinobi_CloneIsHostile, also used by NPC_ValidEnemy), never hurt the owner,
and give the owner credit for their kills. They vanish in smoke when hit,
when the owner dies or when the jutsu runs out.
===========================================================================
*/
char g_shinobiCloneModel[MAX_QPATH];
char g_shinobiCloneSkin[MAX_QPATH];

extern gentity_t *NPC_Spawn_Do( gentity_t *ent );
extern void NPC_Think( gentity_t *self );
extern void G_SetEnemy( gentity_t *self, gentity_t *enemy );
extern void G_ClearEnemy( gentity_t *self );
extern qboolean WP_SaberParseParms( const char *saberName, saberInfo_t *saber );

static gentity_t *Shinobi_CloneOwner( gentity_t *ent ) {
	gentity_t *owner;

	if ( !ent || !ent->shinobiCloneOwner )
		return NULL;
	owner = &g_entities[ent->shinobiCloneOwner - 1];
	if ( !owner->inuse || !owner->client )
		return NULL;
	return owner;
}

// the player an entity "belongs" to: the owner for clones, itself otherwise
static gentity_t *Shinobi_Root( gentity_t *ent ) {
	if ( ent && ent->shinobiCloneOwner )
		return &g_entities[ent->shinobiCloneOwner - 1];
	return ent;
}

qboolean Shinobi_CloneIsHostile( gentity_t *clone, gentity_t *other ) {
	gentity_t *owner = &g_entities[clone->shinobiCloneOwner - 1];
	gentity_t *root;

	if ( !other || other == clone || !other->inuse || !other->client || other->health <= 0 )
		return qfalse;
	if ( other->flags & FL_NOTARGET )
		return qfalse;
	if ( other->s.eType != ET_NPC ) {
		if ( other->client->sess.sessionTeam == TEAM_SPECTATOR || other->client->tempSpectate >= level.time )
			return qfalse;
	}

	root = Shinobi_Root( other );
	if ( root == owner )
		return qfalse; // the owner and the owner's other clones

	if ( level.gametype >= GT_TEAM && root->client && owner->client
		&& root->client->sess.sessionTeam == owner->client->sess.sessionTeam ) {
		return qfalse;
	}
	return qtrue;
}

qboolean Shinobi_DamageBlocked( gentity_t *targ, gentity_t *attacker ) {
	if ( !targ || !attacker || targ == attacker )
		return qfalse;
	if ( !targ->shinobiCloneOwner && !attacker->shinobiCloneOwner )
		return qfalse;
	return ( Shinobi_Root( targ ) == Shinobi_Root( attacker ) ) ? qtrue : qfalse;
}

gentity_t *Shinobi_CreditAttacker( gentity_t *attacker ) {
	gentity_t *owner = Shinobi_CloneOwner( attacker );
	return owner ? owner : attacker;
}

static void Shinobi_ClonePoof( gentity_t *clone ) {
	vec3_t up = { 0, 0, 1 };

	G_PlayEffectID( shinobiMedia.fxClonePoof, clone->r.currentOrigin, up );
	G_SoundAtLoc( clone->r.currentOrigin, CHAN_AUTO, shinobiMedia.sndPoof ); // not tied to the entity we free next
	clone->shinobiCloneOwner = 0;
	G_FreeEntity( clone );
}

static void Shinobi_RemoveClones( int ownerNum ) {
	int i;

	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *e = &g_entities[i];
		if ( e->inuse && e->shinobiCloneOwner == ownerNum + 1 )
			Shinobi_ClonePoof( e );
	}
}

// split the owner's "model" userinfo ("kyle/default", "jedi_hm/|head_a1|torso_a1|lower_a1")
static void Shinobi_GetOwnerModel( gentity_t *owner, char *model, char *skin ) {
	char  userinfo[MAX_INFO_STRING];
	char *slash;

	trap->GetUserinfo( owner->s.number, userinfo, sizeof( userinfo ) );
	Q_strncpyz( model, Info_ValueForKey( userinfo, "model" ), MAX_QPATH );
	if ( !model[0] )
		Q_strncpyz( model, DEFAULT_MODEL, MAX_QPATH );

	slash = strchr( model, '/' );
	if ( slash && slash[1] ) {
		Q_strncpyz( skin, slash + 1, MAX_QPATH );
		*slash = 0;
	}
	else {
		if ( slash )
			*slash = 0;
		Q_strncpyz( skin, "default", MAX_QPATH );
	}
}

// Try to put a clone next to the owner in direction 'yawOffset' (degrees from
// the view direction). In tight spaces it settles for a shorter distance, as
// long as it does not overlap the owner or a clone placed earlier.
#define CLONE_SPOT_DIST     64
#define CLONE_SPOT_MIN_DIST 36   // two 30-unit-wide boxes must not touch
static qboolean Shinobi_CloneSpot( gentity_t *owner, float yawOffset, vec3_t taken[], int numTaken, vec3_t out ) {
	vec3_t  angles, dir, start, end;
	trace_t tr;
	float   dist;
	int     i;

	VectorSet( angles, 0, owner->client->ps.viewangles[YAW] + yawOffset, 0 );
	AngleVectors( angles, dir, NULL, NULL );

	VectorCopy( owner->client->ps.origin, start );
	start[2] += 1;
	VectorMA( start, CLONE_SPOT_DIST, dir, end );

	trap->Trace( &tr, start, owner->r.mins, owner->r.maxs, end, owner->s.number, MASK_PLAYERSOLID, qfalse, 0, 0 );
	if ( tr.startsolid || tr.allsolid )
		return qfalse;
	dist = tr.fraction * CLONE_SPOT_DIST;
	if ( dist < CLONE_SPOT_MIN_DIST )
		return qfalse;

	for ( i = 0; i < numTaken; i++ ) {
		if ( Distance( tr.endpos, taken[i] ) < CLONE_SPOT_MIN_DIST )
			return qfalse;
	}
	VectorCopy( tr.endpos, out );
	return qtrue;
}

static void Shinobi_CloneApplyTeam( gentity_t *clone, gentity_t *owner ) {
	if ( level.gametype >= GT_TEAM ) {
		clone->client->playerTeam = clone->s.teamowner = owner->client->playerTeam;
		clone->client->enemyTeam = ( owner->client->playerTeam == NPCTEAM_ENEMY ) ? NPCTEAM_PLAYER : NPCTEAM_ENEMY;
	}
	else {
		clone->client->playerTeam = clone->s.teamowner = NPCTEAM_ENEMY;
		clone->client->enemyTeam = NPCTEAM_PLAYER;
	}
}

static gentity_t *Shinobi_SpawnClone( gentity_t *owner, vec3_t origin ) {
	gentity_t *spawner, *clone;
	vec3_t     up = { 0, 0, 1 };

	spawner = G_Spawn();
	if ( !spawner )
		return NULL;

	G_SetOrigin( spawner, origin );
	VectorCopy( origin, spawner->s.origin );
	spawner->s.angles[YAW] = owner->client->ps.viewangles[YAW];
	spawner->NPC_type = G_NewString( SHINOBI_CLONE_NPC_TYPE );
	spawner->count = 1;
	spawner->health = SHINOBI_CLONE_HEALTH;
	spawner->think = G_FreeEntity;
	spawner->nextthink = level.time + FRAMETIME;
	trap->LinkEntity( (sharedEntity_t *)spawner );

	Shinobi_GetOwnerModel( owner, g_shinobiCloneModel, g_shinobiCloneSkin );
	clone = NPC_Spawn_Do( spawner );
	g_shinobiCloneModel[0] = 0;
	g_shinobiCloneSkin[0] = 0;

	if ( !clone )
		return NULL;

	clone->shinobiCloneOwner = owner->s.number + 1;
	clone->shinobiCloneExpire = level.time + SHINOBI_CLONE_LIFETIME;
	clone->shinobiCloneThink = 0;
	clone->shinobiCloneReady = qfalse;
	Shinobi_CloneApplyTeam( clone, owner );
	clone->client->leader = owner;

	// carry the owner's saber (single blade or staff)
	if ( owner->client->saber[0].name[0] && WP_SaberParseParms( owner->client->saber[0].name, &clone->client->saber[0] ) ) {
		int b;
		for ( b = 0; b < MAX_BLADES; b++ )
			clone->client->saber[0].blade[b].color = owner->client->saber[0].blade[b].color;
		clone->s.npcSaber1 = G_ModelIndex( va( "@%s", owner->client->saber[0].name ) );
	}

	G_PlayEffectID( shinobiMedia.fxClonePoof, origin, up );
	return clone;
}

void Cmd_KageBunshin_f( gentity_t *ent ) {
	gclient_t *client = ent->client;
	// right, left, then behind and the diagonals: corridors still work
	static const float yaws[] = { -90, 90, 180, -135, 135, -45, 45, 0 };
	vec3_t spots[SHINOBI_CLONE_COUNT];
	int i, spawned = 0;

	if ( !Shinobi_CanAct( ent ) )
		return;
	if ( client->ps.fd.forcePower < SHINOBI_CLONE_COST ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		trap->SendServerCommand( ent - g_entities, va( "cp \"Not enough chakra (%i/%i)\"", client->ps.fd.forcePower, SHINOBI_CLONE_COST ) );
		return;
	}

	Shinobi_RemoveClones( ent->s.number ); // recast replaces the old ones

	for ( i = 0; i < (int)ARRAY_LEN( yaws ) && spawned < SHINOBI_CLONE_COUNT; i++ ) {
		if ( Shinobi_CloneSpot( ent, yaws[i], spots, spawned, spots[spawned] ) && Shinobi_SpawnClone( ent, spots[spawned] ) )
			spawned++;
	}

	if ( !spawned ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		trap->SendServerCommand( ent - g_entities, "cp \"No room for clones\"" );
		return;
	}

	client->ps.fd.forcePower -= SHINOBI_CLONE_COST;
	Shinobi_StopCharging( client );
	G_Sound( ent, CHAN_AUTO, shinobiMedia.sndPoof );
}

static gentity_t *Shinobi_CloneFindEnemy( gentity_t *clone ) {
	gentity_t *best = NULL;
	float      bestDist = (float)SHINOBI_CLONE_SIGHT * SHINOBI_CLONE_SIGHT;
	vec3_t     eye;
	int        i;

	VectorCopy( clone->r.currentOrigin, eye );
	eye[2] += clone->client->ps.viewheight;

	for ( i = 0; i < level.num_entities; i++ ) {
		gentity_t *e = &g_entities[i];
		vec3_t     target;
		trace_t    tr;
		float      d;

		if ( !Shinobi_CloneIsHostile( clone, e ) )
			continue;
		d = DistanceSquared( clone->r.currentOrigin, e->r.currentOrigin );
		if ( d >= bestDist )
			continue;
		if ( !trap->InPVS( eye, e->r.currentOrigin ) )
			continue;

		VectorCopy( e->r.currentOrigin, target );
		target[2] += 16;
		trap->Trace( &tr, eye, NULL, NULL, target, clone->s.number, MASK_SOLID, qfalse, 0, 0 );
		if ( tr.fraction < 1.0f && tr.entityNum != e->s.number )
			continue;

		best = e;
		bestDist = d;
	}
	return best;
}

static void Shinobi_CloneSupervise( gentity_t *clone, gentity_t *owner ) {
	if ( !clone->shinobiCloneReady ) {
		if ( clone->think != NPC_Think )
			return; // still in NPC_Begin
		// NPC_Begin is done: apply what the .npc file and team code can't express
		clone->health = SHINOBI_CLONE_HEALTH;
		clone->client->ps.stats[STAT_HEALTH] = SHINOBI_CLONE_HEALTH;
		clone->client->ps.stats[STAT_MAX_HEALTH] = SHINOBI_CLONE_HEALTH;
		clone->client->pers.maxHealth = SHINOBI_CLONE_HEALTH;
		Shinobi_CloneApplyTeam( clone, owner );
		clone->client->leader = owner;
		clone->NPC->behaviorState = clone->NPC->defaultBehavior = BS_FOLLOW_LEADER;
		clone->shinobiCloneReady = qtrue;
	}

	clone->client->leader = owner;

	if ( clone->enemy && !Shinobi_CloneIsHostile( clone, clone->enemy ) ) {
		G_ClearEnemy( clone );
	}
	if ( !clone->enemy ) {
		gentity_t *enemy = Shinobi_CloneFindEnemy( clone );
		if ( enemy ) {
			G_SetEnemy( clone, enemy );
		}
	}
}

void Shinobi_RunFrame( void ) {
	int i;

	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *clone = &g_entities[i];
		gentity_t *owner;

		if ( !clone->inuse || !clone->shinobiCloneOwner )
			continue;

		owner = Shinobi_CloneOwner( clone );
		if ( !owner || owner->health <= 0 || !owner->client
			|| owner->client->sess.sessionTeam == TEAM_SPECTATOR
			|| !clone->client || clone->health <= 0
			|| level.time >= clone->shinobiCloneExpire ) {
			Shinobi_ClonePoof( clone );
			continue;
		}

		if ( clone->shinobiCloneThink <= level.time ) {
			clone->shinobiCloneThink = level.time + SHINOBI_CLONE_THINK_MS;
			Shinobi_CloneSupervise( clone, owner );
		}
	}
}
