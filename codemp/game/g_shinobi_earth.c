/*
===========================================================================
JK_Shinobi - more Doton: Burrow, Stone Skin (Rock Pillar lives with the earth
wall in g_shinobi_elements.c, Earth Clone with the clones).
===========================================================================
*/
#include "g_shinobi_local.h"

extern void G_Knockdown( gentity_t *victim ); // g_combat.c

qboolean Shinobi_Burrowed( gentity_t *ent ) {
	return ent && ent->client && ent->s.number < MAX_CLIENTS && ent->client->shinobi.burrowEnd ? qtrue : qfalse;
}

qboolean Shinobi_Armored( gentity_t *ent ) {
	return ent && ent->client && ent->s.number < MAX_CLIENTS && ent->client->shinobi.armorEnd > level.time ? qtrue : qfalse;
}

/*
---------------------------------------------------------------------------
Burrow (held): under the ground. Out of sight and untouchable (the Byakugan
still sees you; an Earthquake throws you out), but a trail of dust follows
you. Let go, attack, run dry or run out of time: you burst out, launching
whoever stands right above.
---------------------------------------------------------------------------
*/
qboolean Shinobi_CastBurrow( gentity_t *ent ) {
	gclient_t *client = ent->client;
	vec3_t     up = { 0, 0, 1 };

	if ( client->shinobi.burrowEnd || SHINOBI_HAND_ANY( &client->ps ) || SHINOBI_HIDDEN( &client->ps ) )
		return qfalse;
	if ( client->shinobi.burrowNext > level.time ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		trap->SendServerCommand( ent - g_entities, va( "cp \"Burrow ready in %.1f s\"",
			( client->shinobi.burrowNext - level.time ) / 1000.0f ) );
		return qfalse;
	}
	if ( client->ps.groundEntityNum == ENTITYNUM_NONE ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		trap->SendServerCommand( ent - g_entities, "cp \"Burrow: stand on the ground\"" );
		return qfalse;
	}
	if ( client->ps.weapon == WP_SABER && client->ps.saberHolstered != 2 ) {
		client->ps.saberHolstered = 2; // a humming blade under the ground? no
	}
	client->shinobi.burrowEnd = level.time + SHINOBI_BURROW_MAX_MS;
	VectorSet( client->shinobi.burrowDustPos, 1e6f, 1e6f, 0 ); // the first patch right where you go down
	client->shinobi.burrowTick = level.time + SHINOBI_BURROW_TICK_MS;
	client->shinobi.burrowFx = level.time;
	client->ps.stats[STAT_SHINOBI] |= SHINOBI_STAT_BURROW;
	ent->r.contents = 0;
	ent->r.svFlags |= SVF_BROADCASTCLIENTS; // only to us and Byakugan eyes (Shinobi_PerkBroadcasts)
	ent->r.broadcastClients[0] = ent->r.broadcastClients[1] = 0;
	trap->LinkEntity( (sharedEntity_t *)ent );

	G_PlayEffectID( shinobiMedia.fxWallRise, client->ps.origin, up );
	G_SoundAtLoc( client->ps.origin, CHAN_AUTO, shinobiMedia.sndWallRise );
	return qtrue;
}

void Shinobi_BurrowEnd( gentity_t *ent, qboolean strike ) {
	gclient_t *client = ent->client;
	vec3_t     up = { 0, 0, 1 }, feet;
	int        i;

	if ( !client || !client->shinobi.burrowEnd )
		return;
	client->shinobi.burrowEnd = 0;
	client->shinobi.burrowNext = level.time + SHINOBI_BURROW_COOLDOWN;
	client->ps.stats[STAT_SHINOBI] &= ~SHINOBI_STAT_BURROW;
	ent->r.svFlags &= ~SVF_BROADCASTCLIENTS;
	if ( client->shinobi.chargeJutsu == SJ_BURROW )
		client->shinobi.chargeJutsu = SJ_NONE;
	Shinobi_RestoreSolid( ent );
	trap->LinkEntity( (sharedEntity_t *)ent );

	VectorCopy( client->ps.origin, feet );
	feet[2] += ent->r.mins[2];
	G_PlayEffectID( shinobiMedia.fxWallBreak, feet, up );
	if ( strike )
		G_PlayEffectID( shinobiMedia.fxRockMed, feet, up );
	G_SoundAtLoc( feet, CHAN_AUTO, shinobiMedia.sndWallBreak );
	if ( !strike || ent->health <= 0 )
		return;

	client->ps.velocity[2] = 280; // out of the ground
	client->ps.groundEntityNum = ENTITYNUM_NONE;
	for ( i = 0; i < level.num_entities; i++ ) {
		gentity_t *v = &g_entities[i];
		vec3_t     d;

		if ( v == ent || !v->inuse || !v->client || !v->takedamage || v->health <= 0 )
			continue;
		if ( v->s.number < MAX_CLIENTS && v->client->sess.sessionTeam == TEAM_SPECTATOR )
			continue;
		if ( v->shinobiCloneOwner == ent->s.number + 1 || ( level.gametype >= GT_TEAM && OnSameTeam( ent, v ) ) )
			continue;
		VectorSubtract( v->r.currentOrigin, client->ps.origin, d );
		if ( d[2] < -32 || d[2] > 96 )
			continue;
		d[2] = 0;
		if ( VectorLength( d ) > SHINOBI_BURROW_RADIUS )
			continue;
		shinobiHitElement = SE_EARTH;
		shinobiJutsuDamage = 1;
		G_Damage( v, ent, ent, up, v->r.currentOrigin, SHINOBI_BURROW_DAMAGE, DAMAGE_NO_ARMOR | DAMAGE_NO_KNOCKBACK, MOD_CRUSH );
		shinobiJutsuDamage = 0;
		shinobiHitElement = SE_NONE;
		if ( !v->inuse || v->health <= 0 || !v->client )
			continue;
		v->client->ps.velocity[2] = SHINOBI_BURROW_LAUNCH;
		v->client->ps.groundEntityNum = ENTITYNUM_NONE;
		v->client->ps.otherKiller = ent->s.number;
		v->client->ps.otherKillerTime = level.time + 5000;
		v->client->ps.otherKillerDebounceTime = level.time + 100;
		if ( !Shinobi_CCImmune( v ) ) {
			G_Knockdown( v );
			Shinobi_CCGrant( v, level.time + 1500 );
		}
	}
}

/*
---------------------------------------------------------------------------
Stone Skin: 8 s of stone over the body - 40% less damage, no knockdowns or
knockback, a little slower. Lightning cuts straight through it.
---------------------------------------------------------------------------
*/
qboolean Shinobi_CastArmor( gentity_t *ent ) {
	gclient_t *client = ent->client;
	vec3_t     up = { 0, 0, 1 };

	if ( client->shinobi.armorNext > level.time ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		trap->SendServerCommand( ent - g_entities, va( "cp \"Stone Skin ready in %i s\"",
			( client->shinobi.armorNext - level.time + 999 ) / 1000 ) );
		return qfalse;
	}
	client->shinobi.armorEnd = level.time + SHINOBI_ARMOR_MS;
	client->shinobi.armorNext = level.time + SHINOBI_ARMOR_COOLDOWN;
	client->shinobi.armorFx = level.time;
	G_PlayEffectID( shinobiMedia.fxWallRise, client->ps.origin, up );
	G_Sound( ent, CHAN_AUTO, shinobiMedia.sndWallRise );
	trap->SendServerCommand( ent - g_entities, "cp \"^3Stone Skin\"" );
	return qtrue;
}

// every client frame, before Pmove
void Shinobi_EarthThink( gentity_t *ent, usercmd_t *ucmd ) {
	gclient_t *client = ent->client;
	vec3_t     up = { 0, 0, 1 }, feet;

	VectorCopy( client->ps.origin, feet );
	feet[2] += ent->r.mins[2] + 2;

	// Stone Skin
	if ( client->shinobi.armorEnd && ( client->shinobi.armorEnd <= level.time || ent->health <= 0 ) )
		client->shinobi.armorEnd = 0;
	if ( client->shinobi.armorEnd ) {
		client->ps.stats[STAT_SHINOBI] |= SHINOBI_STAT_ARMOR;
		client->ps.eFlags |= SHINOBI_EF_STONE;
		if ( client->shinobi.armorFx <= level.time ) { // grit falling off the stone
			client->shinobi.armorFx = level.time + 400;
			G_PlayEffectID( shinobiMedia.fxQuakeDust, feet, up );
		}
	}
	else {
		client->ps.stats[STAT_SHINOBI] &= ~SHINOBI_STAT_ARMOR;
		client->ps.eFlags &= ~SHINOBI_EF_STONE;
	}

	// Burrow
	if ( !client->shinobi.burrowEnd ) {
		client->ps.stats[STAT_SHINOBI] &= ~SHINOBI_STAT_BURROW;
		return;
	}
	if ( ent->health <= 0 || client->sess.sessionTeam == TEAM_SPECTATOR ) {
		Shinobi_BurrowEnd( ent, qfalse );
		return;
	}
	if ( level.time >= client->shinobi.burrowEnd || ( ucmd->buttons & ( BUTTON_ATTACK | BUTTON_ALT_ATTACK ) ) ) {
		Shinobi_BurrowEnd( ent, qtrue );
		return;
	}
	client->ps.fd.forcePowerRegenDebounceTime = level.time + 500;
	while ( client->shinobi.burrowTick <= level.time ) {
		client->shinobi.burrowTick += SHINOBI_BURROW_TICK_MS;
		if ( client->ps.fd.forcePower < 1 ) {
			trap->SendServerCommand( ent - g_entities, "cp \"Out of chakra: you burst out\"" );
			Shinobi_BurrowEnd( ent, qtrue );
			return;
		}
		client->ps.fd.forcePower--;
	}
	client->ps.stats[STAT_SHINOBI] |= SHINOBI_STAT_BURROW;
	if ( ent->r.contents ) {
		ent->r.contents = 0;
		trap->LinkEntity( (sharedEntity_t *)ent );
	}
	// the trail: dust heaving up along the way, for everyone to see
	if ( client->shinobi.burrowFx <= level.time ) {
		client->shinobi.burrowFx = level.time + 110;
		G_PlayEffectID( shinobiMedia.fxQuakeDust, feet, up );
	}
	{ // ...and loose earth left on the floor behind (water on it makes mud)
		vec3_t d;
		VectorSubtract( feet, client->shinobi.burrowDustPos, d );
		d[2] = 0;
		if ( VectorLength( d ) >= SHINOBI_BURROW_DUST_STEP ) {
			VectorCopy( feet, client->shinobi.burrowDustPos );
			Shinobi_DustAt( feet, SHINOBI_BURROW_DUST_RADIUS, ent->s.number + 1 );
		}
	}
}
