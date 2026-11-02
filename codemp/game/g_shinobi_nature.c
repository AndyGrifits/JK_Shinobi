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
	m->sndCurse       = G_SoundIndex( "sound/weapons/force/rage.wav" );
	m->sndShadow      = G_SoundIndex( "sound/weapons/force/grip.mp3" );
	m->fxCurse        = G_EffectIndex( SHINOBI_FX_CURSE );
	m->fxGates        = G_EffectIndex( SHINOBI_FX_GATES );
	m->sndEyes        = G_SoundIndex( "sound/weapons/force/see.wav" );
	m->sndGates       = G_SoundIndex( "sound/weapons/force/speed.wav" );
	for ( i = 0; i < 3; i++ ) {
		m->sndBlockSaber[i] = G_SoundIndex( va( "sound/weapons/saber/saberblock%d.wav", i + 1 ) );
		m->sndBlockFist[i]  = G_SoundIndex( va( "sound/weapons/melee/punch%d.mp3", i + 1 ) );
	}
	m->sndParry       = G_SoundIndex( "sound/weapons/saber/saberbounce1.wav" );
	m->sndGuardBreak  = G_SoundIndex( "sound/weapons/saber/saberhitwall1.wav" );
	m->fxBlock        = G_EffectIndex( "saber/saber_block" );
	m->fxWindEdge     = G_EffectIndex( SHINOBI_FX_WIND_EDGE );
	m->fxRaiEdge      = G_EffectIndex( SHINOBI_FX_CHIDORI_HAND );
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
void Shinobi_Ignite( gentity_t *ent, int attackerNum ) {
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

void Shinobi_Wet( gentity_t *ent ) {
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
	if ( client->shinobi.flameEnt )
		Shinobi_FlameStop( ent );
	if ( client->shinobi.shadowEnt || client->shinobi.shadowBy )
		Shinobi_ShadowRelease( ent );
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
	if ( client->shinobi.shadowBy )
		Shinobi_ShadowVictimThink( ent, ucmd );
	if ( client->shinobi.prisonCaster ) {
		// held in the water: no moving, no attacking, no force (Kawarimi still works)
		gentity_t *caster = &g_entities[client->shinobi.prisonCaster - 1];
		if ( !caster->inuse || !caster->client || caster->client->shinobi.prisonTarget != ent->s.number + 1 ) {
			Shinobi_PrisonRelease( ent );
		}
		else {
			ucmd->forwardmove = ucmd->rightmove = ucmd->upmove = 0;
			ucmd->generic_cmd = 0;
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
		case SZ_MIST:
			p->client->shinobi.burnEnd = 0;
			break;
		case SZ_ASH:
			if ( p->s.number + 1 != ZONE_OWNER( z ) ) { // the caster breathes through it
				shinobiHitElement = SE_NONE;
				G_Damage( p, z, ZONE_OWNER( z ) ? &g_entities[ZONE_OWNER( z ) - 1] : p, NULL, NULL,
					SHINOBI_ASH_DAMAGE, DAMAGE_NO_ARMOR | DAMAGE_NO_KNOCKBACK, MOD_TRIGGER_HURT );
			}
			break;
		case SZ_ELECTRIC: {
			gentity_t *attacker = ZONE_OWNER( z ) ? &g_entities[ZONE_OWNER( z ) - 1] : p;
			if ( !attacker->inuse || !attacker->client )
				attacker = p;
			Shinobi_Wet( p );
			if ( !Shinobi_CCImmune( p ) ) {
				p->client->ps.electrifyTime = level.time + SHINOBI_ELECTRIC_STUN_MS;
				if ( p->client->ps.weaponTime < SHINOBI_ELECTRIC_STUN_MS )
					p->client->ps.weaponTime = SHINOBI_ELECTRIC_STUN_MS;
				Shinobi_CCGrant( p, level.time + SHINOBI_ELECTRIC_STUN_MS );
			}
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
	if ( radius > SHINOBI_ZONE_MAX_RADIUS && type != SZ_MIST )
		radius = SHINOBI_ZONE_MAX_RADIUS;

	if ( type == SZ_FIRE ) {
		vec3_t above;
		Shinobi_IgniteAshAt( origin, radius, ownerNum ); // fire on ash: boom
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

	Shinobi_IgniteAshAt( point, 24, ownerNum ); // lightning sets ash off too

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
			else if ( ZONE_TYPE( e ) == SZ_STEAM || ZONE_TYPE( e ) == SZ_ASH || ZONE_TYPE( e ) == SZ_MIST ) { // blown away
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
		if ( SHINOBI_HIDDEN( &e->client->ps ) || e->client->shinobi.prisonCaster || e->client->shinobi.prisonTarget
			|| e->client->shinobi.shadowBy || Shinobi_CCImmune( e ) )
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
	if ( ent->s.number >= MAX_CLIENTS && ent->enemy ) {
		// a Water Clone traps the one it is fighting, wherever it faces
		gentity_t *e = ent->enemy;
		if ( e->inuse && e->client && e->s.number < MAX_CLIENTS && e->health > 0
			&& !SHINOBI_HIDDEN( &e->client->ps ) && !e->client->shinobi.prisonCaster && !e->client->shinobi.prisonTarget
			&& !e->client->shinobi.shadowBy && !Shinobi_CCImmune( e )
			&& Distance( ent->r.currentOrigin, e->r.currentOrigin ) <= SHINOBI_PRISON_RANGE + 40 )
			target = e;
	}
	if ( !target ) {
		Shinobi_Debug( "Water Prison by %i: no target (enemy %i)", ent->s.number, ent->enemy ? ent->enemy->s.number : -1 );
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
	Shinobi_Debug( "Water Prison by %i caught %i", ent->s.number, target->s.number );
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
			Shinobi_CCGrant( target, target->client->shinobi.stunEnd );
		}
		if ( target->inuse ) {
			G_PlayEffectID( shinobiMedia.fxWaterSplash[0], target->r.currentOrigin, up );
			G_SoundAtLoc( target->r.currentOrigin, CHAN_AUTO, shinobiMedia.sndSplash );
		}
	}
	client->shinobi.prisonTarget = 0;
	client->shinobi.prisonCaster = 0;
}

void Shinobi_PrisonCasterThink( gentity_t *caster ) {
	usercmd_t none;
	memset( &none, 0, sizeof( none ) );
	Shinobi_PrisonThink( caster, &none );
}

// the caster's side, every frame
static void Shinobi_PrisonThink( gentity_t *caster, usercmd_t *ucmd ) {
	gclient_t *client = caster->client;
	gentity_t *target = &g_entities[client->shinobi.prisonTarget - 1];

	if ( !target->inuse || !target->client || target->health <= 0
		|| target->client->shinobi.prisonCaster != caster->s.number + 1
		|| Distance( caster->r.currentOrigin, target->r.currentOrigin ) > ( SHINOBI_PRISON_RANGE + 40 ) * 1.6f
		|| ( ucmd->buttons & ( BUTTON_ATTACK | BUTTON_ALT_ATTACK | BUTTON_USE ) ) || ucmd->upmove > 0 ) {
		Shinobi_Debug( "Water Prison by %i released: target ok %i, dist %.0f, buttons %i up %i", caster->s.number,
			target->inuse && target->client && target->health > 0 && target->client->shinobi.prisonCaster == caster->s.number + 1,
			Distance( caster->r.currentOrigin, target->r.currentOrigin ), ucmd->buttons, ucmd->upmove );
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

/*
===========================================================================
Ash Cloud: hot ash hanging in the air. Blinds, chokes, and any fire that
touches it (fireball, fire breath, burning ground, Chidori) blows it up.
===========================================================================
*/
#define ZONE_LEVEL(z)    ( (z)->genericValue5 )

static void Shinobi_AshIgnite( gentity_t *z, int igniterNum ) {
	const int  li = ZONE_LEVEL( z ) > 0 ? ZONE_LEVEL( z ) - 1 : 0;
	const float r = ZONE_RADIUS( z );
	gentity_t *attacker = NULL;
	vec3_t     center, up = { 0, 0, 1 };

	if ( igniterNum > 0 )
		attacker = &g_entities[igniterNum - 1];
	else if ( ZONE_OWNER( z ) > 0 )
		attacker = &g_entities[ZONE_OWNER( z ) - 1];
	if ( !attacker || !attacker->inuse )
		attacker = z;

	VectorCopy( z->r.currentOrigin, center );
	center[2] += 32;
	Shinobi_ZoneKill( z ); // first: the blast below must not set it off again

	shinobiHitElement = SE_FIRE;
	G_RadiusDamage( center, attacker, shinobiAshBlast[li], r * 1.2f, NULL, z, MOD_ROCKET_SPLASH );
	shinobiHitElement = SE_NONE;

	Shinobi_EffectDir( shinobiMedia.fxFireballBoom[2], center, up );
	G_SoundAtLoc( center, CHAN_AUTO, shinobiMedia.sndFireballBoom );
	G_SoundAtLoc( center, CHAN_AUTO, shinobiMedia.sndFireballBoom2 );
	G_ScreenShake( center, NULL, 4.0f, 600, qfalse );
	Shinobi_SpawnZone( SZ_FIRE, z->r.currentOrigin, r * 0.7f, 4000, igniterNum );
}

void Shinobi_IgniteAshAt( vec3_t pos, float radius, int igniterNum ) {
	int i;

	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *z = &g_entities[i];
		vec3_t     d;

		if ( !Shinobi_IsZone( z ) || ZONE_TYPE( z ) != SZ_ASH )
			continue;
		VectorSubtract( pos, z->r.currentOrigin, d );
		if ( d[2] < -32 || d[2] > SHINOBI_ZONE_HEIGHT + 64 )
			continue;
		d[2] = 0;
		if ( VectorLength( d ) <= ZONE_RADIUS( z ) + radius )
			Shinobi_AshIgnite( z, igniterNum );
	}
}

qboolean Shinobi_CastAsh( gentity_t *ent, int lvl ) {
	gclient_t *client = ent->client;
	vec3_t     fwd, eye, end, floor;
	trace_t    tr;
	gentity_t *z;

	if ( SHINOBI_HAND_ANY( &client->ps ) )
		return qfalse;
	AngleVectors( client->ps.viewangles, fwd, NULL, NULL );
	VectorCopy( client->ps.origin, eye );
	eye[2] += client->ps.viewheight;
	VectorMA( eye, SHINOBI_ASH_DISTANCE, fwd, end );
	trap->Trace( &tr, eye, NULL, NULL, end, ent->s.number, MASK_SOLID, qfalse, 0, 0 );
	if ( !Shinobi_FloorBelow( tr.endpos, 160, floor ) && !Shinobi_FloorBelow( client->ps.origin, 64, floor ) ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		return qfalse;
	}
	z = Shinobi_SpawnZone( SZ_ASH, floor, (float)shinobiAshRadius[lvl - 1], SHINOBI_ASH_LIFE, ent->s.number + 1 );
	if ( !z )
		return qfalse;
	ZONE_LEVEL( z ) = lvl;

	client->ps.forceHandExtend = HANDEXTEND_TAUNT;
	client->ps.forceDodgeAnim = BOTH_FORCE_2HANDEDLIGHTNING_HOLD;
	client->ps.forceHandExtendTime = level.time + 500;
	G_Sound( ent, CHAN_AUTO, shinobiMedia.sndHiss );
	return qtrue;
}

/*
Hidden Mist: a wide bank of fog around the caster. Inside it everyone is
reduced to vague shapes (cgame); it puts out burning people; Storm Wind
blows it away.
*/
qboolean Shinobi_CastMist( gentity_t *ent, int lvl ) {
	gclient_t *client = ent->client;
	vec3_t     floor;
	gentity_t *z;

	if ( client && SHINOBI_HAND_ANY( &client->ps ) )
		return qfalse;
	if ( !Shinobi_FloorBelow( ent->r.currentOrigin, 160, floor ) ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		return qfalse;
	}
	z = Shinobi_SpawnZone( SZ_MIST, floor, (float)shinobiMistRadius[lvl - 1], shinobiMistLife[lvl - 1], ent->s.number + 1 );
	if ( !z )
		return qfalse;
	ZONE_LEVEL( z ) = lvl;
	z->r.svFlags |= SVF_BROADCAST; // a big bank: visible from afar
	{ // only its maker sees the shapes inside (a Water Clone works for its owner)
		gentity_t *maker = ent;
		if ( ent->s.number >= MAX_CLIENTS && ent->shinobiCloneOwner )
			maker = &g_entities[ent->shinobiCloneOwner - 1];
		z->s.otherEntityNum = maker->s.number;
	}

	if ( client ) {
		client->ps.forceHandExtend = HANDEXTEND_TAUNT;
		client->ps.forceDodgeAnim = BOTH_FORCEHEAL_QUICK;
		client->ps.forceHandExtendTime = level.time + 500;
	}
	G_Sound( ent, CHAN_AUTO, shinobiMedia.sndHiss );
	G_SoundAtLoc( floor, CHAN_AUTO, shinobiMedia.sndWaterCast );
	return qtrue;
}

/*
===========================================================================
Fire Breath: held. The stream turns toward where you look at a limited rate;
it burns people in the cone and sets the ground it touches alight.
===========================================================================
*/
static void Shinobi_FlameEntThink( gentity_t *flame );

void Shinobi_FlameStop( gentity_t *ent ) {
	gclient_t *client = ent->client;

	if ( client->shinobi.flameEnt ) {
		gentity_t *flame = &g_entities[client->shinobi.flameEnt - 1];
		if ( flame->inuse && flame->classname && !strcmp( flame->classname, "shinobi_flame" ) )
			G_FreeEntity( flame );
	}
	client->shinobi.flameEnt = 0;
	client->ps.stats[STAT_SHINOBI] &= ~SHINOBI_STAT_CHANNEL;
	if ( client->ps.forceHandExtend == HANDEXTEND_TAUNT && client->ps.forceDodgeAnim == BOTH_FORCELIGHTNING_HOLD )
		client->ps.forceHandExtend = HANDEXTEND_NONE;
}

qboolean Shinobi_CastFlame( gentity_t *ent ) {
	gclient_t *client = ent->client;
	gentity_t *flame;

	if ( client->shinobi.flameEnt || SHINOBI_HAND_ANY( &client->ps ) )
		return qfalse;
	flame = G_Spawn();
	if ( !flame )
		return qfalse;
	flame->classname = "shinobi_flame";
	flame->s.eType = ET_GENERAL;
	flame->s.generic1 = SHINOBI_ENT_FLAME;
	flame->r.ownerNum = ent->s.number;
	flame->r.contents = 0;
	flame->r.svFlags |= SVF_BROADCAST;
	flame->s.loopSound = shinobiMedia.sndFireLoop;
	G_SetOrigin( flame, client->ps.origin );
	flame->think = Shinobi_FlameEntThink;
	flame->nextthink = level.time + FRAMETIME;
	trap->LinkEntity( (sharedEntity_t *)flame );

	VectorCopy( client->ps.viewangles, client->shinobi.flameAngles );
	client->shinobi.flameAngles[ROLL] = 0;
	client->shinobi.flameTick = level.time;
	client->shinobi.flameDrain = level.time + SHINOBI_FLAME_DRAIN_MS;
	client->shinobi.flameZone = level.time + SHINOBI_FLAME_ZONE_MS;
	client->shinobi.flameEnt = flame->s.number + 1;
	G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFireballCast );
	return qtrue;
}

static void Shinobi_FlameEntThink( gentity_t *flame ) {
	gentity_t  *owner = &g_entities[flame->r.ownerNum];
	gclient_t  *client = owner->client;
	const float step = SHINOBI_FLAME_TURN * ( FRAMETIME / 1000.0f );
	vec3_t      fwd, mouth, end, viewFwd;
	trace_t     tr;
	float       reach;
	int         i;

	if ( !owner->inuse || !client || client->shinobi.flameEnt != flame->s.number + 1 ) {
		G_FreeEntity( flame );
		return;
	}
	if ( owner->health <= 0 || client->shinobi.prisonCaster || client->shinobi.shadowBy
		|| client->shinobi.stunEnd > level.time || !Shinobi_CanAct( owner ) ) {
		Shinobi_FlameStop( owner );
		return;
	}

	// turn slowly toward the view
	for ( i = 0; i < 2; i++ ) {
		float d = AngleSubtract( client->ps.viewangles[i], client->shinobi.flameAngles[i] );
		if ( d > step ) d = step;
		if ( d < -step ) d = -step;
		client->shinobi.flameAngles[i] = AngleNormalize180( client->shinobi.flameAngles[i] + d );
	}
	AngleVectors( client->shinobi.flameAngles, fwd, NULL, NULL );
	AngleVectors( client->ps.viewangles, viewFwd, NULL, NULL );
	VectorCopy( client->ps.origin, mouth );
	mouth[2] += client->ps.viewheight - 6;
	VectorMA( mouth, 10, fwd, mouth );
	VectorMA( mouth, SHINOBI_FLAME_RANGE, fwd, end );
	trap->Trace( &tr, mouth, NULL, NULL, end, owner->s.number, MASK_SOLID, qfalse, 0, 0 );
	reach = tr.fraction * SHINOBI_FLAME_RANGE;

	// chakra
	client->ps.fd.forcePowerRegenDebounceTime = level.time + 500;
	if ( client->shinobi.flameDrain <= level.time ) {
		client->shinobi.flameDrain = level.time + SHINOBI_FLAME_DRAIN_MS;
		if ( client->ps.fd.forcePower < SHINOBI_FLAME_DRAIN ) {
			Shinobi_FlameStop( owner );
			return;
		}
		client->ps.fd.forcePower -= SHINOBI_FLAME_DRAIN;
	}

	// burn what's in the cone
	if ( client->shinobi.flameTick <= level.time ) {
		client->shinobi.flameTick = level.time + SHINOBI_FLAME_TICK_MS;
		for ( i = 0; i < level.num_entities; i++ ) {
			gentity_t *e = &g_entities[i];
			vec3_t     d;
			float      dist;
			trace_t    los;

			if ( !e->inuse || !e->client || e == owner || e->health <= 0 )
				continue;
			if ( Shinobi_DamageBlocked( e, owner ) || ( level.gametype >= GT_TEAM && OnSameTeam( owner, e ) ) )
				continue;
			if ( i < MAX_CLIENTS && SHINOBI_HIDDEN( &e->client->ps ) )
				continue;
			VectorSubtract( e->r.currentOrigin, mouth, d );
			dist = VectorNormalize( d );
			if ( dist > reach + 24 || DotProduct( d, fwd ) < SHINOBI_FLAME_CONE )
				continue;
			trap->Trace( &los, mouth, NULL, NULL, e->r.currentOrigin, owner->s.number, MASK_SOLID, qfalse, 0, 0 );
			if ( los.fraction < 1.0f && los.entityNum != e->s.number )
				continue;
			shinobiHitElement = SE_FIRE;
			G_Damage( e, flame, owner, fwd, e->r.currentOrigin, SHINOBI_FLAME_DAMAGE, DAMAGE_NO_KNOCKBACK, MOD_LAVA );
			shinobiHitElement = SE_NONE;
		}
		// ash anywhere along the stream goes up
		for ( i = 0; i * 40 < reach; i++ ) {
			vec3_t p;
			VectorMA( mouth, i * 40.0f, fwd, p );
			Shinobi_IgniteAshAt( p, 16, owner->s.number + 1 );
		}
	}

	// the ground it licks catches fire (a lake just steams)
	if ( client->shinobi.flameZone <= level.time && tr.fraction < 1.0f ) {
		vec3_t floor;
		client->shinobi.flameZone = level.time + SHINOBI_FLAME_ZONE_MS;
		if ( Shinobi_InMapWater( tr.endpos ) )
			Shinobi_SteamAt( tr.endpos, 0 );
		else if ( tr.plane.normal[2] > 0.6f || Shinobi_FloorBelow( tr.endpos, 64, floor ) ) {
			if ( tr.plane.normal[2] > 0.6f )
				VectorCopy( tr.endpos, floor );
			Shinobi_SpawnZone( SZ_FIRE, floor, 50, 3500, owner->s.number + 1 );
		}
	}

	// keep the pose and the slow walk
	client->ps.forceHandExtend = HANDEXTEND_TAUNT;
	client->ps.forceDodgeAnim = BOTH_FORCELIGHTNING_HOLD;
	client->ps.forceHandExtendTime = level.time + 150;
	client->ps.stats[STAT_SHINOBI] |= SHINOBI_STAT_CHANNEL;

	// the cgame draws the stream from these
	G_SetOrigin( flame, mouth );
	VectorCopy( client->shinobi.flameAngles, flame->s.angles2 );
	flame->s.angles2[ROLL] = reach;
	trap->LinkEntity( (sharedEntity_t *)flame );
	flame->nextthink = level.time + FRAMETIME;
}

/*
===========================================================================
Wind Wall (Futon): a wall of wind ahead. Fireballs, water bullets and kunai
flying into its face go back at whoever threw them.
===========================================================================
*/
#define WINDWALL_LEVEL(w) ( (w)->count )

static void Shinobi_WindWallReflect( gentity_t *w, gentity_t *p, const vec3_t at, const vec3_t face ) {
	gentity_t *owner = &g_entities[w->r.ownerNum];
	gentity_t *thrower = ( p->r.ownerNum < ENTITYNUM_WORLD ) ? &g_entities[p->r.ownerNum] : NULL;
	vec3_t     vel, aim;
	float      speed;

	BG_EvaluateTrajectoryDelta( &p->s.pos, level.time, vel );
	speed = VectorLength( vel );
	if ( speed < 100 )
		speed = 100;
	if ( thrower && thrower->inuse && thrower->health > 0 && thrower != owner ) {
		vec3_t target;
		VectorCopy( thrower->r.currentOrigin, target );
		target[2] += 8;
		VectorSubtract( target, at, aim );
		VectorNormalize( aim );
	}
	else { // mirror it off the wall
		const float dn = DotProduct( vel, face );
		VectorMA( vel, -2.0f * dn, face, aim );
		VectorNormalize( aim );
	}

	p->s.pos.trTime = level.time;
	VectorCopy( at, p->s.pos.trBase );
	VectorScale( aim, speed, p->s.pos.trDelta );
	VectorCopy( at, p->r.currentOrigin );
	p->r.ownerNum = owner->s.number;
	p->parent = owner;
	p->enemy = w; // caught once

	if ( !strcmp( p->classname, "shinobi_fireball" ) ) {
		const int lf = p->genericValue2;
		Shinobi_FireballSetLevel( p, lf < SHINOBI_MAX_LEVEL ? lf + 1 : lf, 1.0f ); // the wind feeds it
		p->genericValue1 = level.time + SHINOBI_FIREBALL_LIFE;
		VectorScale( aim, VectorLength( p->s.pos.trDelta ), p->s.pos.trDelta );
	}
	else if ( !strcmp( p->classname, "shinobi_waterball" ) ) {
		p->genericValue1 = level.time + SHINOBI_WATER_LIFE;
	}
	else {
		p->s.pos.trDelta[2] += 40; // kunai arc
		Shinobi_KunaiReflect( p, owner );
	}
	trap->LinkEntity( (sharedEntity_t *)p );
	Shinobi_EffectDir( shinobiMedia.fxWindGust[0], (float *)at, aim );
	G_SoundAtLoc( (float *)at, CHAN_AUTO, shinobiMedia.sndWind );
}

static void Shinobi_WindWallThink( gentity_t *w ) {
	const float halfW = w->s.angles2[1];
	vec3_t      face, right, yaw;
	int         i;

	if ( level.time >= w->s.time2 ) {
		G_FreeEntity( w );
		return;
	}
	VectorSet( yaw, 0, w->s.angles2[0], 0 );
	AngleVectors( yaw, face, right, NULL ); // face points away from the caster

	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *p = &g_entities[i];
		vec3_t     d, vel;
		float      along, depth;

		if ( !p->inuse || !p->classname || p->enemy == w || p->r.ownerNum == w->r.ownerNum )
			continue;
		if ( strcmp( p->classname, "shinobi_fireball" ) && strcmp( p->classname, "shinobi_waterball" )
			&& strcmp( p->classname, "shinobi_kunai" ) )
			continue;
		if ( p->s.pos.trType == TR_STATIONARY )
			continue; // a kunai stuck somewhere
		VectorSubtract( p->r.currentOrigin, w->r.currentOrigin, d );
		if ( d[2] < -24 || d[2] > SHINOBI_WINDWALL_HEIGHT )
			continue;
		along = DotProduct( d, right );
		depth = DotProduct( d, face );
		if ( along < -halfW || along > halfW || depth < -SHINOBI_WINDWALL_DEPTH || depth > SHINOBI_WINDWALL_DEPTH * 2 )
			continue;
		BG_EvaluateTrajectoryDelta( &p->s.pos, level.time, vel );
		if ( DotProduct( vel, face ) >= 0 )
			continue; // flying away from the caster: let it be
		Shinobi_WindWallReflect( w, p, p->r.currentOrigin, face );
	}
	w->nextthink = level.time + FRAMETIME;
}

qboolean Shinobi_CastWindWall( gentity_t *ent, int lvl ) {
	gclient_t *client = ent->client;
	gentity_t *w;
	vec3_t     fwd, yaw, pos, floor;

	if ( SHINOBI_HAND_ANY( &client->ps ) )
		return qfalse;
	if ( client->shinobi.windWallNext > level.time ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		trap->SendServerCommand( ent - g_entities, va( "cp \"Wind Wall ready in %.1f s\"",
			( client->shinobi.windWallNext - level.time ) / 1000.0f ) );
		return qfalse;
	}
	VectorSet( yaw, 0, client->ps.viewangles[YAW], 0 );
	AngleVectors( yaw, fwd, NULL, NULL );
	VectorMA( client->ps.origin, SHINOBI_WINDWALL_DISTANCE, fwd, pos );
	if ( Shinobi_FloorBelow( pos, 96, floor ) )
		VectorCopy( floor, pos );
	else
		pos[2] += ent->r.mins[2];

	w = G_Spawn();
	if ( !w )
		return qfalse;
	w->classname = "shinobi_windwall";
	w->s.eType = ET_GENERAL;
	w->s.generic1 = SHINOBI_ENT_WINDWALL;
	w->s.angles2[0] = client->ps.viewangles[YAW];
	w->s.angles2[1] = (float)shinobiWindWallHalfW[lvl - 1];
	w->s.time = level.time;
	w->s.time2 = level.time + shinobiWindWallLife[lvl - 1];
	WINDWALL_LEVEL( w ) = lvl;
	w->r.ownerNum = ent->s.number;
	w->r.contents = 0;
	w->r.svFlags |= SVF_BROADCAST;
	G_SetOrigin( w, pos );
	w->think = Shinobi_WindWallThink;
	w->nextthink = level.time + FRAMETIME;
	trap->LinkEntity( (sharedEntity_t *)w );

	client->shinobi.windWallNext = level.time + SHINOBI_WINDWALL_COOLDOWN;
	client->ps.forceHandExtend = HANDEXTEND_FORCEPUSH;
	client->ps.forceHandExtendTime = level.time + 400;
	G_Sound( ent, CHAN_AUTO, shinobiMedia.sndWind );
	G_SoundAtLoc( pos, CHAN_AUTO, shinobiMedia.sndWind2 );
	return qtrue;
}
