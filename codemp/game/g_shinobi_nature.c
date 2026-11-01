/*
===========================================================================
JK_Shinobi - elements (chakra natures).

Zones: puddles, burning ground, mud, steam and electrified water lie on the
floor as ET_GENERAL entities (the cgame draws them from s.generic1). They
react when a new zone or a jutsu meets them, and they put statuses on the
players standing in them:

  burning  damage over time, put out by water, steam, map water, getting wet
  wet      fire hurts half, lightning 1.5x, can't catch fire
  muddy    slowed (predicted in PM_ShinobiPreMove)

New jutsu here: Suiton (water bullet), Futon (wind gust), Suiro (water
prison). The element rules themselves are in bg_shinobi.h.
===========================================================================
*/
#include "g_shinobi_local.h"

int shinobiHitElement = SE_NONE;

#define ZONE_TYPE(z)      ( (z)->genericValue1 )
#define ZONE_OWNER(z)     ( (z)->genericValue2 )   // client num + 1 (credit)
#define ZONE_END(z)       ( (z)->genericValue3 )
#define ZONE_REVERT(z)    ( (z)->genericValue4 )   // electric -> water again at
#define ZONE_RADIUS(z)    ( (z)->s.angles2[0] )

#define WATERBALL_LEVEL(b) ( (b)->genericValue2 )
#define FIREBALL_LEVEL(b)  ( (b)->genericValue2 )

void Shinobi_NatureInit( void ) {
	shinobiMedia_t *m = &shinobiMedia;
	int i;

	m->fxSteamBurst = G_EffectIndex( SHINOBI_FX_STEAM_BURST );
	for ( i = 1; i <= SHINOBI_MAX_LEVEL; i++ ) {
		m->fxWaterSplash[i - 1] = Shinobi_LevelEffect( SHINOBI_FX_WATER_SPLASH, i );
		m->fxWindGust[i - 1]    = Shinobi_LevelEffect( SHINOBI_FX_WIND_GUST, i );
		m->fxStorm[i - 1]       = Shinobi_LevelEffect( SHINOBI_FX_STORM, i );
	}
	m->sndHiss        = G_SoundIndex( "sound/chars/remote/misc/hiss.wav" );
	m->sndSplash      = G_SoundIndex( "sound/player/watr_in.wav" );
	m->sndWaterCast   = G_SoundIndex( "sound/player/watr_out.wav" );
	m->sndWind        = G_SoundIndex( "sound/weapons/force/push.wav" );
	m->sndWind2       = G_SoundIndex( "sound/misc/windfly.wav" );
	m->sndIgnite      = G_SoundIndex( "sound/player/fry.wav" );
	m->sndPrison      = G_SoundIndex( "sound/player/watr_un.wav" );
	m->sndGurgle      = G_SoundIndex( "sound/player/gurp1.wav" );
	m->sndFireLoop    = G_SoundIndex( "sound/effects/fire_lp.wav" );
	m->sndCrackleLoop = G_SoundIndex( "sound/effects/energy_crackle.wav" );
}

/*
===========================================================================
Helpers
===========================================================================
*/
qboolean Shinobi_InMapWater( vec3_t pos ) {
	return ( trap->PointContents( pos, ENTITYNUM_NONE ) & ( CONTENTS_WATER | CONTENTS_SLIME ) ) ? qtrue : qfalse;
}

// the floor under 'pos' if it is at most 'maxDrop' below
qboolean Shinobi_FloorBelow( vec3_t pos, float maxDrop, vec3_t floor ) {
	vec3_t  start, end;
	trace_t tr;

	VectorCopy( pos, start );
	start[2] += 4;
	VectorCopy( pos, end );
	end[2] -= maxDrop;
	trap->Trace( &tr, start, NULL, NULL, end, ENTITYNUM_NONE, MASK_SOLID, qfalse, 0, 0 );
	if ( tr.allsolid || tr.fraction >= 1.0f || tr.plane.normal[2] < 0.6f )
		return qfalse;
	VectorCopy( tr.endpos, floor );
	return qtrue;
}

static qboolean Shinobi_IsZone( gentity_t *e ) {
	return ( e->inuse && e->classname && !strcmp( e->classname, "shinobi_zone" ) ) ? qtrue : qfalse;
}

static qboolean Shinobi_ZonesOverlap( gentity_t *z, vec3_t origin, float radius ) {
	vec3_t d;
	VectorSubtract( z->r.currentOrigin, origin, d );
	if ( fabs( d[2] ) > SHINOBI_ZONE_HEIGHT )
		return qfalse;
	d[2] = 0;
	return VectorLength( d ) < ( ZONE_RADIUS( z ) + radius ) * 0.7f ? qtrue : qfalse;
}

static qboolean Shinobi_InZone( gentity_t *z, vec3_t feet ) {
	vec3_t d;
	VectorSubtract( feet, z->r.currentOrigin, d );
	if ( d[2] < -16 || d[2] > SHINOBI_ZONE_HEIGHT )
		return qfalse;
	d[2] = 0;
	return VectorLength( d ) <= ZONE_RADIUS( z ) ? qtrue : qfalse;
}

static void Shinobi_ZoneFree( gentity_t *z ) {
	G_FreeEntity( z );
}

// end a zone now (the cgame fades it out quickly)
static void Shinobi_ZoneKill( gentity_t *z ) {
	z->s.time2 = level.time + 300;
	z->s.loopSound = 0;
	ZONE_END( z ) = 0;
	z->think = Shinobi_ZoneFree;
	z->nextthink = level.time + 300;
	z->classname = "shinobi_zone_dying"; // no more reactions
}

static void Shinobi_ZoneSetType( gentity_t *z, int type, int lifeMs ) {
	ZONE_TYPE( z ) = type;
	z->s.generic1 = SHINOBI_ENT_ZONE + type;
	if ( lifeMs > 0 ) {
		ZONE_END( z ) = level.time + lifeMs;
		z->s.time2 = ZONE_END( z );
	}
	z->s.loopSound = type == SZ_FIRE ? shinobiMedia.sndFireLoop
		: ( type == SZ_ELECTRIC ? shinobiMedia.sndCrackleLoop : 0 );
	trap->LinkEntity( (sharedEntity_t *)z );
}

void Shinobi_SteamAt( vec3_t pos, float radius ) {
	vec3_t up = { 0, 0, 1 }, floor;

	G_PlayEffectID( shinobiMedia.fxSteamBurst, pos, up );
	G_SoundAtLoc( pos, CHAN_AUTO, shinobiMedia.sndHiss );
	if ( radius > 0 && Shinobi_FloorBelow( pos, 96, floor ) )
		Shinobi_SpawnZone( SZ_STEAM, floor, radius, SHINOBI_STEAM_MS, 0 );
}

/*
===========================================================================
Statuses
===========================================================================
*/
static void Shinobi_Ignite( gentity_t *ent, int attackerNum ) {
	gclient_t *client = ent->client;

	if ( !client || ent->s.number >= MAX_CLIENTS || ent->health <= 0 )
		return;
	if ( client->shinobi.wetEnd > level.time || client->shinobi.prisonCaster )
		return; // wet people don't burn
	if ( client->shinobi.burnEnd <= level.time ) {
		client->shinobi.burnTick = level.time + SHINOBI_BURN_TICK_MS;
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndIgnite );
	}
	client->shinobi.burnEnd = level.time + SHINOBI_BURN_MS;
	client->shinobi.burnAttacker = attackerNum;
}

static void Shinobi_Wet( gentity_t *ent ) {
	gclient_t *client = ent->client;

	if ( !client || ent->s.number >= MAX_CLIENTS )
		return;
	client->shinobi.wetEnd = level.time + SHINOBI_WET_MS;
	client->shinobi.burnEnd = 0; // and the fire is out
}

// damage dealt with shinobiHitElement set: element rules for the target
int Shinobi_ElementDamage( gentity_t *targ, gentity_t *attacker, int damage ) {
	const int elem = shinobiHitElement;
	float     f = 1.0f;

	if ( elem == SE_NONE || !targ )
		return damage;

	if ( targ->classname && !strcmp( targ->classname, "shinobi_earthwall" ) ) {
		if ( elem == SE_FIRE )
			f = 0.5f;
		else if ( elem == SE_WATER || elem == SE_WIND )
			f = 0.3f;
	}
	else if ( targ->client && targ->s.number < MAX_CLIENTS ) {
		const qboolean wet = targ->client->shinobi.wetEnd > level.time;

		if ( elem == SE_FIRE ) {
			if ( wet )
				f = 0.5f;
			else
				Shinobi_Ignite( targ, attacker && attacker->client ? attacker->s.number + 1 : 0 );
		}
		else if ( elem == SE_LIGHTNING && wet ) {
			f = 1.5f;
		}
		else if ( elem == SE_WATER ) {
			Shinobi_Wet( targ );
		}
	}

	damage = (int)( damage * f + 0.5f );
	return damage < 1 ? 1 : damage;
}

void Shinobi_StatusClear( gentity_t *ent ) {
	gclient_t *client = ent->client;

	client->shinobi.burnEnd = client->shinobi.wetEnd = client->shinobi.mudEnd = 0;
	if ( client->shinobi.prisonTarget || client->shinobi.prisonCaster )
		Shinobi_PrisonRelease( ent );
	Shinobi_SetRawBit( &client->ps.userVec1[0], 0 );
	Shinobi_SetRawBit( &client->ps.userVec1[1], 0 );
	Shinobi_SetRawBit( &client->ps.userVec1[2], 0 );
	Shinobi_SetRawBit( &client->ps.userVec2[0], 0 );
	Shinobi_SetRawBit( &client->ps.userVec2[1], 0 );
	Shinobi_SetRawBit( &client->ps.userVec2[2], 0 );
	client->shinobi.stunEnd = 0;
}

static void Shinobi_PrisonThink( gentity_t *caster, usercmd_t *ucmd );
static void Shinobi_SpawnWaterBullet( gentity_t *ent );

// every client frame, before Pmove
void Shinobi_StatusThink( gentity_t *ent, usercmd_t *ucmd ) {
	gclient_t *client = ent->client;

	// map water puts the fire out and soaks you
	if ( ent->waterlevel >= 1 )
		Shinobi_Wet( ent );

	if ( client->shinobi.burnEnd > level.time && client->shinobi.burnTick <= level.time ) {
		gentity_t *attacker = client->shinobi.burnAttacker ? &g_entities[client->shinobi.burnAttacker - 1] : ent;
		client->shinobi.burnTick = level.time + SHINOBI_BURN_TICK_MS;
		if ( !attacker->inuse || !attacker->client )
			attacker = ent;
		shinobiHitElement = SE_NONE; // plain damage: must not re-ignite
		G_Damage( ent, attacker, attacker, NULL, NULL, SHINOBI_BURN_DAMAGE, DAMAGE_NO_ARMOR | DAMAGE_NO_KNOCKBACK, MOD_LAVA );
	}

	if ( client->shinobi.prisonTarget )
		Shinobi_PrisonThink( ent, ucmd );
	if ( client->shinobi.prisonCaster ) {
		// held in the water: no moving, no attacking, no force (Kawarimi still works)
		gentity_t *caster = &g_entities[client->shinobi.prisonCaster - 1];
		if ( !caster->inuse || !caster->client || caster->client->shinobi.prisonTarget != ent->s.number + 1 ) {
			Shinobi_PrisonRelease( ent );
		}
		else {
			ucmd->forwardmove = ucmd->rightmove = ucmd->upmove = 0;
			ucmd->buttons &= ~( BUTTON_ATTACK | BUTTON_ALT_ATTACK | BUTTON_USE | BUTTON_FORCEPOWER
				| BUTTON_FORCEGRIP | BUTTON_FORCE_LIGHTNING | BUTTON_FORCE_DRAIN | BUTTON_USE_HOLDABLE );
			client->shinobi.wetEnd = level.time + SHINOBI_WET_MS;
			client->shinobi.burnEnd = 0;
		}
	}
	else if ( client->shinobi.stunEnd > level.time ) { // gasping for air: a free hit for the caster
		ucmd->forwardmove = ucmd->rightmove = ucmd->upmove = 0;
		ucmd->buttons &= ~( BUTTON_ATTACK | BUTTON_ALT_ATTACK | BUTTON_USE | BUTTON_FORCEPOWER
			| BUTTON_FORCEGRIP | BUTTON_FORCE_LIGHTNING | BUTTON_FORCE_DRAIN | BUTTON_USE_HOLDABLE );
	}

	Shinobi_SetRawBit( &client->ps.userVec1[0], client->shinobi.burnEnd > level.time );
	Shinobi_SetRawBit( &client->ps.userVec1[1], client->shinobi.wetEnd > level.time );
	Shinobi_SetRawBit( &client->ps.userVec1[2], client->shinobi.mudEnd > level.time );
	Shinobi_SetRawBit( &client->ps.userVec2[0], client->shinobi.prisonCaster != 0 );
	Shinobi_SetRawBit( &client->ps.userVec2[1], client->shinobi.prisonTarget != 0 );
	Shinobi_SetRawBit( &client->ps.userVec2[2], client->shinobi.stunEnd > level.time );
	if ( client->shinobi.puddleEnd > level.time )
		client->ps.stats[STAT_SHINOBI] |= SHINOBI_STAT_PUDDLE;
	else
		client->ps.stats[STAT_SHINOBI] &= ~SHINOBI_STAT_PUDDLE;

	// the rest of a Water Bullets burst
	if ( client->shinobi.burstLeft > 0 && client->shinobi.burstNext <= level.time ) {
		client->shinobi.burstLeft--;
		client->shinobi.burstNext = level.time + SHINOBI_WATER_BURST_MS;
		if ( !client->shinobi.prisonCaster && ent->health > 0 )
			Shinobi_SpawnWaterBullet( ent );
	}
}

/*
===========================================================================
Zones
===========================================================================
*/
static void Shinobi_ZoneThink( gentity_t *z ) {
	const int type = ZONE_TYPE( z );
	int       i;

	if ( level.time >= ZONE_END( z ) ) {
		Shinobi_ZoneKill( z );
		return;
	}
	if ( type == SZ_ELECTRIC && level.time >= ZONE_REVERT( z ) ) {
		Shinobi_ZoneSetType( z, SZ_WATER, 0 );
	}

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		gentity_t *p = &g_entities[i];
		vec3_t     feet;

		if ( !p->inuse || !p->client || p->health <= 0 || p->client->sess.sessionTeam == TEAM_SPECTATOR )
			continue;
		VectorCopy( p->client->ps.origin, feet );
		feet[2] += p->r.mins[2];
		if ( !Shinobi_InZone( z, feet ) )
			continue;

		switch ( ZONE_TYPE( z ) ) {
		case SZ_FIRE:
			Shinobi_Ignite( p, ZONE_OWNER( z ) );
			break;
		case SZ_WATER:
			Shinobi_Wet( p );
			p->client->shinobi.puddleEnd = level.time + SHINOBI_ZONE_TICK_MS + 150;
			break;
		case SZ_MUD:
			p->client->shinobi.mudEnd = level.time + SHINOBI_ZONE_TICK_MS + 150;
			break;
		case SZ_STEAM:
			p->client->shinobi.burnEnd = 0;
			break;
		case SZ_ELECTRIC: {
			gentity_t *attacker = ZONE_OWNER( z ) ? &g_entities[ZONE_OWNER( z ) - 1] : p;
			if ( !attacker->inuse || !attacker->client )
				attacker = p;
			Shinobi_Wet( p );
			p->client->ps.electrifyTime = level.time + SHINOBI_ELECTRIC_STUN_MS;
			if ( p->client->ps.weaponTime < SHINOBI_ELECTRIC_STUN_MS )
				p->client->ps.weaponTime = SHINOBI_ELECTRIC_STUN_MS;
			shinobiHitElement = SE_LIGHTNING;
			G_Damage( p, attacker, attacker, NULL, NULL, SHINOBI_ELECTRIC_DAMAGE, DAMAGE_NO_ARMOR | DAMAGE_NO_KNOCKBACK, MOD_DEMP2 );
			shinobiHitElement = SE_NONE;
			break;
		}
		default:
			break;
		}
	}

	z->nextthink = level.time + SHINOBI_ZONE_TICK_MS;
}

/*
Spawns a zone and resolves what it meets:
  fire on water / steam / map water   -> no fire, steam
  water on fire                       -> the fire goes out, steam there
  water next to an earth wall         -> mud
*/
gentity_t *Shinobi_SpawnZone( int type, vec3_t origin, float radius, int lifeMs, int ownerNum ) {
	gentity_t *z;
	int        i;

	if ( radius <= 0 || lifeMs <= 0 )
		return NULL;
	if ( radius > SHINOBI_ZONE_MAX_RADIUS )
		radius = SHINOBI_ZONE_MAX_RADIUS;

	if ( type == SZ_FIRE ) {
		vec3_t above;
		VectorCopy( origin, above );
		above[2] += 8;
		if ( Shinobi_InMapWater( above ) ) {
			Shinobi_SteamAt( above, 0 );
			return NULL;
		}
	}

	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *other = &g_entities[i];

		if ( type == SZ_WATER && other->inuse && other->classname && !strcmp( other->classname, "shinobi_earthwall" )
			&& !( other->s.eFlags & EF_NODRAW ) ) {
			vec3_t d;
			VectorSubtract( other->r.currentOrigin, origin, d );
			d[2] = 0;
			if ( VectorLength( d ) < radius + SHINOBI_WALL_HALF_WIDTH )
				type = SZ_MUD; // water soaks into the earth wall's base
			continue;
		}
		if ( !Shinobi_IsZone( other ) || !Shinobi_ZonesOverlap( other, origin, radius ) )
			continue;

		if ( type == SZ_FIRE && ( ZONE_TYPE( other ) == SZ_WATER || ZONE_TYPE( other ) == SZ_ELECTRIC
			|| ZONE_TYPE( other ) == SZ_STEAM ) ) {
			vec3_t above;
			VectorCopy( origin, above );
			above[2] += 16;
			Shinobi_SteamAt( above, radius * 0.8f );
			return NULL;
		}
		if ( ( type == SZ_WATER || type == SZ_MUD ) && ZONE_TYPE( other ) == SZ_FIRE ) {
			vec3_t above;
			VectorCopy( other->r.currentOrigin, above );
			above[2] += 16;
			Shinobi_ZoneKill( other );
			Shinobi_SteamAt( above, ZONE_RADIUS( other ) * 0.8f );
		}
	}

	z = G_Spawn();
	if ( !z )
		return NULL;
	z->classname = "shinobi_zone";
	z->s.eType = ET_GENERAL;
	z->r.contents = 0;
	ZONE_RADIUS( z ) = radius;
	VectorSet( z->r.mins, -radius, -radius, -8 );
	VectorSet( z->r.maxs, radius, radius, SHINOBI_ZONE_HEIGHT );
	G_SetOrigin( z, origin );
	ZONE_OWNER( z ) = ownerNum;
	z->s.time = level.time;
	Shinobi_ZoneSetType( z, type, type == SZ_MUD ? SHINOBI_MUD_MS : lifeMs );
	z->think = Shinobi_ZoneThink;
	z->nextthink = level.time + FRAMETIME;
	trap->LinkEntity( (sharedEntity_t *)z );
	return z;
}

// lightning touching a puddle electrifies it
void Shinobi_ElectrifyAt( vec3_t point, int ownerNum ) {
	int i;

	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *z = &g_entities[i];

		if ( !Shinobi_IsZone( z ) || ( ZONE_TYPE( z ) != SZ_WATER && ZONE_TYPE( z ) != SZ_ELECTRIC ) )
			continue;
		if ( !Shinobi_InZone( z, point ) )
			continue;
		if ( ZONE_TYPE( z ) == SZ_WATER )
			G_SoundAtLoc( z->r.currentOrigin, CHAN_AUTO, shinobiMedia.sndChidoriHit );
		Shinobi_ZoneSetType( z, SZ_ELECTRIC, 0 );
		ZONE_REVERT( z ) = level.time + SHINOBI_ELECTRIC_MS;
		ZONE_OWNER( z ) = ownerNum;
	}
}

// an earth wall rising: puddles under and around it turn to mud
void Shinobi_EarthOnWater( vec3_t center, float radius ) {
	int i;

	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *z = &g_entities[i];
		if ( Shinobi_IsZone( z ) && ( ZONE_TYPE( z ) == SZ_WATER || ZONE_TYPE( z ) == SZ_ELECTRIC )
			&& Shinobi_ZonesOverlap( z, center, radius ) ) {
			Shinobi_ZoneSetType( z, SZ_MUD, SHINOBI_MUD_MS );
		}
	}
}

// a Lv2+ fireball sets the ground on fire where it bursts
void Shinobi_FireLands( vec3_t pos, int lvl, int ownerNum ) {
	vec3_t floor;
	const int li = lvl - 1;

	if ( li < 0 || li >= SHINOBI_MAX_LEVEL || shinobiFireZoneRadius[li] <= 0 )
		return;
	if ( !Shinobi_FloorBelow( pos, 128, floor ) )
		return; // burst in the air or on a wall far above the floor
	Shinobi_SpawnZone( SZ_FIRE, floor, (float)shinobiFireZoneRadius[li], shinobiFireZoneLife[li], ownerNum );
}

/*
===========================================================================
Fireball levels (wind feeds them, water eats them)
===========================================================================
*/
void Shinobi_FireballSetLevel( gentity_t *ball, int lvl, float speedScale ) {
	const int size = shinobiFireballSize[lvl - 1];
	vec3_t    pos;

	BG_EvaluateTrajectory( &ball->s.pos, level.time, pos );
	FIREBALL_LEVEL( ball ) = lvl;
	ball->s.generic1 = SHINOBI_ENT_FIREBALL + lvl - 1;
	VectorSet( ball->r.mins, -size, -size, -size );
	VectorSet( ball->r.maxs, size, size, size );
	VectorCopy( pos, ball->s.pos.trBase );
	VectorCopy( pos, ball->r.currentOrigin );
	ball->s.pos.trTime = level.time;
	VectorScale( ball->s.pos.trDelta, speedScale, ball->s.pos.trDelta );
	trap->LinkEntity( (sharedEntity_t *)ball );
}

/*
===========================================================================
Suiton: water bullet
===========================================================================
*/
static void Shinobi_WaterballImpact( gentity_t *ball, vec3_t pos, vec3_t normal, int hitEnt ) {
	gentity_t *owner = &g_entities[ball->r.ownerNum];
	const int  li = WATERBALL_LEVEL( ball ) - 1;
	vec3_t     dir, floor;

	if ( !owner->inuse || !owner->client )
		owner = ball;
	VectorCopy( ball->s.pos.trDelta, dir );
	VectorNormalize( dir );

	if ( hitEnt >= 0 && hitEnt < ENTITYNUM_WORLD ) {
		gentity_t *other = &g_entities[hitEnt];
		if ( other->takedamage && !Shinobi_DamageBlocked( other, owner ) ) {
			shinobiHitElement = SE_WATER;
			G_Damage( other, ball, owner, dir, pos, shinobiWaterDamage[li], DAMAGE_NO_KNOCKBACK, MOD_WATER );
			shinobiHitElement = SE_NONE;
			if ( other->inuse && other->client && other->health > 0 ) {
				vec3_t push;
				VectorCopy( dir, push );
				push[2] = 0.3f;
				VectorNormalize( push );
				VectorMA( other->client->ps.velocity, shinobiWaterPush[li], push, other->client->ps.velocity );
				other->client->ps.groundEntityNum = ENTITYNUM_NONE;
				other->client->ps.otherKiller = owner->s.number;
				other->client->ps.otherKillerTime = level.time + 5000;
				other->client->ps.otherKillerDebounceTime = level.time + 100;
			}
		}
	}

	Shinobi_EffectDir( shinobiMedia.fxWaterSplash[li], pos, normal );
	G_SoundAtLoc( pos, CHAN_AUTO, shinobiMedia.sndSplash );
	if ( Shinobi_FloorBelow( pos, 128, floor ) )
		Shinobi_SpawnZone( SZ_WATER, floor, (float)shinobiPuddleRadius[li], shinobiPuddleLife[li],
			owner->client ? owner->s.number + 1 : 0 );
	G_FreeEntity( ball );
}

// water bullet meets a fireball: the stronger one goes on, weakened
static qboolean Shinobi_WaterMeetsFire( gentity_t *water, vec3_t waterPos ) {
	int i;

	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *fire = &g_entities[i];
		vec3_t     firePos;
		int        lw, lf;

		if ( !fire->inuse || !fire->classname || strcmp( fire->classname, "shinobi_fireball" ) )
			continue;
		BG_EvaluateTrajectory( &fire->s.pos, level.time, firePos );
		if ( Distance( firePos, waterPos ) > shinobiFireballSize[FIREBALL_LEVEL( fire ) - 1]
			+ shinobiWaterSize[WATERBALL_LEVEL( water ) - 1] + 16 )
			continue;

		lw = WATERBALL_LEVEL( water );
		lf = FIREBALL_LEVEL( fire );
		Shinobi_SteamAt( firePos, 60.0f + 25.0f * lf );
		if ( lw >= lf ) {
			G_FreeEntity( fire );
			if ( lw == lf ) {
				G_FreeEntity( water );
				return qtrue;
			}
			WATERBALL_LEVEL( water ) = lw - lf;
			water->s.generic1 = SHINOBI_ENT_WATERBALL + lw - lf - 1;
		}
		else {
			Shinobi_FireballSetLevel( fire, lf - lw, 1.0f );
			G_FreeEntity( water );
			return qtrue;
		}
	}
	return qfalse;
}

static void Shinobi_WaterballThink( gentity_t *ball ) {
	const int size = shinobiWaterSize[WATERBALL_LEVEL( ball ) - 1];
	vec3_t    newPos, mins, maxs, up = { 0, 0, 1 };
	trace_t   tr;

	BG_EvaluateTrajectory( &ball->s.pos, level.time, newPos );
	if ( Shinobi_WaterMeetsFire( ball, newPos ) )
		return;

	VectorSet( mins, -size, -size, -size );
	VectorSet( maxs, size, size, size );
	trap->Trace( &tr, ball->r.currentOrigin, mins, maxs, newPos, ball->r.ownerNum, MASK_SHOT, qfalse, 0, 0 );
	if ( tr.startsolid || tr.allsolid ) {
		Shinobi_WaterballImpact( ball, ball->r.currentOrigin, up, tr.entityNum );
		return;
	}
	if ( tr.fraction < 1.0f ) {
		Shinobi_WaterballImpact( ball, tr.endpos, tr.plane.normal, tr.entityNum );
		return;
	}
	if ( level.time >= ball->genericValue1 ) {
		Shinobi_WaterballImpact( ball, newPos, up, ENTITYNUM_NONE );
		return;
	}

	VectorCopy( newPos, ball->r.currentOrigin );
	trap->LinkEntity( (sharedEntity_t *)ball );
	ball->nextthink = level.time + FRAMETIME;
}

static void Shinobi_SpawnWaterBullet( gentity_t *ent ) {
	gclient_t *client = ent->client;
	const int  li = 0; // every bullet of a burst is a level 1 bullet
	const int  size = shinobiWaterSize[li];
	gentity_t *ball;
	vec3_t     fwd, eye, start, mins, maxs;
	trace_t    tr;

	AngleVectors( client->ps.viewangles, fwd, NULL, NULL );
	VectorCopy( client->ps.origin, eye );
	eye[2] += client->ps.viewheight;
	VectorMA( eye, 20 + size, fwd, start );
	VectorSet( mins, -size, -size, -size );
	VectorSet( maxs, size, size, size );
	trap->Trace( &tr, eye, mins, maxs, start, ent->s.number, MASK_SOLID, qfalse, 0, 0 );
	VectorCopy( tr.endpos, start );

	ball = G_Spawn();
	if ( !ball )
		return;
	ball->classname = "shinobi_waterball";
	ball->s.eType = ET_GENERAL;
	ball->s.generic1 = SHINOBI_ENT_WATERBALL + li;
	WATERBALL_LEVEL( ball ) = li + 1;
	ball->r.ownerNum = ent->s.number;
	ball->parent = ent;
	VectorCopy( mins, ball->r.mins );
	VectorCopy( maxs, ball->r.maxs );
	ball->r.contents = 0;
	ball->s.pos.trType = TR_LINEAR;
	ball->s.pos.trTime = level.time;
	VectorCopy( start, ball->s.pos.trBase );
	VectorScale( fwd, shinobiWaterSpeed[li], ball->s.pos.trDelta );
	VectorCopy( start, ball->s.origin );
	VectorCopy( start, ball->r.currentOrigin );
	ball->genericValue1 = level.time + SHINOBI_WATER_LIFE;
	ball->think = Shinobi_WaterballThink;
	ball->nextthink = level.time + FRAMETIME;
	trap->LinkEntity( (sharedEntity_t *)ball );

	client->ps.forceHandExtend = HANDEXTEND_FORCEPUSH;
	client->ps.forceHandExtendTime = level.time + 250;
	G_Sound( ent, CHAN_AUTO, shinobiMedia.sndWaterCast );
}

// Water Bullets: 'lvl' bullets in a quick burst (the rest follow from StatusThink)
qboolean Shinobi_CastSuiton( gentity_t *ent, int lvl ) {
	gclient_t *client = ent->client;

	if ( SHINOBI_HAND_ANY( &client->ps ) || client->shinobi.waterNext > level.time || client->shinobi.burstLeft )
		return qfalse;
	Shinobi_SpawnWaterBullet( ent );
	client->shinobi.burstLeft = lvl - 1;
	client->shinobi.burstNext = level.time + SHINOBI_WATER_BURST_MS;
	client->shinobi.waterNext = level.time + SHINOBI_WATER_COOLDOWN + ( lvl - 1 ) * SHINOBI_WATER_BURST_MS;
	return qtrue;
}

// a mass of earth (or a wave) rolling over burning ground puts it out
void Shinobi_SmotherFire( vec3_t pos, float radius, qboolean steam ) {
	int i;

	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *z = &g_entities[i];
		if ( Shinobi_IsZone( z ) && ZONE_TYPE( z ) == SZ_FIRE && Shinobi_ZonesOverlap( z, pos, radius ) ) {
			vec3_t above;
			VectorCopy( z->r.currentOrigin, above );
			above[2] += 16;
			Shinobi_ZoneKill( z );
			if ( steam )
				Shinobi_SteamAt( above, ZONE_RADIUS( z ) * 0.7f );
		}
	}
}

/*
===========================================================================
Water Wave: a low wall of water rolling along the floor
===========================================================================
*/
#define WAVE_LEVEL(w)    ( (w)->genericValue2 )
#define WAVE_END(w)      ( (w)->genericValue3 )   // distance left
#define WAVE_PUDDLE(w)   ( (w)->genericValue4 )   // distance to the next puddle
#define WAVE_HIT(w)      ( (w)->genericValue5 )   // clients already hit (bits)
#define WAVE_YAW(w)      ( (w)->s.angles2[1] )

static void Shinobi_WaveEnd( gentity_t *wave ) {
	const int li = WAVE_LEVEL( wave ) - 1;
	vec3_t    fwd, angles, pos;

	VectorSet( angles, 0, WAVE_YAW( wave ), 0 );
	AngleVectors( angles, fwd, NULL, NULL );
	VectorCopy( wave->r.currentOrigin, pos );
	pos[2] += SHINOBI_WAVE_HEIGHT * 0.5f;
	Shinobi_EffectDir( shinobiMedia.fxWaterSplash[li], pos, fwd );
	G_SoundAtLoc( pos, CHAN_AUTO, shinobiMedia.sndSplash );
	Shinobi_SpawnZone( SZ_WATER, wave->r.currentOrigin, shinobiWaveHalfWidth[li] * 0.9f, shinobiPuddleLife[2],
		wave->r.ownerNum < MAX_CLIENTS ? wave->r.ownerNum + 1 : 0 );
	G_FreeEntity( wave );
}

static void Shinobi_WaveThink( gentity_t *wave ) {
	const int   li = WAVE_LEVEL( wave ) - 1;
	const float half = (float)shinobiWaveHalfWidth[li];
	const float step = SHINOBI_WAVE_SPEED * ( FRAMETIME / 1000.0f );
	gentity_t  *owner = &g_entities[wave->r.ownerNum];
	vec3_t      angles, fwd, right, cur, next, top, bottom, mins, maxs;
	trace_t     tr;
	int         i;

	if ( !owner->inuse || !owner->client )
		owner = wave;
	VectorSet( angles, 0, WAVE_YAW( wave ), 0 );
	AngleVectors( angles, fwd, right, NULL );
	VectorCopy( wave->r.currentOrigin, cur );

	// roll forward; a wall (or an earth wall) breaks the wave
	VectorSet( mins, -8, -8, 8 );
	VectorSet( maxs, 8, 8, SHINOBI_WAVE_HEIGHT - 8 );
	VectorMA( cur, step, fwd, next );
	trap->Trace( &tr, cur, mins, maxs, next, wave->r.ownerNum, MASK_SOLID, qfalse, 0, 0 );
	if ( tr.fraction < 1.0f || tr.startsolid ) {
		Shinobi_WaveEnd( wave );
		return;
	}
	// follow the floor: small steps up and slopes down, not cliffs
	VectorCopy( next, top );
	top[2] += 24;
	VectorCopy( next, bottom );
	bottom[2] -= 48;
	trap->Trace( &tr, top, NULL, NULL, bottom, ENTITYNUM_NONE, MASK_SOLID, qfalse, 0, 0 );
	if ( tr.startsolid || tr.fraction >= 1.0f || tr.plane.normal[2] < 0.6f ) {
		Shinobi_WaveEnd( wave );
		return;
	}
	VectorCopy( tr.endpos, next );

	// puts out fire it rolls over, and the fireballs it meets
	Shinobi_SmotherFire( next, half, qtrue );
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *e = &g_entities[i];
		vec3_t     d, fp;
		if ( !e->inuse || !e->classname || strcmp( e->classname, "shinobi_fireball" ) )
			continue;
		BG_EvaluateTrajectory( &e->s.pos, level.time, fp );
		VectorSubtract( fp, next, d );
		if ( fabs( DotProduct( d, fwd ) ) < 40 && fabs( DotProduct( d, right ) ) < half
			&& d[2] > -16 && d[2] < SHINOBI_WAVE_HEIGHT + 16 ) {
			Shinobi_SteamAt( fp, 60 );
			G_FreeEntity( e );
		}
	}

	// sweep people up
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		gentity_t *p = &g_entities[i];
		vec3_t     d, push;
		float      feet;

		if ( !p->inuse || !p->client || p->health <= 0 || p->client->sess.sessionTeam == TEAM_SPECTATOR )
			continue;
		if ( p == owner || ( WAVE_HIT( wave ) & ( 1 << i ) ) || SHINOBI_HIDDEN( &p->client->ps ) )
			continue;
		if ( level.gametype >= GT_TEAM && OnSameTeam( owner, p ) )
			continue;
		VectorSubtract( p->r.currentOrigin, next, d );
		feet = d[2] + p->r.mins[2];
		if ( DotProduct( d, fwd ) < -24 || DotProduct( d, fwd ) > 32 || fabs( DotProduct( d, right ) ) > half + 16 )
			continue;
		if ( feet > SHINOBI_WAVE_HEIGHT || feet < -32 )
			continue; // jumped over it (or far below)

		WAVE_HIT( wave ) |= ( 1 << i );
		shinobiHitElement = SE_WATER;
		G_Damage( p, wave, owner, fwd, p->r.currentOrigin, shinobiWaveDamage[li], DAMAGE_NO_KNOCKBACK, MOD_WATER );
		shinobiHitElement = SE_NONE;
		if ( p->inuse && p->health > 0 ) {
			VectorScale( fwd, shinobiWavePush[li], push );
			push[2] = 220;
			VectorCopy( push, p->client->ps.velocity );
			p->client->ps.groundEntityNum = ENTITYNUM_NONE;
			p->client->ps.otherKiller = owner->s.number;
			p->client->ps.otherKillerTime = level.time + 5000;
			p->client->ps.otherKillerDebounceTime = level.time + 100;
		}
	}

	// puddles along the way
	WAVE_PUDDLE( wave ) -= (int)step;
	if ( WAVE_PUDDLE( wave ) <= 0 ) {
		WAVE_PUDDLE( wave ) = SHINOBI_WAVE_PUDDLE_STEP;
		Shinobi_SpawnZone( SZ_WATER, next, half * 0.6f, shinobiPuddleLife[1],
			owner->client ? owner->s.number + 1 : 0 );
	}

	WAVE_END( wave ) -= (int)step;
	if ( WAVE_END( wave ) <= 0 ) {
		VectorCopy( next, wave->r.currentOrigin );
		Shinobi_WaveEnd( wave );
		return;
	}

	// the client draws it moving smoothly between server frames
	VectorCopy( next, wave->r.currentOrigin );
	wave->s.pos.trType = TR_LINEAR;
	wave->s.pos.trTime = level.time;
	VectorCopy( next, wave->s.pos.trBase );
	VectorScale( fwd, SHINOBI_WAVE_SPEED, wave->s.pos.trDelta );
	trap->LinkEntity( (sharedEntity_t *)wave );
	wave->nextthink = level.time + FRAMETIME;
}

qboolean Shinobi_CastWave( gentity_t *ent, int lvl ) {
	gclient_t *client = ent->client;
	const int  li = lvl - 1;
	gentity_t *wave;
	vec3_t     angles, fwd, start, floor;

	if ( SHINOBI_HAND_ANY( &client->ps ) || client->shinobi.waterNext > level.time )
		return qfalse;
	VectorSet( angles, 0, client->ps.viewangles[YAW], 0 );
	AngleVectors( angles, fwd, NULL, NULL );
	VectorMA( client->ps.origin, 40, fwd, start );
	if ( !Shinobi_FloorBelow( start, 96, floor ) ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		trap->SendServerCommand( ent - g_entities, "cp \"Water Wave needs ground to roll on\"" );
		return qfalse;
	}
	client->shinobi.waterNext = level.time + SHINOBI_WATER_COOLDOWN;

	wave = G_Spawn();
	if ( !wave )
		return qfalse;
	wave->classname = "shinobi_wave";
	wave->s.eType = ET_GENERAL;
	wave->s.generic1 = SHINOBI_ENT_WAVE + li;
	WAVE_LEVEL( wave ) = lvl;
	WAVE_END( wave ) = shinobiWaveRange[li];
	WAVE_PUDDLE( wave ) = SHINOBI_WAVE_PUDDLE_STEP;
	WAVE_HIT( wave ) = 0;
	wave->s.angles2[0] = (float)shinobiWaveHalfWidth[li];
	WAVE_YAW( wave ) = client->ps.viewangles[YAW];
	wave->r.ownerNum = ent->s.number;
	wave->r.contents = 0;
	VectorSet( wave->r.mins, -shinobiWaveHalfWidth[li], -shinobiWaveHalfWidth[li], 0 );
	VectorSet( wave->r.maxs, shinobiWaveHalfWidth[li], shinobiWaveHalfWidth[li], SHINOBI_WAVE_HEIGHT );
	G_SetOrigin( wave, floor );
	wave->think = Shinobi_WaveThink;
	wave->nextthink = level.time + FRAMETIME;
	trap->LinkEntity( (sharedEntity_t *)wave );

	Shinobi_EffectDir( shinobiMedia.fxWaterSplash[li], floor, fwd );
	G_Sound( ent, CHAN_AUTO, shinobiMedia.sndSplash );
	client->ps.forceHandExtend = HANDEXTEND_FORCEPUSH;
	client->ps.forceHandExtendTime = level.time + 500;
	return qtrue;
}

/*
===========================================================================
Wind Blade: an arc of wind sweeping out from the caster
===========================================================================
*/
#define BLADE_LEVEL(b)   ( (b)->genericValue2 )
#define BLADE_HIT(b)     ( (b)->genericValue5 )
#define BLADE_LAST_R(b)  ( (b)->genericValue6 )
#define BLADE_YAW(b)     ( (b)->s.angles2[0] )

static void Shinobi_BladeThink( gentity_t *blade ) {
	const int   li = BLADE_LEVEL( blade ) - 1;
	const float radius = ( level.time - blade->s.time ) * ( SHINOBI_BLADE_SPEED / 1000.0f );
	const float lastR = (float)BLADE_LAST_R( blade );
	gentity_t  *owner = &g_entities[blade->r.ownerNum];
	vec3_t      angles, fwd, org;
	int         i;

	if ( !owner->inuse || !owner->client )
		owner = blade;
	VectorSet( angles, 0, BLADE_YAW( blade ), 0 );
	AngleVectors( angles, fwd, NULL, NULL );
	VectorCopy( blade->r.currentOrigin, org );

	for ( i = 0; i < level.num_entities; i++ ) {
		gentity_t *p = &g_entities[i];
		vec3_t     d, push;
		float      dist;
		trace_t    tr;

		if ( !p->inuse || !p->client || p == owner || p->health <= 0 )
			continue;
		if ( i < MAX_CLIENTS ) {
			if ( ( BLADE_HIT( blade ) & ( 1 << i ) ) || p->client->sess.sessionTeam == TEAM_SPECTATOR
				|| SHINOBI_HIDDEN( &p->client->ps ) )
				continue;
		}
		if ( Shinobi_DamageBlocked( p, owner ) || ( level.gametype >= GT_TEAM && OnSameTeam( owner, p ) ) )
			continue;

		VectorSubtract( p->r.currentOrigin, org, d );
		if ( fabs( d[2] ) > SHINOBI_BLADE_HALF_HEIGHT )
			continue; // jumped over / below it
		d[2] = 0;
		dist = VectorNormalize( d );
		if ( dist < lastR - 24 || dist > radius + 24 || DotProduct( d, fwd ) < SHINOBI_BLADE_ARC )
			continue;
		trap->Trace( &tr, org, NULL, NULL, p->r.currentOrigin, blade->r.ownerNum, MASK_SOLID, qfalse, 0, 0 );
		if ( tr.fraction < 1.0f && tr.entityNum != p->s.number )
			continue; // behind a wall

		if ( i < MAX_CLIENTS )
			BLADE_HIT( blade ) |= ( 1 << i );
		shinobiHitElement = SE_WIND;
		G_Damage( p, blade, owner, d, p->r.currentOrigin, shinobiBladeDamage[li], DAMAGE_NO_KNOCKBACK, MOD_FORCE_DARK );
		shinobiHitElement = SE_NONE;
		if ( p->inuse && p->client && p->health > 0 ) {
			VectorScale( d, shinobiBladePush[li], push );
			push[2] = 100;
			VectorAdd( p->client->ps.velocity, push, p->client->ps.velocity );
		}
	}

	BLADE_LAST_R( blade ) = (int)radius;
	if ( radius >= shinobiBladeRange[li] ) {
		G_FreeEntity( blade );
		return;
	}
	blade->nextthink = level.time + FRAMETIME;
}

qboolean Shinobi_CastBlade( gentity_t *ent, int lvl ) {
	gclient_t *client = ent->client;
	const int  li = lvl - 1;
	const float range = (float)shinobiBladeRange[li];
	gentity_t *blade;
	vec3_t     angles, fwd, org, fxOrg;

	if ( client->shinobi.bladeNext > level.time )
		return qfalse;
	client->shinobi.bladeNext = level.time + SHINOBI_BLADE_COOLDOWN;

	VectorSet( angles, 0, client->ps.viewangles[YAW], 0 );
	AngleVectors( angles, fwd, NULL, NULL );
	VectorCopy( client->ps.origin, org );
	org[2] += 8; // chest height

	blade = G_Spawn();
	if ( !blade )
		return qfalse;
	blade->classname = "shinobi_blade";
	blade->s.eType = ET_GENERAL;
	blade->s.generic1 = SHINOBI_ENT_BLADE + li;
	BLADE_LEVEL( blade ) = lvl;
	BLADE_YAW( blade ) = client->ps.viewangles[YAW];
	BLADE_HIT( blade ) = 0;
	BLADE_LAST_R( blade ) = 0;
	blade->s.time = level.time;
	blade->r.ownerNum = ent->s.number;
	blade->r.contents = 0;
	blade->r.svFlags |= SVF_BROADCAST; // big, and drawn from its origin
	VectorSet( blade->r.mins, -range, -range, -SHINOBI_BLADE_HALF_HEIGHT );
	VectorSet( blade->r.maxs, range, range, SHINOBI_BLADE_HALF_HEIGHT );
	G_SetOrigin( blade, org );
	blade->think = Shinobi_BladeThink;
	blade->nextthink = level.time + FRAMETIME;
	trap->LinkEntity( (sharedEntity_t *)blade );

	VectorMA( org, 32, fwd, fxOrg );
	Shinobi_EffectDir( shinobiMedia.fxWindGust[li], fxOrg, fwd );
	G_Sound( ent, CHAN_AUTO, shinobiMedia.sndWind );
	client->ps.forceHandExtend = HANDEXTEND_FORCEPUSH;
	client->ps.forceHandExtendTime = level.time + 400;
	return qtrue;
}

/*
===========================================================================
Futon: Daitoppa (great breakthrough): a gust in a cone in front
===========================================================================
*/
static qboolean Shinobi_InCone( vec3_t eye, vec3_t fwd, vec3_t point, float range, float extra ) {
	vec3_t d;
	float  dist;

	VectorSubtract( point, eye, d );
	dist = VectorNormalize( d );
	if ( dist > range + extra )
		return qfalse;
	return ( dist < extra + 32 || DotProduct( d, fwd ) >= SHINOBI_WIND_CONE ) ? qtrue : qfalse;
}

qboolean Shinobi_CastFuton( gentity_t *ent, int lvl ) {
	gclient_t *client = ent->client;
	const int  li = lvl - 1;
	const float range = (float)shinobiWindRange[li];
	vec3_t     angles, fwd, eye, fxOrg;
	int        i;

	if ( client->shinobi.windNext > level.time )
		return qfalse;
	client->shinobi.windNext = level.time + SHINOBI_WIND_COOLDOWN;

	VectorCopy( client->ps.viewangles, angles );
	if ( angles[PITCH] > 30 ) angles[PITCH] = 30;
	if ( angles[PITCH] < -30 ) angles[PITCH] = -30;
	AngleVectors( angles, fwd, NULL, NULL );
	VectorCopy( client->ps.origin, eye );
	eye[2] += client->ps.viewheight * 0.6f;

	for ( i = 0; i < level.num_entities; i++ ) {
		gentity_t *e = &g_entities[i];
		vec3_t     center;
		trace_t    tr;

		if ( !e->inuse || e == ent )
			continue;

		// people (and clones): blown away
		if ( e->client ) {
			vec3_t push;
			if ( e->health <= 0 || ( e->s.number < MAX_CLIENTS && e->client->sess.sessionTeam == TEAM_SPECTATOR ) )
				continue;
			if ( Shinobi_DamageBlocked( e, ent ) || ( level.gametype >= GT_TEAM && OnSameTeam( ent, e ) ) )
				continue;
			if ( e->client->shinobi.prisonCaster )
				continue; // the water holds
			VectorCopy( e->r.currentOrigin, center );
			if ( !Shinobi_InCone( eye, fwd, center, range, 0 ) )
				continue;
			trap->Trace( &tr, eye, NULL, NULL, center, ent->s.number, MASK_SOLID, qfalse, 0, 0 );
			if ( tr.fraction < 1.0f && tr.entityNum != e->s.number )
				continue;

			shinobiHitElement = SE_WIND;
			G_Damage( e, ent, ent, fwd, center, shinobiWindDamage[li], DAMAGE_NO_KNOCKBACK, MOD_FORCE_DARK );
			shinobiHitElement = SE_NONE;
			if ( !e->inuse || !e->client || e->health <= 0 )
				continue;
			VectorCopy( fwd, push );
			push[2] = 0;
			VectorNormalize( push );
			VectorScale( push, shinobiWindPush[li], e->client->ps.velocity );
			e->client->ps.velocity[2] = 120 + shinobiWindPush[li] * 0.15f;
			e->client->ps.groundEntityNum = ENTITYNUM_NONE;
			e->client->ps.otherKiller = ent->s.number;
			e->client->ps.otherKillerTime = level.time + 5000;
			e->client->ps.otherKillerDebounceTime = level.time + 100;
			continue;
		}

		if ( !e->classname )
			continue;

		// fireballs in flight: fed by the wind
		if ( !strcmp( e->classname, "shinobi_fireball" ) ) {
			BG_EvaluateTrajectory( &e->s.pos, level.time, center );
			if ( Shinobi_InCone( eye, fwd, center, range, 0 ) ) {
				const int lf = FIREBALL_LEVEL( e );
				Shinobi_FireballSetLevel( e, lf < SHINOBI_MAX_LEVEL ? lf + 1 : lf, 1.25f );
				e->genericValue1 = level.time + SHINOBI_FIREBALL_LIFE;
			}
			continue;
		}

		if ( Shinobi_IsZone( e ) ) {
			VectorCopy( e->r.currentOrigin, center );
			center[2] += 16;
			if ( !Shinobi_InCone( eye, fwd, center, range, ZONE_RADIUS( e ) ) )
				continue;
			if ( ZONE_TYPE( e ) == SZ_FIRE ) { // fanned flames spread
				float r = ZONE_RADIUS( e ) * 1.35f;
				if ( r > SHINOBI_ZONE_MAX_RADIUS )
					r = SHINOBI_ZONE_MAX_RADIUS;
				ZONE_RADIUS( e ) = r;
				VectorSet( e->r.mins, -r, -r, -8 );
				VectorSet( e->r.maxs, r, r, SHINOBI_ZONE_HEIGHT );
				ZONE_END( e ) += 3000;
				e->s.time2 = ZONE_END( e );
				ZONE_OWNER( e ) = ent->s.number + 1;
				trap->LinkEntity( (sharedEntity_t *)e );
			}
			else if ( ZONE_TYPE( e ) == SZ_STEAM ) { // blown away
				Shinobi_ZoneKill( e );
			}
		}
	}

	VectorMA( eye, 32, fwd, fxOrg );
	Shinobi_EffectDir( shinobiMedia.fxStorm[li], fxOrg, fwd );
	G_Sound( ent, CHAN_AUTO, shinobiMedia.sndWind );
	G_SoundAtLoc( fxOrg, CHAN_AUTO, shinobiMedia.sndWind2 );
	client->ps.forceHandExtend = HANDEXTEND_FORCEPUSH;
	client->ps.forceHandExtendTime = level.time + 500;
	return qtrue;
}

/*
===========================================================================
Suiro no Jutsu: water prison
===========================================================================
*/
static gentity_t *Shinobi_PrisonFindTarget( gentity_t *ent ) {
	gclient_t *client = ent->client;
	vec3_t     fwd, eye, angles;
	gentity_t *best = NULL;
	float      bestScore = 0;
	int        i;

	VectorCopy( client->ps.viewangles, angles );
	angles[PITCH] *= 0.5f;
	AngleVectors( angles, fwd, NULL, NULL );
	VectorCopy( client->ps.origin, eye );
	eye[2] += client->ps.viewheight;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		gentity_t *e = &g_entities[i];
		vec3_t     d;
		float      dist, dot;
		trace_t    tr;

		if ( e == ent || !e->inuse || !e->client || e->health <= 0 )
			continue;
		if ( e->client->sess.sessionTeam == TEAM_SPECTATOR || e->client->tempSpectate >= level.time )
			continue;
		if ( level.gametype >= GT_TEAM && OnSameTeam( ent, e ) )
			continue;
		if ( SHINOBI_HIDDEN( &e->client->ps ) || e->client->shinobi.prisonCaster || e->client->shinobi.prisonTarget )
			continue;

		VectorSubtract( e->r.currentOrigin, eye, d );
		dist = VectorNormalize( d );
		if ( dist > SHINOBI_PRISON_RANGE )
			continue;
		dot = DotProduct( d, fwd );
		if ( dot < 0.6f && dist > 48 )
			continue;
		trap->Trace( &tr, eye, NULL, NULL, e->r.currentOrigin, ent->s.number, MASK_SOLID, qfalse, 0, 0 );
		if ( tr.fraction < 1.0f && tr.entityNum != e->s.number )
			continue;
		if ( !best || dot - dist * 0.001f > bestScore ) {
			best = e;
			bestScore = dot - dist * 0.001f;
		}
	}
	return best;
}

qboolean Shinobi_CastSuiro( gentity_t *ent, int lvl ) {
	gclient_t *client = ent->client;
	gentity_t *target;
	vec3_t     up = { 0, 0, 1 };

	if ( client->shinobi.prisonTarget || SHINOBI_HAND_ANY( &client->ps ) )
		return qfalse;
	if ( client->shinobi.prisonNext > level.time ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		trap->SendServerCommand( ent - g_entities, va( "cp \"Water Prison ready in %.1f s\"",
			( client->shinobi.prisonNext - level.time ) / 1000.0f ) );
		return qfalse;
	}
	target = Shinobi_PrisonFindTarget( ent );
	if ( !target ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		trap->SendServerCommand( ent - g_entities, "cp \"Water Prison: nobody within reach\"" );
		return qfalse;
	}

	client->shinobi.prisonTarget = target->s.number + 1;
	client->shinobi.prisonEnd = 0; // as long as the key is held and the chakra lasts
	client->shinobi.prisonTick = level.time + SHINOBI_PRISON_TICK_MS;
	client->shinobi.prisonNext = level.time + SHINOBI_PRISON_COOLDOWN;

	target->client->shinobi.prisonCaster = ent->s.number + 1;
	target->client->shinobi.chargeJutsu = SJ_NONE;
	target->client->ps.userInt1 = target->client->ps.userInt2 = target->client->ps.userInt3 = 0; // drops a Rasengan / Chidori
	VectorClear( target->client->ps.velocity );
	Shinobi_Wet( target );

	G_PlayEffectID( shinobiMedia.fxWaterSplash[1], target->r.currentOrigin, up );
	G_Sound( target, CHAN_AUTO, shinobiMedia.sndPrison );
	G_Sound( ent, CHAN_AUTO, shinobiMedia.sndWaterCast );
	trap->SendServerCommand( target - g_entities, "cp \"^4Water Prison!^7\nKawarimi gets you out\"" );
	return qtrue;
}

void Shinobi_PrisonRelease( gentity_t *ent ) {
	gclient_t *client = ent->client;
	gentity_t *caster = NULL, *target = NULL;
	vec3_t     up = { 0, 0, 1 };

	if ( client->shinobi.prisonTarget ) {
		caster = ent;
		target = &g_entities[client->shinobi.prisonTarget - 1];
	}
	else if ( client->shinobi.prisonCaster ) {
		target = ent;
		caster = &g_entities[client->shinobi.prisonCaster - 1];
	}
	else {
		return;
	}

	if ( caster->client && caster->client->shinobi.prisonTarget == target->s.number + 1 ) {
		caster->client->shinobi.prisonTarget = 0;
		Shinobi_SetRawBit( &caster->client->ps.userVec2[1], 0 );
		caster->client->ps.forceHandExtend = HANDEXTEND_NONE;
		caster->client->ps.forceDodgeAnim = 0;
	}
	if ( target->client && target->client->shinobi.prisonCaster == caster->s.number + 1 ) {
		target->client->shinobi.prisonCaster = 0;
		Shinobi_SetRawBit( &target->client->ps.userVec2[0], 0 );
		if ( target->health > 0 ) { // a moment to catch your breath (Kawarimi clears it)
			target->client->shinobi.stunEnd = level.time + SHINOBI_PRISON_STUN_MS;
			Shinobi_SetRawBit( &target->client->ps.userVec2[2], 1 );
		}
		if ( target->inuse ) {
			G_PlayEffectID( shinobiMedia.fxWaterSplash[0], target->r.currentOrigin, up );
			G_SoundAtLoc( target->r.currentOrigin, CHAN_AUTO, shinobiMedia.sndSplash );
		}
	}
	client->shinobi.prisonTarget = 0;
	client->shinobi.prisonCaster = 0;
}

// the caster's side, every frame
static void Shinobi_PrisonThink( gentity_t *caster, usercmd_t *ucmd ) {
	gclient_t *client = caster->client;
	gentity_t *target = &g_entities[client->shinobi.prisonTarget - 1];

	if ( !target->inuse || !target->client || target->health <= 0
		|| target->client->shinobi.prisonCaster != caster->s.number + 1
		|| Distance( caster->r.currentOrigin, target->r.currentOrigin ) > SHINOBI_PRISON_RANGE * 1.6f
		|| ( ucmd->buttons & ( BUTTON_ATTACK | BUTTON_ALT_ATTACK | BUTTON_USE ) ) || ucmd->upmove > 0 ) {
		Shinobi_PrisonRelease( caster ); // let go (or it ran out)
		return;
	}

	// hold still with the hand in the water
	ucmd->forwardmove = ucmd->rightmove = 0;

	client->ps.fd.forcePowerRegenDebounceTime = level.time + 500; // no regen while holding

	if ( client->shinobi.prisonTick <= level.time ) {
		client->shinobi.prisonTick = level.time + SHINOBI_PRISON_TICK_MS;
		if ( client->ps.fd.forcePower < SHINOBI_PRISON_DRAIN ) {
			trap->SendServerCommand( caster - g_entities, "cp \"Out of chakra: the Water Prison bursts\"" );
			Shinobi_PrisonRelease( caster );
			return;
		}
		client->ps.fd.forcePower -= SHINOBI_PRISON_DRAIN;
		shinobiHitElement = SE_WATER;
		G_Damage( target, caster, caster, NULL, NULL, SHINOBI_PRISON_DAMAGE, DAMAGE_NO_ARMOR | DAMAGE_NO_KNOCKBACK, MOD_WATER );
		shinobiHitElement = SE_NONE;
		if ( target->inuse && target->health > 0 && Q_irand( 0, 2 ) == 0 )
			G_Sound( target, CHAN_VOICE, shinobiMedia.sndGurgle );
	}
}
