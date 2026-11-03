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
	for ( i = 0; i < 3; i++ )
		m->rockBlockModel[i] = G_ModelIndex( va( "models/shinobi/rock_block%i.md3", i + 1 ) );
	m->rockLedgeModel = G_ModelIndex( "models/shinobi/rock_ledge.md3" );
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
	Shinobi_EffectDir( li ? shinobiMedia.fxStockExplode : shinobiMedia.fxStockExplodeSmall, pos, normal );
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
	ball->s.owner = ent->s.number; // (the Sharingan draws the paths of enemy missiles)
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
	float  dustR;

	if ( !p->inuse || p->s.eFlags & EF_NODRAW )
		return;

	VectorAdd( p->r.mins, p->r.maxs, center );
	VectorMA( p->r.currentOrigin, 0.5f, center, center );
	dustR = ( p->r.maxs[0] - p->r.mins[0] ) * 0.5f + 28;
	G_PlayEffectID( shinobiMedia.fxWallBreak, center, up );
	G_PlayEffectID( shinobiMedia.fxRockMed, center, up );
	G_SoundAtLoc( center, CHAN_AUTO, shinobiMedia.sndWallBreak );

	// gone at once for collision and rendering, freed next frame (we may be inside G_Damage)
	p->takedamage = qfalse;
	p->r.contents = 0;
	p->s.eFlags |= EF_NODRAW;
	p->pain = NULL;
	trap->UnlinkEntity( (sharedEntity_t *)p );
	p->think = Shinobi_PillarFree;
	p->nextthink = level.time + FRAMETIME;

	// the broken stone lies around as sand and dust (water makes it mud)
	Shinobi_DustAt( center, dustR, p->parent && p->parent->inuse ? p->parent->s.number + 1 : 0 );
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
		if ( p->speed > 0 ) // a Rock Pillar block: back the way it came (down, or into the wall)
			VectorScale( p->movedir, -p->speed * 1000.0f / SHINOBI_WALL_SINK_MS, p->s.pos.trDelta );
		else
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

/*
===========================================================================
Earthquake (Doton): the charge decides the radius. Cracks race out to it,
then the ground heaves: everyone standing on it is knocked down (level 1:
staggered) and hurt; puddles turn to mud. Anyone in the air is spared.
===========================================================================
*/
static void Shinobi_QuakeStrike( gentity_t *q ) {
	gentity_t *owner = &g_entities[q->r.ownerNum];
	const int  li = q->count > 0 ? q->count - 1 : 0;
	const float R = q->s.angles2[0];
	vec3_t     center, up = { 0, 0, 1 };
	int        i;

	VectorCopy( q->r.currentOrigin, center );
	if ( !owner->inuse || !owner->client )
		owner = q;

	Shinobi_EarthOnWater( center, R );
	Shinobi_DustAt( center, R, owner->s.number + 1 ); // the whole area is left under loose earth
	G_PlayEffectID( shinobiMedia.fxWallRise, center, up );
	G_PlayEffectID( shinobiMedia.fxRockLg, center, up );
	for ( i = 0; i < 8; i++ ) {
		const float a = DEG2RAD( i * 45.0f + 22.5f );
		vec3_t p, floor;
		VectorSet( p, center[0] + cosf( a ) * R * 0.65f, center[1] + sinf( a ) * R * 0.65f, center[2] + 32 );
		if ( Shinobi_FloorBelow( p, 96, floor ) )
			G_PlayEffectID( shinobiMedia.fxWallRise, floor, up );
	}
	G_SoundAtLoc( center, CHAN_AUTO, shinobiMedia.sndWallBreak );
	G_SoundAtLoc( center, CHAN_AUTO, shinobiMedia.sndWallRise );

	for ( i = 0; i < level.num_entities; i++ ) {
		gentity_t *v = &g_entities[i];
		vec3_t     d;
		float      dist;

		if ( !v->inuse || !v->client || !v->takedamage || v->health <= 0 || v == owner )
			continue;
		if ( v->s.number < MAX_CLIENTS && v->client->sess.sessionTeam == TEAM_SPECTATOR )
			continue;
		if ( v->shinobiCloneOwner && v->shinobiCloneOwner == owner->s.number + 1 )
			continue; // our own clones keep their feet
		if ( level.gametype >= GT_TEAM && owner->client && OnSameTeam( owner, v ) )
			continue;
		if ( v->client->ps.groundEntityNum == ENTITYNUM_NONE && !Shinobi_Burrowed( v ) )
			continue; // in the air: it passes under
		if ( v->client->ps.pm_flags & SHINOBI_PMF_ONWATER )
			continue; // standing on water: no ground to shake
		VectorSubtract( v->r.currentOrigin, center, d );
		if ( d[2] < -48 || d[2] > 96 )
			continue;
		d[2] = 0;
		dist = VectorLength( d );
		if ( dist > R )
			continue;

		if ( Shinobi_Burrowed( v ) )
			Shinobi_BurrowEnd( v, qfalse ); // shaken out of the ground
		shinobiHitElement = SE_EARTH;
		shinobiJutsuDamage = 1;
		G_Damage( v, q, owner, up, v->r.currentOrigin, shinobiQuakeDamage[li], DAMAGE_NO_ARMOR | DAMAGE_NO_KNOCKBACK, MOD_CRUSH );
		shinobiJutsuDamage = 0;
		shinobiHitElement = SE_NONE;
		if ( !v->inuse || v->health <= 0 || !v->client )
			continue;
		v->client->ps.velocity[2] += 120; // the ground jumps
		if ( Shinobi_CCImmune( v ) )
			continue;
		if ( li == 0 && v->s.number < MAX_CLIENTS ) {
			Shinobi_Stagger( v, 700 );
			Shinobi_CCGrant( v, level.time + 700 );
		}
		else {
			G_Knockdown( v );
			Shinobi_CCGrant( v, level.time + 1500 );
		}
	}
}

static void Shinobi_QuakeThink( gentity_t *q ) {
	if ( level.time >= q->s.time2 + SHINOBI_QUAKE_LINGER ) {
		G_FreeEntity( q );
		return;
	}
	if ( !q->genericValue1 && level.time >= q->s.time2 ) {
		q->genericValue1 = 1;
		Shinobi_QuakeStrike( q );
	}
	q->nextthink = level.time + FRAMETIME;
}

qboolean Shinobi_CastQuake( gentity_t *ent, int lvl ) {
	gclient_t *client = ent->client;
	gentity_t *q;
	vec3_t     floor;

	if ( SHINOBI_HAND_ANY( &client->ps ) )
		return qfalse;
	if ( client->shinobi.quakeNext > level.time ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		trap->SendServerCommand( ent - g_entities, va( "cp \"Earthquake ready in %.1f s\"",
			( client->shinobi.quakeNext - level.time ) / 1000.0f ) );
		return qfalse;
	}
	if ( client->ps.groundEntityNum == ENTITYNUM_NONE || ( client->ps.pm_flags & SHINOBI_PMF_ONWATER )
		|| !Shinobi_FloorBelow( ent->r.currentOrigin, 64, floor ) ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		trap->SendServerCommand( ent - g_entities, "cp \"Earthquake: stand on the ground\"" );
		return qfalse;
	}
	q = G_Spawn();
	if ( !q )
		return qfalse;
	q->classname = "shinobi_quake";
	q->s.eType = ET_GENERAL;
	q->s.generic1 = SHINOBI_ENT_QUAKE;
	q->s.angles2[0] = (float)shinobiQuakeRadius[lvl - 1];
	q->s.time = level.time;
	q->s.time2 = level.time + shinobiQuakeWindup[lvl - 1];
	q->count = lvl;
	q->r.ownerNum = ent->s.number;
	q->r.contents = 0;
	q->r.svFlags |= SVF_BROADCAST;
	G_SetOrigin( q, floor );
	q->think = Shinobi_QuakeThink;
	q->nextthink = level.time + FRAMETIME;
	trap->LinkEntity( (sharedEntity_t *)q );

	client->shinobi.quakeNext = level.time + SHINOBI_QUAKE_COOLDOWN;
	client->ps.forceHandExtend = HANDEXTEND_TAUNT;
	client->ps.forceDodgeAnim = BOTH_FORCEPUSH;
	client->ps.forceHandExtendTime = level.time + 600;
	G_Sound( ent, CHAN_AUTO, shinobiMedia.sndWallRise );
	return qtrue;
}

/*
===========================================================================
Rock Pillar (Doton): a rumble where you look, then a wide block of stone
erupts there, launching whoever is on the spot. Aimed at the floor it is a
pillar to stand on; aimed at a wall it is a ledge sticking out of it. The
blocks are axis-aligned (the model is the collision box), stay 90 s and each
caster keeps at most SHINOBI_PILLAR_MAX: a new one takes the oldest down.
They take hits like an earth wall (not pushable); broken, they leave dust.
===========================================================================
*/
#define PILLARCAST_STAGE(c)  ( (c)->genericValue1 ) // 0 rumbling, 1 erupted (placing the stone)
#define PILLARCAST_LEDGE(c)  ( (c)->genericValue2 ) // out of a wall (c->movedir: out of the wall)

static void Shinobi_RockBox( qboolean ledge, int lvl, vec3_t mins, vec3_t maxs ) {
	if ( ledge ) {
		VectorSet( mins, -SHINOBI_ROCK_HALF, -SHINOBI_ROCK_HALF, -SHINOBI_ROCK_LEDGE_HALF );
		VectorSet( maxs, SHINOBI_ROCK_HALF, SHINOBI_ROCK_HALF, SHINOBI_ROCK_LEDGE_HALF );
	}
	else {
		VectorSet( mins, -SHINOBI_ROCK_HALF, -SHINOBI_ROCK_HALF, -16 );
		VectorSet( maxs, SHINOBI_ROCK_HALF, SHINOBI_ROCK_HALF, shinobiRockHeight[lvl - 1] );
	}
}

// the stone's final origin, if nobody stands in its way (the world may cut into it)
static qboolean Shinobi_RockSpot( gentity_t *c, vec3_t origin ) {
	const qboolean ledge = PILLARCAST_LEDGE( c );
	vec3_t         mins, maxs, mid;
	trace_t        tr;

	Shinobi_RockBox( ledge, c->count > 0 ? c->count : 1, mins, maxs );
	if ( ledge )
		VectorMA( c->r.currentOrigin, SHINOBI_ROCK_HALF, c->movedir, origin );
	else
		VectorCopy( c->r.currentOrigin, origin );
	VectorCopy( origin, mid );
	mid[2] += ledge ? 0 : 24;
	if ( trap->PointContents( mid, ENTITYNUM_NONE ) & CONTENTS_SOLID )
		return qfalse; // buried in the map
	trap->Trace( &tr, origin, mins, maxs, origin, ENTITYNUM_NONE, CONTENTS_BODY, qfalse, 0, 0 );
	return ( tr.startsolid || tr.allsolid ) ? qfalse : qtrue;
}

static gentity_t *Shinobi_SpawnRock( gentity_t *caster, gentity_t *wall, vec3_t origin, qboolean ledge, int lvl, vec3_t dir ) {
	gentity_t  *p = G_Spawn();
	vec3_t      up = { 0, 0, 1 };
	const float travel = ledge ? SHINOBI_ROCK_HALF * 2 + 8 : shinobiRockHeight[lvl - 1] + 24;

	if ( !p )
		return NULL;
	p->classname = "shinobi_earthwall"; // stone for the element rules (Lightning Edge, water -> mud...)
	p->s.eType = ET_GENERAL;
	p->s.modelindex = ledge ? shinobiMedia.rockLedgeModel : shinobiMedia.rockBlockModel[lvl - 1];
	p->r.ownerNum = ENTITYNUM_NONE; // the caster stands on it too
	p->parent = caster;
	p->target_ent = wall;
	Shinobi_RockBox( ledge, lvl, p->r.mins, p->r.maxs );
	p->r.contents = CONTENTS_SOLID;
	p->clipmask = MASK_SOLID;
	p->takedamage = qtrue;
	p->health = PILLAR_HEALTH;
	p->pain = Shinobi_PillarPain;
	VectorCopy( dir, p->movedir );
	p->speed = travel;

	VectorClear( p->s.angles );
	VectorClear( p->r.currentAngles );
	p->s.apos.trType = TR_STATIONARY;
	VectorClear( p->s.apos.trBase );

	// solid at once where it ends; clients see it come out of the floor / wall
	VectorCopy( origin, p->r.currentOrigin );
	VectorCopy( origin, p->s.origin );
	p->s.pos.trType = TR_LINEAR_STOP;
	p->s.pos.trTime = level.time;
	p->s.pos.trDuration = SHINOBI_ROCK_RISE_MS;
	VectorMA( origin, -travel, dir, p->s.pos.trBase );
	VectorScale( dir, travel * 1000.0f / SHINOBI_ROCK_RISE_MS, p->s.pos.trDelta );

	trap->LinkEntity( (sharedEntity_t *)p );
	G_PlayEffectID( shinobiMedia.fxWallRise, origin, ledge ? dir : up );
	return p;
}

// keep at most SHINOBI_PILLAR_MAX blocks per caster: the oldest goes down
static void Shinobi_RockRemember( gentity_t *owner, gentity_t *wall ) {
	gclient_t *client = owner->client;
	int        n = 0, i;

	if ( !client )
		return;
	for ( i = 0; i < SHINOBI_PILLAR_MAX; i++ ) { // forget the ones already gone
		const int num = client->shinobi.pillars[i] - 1;
		gentity_t *w = num > 0 ? &g_entities[num] : NULL;
		if ( w && w->inuse && w->classname && !strcmp( w->classname, "shinobi_rockpillar_ctrl" )
			&& w->r.ownerNum == owner->s.number && w->think == Shinobi_WallSink )
			client->shinobi.pillars[n++] = num + 1;
	}
	if ( n >= SHINOBI_PILLAR_MAX ) {
		Shinobi_WallSink( &g_entities[client->shinobi.pillars[0] - 1] );
		for ( i = 0; i < n - 1; i++ )
			client->shinobi.pillars[i] = client->shinobi.pillars[i + 1];
		n--;
	}
	client->shinobi.pillars[n++] = wall->s.number + 1;
	for ( i = n; i < SHINOBI_PILLAR_MAX; i++ )
		client->shinobi.pillars[i] = 0;
}

static void Shinobi_PillarCastThink( gentity_t *c ) {
	gentity_t     *owner = &g_entities[c->r.ownerNum];
	const int      lvl = c->count > 0 ? c->count : 1;
	const qboolean ledge = PILLARCAST_LEDGE( c );
	vec3_t         up = { 0, 0, 1 }, origin, mins, maxs, center;
	int            i;

	if ( !owner->inuse || !owner->client )
		owner = c;

	if ( !PILLARCAST_STAGE( c ) ) {
		if ( level.time < c->s.time2 ) {
			c->nextthink = level.time + FRAMETIME;
			return;
		}
		PILLARCAST_STAGE( c ) = 1;
		G_PlayEffectID( shinobiMedia.fxWallRise, c->r.currentOrigin, ledge ? c->movedir : up );
		G_PlayEffectID( shinobiMedia.fxRockMed, c->r.currentOrigin, ledge ? c->movedir : up );
		G_SoundAtLoc( c->r.currentOrigin, CHAN_AUTO, shinobiMedia.sndWallRise );
		if ( !ledge )
			Shinobi_EarthOnWater( c->r.currentOrigin, SHINOBI_PILLAR_RADIUS );

		// whoever is where the stone comes: hurt and thrown off it
		Shinobi_RockBox( ledge, lvl, mins, maxs );
		if ( ledge )
			VectorMA( c->r.currentOrigin, SHINOBI_ROCK_HALF, c->movedir, center );
		else
			VectorCopy( c->r.currentOrigin, center );
		for ( i = 0; i < level.num_entities; i++ ) {
			gentity_t *v = &g_entities[i];
			vec3_t     d;

			if ( !v->inuse || !v->client || !v->takedamage || v->health <= 0 || v == owner )
				continue;
			if ( v->s.number < MAX_CLIENTS && v->client->sess.sessionTeam == TEAM_SPECTATOR )
				continue;
			if ( v->shinobiCloneOwner && v->shinobiCloneOwner == owner->s.number + 1 )
				continue;
			if ( level.gametype >= GT_TEAM && owner->client && OnSameTeam( owner, v ) )
				continue;
			if ( v->client && SHINOBI_UNSEEN( &v->client->ps ) )
				continue;
			VectorSubtract( v->r.currentOrigin, center, d );
			if ( fabs( d[0] ) > maxs[0] + 20 || fabs( d[1] ) > maxs[1] + 20
				|| d[2] + v->r.maxs[2] < mins[2] - 8 || d[2] + v->r.mins[2] > maxs[2] + 24 )
				continue;
			shinobiHitElement = SE_EARTH;
			shinobiJutsuDamage = 1;
			G_Damage( v, c, owner, ledge ? c->movedir : up, v->r.currentOrigin, shinobiPillarDamage[lvl - 1], DAMAGE_NO_KNOCKBACK, MOD_CRUSH );
			shinobiJutsuDamage = 0;
			shinobiHitElement = SE_NONE;
			if ( !v->inuse || v->health <= 0 || !v->client || Shinobi_Armored( v ) )
				continue;
			if ( ledge ) { // shoved away from the wall
				VectorScale( c->movedir, shinobiPillarLaunch[lvl - 1] * 1.2f, v->client->ps.velocity );
				v->client->ps.velocity[2] = 200;
			}
			else { // thrown up and off the rising stone
				d[2] = 0;
				if ( VectorNormalize( d ) < 0.1f )
					VectorSet( d, 1, 0, 0 );
				VectorScale( d, 200, v->client->ps.velocity );
				v->client->ps.velocity[2] = shinobiPillarLaunch[lvl - 1];
			}
			v->client->ps.groundEntityNum = ENTITYNUM_NONE;
			v->client->ps.otherKiller = owner->s.number;
			v->client->ps.otherKillerTime = level.time + 5000;
			v->client->ps.otherKillerDebounceTime = level.time + 100;
			if ( !Shinobi_CCImmune( v ) ) {
				G_Knockdown( v );
				Shinobi_CCGrant( v, level.time + 1500 );
			}
		}
		c->nextthink = level.time + 100; // let them fly clear first
		return;
	}

	// stage 1: put the stone there as soon as nobody is in the way (give up after a while)
	if ( Shinobi_RockSpot( c, origin ) ) {
		gentity_t *wall = G_Spawn();
		if ( wall ) {
			gentity_t *p;
			wall->classname = "shinobi_rockpillar_ctrl"; // (not an earth wall: F doesn't shove it)
			wall->r.ownerNum = owner->s.number;
			WALL_LEVEL( wall ) = lvl;
			wall->r.svFlags |= SVF_NOCLIENT;
			wall->health = WALL_MAX_HEALTH( wall ) = shinobiPillarHealth[lvl - 1];
			WALL_HALF_BROKEN( wall ) = qtrue; // one block: no half-crumbling
			wall->think = Shinobi_WallSink;
			wall->nextthink = level.time + SHINOBI_PILLAR_LIFE;
			p = Shinobi_SpawnRock( owner, wall, origin, ledge, lvl, ledge ? c->movedir : up );
			if ( p ) {
				WALL_PILLAR( wall, WALL_COUNT( wall )++ ) = p->s.number + 1;
				Shinobi_RockRemember( owner, wall );
			}
			else
				G_FreeEntity( wall );
		}
		G_FreeEntity( c );
		return;
	}
	if ( level.time > c->s.time2 + 700 ) {
		G_FreeEntity( c ); // someone is still in the way: no stone this time
		return;
	}
	c->nextthink = level.time + FRAMETIME;
}

qboolean Shinobi_CastPillar( gentity_t *ent, int lvl ) {
	gclient_t *client = ent->client;
	vec3_t     fwd, eye, end, spot, floor, dir = { 0, 0, 1 };
	trace_t    tr;
	gentity_t *c;
	qboolean   ledge = qfalse;

	if ( SHINOBI_HAND_ANY( &client->ps ) )
		return qfalse;
	if ( client->shinobi.pillarNext > level.time ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		trap->SendServerCommand( ent - g_entities, va( "cp \"Rock Pillar ready in %.1f s\"",
			( client->shinobi.pillarNext - level.time ) / 1000.0f ) );
		return qfalse;
	}
	AngleVectors( client->ps.viewangles, fwd, NULL, NULL );
	VectorCopy( client->ps.origin, eye );
	eye[2] += client->ps.viewheight;
	VectorMA( eye, shinobiPillarRange[lvl - 1], fwd, end );
	trap->Trace( &tr, eye, NULL, NULL, end, ent->s.number, MASK_SOLID, qfalse, 0, 0 );

	if ( tr.fraction < 1.0f && fabs( tr.plane.normal[2] ) < 0.35f
		&& !( tr.entityNum < ENTITYNUM_WORLD && g_entities[tr.entityNum].classname
			&& !strcmp( g_entities[tr.entityNum].classname, "shinobi_earthwall" ) ) ) {
		// a wall: a ledge straight out of it (snapped to the map's axes)
		ledge = qtrue;
		if ( fabs( tr.plane.normal[0] ) >= fabs( tr.plane.normal[1] ) )
			VectorSet( dir, tr.plane.normal[0] > 0 ? 1 : -1, 0, 0 );
		else
			VectorSet( dir, 0, tr.plane.normal[1] > 0 ? 1 : -1, 0 );
		VectorMA( tr.endpos, 1, tr.plane.normal, floor );
	}
	else {
		VectorCopy( tr.endpos, spot );
		if ( tr.fraction < 1.0f )
			VectorMA( spot, 4, tr.plane.normal, spot ); // off the surface we hit
		spot[2] += 16;
		if ( !Shinobi_FloorBelow( spot, 256, floor ) ) {
			G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
			trap->SendServerCommand( ent - g_entities, "cp \"Rock Pillar: no ground there\"" );
			return qfalse;
		}
	}

	c = G_Spawn();
	if ( !c )
		return qfalse;
	c->classname = "shinobi_pillarcast";
	c->s.eType = ET_GENERAL;
	c->s.generic1 = SHINOBI_ENT_PILLARCAST;
	c->s.time = level.time;
	c->s.time2 = level.time + SHINOBI_PILLAR_DELAY;
	c->s.angles2[0] = client->ps.viewangles[YAW];
	c->s.angles2[1] = ledge ? 1 : 0;      // the cgame: dust out of a wall, not a ring on the floor
	VectorCopy( dir, c->s.origin2 );
	VectorCopy( dir, c->movedir );
	PILLARCAST_LEDGE( c ) = ledge;
	c->count = lvl;
	c->r.ownerNum = ent->s.number;
	c->r.contents = 0;
	c->r.svFlags |= SVF_BROADCAST;
	G_SetOrigin( c, floor );
	c->think = Shinobi_PillarCastThink;
	c->nextthink = level.time + FRAMETIME;
	trap->LinkEntity( (sharedEntity_t *)c );

	client->shinobi.pillarNext = level.time + SHINOBI_PILLAR_COOLDOWN;
	client->ps.forceHandExtend = HANDEXTEND_TAUNT;
	client->ps.forceDodgeAnim = BOTH_FORCEPUSH;
	client->ps.forceHandExtendTime = level.time + 500;
	G_SoundAtLoc( floor, CHAN_AUTO, shinobiMedia.sndWallRise );
	return qtrue;
}
