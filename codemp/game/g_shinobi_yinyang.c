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

	if ( client->shinobi.healNext > level.time ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		trap->SendServerCommand( ent - g_entities, va( "cp \"Healing ready in %.1f s\"",
			( client->shinobi.healNext - level.time ) / 1000.0f ) );
		return qfalse;
	}
	if ( ent->health >= client->ps.stats[STAT_MAX_HEALTH] ) {
		trap->SendServerCommand( ent - g_entities, "cp \"Not hurt\"" );
		return qfalse;
	}
	client->shinobi.healNext = level.time + SHINOBI_HEAL_COOLDOWN;
	ent->health += shinobiHealAmount[lvl - 1];
	if ( ent->health > client->ps.stats[STAT_MAX_HEALTH] )
		ent->health = client->ps.stats[STAT_MAX_HEALTH];
	client->ps.stats[STAT_HEALTH] = ent->health;
	G_PlayEffectID( shinobiMedia.fxGates, client->ps.origin, up );
	G_Sound( ent, CHAN_AUTO, shinobiMedia.sndHeal );
	return qtrue;
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

// every client frame, before Pmove
void Shinobi_YinYangThink( gentity_t *ent, usercmd_t *ucmd ) {
	gclient_t *client = ent->client;

	Shinobi_RaiDashThink( ent );

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
