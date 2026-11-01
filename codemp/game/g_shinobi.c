/*
===========================================================================
JK_Shinobi - server-side jutsu logic (chakra charging, Rasengan).

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

void Shinobi_Init( void ) {
	fxRasenganHit    = G_EffectIndex( SHINOBI_FX_RASENGAN_HIT );
	fxRasenganFizzle = G_EffectIndex( SHINOBI_FX_RASENGAN_FIZZLE );
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
