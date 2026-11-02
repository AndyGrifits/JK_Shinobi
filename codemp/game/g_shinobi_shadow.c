/*
===========================================================================
JK_Shinobi - Kagemane no Jutsu (Shadow Bind), Yin/Yang school.

Held: your shadow crawls forward along the floor in a straight line (it can't
cross gaps or go through walls). An enemy standing on it - not one in the air -
is caught: until you let go, he moves the way you move over the ground (you back
off - he comes after you), swings when you swing and turns as you turn. Costs chakra while held; getting hit, running dry or
Kawarimi breaks it.
===========================================================================
*/
#include "g_shinobi_local.h"

static void Shinobi_ShadowThink( gentity_t *sh );

void Shinobi_ShadowRelease( gentity_t *ent ) {
	gclient_t *client = ent->client;
	gentity_t *binder = NULL, *victim = NULL;

	if ( client->shinobi.shadowEnt || client->shinobi.shadowTarget ) {
		binder = ent;
		if ( client->shinobi.shadowTarget )
			victim = &g_entities[client->shinobi.shadowTarget - 1];
	}
	else if ( client->shinobi.shadowBy ) {
		victim = ent;
		binder = &g_entities[client->shinobi.shadowBy - 1];
	}
	else {
		return;
	}

	if ( binder && binder->client ) {
		gclient_t *bc = binder->client;
		if ( bc->shinobi.shadowEnt ) {
			gentity_t *sh = &g_entities[bc->shinobi.shadowEnt - 1];
			if ( sh->inuse && sh->classname && !strcmp( sh->classname, "shinobi_shadow" ) )
				G_FreeEntity( sh );
		}
		bc->shinobi.shadowEnt = 0;
		if ( !victim || bc->shinobi.shadowTarget == victim->s.number + 1 )
			bc->shinobi.shadowTarget = 0;
	}
	if ( victim && victim->client && ( !binder || victim->client->shinobi.shadowBy == binder->s.number + 1 ) ) {
		victim->client->shinobi.shadowBy = 0;
		victim->client->ps.stats[STAT_SHINOBI] &= ~SHINOBI_STAT_BOUND;
		Shinobi_CCGrant( victim, level.time );
	}
}

qboolean Shinobi_CastShadow( gentity_t *ent ) {
	gclient_t *client = ent->client;
	gentity_t *sh;
	vec3_t     floor, angles;

	if ( client->shinobi.shadowEnt || SHINOBI_HAND_ANY( &client->ps ) )
		return qfalse;
	if ( !Shinobi_FloorBelow( client->ps.origin, 96, floor ) ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		trap->SendServerCommand( ent - g_entities, "cp \"Shadow Bind: no ground to cast a shadow on\"" );
		return qfalse;
	}

	sh = G_Spawn();
	if ( !sh )
		return qfalse;
	sh->classname = "shinobi_shadow";
	sh->s.eType = ET_GENERAL;
	sh->s.generic1 = SHINOBI_ENT_SHADOW;
	sh->r.ownerNum = ent->s.number;
	sh->r.contents = 0;
	sh->r.svFlags |= SVF_BROADCAST;
	G_SetOrigin( sh, floor );
	VectorCopy( floor, sh->s.angles2 );
	sh->think = Shinobi_ShadowThink;
	sh->nextthink = level.time + FRAMETIME;
	trap->LinkEntity( (sharedEntity_t *)sh );

	VectorSet( angles, 0, client->ps.viewangles[YAW], 0 );
	AngleVectors( angles, client->shinobi.shadowDir, NULL, NULL );
	VectorCopy( floor, client->shinobi.shadowStart );
	client->shinobi.shadowLen = 0;
	client->shinobi.shadowTarget = 0;
	client->shinobi.shadowDrain = level.time + SHINOBI_SHADOW_DRAIN_MS;
	client->shinobi.shadowEnt = sh->s.number + 1;

	client->ps.forceHandExtend = HANDEXTEND_TAUNT;
	client->ps.forceDodgeAnim = BOTH_FORCEHEAL_QUICK;
	client->ps.forceHandExtendTime = level.time + 400;
	G_Sound( ent, CHAN_AUTO, shinobiMedia.sndShadow );
	return qtrue;
}

// distance on the floor plane from a point to the segment a-b
static float Shinobi_DistToSegment2D( const vec3_t p, const vec3_t a, const vec3_t b ) {
	vec3_t ab, ap, closest;
	float  t, len2;

	VectorSubtract( b, a, ab );
	ab[2] = 0;
	VectorSubtract( p, a, ap );
	ap[2] = 0;
	len2 = DotProduct( ab, ab );
	t = len2 > 0 ? DotProduct( ap, ab ) / len2 : 0;
	if ( t < 0 ) t = 0;
	if ( t > 1 ) t = 1;
	VectorMA( ap, -t, ab, closest );
	closest[2] = 0;
	return VectorLength( closest );
}

static gentity_t *Shinobi_ShadowCatch( gentity_t *binder, const vec3_t a, const vec3_t b ) {
	int i;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		gentity_t *v = &g_entities[i];
		vec3_t     feet;

		if ( v == binder || !v->inuse || !v->client || v->health <= 0 || v->client->sess.sessionTeam == TEAM_SPECTATOR )
			continue;
		if ( level.gametype >= GT_TEAM && OnSameTeam( binder, v ) )
			continue;
		if ( SHINOBI_UNSEEN( &v->client->ps ) || Shinobi_CCImmune( v ) || v->client->shinobi.shadowBy
			|| v->client->shinobi.prisonCaster )
			continue;
		VectorCopy( v->client->ps.origin, feet );
		feet[2] += v->r.mins[2];
		if ( feet[2] - b[2] > 20 || feet[2] - b[2] < -40 )
			continue; // in the air over it (or far below)
		if ( Shinobi_DistToSegment2D( feet, a, b ) <= SHINOBI_SHADOW_WIDTH )
			return v;
	}
	return NULL;
}

static void Shinobi_ShadowThink( gentity_t *sh ) {
	gentity_t *binder = &g_entities[sh->r.ownerNum];
	gclient_t *client = binder->client;
	vec3_t     tip, feet;

	if ( !binder->inuse || !client || client->shinobi.shadowEnt != sh->s.number + 1 ) {
		G_FreeEntity( sh );
		return;
	}
	if ( binder->health <= 0 || client->shinobi.prisonCaster || client->shinobi.stunEnd > level.time
		|| client->shinobi.shadowBy || !Shinobi_CanAct( binder ) ) {
		Shinobi_ShadowRelease( binder );
		return;
	}

	// chakra
	client->ps.fd.forcePowerRegenDebounceTime = level.time + 500;
	if ( client->shinobi.shadowDrain <= level.time ) {
		client->shinobi.shadowDrain = level.time + SHINOBI_SHADOW_DRAIN_MS;
		if ( client->ps.fd.forcePower < SHINOBI_SHADOW_DRAIN ) {
			trap->SendServerCommand( binder - g_entities, "cp \"Out of chakra: the shadow pulls back\"" );
			Shinobi_ShadowRelease( binder );
			return;
		}
		client->ps.fd.forcePower -= SHINOBI_SHADOW_DRAIN;
	}

	VectorCopy( sh->s.angles2, tip );

	if ( !client->shinobi.shadowTarget ) {
		// crawl forward along the floor
		float   len = client->shinobi.shadowLen + SHINOBI_SHADOW_SPEED * ( FRAMETIME / 1000.0f );
		vec3_t  next, from, to, floor;
		trace_t tr;

		if ( len > SHINOBI_SHADOW_RANGE )
			len = SHINOBI_SHADOW_RANGE;
		VectorMA( client->shinobi.shadowStart, len, client->shinobi.shadowDir, next );
		next[2] = tip[2];
		VectorCopy( tip, from );
		from[2] += 16;
		VectorCopy( next, to );
		to[2] += 16;
		trap->Trace( &tr, from, NULL, NULL, to, ENTITYNUM_NONE, MASK_SOLID, qfalse, 0, 0 );
		if ( tr.fraction >= 1.0f && Shinobi_FloorBelow( to, 48, floor ) ) {
			gentity_t *v = Shinobi_ShadowCatch( binder, tip, floor );
			client->shinobi.shadowLen = len;
			VectorCopy( floor, tip );
			if ( v ) {
				client->shinobi.shadowTarget = v->s.number + 1;
				v->client->shinobi.shadowBy = binder->s.number + 1;
				v->client->shinobi.shadowYaw = AngleSubtract( v->client->ps.viewangles[YAW], client->ps.viewangles[YAW] );
				v->client->shinobi.chargeJutsu = SJ_NONE;
				v->client->ps.userInt1 = v->client->ps.userInt2 = v->client->ps.userInt3 = 0; // drops a Rasengan / Chidori
				if ( v->client->shinobi.flameEnt )
					Shinobi_FlameStop( v );
				G_Sound( v, CHAN_AUTO, shinobiMedia.sndShadow );
				Shinobi_HitText( v, STX_BOUND, 0 );
				G_LogPrintf( "[shinobi] Shadow Bind: %i caught %i\n", binder->s.number, v->s.number );
				trap->SendServerCommand( v - g_entities, "cp \"^0Shadow Bind!^7\nyou move as he moves - Kawarimi gets you out\"" );
				trap->SendServerCommand( binder - g_entities, "cp \"Caught! They copy your every move\"" );
			}
		}
		// else: blocked by a wall or a gap - it stops growing there
	}
	else {
		gentity_t *v = &g_entities[client->shinobi.shadowTarget - 1];
		if ( !v->inuse || !v->client || v->health <= 0 || v->client->shinobi.shadowBy != binder->s.number + 1
			|| Distance( binder->r.currentOrigin, v->r.currentOrigin ) > SHINOBI_SHADOW_HOLD_RANGE ) {
			Shinobi_ShadowRelease( binder );
			return;
		}
		VectorCopy( v->client->ps.origin, tip );
		tip[2] += v->r.mins[2];
	}

	VectorCopy( client->ps.origin, feet );
	feet[2] += binder->r.mins[2];
	G_SetOrigin( sh, feet );
	VectorCopy( tip, sh->s.angles2 );
	trap->LinkEntity( (sharedEntity_t *)sh );
	sh->nextthink = level.time + FRAMETIME;
}

// the victim's side, every client frame before Pmove: the binder's moves
void Shinobi_ShadowVictimThink( gentity_t *ent, usercmd_t *ucmd ) {
	gclient_t *client = ent->client;
	gentity_t *binder = &g_entities[client->shinobi.shadowBy - 1];
	vec3_t     angles;
	const int  mirrored = BUTTON_ATTACK | BUTTON_ALT_ATTACK;

	if ( !binder->inuse || !binder->client || binder->client->shinobi.shadowTarget != ent->s.number + 1 ) {
		Shinobi_ShadowRelease( ent );
		return;
	}
	ucmd->generic_cmd = 0; // no force powers, saber style changes... of his own
	{ // the binder's keys turned into his own frame: he goes the same way over the
	  // ground as the binder (the binder backs off - he comes after him), facing as he faces
		const float a = DEG2RAD( client->shinobi.shadowYaw );
		const float f = binder->client->pers.cmd.forwardmove, r = binder->client->pers.cmd.rightmove;
		const float nf = f * cosf( a ) - r * sinf( a );
		const float nr = f * sinf( a ) + r * cosf( a );
		ucmd->forwardmove = (signed char)Com_Clamp( -127, 127, nf );
		ucmd->rightmove = (signed char)Com_Clamp( -127, 127, nr );
	}
	ucmd->upmove = binder->client->pers.cmd.upmove < 0 ? binder->client->pers.cmd.upmove : 0; // crouches with him, never jumps
	ucmd->buttons = ( ucmd->buttons & ~( mirrored | BUTTON_USE | BUTTON_FORCEPOWER | BUTTON_FORCEGRIP
		| BUTTON_FORCE_LIGHTNING | BUTTON_FORCE_DRAIN | BUTTON_USE_HOLDABLE | BUTTON_WALKING ) )
		| ( binder->client->pers.cmd.buttons & ( mirrored | BUTTON_WALKING ) ) | SHINOBI_BUTTON_MIRROR;

	VectorCopy( binder->client->ps.viewangles, angles );
	angles[YAW] += client->shinobi.shadowYaw;
	angles[ROLL] = 0;
	SetClientViewAngle( ent, angles );

	// and the binder's very speed over the ground: even if something else feeds
	// this player moves, he goes where the binder goes
	client->ps.velocity[0] = binder->client->ps.velocity[0];
	client->ps.velocity[1] = binder->client->ps.velocity[1];
	client->ps.stats[STAT_SHINOBI] |= SHINOBI_STAT_BOUND;

	if ( client->shinobi.debugNext <= level.time && trap->Cvar_VariableIntegerValue( "shinobi_debug" ) ) {
		client->shinobi.debugNext = level.time + 1000;
		trap->Print( "[shinobi] bound %i by %i: cmd %i/%i/%i btn %x vel %.0f %.0f binder cmd %i/%i/%i vel %.0f %.0f\n",
			ent->s.number, binder->s.number, ucmd->forwardmove, ucmd->rightmove, ucmd->upmove, ucmd->buttons,
			client->ps.velocity[0], client->ps.velocity[1], binder->client->pers.cmd.forwardmove,
			binder->client->pers.cmd.rightmove, binder->client->pers.cmd.upmove,
			binder->client->ps.velocity[0], binder->client->ps.velocity[1] );
	}
}
