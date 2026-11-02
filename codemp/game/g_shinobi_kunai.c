/*
===========================================================================
JK_Shinobi - kunai and what is built on them.

  Z            throw a kunai (everyone)
  Shift + Z    throw a marked kunai (Hiraishin seal, 10 chakra): stays 30 s;
               if it hits an enemy, that enemy carries the mark for 12 s
  Hiraishin    (Yin/Yang jutsu) teleport to your newest mark - behind the
               marked enemy if it is on one
  Lightning Trap (Raiton jutsu) every mark of yours discharges: damage and a
               short stun around it

Also here: Shift + attack infuses fists, kicks and the blade with chakra.
===========================================================================
*/
#include "g_shinobi_local.h"

#define KUNAI_MARKED(k)    ( (k)->genericValue2 )
#define KUNAI_EXPIRE(k)    ( (k)->genericValue1 )
#define KUNAI_ON(k)        ( (k)->genericValue3 )  // marked enemy num + 1
#define KUNAI_STUCK(k)     ( (k)->genericValue4 )
#define KUNAI_BORN(k)      ( (k)->genericValue5 )

static int kunaiModel, sndThrow, sndStick, sndFlash;
static int fxFlash, fxFireBody;

void Shinobi_KunaiInit( void ) {
	kunaiModel = G_ModelIndex( SHINOBI_KUNAI_MODEL );
	sndThrow   = G_SoundIndex( "sound/weapons/saber/saberhup1.wav" );
	sndStick   = G_SoundIndex( "sound/weapons/saber/saberhitwall1" );
	sndFlash   = G_SoundIndex( "sound/weapons/force/speed.wav" );
	fxFlash    = G_EffectIndex( SHINOBI_FX_HIRAISHIN );
	fxFireBody = G_EffectIndex( SHINOBI_FX_FIRE_BODY );
}

static qboolean Shinobi_IsMark( gentity_t *k, int ownerNum ) {
	return ( k->inuse && k->classname && !strcmp( k->classname, "shinobi_kunai" ) && KUNAI_MARKED( k )
		&& k->r.ownerNum == ownerNum ) ? qtrue : qfalse;
}

static void Shinobi_KunaiStick( gentity_t *k, vec3_t pos, vec3_t normal ) {
	vec3_t dir;

	VectorCopy( k->s.pos.trDelta, dir );
	VectorNormalize( dir );
	VectorMA( pos, 3, dir, pos ); // bite into the surface
	G_SetOrigin( k, pos );
	VectorCopy( normal, k->movedir );
	KUNAI_STUCK( k ) = 1;
	KUNAI_EXPIRE( k ) = level.time + ( KUNAI_MARKED( k ) ? SHINOBI_MARK_LIFE : SHINOBI_KUNAI_LIFE );
	G_SoundAtLoc( pos, CHAN_AUTO, sndStick );
	trap->LinkEntity( (sharedEntity_t *)k );
}

static void Shinobi_KunaiThink( gentity_t *k ) {
	gentity_t *owner = &g_entities[k->r.ownerNum];
	vec3_t     newPos, vel, mins = { -2, -2, -2 }, maxs = { 2, 2, 2 };
	trace_t    tr;

	if ( level.time >= KUNAI_EXPIRE( k ) ) {
		G_FreeEntity( k );
		return;
	}

	if ( KUNAI_ON( k ) ) { // riding on the enemy it marked
		gentity_t *t = &g_entities[KUNAI_ON( k ) - 1];
		vec3_t     pos;
		if ( !t->inuse || !t->client || t->health <= 0 ) {
			G_FreeEntity( k );
			return;
		}
		VectorCopy( t->r.currentOrigin, pos );
		pos[2] += 6;
		G_SetOrigin( k, pos );
		trap->LinkEntity( (sharedEntity_t *)k );
		k->nextthink = level.time + FRAMETIME;
		return;
	}
	if ( KUNAI_STUCK( k ) ) {
		k->nextthink = KUNAI_EXPIRE( k );
		return;
	}

	// in flight: point first along the curve
	BG_EvaluateTrajectory( &k->s.pos, level.time, newPos );
	BG_EvaluateTrajectoryDelta( &k->s.pos, level.time, vel );
	vectoangles( vel, k->s.apos.trBase );
	VectorCopy( k->s.apos.trBase, k->r.currentAngles );

	trap->Trace( &tr, k->r.currentOrigin, mins, maxs, newPos, k->r.ownerNum, MASK_SHOT, qfalse, 0, 0 );
	if ( tr.startsolid || tr.fraction < 1.0f ) {
		gentity_t *other = ( tr.entityNum < ENTITYNUM_WORLD ) ? &g_entities[tr.entityNum] : NULL;
		vec3_t     dir;

		VectorCopy( vel, dir );
		VectorNormalize( dir );
		if ( other && other->takedamage && ( !owner->inuse || !Shinobi_DamageBlocked( other, owner ) ) ) {
			G_Damage( other, k, owner->inuse ? owner : k, dir, tr.endpos, SHINOBI_KUNAI_DAMAGE, 0, MOD_SABER );
			if ( KUNAI_MARKED( k ) && other->inuse && other->client && other->s.number < MAX_CLIENTS && other->health > 0 ) {
				KUNAI_ON( k ) = other->s.number + 1;
				KUNAI_EXPIRE( k ) = level.time + SHINOBI_MARK_ON_ENEMY;
				k->s.pos.trType = TR_STATIONARY;
				if ( owner->inuse )
					trap->SendServerCommand( owner - g_entities, "cp \"^3Marked!\"" );
				k->nextthink = level.time + FRAMETIME;
				return;
			}
			G_FreeEntity( k );
			return;
		}
		if ( other && other->client ) { // a body we may not hurt (own clone): drop it
			G_FreeEntity( k );
			return;
		}
		Shinobi_KunaiStick( k, tr.endpos, tr.plane.normal );
		k->nextthink = KUNAI_EXPIRE( k );
		return;
	}

	VectorCopy( newPos, k->r.currentOrigin );
	trap->LinkEntity( (sharedEntity_t *)k );
	k->nextthink = level.time + FRAMETIME;
}

void Cmd_Kunai_f( gentity_t *ent ) {
	gclient_t *client = ent->client;
	const qboolean marked = SHINOBI_MODIFIER( &client->pers.cmd );
	gentity_t *k;
	vec3_t     fwd, eye, start;
	trace_t    tr;
	int        i;

	if ( Shinobi_ZUsesTool( ent ) )
		return; // an explosive kunai / tag / shuriken in hand: Z uses that
	if ( client->shinobi.blocking || client->shinobi.burrowEnd || client->shinobi.shadowHideEnd )
		return; // guard up / out of sight

	if ( !Shinobi_CanAct( ent ) || client->shinobi.prisonCaster || client->shinobi.shadowBy || client->shinobi.shadowTrapEnt
		|| client->shinobi.stunEnd > level.time || client->shinobi.flinchEnd > level.time || SHINOBI_HAND_ANY( &client->ps ) )
		return;
	if ( client->shinobi.kunaiNext > level.time )
		return;
	if ( marked && !Shinobi_SpendChakra( ent, SHINOBI_MARK_COST ) )
		return;
	Shinobi_Reveal( ent );
	client->shinobi.kunaiNext = level.time + SHINOBI_KUNAI_COOLDOWN;

	if ( marked ) { // only so many marks: the oldest goes
		gentity_t *oldest = NULL;
		int        count = 0;
		for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
			gentity_t *e = &g_entities[i];
			if ( !Shinobi_IsMark( e, ent->s.number ) )
				continue;
			count++;
			if ( !oldest || KUNAI_BORN( e ) < KUNAI_BORN( oldest ) )
				oldest = e;
		}
		if ( count >= SHINOBI_MARK_MAX && oldest )
			G_FreeEntity( oldest );
	}

	AngleVectors( client->ps.viewangles, fwd, NULL, NULL );
	VectorCopy( client->ps.origin, eye );
	eye[2] += client->ps.viewheight;
	VectorMA( eye, 16, fwd, start );
	trap->Trace( &tr, eye, NULL, NULL, start, ent->s.number, MASK_SOLID, qfalse, 0, 0 );
	VectorCopy( tr.endpos, start );

	k = G_Spawn();
	if ( !k )
		return;
	k->classname = "shinobi_kunai";
	k->s.eType = ET_GENERAL;
	k->s.modelindex = kunaiModel;
	k->s.generic1 = marked ? SHINOBI_ENT_KUNAI_MARKED : SHINOBI_ENT_KUNAI;
	k->r.ownerNum = ent->s.number;
	k->parent = ent;
	k->r.contents = 0;
	VectorSet( k->r.mins, -2, -2, -2 );
	VectorSet( k->r.maxs, 2, 2, 2 );
	KUNAI_MARKED( k ) = marked;
	KUNAI_BORN( k ) = level.time;
	KUNAI_EXPIRE( k ) = level.time + 3000;
	k->s.pos.trType = TR_GRAVITY;
	k->s.pos.trTime = level.time;
	VectorCopy( start, k->s.pos.trBase );
	VectorScale( fwd, SHINOBI_KUNAI_SPEED, k->s.pos.trDelta );
	k->s.pos.trDelta[2] += 40;
	VectorCopy( start, k->r.currentOrigin );
	k->s.apos.trType = TR_STATIONARY;
	vectoangles( fwd, k->s.apos.trBase );
	k->think = Shinobi_KunaiThink;
	k->nextthink = level.time + FRAMETIME;
	trap->LinkEntity( (sharedEntity_t *)k );

	client->ps.forceHandExtend = HANDEXTEND_FORCEPUSH;
	client->ps.forceHandExtendTime = level.time + 250;
	G_Sound( ent, CHAN_WEAPON, sndThrow );
}

// newest mark of this player that is in place (stuck or on an enemy)
static gentity_t *Shinobi_NewestMark( gentity_t *ent ) {
	gentity_t *best = NULL;
	int        i;

	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *e = &g_entities[i];
		if ( !Shinobi_IsMark( e, ent->s.number ) || ( !KUNAI_STUCK( e ) && !KUNAI_ON( e ) ) )
			continue;
		if ( !best || KUNAI_BORN( e ) > KUNAI_BORN( best ) )
			best = e;
	}
	return best;
}

// is there room to stand at 'spot'?
static qboolean Shinobi_RoomAt( gentity_t *ent, vec3_t spot ) {
	trace_t tr;
	trap->Trace( &tr, spot, ent->r.mins, ent->r.maxs, spot, ent->s.number, MASK_PLAYERSOLID, qfalse, 0, 0 );
	return ( tr.startsolid || tr.allsolid ) ? qfalse : qtrue;
}

qboolean Shinobi_CastHiraishin( gentity_t *ent ) {
	gclient_t *client = ent->client;
	gentity_t *mark = Shinobi_NewestMark( ent );
	vec3_t     dest, look, angles;
	qboolean   found = qfalse;
	int        tries;

	if ( client->shinobi.hiraishinNext > level.time ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		trap->SendServerCommand( ent - g_entities, va( "cp \"Hiraishin ready in %.1f s\"",
			( client->shinobi.hiraishinNext - level.time ) / 1000.0f ) );
		return qfalse;
	}
	if ( !mark ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		trap->SendServerCommand( ent - g_entities, "cp \"No marked kunai (Shift+Z to throw one)\"" );
		return qfalse;
	}

	if ( KUNAI_ON( mark ) ) { // behind the marked enemy, facing him
		gentity_t *t = &g_entities[KUNAI_ON( mark ) - 1];
		static const float around[] = { 180, 135, -135, 90, -90, 0 };
		vec3_t fwd;
		for ( tries = 0; tries < (int)ARRAY_LEN( around ) && !found; tries++ ) {
			VectorSet( angles, 0, t->client->ps.viewangles[YAW] + around[tries], 0 );
			AngleVectors( angles, fwd, NULL, NULL );
			VectorMA( t->r.currentOrigin, 48, fwd, dest );
			found = Shinobi_RoomAt( ent, dest );
		}
		VectorSubtract( t->r.currentOrigin, dest, look );
	}
	else { // next to the kunai, out of the wall it is stuck in
		vec3_t base;
		VectorMA( mark->r.currentOrigin, 24, mark->movedir, base );
		for ( tries = 0; tries < 4 && !found; tries++ ) {
			VectorCopy( base, dest );
			dest[2] += tries * 18 + ( mark->movedir[2] > 0.6f ? 24 : 0 );
			found = Shinobi_RoomAt( ent, dest );
		}
		AngleVectors( client->ps.viewangles, look, NULL, NULL );
	}
	if ( !found ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		trap->SendServerCommand( ent - g_entities, "cp \"No room by the mark\"" );
		return qfalse;
	}

	client->shinobi.hiraishinNext = level.time + SHINOBI_HIRAISHIN_COOLDOWN;
	Shinobi_StopCharging( client );
	{
		vec3_t up = { 0, 0, 1 };
		G_PlayEffectID( fxFlash, client->ps.origin, up );
		G_SoundAtLoc( client->ps.origin, CHAN_AUTO, sndFlash );
		Shinobi_TeleportClient( ent, dest );
		G_PlayEffectID( fxFlash, client->ps.origin, up );
		G_Sound( ent, CHAN_AUTO, sndFlash );
	}
	vectoangles( look, angles );
	angles[PITCH] = 0;
	angles[ROLL] = 0;
	SetClientViewAngle( ent, angles );
	return qtrue;
}

qboolean Shinobi_CastRaiTrap( gentity_t *ent ) {
	int i, n = 0;

	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *k = &g_entities[i];
		vec3_t     pos, up = { 0, 0, 1 };
		int        j;

		if ( !Shinobi_IsMark( k, ent->s.number ) || ( !KUNAI_STUCK( k ) && !KUNAI_ON( k ) ) )
			continue;
		VectorCopy( k->r.currentOrigin, pos );

		for ( j = 0; j < level.num_entities; j++ ) {
			gentity_t *v = &g_entities[j];
			trace_t    tr;
			if ( !v->inuse || !v->client || v == ent || v->health <= 0 )
				continue;
			if ( Shinobi_DamageBlocked( v, ent ) || ( level.gametype >= GT_TEAM && OnSameTeam( ent, v ) ) )
				continue;
			if ( Distance( v->r.currentOrigin, pos ) > SHINOBI_RAITRAP_RADIUS )
				continue;
			trap->Trace( &tr, pos, NULL, NULL, v->r.currentOrigin, k->s.number, MASK_SOLID, qfalse, 0, 0 );
			if ( tr.fraction < 1.0f && tr.entityNum != v->s.number && !( KUNAI_ON( k ) && j == KUNAI_ON( k ) - 1 ) )
				continue;
			shinobiHitElement = SE_LIGHTNING;
			G_Damage( v, k, ent, NULL, v->r.currentOrigin, SHINOBI_RAITRAP_DAMAGE, DAMAGE_NO_KNOCKBACK, MOD_DEMP2 );
			shinobiHitElement = SE_NONE;
			if ( v->inuse && v->health > 0 && j < MAX_CLIENTS && !Shinobi_CCImmune( v ) ) {
				v->client->ps.electrifyTime = level.time + SHINOBI_RAITRAP_STUN_MS;
				if ( v->client->ps.weaponTime < SHINOBI_RAITRAP_STUN_MS )
					v->client->ps.weaponTime = SHINOBI_RAITRAP_STUN_MS;
				v->client->ps.pm_flags |= PMF_TIME_KNOCKBACK;
				v->client->ps.pm_time = SHINOBI_RAITRAP_STUN_MS / 2;
				Shinobi_CCGrant( v, level.time + SHINOBI_RAITRAP_STUN_MS );
			}
		}
		Shinobi_ElectrifyAt( pos, ent->s.number + 1 );
		Shinobi_EffectDir( shinobiMedia.fxChidoriHit[1], pos, up );
		G_PlayEffectID( shinobiMedia.fxEmp, pos, up );
		G_SoundAtLoc( pos, CHAN_AUTO, shinobiMedia.sndChidoriHit );
		G_FreeEntity( k );
		n++;
	}
	if ( !n ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		trap->SendServerCommand( ent - g_entities, "cp \"No marked kunai to discharge (Shift+Z)\"" );
		return qfalse;
	}
	return qtrue;
}

/*
===========================================================================
Shadow Trap (Yin/Yang): from each marked kunai a shadow reaches out along the
floor to the nearest enemy in range and holds him where he stands while its
maker pays for it (up to SHINOBI_SHADOWTRAP_MS). Any damage to him breaks it,
and Kawarimi gets him out. The trap entity is drawn like a Shadow Bind: from
its origin (where the kunai was) to s.angles2 (the shadow's tip).
===========================================================================
*/
#define STRAP_VICTIM(t)  ( (t)->genericValue1 )   // client num + 1
#define STRAP_CAUGHT(t)  ( (t)->genericValue2 )
#define STRAP_START(t)   ( (t)->genericValue3 )
#define STRAP_DRAIN(t)   ( (t)->genericValue4 )
#define STRAP_END(t)     ( (t)->genericValue5 )

static qboolean Shinobi_IsShadowTrap( gentity_t *t ) {
	return ( t && t->inuse && t->classname && !strcmp( t->classname, "shinobi_shadowtrap" ) ) ? qtrue : qfalse;
}

void Shinobi_ShadowTrapRelease( gentity_t *victim ) {
	gclient_t *client = victim->client;
	gentity_t *t;

	if ( !client || !client->shinobi.shadowTrapEnt )
		return;
	t = &g_entities[client->shinobi.shadowTrapEnt - 1];
	client->shinobi.shadowTrapEnt = 0;
	client->ps.stats[STAT_SHINOBI2] &= ~SHINOBI_STAT2_SHADOWTRAP;
	Shinobi_CCGrant( victim, level.time );
	if ( Shinobi_IsShadowTrap( t ) && STRAP_VICTIM( t ) == victim->s.number + 1 )
		G_FreeEntity( t );
}

// may the trap of 'ent' take v?
static qboolean Shinobi_ShadowTrapTarget( gentity_t *ent, gentity_t *v ) {
	if ( !v->inuse || !v->client || v == ent || v->s.number >= MAX_CLIENTS || v->health <= 0
		|| v->client->sess.sessionTeam == TEAM_SPECTATOR )
		return qfalse;
	if ( level.gametype >= GT_TEAM && OnSameTeam( ent, v ) )
		return qfalse;
	if ( Shinobi_DamageBlocked( v, ent ) || SHINOBI_UNSEEN( &v->client->ps ) || Shinobi_Burrowed( v ) )
		return qfalse;
	if ( v->client->shinobi.shadowTrapEnt || v->client->shinobi.shadowBy || v->client->shinobi.prisonCaster )
		return qfalse;
	return qtrue;
}

static void Shinobi_ShadowTrapThink( gentity_t *t ) {
	gentity_t *owner = &g_entities[t->r.ownerNum];
	gentity_t *v = STRAP_VICTIM( t ) ? &g_entities[STRAP_VICTIM( t ) - 1] : NULL;
	vec3_t     feet, tip;

	if ( !v || !v->inuse || !v->client || v->health <= 0 || v->client->sess.sessionTeam == TEAM_SPECTATOR
		|| !owner->inuse || !owner->client || owner->health <= 0 ) {
		if ( v && v->client && v->client->shinobi.shadowTrapEnt == t->s.number + 1 )
			Shinobi_ShadowTrapRelease( v );
		else
			G_FreeEntity( t );
		return;
	}
	VectorCopy( v->client->ps.origin, feet );
	feet[2] += v->r.mins[2];
	VectorCopy( t->s.angles2, tip );

	if ( !STRAP_CAUGHT( t ) ) {
		// reaching out along the floor
		vec3_t  d, next, from, to, floor;
		float   dist, step = SHINOBI_SHADOWTRAP_SPEED * ( FRAMETIME / 1000.0f );
		trace_t tr;

		if ( level.time - STRAP_START( t ) > SHINOBI_SHADOWTRAP_REACH_MS
			|| Distance( feet, t->r.currentOrigin ) > SHINOBI_SHADOWTRAP_RANGE * 1.5f || !Shinobi_ShadowTrapTarget( owner, v ) ) {
			G_FreeEntity( t ); // got away: the shadow pulls back
			return;
		}
		VectorSubtract( feet, tip, d );
		d[2] = 0;
		dist = VectorNormalize( d );
		if ( dist > 0.5f ) {
			VectorMA( tip, dist < step ? dist : step, d, next );
			VectorCopy( tip, from );
			from[2] += 16;
			VectorCopy( next, to );
			to[2] = feet[2] > tip[2] ? feet[2] + 16 : tip[2] + 16;
			trap->Trace( &tr, from, NULL, NULL, to, ENTITYNUM_NONE, MASK_SOLID, qfalse, 0, 0 );
			if ( tr.fraction >= 1.0f ) {
				if ( Shinobi_FloorBelow( to, 64, floor ) )
					VectorCopy( floor, tip );
				else
					VectorCopy( next, tip ); // over a gap: on the same level
			}
		}
		VectorSubtract( feet, tip, d );
		d[2] = 0;
		if ( VectorLength( d ) <= 24 && feet[2] - tip[2] < 40 && !Shinobi_CCImmune( v ) ) {
			gclient_t *vc = v->client;
			STRAP_CAUGHT( t ) = 1;
			STRAP_END( t ) = level.time + SHINOBI_SHADOWTRAP_MS;
			STRAP_DRAIN( t ) = level.time + SHINOBI_SHADOWTRAP_DRAIN_MS;
			vc->shinobi.shadowTrapEnt = t->s.number + 1;
			vc->shinobi.chargeJutsu = SJ_NONE;
			vc->ps.userInt1 = vc->ps.userInt2 = vc->ps.userInt3 = 0; // drops a Rasengan / Chidori
			if ( vc->shinobi.flameEnt )
				Shinobi_FlameStop( v );
			vc->ps.velocity[0] = vc->ps.velocity[1] = 0;
			G_Sound( v, CHAN_AUTO, shinobiMedia.sndShadow );
			Shinobi_HitText( v, STX_TRAPPED, 0 );
			trap->SendServerCommand( v - g_entities, "cp \"^0Shadow Trap!^7\nheld in place - any hit frees you, Kawarimi gets you out\"" );
			trap->SendServerCommand( owner - g_entities, "cp \"Shadow Trap: caught!\"" );
		}
	}
	else {
		if ( v->client->shinobi.shadowTrapEnt != t->s.number + 1 ) {
			G_FreeEntity( t ); // let go elsewhere
			return;
		}
		if ( level.time >= STRAP_END( t ) ) {
			Shinobi_ShadowTrapRelease( v );
			return;
		}
		owner->client->ps.fd.forcePowerRegenDebounceTime = level.time + 500;
		if ( STRAP_DRAIN( t ) <= level.time ) {
			STRAP_DRAIN( t ) = level.time + SHINOBI_SHADOWTRAP_DRAIN_MS;
			if ( owner->client->ps.fd.forcePower < SHINOBI_SHADOWTRAP_DRAIN ) {
				trap->SendServerCommand( owner - g_entities, "cp \"Out of chakra: the shadow trap lets go\"" );
				Shinobi_ShadowTrapRelease( v );
				return;
			}
			owner->client->ps.fd.forcePower -= SHINOBI_SHADOWTRAP_DRAIN;
		}
		VectorCopy( feet, tip );
	}
	VectorCopy( tip, t->s.angles2 );
	trap->LinkEntity( (sharedEntity_t *)t );
	t->nextthink = level.time + FRAMETIME;
}

// the caught one, every client frame before Pmove: stands where he is
void Shinobi_ShadowTrapVictimThink( gentity_t *ent, usercmd_t *ucmd ) {
	gclient_t *client = ent->client;
	gentity_t *t = &g_entities[client->shinobi.shadowTrapEnt - 1];

	if ( !Shinobi_IsShadowTrap( t ) || STRAP_VICTIM( t ) != ent->s.number + 1 || !STRAP_CAUGHT( t ) ) {
		client->shinobi.shadowTrapEnt = 0;
		client->ps.stats[STAT_SHINOBI2] &= ~SHINOBI_STAT2_SHADOWTRAP;
		return;
	}
	ucmd->forwardmove = ucmd->rightmove = 0;
	if ( ucmd->upmove > 0 )
		ucmd->upmove = 0;
	ucmd->generic_cmd = 0;
	ucmd->buttons &= ~( BUTTON_ATTACK | BUTTON_ALT_ATTACK | BUTTON_USE | BUTTON_FORCEPOWER | BUTTON_FORCEGRIP
		| BUTTON_FORCE_LIGHTNING | BUTTON_FORCE_DRAIN | BUTTON_USE_HOLDABLE | SHINOBI_BUTTON_BLOCK );
	client->ps.velocity[0] = client->ps.velocity[1] = 0;
	client->ps.stats[STAT_SHINOBI2] |= SHINOBI_STAT2_SHADOWTRAP;
}

qboolean Shinobi_CastShadowTrap( gentity_t *ent ) {
	int i, marks = 0, n = 0;

	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *k = &g_entities[i];
		gentity_t *best = NULL, *t;
		vec3_t     pos, start;
		float      bestDist = SHINOBI_SHADOWTRAP_RANGE;
		int        j;

		if ( !Shinobi_IsMark( k, ent->s.number ) || ( !KUNAI_STUCK( k ) && !KUNAI_ON( k ) ) )
			continue;
		marks++;
		VectorCopy( k->r.currentOrigin, pos );
		if ( KUNAI_ON( k ) ) { // riding on the enemy it marked: him
			gentity_t *v = &g_entities[KUNAI_ON( k ) - 1];
			if ( Shinobi_ShadowTrapTarget( ent, v ) )
				best = v;
		}
		else {
			for ( j = 0; j < MAX_CLIENTS; j++ ) {
				gentity_t *v = &g_entities[j];
				trace_t    tr;
				float      d;
				int        m;
				qboolean   taken = qfalse;

				if ( !Shinobi_ShadowTrapTarget( ent, v ) )
					continue;
				d = Distance( v->r.currentOrigin, pos );
				if ( d > bestDist )
					continue;
				for ( m = MAX_CLIENTS; m < level.num_entities && !taken; m++ ) // another shadow is already on its way to him
					taken = Shinobi_IsShadowTrap( &g_entities[m] ) && STRAP_VICTIM( &g_entities[m] ) == v->s.number + 1;
				if ( taken )
					continue;
				trap->Trace( &tr, pos, NULL, NULL, v->r.currentOrigin, k->s.number, MASK_SOLID, qfalse, 0, 0 );
				if ( tr.fraction < 1.0f && tr.entityNum != v->s.number )
					continue;
				best = v;
				bestDist = d;
			}
		}
		if ( !best )
			continue;

		if ( KUNAI_ON( k ) ) {
			VectorCopy( best->client->ps.origin, start );
			start[2] += best->r.mins[2];
		}
		else if ( !Shinobi_FloorBelow( pos, 256, start ) )
			VectorCopy( pos, start );
		t = G_Spawn();
		if ( !t )
			break;
		t->classname = "shinobi_shadowtrap";
		t->s.eType = ET_GENERAL;
		t->s.generic1 = SHINOBI_ENT_SHADOW;
		t->r.ownerNum = ent->s.number;
		t->r.contents = 0;
		t->r.svFlags |= SVF_BROADCAST;
		G_SetOrigin( t, start );
		VectorCopy( start, t->s.angles2 );
		STRAP_VICTIM( t ) = best->s.number + 1;
		STRAP_START( t ) = level.time;
		t->think = Shinobi_ShadowTrapThink;
		t->nextthink = level.time + FRAMETIME;
		trap->LinkEntity( (sharedEntity_t *)t );
		G_SoundAtLoc( start, CHAN_AUTO, shinobiMedia.sndShadow );
		G_FreeEntity( k ); // the kunai goes into the shadow
		n++;
	}
	if ( !marks ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		trap->SendServerCommand( ent - g_entities, "cp \"No marked kunai to trap with (Shift+Z)\"" );
		return qfalse;
	}
	if ( !n ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		trap->SendServerCommand( ent - g_entities, "cp \"Shadow Trap: no enemy near your marked kunai\"" );
		return qfalse;
	}
	ent->client->ps.forceHandExtend = HANDEXTEND_TAUNT;
	ent->client->ps.forceDodgeAnim = BOTH_FORCEHEAL_QUICK;
	ent->client->ps.forceHandExtendTime = level.time + 400;
	return qtrue;
}

/*
===========================================================================
Wind Edge (Futon) / Lightning Edge (Raiton): chakra on the fists and blade
for a while. Wind: every swing throws an air slash a few metres ahead, and
the hits cut through guards (a perfect parry still stops them). Lightning:
hits 20% harder, 3x against earth walls and other stone.
===========================================================================
*/
extern qboolean BG_SaberInAttack( int move );

qboolean Shinobi_WindEdgeActive( gentity_t *ent ) {
	return ent && ent->client && ent->s.number < MAX_CLIENTS && ent->client->shinobi.infuseType == SS_FUTON
		&& ent->client->shinobi.infuseUntil > level.time;
}

qboolean Shinobi_CastEdge( gentity_t *ent, int school ) {
	gclient_t *client = ent->client;

	client->shinobi.infuseType = school;
	client->shinobi.infuseUntil = level.time + SHINOBI_EDGE_MS;
	client->shinobi.edgeFx = level.time;
	G_Sound( ent, CHAN_AUTO, school == SS_FUTON ? shinobiMedia.sndWind : shinobiMedia.sndChidoriForm );
	trap->SendServerCommand( ent - g_entities, va( "cp \"%s (%i s)\"",
		school == SS_FUTON ? "^5Wind Edge" : "^4Lightning Edge", SHINOBI_EDGE_MS / 1000 ) );
	return qtrue;
}

// a swing with the Wind Edge: an air slash a few metres past the fist / blade
static void Shinobi_WindSlash( gentity_t *ent ) {
	gclient_t *client = ent->client;
	const qboolean saber = client->ps.weapon == WP_SABER;
	vec3_t     fwd, eye, start, end, fxOrg, mins = { -12, -12, -12 }, maxs = { 12, 12, 12 };
	trace_t    tr;

	AngleVectors( client->ps.viewangles, fwd, NULL, NULL );
	VectorCopy( client->ps.origin, eye );
	eye[2] += client->ps.viewheight - 8;
	VectorMA( eye, 36, fwd, start ); // past the reach of the hit itself
	VectorMA( eye, SHINOBI_WINDSLASH_REACH, fwd, end );
	trap->Trace( &tr, start, mins, maxs, end, ent->s.number, MASK_SHOT, qfalse, 0, 0 );
	if ( tr.entityNum < ENTITYNUM_WORLD ) {
		gentity_t *other = &g_entities[tr.entityNum];
		if ( other->takedamage && other != ent ) {
			G_Damage( other, ent, ent, fwd, tr.endpos,
				saber ? SHINOBI_WINDSLASH_DAMAGE_SABER : SHINOBI_WINDSLASH_DAMAGE_FIST, DAMAGE_NO_KNOCKBACK,
				saber ? MOD_SABER : MOD_MELEE );
		}
	}
	VectorMA( eye, 60, fwd, fxOrg );
	Shinobi_EffectDir( shinobiMedia.fxWindGust[0], fxOrg, fwd );
	G_Sound( ent, CHAN_AUTO, shinobiMedia.sndWind );
}

void Shinobi_InfuseThink( gentity_t *ent, usercmd_t *ucmd ) {
	gclient_t *client = ent->client;
	const qboolean on = client->shinobi.infuseUntil > level.time && client->shinobi.infuseType;

	client->ps.stats[STAT_SHINOBI] &= ~( SHINOBI_STAT_WINDEDGE | SHINOBI_STAT_RAIEDGE );
	if ( !on ) {
		client->shinobi.infuseType = 0;
		return;
	}
	client->ps.stats[STAT_SHINOBI] |= client->shinobi.infuseType == SS_FUTON ? SHINOBI_STAT_WINDEDGE : SHINOBI_STAT_RAIEDGE;

	// chakra licking the hands / blade, for everyone to see
	if ( client->shinobi.edgeFx <= level.time ) {
		vec3_t yaw, fwd, right, org, up = { 0, 0, 1 };
		client->shinobi.edgeFx = level.time + SHINOBI_EDGE_FX_MS;
		VectorSet( yaw, 0, client->ps.viewangles[YAW], 0 );
		AngleVectors( yaw, fwd, right, NULL );
		VectorMA( client->ps.origin, 14, fwd, org );
		VectorMA( org, 8, right, org );
		org[2] += 6;
		G_PlayEffectID( client->shinobi.infuseType == SS_FUTON ? shinobiMedia.fxWindEdge : shinobiMedia.fxRaiEdge, org, up );
	}

	if ( client->shinobi.infuseType == SS_FUTON && !client->shinobi.blocking
		&& ( client->ps.weapon == WP_MELEE || ( client->ps.weapon == WP_SABER && !client->ps.saberHolstered ) ) ) {
		const qboolean pressed = ( ucmd->buttons & ( BUTTON_ATTACK | BUTTON_ALT_ATTACK ) )
			&& !( client->buttons & ( BUTTON_ATTACK | BUTTON_ALT_ATTACK ) );
		const qboolean newSwing = client->ps.weapon == WP_SABER && BG_SaberInAttack( client->ps.saberMove )
			&& client->ps.saberMove != client->shinobi.lastSaberMove;
		if ( ( pressed || newSwing ) && client->shinobi.slashNext <= level.time ) {
			client->shinobi.slashNext = level.time + SHINOBI_WINDSLASH_MS;
			Shinobi_WindSlash( ent );
		}
	}
	client->shinobi.lastSaberMove = client->ps.saberMove;
}

// called from Shinobi_BuildDamage for a fist, kick or blade hit
int Shinobi_InfuseHit( gentity_t *targ, gentity_t *attacker, int damage ) {
	gclient_t *ac = attacker->client;

	if ( ac->shinobi.infuseUntil <= level.time || ac->shinobi.infuseType != SS_RAITON || !targ || targ == attacker )
		return damage;
	damage = damage * SHINOBI_RAIEDGE_BONUS / 100;
	if ( ( targ->classname && !strcmp( targ->classname, "shinobi_earthwall" ) ) || targ->shinobiCloneEarth )
		damage *= SHINOBI_RAIEDGE_STONE; // lightning cuts through stone
	return damage;
}

// thrown back by a Wind Wall: it becomes newOwner's plain kunai (a mark doesn't survive the wind)
void Shinobi_KunaiReflect( gentity_t *k, gentity_t *newOwner ) {
	KUNAI_MARKED( k ) = 0;
	KUNAI_ON( k ) = 0;
	KUNAI_EXPIRE( k ) = level.time + 3000;
	k->s.generic1 = SHINOBI_ENT_KUNAI;
	k->r.ownerNum = newOwner->s.number;
	k->parent = newOwner;
}
