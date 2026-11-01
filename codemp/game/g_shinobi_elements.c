/*
===========================================================================
JK_Shinobi - elemental ninjutsu.

  Katon: Goukakyuu no Jutsu  ("katon")  great fireball, explodes on impact
  Doton: Doryuheki           ("doton")  stone pillars rise into a wall

Both scale with the jutsu level (1-3, see bg_shinobi.h tables).

The fireball is a plain ET_GENERAL entity with no model; the cgame
recognises it by s.generic1 (SHINOBI_ENT_FIREBALL + level - 1) and draws the
ball itself. Wall pillars are md3 models with a solid bounding box; a hidden
controller entity owns the wall's shared health.
===========================================================================
*/
#include "g_shinobi_local.h"

extern void G_Knockdown( gentity_t *victim ); // g_combat.c

void Shinobi_ElementsInit( void ) {
	shinobiMedia_t *m = &shinobiMedia;
	int i;

	for ( i = 1; i <= SHINOBI_MAX_LEVEL; i++ ) {
		m->fxFireballBoom[i - 1] = Shinobi_LevelEffect( SHINOBI_FX_FIREBALL_BOOM, i );
	}
	m->fxWallRise  = G_EffectIndex( SHINOBI_FX_WALL_RISE );
	m->fxWallBreak = G_EffectIndex( SHINOBI_FX_WALL_BREAK );

	m->sndFireballCast = G_SoundIndex( "sound/effects/combustfire.mp3" );
	m->sndFireballBoom = G_SoundIndex( "sound/weapons/explosions/cargoexplode.wav" );
	m->sndFireballBoom2 = G_SoundIndex( "sound/weapons/explosions/explode5.wav" );
	m->sndLog          = G_SoundIndex( "sound/weapons/force/distract.wav" );
	m->logModel        = G_ModelIndex( SHINOBI_LOG_MODEL );
	m->sndWallRise     = G_SoundIndex( "sound/effects/wall_smash" );
	m->sndWallBreak    = G_SoundIndex( "sound/effects/stone_bounce" );

	for ( i = 0; i < SHINOBI_WALL_MODELS; i++ ) {
		m->wallModels[i] = G_ModelIndex( va( "models/shinobi/earth_pillar%i.md3", i + 1 ) );
	}
}

/*
===========================================================================
Katon: great fireball
===========================================================================
*/
#define FIREBALL_LEVEL(ball) ( (ball)->genericValue2 ) // 1..3

static void Shinobi_FireballExplode( gentity_t *ball, vec3_t pos, vec3_t normal, int hitEnt ) {
	gentity_t *owner = &g_entities[ball->r.ownerNum];
	const int  li = FIREBALL_LEVEL( ball ) - 1;
	vec3_t     dir;

	if ( !owner->inuse || !owner->client )
		owner = ball;

	VectorCopy( ball->s.pos.trDelta, dir );
	VectorNormalize( dir );

	shinobiHitElement = SE_FIRE; // sets people on fire, half damage to wet ones and earth walls
	if ( hitEnt >= 0 && hitEnt < ENTITYNUM_WORLD ) {
		gentity_t *other = &g_entities[hitEnt];
		if ( other->takedamage ) {
			G_Damage( other, ball, owner, dir, pos, shinobiFireballDamage[li], 0, MOD_ROCKET );
		}
	}
	// the caster is immune to their own blast
	G_RadiusDamage( pos, owner, shinobiFireballSplash[li], shinobiFireballRadius[li], owner, ball, MOD_ROCKET_SPLASH );
	shinobiHitElement = SE_NONE;

	// a big one leaves the ground burning (or steaming, on water)
	Shinobi_FireLands( pos, li + 1, owner->client ? owner->s.number + 1 : 0 );
	Shinobi_IgniteAshAt( pos, shinobiFireballRadius[li] * 0.5f, owner->client ? owner->s.number + 1 : 0 );

	Shinobi_EffectDir( shinobiMedia.fxFireballBoom[li], pos, normal );
	// a deep blast plus the crackle of fire, and the ground shakes nearby
	G_SoundAtLoc( pos, CHAN_AUTO, shinobiMedia.sndFireballBoom );
	G_SoundAtLoc( pos, CHAN_AUTO, shinobiMedia.sndFireballBoom2 );
	G_SoundAtLoc( pos, CHAN_AUTO, shinobiMedia.sndFireballCast );
	G_ScreenShake( pos, NULL, 1.5f + 1.5f * li, 350 + 150 * li, qfalse );
	G_FreeEntity( ball );
}

static void Shinobi_FireballThink( gentity_t *ball ) {
	const int size = shinobiFireballSize[FIREBALL_LEVEL( ball ) - 1];
	vec3_t    newPos, mins, maxs, up = { 0, 0, 1 };
	trace_t   tr;

	BG_EvaluateTrajectory( &ball->s.pos, level.time, newPos );
	Shinobi_IgniteAshAt( newPos, size, ball->r.ownerNum < MAX_CLIENTS ? ball->r.ownerNum + 1 : 0 ); // through ash: boom
	if ( Shinobi_InMapWater( newPos ) ) { // flew into a lake: out with a hiss
		Shinobi_SteamAt( newPos, 0 );
		G_FreeEntity( ball );
		return;
	}
	VectorSet( mins, -size, -size, -size );
	VectorSet( maxs, size, size, size );

	trap->Trace( &tr, ball->r.currentOrigin, mins, maxs, newPos, ball->r.ownerNum, MASK_SHOT, qfalse, 0, 0 );
	if ( tr.startsolid || tr.allsolid ) {
		Shinobi_FireballExplode( ball, ball->r.currentOrigin, up, tr.entityNum );
		return;
	}
	if ( tr.fraction < 1.0f ) {
		Shinobi_FireballExplode( ball, tr.endpos, tr.plane.normal, tr.entityNum );
		return;
	}
	if ( level.time >= ball->genericValue1 ) {
		Shinobi_FireballExplode( ball, newPos, up, ENTITYNUM_NONE );
		return;
	}

	VectorCopy( newPos, ball->r.currentOrigin );
	trap->LinkEntity( (sharedEntity_t *)ball );
	ball->nextthink = level.time + FRAMETIME;
}

qboolean Shinobi_CastKaton( gentity_t *ent, int lvl ) {
	gclient_t *client = ent->client;
	const int  li = lvl - 1;
	const int  size = shinobiFireballSize[li];
	gentity_t *ball;
	vec3_t     fwd, eye, start, mins, maxs;
	trace_t    tr;

	if ( SHINOBI_HAND_ANY( &client->ps ) )
		return qfalse; // hands are busy with a Rasengan / Chidori
	if ( client->shinobi.fireballNext > level.time )
		return qfalse;

	client->shinobi.fireballNext = level.time + SHINOBI_FIREBALL_COOLDOWN;

	AngleVectors( client->ps.viewangles, fwd, NULL, NULL );
	VectorCopy( client->ps.origin, eye );
	eye[2] += client->ps.viewheight;
	VectorMA( eye, 20 + size, fwd, start );
	// don't start inside a wall the caster is hugging
	VectorSet( mins, -size, -size, -size );
	VectorSet( maxs, size, size, size );
	trap->Trace( &tr, eye, mins, maxs, start, ent->s.number, MASK_SOLID, qfalse, 0, 0 );
	VectorCopy( tr.endpos, start );

	ball = G_Spawn();
	if ( !ball )
		return qfalse;
	ball->classname = "shinobi_fireball";
	ball->s.eType = ET_GENERAL;
	ball->s.generic1 = SHINOBI_ENT_FIREBALL + li;
	ball->genericValue2 = lvl;
	ball->r.ownerNum = ent->s.number;
	ball->parent = ent;
	VectorCopy( mins, ball->r.mins );
	VectorCopy( maxs, ball->r.maxs );
	ball->r.contents = 0;

	ball->s.pos.trType = TR_LINEAR;
	ball->s.pos.trTime = level.time;
	VectorCopy( start, ball->s.pos.trBase );
	VectorScale( fwd, shinobiFireballSpeed[li], ball->s.pos.trDelta );
	VectorCopy( start, ball->s.origin );
	VectorCopy( start, ball->r.currentOrigin );

	ball->genericValue1 = level.time + SHINOBI_FIREBALL_LIFE;
	ball->think = Shinobi_FireballThink;
	ball->nextthink = level.time + FRAMETIME;
	trap->LinkEntity( (sharedEntity_t *)ball );

	// blast it out with both hands (the pose plays on the torso while moving,
	// full body when standing still)
	client->ps.forceHandExtend = HANDEXTEND_TAUNT;
	client->ps.forceDodgeAnim = BOTH_FORCE_2HANDEDLIGHTNING_HOLD;
	client->ps.forceHandExtendTime = level.time + 450 + 100 * li;
	G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFireballCast );
	return qtrue;
}

/*
===========================================================================
Doton: earth wall

A hidden controller entity holds the wall's health and the list of pillars
(genericValue1..7 = pillar entity numbers + 1). Pillars themselves are
practically indestructible and forward every hit to the controller from
their pain callback. At half health the odd pillars (1st, 3rd, ...) crumble,
leaving gaps to see and shoot through; at zero the rest go too.
===========================================================================
*/
#define WALL_TRAVEL ( SHINOBI_WALL_HEIGHT + 8 ) // how far a pillar rises / sinks
#define PILLAR_HEALTH 100000

#define WALL_PILLAR(c, i)    ( (&(c)->genericValue1)[i] )  // genericValue1..7: pillar num + 1
#define WALL_COUNT(c)        ( (c)->genericValue8 )
#define WALL_MAX_HEALTH(c)   ( (c)->genericValue9 )
#define WALL_HALF_BROKEN(c)  ( (c)->genericValue10 )
#define WALL_LAST_ATTACKER(c) ( (c)->genericValue11 )
#define WALL_LAST_TIME(c)    ( (c)->genericValue12 )
#define WALL_LAST_DAMAGE(c)  ( (c)->genericValue13 )
#define WALL_LEVEL(c)        ( (c)->genericValue14 )
#define WALL_PUSH_END(c)     ( (c)->genericValue15 )
#define WALL_PUSH_HIT(c)     ( (c)->count )          // clients hit by a push (bits)

static void Shinobi_PillarFree( gentity_t *self ) {
	G_FreeEntity( self );
}

static void Shinobi_PillarCrumble( gentity_t *p ) {
	vec3_t center, up = { 0, 0, 1 };

	if ( !p->inuse || p->s.eFlags & EF_NODRAW )
		return;

	VectorCopy( p->r.currentOrigin, center );
	center[2] += SHINOBI_WALL_HEIGHT * 0.5f;
	G_PlayEffectID( shinobiMedia.fxWallBreak, center, up );
	G_SoundAtLoc( center, CHAN_AUTO, shinobiMedia.sndWallBreak );

	// gone at once for collision and rendering, freed next frame (we may be inside G_Damage)
	p->takedamage = qfalse;
	p->r.contents = 0;
	p->s.eFlags |= EF_NODRAW;
	p->pain = NULL;
	trap->UnlinkEntity( (sharedEntity_t *)p );
	p->think = Shinobi_PillarFree;
	p->nextthink = level.time + FRAMETIME;
}

static gentity_t *Shinobi_WallPillar( gentity_t *wall, int i ) {
	int num = WALL_PILLAR( wall, i ) - 1;
	if ( num <= 0 || !g_entities[num].inuse || g_entities[num].target_ent != wall )
		return NULL;
	return &g_entities[num];
}

static void Shinobi_WallCrumble( gentity_t *wall, qboolean oddOnly ) {
	int i;
	for ( i = 0; i < WALL_COUNT( wall ); i++ ) {
		gentity_t *p = Shinobi_WallPillar( wall, i );
		if ( p && ( !oddOnly || ( i % 2 ) == 0 ) ) // index 0, 2, 4... = 1st, 3rd, 5th pillar
			Shinobi_PillarCrumble( p );
	}
}

static void Shinobi_WallFree( gentity_t *wall ) {
	G_FreeEntity( wall );
}

static void Shinobi_PillarPain( gentity_t *self, gentity_t *attacker, int damage ) {
	gentity_t *wall = self->target_ent;
	int        attackerNum = attacker ? attacker->s.number : ENTITYNUM_NONE;

	self->health = PILLAR_HEALTH;
	if ( !wall || !wall->inuse || wall->health <= 0 )
		return;

	// one explosion touching several pillars counts once (the biggest hit)
	if ( WALL_LAST_ATTACKER( wall ) == attackerNum && WALL_LAST_TIME( wall ) == level.time ) {
		if ( damage <= WALL_LAST_DAMAGE( wall ) )
			return;
		damage -= WALL_LAST_DAMAGE( wall );
		WALL_LAST_DAMAGE( wall ) += damage;
	}
	else {
		WALL_LAST_ATTACKER( wall ) = attackerNum;
		WALL_LAST_TIME( wall ) = level.time;
		WALL_LAST_DAMAGE( wall ) = damage;
	}

	wall->health -= damage;
	if ( wall->health <= 0 ) {
		Shinobi_WallCrumble( wall, qfalse );
		wall->think = Shinobi_WallFree;
		wall->nextthink = level.time + FRAMETIME;
	}
	else if ( !WALL_HALF_BROKEN( wall ) && wall->health * 2 <= WALL_MAX_HEALTH( wall ) ) {
		WALL_HALF_BROKEN( wall ) = qtrue;
		Shinobi_WallCrumble( wall, qtrue );
	}
}

// lifetime over: every remaining pillar sinks back into the ground
static void Shinobi_WallSink( gentity_t *wall ) {
	vec3_t up = { 0, 0, 1 };
	int    i;

	for ( i = 0; i < WALL_COUNT( wall ); i++ ) {
		gentity_t *p = Shinobi_WallPillar( wall, i );
		if ( !p || ( p->s.eFlags & EF_NODRAW ) )
			continue;

		p->takedamage = qfalse;
		p->r.contents = 0;
		p->pain = NULL;
		p->s.pos.trType = TR_LINEAR_STOP;
		p->s.pos.trTime = level.time;
		p->s.pos.trDuration = SHINOBI_WALL_SINK_MS;
		VectorCopy( p->r.currentOrigin, p->s.pos.trBase );
		VectorSet( p->s.pos.trDelta, 0, 0, -WALL_TRAVEL * 1000.0f / SHINOBI_WALL_SINK_MS );
		trap->LinkEntity( (sharedEntity_t *)p );
		G_PlayEffectID( shinobiMedia.fxWallRise, p->r.currentOrigin, up );
		p->think = Shinobi_PillarFree;
		p->nextthink = level.time + SHINOBI_WALL_SINK_MS;
	}
	wall->think = Shinobi_WallFree;
	wall->nextthink = level.time + SHINOBI_WALL_SINK_MS + 100;
}

// find the floor under a spot and make sure a pillar fits there
static qboolean Shinobi_WallSpot( gentity_t *caster, vec3_t spot, vec3_t ground ) {
	vec3_t  top, bottom, mins, maxs;
	trace_t tr;

	VectorCopy( spot, top );
	top[2] += 48;
	VectorCopy( spot, bottom );
	bottom[2] -= 96;
	trap->Trace( &tr, top, NULL, NULL, bottom, caster->s.number, MASK_SOLID, qfalse, 0, 0 );
	if ( tr.startsolid || tr.allsolid || tr.fraction >= 1.0f )
		return qfalse; // no floor (ledge, pit) or inside geometry
	if ( tr.plane.normal[2] < 0.7f )
		return qfalse; // too steep
	VectorCopy( tr.endpos, ground );

	// the whole pillar must be free of world and bodies (never trap a player)
	VectorSet( mins, -SHINOBI_WALL_HALF_WIDTH, -SHINOBI_WALL_HALF_WIDTH, 2 );
	VectorSet( maxs, SHINOBI_WALL_HALF_WIDTH, SHINOBI_WALL_HALF_WIDTH, SHINOBI_WALL_HEIGHT );
	trap->Trace( &tr, ground, mins, maxs, ground, ENTITYNUM_NONE, MASK_PLAYERSOLID, qfalse, 0, 0 );
	if ( tr.startsolid || tr.allsolid )
		return qfalse;
	return qtrue;
}

static gentity_t *Shinobi_SpawnPillar( gentity_t *caster, gentity_t *wall, vec3_t ground, float yaw ) {
	gentity_t *p = G_Spawn();
	vec3_t     up = { 0, 0, 1 };

	if ( !p )
		return NULL;

	p->classname = "shinobi_earthwall";
	p->s.eType = ET_GENERAL;
	p->s.modelindex = shinobiMedia.wallModels[Q_irand( 0, SHINOBI_WALL_MODELS - 1 )];
	p->r.ownerNum = ENTITYNUM_NONE; // the caster must collide with it too
	p->parent = caster;
	p->target_ent = wall;

	VectorSet( p->r.mins, -SHINOBI_WALL_HALF_WIDTH, -SHINOBI_WALL_HALF_WIDTH, 0 );
	VectorSet( p->r.maxs, SHINOBI_WALL_HALF_WIDTH, SHINOBI_WALL_HALF_WIDTH, SHINOBI_WALL_HEIGHT );
	p->r.contents = CONTENTS_SOLID;
	p->clipmask = MASK_SOLID;

	p->takedamage = qtrue;
	p->health = PILLAR_HEALTH;
	p->pain = Shinobi_PillarPain;

	VectorSet( p->s.angles, 0, yaw + flrand( -20, 20 ), 0 );
	VectorCopy( p->s.angles, p->r.currentAngles );
	p->s.apos.trType = TR_STATIONARY;
	VectorCopy( p->s.angles, p->s.apos.trBase );

	// collision is at the final spot at once; clients see it rise out of the ground
	VectorCopy( ground, p->r.currentOrigin );
	VectorCopy( ground, p->s.origin );
	p->s.pos.trType = TR_LINEAR_STOP;
	p->s.pos.trTime = level.time;
	p->s.pos.trDuration = SHINOBI_WALL_RISE_MS;
	VectorCopy( ground, p->s.pos.trBase );
	p->s.pos.trBase[2] -= WALL_TRAVEL;
	VectorSet( p->s.pos.trDelta, 0, 0, WALL_TRAVEL * 1000.0f / SHINOBI_WALL_RISE_MS );

	trap->LinkEntity( (sharedEntity_t *)p );
	G_PlayEffectID( shinobiMedia.fxWallRise, ground, up );
	return p;
}

qboolean Shinobi_CastDoton( gentity_t *ent, int lvl ) {
	gclient_t *client = ent->client;
	const int  wanted = shinobiWallPillars[lvl - 1];
	vec3_t     yawAngles, fwd, right, center;
	vec3_t     grounds[SHINOBI_WALL_MAX_PILLARS];
	gentity_t *wall;
	int        i, count = 0;

	if ( client->shinobi.wallNext > level.time )
		return qfalse;

	VectorSet( yawAngles, 0, client->ps.viewangles[YAW], 0 );
	AngleVectors( yawAngles, fwd, right, NULL );
	VectorMA( client->ps.origin, SHINOBI_WALL_DISTANCE, fwd, center );

	for ( i = 0; i < wanted; i++ ) {
		float  offset = ( i - ( wanted - 1 ) * 0.5f ) * SHINOBI_WALL_SPACING;
		vec3_t spot;
		VectorMA( center, offset, right, spot );
		if ( Shinobi_WallSpot( ent, spot, grounds[count] ) )
			count++;
	}

	if ( !count ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		trap->SendServerCommand( ent - g_entities, "cp \"No ground for an earth wall\"" );
		return qfalse;
	}

	wall = G_Spawn();
	if ( !wall )
		return qfalse;
	wall->classname = "shinobi_earthwall_ctrl";
	wall->r.ownerNum = ent->s.number;
	WALL_LEVEL( wall ) = lvl;
	wall->r.svFlags |= SVF_NOCLIENT;
	wall->health = WALL_MAX_HEALTH( wall ) = shinobiWallHealth[lvl - 1];
	wall->think = Shinobi_WallSink;
	wall->nextthink = level.time + SHINOBI_WALL_LIFETIME;

	for ( i = 0; i < count; i++ ) {
		gentity_t *p = Shinobi_SpawnPillar( ent, wall, grounds[i], client->ps.viewangles[YAW] );
		if ( p )
			WALL_PILLAR( wall, WALL_COUNT( wall )++ ) = p->s.number + 1;
	}

	client->shinobi.wallNext = level.time + SHINOBI_WALL_COOLDOWN;
	Shinobi_EarthOnWater( center, ( wanted * SHINOBI_WALL_SPACING ) * 0.5f + 24 ); // puddles turn to mud

	// slam the ground
	client->ps.forceHandExtend = HANDEXTEND_FORCEPUSH;
	client->ps.forceHandExtendTime = level.time + 500;
	G_SoundAtLoc( center, CHAN_AUTO, shinobiMedia.sndWallRise );
	return qtrue;
}

/*
===========================================================================
Pushing the wall: every standing pillar slides forward as a wave of earth.
People in the way are shoved ahead; pinned against something solid they are
crushed (more damage, knocked down) and the wall breaks on them. Burning
ground under it is smothered. At the end of the run the pillars crumble.
===========================================================================
*/

static void Shinobi_WallPushThink( gentity_t *wall ) {
	gentity_t *owner = &g_entities[wall->r.ownerNum];
	const int  li = WALL_LEVEL( wall ) - 1;
	vec3_t     dir;
	int        i, alive = 0;

	if ( !owner->inuse || !owner->client )
		owner = wall;
	VectorCopy( wall->movedir, dir );

	for ( i = 0; i < WALL_COUNT( wall ); i++ ) {
		gentity_t *p = Shinobi_WallPillar( wall, i );
		vec3_t     newPos, mins, maxs, floor;
		trace_t    tr;
		int        c;

		if ( !p || ( p->s.eFlags & EF_NODRAW ) )
			continue;
		BG_EvaluateTrajectory( &p->s.pos, level.time, newPos );

		// slam into the world (or run off a ledge): this pillar breaks
		VectorSet( mins, -SHINOBI_WALL_HALF_WIDTH + 2, -SHINOBI_WALL_HALF_WIDTH + 2, 16 );
		VectorSet( maxs, SHINOBI_WALL_HALF_WIDTH - 2, SHINOBI_WALL_HALF_WIDTH - 2, SHINOBI_WALL_HEIGHT - 8 );
		trap->Trace( &tr, p->r.currentOrigin, mins, maxs, newPos, p->s.number, MASK_SOLID, qfalse, 0, 0 );
		if ( tr.fraction < 1.0f || tr.startsolid || !Shinobi_FloorBelow( newPos, 40, floor ) ) {
			Shinobi_PillarCrumble( p );
			continue;
		}
		VectorCopy( newPos, p->r.currentOrigin );
		trap->LinkEntity( (sharedEntity_t *)p );
		alive++;

		Shinobi_SmotherFire( newPos, SHINOBI_WALL_HALF_WIDTH + 8, qfalse );

		// people in front of this pillar
		for ( c = 0; c < MAX_CLIENTS; c++ ) {
			gentity_t *v = &g_entities[c];
			vec3_t     d, ahead;
			trace_t    pin;

			if ( !v->inuse || !v->client || v == owner || v->health <= 0 || v->client->sess.sessionTeam == TEAM_SPECTATOR )
				continue;
			if ( level.gametype >= GT_TEAM && OnSameTeam( owner, v ) )
				continue;
			VectorSubtract( v->r.currentOrigin, newPos, d );
			if ( fabs( d[0] ) > SHINOBI_WALL_HALF_WIDTH + 18 || fabs( d[1] ) > SHINOBI_WALL_HALF_WIDTH + 18
				|| d[2] + v->r.mins[2] > SHINOBI_WALL_HEIGHT || d[2] + v->r.maxs[2] < 0 )
				continue;

			// pinned: something solid right ahead of him
			VectorMA( v->r.currentOrigin, 24, dir, ahead );
			trap->Trace( &pin, v->r.currentOrigin, v->r.mins, v->r.maxs, ahead, v->s.number, MASK_PLAYERSOLID, qfalse, 0, 0 );
			if ( pin.fraction < 1.0f && pin.entityNum == ENTITYNUM_WORLD ) {
				shinobiHitElement = SE_EARTH;
				G_Damage( v, wall, owner, dir, v->r.currentOrigin, shinobiWallCrushDamage[li], DAMAGE_NO_KNOCKBACK, MOD_CRUSH );
				shinobiHitElement = SE_NONE;
				if ( v->inuse && v->health > 0 && !Shinobi_CCImmune( v ) ) {
					G_Knockdown( v );
					Shinobi_CCGrant( v, level.time + 1500 );
				}
				G_SoundAtLoc( v->r.currentOrigin, CHAN_AUTO, shinobiMedia.sndWallBreak );
				WALL_PUSH_HIT( wall ) |= ( 1 << c );
				Shinobi_WallCrumble( wall, qfalse ); // the wall breaks on him
				wall->think = Shinobi_WallFree;
				wall->nextthink = level.time + FRAMETIME;
				return;
			}
			if ( !( WALL_PUSH_HIT( wall ) & ( 1 << c ) ) ) {
				WALL_PUSH_HIT( wall ) |= ( 1 << c );
				shinobiHitElement = SE_EARTH;
				G_Damage( v, wall, owner, dir, v->r.currentOrigin, shinobiWallPushDamage[li], DAMAGE_NO_KNOCKBACK, MOD_CRUSH );
				shinobiHitElement = SE_NONE;
			}
			if ( v->inuse && v->health > 0 ) { // keep him moving ahead of the wall
				VectorScale( dir, SHINOBI_WALL_PUSH_SPEED + 150, v->client->ps.velocity );
				v->client->ps.velocity[2] = 160;
				v->client->ps.groundEntityNum = ENTITYNUM_NONE;
				v->client->ps.otherKiller = owner->s.number;
				v->client->ps.otherKillerTime = level.time + 5000;
				v->client->ps.otherKillerDebounceTime = level.time + 100;
			}
		}
	}

	if ( !alive || level.time >= WALL_PUSH_END( wall ) ) {
		Shinobi_WallCrumble( wall, qfalse );
		wall->think = Shinobi_WallFree;
		wall->nextthink = level.time + FRAMETIME;
		return;
	}
	wall->nextthink = level.time + FRAMETIME;
}

qboolean Shinobi_PushWall( gentity_t *ent ) {
	gclient_t *client = ent->client;
	vec3_t     angles, fwd, eye;
	gentity_t *best = NULL;
	float      bestDist = SHINOBI_WALL_PUSH_REACH;
	int        i;

	VectorSet( angles, 0, client->ps.viewangles[YAW], 0 );
	AngleVectors( angles, fwd, NULL, NULL );
	VectorCopy( client->ps.origin, eye );

	// our own standing wall, in front of us
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *w = &g_entities[i];
		vec3_t     center = { 0, 0, 0 }, d;
		int        n = 0, k;
		float      dist;

		if ( !w->inuse || !w->classname || strcmp( w->classname, "shinobi_earthwall_ctrl" ) )
			continue;
		if ( w->r.ownerNum != ent->s.number || w->health <= 0 || w->think != Shinobi_WallSink )
			continue;
		for ( k = 0; k < WALL_COUNT( w ); k++ ) {
			gentity_t *p = Shinobi_WallPillar( w, k );
			if ( p && !( p->s.eFlags & EF_NODRAW ) ) {
				VectorAdd( center, p->r.currentOrigin, center );
				n++;
			}
		}
		if ( !n )
			continue;
		VectorScale( center, 1.0f / n, center );
		VectorSubtract( center, eye, d );
		d[2] = 0;
		dist = VectorNormalize( d );
		if ( dist > bestDist || DotProduct( d, fwd ) < 0.3f )
			continue;
		best = w;
		bestDist = dist;
	}
	if ( !best )
		return qfalse;

	if ( !Shinobi_SpendChakra( ent, SHINOBI_WALL_PUSH_COST ) )
		return qtrue; // it was there, we just couldn't pay

	VectorCopy( fwd, best->movedir );
	WALL_PUSH_HIT( best ) = 0;
	WALL_PUSH_END( best ) = level.time + (int)( shinobiWallPushDist[WALL_LEVEL( best ) - 1] * 1000.0f / SHINOBI_WALL_PUSH_SPEED );
	for ( i = 0; i < WALL_COUNT( best ); i++ ) {
		gentity_t *p = Shinobi_WallPillar( best, i );
		if ( !p || ( p->s.eFlags & EF_NODRAW ) )
			continue;
		p->r.contents = 0; // we move people ourselves; the pillars must not block each other
		p->takedamage = qfalse;
		p->s.pos.trType = TR_LINEAR;
		p->s.pos.trTime = level.time;
		VectorCopy( p->r.currentOrigin, p->s.pos.trBase );
		VectorScale( fwd, SHINOBI_WALL_PUSH_SPEED, p->s.pos.trDelta );
		trap->LinkEntity( (sharedEntity_t *)p );
	}
	best->think = Shinobi_WallPushThink;
	best->nextthink = level.time + FRAMETIME;

	client->ps.forceHandExtend = HANDEXTEND_FORCEPUSH;
	client->ps.forceHandExtendTime = level.time + 500;
	G_Sound( ent, CHAN_AUTO, shinobiMedia.sndWallRise );
	return qtrue;
}
