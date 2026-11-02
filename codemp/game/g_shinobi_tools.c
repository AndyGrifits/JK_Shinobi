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
		if ( client->ps.ammo[slot] > SHINOBI_TOOL_MAX )
			client->ps.ammo[slot] = SHINOBI_TOOL_MAX;
		client->ps.stats[STAT_WEAPONS] |= 1 << ( tool == STOOL_EXKUNAI ? SHINOBI_WP_EXKUNAI : ( tool == STOOL_TAG ? SHINOBI_WP_TAG : SHINOBI_WP_SHURIKEN ) );
		return 30;
	}
	}
	return -1;
}

static qboolean Shinobi_ToolReady( gentity_t *ent, int slot ) {
	gclient_t *client = ent->client;

	if ( !Shinobi_CanAct( ent ) || client->shinobi.blocking || client->shinobi.burrowEnd || client->shinobi.shadowHideEnd
		|| client->shinobi.prisonCaster || client->shinobi.shadowBy || client->shinobi.stunEnd > level.time
		|| SHINOBI_HAND_ANY( &client->ps ) || client->shinobi.toolNext > level.time )
		return qfalse;
	if ( client->ps.ammo[slot] <= 0 ) {
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
Fuma Shuriken (J): a big spinning star flying straight; hits hard, knocks
back, sticks in walls.
---------------------------------------------------------------------------
*/
static void Shinobi_ShurikenThink( gentity_t *s ) {
	gentity_t *owner = &g_entities[s->r.ownerNum];
	vec3_t     newPos, dir, mins = { -10, -10, -3 }, maxs = { 10, 10, 3 };
	trace_t    tr;

	if ( s->s.pos.trType == TR_STATIONARY ) { // stuck: gone after a while
		G_FreeEntity( s );
		return;
	}
	if ( level.time >= s->genericValue1 ) {
		G_FreeEntity( s );
		return;
	}
	BG_EvaluateTrajectory( &s->s.pos, level.time, newPos );
	trap->Trace( &tr, s->r.currentOrigin, mins, maxs, newPos, s->r.ownerNum, MASK_SHOT, qfalse, 0, 0 );
	if ( tr.startsolid || tr.fraction < 1.0f ) {
		gentity_t *other = tr.entityNum < ENTITYNUM_WORLD ? &g_entities[tr.entityNum] : NULL;
		VectorCopy( s->s.pos.trDelta, dir );
		VectorNormalize( dir );
		if ( other && other->takedamage ) {
			G_Damage( other, s, owner->inuse ? owner : s, dir, tr.endpos, SHINOBI_SHURIKEN_DAMAGE, 0, MOD_SABER );
			G_SoundAtLoc( tr.endpos, CHAN_AUTO, shinobiMedia.sndBlockSaber[0] );
			G_FreeEntity( s );
			return;
		}
		// into the wall: it stays there a moment
		G_SetOrigin( s, tr.endpos );
		s->s.apos.trType = TR_STATIONARY;
		s->s.pos.trType = TR_STATIONARY;
		G_SoundAtLoc( tr.endpos, CHAN_AUTO, shinobiMedia.sndBlockSaber[1] );
		trap->LinkEntity( (sharedEntity_t *)s );
		s->nextthink = level.time + 4000;
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
	s->parent = ent;
	s->r.contents = 0;
	s->genericValue1 = level.time + SHINOBI_SHURIKEN_LIFE;
	s->s.pos.trType = TR_LINEAR;
	s->s.pos.trTime = level.time;
	VectorCopy( start, s->s.pos.trBase );
	VectorScale( fwd, SHINOBI_SHURIKEN_SPEED, s->s.pos.trDelta );
	VectorCopy( start, s->r.currentOrigin );
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
	Shinobi_Reveal( ent );
	if ( weapon == SHINOBI_WP_EXKUNAI )
		Shinobi_SpawnExKunai( ent );
	else if ( weapon == SHINOBI_WP_TAG ) {
		if ( !Shinobi_PlaceTag( ent ) )
			ent->client->ps.ammo[SHINOBI_AMMO_TAG]++; // nowhere to stick it: keep it
	}
	else
		Shinobi_SpawnShuriken( ent );
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
