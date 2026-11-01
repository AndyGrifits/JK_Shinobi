/*
===========================================================================
JK_Shinobi - ninja build on the server: read from the "shinobi_build"
userinfo key, validated, applied on (re)spawn, enforced when casting.
Stats and perks take effect here; the parsing lives in bg_shinobi.c.
===========================================================================
*/
#include "g_shinobi_local.h"

int shinobiJutsuDamage = 0; // set while a jutsu deals damage (not taijutsu)

void Shinobi_BuildUserinfo( gentity_t *ent, const char *userinfo ) {
	gclient_t     *client = ent->client;
	shinobiBuild_t next;
	const char    *str = Info_ValueForKey( userinfo, "shinobi_build" );

	Shinobi_BuildParse( str, &next );
	Shinobi_BuildFix( &next );
	if ( client->pers.connected == CON_CONNECTED && client->pers.shinobiBuildSet
		&& memcmp( &next, &client->pers.shinobiBuildNext, sizeof( next ) ) ) {
		trap->SendServerCommand( ent - g_entities, "cp \"^5Build saved^7 - it takes effect when you respawn\"" );
	}
	client->pers.shinobiBuildNext = next;
	client->pers.shinobiBuildSet = qtrue;

	// a userinfo change resets max health; keep the build's
	if ( client->pers.shinobiBuild.stats[SST_HEALTH] ) {
		client->pers.maxHealth = 100 + 15 * client->pers.shinobiBuild.stats[SST_HEALTH];
		client->ps.stats[STAT_MAX_HEALTH] = client->pers.maxHealth;
	}
}

void Shinobi_ApplyBuild( gentity_t *ent ) {
	gclient_t      *client = ent->client;
	shinobiBuild_t *b;
	char            str[256];

	if ( !client->pers.shinobiBuildSet ) {
		Shinobi_BuildDefault( &client->pers.shinobiBuildNext );
		client->pers.shinobiBuildSet = qtrue;
	}
	client->pers.shinobiBuild = client->pers.shinobiBuildNext;
	b = &client->pers.shinobiBuild;

	if ( client->sess.sessionTeam != TEAM_SPECTATOR && level.gametype != GT_SIEGE ) {
		client->pers.maxHealth = 100 + 15 * b->stats[SST_HEALTH];
		client->ps.stats[STAT_MAX_HEALTH] = client->pers.maxHealth;
		ent->health = client->ps.stats[STAT_HEALTH] = client->pers.maxHealth;
		client->ps.fd.forcePowerMax = 100 + 20 * b->stats[SST_CHAKRA];
		client->ps.fd.forcePower = client->ps.fd.forcePowerMax;
	}

	Shinobi_BuildString( b, str, sizeof( str ) );
	trap->SendServerCommand( ent - g_entities, va( "sbuild %s", str ) );
}

// may this client cast that jutsu? (cheats: everything, for tests)
qboolean Shinobi_HasJutsu( gentity_t *ent, int jutsu ) {
	if ( sv_cheats.integer || ent->s.number >= MAX_CLIENTS )
		return qtrue;
	return Shinobi_BuildHas( &ent->client->pers.shinobiBuild, jutsu );
}

int Shinobi_CastStat( gentity_t *ent ) {
	return ent->client ? ent->client->pers.shinobiBuild.stats[SST_CAST] : 0;
}

int Shinobi_BuildDamage( gentity_t *targ, gentity_t *attacker, int damage, int mod ) {
	gclient_t *ac;

	if ( !attacker || !attacker->client || attacker->s.number >= MAX_CLIENTS || shinobiJutsuDamage )
		return damage;
	ac = attacker->client;
	if ( mod != MOD_MELEE && mod != MOD_SABER )
		return damage;

	// Taijutsu: fists, kicks and blades hit harder
	damage = (int)( damage * ( 1.0f + 0.1f * ac->pers.shinobiBuild.stats[SST_TAIJUTSU] ) + 0.5f );

	// Gentle Fist: bare-handed hits burn the target's chakra
	if ( mod == MOD_MELEE && ac->pers.shinobiBuild.perk == SP_GENTLE_FIST && targ && targ->client && targ != attacker ) {
		targ->client->ps.fd.forcePower -= damage;
		if ( targ->client->ps.fd.forcePower < 0 )
			targ->client->ps.fd.forcePower = 0;
	}
	return damage;
}

/*
Curse Seal: awakens when the chakra runs dry and stays awake until it has
eaten your health down to 10. While awake chakra pours back 2.5x faster;
dark chakra licks around the body.
*/
float Shinobi_RegenScale( gentity_t *ent ) {
	float f;
	if ( !ent->client || ent->s.number >= MAX_CLIENTS )
		return 1.0f;
	f = 1.0f - 0.12f * ent->client->pers.shinobiBuild.stats[SST_REGEN];
	if ( ent->client->shinobi.curseAwake )
		f *= 0.4f;
	return f;
}

void Shinobi_PerkThink( gentity_t *ent ) {
	gclient_t *client = ent->client;
	const qboolean hasSeal = client->pers.shinobiBuild.perk == SP_CURSE_SEAL && !client->pers.shinobiCurseOff;
	vec3_t     up = { 0, 0, 1 };

	if ( !hasSeal || ent->health <= 0 ) {
		client->shinobi.curseAwake = qfalse;
	}
	else if ( !client->shinobi.curseAwake && client->ps.fd.forcePower <= 1 ) {
		client->shinobi.curseAwake = qtrue;
		client->shinobi.curseTick = level.time + 300;
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndCurse );
		G_PlayEffectID( shinobiMedia.fxCurse, ent->r.currentOrigin, up );
		trap->SendServerCommand( ent - g_entities, "cp \"^6Curse Seal awakened\"" );
	}
	else if ( client->shinobi.curseAwake && ent->health <= 10 ) {
		client->shinobi.curseAwake = qfalse; // it has taken all it can: goes back to sleep
		trap->SendServerCommand( ent - g_entities, "cp \"^6Curse Seal fades\"" );
	}

	if ( client->shinobi.curseAwake && client->shinobi.curseTick <= level.time ) {
		client->shinobi.curseTick = level.time + 300;
		if ( ent->health > 10 ) {
			ent->health--;
			client->ps.stats[STAT_HEALTH] = ent->health;
		}
		G_PlayEffectID( shinobiMedia.fxCurse, ent->r.currentOrigin, up );
	}

	if ( client->shinobi.curseAwake )
		client->ps.stats[STAT_SHINOBI] |= SHINOBI_STAT_CURSE;
	else
		client->ps.stats[STAT_SHINOBI] &= ~SHINOBI_STAT_CURSE;
}

void Cmd_CurseSeal_f( gentity_t *ent ) {
	gclient_t *client = ent->client;

	if ( client->pers.shinobiBuild.perk != SP_CURSE_SEAL ) {
		trap->SendServerCommand( ent - g_entities, "print \"You don't have the Curse Seal.\n\"" );
		return;
	}
	client->pers.shinobiCurseOff = !client->pers.shinobiCurseOff;
	client->shinobi.curseAwake = qfalse;
	trap->SendServerCommand( ent - g_entities, va( "cp \"Curse Seal %s\"", client->pers.shinobiCurseOff ? "^2sealed (off)" : "^1released (on)" ) );
}
