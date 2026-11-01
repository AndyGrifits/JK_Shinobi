/*
===========================================================================
JK_Shinobi - server-side jutsu core: media, chakra charging, hand jutsu
(Rasengan / Chidori) and Shunshin.

Flags that the client predicts or renders live in playerState user fields
(see bg_shinobi.h). Timers live in gclient_t->shinobi.

Other jutsu: g_shinobi_clones.c (Kage Bunshin), g_shinobi_elements.c
(Katon fireball, Doton earth wall).
===========================================================================
*/
#include "g_shinobi_local.h"

extern void G_Knockdown( gentity_t *victim ); // g_combat.c (AnimalNPC.c has an unrelated static one)

shinobiMedia_t shinobiMedia;

void Shinobi_Init( void ) {
	shinobiMedia_t *m = &shinobiMedia;

	m->fxRasenganHit    = G_EffectIndex( SHINOBI_FX_RASENGAN_HIT );
	m->fxRasenganFizzle = G_EffectIndex( SHINOBI_FX_RASENGAN_FIZZLE );
	m->fxChidoriHit     = G_EffectIndex( SHINOBI_FX_CHIDORI_HIT );
	m->fxChidoriFizzle  = G_EffectIndex( SHINOBI_FX_CHIDORI_FIZZLE );
	m->fxShunshin       = G_EffectIndex( SHINOBI_FX_SHUNSHIN );
	m->fxClonePoof      = G_EffectIndex( SHINOBI_FX_CLONE_POOF );

	// Sounds from the base game, reused until we have our own.
	m->sndChargeStart  = G_SoundIndex( "sound/weapons/force/jumpbuild.wav" );
	m->sndFizzle       = G_SoundIndex( "sound/weapons/force/noforce.wav" );
	m->sndPoof         = G_SoundIndex( "sound/weapons/force/distract.wav" );
	m->sndRasenganForm = G_SoundIndex( "sound/weapons/force/drain.wav" );
	m->sndRasenganDash = G_SoundIndex( "sound/weapons/force/speed.wav" );
	m->sndRasenganHit  = G_SoundIndex( "sound/weapons/force/push.wav" );
	m->sndChidoriForm  = G_SoundIndex( "sound/weapons/force/lightning" );
	m->sndChidoriDash  = G_SoundIndex( "sound/weapons/force/speed.wav" );
	m->sndChidoriHit   = G_SoundIndex( "sound/weapons/force/lightninghit1" );

	Shinobi_ElementsInit();

	trap->Print( "[shinobi] game module built " __DATE__ " " __TIME__ ": fx rasenganHit %i chidoriHit %i shunshin %i clonePoof %i fireballBoom %i wallRise %i, wall models %i %i %i\n",
		m->fxRasenganHit, m->fxChidoriHit, m->fxShunshin, m->fxClonePoof, m->fxFireballBoom, m->fxWallRise,
		m->wallModels[0], m->wallModels[1], m->wallModels[2] );
}

/*
===========================================================================
Common helpers
===========================================================================
*/
qboolean Shinobi_CanAct( gentity_t *ent ) {
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

// userInt1 doubles as the Chidori marker while a hand jutsu exists
void Shinobi_StopCharging( gclient_t *client ) {
	if ( !SHINOBI_HAND_ANY( &client->ps ) )
		client->ps.userInt1 = 0;
}

qboolean Shinobi_SpendChakra( gentity_t *ent, int cost ) {
	gclient_t *client = ent->client;

	if ( client->ps.fd.forcePower < cost ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		trap->SendServerCommand( ent - g_entities, va( "cp \"Not enough chakra (%i/%i)\"", client->ps.fd.forcePower, cost ) );
		return qfalse;
	}
	client->ps.fd.forcePower -= cost;
	return qtrue;
}

/*
===========================================================================
Hand jutsu: Rasengan and Chidori share one state machine (form -> armed ->
dash strike), they differ only in numbers and effects.
===========================================================================
*/
typedef enum {
	HAND_RASENGAN,
	HAND_CHIDORI
} handJutsuType_t;

typedef struct {
	int      cost, formMs, holdMs, dashMs;
	int      damage, throwSpeed, stunMs;
	qboolean knockdown;
	int     *fxHit, *fxFizzle;
	int     *sndForm, *sndDash, *sndHit;
} handJutsu_t;

static const handJutsu_t handJutsu[] = {
	{ // HAND_RASENGAN: heavy knockback and knockdown
		SHINOBI_RASENGAN_COST, SHINOBI_RASENGAN_FORM_MS, SHINOBI_RASENGAN_HOLD_MS, SHINOBI_RASENGAN_DASH_MS,
		SHINOBI_RASENGAN_DAMAGE, SHINOBI_RASENGAN_THROW, 0, qtrue,
		&shinobiMedia.fxRasenganHit, &shinobiMedia.fxRasenganFizzle,
		&shinobiMedia.sndRasenganForm, &shinobiMedia.sndRasenganDash, &shinobiMedia.sndRasenganHit
	},
	{ // HAND_CHIDORI: faster dash, more damage, electrocutes instead of knocking down
		SHINOBI_CHIDORI_COST, SHINOBI_CHIDORI_FORM_MS, SHINOBI_CHIDORI_HOLD_MS, SHINOBI_CHIDORI_DASH_MS,
		SHINOBI_CHIDORI_DAMAGE, SHINOBI_CHIDORI_THROW, SHINOBI_CHIDORI_STUN_MS, qfalse,
		&shinobiMedia.fxChidoriHit, &shinobiMedia.fxChidoriFizzle,
		&shinobiMedia.sndChidoriForm, &shinobiMedia.sndChidoriDash, &shinobiMedia.sndChidoriHit
	},
};

static const handJutsu_t *Shinobi_HandJutsu( playerState_t *ps ) {
	return &handJutsu[SHINOBI_HAND_IS_CHIDORI( ps ) ? HAND_CHIDORI : HAND_RASENGAN];
}

static void Shinobi_ClearHand( gclient_t *client ) {
	client->ps.userInt1 = 0; // also clears the Chidori marker
	client->ps.userInt2 = 0;
	client->ps.userInt3 = 0;
	client->shinobi.formEnd = 0;
	client->shinobi.armedEnd = 0;
	client->shinobi.dashEnd = 0;
	client->shinobi.handSoundNext = 0;
}

static void Shinobi_HandFizzle( gentity_t *ent ) {
	const handJutsu_t *hj = Shinobi_HandJutsu( &ent->client->ps );
	vec3_t up = { 0, 0, 1 };
	vec3_t org;

	VectorCopy( ent->client->ps.origin, org );
	org[2] += 16;
	G_PlayEffectID( *hj->fxFizzle, org, up );
	Shinobi_ClearHand( ent->client );
}

static void Shinobi_StartHandJutsu( gentity_t *ent, handJutsuType_t type ) {
	gclient_t         *client = ent->client;
	const handJutsu_t *hj = &handJutsu[type];

	if ( !Shinobi_CanAct( ent ) )
		return;
	if ( SHINOBI_HAND_ANY( &client->ps ) )
		return;
	if ( !Shinobi_SpendChakra( ent, hj->cost ) )
		return;

	client->ps.userInt1 = ( type == HAND_CHIDORI ) ? 1 : 0; // charging stops, marker set
	client->ps.userInt2 = 0;
	client->ps.userInt3 = 1; // forming
	client->shinobi.formEnd = level.time + hj->formMs;
	client->shinobi.handSoundNext = level.time + 900;
	G_Sound( ent, CHAN_AUTO, *hj->sndForm );
}

void Cmd_Rasengan_f( gentity_t *ent ) {
	Shinobi_StartHandJutsu( ent, HAND_RASENGAN );
}

void Cmd_Chidori_f( gentity_t *ent ) {
	Shinobi_StartHandJutsu( ent, HAND_CHIDORI );
}

static gentity_t *Shinobi_FindDashTarget( gentity_t *ent, vec3_t fwd, vec3_t hitPoint ) {
	int       touch[MAX_GENTITIES];
	int       num, i;
	vec3_t    center, mins, maxs;
	gentity_t *best = NULL;
	float     bestDist = 0;

	VectorMA( ent->client->ps.origin, SHINOBI_HAND_REACH, fwd, center );
	VectorSet( mins, center[0] - SHINOBI_HAND_HITBOX, center[1] - SHINOBI_HAND_HITBOX, center[2] - SHINOBI_HAND_HITBOX );
	VectorSet( maxs, center[0] + SHINOBI_HAND_HITBOX, center[1] + SHINOBI_HAND_HITBOX, center[2] + SHINOBI_HAND_HITBOX );

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
		if ( Shinobi_DamageBlocked( other, ent ) )
			continue; // our own shadow clones

		// don't hit through walls
		trap->Trace( &tr, ent->client->ps.origin, NULL, NULL, other->r.currentOrigin, ent->s.number, MASK_SOLID, qfalse, 0, 0 );
		if ( tr.fraction < 1.0f )
			continue;

		dist = Distance( ent->client->ps.origin, other->r.currentOrigin );
		if ( !best || dist < bestDist ) {
			best = other;
			bestDist = dist;
		}
	}

	if ( best ) {
		VectorAdd( ent->client->ps.origin, best->r.currentOrigin, hitPoint );
		VectorScale( hitPoint, 0.5f, hitPoint );
		hitPoint[2] += 12;
	}
	return best;
}

static void Shinobi_HandHit( gentity_t *ent, gentity_t *victim, vec3_t fwd, vec3_t point ) {
	const handJutsu_t *hj = Shinobi_HandJutsu( &ent->client->ps );
	vec3_t dir;

	VectorCopy( fwd, dir );
	dir[2] = 0.35f;
	VectorNormalize( dir );

	G_Damage( victim, ent, ent, dir, point, hj->damage, DAMAGE_NO_KNOCKBACK, MOD_MELEE );

	if ( victim->inuse && victim->client && victim->health > 0 ) {
		gclient_t *vc = victim->client;

		VectorScale( dir, hj->throwSpeed, vc->ps.velocity );
		vc->ps.groundEntityNum = ENTITYNUM_NONE;
		// credit the kill if they hit a wall or fall off a ledge
		vc->ps.otherKiller = ent->s.number;
		vc->ps.otherKillerTime = level.time + 5000;
		vc->ps.otherKillerDebounceTime = level.time + 100;

		if ( hj->knockdown ) {
			G_Knockdown( victim );
		}
		if ( hj->stunMs ) {
			// electrocuted: electric shell on the model, no attacks, no control for a moment
			vc->ps.electrifyTime = level.time + hj->stunMs;
			if ( vc->ps.weaponTime < hj->stunMs )
				vc->ps.weaponTime = hj->stunMs;
			vc->ps.pm_flags |= PMF_TIME_KNOCKBACK;
			vc->ps.pm_time = hj->stunMs / 2;
		}
	}

	G_PlayEffectID( *hj->fxHit, point, dir );
	G_SoundAtLoc( point, CHAN_AUTO, *hj->sndHit );
	Shinobi_ClearHand( ent->client );
}

static void Shinobi_HandThink( gentity_t *ent, qboolean attackPressed ) {
	gclient_t         *client = ent->client;
	const handJutsu_t *hj;

	if ( !SHINOBI_HAND_ANY( &client->ps ) )
		return;
	hj = Shinobi_HandJutsu( &client->ps );

	// Chidori keeps chirping while it exists
	if ( SHINOBI_HAND_IS_CHIDORI( &client->ps ) && client->shinobi.handSoundNext <= level.time ) {
		G_Sound( ent, CHAN_AUTO, *hj->sndForm );
		client->shinobi.handSoundNext = level.time + 900;
	}

	if ( SHINOBI_HAND_FORMING( &client->ps ) ) {
		if ( level.time >= client->shinobi.formEnd ) {
			client->ps.userInt3 = 0;
			client->ps.userInt2 = 1; // armed
			client->shinobi.armedEnd = level.time + hj->holdMs;
		}
		return;
	}

	if ( SHINOBI_HAND_ARMED( &client->ps ) ) {
		if ( attackPressed ) {
			client->ps.userInt3 = 1; // armed + forming bits = dash
			client->shinobi.dashEnd = level.time + hj->dashMs;
			G_Sound( ent, CHAN_BODY, *hj->sndDash );
		}
		else if ( level.time >= client->shinobi.armedEnd ) {
			Shinobi_HandFizzle( ent );
		}
		return;
	}

	if ( SHINOBI_HAND_DASH( &client->ps ) ) {
		vec3_t     yaw, fwd, point;
		gentity_t *victim;

		VectorSet( yaw, 0, client->ps.viewangles[YAW], 0 );
		AngleVectors( yaw, fwd, NULL, NULL );

		victim = Shinobi_FindDashTarget( ent, fwd, point );
		if ( victim ) {
			Shinobi_HandHit( ent, victim, fwd, point );
		}
		else if ( level.time >= client->shinobi.dashEnd ) {
			Shinobi_HandFizzle( ent );
		}
	}
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
		Shinobi_ClearHand( client );
		return;
	}

	attackPressed = ( ucmd->buttons & BUTTON_ATTACK ) && !( client->buttons & BUTTON_ATTACK );

	// --- Chakra charging: hold USE while standing still on the ground ---
	if ( ( ucmd->buttons & BUTTON_USE )
		&& !ucmd->forwardmove && !ucmd->rightmove && ucmd->upmove <= 0
		&& client->ps.groundEntityNum != ENTITYNUM_NONE
		&& !SHINOBI_HAND_ANY( &client->ps )
		&& client->ps.weaponTime <= 0 ) {
		if ( !client->ps.userInt1 ) {
			client->ps.userInt1 = 1;
			client->shinobi.chargeTick = level.time + SHINOBI_CHARGE_TICK_MS;
			G_Sound( ent, CHAN_AUTO, shinobiMedia.sndChargeStart );
			// meditate with the blade off, like the stock /meditate taunt
			if ( client->ps.weapon == WP_SABER && client->ps.saberHolstered != 2 ) {
				G_Sound( ent, CHAN_WEAPON, client->saber[0].soundOff );
				client->ps.saberHolstered = 2;
			}
		}
		while ( client->shinobi.chargeTick <= level.time ) {
			client->ps.fd.forcePower += SHINOBI_CHARGE_PER_TICK;
			client->shinobi.chargeTick += SHINOBI_CHARGE_TICK_MS;
		}
		if ( client->ps.fd.forcePower > client->ps.fd.forcePowerMax )
			client->ps.fd.forcePower = client->ps.fd.forcePowerMax;
	}
	else {
		Shinobi_StopCharging( client );
	}

	Shinobi_HandThink( ent, attackPressed );
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
	if ( SHINOBI_HAND_DASH( &client->ps ) )
		return;

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
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		return;
	}
	if ( !Shinobi_SpendChakra( ent, SHINOBI_SHUNSHIN_COST ) )
		return;

	client->shinobi.shunshinNext = level.time + SHINOBI_SHUNSHIN_COOLDOWN;
	Shinobi_StopCharging( client );

	// smoke and leaves where we were, and where we land
	G_PlayEffectID( shinobiMedia.fxShunshin, client->ps.origin, dir );
	G_SoundAtLoc( client->ps.origin, CHAN_AUTO, shinobiMedia.sndPoof );

	trap->UnlinkEntity( (sharedEntity_t *)ent );
	VectorCopy( tr.endpos, client->ps.origin );
	if ( client->ps.velocity[2] < 0 )
		client->ps.velocity[2] = 0; // don't arrive mid-fall at full speed
	client->ps.eFlags ^= EF_TELEPORT_BIT; // tell clients not to lerp
	BG_PlayerStateToEntityState( &client->ps, &ent->s, qtrue );
	VectorCopy( client->ps.origin, ent->r.currentOrigin );
	trap->LinkEntity( (sharedEntity_t *)ent );

	G_PlayEffectID( shinobiMedia.fxShunshin, client->ps.origin, dir );
}
