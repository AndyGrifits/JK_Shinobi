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
		client->ps.stats[STAT_ARMOR] = SHINOBI_GUARD_MAX; // the guard (block)
		client->shinobi.staggerEnd = 0;
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

	if ( attacker && attacker->shinobiCloneEarth && ( mod == MOD_MELEE || mod == MOD_SABER ) && !shinobiJutsuDamage )
		return damage * SHINOBI_EARTHCLONE_HIT / 100; // an earth clone's fists are stone
	if ( !attacker || !attacker->client || attacker->s.number >= MAX_CLIENTS || shinobiJutsuDamage )
		return damage;
	ac = attacker->client;
	if ( mod != MOD_MELEE && mod != MOD_SABER )
		return damage;

	// Taijutsu: fists, kicks and blades hit harder (Inner Gates: much harder, then weaker)
	{
		float f = 1.0f + 0.1f * ac->pers.shinobiBuild.stats[SST_TAIJUTSU];
		if ( ac->shinobi.gatesEnd > level.time )
			f *= SHINOBI_GATES_TAIJUTSU;
		else if ( ac->shinobi.gatesTiredEnd > level.time )
			f *= SHINOBI_GATES_TIRED_TAIJUTSU;
		damage = (int)( damage * f + 0.5f );
	}
	// Shift + attack: the hit carries your chakra nature
	damage = Shinobi_InfuseHit( targ, attacker, damage );

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
		f *= 0.2f;
	if ( ent->client->shinobi.gatesTiredEnd > level.time && ent->client->shinobi.gatesEnd <= level.time )
		f = 1000.0f; // exhausted after the Gates: no chakra comes back
	return f;
}

static void Shinobi_CurseThink( gentity_t *ent ) {
	gclient_t *client = ent->client;
	const qboolean hasSeal = client->pers.shinobiBuild.perk == SP_CURSE_SEAL && !client->pers.shinobiCurseOff;
	vec3_t     up = { 0, 0, 1 };

	client->ps.stats[STAT_SHINOBI] &= ~SHINOBI_STAT_CURSEWEAK;
	if ( !hasSeal || ent->health <= 0 ) {
		client->shinobi.curseAwake = qfalse;
	}
	else if ( !client->shinobi.curseAwake && client->ps.fd.forcePower <= 1 && ent->health <= 10 ) {
		client->ps.stats[STAT_SHINOBI] |= SHINOBI_STAT_CURSEWEAK; // it stirs, but there is nothing left to feed on
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

/*
===========================================================================
Sharingan / Byakugan / Inner Gates
===========================================================================
*/
static void Shinobi_EyesOff( gentity_t *ent, const char *why ) {
	gclient_t *client = ent->client;

	if ( !client->shinobi.eyes )
		return;
	client->shinobi.eyes = 0;
	if ( why && ent->health > 0 )
		trap->SendServerCommand( ent - g_entities, va( "cp \"%s\"", why ) );
	trap->SendServerCommand( ent - g_entities, "sinfo" ); // forget what we saw
}

static void Shinobi_EyesOn( gentity_t *ent, int perk ) {
	gclient_t *client = ent->client;

	if ( client->ps.fd.forcePower < SHINOBI_DOJUTSU_COST ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		trap->SendServerCommand( ent - g_entities, "cp \"Not enough chakra\"" );
		return;
	}
	client->ps.fd.forcePower -= SHINOBI_DOJUTSU_COST;
	client->shinobi.eyes = perk;
	client->shinobi.eyesDrain = level.time + ( perk == SP_BYAKUGAN ? SHINOBI_BYAKUGAN_DRAIN_MS : SHINOBI_SHARINGAN_DRAIN_MS );
	client->shinobi.eyesInfo = level.time;
	if ( perk == SP_BYAKUGAN ) {
		// the veins bulge with an audible rush: everyone around hears it
		gentity_t *te = G_TempEntity( ent->r.currentOrigin, EV_GLOBAL_SOUND );
		te->s.eventParm = shinobiMedia.sndEyes;
		te->r.svFlags |= SVF_BROADCAST;
		trap->SendServerCommand( -1, va( "print \"%s^7 activates the ^7Byakugan^7!\n\"", client->pers.netname ) );
		trap->SendServerCommand( ent - g_entities, "cp \"^7Byakugan\"" );
	}
	else {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndEyes );
		trap->SendServerCommand( ent - g_entities, "cp \"^1Sharingan\"" );
	}
}

static void Shinobi_OpenGates( gentity_t *ent ) {
	gclient_t *client = ent->client;
	vec3_t     up = { 0, 0, 1 };

	if ( client->shinobi.gatesEnd > level.time ) {
		trap->SendServerCommand( ent - g_entities, "cp \"The Gates are already open\"" );
		return;
	}
	if ( client->shinobi.gatesNext > level.time ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		trap->SendServerCommand( ent - g_entities, va( "cp \"Inner Gates ready in %i s\"",
			( client->shinobi.gatesNext - level.time + 999 ) / 1000 ) );
		return;
	}
	client->shinobi.gatesEnd = level.time + SHINOBI_GATES_MS;
	client->shinobi.gatesTiredEnd = client->shinobi.gatesEnd + SHINOBI_GATES_TIRED_MS;
	client->shinobi.gatesNext = level.time + SHINOBI_GATES_COOLDOWN;
	client->shinobi.gatesFx = level.time;
	G_Sound( ent, CHAN_AUTO, shinobiMedia.sndGates );
	G_PlayEffectID( shinobiMedia.fxGates, ent->r.currentOrigin, up );
	trap->SendServerCommand( ent - g_entities, "cp \"^2Inner Gates open!\"" );
}

void Cmd_Perk_f( gentity_t *ent ) {
	gclient_t *client = ent->client;
	const int  perk = client->pers.shinobiBuild.perk;

	if ( ( client->shinobi.shadowBy || client->shinobi.prisonCaster ) && !client->shinobi.eyes ) {
		trap->SendServerCommand( ent - g_entities, "cp \"You can't - only Kawarimi (X) works now\"" );
		return; // bound or imprisoned (switching the eyes off still works)
	}
	switch ( perk ) {
	case SP_SHARINGAN:
	case SP_BYAKUGAN:
		if ( client->shinobi.eyes )
			Shinobi_EyesOff( ent, perk == SP_BYAKUGAN ? "Byakugan off" : "Sharingan off" );
		else
			Shinobi_EyesOn( ent, perk );
		break;
	case SP_GATES:
		Shinobi_OpenGates( ent );
		break;
	case SP_CURSE_SEAL:
		Cmd_CurseSeal_f( ent );
		break;
	case SP_GENTLE_FIST:
		trap->SendServerCommand( ent - g_entities, "cp \"Gentle Fist works by itself:\nyour bare hands burn chakra\"" );
		break;
	default:
		trap->SendServerCommand( ent - g_entities, "cp \"No special ability - pick one in the build menu (B)\"" );
		break;
	}
}

// what the eyes may look at: other living players and the shinobi clones
static qboolean Shinobi_EyeTarget( gentity_t *viewer, gentity_t *e ) {
	if ( e == viewer || !e->inuse || !e->client || e->health <= 0 )
		return qfalse;
	if ( e->s.number < MAX_CLIENTS )
		return e->client->pers.connected == CON_CONNECTED && e->client->sess.sessionTeam != TEAM_SPECTATOR;
	return e->shinobiCloneOwner && Shinobi_CloneOwner( e ) != viewer;
}

// tell the eye user what he sees: "sinfo <num> <hp> <maxhp> <chakra> <chakramax> <jutsu> <flags> ..."
static void Shinobi_SendEyeInfo( gentity_t *ent ) {
	gclient_t *client = ent->client;
	const int  eyes = client->shinobi.eyes;
	char       buf[MAX_STRING_CHARS - 64];
	int        i, len, n = 0;
	vec3_t     eye;

	Q_strncpyz( buf, "sinfo", sizeof( buf ) );
	len = strlen( buf );
	VectorCopy( client->ps.origin, eye );
	eye[2] += client->ps.viewheight;

	for ( i = 0; i < level.num_entities && n < 32; i++ ) {
		gentity_t *e = &g_entities[i];
		gentity_t *who;
		const qboolean clone = ( i >= MAX_CLIENTS );
		qboolean hidden;
		int   hp, maxhp, chakra = 0, chakraMax = 0, jutsu = 0, flags = 0;
		float dist;
		char  entry[64];

		if ( !Shinobi_EyeTarget( ent, e ) )
			continue;
		hidden = !clone && SHINOBI_UNSEEN( &e->client->ps ); // (only now: empty slots have no client)
		dist = Distance( client->ps.origin, e->r.currentOrigin );

		if ( eyes == SP_SHARINGAN ) {
			if ( hidden && ( e->client->ps.stats[STAT_SHINOBI] & SHINOBI_STAT_BURROW ) )
				continue; // under the ground: only the Byakugan sees that far
			if ( hidden ? dist > SHINOBI_SHARINGAN_HIDDEN_RANGE
				: ( dist > SHINOBI_SHARINGAN_RANGE || !trap->InPVS( eye, e->r.currentOrigin ) ) )
				continue;
			flags = SHINOBI_SEEN_HEALTH | ( hidden ? SHINOBI_SEEN_HIDDEN : 0 );
		}
		else {
			if ( dist > SHINOBI_BYAKUGAN_RANGE )
				continue;
			flags = SHINOBI_SEEN_CHAKRA | ( clone ? SHINOBI_SEEN_CLONE : 0 ) | ( hidden ? SHINOBI_SEEN_HIDDEN : 0 );
		}

		// a clone is a perfect copy to the Sharingan: it shows its owner's health
		who = clone ? Shinobi_CloneOwner( e ) : e;
		if ( !who || !who->client )
			who = e;
		hp = who->health;
		maxhp = who->client->ps.stats[STAT_MAX_HEALTH];
		if ( !clone ) {
			chakra = e->client->ps.fd.forcePower;
			chakraMax = e->client->ps.fd.forcePowerMax;
			jutsu = e->client->shinobi.chargeJutsu;
			if ( jutsu == SJ_SHUNSHIN )
				jutsu = 0;
		}
		Com_sprintf( entry, sizeof( entry ), " %i %i %i %i %i %i %i", i, hp, maxhp, chakra, chakraMax, jutsu, flags );
		if ( len + (int)strlen( entry ) >= (int)sizeof( buf ) )
			break;
		Q_strcat( buf, sizeof( buf ), entry );
		len += strlen( entry );
		n++;
	}
	trap->SendServerCommand( ent - g_entities, buf );
}

/*
Who receives this entity besides its PVS (called after G_UpdateClientBroadcasts
for players, every frame for clones): Byakugan eyes in range always do; a
Kawarimi-hidden player goes only to himself and Sharingan eyes in range.
*/
void Shinobi_PerkBroadcasts( gentity_t *self ) {
	const qboolean kawarimi = self->s.number < MAX_CLIENTS && self->client && SHINOBI_HIDDEN( &self->client->ps );
	const qboolean hidden = self->s.number < MAX_CLIENTS && self->client && SHINOBI_UNSEEN( &self->client->ps );
	int i;

	if ( hidden ) {
		self->r.svFlags |= SVF_BROADCASTCLIENTS;
		self->r.broadcastClients[0] = self->r.broadcastClients[1] = 0;
	}
	else if ( self->s.number >= MAX_CLIENTS ) {
		self->r.broadcastClients[0] = self->r.broadcastClients[1] = 0;
	}

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		gentity_t *other = &g_entities[i];
		float      range;

		if ( other == self || !other->inuse || !other->client || !other->client->shinobi.eyes
			|| other->health <= 0 || other->client->pers.connected != CON_CONNECTED )
			continue;
		if ( other->client->shinobi.eyes == SP_BYAKUGAN )
			range = SHINOBI_BYAKUGAN_RANGE; // through walls, and through Kawarimi too
		else
			range = kawarimi ? SHINOBI_SHARINGAN_HIDDEN_RANGE : 0; // (not under the ground)
		if ( range > 0 && DistanceSquared( other->client->ps.origin, self->r.currentOrigin ) < range * range )
			Q_AddToBitflags( self->r.broadcastClients, i, 32 );
	}
}

void Shinobi_PerkThink( gentity_t *ent ) {
	gclient_t *client = ent->client;
	const int  perk = client->pers.shinobiBuild.perk;
	const qboolean alive = ent->health > 0 && client->sess.sessionTeam != TEAM_SPECTATOR;
	vec3_t     up = { 0, 0, 1 };

	Shinobi_CurseThink( ent );

	// Sky Step (passive): cheap jumps and wall runs (read by pmove)
	if ( Shinobi_BuildHas( &client->pers.shinobiBuild, SJ_SKYSTEP ) )
		client->ps.stats[STAT_SHINOBI] |= SHINOBI_STAT_SKYSTEP;
	else
		client->ps.stats[STAT_SHINOBI] &= ~SHINOBI_STAT_SKYSTEP;

	// --- Sharingan / Byakugan ---
	if ( client->shinobi.eyes && ( !alive || client->shinobi.eyes != perk ) )
		Shinobi_EyesOff( ent, NULL );
	if ( client->shinobi.eyes ) {
		client->ps.fd.forcePowerRegenDebounceTime = level.time + 500; // open eyes: no natural regen
		if ( client->shinobi.eyesDrain <= level.time ) {
			client->shinobi.eyesDrain = level.time + ( client->shinobi.eyes == SP_BYAKUGAN ? SHINOBI_BYAKUGAN_DRAIN_MS : SHINOBI_SHARINGAN_DRAIN_MS );
			if ( client->ps.fd.forcePower < 1 )
				Shinobi_EyesOff( ent, "Out of chakra - your eyes go dark" );
			else
				client->ps.fd.forcePower--;
		}
		if ( client->shinobi.eyes && client->shinobi.eyesInfo <= level.time ) {
			client->shinobi.eyesInfo = level.time + SHINOBI_PERK_INFO_MS;
			Shinobi_SendEyeInfo( ent );
		}
	}
	client->ps.eFlags2 &= ~( SHINOBI_EF2_SHARINGAN | SHINOBI_EF2_BYAKUGAN );
	if ( client->shinobi.eyes == SP_SHARINGAN )
		client->ps.eFlags2 |= SHINOBI_EF2_SHARINGAN;
	else if ( client->shinobi.eyes == SP_BYAKUGAN )
		client->ps.eFlags2 |= SHINOBI_EF2_BYAKUGAN;

	// --- Inner Gates ---
	if ( !alive || perk != SP_GATES )
		client->shinobi.gatesEnd = client->shinobi.gatesTiredEnd = 0;
	client->ps.stats[STAT_SHINOBI] &= ~( SHINOBI_STAT_GATES | SHINOBI_STAT_TIRED );
	if ( client->shinobi.gatesEnd > level.time ) {
		client->ps.stats[STAT_SHINOBI] |= SHINOBI_STAT_GATES;
		if ( client->shinobi.gatesFx <= level.time ) {
			client->shinobi.gatesFx = level.time + 250;
			G_PlayEffectID( shinobiMedia.fxGates, ent->r.currentOrigin, up );
		}
	}
	else if ( client->shinobi.gatesTiredEnd > level.time ) {
		if ( client->shinobi.gatesTiredEnd - level.time > SHINOBI_GATES_TIRED_MS - 100 && client->shinobi.gatesFx < client->shinobi.gatesEnd + 250 ) {
			client->shinobi.gatesFx = client->shinobi.gatesEnd + 250;
			trap->SendServerCommand( ent - g_entities, "cp \"^3The Gates close - you are exhausted\"" );
		}
		client->ps.stats[STAT_SHINOBI] |= SHINOBI_STAT_TIRED;
	}
}
