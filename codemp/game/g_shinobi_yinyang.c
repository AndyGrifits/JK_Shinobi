/*
===========================================================================
JK_Shinobi - Yin / Yang: Shadow Hide, Flash, Healing; and Raiton's Lightning
Dash (the Lightning Clone lives with the clones).
===========================================================================
*/
#include "g_shinobi_local.h"

qboolean Shinobi_ShadowHidden( gentity_t *ent ) {
	return ent && ent->client && ent->s.number < MAX_CLIENTS && ent->client->shinobi.shadowHideEnd ? qtrue : qfalse;
}

/*
---------------------------------------------------------------------------
Shadow Hide (held): you melt into your own shadow. Others see only a dark
patch sliding over the ground (the Byakugan sees you). Blades, fists and
missiles pass over it; fire, lightning and a Flash drive you out.
---------------------------------------------------------------------------
*/
static void Shinobi_ShadowPatchThink( gentity_t *patch ) {
	gentity_t *owner = &g_entities[patch->r.ownerNum];
	vec3_t     feet;

	if ( !owner->inuse || !owner->client || !owner->client->shinobi.shadowHideEnd
		|| owner->client->shinobi.shadowPatch != patch->s.number + 1 ) {
		G_FreeEntity( patch );
		return;
	}
	VectorCopy( owner->client->ps.origin, feet );
	feet[2] += owner->r.mins[2] + 1;
	G_SetOrigin( patch, feet );
	trap->LinkEntity( (sharedEntity_t *)patch );
	patch->nextthink = level.time + FRAMETIME;
}

qboolean Shinobi_CastShadowHide( gentity_t *ent ) {
	gclient_t *client = ent->client;
	gentity_t *patch;

	if ( client->shinobi.shadowHideEnd || client->shinobi.burrowEnd || SHINOBI_HAND_ANY( &client->ps )
		|| SHINOBI_HIDDEN( &client->ps ) )
		return qfalse;
	if ( client->shinobi.shadowHideNext > level.time ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		trap->SendServerCommand( ent - g_entities, va( "cp \"Shadow Hide ready in %.1f s\"",
			( client->shinobi.shadowHideNext - level.time ) / 1000.0f ) );
		return qfalse;
	}
	if ( client->ps.groundEntityNum == ENTITYNUM_NONE ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		trap->SendServerCommand( ent - g_entities, "cp \"Shadow Hide: stand on the ground\"" );
		return qfalse;
	}
	patch = G_Spawn();
	if ( !patch )
		return qfalse;
	patch->classname = "shinobi_shadowpatch";
	patch->s.eType = ET_GENERAL;
	patch->s.generic1 = SHINOBI_ENT_SHADOWPATCH;
	patch->r.ownerNum = ent->s.number;
	patch->r.contents = 0;
	patch->think = Shinobi_ShadowPatchThink;
	patch->nextthink = level.time + FRAMETIME;
	G_SetOrigin( patch, client->ps.origin );
	trap->LinkEntity( (sharedEntity_t *)patch );

	if ( client->ps.weapon == WP_SABER )
		client->ps.saberHolstered = 2;
	client->shinobi.shadowPatch = patch->s.number + 1;
	client->shinobi.shadowHideEnd = level.time + SHINOBI_SHADOWHIDE_MAX_MS;
	client->shinobi.shadowHideTick = level.time + SHINOBI_SHADOWHIDE_TICK_MS;
	client->ps.stats[STAT_SHINOBI] |= SHINOBI_STAT_SHADOWHIDE;
	ent->r.contents = 0;
	ent->r.svFlags |= SVF_BROADCASTCLIENTS; // only to us and Byakugan eyes
	ent->r.broadcastClients[0] = ent->r.broadcastClients[1] = 0;
	trap->LinkEntity( (sharedEntity_t *)ent );
	G_Sound( ent, CHAN_AUTO, shinobiMedia.sndShadow );
	return qtrue;
}

void Shinobi_ShadowHideEnd( gentity_t *ent ) {
	gclient_t *client = ent->client;

	if ( !client || !client->shinobi.shadowHideEnd )
		return;
	client->shinobi.shadowHideEnd = 0;
	client->shinobi.shadowHideNext = level.time + SHINOBI_SHADOWHIDE_COOLDOWN;
	client->ps.stats[STAT_SHINOBI] &= ~SHINOBI_STAT_SHADOWHIDE;
	if ( client->shinobi.shadowPatch ) {
		gentity_t *patch = &g_entities[client->shinobi.shadowPatch - 1];
		if ( patch->inuse && patch->classname && !strcmp( patch->classname, "shinobi_shadowpatch" ) )
			G_FreeEntity( patch );
		client->shinobi.shadowPatch = 0;
	}
	if ( client->shinobi.chargeJutsu == SJ_SHADOWHIDE )
		client->shinobi.chargeJutsu = SJ_NONE;
	ent->r.svFlags &= ~SVF_BROADCASTCLIENTS;
	Shinobi_RestoreSolid( ent );
	trap->LinkEntity( (sharedEntity_t *)ent );
	G_Sound( ent, CHAN_AUTO, shinobiMedia.sndShadow );
}

// fire, lightning (and the blind) reach into the shadow; the rest passes over
qboolean Shinobi_ShadowHideDamage( gentity_t *targ ) {
	if ( !Shinobi_ShadowHidden( targ ) )
		return qtrue;
	if ( shinobiHitElement == SE_FIRE || shinobiHitElement == SE_LIGHTNING ) {
		Shinobi_ShadowHideEnd( targ );
		return qtrue;
	}
	return qfalse;
}

/*
---------------------------------------------------------------------------
Flash: a burst of light. Everyone in range who is looking your way (and can
see you) is blinded; the hidden around you (Kawarimi, Shadow Hide) are
flushed out.
---------------------------------------------------------------------------
*/
qboolean Shinobi_CastFlash( gentity_t *ent ) {
	gclient_t *client = ent->client;
	vec3_t     eye, up = { 0, 0, 1 };
	int        i;

	if ( client->shinobi.flashNext > level.time ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		trap->SendServerCommand( ent - g_entities, va( "cp \"Flash ready in %.1f s\"",
			( client->shinobi.flashNext - level.time ) / 1000.0f ) );
		return qfalse;
	}
	client->shinobi.flashNext = level.time + SHINOBI_FLASH_COOLDOWN;
	VectorCopy( client->ps.origin, eye );
	eye[2] += client->ps.viewheight;
	G_PlayEffectID( shinobiMedia.fxFlash, eye, up );
	G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFlash );

	for ( i = 0; i < level.num_entities; i++ ) {
		gentity_t *v = &g_entities[i];
		vec3_t     veye, d, fwd;
		trace_t    tr;
		float      dist;

		if ( v == ent || !v->inuse || !v->client || v->health <= 0 )
			continue;
		if ( v->s.number < MAX_CLIENTS && v->client->sess.sessionTeam == TEAM_SPECTATOR )
			continue;
		if ( level.gametype >= GT_TEAM && OnSameTeam( ent, v ) )
			continue;
		VectorCopy( v->client->ps.origin, veye );
		veye[2] += v->client->ps.viewheight;
		VectorSubtract( eye, veye, d );
		dist = VectorNormalize( d );
		if ( dist > SHINOBI_FLASH_RADIUS )
			continue;
		trap->Trace( &tr, eye, NULL, NULL, veye, ent->s.number, MASK_SOLID, qfalse, 0, 0 );
		if ( tr.fraction < 1.0f )
			continue;
		if ( v->s.number < MAX_CLIENTS ) { // the light chases out what hides
			Shinobi_Reveal( v );
			Shinobi_ShadowHideEnd( v );
		}
		AngleVectors( v->client->ps.viewangles, fwd, NULL, NULL );
		if ( DotProduct( fwd, d ) < SHINOBI_FLASH_FACING )
			continue; // looking away
		if ( v->s.number < MAX_CLIENTS && !( v->r.svFlags & SVF_BOT ) ) {
			v->client->shinobi.blindEnd = level.time + SHINOBI_FLASH_BLIND_MS;
			trap->SendServerCommand( v - g_entities, "cp \"^7Blinded!\"" );
		}
		else if ( !Shinobi_CCImmune( v ) ) { // bots and NPCs just reel
			Shinobi_Stagger( v, 900 );
			if ( v->s.number >= MAX_CLIENTS && v->client->ps.weaponTime < 900 )
				v->client->ps.weaponTime = 900;
		}
	}
	return qtrue;
}

// Healing: chakra into health
qboolean Shinobi_CastHeal( gentity_t *ent, int lvl ) {
	gclient_t *client = ent->client;
	vec3_t     up = { 0, 0, 1 };

	if ( client->shinobi.healing )
		return qfalse;
	if ( ent->health >= client->ps.stats[STAT_MAX_HEALTH] ) {
		trap->SendServerCommand( ent - g_entities, "cp \"Not hurt\"" );
		return qfalse;
	}
	client->shinobi.healing = 1;
	client->shinobi.healTick = level.time + SHINOBI_HEAL_TICK_MS;
	G_PlayEffectID( shinobiMedia.fxGates, client->ps.origin, up );
	G_Sound( ent, CHAN_AUTO, shinobiMedia.sndHeal );
	return qtrue;
}

void Shinobi_HealStop( gentity_t *ent ) {
	gclient_t *client = ent->client;

	if ( !client->shinobi.healing )
		return;
	client->shinobi.healing = 0;
	client->ps.stats[STAT_SHINOBI] &= ~SHINOBI_STAT_CHANNEL;
	if ( client->ps.forceHandExtend == HANDEXTEND_TAUNT && client->ps.forceDodgeAnim == BOTH_FORCEHEAL_QUICK )
		client->ps.forceHandExtend = HANDEXTEND_NONE;
}

// held Healing, every client frame: chakra flows into health
static void Shinobi_HealThink( gentity_t *ent, usercmd_t *ucmd ) {
	gclient_t *client = ent->client;
	vec3_t     up = { 0, 0, 1 };

	if ( !client->shinobi.healing )
		return;
	if ( ent->health <= 0 || client->sess.sessionTeam == TEAM_SPECTATOR || !Shinobi_CanAct( ent )
		|| client->shinobi.blocking || ( ucmd->buttons & ( BUTTON_ATTACK | BUTTON_ALT_ATTACK ) ) ) {
		Shinobi_HealStop( ent );
		return;
	}
	client->ps.fd.forcePowerRegenDebounceTime = level.time + 500;
	while ( client->shinobi.healTick <= level.time ) {
		client->shinobi.healTick += SHINOBI_HEAL_TICK_MS;
		if ( ent->health >= client->ps.stats[STAT_MAX_HEALTH] ) {
			trap->SendServerCommand( ent - g_entities, "cp \"Fully healed\"" );
			Shinobi_HealStop( ent );
			return;
		}
		if ( client->ps.fd.forcePower < SHINOBI_HEAL_PER_TICK ) {
			trap->SendServerCommand( ent - g_entities, "cp \"Out of chakra\"" );
			Shinobi_HealStop( ent );
			return;
		}
		client->ps.fd.forcePower -= SHINOBI_HEAL_PER_TICK;
		ent->health += SHINOBI_HEAL_PER_TICK;
		if ( ent->health > client->ps.stats[STAT_MAX_HEALTH] )
			ent->health = client->ps.stats[STAT_MAX_HEALTH];
		client->ps.stats[STAT_HEALTH] = ent->health;
		if ( ( client->shinobi.healTick / SHINOBI_HEAL_TICK_MS ) % 8 == 0 )
			G_PlayEffectID( shinobiMedia.fxGates, client->ps.origin, up );
	}
	client->ps.stats[STAT_SHINOBI] |= SHINOBI_STAT_CHANNEL; // walking slowly
	client->ps.forceHandExtend = HANDEXTEND_TAUNT;
	client->ps.forceDodgeAnim = BOTH_FORCEHEAL_QUICK;
	client->ps.forceHandExtendTime = level.time + 150;
}

/*
---------------------------------------------------------------------------
Kaiten (Yin / Yang, held): half a second winding up on the spot, then you spin
inside a dome of chakra for as long as the key is held and the chakra lasts
(SHINOBI_KAITEN_DRAIN a tick). Nothing that deals damage gets through, no
knockdown or bind takes (Shinobi_CCImmune), missiles glance off the dome, and
whoever comes close is thrown away and knocked down. Rooted, no attacks.
---------------------------------------------------------------------------
*/
extern void G_Knockdown( gentity_t *victim );
extern stringID_table_t animTable[MAX_ANIMATIONS + 1];

// the poses come from cvars (shinobi_kaitenAnim1 / 2: animation names) - to try others without a rebuild
static int Shinobi_KaitenAnim( int which ) {
	static int  anim[2] = { -1, -1 }, mod[2] = { -1, -1 };
	vmCvar_t   *cv = which ? &shinobi_kaitenAnim2 : &shinobi_kaitenAnim1;
	if ( mod[which] != cv->modificationCount ) {
		mod[which] = cv->modificationCount;
		anim[which] = GetIDForString( animTable, cv->string );
		if ( anim[which] < 0 || anim[which] >= MAX_ANIMATIONS ) {
			Com_Printf( "^3%s: no animation \"%s\" - using BOTH_FORCE_RAGE\n", which ? "shinobi_kaitenAnim2" : "shinobi_kaitenAnim1", cv->string );
			anim[which] = BOTH_FORCE_RAGE;
		}
	}
	return anim[which];
}

qboolean Shinobi_KaitenSpinning( gentity_t *ent ) {
	return ( ent && ent->client && ent->s.number < MAX_CLIENTS && ent->client->shinobi.kaitenStart
		&& level.time >= ent->client->shinobi.kaitenStart + SHINOBI_KAITEN_WINDUP_MS ) ? qtrue : qfalse;
}

// from G_Damage: the dome takes the blow (falls, lava, drowning still hurt)
qboolean Shinobi_KaitenBlocks( gentity_t *targ, gentity_t *attacker, int mod ) {
	vec3_t up = { 0, 0, 1 };

	if ( !Shinobi_KaitenSpinning( targ ) || attacker == targ )
		return qfalse;
	switch ( mod ) {
	case MOD_LAVA: case MOD_WATER: case MOD_SLIME: case MOD_FALLING: case MOD_TRIGGER_HURT:
	case MOD_SUICIDE: case MOD_UNKNOWN: case MOD_TELEFRAG: case MOD_CRUSH:
		return qfalse;
	default:
		break;
	}
	if ( targ->client->shinobi.kaitenFx <= level.time ) { // a flare where it struck (not every tick of a flame)
		targ->client->shinobi.kaitenFx = level.time + 200;
		G_PlayEffectID( shinobiMedia.fxGates, targ->client->ps.origin, up );
	}
	Shinobi_HitText( targ, STX_BLOCK, 0 );
	shinobiTest.kaitenBlocks++;
	return qtrue;
}

qboolean Shinobi_CastKaiten( gentity_t *ent ) {
	gclient_t *client = ent->client;

	if ( ent->s.number >= MAX_CLIENTS || client->shinobi.kaitenStart || SHINOBI_HAND_ANY( &client->ps ) )
		return qfalse; // (not for clones)
	if ( client->ps.stats[STAT_SHINOBI2] & SHINOBI_STAT2_FLINCH )
		return qfalse;
	if ( client->shinobi.healing )
		Shinobi_HealStop( ent );
	client->shinobi.blocking = qfalse;
	client->shinobi.kaitenStart = level.time;
	client->shinobi.kaitenTick = level.time + SHINOBI_KAITEN_WINDUP_MS;
	client->shinobi.kaitenFx = 0;
	client->ps.stats[STAT_SHINOBI2] |= SHINOBI_STAT2_KAITEN;
	G_Sound( ent, CHAN_AUTO, shinobiMedia.sndJutsuCharge );
	return qtrue;
}

void Shinobi_KaitenStop( gentity_t *ent ) {
	gclient_t     *client = ent->client;
	qboolean       spun;

	if ( !client || !client->shinobi.kaitenStart )
		return;
	spun = Shinobi_KaitenSpinning( ent );
	client->shinobi.kaitenStart = 0;
	client->ps.stats[STAT_SHINOBI2] &= ~SHINOBI_STAT2_KAITEN;
	client->ps.eFlags &= ~SHINOBI_EF_KAITEN;
	if ( client->shinobi.chargeJutsu == SJ_KAITEN )
		client->shinobi.chargeJutsu = SJ_NONE;
	if ( spun && ent->health > 0 && client->ps.forceHandExtend == HANDEXTEND_TAUNT ) {
		// out of the spin with the second pose (the same as the first: its frozen pose plays on)
		const int anim = Shinobi_KaitenAnim( 1 );
		int       len = BG_AnimLength( ent->localAnimIndex, anim );
		if ( len > 900 || len <= 0 )
			len = 900;
		client->ps.forceDodgeAnim = anim;
		client->ps.forceHandExtendTime = level.time + len;
	}
	else if ( client->ps.forceHandExtend == HANDEXTEND_TAUNT )
		client->ps.forceHandExtend = HANDEXTEND_NONE;
}

void Shinobi_KaitenInterrupt( gentity_t *ent ) {
	if ( ent && ent->client && ent->client->shinobi.kaitenStart && !Shinobi_KaitenSpinning( ent ) ) {
		Shinobi_KaitenStop( ent );
		Shinobi_HitText( ent, STX_INTERRUPT, 0 );
	}
}

// a missile about to reach the dome glances off it (and now belongs to the spinner)
static void Shinobi_KaitenDeflect( gentity_t *ent ) {
	vec3_t centre;
	int    i;

	VectorCopy( ent->client->ps.origin, centre );
	centre[2] += 8;
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *p = &g_entities[i];
		vec3_t     now, vel, next, d, n, aim, closest;
		float      t, len, speed, dn;

		if ( !p->inuse || !p->classname || p->r.ownerNum == ent->s.number || p->s.pos.trType == TR_STATIONARY )
			continue;
		if ( strcmp( p->classname, "shinobi_fireball" ) && strcmp( p->classname, "shinobi_waterball" )
			&& strcmp( p->classname, "shinobi_kunai" ) && strcmp( p->classname, "shinobi_exkunai" )
			&& strcmp( p->classname, "shinobi_shuriken" ) )
			continue;
		BG_EvaluateTrajectory( &p->s.pos, level.time, now );
		BG_EvaluateTrajectoryDelta( &p->s.pos, level.time, vel );
		if ( DistanceSquared( now, centre ) > 600.0f * 600.0f )
			continue;
		// where it will be by the next server frame or two: does that stretch come near the dome?
		VectorMA( now, 0.1f, vel, next );
		VectorSubtract( next, now, d );
		len = VectorNormalize( d );
		VectorSubtract( centre, now, n );
		t = DotProduct( n, d );
		if ( t < 0 ) t = 0;
		if ( t > len ) t = len;
		VectorMA( now, t, d, closest );
		if ( Distance( closest, centre ) > SHINOBI_KAITEN_DEFLECT )
			continue;
		// mirror it off the sphere where it touches
		VectorSubtract( closest, centre, n );
		if ( VectorNormalize( n ) < 1.0f )
			VectorScale( d, -1.0f, n );
		speed = VectorLength( vel );
		if ( speed < 300 )
			speed = 300;
		dn = DotProduct( vel, n );
		if ( dn >= 0 )
			continue; // flying away already
		VectorMA( vel, -2.0f * dn, n, aim );
		VectorNormalize( aim );
		p->s.pos.trTime = level.time;
		VectorCopy( closest, p->s.pos.trBase );
		VectorScale( aim, speed, p->s.pos.trDelta );
		VectorCopy( closest, p->r.currentOrigin );
		p->r.ownerNum = ent->s.number;
		p->s.owner = ent->s.number;
		p->parent = ent;
		if ( !strcmp( p->classname, "shinobi_kunai" ) )
			Shinobi_KunaiReflect( p, ent );
		trap->LinkEntity( (sharedEntity_t *)p );
		shinobiTest.kaitenDeflects++;
		Shinobi_EffectDir( shinobiMedia.fxWindGust[0], closest, n );
		G_SoundAtLoc( closest, CHAN_AUTO, shinobiMedia.sndWind );
	}
}

// whoever is close is thrown off the dome and knocked down
static void Shinobi_KaitenThrow( gentity_t *ent ) {
	gclient_t *client = ent->client;
	vec3_t     up = { 0, 0, 1 };
	int        i;

	for ( i = 0; i < level.num_entities; i++ ) {
		gentity_t *v = &g_entities[i];
		vec3_t     d;
		float      dist;

		if ( v == ent || !v->inuse || !v->client || v->health <= 0 || !v->takedamage )
			continue;
		if ( i < MAX_CLIENTS && v->client->sess.sessionTeam == TEAM_SPECTATOR )
			continue;
		if ( Shinobi_DamageBlocked( v, ent ) )
			continue; // our own clones
		if ( level.gametype >= GT_TEAM && i < MAX_CLIENTS && OnSameTeam( ent, v ) )
			continue;
		VectorSubtract( v->client->ps.origin, client->ps.origin, d );
		if ( fabs( d[2] ) > 64 )
			continue;
		d[2] = 0;
		dist = VectorNormalize( d );
		if ( dist > SHINOBI_KAITEN_RADIUS )
			continue;
		if ( dist < 1.0f )
			AngleVectors( client->ps.viewangles, d, NULL, NULL );
		v->client->ps.velocity[0] = d[0] * SHINOBI_KAITEN_THROW;
		v->client->ps.velocity[1] = d[1] * SHINOBI_KAITEN_THROW;
		v->client->ps.velocity[2] = SHINOBI_KAITEN_THROW_UP;
		v->client->ps.pm_flags |= PMF_TIME_KNOCKBACK;
		v->client->ps.pm_time = 300;
		v->client->ps.groundEntityNum = ENTITYNUM_NONE;
		if ( !Shinobi_CCImmune( v ) )
			G_Knockdown( v );
		Shinobi_InterruptCharge( v );
		Shinobi_EffectDir( shinobiMedia.fxWindGust[1], v->client->ps.origin, d );
		G_PlayEffectID( shinobiMedia.fxGates, v->client->ps.origin, up );
	}
}

// held Kaiten, every client frame
static void Shinobi_KaitenThink( gentity_t *ent, usercmd_t *ucmd ) {
	gclient_t *client = ent->client;
	vec3_t     up = { 0, 0, 1 };

	if ( !client->shinobi.kaitenStart ) {
		client->ps.stats[STAT_SHINOBI2] &= ~SHINOBI_STAT2_KAITEN;
		client->ps.eFlags &= ~SHINOBI_EF_KAITEN;
		return;
	}
	if ( ent->health <= 0 || client->sess.sessionTeam == TEAM_SPECTATOR || !Shinobi_CanAct( ent )
		|| client->shinobi.chargeJutsu != SJ_KAITEN ) {
		Shinobi_KaitenStop( ent );
		return;
	}
	client->ps.stats[STAT_SHINOBI2] |= SHINOBI_STAT2_KAITEN;
	client->ps.fd.forcePowerRegenDebounceTime = level.time + 500;
	ucmd->buttons &= ~( BUTTON_ATTACK | BUTTON_ALT_ATTACK );
	client->ps.forceHandExtend = HANDEXTEND_TAUNT;
	client->ps.forceHandExtendTime = level.time + 150;

	if ( !Shinobi_KaitenSpinning( ent ) ) { // winding up
		client->ps.forceDodgeAnim = Shinobi_KaitenAnim( 0 );
		return;
	}
	if ( !( client->ps.eFlags & SHINOBI_EF_KAITEN ) ) { // the dome goes up
		client->ps.eFlags |= SHINOBI_EF_KAITEN;
		G_PlayEffectID( shinobiMedia.fxGates, client->ps.origin, up );
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndWind2 );
	}
	client->ps.forceDodgeAnim = Shinobi_KaitenAnim( 0 ); // the wind-up pose, frozen by the cgame while it spins
	Shinobi_KaitenDeflect( ent );
	while ( client->shinobi.kaitenTick <= level.time ) {
		client->shinobi.kaitenTick += SHINOBI_KAITEN_TICK_MS;
		if ( client->ps.fd.forcePower < SHINOBI_KAITEN_DRAIN ) {
			trap->SendServerCommand( ent - g_entities, "cp \"Out of chakra\"" );
			Shinobi_KaitenStop( ent );
			return;
		}
		client->ps.fd.forcePower -= SHINOBI_KAITEN_DRAIN;
		Shinobi_KaitenThrow( ent );
		if ( ( client->shinobi.kaitenTick / SHINOBI_KAITEN_TICK_MS ) % 4 == 0 )
			G_Sound( ent, CHAN_AUTO, shinobiMedia.sndWind );
	}
}

/*
---------------------------------------------------------------------------
Lightning Dash (Raiton, held): you become a bolt of lightning racing ahead
(Pmove forces the run, 3x speed, steered with the mouse) for as long as the
key is held and the chakra lasts. Whoever you pass close to is shocked
(damage, no stun); puddles you cross are electrified.
---------------------------------------------------------------------------
*/
qboolean Shinobi_CastRaiDash( gentity_t *ent ) {
	gclient_t *client = ent->client;
	vec3_t     up = { 0, 0, 1 };

	if ( SHINOBI_HAND_ANY( &client->ps ) || client->shinobi.raiDashEnd )
		return qfalse;
	if ( client->shinobi.raiDashNext > level.time ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		trap->SendServerCommand( ent - g_entities, va( "cp \"Lightning Dash ready in %.1f s\"",
			( client->shinobi.raiDashNext - level.time ) / 1000.0f ) );
		return qfalse;
	}
	client->shinobi.raiDashEnd = level.time + SHINOBI_RAIDASH_MAX_MS;
	client->shinobi.raiDashTick = level.time + SHINOBI_RAIDASH_TICK_MS;
	client->shinobi.raiDashFx = 0;
	client->ps.stats[STAT_SHINOBI] |= SHINOBI_STAT_RAIDASH;
	client->ps.eFlags |= SHINOBI_EF_RAIDASH;
	G_PlayEffectID( shinobiMedia.fxChidoriHit[1], client->ps.origin, up );
	G_Sound( ent, CHAN_AUTO, shinobiMedia.sndChidoriDash );
	return qtrue;
}

void Shinobi_RaiDashEnd( gentity_t *ent ) {
	gclient_t *client = ent->client;
	vec3_t     up = { 0, 0, 1 };

	if ( !client->shinobi.raiDashEnd )
		return;
	client->shinobi.raiDashEnd = 0;
	client->shinobi.raiDashNext = level.time + SHINOBI_RAIDASH_COOLDOWN;
	client->ps.stats[STAT_SHINOBI] &= ~SHINOBI_STAT_RAIDASH;
	client->ps.eFlags &= ~SHINOBI_EF_RAIDASH;
	if ( client->shinobi.chargeJutsu == SJ_RAIDASH )
		client->shinobi.chargeJutsu = SJ_NONE;
	if ( ent->health > 0 )
		G_PlayEffectID( shinobiMedia.fxChidoriHit[0], client->ps.origin, up );
}

static void Shinobi_RaiDashThink( gentity_t *ent ) {
	gclient_t *client = ent->client;
	vec3_t     up = { 0, 0, 1 }, feet;
	int        i;

	if ( !client->shinobi.raiDashEnd ) {
		client->ps.stats[STAT_SHINOBI] &= ~SHINOBI_STAT_RAIDASH;
		client->ps.eFlags &= ~SHINOBI_EF_RAIDASH;
		return;
	}
	if ( ent->health <= 0 || client->sess.sessionTeam == TEAM_SPECTATOR || level.time >= client->shinobi.raiDashEnd
		|| !Shinobi_CanAct( ent ) || client->shinobi.stunEnd > level.time || client->shinobi.prisonCaster
		|| client->shinobi.shadowBy || ( client->ps.stats[STAT_SHINOBI] & SHINOBI_STAT_STAGGER ) ) {
		Shinobi_RaiDashEnd( ent );
		return;
	}
	client->ps.fd.forcePowerRegenDebounceTime = level.time + 500;
	while ( client->shinobi.raiDashTick <= level.time ) {
		client->shinobi.raiDashTick += SHINOBI_RAIDASH_TICK_MS;
		if ( client->ps.fd.forcePower < SHINOBI_RAIDASH_DRAIN ) {
			client->ps.fd.forcePower = 0;
			trap->SendServerCommand( ent - g_entities, "cp \"Out of chakra: the lightning fades\"" );
			Shinobi_RaiDashEnd( ent );
			return;
		}
		client->ps.fd.forcePower -= SHINOBI_RAIDASH_DRAIN;
	}
	client->ps.stats[STAT_SHINOBI] |= SHINOBI_STAT_RAIDASH;
	client->ps.eFlags |= SHINOBI_EF_RAIDASH;

	// puddles under the bolt crackle
	VectorCopy( client->ps.origin, feet );
	feet[2] += ent->r.mins[2] + 2;
	if ( client->shinobi.raiDashFx <= level.time ) {
		client->shinobi.raiDashFx = level.time + 150;
		Shinobi_ElectrifyAt( feet, ent->s.number + 1 );
	}

	// shock whoever the bolt passes
	for ( i = 0; i < level.num_entities; i++ ) {
		gentity_t *v = &g_entities[i];
		vec3_t     rel;

		if ( v == ent || !v->inuse || !v->client || !v->takedamage || v->health <= 0 )
			continue;
		if ( v->s.number < MAX_CLIENTS && ( v->client->sess.sessionTeam == TEAM_SPECTATOR || SHINOBI_UNSEEN( &v->client->ps ) ) )
			continue;
		if ( v->shinobiCloneOwner == ent->s.number + 1 || ( level.gametype >= GT_TEAM && OnSameTeam( ent, v ) ) )
			continue;
		if ( v->client->shinobi.raiShockAt && level.time - v->client->shinobi.raiShockAt < SHINOBI_RAIDASH_REHIT_MS )
			continue;
		VectorSubtract( v->r.currentOrigin, client->ps.origin, rel );
		if ( fabs( rel[2] ) > 64 )
			continue;
		rel[2] = 0;
		if ( VectorLength( rel ) > SHINOBI_RAIDASH_WIDTH + 16 )
			continue;
		v->client->shinobi.raiShockAt = level.time;
		shinobiHitElement = SE_LIGHTNING;
		shinobiJutsuDamage = 1;
		G_Damage( v, ent, ent, NULL, v->r.currentOrigin, SHINOBI_RAIDASH_DAMAGE, DAMAGE_NO_KNOCKBACK, MOD_DEMP2 );
		shinobiJutsuDamage = 0;
		shinobiHitElement = SE_NONE;
		if ( v->inuse && v->client )
			v->client->ps.electrifyTime = level.time + 300; // a crackle, no stun
		G_PlayEffectID( shinobiMedia.fxChidoriHit[0], v->r.currentOrigin, up );
	}
}

void Shinobi_TaiThink( gentity_t *ent, usercmd_t *ucmd );

// every client frame, before Pmove
void Shinobi_YinYangThink( gentity_t *ent, usercmd_t *ucmd ) {
	gclient_t *client = ent->client;

	Shinobi_TaiThink( ent, ucmd ); // 64 Palms, Reverse Lotus, Dynamic Entry
	Shinobi_RaiDashThink( ent );
	Shinobi_HealThink( ent, ucmd );
	Shinobi_KaitenThink( ent, ucmd );

	if ( client->shinobi.blindEnd > level.time && ent->health > 0 )
		client->ps.stats[STAT_SHINOBI] |= SHINOBI_STAT_BLIND;
	else
		client->ps.stats[STAT_SHINOBI] &= ~SHINOBI_STAT_BLIND;

	if ( !client->shinobi.shadowHideEnd ) {
		client->ps.stats[STAT_SHINOBI] &= ~SHINOBI_STAT_SHADOWHIDE;
		return;
	}
	if ( ent->health <= 0 || client->sess.sessionTeam == TEAM_SPECTATOR || level.time >= client->shinobi.shadowHideEnd
		|| ( ucmd->buttons & ( BUTTON_ATTACK | BUTTON_ALT_ATTACK ) ) ) {
		Shinobi_ShadowHideEnd( ent );
		return;
	}
	client->ps.fd.forcePowerRegenDebounceTime = level.time + 500;
	while ( client->shinobi.shadowHideTick <= level.time ) {
		client->shinobi.shadowHideTick += SHINOBI_SHADOWHIDE_TICK_MS;
		if ( client->ps.fd.forcePower < 1 ) {
			trap->SendServerCommand( ent - g_entities, "cp \"Out of chakra: you rise from the shadow\"" );
			Shinobi_ShadowHideEnd( ent );
			return;
		}
		client->ps.fd.forcePower--;
	}
	client->ps.stats[STAT_SHINOBI] |= SHINOBI_STAT_SHADOWHIDE;
	if ( ent->r.contents ) {
		ent->r.contents = 0;
		trap->LinkEntity( (sharedEntity_t *)ent );
	}
}

/*
===========================================================================
Taijutsu of the Leaf and the Hyuga (Yin / Yang)
===========================================================================
*/
// someone a technique may strike: alive, not on our side, not ourselves (players and clones)
static qboolean Shinobi_TaiTarget( gentity_t *ent, gentity_t *v ) {
	extern qboolean Shinobi_ComboTarget( gentity_t *ent, gentity_t *v );
	return Shinobi_ComboTarget( ent, v ) && v->client ? qtrue : qfalse;
}

/*
---------------------------------------------------------------------------
64 Seals (held; "palms" in the code): rooted in the stance, the field
grows; full, the air palm strikes all in it - held on, the full field guards you.
---------------------------------------------------------------------------
*/
static int Shinobi_StanceAnim( void ) {
	static int anim = -1, mod = -1;
	if ( mod != shinobi_stanceAnim.modificationCount ) {
		mod = shinobi_stanceAnim.modificationCount;
		anim = GetIDForString( animTable, shinobi_stanceAnim.string );
		if ( anim < 0 || anim >= MAX_ANIMATIONS ) {
			Com_Printf( "^3shinobi_stanceAnim: no animation \"%s\" - using BOTH_SHINOBI_STANCE_HYUGA\n", shinobi_stanceAnim.string );
			anim = BOTH_SHINOBI_STANCE_HYUGA;
		}
	}
	return anim;
}

qboolean Shinobi_CastPalms( gentity_t *ent ) {
	gclient_t *client = ent->client;
	gentity_t *f;
	vec3_t     floor, pos;

	if ( ent->s.number >= MAX_CLIENTS || client->shinobi.palmsStart || SHINOBI_HAND_ANY( &client->ps )
		|| ( client->ps.stats[STAT_SHINOBI2] & SHINOBI_STAT2_FLINCH ) )
		return qfalse;
	VectorCopy( client->ps.origin, pos );
	if ( !Shinobi_FloorBelow( pos, 128, floor ) ) {
		VectorCopy( pos, floor );
		floor[2] += ent->r.mins[2];
	}
	f = G_Spawn();
	if ( !f )
		return qfalse;
	client->shinobi.blocking = qfalse;
	client->shinobi.palmsStart = level.time;
	client->shinobi.palmsFull = 0;
	client->shinobi.palmsTick = level.time + SHINOBI_PALMS_DRAIN_MS;
	client->ps.stats[STAT_SHINOBI2] |= SHINOBI_STAT2_ROOTED; // (at once: BlockThink keeps it while held)
	VectorClear( client->ps.velocity );
	f->classname = "shinobi_palms";
	f->s.eType = ET_GENERAL;
	f->s.generic1 = SHINOBI_ENT_PALMS;
	f->r.ownerNum = ent->s.number;
	f->s.owner = ent->s.number;
	f->r.contents = 0;
	f->r.svFlags |= SVF_BROADCAST;
	G_SetOrigin( f, floor );
	f->s.time = level.time;
	f->s.time2 = level.time + SHINOBI_PALMS_GROW_MS;
	f->s.angles2[0] = SHINOBI_PALMS_RADIUS;
	trap->LinkEntity( (sharedEntity_t *)f );
	client->shinobi.palmsEnt = f->s.number + 1;
	client->ps.forceHandExtend = HANDEXTEND_TAUNT; // into the stance
	client->ps.forceDodgeAnim = Shinobi_StanceAnim();
	client->ps.forceHandExtendTime = level.time + 150;
	G_Sound( ent, CHAN_AUTO, shinobiMedia.sndJutsuCharge );
	return qtrue;
}

void Shinobi_PalmsStop( gentity_t *ent, qboolean struck ) {
	gclient_t *client = ent->client;

	if ( !client || !client->shinobi.palmsStart )
		return;
	struck = struck || client->shinobi.palmsFull; // (a full field fades out, an unfinished one is just gone)
	client->shinobi.palmsStart = client->shinobi.palmsFull = 0;
	if ( client->shinobi.chargeJutsu == SJ_PALMS )
		client->shinobi.chargeJutsu = SJ_NONE;
	client->ps.stats[STAT_SHINOBI2] &= ~SHINOBI_STAT2_ROOTED;
	if ( client->ps.forceHandExtend == HANDEXTEND_TAUNT && client->ps.forceDodgeAnim == Shinobi_StanceAnim() )
		client->ps.forceHandExtend = HANDEXTEND_NONE; // out of the stance
	if ( client->shinobi.palmsEnt ) {
		gentity_t *f = &g_entities[client->shinobi.palmsEnt - 1];
		client->shinobi.palmsEnt = 0;
		if ( f->inuse && f->classname && !strcmp( f->classname, "shinobi_palms" ) ) {
			if ( struck ) { // the field flashes and fades
				f->s.frame = 1;
				f->s.time2 = level.time;
				f->think = G_FreeEntity;
				f->nextthink = level.time + 500;
			}
			else
				G_FreeEntity( f );
		}
	}
}

// the air palm: everyone in the field (the first time, when it fills) - or only those not struck
// in the last SHINOBI_PALMS_GUARD_MS (the full field guarding); returns how many it struck
static int Shinobi_PalmsStrike( gentity_t *ent, qboolean guarding ) {
	gclient_t *client = ent->client;
	vec3_t     eye, up = { 0, 0, 1 };
	int        i, hits = 0;

	VectorCopy( client->ps.origin, eye );
	eye[2] += client->ps.viewheight - 8;
	for ( i = 0; i < level.num_entities; i++ ) {
		gentity_t *v = &g_entities[i];
		vec3_t     d, at;
		trace_t    tr;
		float      dist;
		if ( !Shinobi_TaiTarget( ent, v ) )
			continue;
		if ( guarding && v->client->shinobi.palmsHitNext > level.time )
			continue;
		VectorSubtract( v->r.currentOrigin, client->ps.origin, d );
		if ( fabs( d[2] ) > 96 )
			continue;
		d[2] = 0;
		dist = VectorNormalize( d );
		if ( dist > SHINOBI_PALMS_RADIUS )
			continue;
		if ( dist < 1.0f ) { // right on top of us: out the way we face
			vec3_t yaw;
			VectorSet( yaw, 0, client->ps.viewangles[YAW], 0 );
			AngleVectors( yaw, d, NULL, NULL );
		}
		VectorCopy( v->r.currentOrigin, at );
		trap->Trace( &tr, eye, NULL, NULL, at, ent->s.number, MASK_SOLID, qfalse, 0, 0 );
		if ( tr.fraction < 1.0f )
			continue; // a wall between
		v->client->shinobi.palmsHitNext = level.time + SHINOBI_PALMS_GUARD_MS;
		shinobiJutsuDamage = 1;
		G_Damage( v, ent, ent, d, at, SHINOBI_PALMS_DAMAGE, DAMAGE_NO_KNOCKBACK | DAMAGE_NO_ARMOR, MOD_FORCE_DARK ); // (no guard holds it)
		shinobiJutsuDamage = 0;
		if ( v->inuse && v->client && v->health > 0 && !Shinobi_CCImmune( v ) ) {
			VectorScale( d, SHINOBI_PALMS_PUSH, v->client->ps.velocity );
			v->client->ps.velocity[2] = SHINOBI_PALMS_UP;
			v->client->ps.groundEntityNum = ENTITYNUM_NONE;
			Shinobi_CCGrant( v, level.time + 800 );
		}
		Shinobi_EffectDir( shinobiMedia.fxWindGust[0], at, d );
		hits++;
	}
	shinobiTest.palmsHits += hits;
	if ( hits || !guarding ) {
		gentity_t *f = client->shinobi.palmsEnt ? &g_entities[client->shinobi.palmsEnt - 1] : NULL;
		if ( f && f->inuse )
			f->s.apos.trTime = level.time; // (the field flashes)
		G_PlayEffectID( shinobiMedia.fxGates, client->ps.origin, up );
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndWind2 );
	}
	return hits;
}

static void Shinobi_PalmsThink( gentity_t *ent, usercmd_t *ucmd ) {
	gclient_t *client = ent->client;

	if ( !client->shinobi.palmsStart )
		return;
	if ( ent->health <= 0 || client->sess.sessionTeam == TEAM_SPECTATOR || !Shinobi_CanAct( ent )
		|| client->shinobi.chargeJutsu != SJ_PALMS ) {
		Shinobi_PalmsStop( ent, qfalse ); // let go (early: lost)
		return;
	}
	client->ps.fd.forcePowerRegenDebounceTime = level.time + 500;
	ucmd->forwardmove = ucmd->rightmove = 0; // feet planted
	if ( ucmd->upmove > 0 )
		ucmd->upmove = 0;
	ucmd->buttons &= ~( BUTTON_ATTACK | BUTTON_ALT_ATTACK );
	client->ps.stats[STAT_SHINOBI2] |= SHINOBI_STAT2_ROOTED;
	client->ps.velocity[0] = client->ps.velocity[1] = 0;
	client->ps.forceHandExtend = HANDEXTEND_TAUNT; // the stance, held
	client->ps.forceDodgeAnim = Shinobi_StanceAnim();
	client->ps.forceHandExtendTime = level.time + 150;
	while ( client->shinobi.palmsTick <= level.time ) {
		client->shinobi.palmsTick += SHINOBI_PALMS_DRAIN_MS;
		if ( client->ps.fd.forcePower < 1 ) {
			trap->SendServerCommand( ent - g_entities, "cp \"Out of chakra\"" );
			Shinobi_PalmsStop( ent, qfalse );
			return;
		}
		client->ps.fd.forcePower--;
	}
	if ( !client->shinobi.palmsFull ) {
		if ( level.time >= client->shinobi.palmsStart + SHINOBI_PALMS_GROW_MS ) { // full: the strike, then it guards
			gentity_t *f = client->shinobi.palmsEnt ? &g_entities[client->shinobi.palmsEnt - 1] : NULL;
			client->shinobi.palmsFull = level.time;
			if ( f && f->inuse )
				f->s.frame = 2;
			Shinobi_PalmsStrike( ent, qfalse );
			client->ps.forceHandExtend = HANDEXTEND_TAUNT;
		}
		return;
	}
	Shinobi_PalmsStrike( ent, qtrue ); // whoever comes into it (each once per SHINOBI_PALMS_GUARD_MS)
}

/*
---------------------------------------------------------------------------
Reverse Lotus: the blink with a kick up; caught, both go up, hang spinning,
come down hard (Shinobi_LotusThink on the attacker, Shinobi_LotusHeld on him).
---------------------------------------------------------------------------
*/
static void Shinobi_LotusEnd( gentity_t *ent ) {
	gclient_t *client = ent->client;
	if ( client->shinobi.lotusVictim ) {
		gentity_t *v = &g_entities[client->shinobi.lotusVictim - 1];
		if ( v->inuse && v->client && v->client->shinobi.lotusBy == ent->s.number + 1 )
			v->client->shinobi.lotusBy = 0;
	}
	client->shinobi.lotusPhase = client->shinobi.lotusVictim = 0;
}

qboolean Shinobi_CastLotus( gentity_t *ent ) {
	gclient_t *client = ent->client;
	vec3_t     yaw, fwd, end;
	trace_t    tr;
	gentity_t *best = NULL;
	float      bestDist = SHINOBI_LOTUS_REACH + 1;
	int        i;

	if ( ent->s.number >= MAX_CLIENTS || client->shinobi.lotusPhase || SHINOBI_HAND_ANY( &client->ps ) )
		return qfalse;
	VectorSet( yaw, 0, client->ps.viewangles[YAW], 0 );
	AngleVectors( yaw, fwd, NULL, NULL );
	// the blink in: as far as SHINOBI_LOTUS_BLINK, or up to someone standing closer
	for ( i = 0; i < level.num_entities; i++ ) {
		gentity_t *v = &g_entities[i];
		vec3_t     d;
		float      dist;
		if ( !Shinobi_TaiTarget( ent, v ) )
			continue;
		VectorSubtract( v->r.currentOrigin, client->ps.origin, d );
		if ( fabs( d[2] ) > 64 )
			continue;
		d[2] = 0;
		dist = VectorNormalize( d );
		if ( dist > SHINOBI_LOTUS_BLINK + SHINOBI_LOTUS_REACH || DotProduct( d, fwd ) < 0.7f )
			continue;
		if ( !best || dist < bestDist ) {
			best = v;
			bestDist = dist;
		}
	}
	{
		const float go = best ? ( bestDist - 40 > 0 ? ( bestDist - 40 < SHINOBI_LOTUS_BLINK ? bestDist - 40 : SHINOBI_LOTUS_BLINK ) : 0 )
			: SHINOBI_LOTUS_BLINK;
		VectorMA( client->ps.origin, go, fwd, end );
		trap->Trace( &tr, client->ps.origin, ent->r.mins, ent->r.maxs, end, ent->s.number, MASK_PLAYERSOLID, qfalse, 0, 0 );
		if ( !tr.startsolid && !tr.allsolid && go > 8 ) {
			Shinobi_EffectDir( shinobiMedia.fxShunshin, client->ps.origin, fwd );
			Shinobi_TeleportClient( ent, tr.endpos );
			SetClientViewAngle( ent, yaw );
		}
	}
	client->ps.forceHandExtend = HANDEXTEND_TAUNT; // the kick up (our own: down onto a hand, the leg straight up)
	client->ps.forceDodgeAnim = BOTH_SHINOBI_KICK_UP;
	client->ps.forceHandExtendTime = level.time + SHINOBI_LOTUS_KICK_MS + 300;
	VectorClear( client->ps.velocity );
	G_Sound( ent, CHAN_AUTO, shinobiMedia.sndWind2 );

	if ( !best || Distance( best->r.currentOrigin, client->ps.origin ) > SHINOBI_LOTUS_REACH + 8 )
		return qtrue; // the kick goes into the air
	// a grab: a guard or a parry doesn't stop it (as with every grab - Water Prison, Shadow Bind, the stock grab)
	if ( Shinobi_CCImmune( best ) || best->client->shinobi.lotusBy ) {
		G_Damage( best, ent, ent, fwd, best->r.currentOrigin, 12, DAMAGE_NO_KNOCKBACK, MOD_MELEE ); // (immune: just the kick)
		return qtrue;
	}
	// caught: he stands stunned while the kick comes up (Shinobi_LotusThink launches you both)
	best->client->shinobi.lotusBy = ent->s.number + 1;
	best->client->shinobi.flinchEnd = 0;
	best->client->ps.weaponTime = 0; // (whatever he was swinging is gone)
	// neither of you is hurt by the floor (the slam is the damage), from now to well after the landing
	best->client->shinobi.slamNoFall = client->shinobi.slamNoFall = level.time + SHINOBI_LOTUS_KICK_MS + SHINOBI_LOTUS_MAX_MS + 2000;
	client->shinobi.lotusVictim = best->s.number + 1;
	client->shinobi.lotusPhase = 1; // the kick
	client->shinobi.lotusTime = level.time;
	Shinobi_CCGrant( best, level.time + SHINOBI_LOTUS_KICK_MS + SHINOBI_LOTUS_MAX_MS + 1500 );
	return qtrue;
}

// the kick lands: up into the air, both of you
static void Shinobi_LotusLaunch( gentity_t *ent, gentity_t *best ) {
	gclient_t *client = ent->client;
	vec3_t     spot, top, side, yaw, fwd, up = { 0, 0, 1 };
	trace_t    tr;

	VectorSet( yaw, 0, client->ps.viewangles[YAW], 0 );
	AngleVectors( yaw, fwd, NULL, NULL );
	VectorCopy( best->r.currentOrigin, spot );
	VectorCopy( spot, top );
	top[2] += SHINOBI_LOTUS_HEIGHT;
	trap->Trace( &tr, spot, best->r.mins, best->r.maxs, top, best->s.number, MASK_PLAYERSOLID, qfalse, 0, 0 );
	VectorCopy( tr.endpos, spot );
	spot[2] -= 8;
	VectorCopy( spot, client->shinobi.lotusSpot );
	Shinobi_EffectDir( shinobiMedia.fxShunshin, best->r.currentOrigin, up );
	Shinobi_TeleportClient( best, spot );
	VectorMA( spot, -44, fwd, side );
	trap->Trace( &tr, spot, ent->r.mins, ent->r.maxs, side, ent->s.number, MASK_PLAYERSOLID, qfalse, 0, 0 );
	Shinobi_TeleportClient( ent, tr.endpos );
	SetClientViewAngle( ent, yaw );
	Shinobi_EffectDir( shinobiMedia.fxShunshin, spot, up );
	G_Sound( best, CHAN_AUTO, G_SoundIndex( va( "sound/weapons/melee/punch%d", Q_irand( 1, 4 ) ) ) );
	Shinobi_HitText( best, STX_LAUNCH, 0 );
	if ( client->ps.forceHandExtend == HANDEXTEND_TAUNT )
		client->ps.forceHandExtend = HANDEXTEND_NONE; // (out of the kick: the spin takes over)
	client->shinobi.lotusPhase = 2; // hanging up there
	client->shinobi.lotusTime = level.time;
}

// up there and coming down: straight as a string, whatever was playing (the kick) cut off
static void Shinobi_LotusStraight( gentity_t *e ) {
	gclient_t *c = e->client;
	if ( c->ps.forceHandExtend == HANDEXTEND_TAUNT )
		c->ps.forceHandExtend = HANDEXTEND_NONE;
	c->ps.legsAnim = c->ps.torsoAnim = BOTH_SHINOBI_STRAIGHT;
	c->ps.legsTimer = c->ps.torsoTimer = 200;
}

static void Shinobi_LotusSlam( gentity_t *ent, gentity_t *v ) {
	vec3_t up = { 0, 0, 1 }, down = { 0, 0, -1 }, feet;
	VectorCopy( v->r.currentOrigin, feet );
	feet[2] += v->r.mins[2] + 2;
	G_PlayEffectID( shinobiMedia.fxQuakeDust, feet, up );
	G_PlayEffectID( shinobiMedia.fxRockLg, feet, up );
	G_Sound( v, CHAN_AUTO, shinobiMedia.sndWallBreak );
	G_ScreenShake( feet, NULL, 4.0f, 500, qfalse );
	v->client->shinobi.slamNoFall = level.time + 2000;
	ent->client->shinobi.slamNoFall = level.time + 2000;
	shinobiTest.lotusSlams++;
	G_Damage( v, ent, ent, down, v->r.currentOrigin, SHINOBI_LOTUS_DAMAGE, DAMAGE_NO_KNOCKBACK, MOD_MELEE );
	if ( v->inuse && v->client && v->health > 0 ) {
		VectorClear( v->client->ps.velocity );
		G_Knockdown( v );
	}
}

// the attacker's side, every frame
static void Shinobi_LotusThink( gentity_t *ent, usercmd_t *ucmd ) {
	gclient_t *client = ent->client;
	gentity_t *v;

	if ( !client->shinobi.lotusPhase )
		return;
	v = &g_entities[client->shinobi.lotusVictim - 1];
	if ( ent->health <= 0 || !v->inuse || !v->client || v->health <= 0 || v->client->shinobi.lotusBy != ent->s.number + 1
		|| SHINOBI_UNSEEN( &v->client->ps ) || level.time - client->shinobi.lotusTime > SHINOBI_LOTUS_MAX_MS ) {
		Shinobi_LotusEnd( ent );
		return;
	}
	ucmd->forwardmove = ucmd->rightmove = ucmd->upmove = 0;
	ucmd->buttons &= ~( BUTTON_ATTACK | BUTTON_ALT_ATTACK );
	if ( client->shinobi.lotusPhase == 1 ) { // the kick coming up (its anim, on the spot)
		client->ps.velocity[0] = client->ps.velocity[1] = 0;
		client->ps.forceHandExtend = HANDEXTEND_TAUNT;
		client->ps.forceDodgeAnim = BOTH_SHINOBI_KICK_UP;
		if ( client->ps.forceHandExtendTime < level.time + 150 )
			client->ps.forceHandExtendTime = level.time + 150;
		if ( level.time - client->shinobi.lotusTime >= SHINOBI_LOTUS_KICK_MS ) {
			if ( Distance( v->r.currentOrigin, client->ps.origin ) > SHINOBI_LOTUS_REACH + 40 ) {
				Shinobi_LotusEnd( ent ); // he got away (pushed off, Kawarimi...)
				return;
			}
			Shinobi_LotusLaunch( ent, v );
		}
		return;
	}
	// up there and coming down: both head down (the cgame turns them over: SHINOBI_PFRAME_LOTUS),
	// spinning about his axis - you held at his back, the way he faces turning with the spin
	ent->s.frame |= SHINOBI_PFRAME_LOTUS;
	ent->client->ps.stats[STAT_SHINOBI_LOCK] |= SHINOBI_LOCK_LOTUS;
	Shinobi_LotusStraight( ent );
	{
		vec3_t  ang = { 0, 0, 0 }, dir, spot;
		trace_t tr;
		ang[YAW] = SHINOBI_LOTUS_SPIN_YAW( level.time );
		AngleVectors( ang, dir, NULL, NULL );
		VectorMA( v->client->ps.origin, -SHINOBI_LOTUS_BEHIND, dir, spot );
		ent->r.contents = 0; // (you two overlap: not solid while you hold on - solidHack gives it back after)
		client->solidHack = level.time + 150;
		trap->Trace( &tr, v->client->ps.origin, ent->r.mins, ent->r.maxs, spot, v->s.number, MASK_PLAYERSOLID, qfalse, 0, 0 );
		VectorCopy( tr.allsolid ? client->ps.origin : tr.endpos, client->ps.origin );
		VectorCopy( v->client->ps.velocity, client->ps.velocity );
		client->ps.gravity = v->client->ps.gravity;
		client->ps.groundEntityNum = ENTITYNUM_NONE;
	}
	if ( client->shinobi.lotusPhase == 2 ) { // hanging up there
		client->ps.gravity = 1; // (0 would set off Pmove's zero-g jump code: it sank them at ~300 a second)
		VectorClear( client->ps.velocity );
		if ( level.time - client->shinobi.lotusTime >= SHINOBI_LOTUS_HANG_MS )
			client->shinobi.lotusPhase = 3; // down (he falls: Shinobi_LotusHeld; you go with him)
		return;
	}
	// coming down with him
	if ( v->client->ps.groundEntityNum != ENTITYNUM_NONE ) {
		Shinobi_LotusSlam( ent, v );
		Shinobi_LotusEnd( ent );
		VectorClear( client->ps.velocity ); // (the 1300 down left on him bounced him 150 up off the floor)
		client->ps.legsTimer = client->ps.torsoTimer = 0; // on his feet
		{ // let go: a step back off him, so the two of you aren't stuck in each other when you're solid again
			vec3_t  ang = { 0, 0, 0 }, dir, spot;
			trace_t tr;
			ang[YAW] = SHINOBI_LOTUS_SPIN_YAW( level.time );
			AngleVectors( ang, dir, NULL, NULL );
			VectorMA( v->client->ps.origin, -40, dir, spot );
			trap->Trace( &tr, v->client->ps.origin, ent->r.mins, ent->r.maxs, spot, v->s.number, MASK_PLAYERSOLID, qfalse, 0, 0 );
			if ( !tr.allsolid && tr.fraction * 40 >= 32 )
				VectorCopy( tr.endpos, client->ps.origin );
		}
	}
}

// from G_Damage: while he is held in your lotus, he can't hurt you (his blade, a swing already going)
qboolean Shinobi_LotusShield( gentity_t *targ, gentity_t *attacker ) {
	gentity_t *v;
	if ( !targ || !targ->client || targ->s.number >= MAX_CLIENTS || !targ->client->shinobi.lotusPhase
		|| !targ->client->shinobi.lotusVictim || !attacker || attacker == targ )
		return qfalse;
	v = &g_entities[targ->client->shinobi.lotusVictim - 1];
	if ( attacker != v && !( attacker->r.ownerNum == v->s.number && !attacker->client ) )
		return qfalse;
	shinobiTest.lotusShielded++;
	return qtrue;
}

// his side, every frame: helpless, held up there, then driven down
static void Shinobi_LotusHeld( gentity_t *ent, usercmd_t *ucmd ) {
	gclient_t *client = ent->client;
	gentity_t *a;

	if ( !client->shinobi.lotusBy )
		return;
	a = &g_entities[client->shinobi.lotusBy - 1];
	if ( !a->inuse || !a->client || a->client->shinobi.lotusVictim != ent->s.number + 1 || !a->client->shinobi.lotusPhase ) {
		client->shinobi.lotusBy = 0;
		return;
	}
	ucmd->forwardmove = ucmd->rightmove = ucmd->upmove = 0;
	ucmd->buttons &= ~( BUTTON_ATTACK | BUTTON_ALT_ATTACK | SHINOBI_BUTTON_BLOCK );
	client->shinobi.blocking = qfalse;
	if ( a->client->shinobi.lotusPhase == 1 ) { // stunned on his feet while the kick comes up
		client->ps.velocity[0] = client->ps.velocity[1] = 0;
		return;
	}
	ent->s.frame |= SHINOBI_PFRAME_LOTUS;
	ent->client->ps.stats[STAT_SHINOBI_LOCK] |= SHINOBI_LOCK_LOTUS; // head down, spinning (the cgame)
	Shinobi_LotusStraight( ent );
	client->ps.velocity[0] = client->ps.velocity[1] = 0;
	if ( a->client->shinobi.lotusPhase == 2 ) {
		client->ps.gravity = 1;
		VectorClear( client->ps.velocity );
	}
	else if ( client->ps.velocity[2] > -SHINOBI_LOTUS_FALL )
		client->ps.velocity[2] = -SHINOBI_LOTUS_FALL;
}

/*
---------------------------------------------------------------------------
Dynamic Entry: the flying kick (Pmove flies it: SHINOBI_BC_ENTRY); whoever it
meets is hurt, thrown along and knocked down.
---------------------------------------------------------------------------
*/
qboolean Shinobi_CastEntry( gentity_t *ent ) {
	gclient_t *client = ent->client;
	vec3_t     yaw, fwd;

	if ( ent->s.number >= MAX_CLIENTS || client->shinobi.entryEnd || SHINOBI_HAND_ANY( &client->ps ) )
		return qfalse;
	if ( client->shinobi.entryNext > level.time ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		trap->SendServerCommand( ent - g_entities, va( "cp \"Dynamic Entry ready in %.1f s\"",
			( client->shinobi.entryNext - level.time ) / 1000.0f ) );
		return qfalse;
	}
	client->shinobi.entryNext = level.time + SHINOBI_ENTRY_COOLDOWN;
	VectorSet( yaw, 0, client->ps.viewangles[YAW], 0 );
	AngleVectors( yaw, fwd, NULL, NULL );
	client->shinobi.entryEnd = level.time + SHINOBI_ENTRY_MS;
	client->shinobi.entryHit[0] = client->shinobi.entryHit[1] = 0;
	client->ps.stats[STAT_SHINOBI_BLADECOMBO] |= SHINOBI_BC_ENTRY;
	VectorScale( fwd, SHINOBI_ENTRY_SPEED, client->ps.velocity );
	client->ps.velocity[2] = SHINOBI_ENTRY_LIFT;
	client->ps.groundEntityNum = ENTITYNUM_NONE;
	client->ps.weaponTime = SHINOBI_ENTRY_MS;
	G_Sound( ent, CHAN_AUTO, shinobiMedia.sndWind2 );
	return qtrue;
}

static void Shinobi_EntryThink( gentity_t *ent, usercmd_t *ucmd ) {
	gclient_t *client = ent->client;
	vec3_t     yaw, fwd;
	int        i;

	if ( !client->shinobi.entryEnd ) {
		client->ps.stats[STAT_SHINOBI_BLADECOMBO] &= ~SHINOBI_BC_ENTRY;
		return;
	}
	if ( level.time >= client->shinobi.entryEnd || ent->health <= 0 || client->sess.sessionTeam == TEAM_SPECTATOR ) {
		client->shinobi.entryEnd = 0;
		client->ps.stats[STAT_SHINOBI_BLADECOMBO] &= ~SHINOBI_BC_ENTRY;
		return;
	}
	client->ps.stats[STAT_SHINOBI_BLADECOMBO] |= SHINOBI_BC_ENTRY;
	ucmd->buttons &= ~( BUTTON_ATTACK | BUTTON_ALT_ATTACK );
	VectorSet( yaw, 0, client->ps.viewangles[YAW], 0 );
	AngleVectors( yaw, fwd, NULL, NULL );
	for ( i = 0; i < level.num_entities; i++ ) {
		gentity_t *v = &g_entities[i];
		vec3_t     d;
		int       *seen;
		if ( !Shinobi_TaiTarget( ent, v ) )
			continue;
		VectorSubtract( v->r.currentOrigin, client->ps.origin, d );
		if ( fabs( d[2] ) > 56 || VectorLength( d ) > SHINOBI_ENTRY_RADIUS + 16 )
			continue;
		seen = &client->shinobi.entryHit[i < 32 ? 0 : 1];
		if ( i < 64 && ( *seen & ( 1 << ( i & 31 ) ) ) )
			continue;
		if ( i < 64 )
			*seen |= 1 << ( i & 31 );
		shinobiTest.entryHits++;
		G_Damage( v, ent, ent, fwd, v->r.currentOrigin, SHINOBI_ENTRY_DAMAGE, DAMAGE_NO_KNOCKBACK, MOD_MELEE );
		G_Sound( v, CHAN_AUTO, G_SoundIndex( va( "sound/weapons/melee/punch%d", Q_irand( 1, 4 ) ) ) );
		if ( v->inuse && v->client && v->health > 0 && shinobiBlockResult != SBR_BLOCKED && shinobiBlockResult != SBR_PARRIED
			&& !Shinobi_CCImmune( v ) ) {
			VectorScale( fwd, SHINOBI_ENTRY_PUSH, v->client->ps.velocity );
			v->client->ps.velocity[2] = 220;
			v->client->ps.groundEntityNum = ENTITYNUM_NONE;
			G_Knockdown( v );
			Shinobi_CCGrant( v, level.time + 1500 );
		}
	}
}

// every client frame (from Shinobi_YinYangThink)
void Shinobi_TaiThink( gentity_t *ent, usercmd_t *ucmd ) {
	Shinobi_PalmsThink( ent, ucmd );
	Shinobi_LotusThink( ent, ucmd );
	Shinobi_LotusHeld( ent, ucmd );
	Shinobi_EntryThink( ent, ucmd );
}

void Shinobi_TaiClear( gentity_t *ent ) {
	gclient_t *client = ent->client;
	if ( client->shinobi.palmsStart )
		Shinobi_PalmsStop( ent, qfalse );
	if ( client->shinobi.lotusPhase )
		Shinobi_LotusEnd( ent );
	client->shinobi.lotusBy = 0;
	client->shinobi.entryEnd = 0;
	client->ps.stats[STAT_SHINOBI_BLADECOMBO] &= ~SHINOBI_BC_ENTRY;
}
