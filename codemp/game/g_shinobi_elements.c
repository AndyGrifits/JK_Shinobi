/*
===========================================================================
JK_Shinobi - elemental ninjutsu.

  Katon: Goukakyuu no Jutsu  ("katon")  great fireball, explodes on impact
  Doton: Doryuheki           ("doton")  stone pillars rise into a wall

Both are plain server entities (ET_GENERAL):
  - the fireball has no model; the cgame recognises it by
    s.generic1 == SHINOBI_ENT_FIREBALL and draws the flames itself;
  - wall pillars are md3 models (models/shinobi/earth_pillar*.md3) with a
    solid, damageable bounding box.
===========================================================================
*/
#include "g_shinobi_local.h"

void Shinobi_ElementsInit( void ) {
	shinobiMedia_t *m = &shinobiMedia;
	int i;

	m->fxFireballBoom  = G_EffectIndex( SHINOBI_FX_FIREBALL_BOOM );
	m->fxWallRise      = G_EffectIndex( SHINOBI_FX_WALL_RISE );
	m->fxWallBreak     = G_EffectIndex( SHINOBI_FX_WALL_BREAK );
	G_EffectIndex( SHINOBI_FX_FIREBALL ); // drawn by the cgame, register early anyway

	m->sndFireballCast = G_SoundIndex( "sound/effects/combustfire.mp3" );
	m->sndFireballBoom = G_SoundIndex( "sound/weapons/explosions/explode5.wav" );
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
static void Shinobi_FireballExplode( gentity_t *ball, vec3_t pos, vec3_t normal, int hitEnt ) {
	gentity_t *owner = &g_entities[ball->r.ownerNum];
	vec3_t     dir;

	if ( !owner->inuse || !owner->client )
		owner = ball;

	VectorCopy( ball->s.pos.trDelta, dir );
	VectorNormalize( dir );

	if ( hitEnt >= 0 && hitEnt < ENTITYNUM_WORLD ) {
		gentity_t *other = &g_entities[hitEnt];
		if ( other->takedamage ) {
			G_Damage( other, ball, owner, dir, pos, SHINOBI_FIREBALL_DAMAGE, 0, MOD_ROCKET );
		}
	}
	// the caster is immune to their own blast
	G_RadiusDamage( pos, owner, SHINOBI_FIREBALL_SPLASH, SHINOBI_FIREBALL_RADIUS, owner, ball, MOD_ROCKET_SPLASH );

	G_PlayEffectID( shinobiMedia.fxFireballBoom, pos, normal );
	G_SoundAtLoc( pos, CHAN_AUTO, shinobiMedia.sndFireballBoom );
	G_FreeEntity( ball );
}

static void Shinobi_FireballThink( gentity_t *ball ) {
	vec3_t  newPos, mins, maxs, up = { 0, 0, 1 };
	trace_t tr;

	BG_EvaluateTrajectory( &ball->s.pos, level.time, newPos );
	VectorSet( mins, -SHINOBI_FIREBALL_SIZE, -SHINOBI_FIREBALL_SIZE, -SHINOBI_FIREBALL_SIZE );
	VectorSet( maxs, SHINOBI_FIREBALL_SIZE, SHINOBI_FIREBALL_SIZE, SHINOBI_FIREBALL_SIZE );

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

void Cmd_Katon_f( gentity_t *ent ) {
	gclient_t *client = ent->client;
	gentity_t *ball;
	vec3_t     fwd, eye, start;
	trace_t    tr;

	if ( !Shinobi_CanAct( ent ) )
		return;
	if ( SHINOBI_HAND_ANY( &client->ps ) )
		return; // hands are busy with a Rasengan / Chidori
	if ( client->shinobi.fireballNext > level.time )
		return;
	if ( !Shinobi_SpendChakra( ent, SHINOBI_FIREBALL_COST ) )
		return;

	client->shinobi.fireballNext = level.time + SHINOBI_FIREBALL_COOLDOWN;
	Shinobi_StopCharging( client );

	AngleVectors( client->ps.viewangles, fwd, NULL, NULL );
	VectorCopy( client->ps.origin, eye );
	eye[2] += client->ps.viewheight;
	VectorMA( eye, 24, fwd, start );
	// don't start inside a wall the caster is hugging
	trap->Trace( &tr, eye, NULL, NULL, start, ent->s.number, MASK_SOLID, qfalse, 0, 0 );
	VectorCopy( tr.endpos, start );

	ball = G_Spawn();
	if ( !ball )
		return;
	ball->classname = "shinobi_fireball";
	ball->s.eType = ET_GENERAL;
	ball->s.generic1 = SHINOBI_ENT_FIREBALL;
	ball->r.ownerNum = ent->s.number;
	ball->parent = ent;
	VectorSet( ball->r.mins, -SHINOBI_FIREBALL_SIZE, -SHINOBI_FIREBALL_SIZE, -SHINOBI_FIREBALL_SIZE );
	VectorSet( ball->r.maxs, SHINOBI_FIREBALL_SIZE, SHINOBI_FIREBALL_SIZE, SHINOBI_FIREBALL_SIZE );
	ball->r.contents = 0;

	ball->s.pos.trType = TR_LINEAR;
	ball->s.pos.trTime = level.time;
	VectorCopy( start, ball->s.pos.trBase );
	VectorScale( fwd, SHINOBI_FIREBALL_SPEED, ball->s.pos.trDelta );
	VectorCopy( start, ball->s.origin );
	VectorCopy( start, ball->r.currentOrigin );

	ball->genericValue1 = level.time + SHINOBI_FIREBALL_LIFE;
	ball->think = Shinobi_FireballThink;
	ball->nextthink = level.time + FRAMETIME;
	trap->LinkEntity( (sharedEntity_t *)ball );

	// throw it with the off hand
	client->ps.forceHandExtend = HANDEXTEND_FORCEPUSH;
	client->ps.forceHandExtendTime = level.time + 500;
	G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFireballCast );
}

/*
===========================================================================
Doton: earth wall
===========================================================================
*/
#define WALL_TRAVEL ( SHINOBI_WALL_HEIGHT + 8 ) // how far a pillar rises / sinks

static void Shinobi_WallFree( gentity_t *self ) {
	G_FreeEntity( self );
}

static void Shinobi_WallDie( gentity_t *self, gentity_t *inflictor, gentity_t *attacker, int damage, int mod ) {
	vec3_t center, up = { 0, 0, 1 };

	VectorCopy( self->r.currentOrigin, center );
	center[2] += SHINOBI_WALL_HEIGHT * 0.5f;
	G_PlayEffectID( shinobiMedia.fxWallBreak, center, up );
	G_SoundAtLoc( center, CHAN_AUTO, shinobiMedia.sndWallBreak );

	// gone right away for collision and rendering, freed next frame (we are inside G_Damage)
	self->takedamage = qfalse;
	self->r.contents = 0;
	self->s.eFlags |= EF_NODRAW;
	trap->UnlinkEntity( (sharedEntity_t *)self );
	self->think = Shinobi_WallFree;
	self->nextthink = level.time + FRAMETIME;
}

static void Shinobi_WallSink( gentity_t *self ) {
	vec3_t up = { 0, 0, 1 };

	self->takedamage = qfalse;
	self->r.contents = 0;

	self->s.pos.trType = TR_LINEAR_STOP;
	self->s.pos.trTime = level.time;
	self->s.pos.trDuration = SHINOBI_WALL_SINK_MS;
	VectorCopy( self->r.currentOrigin, self->s.pos.trBase );
	VectorSet( self->s.pos.trDelta, 0, 0, -WALL_TRAVEL * 1000.0f / SHINOBI_WALL_SINK_MS );
	trap->LinkEntity( (sharedEntity_t *)self );

	G_PlayEffectID( shinobiMedia.fxWallRise, self->r.currentOrigin, up );
	self->think = Shinobi_WallFree;
	self->nextthink = level.time + SHINOBI_WALL_SINK_MS;
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

static void Shinobi_SpawnPillar( gentity_t *caster, vec3_t ground, float yaw ) {
	gentity_t *p = G_Spawn();
	vec3_t     up = { 0, 0, 1 };

	if ( !p )
		return;

	p->classname = "shinobi_earthwall";
	p->s.eType = ET_GENERAL;
	p->s.modelindex = shinobiMedia.wallModels[Q_irand( 0, SHINOBI_WALL_MODELS - 1 )];
	p->r.ownerNum = ENTITYNUM_NONE; // the caster must collide with it too
	p->parent = caster;

	VectorSet( p->r.mins, -SHINOBI_WALL_HALF_WIDTH, -SHINOBI_WALL_HALF_WIDTH, 0 );
	VectorSet( p->r.maxs, SHINOBI_WALL_HALF_WIDTH, SHINOBI_WALL_HALF_WIDTH, SHINOBI_WALL_HEIGHT );
	p->r.contents = CONTENTS_SOLID;
	p->clipmask = MASK_SOLID;

	p->takedamage = qtrue;
	p->health = SHINOBI_WALL_HEALTH;
	p->die = Shinobi_WallDie;

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

	p->think = Shinobi_WallSink;
	p->nextthink = level.time + SHINOBI_WALL_LIFETIME;
	trap->LinkEntity( (sharedEntity_t *)p );

	G_PlayEffectID( shinobiMedia.fxWallRise, ground, up );
}

void Cmd_Doton_f( gentity_t *ent ) {
	gclient_t *client = ent->client;
	vec3_t     yawAngles, fwd, right, center;
	vec3_t     spots[SHINOBI_WALL_PILLARS], grounds[SHINOBI_WALL_PILLARS];
	int        i, count = 0;

	if ( !Shinobi_CanAct( ent ) )
		return;
	if ( client->shinobi.wallNext > level.time )
		return;
	if ( client->ps.fd.forcePower < SHINOBI_WALL_COST ) {
		Shinobi_SpendChakra( ent, SHINOBI_WALL_COST ); // prints the message
		return;
	}

	VectorSet( yawAngles, 0, client->ps.viewangles[YAW], 0 );
	AngleVectors( yawAngles, fwd, right, NULL );
	VectorMA( client->ps.origin, SHINOBI_WALL_DISTANCE, fwd, center );

	for ( i = 0; i < SHINOBI_WALL_PILLARS; i++ ) {
		float offset = ( i - ( SHINOBI_WALL_PILLARS - 1 ) * 0.5f ) * SHINOBI_WALL_SPACING;
		VectorMA( center, offset, right, spots[i] );
		if ( Shinobi_WallSpot( ent, spots[i], grounds[count] ) )
			count++;
	}

	if ( !count ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		trap->SendServerCommand( ent - g_entities, "cp \"No ground for an earth wall\"" );
		return;
	}

	Shinobi_SpendChakra( ent, SHINOBI_WALL_COST );
	client->shinobi.wallNext = level.time + SHINOBI_WALL_COOLDOWN;
	Shinobi_StopCharging( client );

	for ( i = 0; i < count; i++ ) {
		Shinobi_SpawnPillar( ent, grounds[i], client->ps.viewangles[YAW] );
	}

	// slam the ground
	client->ps.forceHandExtend = HANDEXTEND_FORCEPUSH;
	client->ps.forceHandExtendTime = level.time + 500;
	G_SoundAtLoc( center, CHAN_AUTO, shinobiMedia.sndWallRise );
}
