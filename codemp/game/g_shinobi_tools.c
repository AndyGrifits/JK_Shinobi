/*
===========================================================================
JK_Shinobi - ninja tools picked up on the maps: pills (at once), Explosive
Kunai (G), Explosive Tags (H), Fuma Shuriken (J). See bg_shinobi.h.
===========================================================================
*/
#include "g_shinobi_local.h"

// Touch_Item: one of ours? returns the respawn time in seconds, or -1
int Shinobi_PickupItem( gentity_t *ent, gentity_t *other ) {
	gclient_t *client = other->client;
	int        amount = 0;
	const int  tool = ent->item ? Shinobi_ToolForItem( ent->item->classname, &amount ) : STOOL_NONE;

	if ( tool == STOOL_NONE || !client )
		return -1;
	switch ( tool ) {
	case STOOL_HEAL:
		other->health += amount;
		if ( other->health > client->ps.stats[STAT_MAX_HEALTH] )
			other->health = client->ps.stats[STAT_MAX_HEALTH];
		client->ps.stats[STAT_HEALTH] = other->health;
		return 20;
	case STOOL_CHAKRA:
		client->ps.fd.forcePower += amount;
		if ( client->ps.fd.forcePower > client->ps.fd.forcePowerMax )
			client->ps.fd.forcePower = client->ps.fd.forcePowerMax;
		return 20;
	case STOOL_EXKUNAI:
	case STOOL_TAG:
	case STOOL_SHURIKEN: {
		const int slot = tool == STOOL_EXKUNAI ? SHINOBI_AMMO_EXKUNAI : ( tool == STOOL_TAG ? SHINOBI_AMMO_TAG : SHINOBI_AMMO_SHURIKEN );
		client->ps.ammo[slot] += amount;
		if ( client->ps.ammo[slot] > ( tool == STOOL_SHURIKEN ? SHINOBI_SHURIKEN_MAX : SHINOBI_TOOL_MAX ) )
			client->ps.ammo[slot] = tool == STOOL_SHURIKEN ? SHINOBI_SHURIKEN_MAX : SHINOBI_TOOL_MAX;
		client->ps.stats[STAT_WEAPONS] |= 1 << ( tool == STOOL_EXKUNAI ? SHINOBI_WP_EXKUNAI : ( tool == STOOL_TAG ? SHINOBI_WP_TAG : SHINOBI_WP_SHURIKEN ) );
		return 30;
	}
	}
	return -1;
}

static qboolean Shinobi_ToolReady( gentity_t *ent, int slot ) {
	gclient_t *client = ent->client;

	if ( !Shinobi_CanAct( ent ) || client->shinobi.blocking || client->shinobi.burrowEnd || client->shinobi.shadowHideEnd
		|| client->shinobi.prisonCaster || client->shinobi.shadowBy || client->shinobi.shadowTrapEnt || client->shinobi.stunEnd > level.time || client->shinobi.flinchEnd > level.time
		|| SHINOBI_HAND_ANY( &client->ps ) || client->shinobi.toolNext > level.time )
		return qfalse;
	if ( client->ps.ammo[slot] <= 0 ) {
		Shinobi_Debug( "%i tool slot %i: none left", ent->s.number, slot );
		trap->SendServerCommand( ent - g_entities, "cp \"None left\"" );
		client->shinobi.toolNext = level.time + 400;
		return qfalse;
	}
	return qtrue;
}

static void Shinobi_ThrowPose( gentity_t *ent ) {
	ent->client->ps.forceHandExtend = HANDEXTEND_FORCEPUSH;
	ent->client->ps.forceHandExtendTime = level.time + 250;
}

// a blast of an explosive tool: fire damage around, sets people (and ash) alight
static void Shinobi_ToolBlast( gentity_t *src, gentity_t *owner, vec3_t at, int damage, float radius ) {
	vec3_t up = { 0, 0, 1 };

	if ( !owner || !owner->inuse )
		owner = src;
	shinobiHitElement = SE_FIRE;
	shinobiJutsuDamage = 1;
	G_RadiusDamage( at, owner, damage, radius, NULL, src, MOD_ROCKET_SPLASH );
	shinobiJutsuDamage = 0;
	shinobiHitElement = SE_NONE;
	Shinobi_IgniteAshAt( at, radius * 0.5f, owner->client ? owner->s.number + 1 : 0 );
	G_PlayEffectID( shinobiMedia.fxFireballBoom[0], at, up );
	G_PlayEffectID( shinobiMedia.fxStockExplode, at, up );
	G_SoundAtLoc( at, CHAN_AUTO, shinobiMedia.sndFireballBoom );
	G_ScreenShake( at, NULL, 2.0f, 300, qfalse );
}

/*
---------------------------------------------------------------------------
Explosive Kunai (G): thrown like a kunai; it sticks where it lands - or in
whomever it hits - and goes off a second later.
---------------------------------------------------------------------------
*/
#define EXK_ON(k)   ( (k)->genericValue3 )  // stuck in this entity num + 1

static void Shinobi_ExKunaiThink( gentity_t *k ) {
	gentity_t *owner = &g_entities[k->r.ownerNum];
	vec3_t     newPos, vel, mins = { -2, -2, -2 }, maxs = { 2, 2, 2 };
	trace_t    tr;

	if ( k->s.time2 && level.time >= k->s.time2 ) { // boom
		vec3_t at;
		VectorCopy( k->r.currentOrigin, at );
		Shinobi_ToolBlast( k, owner->inuse ? owner : NULL, at, SHINOBI_EXKUNAI_DAMAGE, SHINOBI_EXKUNAI_RADIUS );
		G_FreeEntity( k );
		return;
	}
	if ( EXK_ON( k ) ) { // riding in someone
		gentity_t *t = &g_entities[EXK_ON( k ) - 1];
		if ( t->inuse && t->health > 0 ) {
			vec3_t pos;
			VectorCopy( t->r.currentOrigin, pos );
			pos[2] += 8;
			G_SetOrigin( k, pos );
			trap->LinkEntity( (sharedEntity_t *)k );
		}
		k->nextthink = level.time + FRAMETIME;
		return;
	}
	if ( k->s.time2 ) { // stuck in a wall, sizzling
		k->nextthink = level.time + FRAMETIME;
		return;
	}
	if ( level.time >= k->genericValue1 ) { // flew too long: goes off in the air
		k->s.time2 = level.time;
		k->nextthink = level.time + FRAMETIME;
		return;
	}

	BG_EvaluateTrajectory( &k->s.pos, level.time, newPos );
	BG_EvaluateTrajectoryDelta( &k->s.pos, level.time, vel );
	vectoangles( vel, k->s.apos.trBase );
	VectorCopy( k->s.apos.trBase, k->r.currentAngles );
	trap->Trace( &tr, k->r.currentOrigin, mins, maxs, newPos, k->r.ownerNum, MASK_SHOT, qfalse, 0, 0 );
	if ( tr.startsolid || tr.fraction < 1.0f ) {
		gentity_t *other = tr.entityNum < ENTITYNUM_WORLD ? &g_entities[tr.entityNum] : NULL;
		G_SetOrigin( k, tr.endpos );
		k->s.pos.trType = TR_STATIONARY;
		k->s.time2 = level.time + SHINOBI_EXKUNAI_FUSE;
		if ( other && other->takedamage && other->client )
			EXK_ON( k ) = other->s.number + 1;
		else if ( other && other->takedamage && other->classname && !strcmp( other->classname, "shinobi_tag" ) )
			k->s.time2 = level.time; // straight into a tag: both go off
		G_Sound( k, CHAN_AUTO, shinobiMedia.sndIgnite );
		trap->LinkEntity( (sharedEntity_t *)k );
		k->nextthink = level.time + FRAMETIME;
		return;
	}
	VectorCopy( newPos, k->r.currentOrigin );
	trap->LinkEntity( (sharedEntity_t *)k );
	k->nextthink = level.time + FRAMETIME;
}

static void Shinobi_SpawnExKunai( gentity_t *ent ) {
	gclient_t *client = ent->client;
	gentity_t *k;
	vec3_t     fwd, eye, start;
	trace_t    tr;

	AngleVectors( client->ps.viewangles, fwd, NULL, NULL );
	VectorCopy( client->ps.origin, eye );
	eye[2] += client->ps.viewheight - 4;
	VectorMA( eye, 18, fwd, start );
	trap->Trace( &tr, eye, NULL, NULL, start, ent->s.number, MASK_SOLID, qfalse, 0, 0 );
	VectorCopy( tr.endpos, start );

	k = G_Spawn();
	if ( !k )
		return;
	k->classname = "shinobi_exkunai";
	k->s.eType = ET_GENERAL;
	k->s.modelindex = shinobiMedia.exKunaiModel;
	k->s.generic1 = SHINOBI_ENT_EXKUNAI;
	k->r.ownerNum = ent->s.number;
	k->s.owner = ent->s.number; // (the Sharingan draws the paths of enemy missiles)
	k->parent = ent;
	k->r.contents = 0;
	VectorSet( k->r.mins, -2, -2, -2 );
	VectorSet( k->r.maxs, 2, 2, 2 );
	k->genericValue1 = level.time + 3000;
	k->s.pos.trType = TR_GRAVITY;
	k->s.pos.trTime = level.time;
	VectorCopy( start, k->s.pos.trBase );
	VectorScale( fwd, SHINOBI_KUNAI_SPEED, k->s.pos.trDelta );
	k->s.pos.trDelta[2] += 40;
	VectorCopy( start, k->r.currentOrigin );
	k->s.apos.trType = TR_STATIONARY;
	vectoangles( fwd, k->s.apos.trBase );
	k->think = Shinobi_ExKunaiThink;
	k->nextthink = level.time + FRAMETIME;
	trap->LinkEntity( (sharedEntity_t *)k );
	G_Sound( ent, CHAN_AUTO, shinobiMedia.sndWind );
}

/*
---------------------------------------------------------------------------
Explosive Tag (H): stuck to the wall or floor ahead. Armed after a moment,
it goes off shortly after an enemy comes near - or at once when fire or a
blast reaches it (tags set each other off).
---------------------------------------------------------------------------
*/
static void Shinobi_TagBlow( gentity_t *t ) {
	gentity_t *owner = &g_entities[t->r.ownerNum];
	vec3_t     at;

	t->takedamage = qfalse;
	VectorCopy( t->r.currentOrigin, at );
	Shinobi_ToolBlast( t, owner->inuse ? owner : NULL, at, SHINOBI_TAG_DAMAGE, SHINOBI_TAG_RADIUS );
	G_FreeEntity( t );
}

static void Shinobi_TagDie( gentity_t *self, gentity_t *inflictor, gentity_t *attacker, int damage, int mod ) {
	if ( self->s.time2 && self->s.time2 <= level.time + 100 )
		return;
	self->takedamage = qfalse;
	self->s.time2 = level.time + 120; // set off by a blast: goes a moment later (chains)
	self->nextthink = level.time + FRAMETIME;
}

static void Shinobi_TagThink( gentity_t *t ) {
	gentity_t *owner = &g_entities[t->r.ownerNum];
	int        i;

	if ( t->s.time2 ) {
		if ( level.time >= t->s.time2 ) {
			Shinobi_TagBlow( t );
			return;
		}
		t->nextthink = level.time + FRAMETIME;
		return;
	}
	if ( level.time >= t->genericValue1 || !owner->inuse || !owner->client ) { // old, or its maker left
		G_FreeEntity( t );
		return;
	}
	if ( level.time >= t->s.time ) { // armed: anyone coming close?
		for ( i = 0; i < level.num_entities; i++ ) {
			gentity_t *v = &g_entities[i];
			trace_t    tr;

			if ( !v->inuse || !v->client || v->health <= 0 || v == owner )
				continue;
			if ( v->s.number < MAX_CLIENTS && ( v->client->sess.sessionTeam == TEAM_SPECTATOR || SHINOBI_UNSEEN( &v->client->ps ) ) )
				continue;
			if ( v->shinobiCloneOwner == owner->s.number + 1 || ( level.gametype >= GT_TEAM && OnSameTeam( owner, v ) ) )
				continue;
			if ( Distance( v->r.currentOrigin, t->r.currentOrigin ) > SHINOBI_TAG_TRIGGER )
				continue;
			trap->Trace( &tr, t->r.currentOrigin, NULL, NULL, v->r.currentOrigin, t->s.number, MASK_SOLID, qfalse, 0, 0 );
			if ( tr.fraction < 1.0f )
				continue;
			t->s.time2 = level.time + SHINOBI_TAG_FUSE;
			G_Sound( t, CHAN_AUTO, shinobiMedia.sndIgnite );
			break;
		}
	}
	t->nextthink = level.time + 100;
}

static qboolean Shinobi_PlaceTag( gentity_t *ent ) {
	gclient_t *client = ent->client;
	gentity_t *t, *oldest = NULL;
	vec3_t     fwd, eye, end, normal;
	trace_t    tr;
	int        i, count = 0;

	AngleVectors( client->ps.viewangles, fwd, NULL, NULL );
	VectorCopy( client->ps.origin, eye );
	eye[2] += client->ps.viewheight;
	VectorMA( eye, SHINOBI_TAG_REACH, fwd, end );
	trap->Trace( &tr, eye, NULL, NULL, end, ent->s.number, MASK_SOLID, qfalse, 0, 0 );
	if ( tr.fraction >= 1.0f || tr.entityNum != ENTITYNUM_WORLD ) { // nothing ahead: the floor at our feet
		vec3_t down;
		VectorCopy( client->ps.origin, end );
		VectorCopy( end, down );
		down[2] -= 96;
		trap->Trace( &tr, end, NULL, NULL, down, ent->s.number, MASK_SOLID, qfalse, 0, 0 );
		if ( tr.fraction >= 1.0f || tr.entityNum != ENTITYNUM_WORLD ) {
			trap->SendServerCommand( ent - g_entities, "cp \"Nowhere to stick it\"" );
			return qfalse;
		}
	}
	VectorCopy( tr.plane.normal, normal );

	// only so many at once: the oldest goes
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *e = &g_entities[i];
		if ( !e->inuse || !e->classname || strcmp( e->classname, "shinobi_tag" ) || e->r.ownerNum != ent->s.number || e->s.time2 )
			continue;
		count++;
		if ( !oldest || e->genericValue2 < oldest->genericValue2 )
			oldest = e;
	}
	if ( count >= SHINOBI_TAG_MAX && oldest )
		G_FreeEntity( oldest );

	t = G_Spawn();
	if ( !t )
		return qfalse;
	t->classname = "shinobi_tag";
	t->s.eType = ET_GENERAL;
	t->s.modelindex = shinobiMedia.tagModel;
	t->s.generic1 = SHINOBI_ENT_TAG;
	t->r.ownerNum = ent->s.number;
	t->parent = ent;
	t->r.contents = CONTENTS_CORPSE; // shots and kunai can hit it, people walk over it
	VectorSet( t->r.mins, -4, -4, -4 );
	VectorSet( t->r.maxs, 4, 4, 4 );
	t->takedamage = qtrue;
	t->health = 1;
	t->die = Shinobi_TagDie;
	t->s.time = level.time + SHINOBI_TAG_ARM_MS;
	t->genericValue1 = level.time + SHINOBI_TAG_LIFE;
	t->genericValue2 = level.time; // placed at
	vectoangles( normal, t->s.angles ); // the paper lies flat on the surface (+X out of it)
	t->s.angles[ROLL] = flrand( 0, 360 );
	VectorCopy( t->s.angles, t->r.currentAngles );
	VectorCopy( t->s.angles, t->s.apos.trBase );
	VectorMA( tr.endpos, 0.6f, normal, end );
	G_SetOrigin( t, end );
	t->think = Shinobi_TagThink;
	t->nextthink = level.time + 100;
	trap->LinkEntity( (sharedEntity_t *)t );

	G_Sound( ent, CHAN_AUTO, shinobiMedia.sndPoof );
	return qtrue;
}

/*
---------------------------------------------------------------------------
Fuma Shuriken (J): one at a time. Thrown like a saber: it flies out spinning,
turns at SHINOBI_SHURIKEN_RANGE and homes back to the thrower's hand, cutting
everyone it passes (once out, once back). Caught: ready to throw again. A wall
on the way keeps it; anyone without one can walk up and pull it out.
---------------------------------------------------------------------------
*/
#define SHUR_BACK(s)     ( (s)->genericValue2 )   // on the way back
#define SHUR_HITOUT(s)   ( (s)->genericValue3 )   // clients cut on the way out (bits)
#define SHUR_HITBACK(s)  ( (s)->genericValue4 )   // ... and back
#define SHUR_LASTHIT(s)  ( (s)->genericValue5 )   // last non-client entity cut + 1

// give the thrower (or whoever pulls it out) the shuriken back
static qboolean Shinobi_ShurikenGive( gentity_t *e ) {
	gclient_t *c = e->client;
	if ( !c || e->health <= 0 || c->ps.ammo[SHINOBI_AMMO_SHURIKEN] >= SHINOBI_SHURIKEN_MAX )
		return qfalse;
	c->ps.ammo[SHINOBI_AMMO_SHURIKEN] = SHINOBI_SHURIKEN_MAX;
	c->ps.stats[STAT_WEAPONS] |= 1 << SHINOBI_WP_SHURIKEN;
	G_Sound( e, CHAN_AUTO, shinobiMedia.sndBlockSaber[2] );
	return qtrue;
}

static void Shinobi_ShurikenStick( gentity_t *s, const vec3_t at ) {
	G_SetOrigin( s, (float *)at );
	s->s.apos.trType = TR_STATIONARY;
	BG_EvaluateTrajectory( &s->s.apos, level.time, s->s.apos.trBase );
	s->s.pos.trType = TR_STATIONARY;
	s->genericValue1 = level.time + SHINOBI_SHURIKEN_STUCK_LIFE;
	G_SoundAtLoc( (float *)at, CHAN_AUTO, shinobiMedia.sndBlockSaber[1] );
	trap->LinkEntity( (sharedEntity_t *)s );
}

static void Shinobi_ShurikenThink( gentity_t *s ) {
	gentity_t *owner = &g_entities[s->r.ownerNum];
	vec3_t     newPos, dir, mins = { -10, -10, -3 }, maxs = { 10, 10, 3 };
	trace_t    tr;
	int        pass;

	if ( s->s.pos.trType == TR_STATIONARY ) { // stuck in a wall: walk up to it to pull it out
		int i;
		if ( level.time >= s->genericValue1 ) {
			G_FreeEntity( s );
			return;
		}
		for ( i = 0; i < MAX_CLIENTS; i++ ) {
			gentity_t *e = &g_entities[i];
			if ( e->inuse && e->client && e->health > 0 && e->client->sess.sessionTeam != TEAM_SPECTATOR
				&& Distance( e->client->ps.origin, s->r.currentOrigin ) <= SHINOBI_SHURIKEN_GRAB + 24
				&& Shinobi_ShurikenGive( e ) ) {
				G_FreeEntity( s );
				return;
			}
		}
		s->nextthink = level.time + FRAMETIME;
		return;
	}
	if ( level.time >= s->genericValue1 || !owner->inuse || !owner->client || owner->health <= 0 ) {
		G_FreeEntity( s ); // lost
		return;
	}

	// the turn: out far enough - home back to the hand from here on
	if ( !SHUR_BACK( s ) ) {
		BG_EvaluateTrajectory( &s->s.pos, level.time, newPos );
		if ( Distance( newPos, s->s.origin2 ) >= SHINOBI_SHURIKEN_RANGE )
			SHUR_BACK( s ) = 1;
	}
	if ( SHUR_BACK( s ) ) {
		vec3_t hand;
		VectorCopy( owner->client->ps.origin, hand );
		hand[2] += owner->client->ps.viewheight - 12;
		if ( Distance( s->r.currentOrigin, hand ) <= SHINOBI_SHURIKEN_CATCH + SHINOBI_SHURIKEN_BACK_SPEED * ( FRAMETIME / 1000.0f ) * 0.5f ) {
			if ( !Shinobi_ShurikenGive( owner ) )
				G_SoundAtLoc( s->r.currentOrigin, CHAN_AUTO, shinobiMedia.sndBlockSaber[2] );
			G_FreeEntity( s ); // caught
			return;
		}
		VectorSubtract( hand, s->r.currentOrigin, dir );
		VectorNormalize( dir );
		VectorCopy( s->r.currentOrigin, s->s.pos.trBase );
		s->s.pos.trTime = level.time;
		VectorScale( dir, SHINOBI_SHURIKEN_BACK_SPEED, s->s.pos.trDelta );
	}

	BG_EvaluateTrajectory( &s->s.pos, level.time + ( SHUR_BACK( s ) ? FRAMETIME : 0 ), newPos );
	if ( !SHUR_BACK( s ) )
		BG_EvaluateTrajectory( &s->s.pos, level.time, newPos );

	// people in the way are cut (and it flies on); a wall keeps it
	for ( pass = 0; pass < 4; pass++ ) {
		gentity_t *other;
		trap->Trace( &tr, s->r.currentOrigin, mins, maxs, newPos, s->r.ownerNum, MASK_SHOT, qfalse, 0, 0 );
		if ( !( tr.startsolid || tr.fraction < 1.0f ) )
			break;
		other = tr.entityNum < ENTITYNUM_WORLD ? &g_entities[tr.entityNum] : NULL;
		if ( other && other->takedamage && ( other->client || other->s.eType == ET_NPC ) ) {
			int *mask = SHUR_BACK( s ) ? &SHUR_HITBACK( s ) : &SHUR_HITOUT( s );
			const qboolean seen = other->s.number < MAX_CLIENTS ? ( *mask & ( 1 << other->s.number ) ) != 0
				: SHUR_LASTHIT( s ) == other->s.number + 1;
			if ( !seen ) {
				VectorCopy( s->s.pos.trDelta, dir );
				VectorNormalize( dir );
				if ( other->s.number < MAX_CLIENTS )
					*mask |= 1 << other->s.number;
				else
					SHUR_LASTHIT( s ) = other->s.number + 1;
				G_Damage( other, s, owner, dir, tr.endpos, SHINOBI_SHURIKEN_DAMAGE, DAMAGE_NO_HIT_LOC, MOD_SABER ); // (the same wherever it cuts)
				G_SoundAtLoc( tr.endpos, CHAN_AUTO, shinobiMedia.sndBlockSaber[0] );
				if ( !s->inuse )
					return;
			}
			// pass through him: trace on from just past him
			{
				trace_t past;
				trap->Trace( &past, tr.endpos, mins, maxs, newPos, other->s.number, MASK_SOLID, qfalse, 0, 0 );
				if ( past.startsolid || past.fraction < 1.0f ) {
					Shinobi_ShurikenStick( s, past.endpos );
					s->nextthink = level.time + FRAMETIME;
					return;
				}
				break;
			}
		}
		if ( other && other->takedamage ) { // something breakable: cut it, stop in it
			VectorCopy( s->s.pos.trDelta, dir );
			VectorNormalize( dir );
			G_Damage( other, s, owner, dir, tr.endpos, SHINOBI_SHURIKEN_DAMAGE, 0, MOD_SABER );
		}
		// into the wall: it stays there
		Shinobi_ShurikenStick( s, tr.endpos );
		s->nextthink = level.time + FRAMETIME;
		return;
	}
	VectorCopy( newPos, s->r.currentOrigin );
	trap->LinkEntity( (sharedEntity_t *)s );
	s->nextthink = level.time + FRAMETIME;
}

static void Shinobi_SpawnShuriken( gentity_t *ent ) {
	gclient_t *client = ent->client;
	gentity_t *s;
	vec3_t     fwd, eye, start;
	trace_t    tr;

	AngleVectors( client->ps.viewangles, fwd, NULL, NULL );
	VectorCopy( client->ps.origin, eye );
	eye[2] += client->ps.viewheight - 6;
	VectorMA( eye, 24, fwd, start );
	trap->Trace( &tr, eye, NULL, NULL, start, ent->s.number, MASK_SOLID, qfalse, 0, 0 );
	VectorCopy( tr.endpos, start );

	s = G_Spawn();
	if ( !s )
		return;
	s->classname = "shinobi_shuriken";
	s->s.eType = ET_GENERAL;
	s->s.modelindex = shinobiMedia.shurikenModel;
	s->s.generic1 = SHINOBI_ENT_SHURIKEN;
	s->r.ownerNum = ent->s.number;
	s->s.owner = ent->s.number; // (the Sharingan draws the paths of enemy missiles)
	s->parent = ent;
	s->r.contents = 0;
	s->genericValue1 = level.time + SHINOBI_SHURIKEN_LIFE;
	s->s.pos.trType = TR_LINEAR;
	s->s.pos.trTime = level.time;
	VectorCopy( start, s->s.pos.trBase );
	VectorScale( fwd, SHINOBI_SHURIKEN_SPEED, s->s.pos.trDelta );
	VectorCopy( start, s->r.currentOrigin );
	VectorCopy( start, s->s.origin2 ); // where it left the hand (the turn is measured from here)
	s->s.apos.trType = TR_LINEAR; // spinning flat
	s->s.apos.trTime = level.time;
	VectorSet( s->s.apos.trBase, 0, client->ps.viewangles[YAW], 0 );
	VectorSet( s->s.apos.trDelta, 0, 1440, 0 );
	s->think = Shinobi_ShurikenThink;
	s->nextthink = level.time + FRAMETIME;
	trap->LinkEntity( (sharedEntity_t *)s );

	G_Sound( ent, CHAN_AUTO, shinobiMedia.sndWind2 );
}

/*
---------------------------------------------------------------------------
Using a tool: the stock weapon fire (attack with it in hand: pmove already
took the ammo and plays the throw), or Z / the console commands (we take the
ammo, and put the weapon away when the last one is gone).
---------------------------------------------------------------------------
*/
// FireWeapon: the stock explosive weapons fire our tools instead
qboolean Shinobi_FireToolWeapon( gentity_t *ent, int weapon ) {
	if ( !ent->client || !SHINOBI_IS_TOOL_WEAPON( weapon ) )
		return qfalse;
	Shinobi_Debug( "%i fires tool weapon %i (left %i)", ent->s.number, weapon, ent->client->ps.ammo[weaponData[weapon].ammoIndex] );
	Shinobi_Reveal( ent );
	if ( weapon == SHINOBI_WP_EXKUNAI )
		Shinobi_SpawnExKunai( ent );
	else if ( weapon == SHINOBI_WP_TAG ) {
		if ( !Shinobi_PlaceTag( ent ) )
			ent->client->ps.ammo[SHINOBI_AMMO_TAG]++; // nowhere to stick it: keep it
	}
	else
		Shinobi_SpawnShuriken( ent );
	if ( ent->client->ps.ammo[weaponData[weapon].ammoIndex] <= 0 ) { // the last one left the hand: back to the fists / blade
		ent->client->ps.ammo[weaponData[weapon].ammoIndex] = 0;
		ent->client->ps.stats[STAT_WEAPONS] &= ~( 1 << weapon );
		G_AddEvent( ent, EV_NOAMMO, weapon );
	}
	return qtrue;
}

static void Shinobi_UseTool( gentity_t *ent, int weapon ) {
	gclient_t *client = ent->client;
	const int  slot = weaponData[weapon].ammoIndex;
	qboolean   used = qtrue;

	if ( !Shinobi_ToolReady( ent, slot ) )
		return;
	Shinobi_Reveal( ent );
	if ( weapon == SHINOBI_WP_EXKUNAI ) {
		Shinobi_SpawnExKunai( ent );
		client->shinobi.toolNext = level.time + SHINOBI_EXKUNAI_COOLDOWN;
	}
	else if ( weapon == SHINOBI_WP_TAG ) {
		used = Shinobi_PlaceTag( ent );
		client->shinobi.toolNext = level.time + SHINOBI_TAG_COOLDOWN;
	}
	else {
		Shinobi_SpawnShuriken( ent );
		client->shinobi.toolNext = level.time + SHINOBI_SHURIKEN_COOLDOWN;
	}
	Shinobi_Debug( "%i uses tool %i: %s (left %i)", ent->s.number, weapon, used ? "ok" : "FAILED", client->ps.ammo[slot] - ( used ? 1 : 0 ) );
	if ( !used )
		return;
	Shinobi_ThrowPose( ent );
	if ( --client->ps.ammo[slot] <= 0 ) { // the last one: away with the weapon (the cgame picks the next)
		client->ps.ammo[slot] = 0;
		client->ps.stats[STAT_WEAPONS] &= ~( 1 << weapon );
		G_AddEvent( ent, EV_NOAMMO, weapon );
	}
}

// Z with a tool in hand (Shinobi's Cmd_Kunai_f asks first)
qboolean Shinobi_ZUsesTool( gentity_t *ent ) {
	const int w = ent->client->ps.weapon;
	if ( !SHINOBI_IS_TOOL_WEAPON( w ) || SHINOBI_MODIFIER( &ent->client->pers.cmd ) )
		return qfalse;
	Shinobi_UseTool( ent, w );
	return qtrue;
}

void Cmd_ExKunai_f( gentity_t *ent ) { Shinobi_UseTool( ent, SHINOBI_WP_EXKUNAI ); }
void Cmd_ExTag_f( gentity_t *ent )   { Shinobi_UseTool( ent, SHINOBI_WP_TAG ); }
void Cmd_Fuma_f( gentity_t *ent )    { Shinobi_UseTool( ent, SHINOBI_WP_SHURIKEN ); }

/*
===========================================================================
No guns: a player only ever holds fists, the saber and the three tool
weapons. Whatever else sneaks in (spawn loadouts, give, siege classes) is
taken away every frame.
===========================================================================
*/
#define SHINOBI_ALLOWED_WEAPONS ( ( 1 << WP_NONE ) | ( 1 << WP_MELEE ) | ( 1 << WP_SABER ) \
	| ( 1 << WP_THERMAL ) | ( 1 << WP_TRIP_MINE ) | ( 1 << WP_DET_PACK ) )

void Shinobi_StripGuns( gentity_t *ent ) {
	playerState_t *ps = &ent->client->ps;

	ps->stats[STAT_WEAPONS] &= SHINOBI_ALLOWED_WEAPONS;
	ps->stats[STAT_WEAPONS] |= ( 1 << WP_MELEE );
	if ( ent->s.number < MAX_CLIENTS && ent->client->pers.shinobiWeapon == SWS_NONE )
		ps->stats[STAT_WEAPONS] &= ~( 1 << WP_SABER ); // fists only
	if ( !( SHINOBI_ALLOWED_WEAPONS & ( 1 << ps->weapon ) ) || !( ps->stats[STAT_WEAPONS] & ( 1 << ps->weapon ) ) ) {
		ps->weapon = ( ps->stats[STAT_WEAPONS] & ( 1 << WP_SABER ) ) ? WP_SABER : WP_MELEE;
		ps->weaponstate = WEAPON_READY;
		ps->weaponTime = 0;
	}
}

/*
===========================================================================
Weapons: our blades are sabers with no light blade (ext_data/sabers/shinobi_*.sab).
What "saber1" / "saber2" ask for is checked against the weapon style
("shinobi_weapon", applied on the spawn) - nothing but ours, a staff only as
a staff, a second blade only with two.
===========================================================================
*/
extern qboolean WP_SaberParseParm( const char *saberName, const char *parmname, char *saberData );

// one of ours? *pair: made to be held as a pair ("shinobiUse two")
static qboolean Shinobi_IsOurSaber( const char *name, qboolean *pair ) {
	char type[64], use[16];

	if ( !name || Q_stricmpn( name, SHINOBI_WEAPON_PREFIX, strlen( SHINOBI_WEAPON_PREFIX ) ) )
		return qfalse;
	type[0] = 0;
	if ( !WP_SaberParseParm( name, "saberType", type ) )
		return qfalse; // no such saber
	if ( !Q_stricmp( type, "SABER_STAFF" ) )
		return qfalse; // (no staffs any more)
	use[0] = 0;
	WP_SaberParseParm( name, "shinobiUse", use );
	if ( pair )
		*pair = !Q_stricmp( use, "two" ) ? qtrue : qfalse;
	return qtrue;
}

// bots: one of each, by slot - katana, two knives, kurosawa, kubikiribocho, samehada, fists
int Shinobi_BotWeaponStyle( gentity_t *ent, const char **saber2 ) {
	static const int styles[] = { SWS_ONE, SWS_TWO, SWS_ONE, SWS_ONE, SWS_ONE, SWS_NONE };
	static const char * const first[] = { "shinobi_katana", "shinobi_knife", "shinobi_kurosawa", "shinobi_kubikiribocho",
		"shinobi_samehada", "shinobi_katana" };
	int k = ent->s.number % (int)ARRAY_LEN( styles );
	if ( !Q_stricmpn( ent->client->pers.netname, "Test", 4 ) )
		k = 0; // the self-test bots (TestA / TestB): the plain katana
	if ( saber2 )
		*saber2 = first[k];
	return styles[k];
}

const char *Shinobi_SaberFor( gentity_t *ent, int saberNum, const char *wanted ) {
	const int style = ent->client->pers.shinobiWeaponNext;
	qboolean  pair = qfalse;
	static char right[64], left[64];

	if ( ( ent->r.svFlags & SVF_BOT ) && !Shinobi_IsOurSaber( wanted, NULL ) )
		Shinobi_BotWeaponStyle( ent, &wanted ); // a bot's own saber (Kyle...): one of ours
	{ // the weapon itself, not a left-hand copy of it
		const int l = wanted ? strlen( wanted ) : 0;
		if ( l > 2 && !Q_stricmp( wanted + l - 2, "_l" ) ) {
			Q_strncpyz( right, wanted, sizeof( right ) );
			right[l - 2] = 0;
			wanted = right;
		}
	}
	// each weapon is held one way: a pair only as two blades, the rest only as one
	if ( style == SWS_TWO ) {
		if ( !Shinobi_IsOurSaber( wanted, &pair ) || !pair )
			wanted = SHINOBI_WEAPON_DEFAULT_TWO;
	}
	else if ( !Shinobi_IsOurSaber( wanted, &pair ) || pair )
		wanted = SHINOBI_WEAPON_DEFAULT;
	if ( saberNum == 1 ) { // the left hand: only with two blades - the pair's left copy ("<name>_l") if there is one
		if ( style != SWS_TWO )
			return "none";
		Com_sprintf( left, sizeof( left ), "%s_l", wanted );
		return Shinobi_IsOurSaber( left, NULL ) ? left : wanted;
	}
	return wanted;
}

/*
Blades fight in their own stance: the .sab's saberStyle (dual for a pair) - the stock stance
switching does nothing. Every client frame.
*/
void Shinobi_BladeStance( gentity_t *ent ) {
	gclient_t *c = ent->client;
	int        learned, style;

	if ( ent->s.number >= MAX_CLIENTS || c->ps.weapon != WP_SABER || !c->saber[0].model[0]
		|| Q_stricmpn( c->saber[0].name, SHINOBI_WEAPON_PREFIX, strlen( SHINOBI_WEAPON_PREFIX ) ) )
		return;
	learned = c->saber[0].stylesLearned;
	for ( style = SS_FAST; style < SS_NUM_SABER_STYLES; style++ )
		if ( learned & ( 1 << style ) )
			break;
	if ( style >= SS_NUM_SABER_STYLES || style == SS_STAFF || ( style == SS_DUAL && !c->saber[1].model[0] ) )
		style = c->saber[1].model[0] ? SS_DUAL : -1; // (no stance of its own: a pair fights dual)
	if ( style < 0 )
		return;
	c->ps.fd.saberAnimLevel = c->ps.fd.saberAnimLevelBase = c->ps.fd.saberDrawAnimLevel = style;
	c->saberCycleQueue = 0;
}

/*
Map entities: only the items that turn into ninja tools (and the team
items the game modes need) are spawned. Shield / ammo dispensers and
emplaced guns are gone.
*/
qboolean Shinobi_SpawnAllowed( const char *classname, const gitem_t *item ) {
	static const char * const banned[] = {
		"misc_shield_floor_unit", "misc_ammo_floor_unit", "misc_model_health_power_converter",
		"misc_model_shield_power_converter", "misc_model_ammo_power_converter", "emplaced_gun",
	};
	int i;

	if ( item ) {
		if ( item->giType == IT_TEAM || ( item->giType == IT_WEAPON && item->giTag == WP_SABER ) )
			return qtrue; // flags, cubes, the Jedi Master saber
		return Shinobi_ToolForItem( item->classname, NULL ) != STOOL_NONE ? qtrue : qfalse;
	}
	for ( i = 0; i < (int)ARRAY_LEN( banned ); i++ ) {
		if ( !Q_stricmp( classname, banned[i] ) )
			return qfalse;
	}
	return qtrue;
}
