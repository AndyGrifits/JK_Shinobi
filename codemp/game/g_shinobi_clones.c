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

gentity_t *Shinobi_CloneOwner( gentity_t *ent ) {
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
		if ( SHINOBI_UNSEEN( &other->client->ps ) )
			return qfalse; // can't fight what you can't see (Kawarimi)
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

	if ( clone->shinobiCloneLightning ) {
		if ( clone->health <= 0 ) { // struck down: the lightning breaks loose
			gentity_t *owner = Shinobi_CloneOwner( clone );
			int        i;
			G_PlayEffectID( shinobiMedia.fxChidoriHit[2], clone->r.currentOrigin, up );
			G_PlayEffectID( shinobiMedia.fxEmp, clone->r.currentOrigin, up );
			G_SoundAtLoc( clone->r.currentOrigin, CHAN_AUTO, shinobiMedia.sndChidoriHit );
			Shinobi_ElectrifyAt( clone->r.currentOrigin, clone->shinobiCloneOwner );
			for ( i = 0; i < level.num_entities; i++ ) {
				gentity_t *v = &g_entities[i];
				if ( v == clone || !v->inuse || !v->client || !v->takedamage || v->health <= 0 || v == owner )
					continue;
				if ( v->shinobiCloneOwner && v->shinobiCloneOwner == clone->shinobiCloneOwner )
					continue;
				if ( v->s.number < MAX_CLIENTS && v->client->sess.sessionTeam == TEAM_SPECTATOR )
					continue;
				if ( owner && level.gametype >= GT_TEAM && OnSameTeam( owner, v ) )
					continue;
				if ( Distance( v->r.currentOrigin, clone->r.currentOrigin ) > SHINOBI_RAICLONE_RADIUS )
					continue;
				shinobiHitElement = SE_LIGHTNING;
				shinobiJutsuDamage = 1;
				G_Damage( v, clone, owner ? owner : clone, up, v->r.currentOrigin, SHINOBI_RAICLONE_DAMAGE, DAMAGE_NO_KNOCKBACK, MOD_DEMP2 );
				shinobiJutsuDamage = 0;
				shinobiHitElement = SE_NONE;
				if ( v->inuse && v->health > 0 && v->client )
					Shinobi_Paralyse( v, SHINOBI_RAICLONE_STUN );
			}
		}
		else {
			G_PlayEffectID( shinobiMedia.fxChidoriHit[0], clone->r.currentOrigin, up );
		}
		clone->shinobiCloneLightning = qfalse;
		clone->shinobiCloneOwner = 0;
		G_FreeEntity( clone );
		return;
	}
	if ( clone->shinobiCloneEarth ) { // an earth clone crumbles into a heap of mud
		vec3_t floor;
		G_PlayEffectID( shinobiMedia.fxWallBreak, clone->r.currentOrigin, up );
		G_SoundAtLoc( clone->r.currentOrigin, CHAN_AUTO, shinobiMedia.sndWallBreak );
		if ( Shinobi_FloorBelow( clone->r.currentOrigin, 96, floor ) )
			Shinobi_SpawnZone( SZ_MUD, floor, 70, 1, clone->shinobiCloneOwner ); // (mud keeps its own lifetime)
		clone->shinobiCloneEarth = qfalse;
		clone->shinobiCloneOwner = 0;
		G_FreeEntity( clone );
		return;
	}
	G_PlayEffectID( shinobiMedia.fxClonePoof, clone->r.currentOrigin, up );
	G_SoundAtLoc( clone->r.currentOrigin, CHAN_AUTO, shinobiMedia.sndPoof ); // not tied to the entity we free next
	clone->shinobiCloneOwner = 0;
	G_FreeEntity( clone );
}

static void Shinobi_WaterClonePop( gentity_t *clone );

static void Shinobi_RemoveClones( int ownerNum, qboolean water ) {
	int i;

	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *e = &g_entities[i];
		if ( !e->inuse || e->shinobiCloneOwner != ownerNum + 1 || e->shinobiCloneDecoy )
			continue;
		if ( e->shinobiCloneWater ) {
			if ( water )
				Shinobi_WaterClonePop( e );
		}
		else if ( e->shinobiCloneEarth || e->shinobiCloneLightning ) {
			if ( water ) // (a new Kage Bunshin leaves the earth / lightning clone alone)
				Shinobi_ClonePoof( e );
		}
		else
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
	clone->s.owner = owner->s.number; // (the cgame draws its maker's accessories on it)
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

	return clone;
}

qboolean Shinobi_CastKageBunshin( gentity_t *ent, int lvl ) {
	gclient_t *client = ent->client;
	// right, left, then behind and the diagonals: corridors still work
	static const float yaws[] = { -90, 90, 180, -135, 135, -45, 45, 0 };
	vec3_t     spots[SHINOBI_CLONE_MAX];
	vec3_t     up = { 0, 0, 1 }, smoke;
	int        i, found = 0, swap;
	const int  wanted = lvl < 1 ? 1 : ( lvl > SHINOBI_CLONE_MAX ? SHINOBI_CLONE_MAX : lvl );

	Shinobi_RemoveClones( ent->s.number, qfalse ); // recast replaces the old ones

	for ( i = 0; i < (int)ARRAY_LEN( yaws ) && found < wanted; i++ ) {
		if ( Shinobi_CloneSpot( ent, yaws[i], spots, found, spots[found] ) )
			found++;
	}
	if ( !found ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		trap->SendServerCommand( ent - g_entities, "cp \"No room for clones\"" );
		return qfalse;
	}

	// one big smoke cloud hides the caster and every clone...
	VectorCopy( client->ps.origin, smoke );
	G_PlayEffectID( shinobiMedia.fxCloneSmoke, smoke, up );
	G_SoundAtLoc( smoke, CHAN_AUTO, shinobiMedia.sndPoof );

	// ...and the original ends up in a random one of the spots
	swap = Q_irand( 0, found ); // == found: the original stays where it is
	if ( swap < found ) {
		vec3_t newSpot;
		VectorCopy( spots[swap], newSpot );
		VectorCopy( client->ps.origin, spots[swap] );
		Shinobi_TeleportClient( ent, newSpot );
	}

	for ( i = 0; i < found; i++ ) {
		Shinobi_SpawnClone( ent, spots[i] );
	}
	client->shinobi.cloneDrainNext = level.time + SHINOBI_CLONE_DRAIN_MS;
	return qtrue;
}

/*
===========================================================================
Kawarimi double and the log it turns into (the jutsu itself: g_shinobi.c)
===========================================================================
*/
gentity_t *Shinobi_SpawnDecoy( gentity_t *owner ) {
	gentity_t *decoy = Shinobi_SpawnClone( owner, owner->client->ps.origin );

	if ( !decoy )
		return NULL;
	decoy->shinobiCloneDecoy = qtrue;
	decoy->shinobiCloneExpire = level.time + SHINOBI_KAWARIMI_DECOY_MS;
	return decoy;
}

gentity_t *Shinobi_Decoy( gentity_t *owner ) {
	gentity_t *decoy;
	int        num;

	if ( !owner->client || !owner->client->shinobi.kawarimiDecoy )
		return NULL;
	num = owner->client->shinobi.kawarimiDecoy - 1;
	decoy = &g_entities[num];
	if ( !decoy->inuse || !decoy->shinobiCloneDecoy || decoy->shinobiCloneOwner != owner->s.number + 1 )
		return NULL;
	return decoy;
}

static void Shinobi_LogThink( gentity_t *log ) {
	vec3_t up = { 0, 0, 1 };

	if ( level.time >= log->genericValue1 ) { // gone in a puff
		G_PlayEffectID( shinobiMedia.fxClonePoof, log->r.currentOrigin, up );
		G_FreeEntity( log );
		return;
	}

	if ( log->s.pos.trType == TR_GRAVITY ) {
		vec3_t  newPos, mins = { -6, -6, -6 }, maxs = { 6, 6, 6 };
		trace_t tr;

		BG_EvaluateTrajectory( &log->s.pos, level.time, newPos );
		trap->Trace( &tr, log->r.currentOrigin, mins, maxs, newPos, log->s.number, MASK_SOLID, qfalse, 0, 0 );
		if ( tr.startsolid || tr.allsolid || tr.fraction < 1.0f ) {
			// landed: lie still on the ground
			G_SetOrigin( log, ( tr.startsolid || tr.allsolid ) ? log->r.currentOrigin : tr.endpos );
			log->s.apos.trType = TR_STATIONARY;
			log->s.apos.trTime = level.time;
			VectorSet( log->s.apos.trBase, 0, log->s.apos.trBase[YAW], flrand( 0, 360 ) );
			VectorClear( log->s.apos.trDelta );
			VectorCopy( log->s.apos.trBase, log->r.currentAngles );
			log->nextthink = log->genericValue1;
		}
		else {
			VectorCopy( newPos, log->r.currentOrigin );
			log->nextthink = level.time + FRAMETIME;
		}
		trap->LinkEntity( (sharedEntity_t *)log );
		return;
	}
	log->nextthink = log->genericValue1;
}

static void Shinobi_SpawnLog( vec3_t org, float yaw ) {
	gentity_t *log = G_Spawn();

	if ( !log )
		return;
	log->classname = "shinobi_log";
	log->s.eType = ET_GENERAL;
	log->s.modelindex = shinobiMedia.logModel;
	log->r.contents = 0;
	VectorSet( log->r.mins, -26, -26, -8 );
	VectorSet( log->r.maxs, 26, 26, 8 );

	log->s.pos.trType = TR_GRAVITY;
	log->s.pos.trTime = level.time;
	VectorCopy( org, log->s.pos.trBase );
	VectorSet( log->s.pos.trDelta, flrand( -60, 60 ), flrand( -60, 60 ), 160 );
	VectorCopy( org, log->r.currentOrigin );
	VectorCopy( org, log->s.origin );

	// tumble end over end while it drops
	log->s.apos.trType = TR_LINEAR;
	log->s.apos.trTime = level.time;
	VectorSet( log->s.apos.trBase, 0, yaw + 90, 0 );
	VectorSet( log->s.apos.trDelta, 420, 0, 0 );
	VectorCopy( log->s.apos.trBase, log->r.currentAngles );

	log->genericValue1 = level.time + SHINOBI_LOG_LIFETIME;
	log->think = Shinobi_LogThink;
	log->nextthink = level.time + FRAMETIME;
	trap->LinkEntity( (sharedEntity_t *)log );
}

// the double is found out (or no longer needed): it was only a log all along
void Shinobi_DecoyToLog( gentity_t *decoy ) {
	gentity_t *owner = Shinobi_CloneOwner( decoy );
	vec3_t     org, up = { 0, 0, 1 };

	if ( owner && owner->client->shinobi.kawarimiDecoy == decoy->s.number + 1 )
		owner->client->shinobi.kawarimiDecoy = 0;

	if ( decoy->shinobiCloneReady ) { // was visible
		VectorCopy( decoy->r.currentOrigin, org );
		G_PlayEffectID( shinobiMedia.fxClonePoof, org, up );
		G_SoundAtLoc( org, CHAN_AUTO, shinobiMedia.sndLog );
		Shinobi_SpawnLog( org, decoy->client ? decoy->client->ps.viewangles[YAW] : 0 );
	}
	decoy->shinobiCloneOwner = 0;
	decoy->shinobiCloneDecoy = qfalse;
	G_FreeEntity( decoy );
}

// first frame the double is drawn: it steps into the caster's place and pose
static void Shinobi_DecoyTakeOver( gentity_t *decoy, gentity_t *owner ) {
	gclient_t *oc = owner->client, *dc = decoy->client;

	G_SetOrigin( decoy, oc->ps.origin );
	VectorCopy( oc->ps.origin, dc->ps.origin );
	VectorCopy( oc->ps.velocity, dc->ps.velocity );
	SetClientViewAngle( decoy, oc->ps.viewangles );
	if ( decoy->NPC ) {
		decoy->NPC->desiredYaw = oc->ps.viewangles[YAW];
		decoy->NPC->desiredPitch = 0;
	}
	dc->ps.legsAnim = oc->ps.legsAnim;
	dc->ps.torsoAnim = oc->ps.torsoAnim;
	dc->ps.legsTimer = oc->ps.legsTimer;
	dc->ps.torsoTimer = oc->ps.torsoTimer;
	dc->ps.saberHolstered = oc->ps.saberHolstered;
	trap->LinkEntity( (sharedEntity_t *)decoy );
}

/*
===========================================================================
Water Clone
===========================================================================
*/
static gentity_t *Shinobi_CloneFindEnemy( gentity_t *clone );

// one hit (or the end): a splash and a puddle; whatever it was holding lets go
static void Shinobi_WaterClonePop( gentity_t *clone ) {
	vec3_t up = { 0, 0, 1 }, floor;

	if ( clone->client ) {
		if ( clone->client->shinobi.prisonTarget )
			Shinobi_PrisonRelease( clone );
		if ( clone->client->shinobi.flameEnt )
			Shinobi_FlameStop( clone );
		if ( clone->client->shinobi.shadowEnt )
			Shinobi_ShadowRelease( clone );
	}
	if ( clone->shinobiCloneReady ) {
		Shinobi_EffectDir( shinobiMedia.fxWaterSplash[1], clone->r.currentOrigin, up );
		G_PlayEffectID( shinobiMedia.fxWaterImpact, clone->r.currentOrigin, up );
		G_SoundAtLoc( clone->r.currentOrigin, CHAN_AUTO, shinobiMedia.sndSplash );
		if ( Shinobi_FloorBelow( clone->r.currentOrigin, 96, floor ) )
			Shinobi_SpawnZone( SZ_WATER, floor, 70, 16000, clone->shinobiCloneOwner );
	}
	clone->shinobiCloneOwner = 0;
	clone->shinobiCloneWater = qfalse;
	G_FreeEntity( clone );
}

// a water clone can only use water jutsu
#define SHINOBI_WATERCLONE_CAN(j) ( (j) == SJ_SUITON || (j) == SJ_WAVE || (j) == SJ_SUIRO || (j) == SJ_MIST )

// the first water jutsu after Water Clone in the owner's build (wrapping around)
static int Shinobi_WaterCloneJutsu( gentity_t *owner ) {
	const shinobiBuild_t *b = &owner->client->pers.shinobiBuild;
	int i, k;

	for ( i = 0; i < b->numJutsu; i++ ) {
		if ( b->jutsu[i] != SJ_WATERCLONE )
			continue;
		for ( k = 1; k < b->numJutsu; k++ ) {
			const int next = b->jutsu[( i + k ) % b->numJutsu];
			if ( SHINOBI_WATERCLONE_CAN( next ) )
				return next;
		}
		break;
	}
	return SJ_NONE;
}

// how close the clone must be to use a jutsu (0 = it can't use it)
static float Shinobi_WaterCloneRange( int jutsu ) {
	switch ( jutsu ) {
	case SJ_SUIRO:  return SHINOBI_PRISON_RANGE + 30;
	case SJ_SUITON: return 700;
	case SJ_WAVE:   return 500;
	case SJ_MIST:   return 500;
	default:        return 0; // not a water jutsu: it just fights
	}
}

static void Shinobi_WaterCloneStopHold( gentity_t *clone ) {
	gclient_t *c = clone->client;
	if ( c->shinobi.prisonTarget )
		Shinobi_PrisonRelease( clone );
	if ( c->shinobi.flameEnt )
		Shinobi_FlameStop( clone );
	if ( c->shinobi.shadowEnt )
		Shinobi_ShadowRelease( clone );
	c->shinobi.wcHoldJutsu = SJ_NONE;
}

/*
Every server frame for a ready Water Clone. Its chakra is the owner's: what
the clone spends is taken from the owner. Returns qfalse if it burst.
*/
static qboolean Shinobi_WaterCloneThink( gentity_t *clone, gentity_t *owner ) {
	gclient_t *c = clone->client;
	const int  full = 200;
	const int  spent = full - c->ps.fd.forcePower;

	if ( c->ps.fd.forcePowerMax != full ) { // first frame: start the books
		c->ps.fd.forcePowerMax = c->ps.fd.forcePower = full;
		return qtrue;
	}
	if ( spent > 0 ) {
		owner->client->ps.fd.forcePower -= spent;
		if ( owner->client->ps.fd.forcePower < 0 )
			owner->client->ps.fd.forcePower = 0;
	}
	if ( owner->client->ps.fd.forcePower <= 0 ) { // the owner is dry: the water falls apart
		trap->SendServerCommand( owner - g_entities, "cp \"Out of chakra: the water clone bursts\"" );
		Shinobi_WaterClonePop( clone );
		return qfalse;
	}
	c->ps.fd.forcePower = owner->client->ps.fd.forcePower > 0 ? full : 0; // dry owner: its drains fail
	c->ps.fd.forcePowerMax = full;

	// holding something: stand still with the hand out, up to a limit
	if ( c->shinobi.wcHoldJutsu ) {
		const qboolean still = ( c->shinobi.wcHoldJutsu == SJ_SUIRO && c->shinobi.prisonTarget )
			|| ( c->shinobi.wcHoldJutsu == SJ_FLAME && c->shinobi.flameEnt )
			|| ( c->shinobi.wcHoldJutsu == SJ_SHADOW && c->shinobi.shadowEnt );
		if ( !still || level.time >= c->shinobi.wcHoldEnd ) {
			Shinobi_Debug( "Water Clone %i stops holding %s (%s)", clone->s.number, shinobiJutsuTitles[c->shinobi.wcHoldJutsu],
				still ? "time" : "lost it" );
			Shinobi_WaterCloneStopHold( clone );
		}
		else if ( c->shinobi.wcHoldJutsu == SJ_SUIRO ) {
			Shinobi_PrisonCasterThink( clone );
			if ( c->shinobi.prisonTarget ) {
				VectorCopy( c->shinobi.wcHoldPos, c->ps.origin );
				G_SetOrigin( clone, c->shinobi.wcHoldPos );
				VectorClear( c->ps.velocity );
				c->ps.forceHandExtend = HANDEXTEND_TAUNT;
				c->ps.forceDodgeAnim = BOTH_FORCEGRIP_HOLD;
				c->ps.forceHandExtendTime = level.time + 150;
				trap->LinkEntity( (sharedEntity_t *)clone );
			}
		}
		return qtrue;
	}

	// ready to cast its jutsu at the enemy it is fighting
	if ( c->shinobi.wcCastNext <= level.time && clone->enemy && clone->enemy->inuse && clone->enemy->health > 0 ) {
		const int   jutsu = Shinobi_WaterCloneJutsu( owner );
		const float range = Shinobi_WaterCloneRange( jutsu );
		vec3_t      eye, d, angles;

		VectorCopy( c->ps.origin, eye );
		eye[2] += c->ps.viewheight;
		VectorSubtract( clone->enemy->r.currentOrigin, eye, d );
		if ( range > 0 && VectorLength( d ) <= range
			&& owner->client->ps.fd.forcePower >= Shinobi_JutsuCost( jutsu, SHINOBI_JUTSU_MIN_LEVEL( jutsu ) > 2 ? 3 : 2 ) ) {
			vectoangles( d, angles );
			angles[ROLL] = 0;
			SetClientViewAngle( clone, angles );
			if ( clone->NPC ) {
				clone->NPC->desiredYaw = angles[YAW];
				clone->NPC->desiredPitch = angles[PITCH];
			}
			if ( !Shinobi_Cast( clone, jutsu, 2 ) ) {
				Shinobi_Debug( "Water Clone %i: %s failed (enemy %i, dist %.0f)", clone->s.number, shinobiJutsuTitles[jutsu],
					clone->enemy->s.number, VectorLength( d ) );
				c->shinobi.wcCastNext = level.time + 500; // try again soon
				return qtrue;
			}
			c->shinobi.wcCastNext = level.time + SHINOBI_WATERCLONE_CAST_MS;
			Shinobi_Debug( "Water Clone %i casts %s on %i", clone->s.number, shinobiJutsuTitles[jutsu], clone->enemy->s.number );

			if ( SHINOBI_JUTSU_HOLD( jutsu ) ) {
				c->shinobi.wcHoldJutsu = jutsu;
				c->shinobi.wcHoldEnd = level.time + ( jutsu == SJ_FLAME ? SHINOBI_WATERCLONE_BREATH_MS : SHINOBI_WATERCLONE_HOLD_MS );
				VectorCopy( c->ps.origin, c->shinobi.wcHoldPos );
			}
		}
	}
	return qtrue;
}

qboolean Shinobi_CastWaterClone( gentity_t *ent ) {
	static const float yaws[] = { -60, 60, -120, 120, 180, 0 };
	vec3_t     spot, up = { 0, 0, 1 };
	gentity_t *clone;
	int        i;

	// one at a time: a new one replaces the old
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *e = &g_entities[i];
		if ( e->inuse && e->shinobiCloneWater && e->shinobiCloneOwner == ent->s.number + 1 )
			Shinobi_WaterClonePop( e );
	}
	for ( i = 0; i < (int)ARRAY_LEN( yaws ); i++ ) {
		if ( Shinobi_CloneSpot( ent, yaws[i], NULL, 0, spot ) )
			break;
	}
	if ( i == (int)ARRAY_LEN( yaws ) ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		trap->SendServerCommand( ent - g_entities, "cp \"No room for a water clone\"" );
		return qfalse;
	}
	clone = Shinobi_SpawnClone( ent, spot );
	if ( !clone )
		return qfalse;
	clone->shinobiCloneWater = qtrue;
	clone->shinobiCloneExpire = level.time + SHINOBI_WATERCLONE_LIFE;
	clone->client->shinobi.wcCastNext = level.time + 1200;
	clone->client->shinobi.wcHoldJutsu = SJ_NONE;

	Shinobi_EffectDir( shinobiMedia.fxWaterSplash[2], spot, up );
	G_PlayEffectID( shinobiMedia.fxWaterImpact, spot, up );
	G_SoundAtLoc( spot, CHAN_AUTO, shinobiMedia.sndWaterCast );
	{
		const int j = Shinobi_WaterCloneJutsu( ent );
		trap->SendServerCommand( ent - g_entities, va( "cp \"Water Clone: %s\"",
			j ? va( "will use %s", shinobiJutsuTitles[j] ) : "fights (it can only use a water jutsu from your build)" ) );
	}
	return qtrue;
}

static gentity_t *Shinobi_CloneFindEnemy( gentity_t *clone ) {
	gentity_t *best = NULL;
	// a Kawarimi double goes for the nearest enemy anywhere, like the real
	// ninja would; clones only fight what they can see
	const qboolean anywhere = clone->shinobiCloneDecoy || clone->shinobiCloneLightning; // (the lightning clone homes in)
	float      bestDist = anywhere ? 1e12f : (float)SHINOBI_CLONE_SIGHT * SHINOBI_CLONE_SIGHT;
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
		if ( !anywhere ) {
			if ( !trap->InPVS( eye, e->r.currentOrigin ) )
				continue;

			VectorCopy( e->r.currentOrigin, target );
			target[2] += 16;
			trap->Trace( &tr, eye, NULL, NULL, target, clone->s.number, MASK_SOLID, qfalse, 0, 0 );
			if ( tr.fraction < 1.0f && tr.entityNum != e->s.number )
				continue;
		}

		best = e;
		bestDist = d;
	}
	return best;
}

static void Shinobi_CloneSupervise( gentity_t *clone, gentity_t *owner ) {
	if ( clone->shinobiCloneDecoy ) {
		// a double fights on its own and never leads anyone to the hidden caster
		clone->client->leader = NULL;
		if ( clone->NPC && clone->NPC->behaviorState == BS_FOLLOW_LEADER )
			clone->NPC->behaviorState = clone->NPC->defaultBehavior = BS_DEFAULT;
		if ( clone->enemy && !Shinobi_CloneIsHostile( clone, clone->enemy ) )
			G_ClearEnemy( clone );
		{
			gentity_t *enemy = Shinobi_CloneFindEnemy( clone );
			if ( enemy && enemy != clone->enemy )
				G_SetEnemy( clone, enemy );
		}
		return;
	}

	if ( !clone->shinobiCloneReady ) {
		if ( clone->think != NPC_Think )
			return; // still in NPC_Begin
		// NPC_Begin is done: apply what the .npc file and team code can't express
		clone->health = SHINOBI_CLONE_HEALTH;
		clone->client->ps.stats[STAT_HEALTH] = SHINOBI_CLONE_HEALTH;
		clone->client->ps.stats[STAT_MAX_HEALTH] = SHINOBI_CLONE_HEALTH;
		clone->client->pers.maxHealth = SHINOBI_CLONE_HEALTH;
		if ( clone->shinobiCloneEarth ) { // heavy and tough
			clone->health = clone->client->ps.stats[STAT_HEALTH] = SHINOBI_EARTHCLONE_HEALTH;
			clone->client->ps.stats[STAT_MAX_HEALTH] = clone->client->pers.maxHealth = SHINOBI_EARTHCLONE_HEALTH;
			if ( clone->NPC ) {
				clone->NPC->stats.runSpeed = (int)( clone->NPC->stats.runSpeed * SHINOBI_EARTHCLONE_SPEED );
				clone->NPC->stats.walkSpeed = (int)( clone->NPC->stats.walkSpeed * SHINOBI_EARTHCLONE_SPEED );
			}
		}
		if ( clone->shinobiCloneLightning && clone->NPC ) { // quick as lightning
			clone->NPC->stats.runSpeed = (int)( clone->NPC->stats.runSpeed * SHINOBI_RAICLONE_RUN );
			clone->NPC->stats.walkSpeed = (int)( clone->NPC->stats.walkSpeed * SHINOBI_RAICLONE_RUN );
		}
		Shinobi_CloneApplyTeam( clone, owner );
		clone->client->leader = owner;
		clone->NPC->behaviorState = clone->NPC->defaultBehavior = BS_FOLLOW_LEADER;
		clone->shinobiCloneReady = qtrue;
	}

	if ( clone->shinobiCloneLightning ) { // a homing trap: no leader, straight for the nearest enemy
		gentity_t *enemy;
		clone->client->leader = NULL;
		if ( clone->NPC )
			clone->NPC->behaviorState = clone->NPC->defaultBehavior = BS_DEFAULT;
		enemy = Shinobi_CloneFindEnemy( clone );
		if ( enemy && enemy != clone->enemy )
			G_SetEnemy( clone, enemy );
		if ( clone->NPC && clone->enemy ) {
			clone->NPC->goalEntity = clone->enemy;
			clone->NPC->goalRadius = SHINOBI_RAICLONE_TRIGGER / 2;
		}
		return;
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

// living clones slowly drain their owner's chakra (and stop its natural regen);
// when the owner runs dry, the clones vanish
static void Shinobi_CloneDrain( const int clonesPerOwner[MAX_CLIENTS] ) {
	int i;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		gentity_t *owner = &g_entities[i];
		gclient_t *client = owner->client;

		if ( !clonesPerOwner[i] || !owner->inuse || !client )
			continue;

		client->ps.fd.forcePowerRegenDebounceTime = level.time + 500;
		if ( client->shinobi.cloneDrainNext > level.time )
			continue;
		client->shinobi.cloneDrainNext = level.time + SHINOBI_CLONE_DRAIN_MS;
		client->ps.fd.forcePower -= clonesPerOwner[i];
		if ( client->ps.fd.forcePower <= 0 ) {
			client->ps.fd.forcePower = 0;
			Shinobi_RemoveClones( i, qtrue );
			trap->SendServerCommand( i, "cp \"Out of chakra: the clones vanish\"" );
		}
	}
}

void Shinobi_RunFrame( void ) {
	int clonesPerOwner[MAX_CLIENTS];
	int i;

	memset( clonesPerOwner, 0, sizeof( clonesPerOwner ) );

	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *clone = &g_entities[i];
		gentity_t *owner;

		if ( !clone->inuse || !clone->shinobiCloneOwner )
			continue;

		Shinobi_PerkBroadcasts( clone ); // Byakugan eyes see clones through walls
		if ( ( clone->shinobiCloneLightning || clone->shinobiCloneEarth ) && clone->shinobiCloneFx <= level.time && clone->health > 0 ) {
			vec3_t up = { 0, 0, 1 }, feet;
			clone->shinobiCloneFx = level.time + ( clone->shinobiCloneLightning ? 200 : 500 );
			VectorCopy( clone->r.currentOrigin, feet );
			if ( clone->shinobiCloneEarth )
				feet[2] += clone->r.mins[2] + 2;
			G_PlayEffectID( clone->shinobiCloneLightning ? shinobiMedia.fxRaiEdge : shinobiMedia.fxQuakeDust, feet, up );
		}
		owner = Shinobi_CloneOwner( clone );

		if ( clone->shinobiCloneDecoy ) {
			if ( !owner || owner->health <= 0 || !clone->client || clone->health <= 0
				|| owner->client->sess.sessionTeam == TEAM_SPECTATOR
				|| owner->client->shinobi.kawarimiDecoy != clone->s.number + 1
				|| level.time >= clone->shinobiCloneExpire ) {
				Shinobi_DecoyToLog( clone ); // hit, or its time is up: a log
				continue;
			}
			if ( !clone->shinobiCloneReady ) {
				if ( clone->think != NPC_Think || ( clone->s.eFlags & EF_NODRAW ) )
					continue; // still in NPC_Begin
				clone->health = SHINOBI_CLONE_HEALTH;
				clone->client->ps.stats[STAT_HEALTH] = SHINOBI_CLONE_HEALTH;
				Shinobi_CloneApplyTeam( clone, owner );
				clone->client->leader = NULL;
				if ( clone->NPC )
					clone->NPC->behaviorState = clone->NPC->defaultBehavior = BS_DEFAULT;
				Shinobi_DecoyTakeOver( clone, owner );
				clone->shinobiCloneReady = qtrue;
				if ( owner->client->shinobi.kawarimiPending )
					Shinobi_Hide( owner );
			}
			if ( clone->shinobiCloneThink <= level.time ) {
				clone->shinobiCloneThink = level.time + SHINOBI_CLONE_THINK_MS;
				Shinobi_CloneSupervise( clone, owner );
			}
			continue;
		}

		if ( !owner || owner->health <= 0 || !owner->client
			|| owner->client->sess.sessionTeam == TEAM_SPECTATOR
			|| !clone->client || clone->health <= 0
			|| level.time >= clone->shinobiCloneExpire ) {
			if ( clone->shinobiCloneWater )
				Shinobi_WaterClonePop( clone );
			else
				Shinobi_ClonePoof( clone );
			continue;
		}

		if ( clone->shinobiCloneEarth && clone->client )
			clone->client->ps.eFlags |= SHINOBI_EF_STONE; // drawn as stone

		if ( clone->shinobiCloneWater && clone->shinobiCloneReady && !Shinobi_WaterCloneThink( clone, owner ) )
			continue; // burst

		if ( clone->shinobiCloneLightning && clone->shinobiCloneReady ) {
			// a living lightning trap: it homes in and goes off next to its enemy
			gentity_t *enemy = clone->enemy;
			if ( !enemy || !enemy->inuse || enemy->health <= 0 || !Shinobi_CloneIsHostile( clone, enemy ) ) {
				enemy = Shinobi_CloneFindEnemy( clone );
				if ( enemy )
					G_SetEnemy( clone, enemy );
			}
			if ( enemy && Distance( enemy->r.currentOrigin, clone->r.currentOrigin ) <= SHINOBI_RAICLONE_TRIGGER ) {
				Shinobi_Debug( "lightning clone %i goes off at %i", clone->s.number, enemy->s.number );
				clone->health = 0;
				Shinobi_ClonePoof( clone );
				continue;
			}
		}

		if ( owner->s.number < MAX_CLIENTS )
			clonesPerOwner[owner->s.number]++;

		if ( clone->shinobiCloneThink <= level.time ) {
			clone->shinobiCloneThink = level.time + SHINOBI_CLONE_THINK_MS;
			Shinobi_CloneSupervise( clone, owner );
		}
	}

	Shinobi_CloneDrain( clonesPerOwner );
}

/*
Earth Clone (Doton): one at a time - slow, tough (150), its blows hit hard;
when it falls (or its time is up) it crumbles into a heap of mud.
*/
qboolean Shinobi_CastEarthClone( gentity_t *ent ) {
	static const float yaws[] = { 0, -45, 45, -90, 90, 180 };
	vec3_t     spot, up = { 0, 0, 1 };
	gentity_t *clone;
	int        i;

	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *e = &g_entities[i];
		if ( e->inuse && e->shinobiCloneEarth && e->shinobiCloneOwner == ent->s.number + 1 )
			Shinobi_ClonePoof( e ); // a new one replaces the old
	}
	for ( i = 0; i < (int)ARRAY_LEN( yaws ); i++ ) {
		if ( Shinobi_CloneSpot( ent, yaws[i], NULL, 0, spot ) )
			break;
	}
	if ( i == (int)ARRAY_LEN( yaws ) ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		trap->SendServerCommand( ent - g_entities, "cp \"No room for an earth clone\"" );
		return qfalse;
	}
	clone = Shinobi_SpawnClone( ent, spot );
	if ( !clone )
		return qfalse;
	clone->shinobiCloneEarth = qtrue;
	clone->shinobiCloneExpire = level.time + SHINOBI_EARTHCLONE_LIFE;
	G_PlayEffectID( shinobiMedia.fxWallRise, spot, up );
	G_SoundAtLoc( spot, CHAN_AUTO, shinobiMedia.sndWallRise );
	return qtrue;
}

/*
Lightning Clone (Raiton): one at a time, 12 s; it runs at the nearest enemy
and goes off next to it like a lightning trap (also when struck down):
everyone around is shocked and paralysed.
*/
qboolean Shinobi_CastRaiClone( gentity_t *ent ) {
	static const float yaws[] = { 0, -45, 45, -90, 90, 180 };
	vec3_t     spot, up = { 0, 0, 1 };
	gentity_t *clone;
	int        i;

	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *e = &g_entities[i];
		if ( e->inuse && e->shinobiCloneLightning && e->shinobiCloneOwner == ent->s.number + 1 ) {
			e->health = 1; // the old one fizzles out quietly
			Shinobi_ClonePoof( e );
		}
	}
	for ( i = 0; i < (int)ARRAY_LEN( yaws ); i++ ) {
		if ( Shinobi_CloneSpot( ent, yaws[i], NULL, 0, spot ) )
			break;
	}
	if ( i == (int)ARRAY_LEN( yaws ) ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		trap->SendServerCommand( ent - g_entities, "cp \"No room for a lightning clone\"" );
		return qfalse;
	}
	clone = Shinobi_SpawnClone( ent, spot );
	if ( !clone )
		return qfalse;
	clone->shinobiCloneLightning = qtrue;
	clone->shinobiCloneExpire = level.time + SHINOBI_RAICLONE_LIFE;
	G_PlayEffectID( shinobiMedia.fxChidoriHit[1], spot, up );
	G_SoundAtLoc( spot, CHAN_AUTO, shinobiMedia.sndChidoriForm );
	return qtrue;
}
