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

	// weapon style: bots without one get one of each, by their slot
	{
		const char *w = Info_ValueForKey( userinfo, "shinobi_weapon" );
		int         style;
		if ( !w[0] && ( ent->r.svFlags & SVF_BOT ) )
			style = Shinobi_BotWeaponStyle( ent, NULL );
		else
			style = Shinobi_WeaponStyleParse( w );
		if ( client->pers.connected == CON_CONNECTED && client->pers.shinobiWeaponSet && style != client->pers.shinobiWeaponNext )
			trap->SendServerCommand( ent - g_entities, va( "cp \"^5%s^7 - from your next spawn\"", shinobiWeaponStyleTitles[style] ) );
		client->pers.shinobiWeaponNext = style;
		if ( !client->pers.shinobiWeaponSet )
			client->pers.shinobiWeapon = style; // the first spawn gives it anyway
		client->pers.shinobiWeaponSet = qtrue;
	}

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

	client->pers.shinobiWeapon = client->pers.shinobiWeaponNext;
	Shinobi_BotPrepare( ent ); // bots: a random build each life
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
		client->ps.ammo[SHINOBI_AMMO_EXKUNAI] = client->ps.ammo[SHINOBI_AMMO_TAG] = client->ps.ammo[SHINOBI_AMMO_SHURIKEN] = client->ps.ammo[SHINOBI_AMMO_STAR] = 0;
		client->shinobi.staggerEnd = 0;
	}

	Shinobi_BotForcePowers( ent );
	Shinobi_BuildString( b, str, sizeof( str ) );
	trap->SendServerCommand( ent - g_entities, va( "sbuild %s", str ) );
}

// may this client cast that jutsu? (cheats: everything, for tests)
qboolean Shinobi_HasJutsu( gentity_t *ent, int jutsu ) {
	if ( sv_cheats.integer || ent->s.number >= MAX_CLIENTS )
		return qtrue;
	if ( jutsu > SJ_NONE && ent->client->shinobi.copyJutsu == jutsu )
		return qtrue; // in the Copy Sharingan's slot
	return Shinobi_BuildHas( &ent->client->pers.shinobiBuild, jutsu );
}

int Shinobi_CastStat( gentity_t *ent ) {
	return ent->client ? ent->client->pers.shinobiBuild.stats[SST_CAST] : 0;
}

int Shinobi_BuildDamage( gentity_t *targ, gentity_t *attacker, int damage, int mod ) {
	gclient_t *ac;

	if ( attacker && attacker->shinobiCloneOwner && ( mod == MOD_MELEE || mod == MOD_SABER ) && !shinobiJutsuDamage ) {
		// any clone hits with a fraction of its owner's strength (taijutsu included)
		const gentity_t *owner = &g_entities[attacker->shinobiCloneOwner - 1];
		float f = SHINOBI_CLONE_MELEE;
		if ( owner->inuse && owner->client )
			f *= 1.0f + 0.1f * owner->client->pers.shinobiBuild.stats[SST_TAIJUTSU];
		damage = (int)( damage * f + 0.5f );
		return damage < 1 ? 1 : damage;
	}
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
	// Lightning Edge: the hit jolts harder (3x through stone)
	damage = Shinobi_InfuseHit( targ, attacker, damage );

	// Gentle Fist: with the Byakugan open, bare-handed hits burn the target's chakra
	if ( mod == MOD_MELEE && ac->shinobi.eyes == SP_BYAKUGAN && targ && targ->client && targ != attacker ) {
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
	if ( ent->client->ps.fd.forcePower * 100 > ent->client->ps.fd.forcePowerMax * SHINOBI_REGEN_FULL_UPTO )
		f *= SHINOBI_REGEN_SLOW_ABOVE; // above 30%: slow - the rest is earned fighting
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
static int Shinobi_EyesDrainMs( int eyes ) {
	if ( eyes == SP_BYAKUGAN )
		return SHINOBI_BYAKUGAN_DRAIN_MS;
	return eyes == SP_COPYEYE ? SHINOBI_COPYEYE_DRAIN_MS : SHINOBI_SHARINGAN_DRAIN_MS;
}

// can 'viewer' see the spot 'at' (an entity's middle; head too): no walls
static qboolean Shinobi_EyeSees( gentity_t *viewer, gentity_t *e ) {
	trace_t tr;
	vec3_t  eye, head;

	VectorCopy( viewer->client->ps.origin, eye );
	eye[2] += viewer->client->ps.viewheight;
	VectorCopy( e->r.currentOrigin, head );
	head[2] += e->r.maxs[2] - 8;
	trap->Trace( &tr, eye, NULL, NULL, head, viewer->s.number, MASK_SOLID, qfalse, 0, 0 );
	if ( tr.fraction >= 1.0f )
		return qtrue;
	trap->Trace( &tr, eye, NULL, NULL, e->r.currentOrigin, viewer->s.number, MASK_SOLID, qfalse, 0, 0 );
	return tr.fraction >= 1.0f;
}

/*
Copy Sharingan: everyone with it open who sees 'caster' cast 'jutsu' (in range, in front of
him, no walls, neither of them in a Hidden Mist) takes it into his copy slot.
*/
void Shinobi_CopyWitness( gentity_t *caster, int jutsu ) {
	int i;

	if ( !Shinobi_Copyable( jutsu ) || !caster || !caster->inuse )
		return;
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		gentity_t *v = &g_entities[i];
		gclient_t *c = v->client;
		vec3_t     eye, fwd, d;
		float      dist;

		if ( v == caster || !v->inuse || !c || v->health <= 0 || c->shinobi.eyes != SP_COPYEYE
			|| c->pers.connected != CON_CONNECTED )
			continue;
		if ( caster->s.number >= MAX_CLIENTS && caster->shinobiCloneOwner && Shinobi_CloneOwner( caster ) == v )
			continue; // his own clone
		if ( c->shinobi.copyJutsu == jutsu || Shinobi_BuildHas( &c->pers.shinobiBuild, jutsu ) )
			continue; // nothing new
		if ( c->shinobi.mistEnd > level.time || ( caster->client && caster->client->shinobi.mistEnd > level.time ) )
			continue;
		VectorCopy( c->ps.origin, eye );
		eye[2] += c->ps.viewheight;
		VectorSubtract( caster->r.currentOrigin, eye, d );
		dist = VectorNormalize( d );
		if ( dist > SHINOBI_COPY_RANGE )
			continue;
		AngleVectors( c->ps.viewangles, fwd, NULL, NULL );
		if ( dist > 64 && DotProduct( fwd, d ) < SHINOBI_COPY_FOV )
			continue; // not looking that way
		if ( !Shinobi_EyeSees( v, caster ) )
			continue;
		c->shinobi.copyJutsu = jutsu;
		trap->SendServerCommand( i, va( "scopy %i", jutsu ) );
		trap->SendServerCommand( i, va( "cp \"^1Copied:^7 %s\"", shinobiJutsuTitles[jutsu] ) );
		G_Sound( v, CHAN_AUTO, shinobiMedia.sndEyes );
		Shinobi_Debug( "%i copies %s from %i", i, shinobiJutsuTitles[jutsu], caster->s.number );
	}
}

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
	client->shinobi.eyesDrain = level.time + Shinobi_EyesDrainMs( perk );
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
		trap->SendServerCommand( ent - g_entities, perk == SP_COPYEYE ? "cp \"^1Copy Sharingan\"" : "cp \"^1Sharingan\"" );
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
	case SP_COPYEYE:
		if ( client->shinobi.eyes )
			Shinobi_EyesOff( ent, perk == SP_BYAKUGAN ? "Byakugan off" : perk == SP_COPYEYE ? "Copy Sharingan off" : "Sharingan off" );
		else
			Shinobi_EyesOn( ent, perk );
		break;
	case SP_GATES:
		Shinobi_OpenGates( ent );
		break;
	case SP_CURSE_SEAL:
		Cmd_CurseSeal_f( ent );
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

		if ( eyes == SP_COPYEYE ) { // only the jutsu people in plain sight are charging
			if ( clone || hidden || dist > SHINOBI_SHARINGAN_RANGE || !Shinobi_EyeSees( ent, e ) )
				continue;
			jutsu = e->client->shinobi.chargeJutsu;
			if ( jutsu == SJ_SHUNSHIN )
				jutsu = 0;
			Com_sprintf( entry, sizeof( entry ), " %i 0 0 0 0 %i %i", i, jutsu, SHINOBI_SEEN_JUTSU );
			if ( len + (int)strlen( entry ) >= (int)sizeof( buf ) )
				break;
			Q_strcat( buf, sizeof( buf ), entry );
			len += strlen( entry );
			n++;
			continue;
		}
		if ( eyes == SP_SHARINGAN ) {
			if ( hidden && ( e->client->ps.stats[STAT_SHINOBI] & SHINOBI_STAT_BURROW ) )
				continue; // under the ground: only the Byakugan sees that far
			if ( hidden ? dist > SHINOBI_SHARINGAN_HIDDEN_RANGE : dist > SHINOBI_SHARINGAN_RANGE )
				continue;
			{ // only who the eye actually sees: a clear line to his head or his middle (no walls)
				trace_t tr;
				vec3_t  head;
				VectorCopy( e->r.currentOrigin, head );
				head[2] += e->r.maxs[2] - 8;
				trap->Trace( &tr, eye, NULL, NULL, head, ent->s.number, MASK_SOLID, qfalse, 0, 0 );
				if ( tr.fraction < 1.0f ) {
					trap->Trace( &tr, eye, NULL, NULL, e->r.currentOrigin, ent->s.number, MASK_SOLID, qfalse, 0, 0 );
					if ( tr.fraction < 1.0f )
						continue;
				}
			}
			flags = SHINOBI_SEEN_HEALTH | ( clone ? SHINOBI_SEEN_CLONE : 0 ) | ( hidden ? SHINOBI_SEEN_HIDDEN : 0 ); // (it sees through clones)
		}
		else {
			if ( dist > SHINOBI_BYAKUGAN_RANGE )
				continue;
			flags = SHINOBI_SEEN_CHAKRA | ( clone ? SHINOBI_SEEN_CLONE : 0 ) | ( hidden ? SHINOBI_SEEN_HIDDEN : 0 );
		}

		// (a clone's numbers are its owner's; the eyes just mark it "clone")
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
		else if ( other->client->shinobi.eyes == SP_SHARINGAN )
			range = kawarimi ? SHINOBI_SHARINGAN_HIDDEN_RANGE : 0; // (not under the ground)
		else
			range = 0; // the Copy Sharingan doesn't find the hidden
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
			client->shinobi.eyesDrain = level.time + Shinobi_EyesDrainMs( client->shinobi.eyes );
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
	if ( client->shinobi.eyes == SP_SHARINGAN || client->shinobi.eyes == SP_COPYEYE ) // (the same red eyes; the
		client->ps.eFlags2 |= SHINOBI_EF2_SHARINGAN;                                    // owner's cgame knows which from its build)
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

/*
"shinobi_dps 1": every attacker's damage in windows of 5 s (a new window also starts
when he switches between fists, blade and jutsu) - to compare fists with blades
*/
static void Shinobi_DpsFlush( gentity_t *attacker ) {
	gclient_t *c = attacker->client;
	static const char * const kinds[3] = { "fists", "blade", "jutsu/other" };
	const float secs = ( c->shinobi.dpsLast - c->shinobi.dpsStart ) / 1000.0f;

	if ( c->shinobi.dpsHits > 1 && secs > 0.2f )
		G_LogPrintf( "[dps] %s, %s: %i damage, %i hits in %.1f s -> %.1f per second (%.1f per hit)\n",
			c->pers.netname, kinds[c->shinobi.dpsKind], c->shinobi.dpsTotal, c->shinobi.dpsHits, secs,
			// (first to last hit spans hits-1 gaps)
			c->shinobi.dpsTotal * ( c->shinobi.dpsHits - 1.0f ) / c->shinobi.dpsHits / secs,
			(float)c->shinobi.dpsTotal / c->shinobi.dpsHits );
	c->shinobi.dpsHits = c->shinobi.dpsTotal = 0;
}

// chakra earned fighting (scaled by the Chakra regen stat, doubled by an awake Curse Seal)
void Shinobi_GainChakra( gentity_t *ent, float amount, qboolean show ) {
	gclient_t *c;
	int        whole;

	if ( !ent || !ent->client || ent->s.number >= MAX_CLIENTS || ent->health <= 0 || amount <= 0 )
		return;
	c = ent->client;
	amount *= 1.0f + 0.1f * c->pers.shinobiBuild.stats[SST_REGEN];
	if ( c->shinobi.curseAwake )
		amount *= 2.0f;
	c->shinobi.chakraFrac += amount;
	whole = (int)c->shinobi.chakraFrac;
	if ( whole <= 0 )
		return;
	c->shinobi.chakraFrac -= whole;
	if ( c->ps.fd.forcePower >= c->ps.fd.forcePowerMax )
		return;
	c->ps.fd.forcePower += whole;
	if ( c->ps.fd.forcePower > c->ps.fd.forcePowerMax )
		c->ps.fd.forcePower = c->ps.fd.forcePowerMax;
	c->shinobi.chakraShow += whole; // shown to him, a few gains summed (Shinobi_ChakraTextThink)
	shinobiTest.chakraGained[ent->s.number] += whole;
	if ( show )
		c->shinobi.chakraShowNext = 0; // a bonus: at once
	Shinobi_ChakraTextThink( ent );
}

// "+N chakra" over the one who earned it - only to him, at most twice a second
void Shinobi_ChakraTextThink( gentity_t *ent ) {
	gclient_t *c = ent->client;
	if ( !c || c->shinobi.chakraShow <= 0 || level.time < c->shinobi.chakraShowNext || !shinobi_hitnumbers.integer )
		return;
	if ( !( ent->r.svFlags & SVF_BOT ) )
		trap->SendServerCommand( ent - g_entities, va( "stx %i %i %i", ent->s.number, STX_CHAKRA, c->shinobi.chakraShow ) );
	if ( ent->s.number < MAX_CLIENTS )
		shinobiTest.chakraTexts[ent->s.number]++;
	c->shinobi.chakraShow = 0;
	c->shinobi.chakraShowNext = level.time + 500;
}

// floating combat text over a target, for everyone who can see it (shinobi_hitnumbers)
void Shinobi_HitText( gentity_t *targ, int kind, int value ) {
	if ( !shinobi_hitnumbers.integer || !targ || !targ->inuse || kind < 0 || kind >= STX_COUNT )
		return;
	trap->SendServerCommand( -1, va( "stx %i %i %i", targ->s.number, kind, value ) );
}

void Shinobi_DpsLog( gentity_t *targ, gentity_t *attacker, int take, int mod ) {
	gclient_t *c;
	int        kind;

	if ( take > 0 && targ ) { // (the autotest reads who hurt whom with what)
		shinobiTestDamage_t *d = &shinobiTest.dmg[shinobiTest.dmgNext];
		shinobiTest.dmgNext = ( shinobiTest.dmgNext + 1 ) % SHINOBI_TEST_DMGLOG;
		d->time = level.time;
		d->targ = targ->s.number;
		d->attacker = attacker ? attacker->s.number : ENTITYNUM_WORLD;
		d->take = take;
		d->mod = mod;
		Q_strncpyz( d->inflictor, shinobiTestInflictor && shinobiTestInflictor->classname ? shinobiTestInflictor->classname : "", sizeof( d->inflictor ) );
	}

	if ( take > 0 && targ && targ->client && attacker && attacker->client && targ != attacker ) {
		Shinobi_HitText( targ, STX_DAMAGE, take ); // the damage number over him
		// the fight feeds chakra: to the one hitting, and a little to the one hit
		Shinobi_GainChakra( attacker, take * ( ( ( mod == MOD_MELEE || mod == MOD_SABER ) && !shinobiJutsuDamage )
			? SHINOBI_GAIN_HIT_PCT : SHINOBI_GAIN_JUTSU_PCT ) / 100.0f, qfalse );
		if ( mod != MOD_FALLING )
			Shinobi_GainChakra( targ, take * SHINOBI_GAIN_HURT_PCT / 100.0f, qfalse );
	}

	if ( take <= 0 || !attacker || !attacker->client || attacker->s.number >= MAX_CLIENTS || targ == attacker
		|| !trap->Cvar_VariableIntegerValue( "shinobi_dps" ) )
		return;
	c = attacker->client;
	if ( ( mod == MOD_MELEE || mod == MOD_SABER ) && !shinobiJutsuDamage )
		kind = c->ps.weapon == WP_SABER ? 1 : 0;
	else
		kind = 2;
	if ( c->shinobi.dpsHits && ( kind != c->shinobi.dpsKind || level.time - c->shinobi.dpsStart > 5000 ) )
		Shinobi_DpsFlush( attacker );
	if ( !c->shinobi.dpsHits ) {
		c->shinobi.dpsStart = level.time;
		c->shinobi.dpsKind = kind;
	}
	c->shinobi.dpsHits++;
	c->shinobi.dpsTotal += take;
	c->shinobi.dpsLast = level.time;
}
