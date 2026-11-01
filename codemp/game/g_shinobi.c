/*
===========================================================================
JK_Shinobi - server-side jutsu logic (chakra, Rasengan, Shunshin, shadow clones).

Flags that the client predicts or renders live in playerState user fields
(see bg_shinobi.h). Timers live in gclient_t->shinobi.
===========================================================================
*/
#include "g_local.h"
#include "bg_shinobi.h"

extern void G_Knockdown( gentity_t *victim ); // g_combat.c (a different static-like G_Knockdown exists in AnimalNPC.c)

static int fxRasenganHit;
static int fxRasenganFizzle;
static int sndChargeStart;
static int sndRasenganForm;
static int sndRasenganDash;
static int sndRasenganHit;
static int sndFizzle;
static int fxShunshin;
static int fxClonePoof;
static int sndPoof;

void Shinobi_Init( void ) {
	fxRasenganHit    = G_EffectIndex( SHINOBI_FX_RASENGAN_HIT );
	fxRasenganFizzle = G_EffectIndex( SHINOBI_FX_RASENGAN_FIZZLE );
	fxShunshin       = G_EffectIndex( SHINOBI_FX_SHUNSHIN );
	fxClonePoof      = G_EffectIndex( SHINOBI_FX_CLONE_POOF );
	sndPoof          = G_SoundIndex( "sound/weapons/force/distract.wav" );
	// Force sounds from the base game, reused until we have our own.
	sndChargeStart  = G_SoundIndex( "sound/weapons/force/jumpbuild.wav" );
	sndRasenganForm = G_SoundIndex( "sound/weapons/force/drain.wav" );
	sndRasenganDash = G_SoundIndex( "sound/weapons/force/speed.wav" );
	sndRasenganHit  = G_SoundIndex( "sound/weapons/force/push.wav" );
	sndFizzle       = G_SoundIndex( "sound/weapons/force/noforce.wav" );
}

static void Shinobi_ClearRasengan( gclient_t *client ) {
	client->ps.userInt2 = 0;
	client->ps.userInt3 = 0;
	client->shinobi.formEnd = 0;
	client->shinobi.armedEnd = 0;
	client->shinobi.dashEnd = 0;
}

static void Shinobi_Reset( gclient_t *client ) {
	client->ps.userInt1 = 0;
	Shinobi_ClearRasengan( client );
}

static void Shinobi_Fizzle( gentity_t *ent ) {
	vec3_t up = { 0, 0, 1 };
	vec3_t org;

	VectorCopy( ent->client->ps.origin, org );
	org[2] += 16;
	G_PlayEffectID( fxRasenganFizzle, org, up );
	Shinobi_ClearRasengan( ent->client );
}

static qboolean Shinobi_CanAct( gentity_t *ent ) {
	gclient_t *client = ent->client;

	if ( !client || ent->health <= 0 )
		return qfalse;
	if ( client->sess.sessionTeam == TEAM_SPECTATOR || client->tempSpectate >= level.time )
		return qfalse;
	if ( client->ps.pm_type != PM_NORMAL && client->ps.pm_type != PM_JETPACK )
		return qfalse;
	if ( client->ps.m_iVehicleNum || client->ps.emplacedIndex )
		return qfalse;
	if ( client->ps.forceHandExtend == HANDEXTEND_KNOCKDOWN )
		return qfalse;
	return qtrue;
}

/*
==================
Cmd_Rasengan_f

"rasengan" console command: spend chakra and start forming the ball.
==================
*/
void Cmd_Rasengan_f( gentity_t *ent ) {
	gclient_t *client = ent->client;

	if ( !Shinobi_CanAct( ent ) )
		return;
	if ( SHINOBI_RASENGAN_ANY( &client->ps ) )
		return;
	if ( client->ps.fd.forcePower < SHINOBI_RASENGAN_COST ) {
		G_Sound( ent, CHAN_AUTO, sndFizzle );
		trap->SendServerCommand( ent - g_entities, va( "cp \"Not enough chakra (%i/%i)\"", client->ps.fd.forcePower, SHINOBI_RASENGAN_COST ) );
		return;
	}

	client->ps.fd.forcePower -= SHINOBI_RASENGAN_COST;
	client->ps.userInt1 = 0; // stop charging
	client->ps.userInt2 = 0;
	client->ps.userInt3 = 1; // forming
	client->shinobi.formEnd = level.time + SHINOBI_RASENGAN_FORM_MS;
	G_Sound( ent, CHAN_AUTO, sndRasenganForm );
}

static void Shinobi_StartDash( gentity_t *ent ) {
	gclient_t *client = ent->client;

	client->ps.userInt2 = 1;
	client->ps.userInt3 = 1; // armed + forming bits = dash (see bg_shinobi.h)
	client->shinobi.dashEnd = level.time + SHINOBI_RASENGAN_DASH_MS;
	G_Sound( ent, CHAN_BODY, sndRasenganDash );
}

static gentity_t *Shinobi_FindDashTarget( gentity_t *ent, vec3_t fwd, vec3_t hitPoint ) {
	int       touch[MAX_GENTITIES];
	int       num, i;
	vec3_t    center, mins, maxs;
	gentity_t *best = NULL;
	float     bestDist = 0;

	VectorMA( ent->client->ps.origin, SHINOBI_RASENGAN_REACH, fwd, center );
	VectorSet( mins, center[0] - SHINOBI_RASENGAN_HITBOX, center[1] - SHINOBI_RASENGAN_HITBOX, center[2] - SHINOBI_RASENGAN_HITBOX );
	VectorSet( maxs, center[0] + SHINOBI_RASENGAN_HITBOX, center[1] + SHINOBI_RASENGAN_HITBOX, center[2] + SHINOBI_RASENGAN_HITBOX );

	num = trap->EntitiesInBox( mins, maxs, touch, MAX_GENTITIES );
	for ( i = 0; i < num; i++ ) {
		gentity_t *other = &g_entities[touch[i]];
		trace_t    tr;
		float      dist;

		if ( other == ent || !other->inuse || !other->client || other->health <= 0 )
			continue;
		if ( other->client->sess.sessionTeam == TEAM_SPECTATOR )
			continue;
		if ( level.gametype >= GT_TEAM && OnSameTeam( ent, other ) )
			continue;

		// don't hit through walls
		trap->Trace( &tr, ent->client->ps.origin, NULL, NULL, other->client->ps.origin, ent->s.number, MASK_SOLID, qfalse, 0, 0 );
		if ( tr.fraction < 1.0f )
			continue;

		dist = Distance( ent->client->ps.origin, other->client->ps.origin );
		if ( !best || dist < bestDist ) {
			best = other;
			bestDist = dist;
		}
	}

	if ( best ) {
		VectorAdd( ent->client->ps.origin, best->client->ps.origin, hitPoint );
		VectorScale( hitPoint, 0.5f, hitPoint );
		hitPoint[2] += 12;
	}
	return best;
}

static void Shinobi_RasenganHit( gentity_t *ent, gentity_t *victim, vec3_t fwd, vec3_t point ) {
	vec3_t dir;

	VectorCopy( fwd, dir );
	dir[2] = 0.35f;
	VectorNormalize( dir );

	G_Damage( victim, ent, ent, dir, point, SHINOBI_RASENGAN_DAMAGE, DAMAGE_NO_KNOCKBACK, MOD_MELEE );

	if ( victim->client && victim->health > 0 ) {
		VectorScale( dir, SHINOBI_RASENGAN_THROW, victim->client->ps.velocity );
		victim->client->ps.groundEntityNum = ENTITYNUM_NONE;
		// credit the kill if they hit a wall or fall off a ledge
		victim->client->ps.otherKiller = ent->s.number;
		victim->client->ps.otherKillerTime = level.time + 5000;
		victim->client->ps.otherKillerDebounceTime = level.time + 100;
		G_Knockdown( victim );
	}

	G_PlayEffectID( fxRasenganHit, point, dir );
	G_Sound( victim, CHAN_AUTO, sndRasenganHit );
	Shinobi_ClearRasengan( ent->client );
}

/*
==================
Shinobi_ClientThink

Called every client frame from ClientThink_real, before Pmove.
==================
*/
void Shinobi_ClientThink( gentity_t *ent, usercmd_t *ucmd ) {
	gclient_t *client = ent->client;
	qboolean   attackPressed;

	if ( !Shinobi_CanAct( ent ) ) {
		Shinobi_Reset( client );
		return;
	}

	attackPressed = ( ucmd->buttons & BUTTON_ATTACK ) && !( client->buttons & BUTTON_ATTACK );

	// --- Chakra charging: hold USE while standing still on the ground ---
	if ( ( ucmd->buttons & BUTTON_USE )
		&& !ucmd->forwardmove && !ucmd->rightmove && ucmd->upmove <= 0
		&& client->ps.groundEntityNum != ENTITYNUM_NONE
		&& !SHINOBI_RASENGAN_ANY( &client->ps )
		&& client->ps.weaponTime <= 0 ) {
		if ( !client->ps.userInt1 ) {
			client->ps.userInt1 = 1;
			client->shinobi.chargeTick = level.time + SHINOBI_CHARGE_TICK_MS;
			G_Sound( ent, CHAN_AUTO, sndChargeStart );
		}
		while ( client->shinobi.chargeTick <= level.time ) {
			client->ps.fd.forcePower += SHINOBI_CHARGE_PER_TICK;
			client->shinobi.chargeTick += SHINOBI_CHARGE_TICK_MS;
		}
		if ( client->ps.fd.forcePower > client->ps.fd.forcePowerMax )
			client->ps.fd.forcePower = client->ps.fd.forcePowerMax;
	}
	else {
		client->ps.userInt1 = 0;
	}

	// --- Rasengan ---
	if ( SHINOBI_RASENGAN_FORMING( &client->ps ) ) {
		if ( level.time >= client->shinobi.formEnd ) {
			client->ps.userInt3 = 0;
			client->ps.userInt2 = 1; // armed
			client->shinobi.armedEnd = level.time + SHINOBI_RASENGAN_HOLD_MS;
		}
	}
	else if ( SHINOBI_RASENGAN_ARMED( &client->ps ) ) {
		if ( attackPressed ) {
			Shinobi_StartDash( ent );
		}
		else if ( level.time >= client->shinobi.armedEnd ) {
			Shinobi_Fizzle( ent );
		}
	}

	if ( SHINOBI_RASENGAN_DASH( &client->ps ) ) {
		vec3_t     yaw, fwd, point;
		gentity_t *victim;

		VectorSet( yaw, 0, client->ps.viewangles[YAW], 0 );
		AngleVectors( yaw, fwd, NULL, NULL );

		victim = Shinobi_FindDashTarget( ent, fwd, point );
		if ( victim ) {
			Shinobi_RasenganHit( ent, victim, fwd, point );
		}
		else if ( level.time >= client->shinobi.dashEnd ) {
			Shinobi_Fizzle( ent );
		}
	}
}

/*
===========================================================================
Shunshin no Jutsu (body flicker): short teleport along the view direction.
===========================================================================
*/
void Cmd_Shunshin_f( gentity_t *ent ) {
	gclient_t *client = ent->client;
	vec3_t     angles, dir, start, end;
	trace_t    tr;
	float      dist;

	if ( !Shinobi_CanAct( ent ) )
		return;
	if ( client->shinobi.shunshinNext > level.time )
		return;
	if ( SHINOBI_RASENGAN_DASH( &client->ps ) )
		return;
	if ( client->ps.fd.forcePower < SHINOBI_SHUNSHIN_COST ) {
		G_Sound( ent, CHAN_AUTO, sndFizzle );
		return;
	}

	VectorCopy( client->ps.viewangles, angles );
	angles[ROLL] = 0;
	if ( angles[PITCH] > SHINOBI_SHUNSHIN_MAX_PITCH )
		angles[PITCH] = SHINOBI_SHUNSHIN_MAX_PITCH;
	else if ( angles[PITCH] < -SHINOBI_SHUNSHIN_MAX_PITCH )
		angles[PITCH] = -SHINOBI_SHUNSHIN_MAX_PITCH;
	if ( client->ps.groundEntityNum != ENTITYNUM_NONE && angles[PITCH] > 0 )
		angles[PITCH] = 0; // on the ground looking down: blink straight ahead
	AngleVectors( angles, dir, NULL, NULL );

	VectorCopy( client->ps.origin, start );
	start[2] += 1; // clear the floor
	VectorMA( start, SHINOBI_SHUNSHIN_DIST, dir, end );

	trap->Trace( &tr, start, ent->r.mins, ent->r.maxs, end, ent->s.number, MASK_PLAYERSOLID, qfalse, 0, 0 );
	if ( tr.startsolid || tr.allsolid )
		return;
	dist = tr.fraction * SHINOBI_SHUNSHIN_DIST;
	if ( dist < SHINOBI_SHUNSHIN_MIN_DIST ) {
		G_Sound( ent, CHAN_AUTO, sndFizzle );
		return;
	}

	client->ps.fd.forcePower -= SHINOBI_SHUNSHIN_COST;
	client->shinobi.shunshinNext = level.time + SHINOBI_SHUNSHIN_COOLDOWN;
	client->ps.userInt1 = 0; // stop charging

	// smoke and leaves where we were, and where we land
	G_PlayEffectID( fxShunshin, client->ps.origin, dir );
	G_Sound( ent, CHAN_AUTO, sndPoof );

	trap->UnlinkEntity( (sharedEntity_t *)ent );
	VectorCopy( tr.endpos, client->ps.origin );
	if ( client->ps.velocity[2] < 0 )
		client->ps.velocity[2] = 0; // don't arrive mid-fall at full speed
	client->ps.eFlags ^= EF_TELEPORT_BIT; // tell clients not to lerp
	BG_PlayerStateToEntityState( &client->ps, &ent->s, qtrue );
	VectorCopy( client->ps.origin, ent->r.currentOrigin );
	trap->LinkEntity( (sharedEntity_t *)ent );

	G_PlayEffectID( fxShunshin, client->ps.origin, dir );
}

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

	G_PlayEffectID( fxClonePoof, clone->r.currentOrigin, up );
	G_SoundAtLoc( clone->r.currentOrigin, CHAN_AUTO, sndPoof ); // not tied to the entity we free next
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

static qboolean Shinobi_CloneSpot( gentity_t *owner, float sideOffset, vec3_t out ) {
	vec3_t  yaw, fwd, right, start, end;
	trace_t tr;

	VectorSet( yaw, 0, owner->client->ps.viewangles[YAW], 0 );
	AngleVectors( yaw, fwd, right, NULL );

	VectorCopy( owner->client->ps.origin, start );
	start[2] += 1;
	VectorMA( start, sideOffset, right, end );
	VectorMA( end, 16, fwd, end );

	trap->Trace( &tr, start, owner->r.mins, owner->r.maxs, end, owner->s.number, MASK_PLAYERSOLID, qfalse, 0, 0 );
	if ( tr.startsolid || tr.allsolid || tr.fraction < 0.6f )
		return qfalse;
	VectorCopy( tr.endpos, out );
	return qtrue;
}

// NPC teams drive stock AI and saber code (same playerTeam = reduced damage
// and no blade clashes). In FFA every player is NPCTEAM_PLAYER, so clones must
// be on the other side; in team modes they join the owner's side.
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

	G_PlayEffectID( fxClonePoof, origin, up );
	return clone;
}

void Cmd_KageBunshin_f( gentity_t *ent ) {
	gclient_t *client = ent->client;
	static const float sides[] = { -56, 56, -112, 112 };
	int i, spawned = 0;

	if ( !Shinobi_CanAct( ent ) )
		return;
	if ( client->ps.fd.forcePower < SHINOBI_CLONE_COST ) {
		G_Sound( ent, CHAN_AUTO, sndFizzle );
		trap->SendServerCommand( ent - g_entities, va( "cp \"Not enough chakra (%i/%i)\"", client->ps.fd.forcePower, SHINOBI_CLONE_COST ) );
		return;
	}

	Shinobi_RemoveClones( ent->s.number ); // recast replaces the old ones

	for ( i = 0; i < (int)ARRAY_LEN( sides ) && spawned < SHINOBI_CLONE_COUNT; i++ ) {
		vec3_t spot;
		if ( Shinobi_CloneSpot( ent, sides[i], spot ) && Shinobi_SpawnClone( ent, spot ) )
			spawned++;
	}

	if ( !spawned ) {
		G_Sound( ent, CHAN_AUTO, sndFizzle );
		trap->SendServerCommand( ent - g_entities, "cp \"No room for clones\"" );
		return;
	}

	client->ps.fd.forcePower -= SHINOBI_CLONE_COST;
	client->ps.userInt1 = 0;
	G_Sound( ent, CHAN_AUTO, sndPoof );
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
