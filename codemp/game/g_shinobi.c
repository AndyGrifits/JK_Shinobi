/*
===========================================================================
JK_Shinobi - server-side jutsu core: media, chakra charging, hold-to-charge
jutsu levels, hand jutsu (Rasengan / Chidori) and Shunshin.

Flags that the client predicts or renders live in playerState user fields
(see bg_shinobi.h). Timers live in gclient_t->shinobi.

Other jutsu: g_shinobi_clones.c (Kage Bunshin), g_shinobi_elements.c
(Katon fireball, Doton earth wall).
===========================================================================
*/
#include "g_shinobi_local.h"

extern void G_Knockdown( gentity_t *victim ); // g_combat.c (AnimalNPC.c has an unrelated static one)

shinobiMedia_t shinobiMedia;

static qboolean Shinobi_CastShunshin( gentity_t *ent, int lvl, qboolean back );
static void     Shinobi_KawarimiThink( gentity_t *ent, usercmd_t *ucmd );

void Shinobi_EffectDir( int fx, vec3_t org, vec3_t dir ) {
	vec3_t angles;
	vectoangles( dir, angles );
	G_PlayEffectID( fx, org, angles );
}

int Shinobi_LevelEffect( const char *base, int level ) {
	return G_EffectIndex( va( "%s_%i", base, level ) );
}

void Shinobi_Init( void ) {
	shinobiMedia_t *m = &shinobiMedia;
	int lvl;

	for ( lvl = 1; lvl <= SHINOBI_MAX_LEVEL; lvl++ ) {
		m->fxRasenganHit[lvl - 1] = Shinobi_LevelEffect( SHINOBI_FX_RASENGAN_HIT, lvl );
		m->fxChidoriHit[lvl - 1]  = Shinobi_LevelEffect( SHINOBI_FX_CHIDORI_HIT, lvl );
	}
	m->fxRasenganFizzle = G_EffectIndex( SHINOBI_FX_RASENGAN_FIZZLE );
	m->fxChidoriFizzle  = G_EffectIndex( SHINOBI_FX_CHIDORI_FIZZLE );
	m->fxShunshin       = G_EffectIndex( SHINOBI_FX_SHUNSHIN );
	m->fxClonePoof      = G_EffectIndex( SHINOBI_FX_CLONE_POOF );
	m->fxCloneSmoke     = G_EffectIndex( SHINOBI_FX_CLONE_SMOKE );

	// Sounds from the base game, reused until we have our own.
	m->sndChargeStart  = G_SoundIndex( "sound/weapons/force/jumpbuild.wav" );
	m->sndJutsuCharge  = G_SoundIndex( "sound/weapons/force/jumpbuild.wav" );
	m->sndFizzle       = G_SoundIndex( "sound/weapons/force/noforce.wav" );
	m->sndPoof         = G_SoundIndex( "sound/weapons/force/distract.wav" );
	m->sndRasenganForm = G_SoundIndex( "sound/weapons/force/drain.wav" );
	m->sndRasenganDash = G_SoundIndex( "sound/weapons/force/speed.wav" );
	m->sndRasenganHit  = G_SoundIndex( "sound/weapons/force/push.wav" );
	m->sndChidoriForm  = G_SoundIndex( "sound/weapons/force/lightning" );
	m->sndChidoriDash  = G_SoundIndex( "sound/weapons/force/speed.wav" );
	m->sndChidoriHit   = G_SoundIndex( "sound/weapons/force/lightninghit1" );

	Shinobi_ElementsInit();
	Shinobi_NatureInit();
	Shinobi_KunaiInit();

	trap->Print( "[shinobi] game module built " __DATE__ " " __TIME__ ": fx rasenganHit %i/%i/%i fireballBoom %i/%i/%i cloneSmoke %i, wall models %i %i %i\n",
		m->fxRasenganHit[0], m->fxRasenganHit[1], m->fxRasenganHit[2],
		m->fxFireballBoom[0], m->fxFireballBoom[1], m->fxFireballBoom[2], m->fxCloneSmoke,
		m->wallModels[0], m->wallModels[1], m->wallModels[2] );
}

/*
===========================================================================
Common helpers
===========================================================================
*/
// hard crowd control (stuns, knockdowns, prisons, binds) can't chain: after
// one ends the victim is immune for a moment
qboolean Shinobi_CCImmune( gentity_t *e ) {
	return ( e && e->client && e->s.number < MAX_CLIENTS && e->client->shinobi.ccImmuneEnd > level.time ) ? qtrue : qfalse;
}

void Shinobi_CCGrant( gentity_t *e, int ccEnds ) {
	if ( !e || !e->client || e->s.number >= MAX_CLIENTS )
		return;
	if ( e->client->shinobi.ccImmuneEnd < ccEnds + SHINOBI_CC_IMMUNE_MS )
		e->client->shinobi.ccImmuneEnd = ccEnds + SHINOBI_CC_IMMUNE_MS;
}

qboolean Shinobi_CanAct( gentity_t *ent ) {
	gclient_t *client = ent->client;

	if ( !client || ent->health <= 0 )
		return qfalse;
	if ( client->sess.sessionTeam == TEAM_SPECTATOR || client->tempSpectate >= level.time )
		return qfalse;
	if ( client->ps.pm_type != PM_NORMAL && client->ps.pm_type != PM_JETPACK )
		return qfalse;
	if ( client->ps.m_iVehicleNum || client->ps.emplacedIndex )
		return qfalse;
	if ( client->ps.forceHandExtend == HANDEXTEND_KNOCKDOWN )
		return qfalse;
	return qtrue;
}

// userInt1 doubles as the Chidori marker while a hand jutsu exists
void Shinobi_StopCharging( gclient_t *client ) {
	if ( !SHINOBI_HAND_ANY( &client->ps ) )
		client->ps.userInt1 = 0;
}

void Shinobi_NoChakra( gentity_t *ent, int cost ) {
	G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
	trap->SendServerCommand( ent - g_entities, va( "cp \"Not enough chakra (%i/%i)\"", ent->client->ps.fd.forcePower, cost ) );
}

qboolean Shinobi_SpendChakra( gentity_t *ent, int cost ) {
	gclient_t *client = ent->client;

	if ( client->ps.fd.forcePower < cost ) {
		Shinobi_NoChakra( ent, cost );
		return qfalse;
	}
	client->ps.fd.forcePower -= cost;
	return qtrue;
}

/*
===========================================================================
Hand jutsu: Rasengan and Chidori share one state machine (form -> armed ->
dash strike); they differ in numbers and effects, and both scale by level.
===========================================================================
*/
static qboolean Shinobi_HandIsChidori( gclient_t *client ) {
	return SHINOBI_HAND_IS_CHIDORI( &client->ps ) ? qtrue : qfalse;
}

static int Shinobi_HandLevel( gclient_t *client ) {
	int lvl = SHINOBI_HAND_LEVEL( &client->ps );
	return lvl < 1 ? 1 : ( lvl > SHINOBI_MAX_LEVEL ? SHINOBI_MAX_LEVEL : lvl );
}

static void Shinobi_ClearHand( gclient_t *client ) {
	client->ps.userInt1 = 0; // also clears the Chidori marker
	client->ps.userInt2 = 0;
	client->ps.userInt3 = 0;
	SHINOBI_SET_HAND_LEVEL( &client->ps, 1 );
	client->shinobi.formEnd = 0;
	client->shinobi.armedEnd = 0;
	client->shinobi.dashEnd = 0;
	client->shinobi.handSoundNext = 0;
}

static void Shinobi_HandFizzle( gentity_t *ent ) {
	vec3_t up = { 0, 0, 1 };
	vec3_t org;

	VectorCopy( ent->client->ps.origin, org );
	org[2] += 16;
	G_PlayEffectID( Shinobi_HandIsChidori( ent->client ) ? shinobiMedia.fxChidoriFizzle : shinobiMedia.fxRasenganFizzle, org, up );
	Shinobi_ClearHand( ent->client );
}

static qboolean Shinobi_CastHandJutsu( gentity_t *ent, qboolean chidori, int lvl ) {
	gclient_t *client = ent->client;

	if ( SHINOBI_HAND_ANY( &client->ps ) )
		return qfalse;

	client->ps.userInt1 = chidori ? 1 : 0; // charging stops, Chidori marker set
	client->ps.userInt2 = 0;
	client->ps.userInt3 = 1; // forming
	SHINOBI_SET_HAND_LEVEL( &client->ps, lvl );
	client->shinobi.formEnd = level.time + SHINOBI_HAND_FORM_MS;
	client->shinobi.handSoundNext = level.time + 900;
	G_Sound( ent, CHAN_AUTO, chidori ? shinobiMedia.sndChidoriForm : shinobiMedia.sndRasenganForm );
	return qtrue;
}

static gentity_t *Shinobi_FindDashTarget( gentity_t *ent, vec3_t fwd, vec3_t hitPoint ) {
	int       touch[MAX_GENTITIES];
	int       num, i;
	vec3_t    center, mins, maxs;
	gentity_t *best = NULL;
	float     bestDist = 0;

	VectorMA( ent->client->ps.origin, SHINOBI_HAND_REACH, fwd, center );
	VectorSet( mins, center[0] - SHINOBI_HAND_HITBOX, center[1] - SHINOBI_HAND_HITBOX, center[2] - SHINOBI_HAND_HITBOX );
	VectorSet( maxs, center[0] + SHINOBI_HAND_HITBOX, center[1] + SHINOBI_HAND_HITBOX, center[2] + SHINOBI_HAND_HITBOX );

	num = trap->EntitiesInBox( mins, maxs, touch, MAX_GENTITIES );
	for ( i = 0; i < num; i++ ) {
		gentity_t *other = &g_entities[touch[i]];
		trace_t    tr;
		float      dist;

		if ( other == ent || !other->inuse || !other->client || other->health <= 0 )
			continue;
		if ( other->client->sess.sessionTeam == TEAM_SPECTATOR )
			continue;
		if ( level.gametype >= GT_TEAM && OnSameTeam( ent, other ) )
			continue;
		if ( Shinobi_DamageBlocked( other, ent ) )
			continue; // our own shadow clones

		// don't hit through walls
		trap->Trace( &tr, ent->client->ps.origin, NULL, NULL, other->r.currentOrigin, ent->s.number, MASK_SOLID, qfalse, 0, 0 );
		if ( tr.fraction < 1.0f )
			continue;

		dist = Distance( ent->client->ps.origin, other->r.currentOrigin );
		if ( !best || dist < bestDist ) {
			best = other;
			bestDist = dist;
		}
	}

	if ( best ) {
		VectorAdd( ent->client->ps.origin, best->r.currentOrigin, hitPoint );
		VectorScale( hitPoint, 0.5f, hitPoint );
		hitPoint[2] += 12;
	}
	return best;
}

// something solid but not a body right in front of the dash: an earth wall
// pillar, a breakable... it takes the hit too
static gentity_t *Shinobi_FindDashObstacle( gentity_t *ent, vec3_t fwd, vec3_t hitPoint ) {
	vec3_t    start, end, mins = { -8, -8, -8 }, maxs = { 8, 8, 8 };
	trace_t   tr;
	gentity_t *other;

	VectorCopy( ent->client->ps.origin, start );
	start[2] += 8;
	VectorMA( start, SHINOBI_HAND_REACH + SHINOBI_HAND_HITBOX, fwd, end );
	trap->Trace( &tr, start, mins, maxs, end, ent->s.number, MASK_SHOT, qfalse, 0, 0 );
	if ( tr.fraction >= 1.0f || tr.entityNum >= ENTITYNUM_WORLD )
		return NULL;
	other = &g_entities[tr.entityNum];
	if ( !other->inuse || other->client || !other->takedamage )
		return NULL;
	VectorCopy( tr.endpos, hitPoint );
	return other;
}

static void Shinobi_HandHit( gentity_t *ent, gentity_t *victim, vec3_t fwd, vec3_t point ) {
	gclient_t     *client = ent->client;
	const qboolean chidori = Shinobi_HandIsChidori( client );
	const int      li = Shinobi_HandLevel( client ) - 1;
	const int      damage = chidori ? shinobiChidoriDamage[li] : shinobiRasenganDamage[li];
	const int      throwSpeed = chidori ? shinobiChidoriThrow[li] : shinobiRasenganThrow[li];
	vec3_t         dir;

	VectorCopy( fwd, dir );
	dir[2] = 0.35f;
	VectorNormalize( dir );

	shinobiHitElement = chidori ? SE_LIGHTNING : SE_NONE; // wet targets take 1.5x from Chidori
	shinobiJutsuDamage = 1; // a jutsu, not a punch: no taijutsu bonus
	G_Damage( victim, ent, ent, dir, point, damage, DAMAGE_NO_KNOCKBACK, MOD_MELEE );
	shinobiJutsuDamage = 0;
	shinobiHitElement = SE_NONE;
	if ( chidori )
		Shinobi_ElectrifyAt( point, ent->s.number + 1 );

	if ( victim->inuse && victim->client && victim->health > 0 ) {
		gclient_t *vc = victim->client;

		VectorScale( dir, throwSpeed, vc->ps.velocity );
		vc->ps.groundEntityNum = ENTITYNUM_NONE;
		// credit the kill if they hit a wall or fall off a ledge
		vc->ps.otherKiller = ent->s.number;
		vc->ps.otherKillerTime = level.time + 5000;
		vc->ps.otherKillerDebounceTime = level.time + 100;

		if ( Shinobi_CCImmune( victim ) ) {
			// just out of another stun: only the hit and the throw
		}
		else if ( !chidori ) {
			G_Knockdown( victim );
			Shinobi_CCGrant( victim, level.time + 1500 );
		}
		else {
			// electrocuted: electric shell on the model, no attacks, no control for a moment
			const int stun = shinobiChidoriStunMs[li];
			Shinobi_CCGrant( victim, level.time + stun );
			vc->ps.electrifyTime = level.time + stun;
			if ( vc->ps.weaponTime < stun )
				vc->ps.weaponTime = stun;
			vc->ps.pm_flags |= PMF_TIME_KNOCKBACK;
			vc->ps.pm_time = stun / 2;
		}
	}

	Shinobi_EffectDir( chidori ? shinobiMedia.fxChidoriHit[li] : shinobiMedia.fxRasenganHit[li], point, dir );
	G_SoundAtLoc( point, CHAN_AUTO, chidori ? shinobiMedia.sndChidoriHit : shinobiMedia.sndRasenganHit );
	Shinobi_ClearHand( client );
}

static void Shinobi_HandThink( gentity_t *ent, qboolean attackPressed ) {
	gclient_t     *client = ent->client;
	qboolean       chidori;
	int            li;

	if ( !SHINOBI_HAND_ANY( &client->ps ) )
		return;
	chidori = Shinobi_HandIsChidori( client );
	li = Shinobi_HandLevel( client ) - 1;

	// Chidori keeps chirping while it exists
	if ( chidori && client->shinobi.handSoundNext <= level.time ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndChidoriForm );
		client->shinobi.handSoundNext = level.time + 900;
	}

	if ( SHINOBI_HAND_FORMING( &client->ps ) ) {
		if ( level.time >= client->shinobi.formEnd ) {
			client->ps.userInt3 = 0;
			client->ps.userInt2 = 1; // armed
			client->shinobi.armedEnd = level.time + ( chidori ? SHINOBI_CHIDORI_HOLD_MS : SHINOBI_RASENGAN_HOLD_MS );
		}
		return;
	}

	if ( SHINOBI_HAND_ARMED( &client->ps ) ) {
		if ( attackPressed ) {
			client->ps.userInt3 = 1; // armed + forming bits = dash
			client->shinobi.dashEnd = level.time + ( chidori ? shinobiChidoriDashMs[li] : shinobiRasenganDashMs[li] );
			G_Sound( ent, CHAN_BODY, chidori ? shinobiMedia.sndChidoriDash : shinobiMedia.sndRasenganDash );
		}
		else if ( level.time >= client->shinobi.armedEnd ) {
			Shinobi_HandFizzle( ent );
		}
		return;
	}

	if ( SHINOBI_HAND_DASH( &client->ps ) ) {
		vec3_t     yaw, fwd, point;
		gentity_t *victim;

		VectorSet( yaw, 0, client->ps.viewangles[YAW], 0 );
		AngleVectors( yaw, fwd, NULL, NULL );

		if ( chidori ) { // lightning dragged through a puddle electrifies it
			vec3_t feet;
			VectorCopy( client->ps.origin, feet );
			feet[2] += ent->r.mins[2];
			Shinobi_ElectrifyAt( feet, ent->s.number + 1 );
		}

		victim = Shinobi_FindDashTarget( ent, fwd, point );
		if ( !victim )
			victim = Shinobi_FindDashObstacle( ent, fwd, point );
		if ( victim ) {
			Shinobi_HandHit( ent, victim, fwd, point );
		}
		else if ( level.time >= client->shinobi.dashEnd ) {
			Shinobi_HandFizzle( ent );
		}
	}
}

/*
===========================================================================
Casting with levels
===========================================================================
*/

// Casts 'jutsu' at the highest level up to 'wanted' that the caster can pay for.
void Shinobi_Cast( gentity_t *ent, int jutsu, int wanted ) {
	gclient_t *client = ent->client;
	int        lvl = wanted;
	qboolean   cast = qfalse;

	if ( !Shinobi_CanAct( ent ) || jutsu <= SJ_NONE || jutsu >= SJ_COUNT )
		return;
	if ( !Shinobi_HasJutsu( ent, jutsu ) ) {
		trap->SendServerCommand( ent - g_entities, va( "cp \"%s is not in your build\"", shinobiJutsuTitles[jutsu] ) );
		return;
	}
	if ( jutsu != SJ_KAWARIMI ) {
		if ( client->shinobi.prisonCaster || client->shinobi.shadowBy )
			return; // in a Water Prison or a Shadow Bind only Kawarimi works
		Shinobi_Reveal( ent ); // any jutsu ends a substitution
	}

	if ( lvl < 1 )
		lvl = 1;
	if ( lvl > SHINOBI_MAX_LEVEL )
		lvl = SHINOBI_MAX_LEVEL;
	if ( lvl < SHINOBI_JUTSU_MIN_LEVEL( jutsu ) ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		trap->SendServerCommand( ent - g_entities, va( "cp \"%s needs a longer charge\"", shinobiJutsuTitles[jutsu] ) );
		return;
	}
	while ( lvl > SHINOBI_JUTSU_MIN_LEVEL( jutsu ) && client->ps.fd.forcePower < Shinobi_JutsuCost( jutsu, lvl ) )
		lvl--;
	if ( client->ps.fd.forcePower < Shinobi_JutsuCost( jutsu, lvl ) ) {
		Shinobi_NoChakra( ent, Shinobi_JutsuCost( jutsu, lvl ) );
		return;
	}

	switch ( jutsu ) {
	case SJ_RASENGAN: cast = Shinobi_CastHandJutsu( ent, qfalse, lvl ); break;
	case SJ_CHIDORI:  cast = Shinobi_CastHandJutsu( ent, qtrue, lvl ); break;
	case SJ_KATON:    cast = Shinobi_CastKaton( ent, lvl ); break;
	case SJ_DOTON:    cast = Shinobi_CastDoton( ent, lvl ); break;
	case SJ_BUNSHIN:  cast = Shinobi_CastKageBunshin( ent, lvl ); break;
	case SJ_SHUNSHIN: cast = Shinobi_CastShunshin( ent, lvl, client->shinobi.shunshinBack ); break;
	case SJ_KAWARIMI: cast = Shinobi_CastKawarimi( ent ); break;
	case SJ_SUITON:   cast = Shinobi_CastSuiton( ent, lvl ); break;
	case SJ_FUTON:    cast = Shinobi_CastFuton( ent, lvl ); break;
	case SJ_SUIRO:    cast = Shinobi_CastSuiro( ent, lvl ); break;
	case SJ_WAVE:     cast = Shinobi_CastWave( ent, lvl ); break;
	case SJ_BLADE:    cast = Shinobi_CastBlade( ent, lvl ); break;
	case SJ_FLAME:    cast = Shinobi_CastFlame( ent ); break;
	case SJ_ASH:      cast = Shinobi_CastAsh( ent, lvl ); break;
	case SJ_SHADOW:   cast = Shinobi_CastShadow( ent ); break;
	case SJ_WATERCLONE: cast = Shinobi_CastWaterClone( ent ); break;
	case SJ_HIRAISHIN: cast = Shinobi_CastHiraishin( ent ); break;
	case SJ_RAITRAP:  cast = Shinobi_CastRaiTrap( ent ); break;
	default: break;
	}

	if ( cast ) {
		client->ps.fd.forcePower -= Shinobi_JutsuCost( jutsu, lvl );
		Shinobi_StopCharging( client );
	}
}

// "<jutsu> [level]": cast right away (binds use +jutsu/-jutsu, scripts and bots this)
static void Shinobi_CastCommand( gentity_t *ent, int jutsu ) {
	char arg[8];
	int  lvl = 1;

	if ( trap->Argc() > 1 ) {
		trap->Argv( 1, arg, sizeof( arg ) );
		lvl = atoi( arg );
	}
	if ( jutsu == SJ_SHUNSHIN ) { // "shunshin [level] [back]"
		trap->Argv( 2, arg, sizeof( arg ) );
		ent->client->shinobi.shunshinBack = !Q_stricmp( arg, "back" );
	}
	Shinobi_Cast( ent, jutsu, lvl );
}

void Cmd_Rasengan_f( gentity_t *ent )    { Shinobi_CastCommand( ent, SJ_RASENGAN ); }
void Cmd_Chidori_f( gentity_t *ent )     { Shinobi_CastCommand( ent, SJ_CHIDORI ); }
void Cmd_Katon_f( gentity_t *ent )       { Shinobi_CastCommand( ent, SJ_KATON ); }
void Cmd_Doton_f( gentity_t *ent )       { Shinobi_CastCommand( ent, SJ_DOTON ); }
void Cmd_KageBunshin_f( gentity_t *ent ) { Shinobi_CastCommand( ent, SJ_BUNSHIN ); }
void Cmd_Shunshin_f( gentity_t *ent )    { Shinobi_CastCommand( ent, SJ_SHUNSHIN ); }
void Cmd_Kawarimi_f( gentity_t *ent )    { Shinobi_CastCommand( ent, SJ_KAWARIMI ); }
void Cmd_Suiton_f( gentity_t *ent )      { Shinobi_CastCommand( ent, SJ_SUITON ); }
void Cmd_Futon_f( gentity_t *ent )       { Shinobi_CastCommand( ent, SJ_FUTON ); }
void Cmd_Suiro_f( gentity_t *ent )       { Shinobi_CastCommand( ent, SJ_SUIRO ); }
void Cmd_Wave_f( gentity_t *ent )        { Shinobi_CastCommand( ent, SJ_WAVE ); }
void Cmd_WindBlade_f( gentity_t *ent )   { Shinobi_CastCommand( ent, SJ_BLADE ); }
void Cmd_Flame_f( gentity_t *ent )       { Shinobi_CastCommand( ent, SJ_FLAME ); }
void Cmd_Ash_f( gentity_t *ent )         { Shinobi_CastCommand( ent, SJ_ASH ); }
void Cmd_Kagemane_f( gentity_t *ent )    { Shinobi_CastCommand( ent, SJ_SHADOW ); }
void Cmd_MizuBunshin_f( gentity_t *ent ) { Shinobi_CastCommand( ent, SJ_WATERCLONE ); }
void Cmd_PushWall_f( gentity_t *ent ) {
	if ( Shinobi_CanAct( ent ) && !ent->client->shinobi.prisonCaster )
		Shinobi_PushWall( ent );
}

// "jutsu_begin <name>": key pressed, start timing the hold
void Cmd_JutsuBegin_f( gentity_t *ent ) {
	gclient_t *client = ent->client;
	char       name[32];
	int        jutsu;

	if ( !Shinobi_CanAct( ent ) )
		return;
	trap->Argv( 1, name, sizeof( name ) );
	jutsu = Shinobi_JutsuForName( name );
	if ( jutsu == SJ_NONE )
		return;

	if ( SHINOBI_JUTSU_INSTANT( jutsu ) ) {
		Shinobi_Cast( ent, jutsu, 1 );
		return;
	}
	if ( client->shinobi.shadowBy || client->shinobi.prisonCaster )
		return; // bound or imprisoned: no seals
	if ( !Shinobi_HasJutsu( ent, jutsu ) ) {
		trap->SendServerCommand( ent - g_entities, va( "cp \"%s is not in your build\"", shinobiJutsuTitles[jutsu] ) );
		return;
	}
	if ( jutsu == SJ_DOTON && !client->shinobi.prisonCaster && Shinobi_PushWall( ent ) )
		return; // our wall was right in front: F shoves it forward instead of raising a new one
	if ( SHINOBI_JUTSU_HOLD( jutsu ) ) { // starts now, lasts until the key comes up
		Shinobi_Cast( ent, jutsu, 1 );
		if ( ( jutsu == SJ_SUIRO && client->shinobi.prisonTarget ) || ( jutsu == SJ_FLAME && client->shinobi.flameEnt )
			|| ( jutsu == SJ_SHADOW && client->shinobi.shadowEnt ) )
			client->shinobi.chargeJutsu = jutsu;
		return;
	}

	Shinobi_Reveal( ent );
	client->shinobi.chargeJutsu = jutsu;
	client->shinobi.chargeStart = level.time;
	Shinobi_StopCharging( client );
	if ( jutsu != SJ_SHUNSHIN ) // a quiet blink, no wind-up noise
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndJutsuCharge );
}

// "jutsu_release": key released, cast at the level the hold reached
void Cmd_JutsuRelease_f( gentity_t *ent ) {
	gclient_t *client = ent->client;
	int        jutsu = client->shinobi.chargeJutsu;

	if ( jutsu == SJ_NONE )
		return;
	client->shinobi.chargeJutsu = SJ_NONE;
	if ( SHINOBI_JUTSU_HOLD( jutsu ) ) { // key up: let go
		if ( jutsu == SJ_SUIRO && client->shinobi.prisonTarget )
			Shinobi_PrisonRelease( ent );
		else if ( jutsu == SJ_FLAME && client->shinobi.flameEnt )
			Shinobi_FlameStop( ent );
		else if ( jutsu == SJ_SHADOW && client->shinobi.shadowEnt )
			Shinobi_ShadowRelease( ent );
		return;
	}
	if ( level.time - client->shinobi.chargeStart
		< Shinobi_BarEndCast( jutsu, SHINOBI_JUTSU_MIN_LEVEL( jutsu ) - 1, Shinobi_CastStat( ent ) ) ) {
		// let go before the first bar was full: the seals weren't finished
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		return;
	}
	if ( jutsu == SJ_SHUNSHIN ) // Shift + back + jump blinks backward
		client->shinobi.shunshinBack = client->pers.cmd.forwardmove < 0;
	Shinobi_Cast( ent, jutsu, Shinobi_ChargeLevelCast( jutsu, level.time - client->shinobi.chargeStart, Shinobi_CastStat( ent ) ) );
}

/*
==================
Shinobi_ClientThink

Called every client frame from ClientThink_real, before Pmove.
==================
*/
void Shinobi_ClientThink( gentity_t *ent, usercmd_t *ucmd ) {
	gclient_t *client = ent->client;
	qboolean   attackPressed;

	if ( ent->health <= 0 || client->sess.sessionTeam == TEAM_SPECTATOR )
		Shinobi_StatusClear( ent );
	else
		Shinobi_StatusThink( ent, ucmd ); // fire, water, mud, Water Prison (may freeze ucmd)
	Shinobi_PerkThink( ent ); // eyes, gates and seal also run while bound or knocked down

	if ( !Shinobi_CanAct( ent ) ) {
		Shinobi_ClearHand( client );
		client->shinobi.chargeJutsu = SJ_NONE;
		Shinobi_Reveal( ent );
		return;
	}

	Shinobi_KawarimiThink( ent, ucmd );
	Shinobi_InfuseThink( ent, ucmd );

	// forming hand seals while a jutsu key is held (not for the quick Shunshin)
	if ( client->shinobi.chargeJutsu != SJ_NONE && client->shinobi.chargeJutsu != SJ_SHUNSHIN
		&& !SHINOBI_JUTSU_HOLD( client->shinobi.chargeJutsu ) ) {
		static const int seals[] = { BOTH_FORCEHEAL_QUICK, BOTH_MINDTRICK2, BOTH_FORCEPULL, BOTH_MINDTRICK1 };
		const int step = ( level.time - client->shinobi.chargeStart ) / SHINOBI_SEAL_MS;
		client->ps.forceHandExtend = HANDEXTEND_TAUNT;
		client->ps.forceDodgeAnim = seals[step % ARRAY_LEN( seals )];
		client->ps.forceHandExtendTime = level.time + 150;
	}

	attackPressed = ( ucmd->buttons & BUTTON_ATTACK ) && !( client->buttons & BUTTON_ATTACK );

	// --- Chakra charging: hold USE while standing still on the ground ---
	if ( ( ucmd->buttons & BUTTON_USE ) && !client->shinobi.shadowBy && !client->shinobi.prisonCaster
		&& !ucmd->forwardmove && !ucmd->rightmove && ucmd->upmove <= 0
		&& client->ps.groundEntityNum != ENTITYNUM_NONE
		&& !SHINOBI_HAND_ANY( &client->ps )
		&& client->shinobi.chargeJutsu == SJ_NONE
		&& client->ps.weaponTime <= 0 ) {
		if ( !client->ps.userInt1 ) {
			client->ps.userInt1 = 1;
			client->shinobi.chargeTick = level.time + SHINOBI_CHARGE_TICK_MS;
			G_Sound( ent, CHAN_AUTO, shinobiMedia.sndChargeStart );
			// meditate with the blade off, like the stock /meditate taunt
			if ( client->ps.weapon == WP_SABER && client->ps.saberHolstered != 2 ) {
				G_Sound( ent, CHAN_WEAPON, client->saber[0].soundOff );
				client->ps.saberHolstered = 2;
			}
		}
		while ( client->shinobi.chargeTick <= level.time ) {
			client->ps.fd.forcePower += SHINOBI_CHARGE_PER_TICK;
			client->shinobi.chargeTick += SHINOBI_CHARGE_TICK_MS;
		}
		if ( client->ps.fd.forcePower > client->ps.fd.forcePowerMax )
			client->ps.fd.forcePower = client->ps.fd.forcePowerMax;
	}
	else {
		Shinobi_StopCharging( client );
	}

	Shinobi_HandThink( ent, attackPressed );
}

/*
===========================================================================
Shunshin no Jutsu (body flicker): Shift + jump blinks forward along the view,
Shift + back + jump blinks backward. Holding jump charges the distance.
===========================================================================
*/
static qboolean Shinobi_CastShunshin( gentity_t *ent, int lvl, qboolean back ) {
	gclient_t   *client = ent->client;
	const float  range = (float)shinobiShunshinDist[lvl - 1];
	vec3_t       angles, dir, start, end;
	trace_t      tr;
	float        dist;

	if ( client->shinobi.shunshinNext > level.time ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		trap->SendServerCommand( ent - g_entities, va( "cp \"Shunshin ready in %.1f s\"",
			( client->shinobi.shunshinNext - level.time ) / 1000.0f ) );
		return qfalse;
	}
	if ( SHINOBI_HAND_DASH( &client->ps ) )
		return qfalse;

	// straight along the view, up and down included (backward: the opposite way,
	// still facing forward)
	VectorCopy( client->ps.viewangles, angles );
	angles[ROLL] = 0;
	AngleVectors( angles, dir, NULL, NULL );
	if ( back )
		VectorScale( dir, -1, dir );

	VectorCopy( client->ps.origin, start );
	start[2] += 1; // clear the floor
	VectorMA( start, range, dir, end );

	trap->Trace( &tr, start, ent->r.mins, ent->r.maxs, end, ent->s.number, MASK_PLAYERSOLID, qfalse, 0, 0 );
	if ( tr.startsolid || tr.allsolid )
		return qfalse;
	dist = tr.fraction * range;
	if ( dist < SHINOBI_SHUNSHIN_MIN_DIST ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		return qfalse;
	}

	client->shinobi.shunshinNext = level.time + SHINOBI_SHUNSHIN_COOLDOWN;
	Shinobi_StopCharging( client );

	// smoke and leaves where we were, and where we land
	Shinobi_EffectDir( shinobiMedia.fxShunshin, client->ps.origin, dir );
	G_SoundAtLoc( client->ps.origin, CHAN_AUTO, shinobiMedia.sndPoof );

	Shinobi_TeleportClient( ent, tr.endpos );

	Shinobi_EffectDir( shinobiMedia.fxShunshin, client->ps.origin, dir );
	return qtrue;
}

/*
===========================================================================
Kawarimi no Jutsu (substitution)

Cast: you turn non-solid and an NPC double with your looks spawns exactly
where you stand. Once it is in place (next frame) it takes over - same spot,
pose and speed - and you are hidden: your entity is only sent to your own
client (and to Sharingan eyes nearby), so nobody else can see, hear or target you. You
creep slowly. Attacking, jumping, using, any jutsu, taking damage or running
out of time reveals you, and the double turns into a log in a puff of smoke.
Hitting the double also turns it into a log (you stay hidden).
===========================================================================
*/
static void Shinobi_SetSolid( gentity_t *ent, qboolean solid ) {
	ent->r.contents = solid ? CONTENTS_BODY : 0;
	trap->LinkEntity( (sharedEntity_t *)ent );
}

// standing inside someone (player, NPC, our double)?
static qboolean Shinobi_InsideBody( gentity_t *ent ) {
	trace_t tr;

	trap->Trace( &tr, ent->client->ps.origin, ent->r.mins, ent->r.maxs, ent->client->ps.origin,
		ent->s.number, CONTENTS_BODY, qfalse, 0, 0 );
	return ( tr.startsolid || tr.allsolid ) ? qtrue : qfalse;
}

qboolean Shinobi_CastKawarimi( gentity_t *ent ) {
	gclient_t *client = ent->client;
	gentity_t *decoy;

	if ( client->shinobi.kawarimiEnd || client->shinobi.kawarimiPending )
		return qfalse; // already substituted
	if ( client->shinobi.kawarimiNext > level.time ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		trap->SendServerCommand( ent - g_entities, va( "cp \"Kawarimi ready in %.1f s\"",
			( client->shinobi.kawarimiNext - level.time ) / 1000.0f ) );
		return qfalse;
	}

	// substitution is the way out of a Water Prison or a Shadow Bind (no gasping afterwards)
	if ( client->shinobi.prisonCaster )
		Shinobi_PrisonRelease( ent );
	if ( client->shinobi.shadowBy )
		Shinobi_ShadowRelease( ent );
	client->shinobi.stunEnd = 0;

	// drop whatever we were doing - the double carries on instead
	if ( SHINOBI_HAND_ANY( &client->ps ) )
		Shinobi_ClearHand( client );
	client->shinobi.chargeJutsu = SJ_NONE;

	Shinobi_SetSolid( ent, qfalse ); // the double spawns inside us
	decoy = Shinobi_SpawnDecoy( ent );
	if ( !decoy ) {
		Shinobi_SetSolid( ent, qtrue );
		return qfalse;
	}

	client->shinobi.kawarimiDecoy = decoy->s.number + 1;
	client->shinobi.kawarimiPending = qtrue;
	client->shinobi.kawarimiPhase = qtrue;
	client->shinobi.kawarimiNext = level.time + SHINOBI_KAWARIMI_COOLDOWN;
	return qtrue;
}

// the double is in place: vanish
void Shinobi_Hide( gentity_t *ent ) {
	gclient_t *client = ent->client;

	client->shinobi.kawarimiPending = qfalse;
	client->shinobi.kawarimiEnd = level.time + SHINOBI_KAWARIMI_MAX_MS;
	SHINOBI_SET_HIDDEN( &client->ps, 1 );
	ent->r.svFlags |= SVF_BROADCASTCLIENTS; // only to us and Sharingan eyes (Shinobi_PerkBroadcasts)
	ent->r.broadcastClients[0] = ent->r.broadcastClients[1] = 0;
	trap->LinkEntity( (sharedEntity_t *)ent );
	if ( client->ps.weapon == WP_SABER )
		client->ps.saberHolstered = 2; // quietly, a humming blade would give us away
	Shinobi_SetSolid( ent, qfalse );
	trap->SendServerCommand( ent - g_entities, "cp \"^5Kawarimi^7: hidden - move slowly\nany action reveals you\"" );
}

void Shinobi_Reveal( gentity_t *ent ) {
	gclient_t *client = ent->client;
	gentity_t *decoy;

	if ( !client || ( !client->shinobi.kawarimiPending && !client->shinobi.kawarimiEnd ) )
		return;

	client->shinobi.kawarimiPending = qfalse;
	client->shinobi.kawarimiEnd = 0;
	SHINOBI_SET_HIDDEN( &client->ps, 0 );
	ent->r.svFlags &= ~SVF_BROADCASTCLIENTS;

	decoy = Shinobi_Decoy( ent );
	if ( decoy )
		Shinobi_DecoyToLog( decoy );
	client->shinobi.kawarimiDecoy = 0;

	if ( ent->health > 0 ) {
		// solid again as soon as we are not standing inside someone
		client->shinobi.kawarimiPhase = Shinobi_InsideBody( ent );
		Shinobi_SetSolid( ent, !client->shinobi.kawarimiPhase );
	}
	else {
		client->shinobi.kawarimiPhase = qfalse;
		trap->LinkEntity( (sharedEntity_t *)ent ); // svFlags changed
	}
}

void Shinobi_Damaged( gentity_t *targ, gentity_t *attacker ) {
	if ( !targ || !targ->client || targ->s.number >= MAX_CLIENTS )
		return;
	Shinobi_Reveal( targ );
	if ( targ->client->shinobi.prisonTarget && attacker != targ )
		Shinobi_PrisonRelease( targ ); // hit while holding a Water Prison: it bursts
	if ( targ->client->shinobi.shadowEnt && attacker != targ )
		Shinobi_ShadowRelease( targ ); // ...or a Shadow Bind
}

static void Shinobi_KawarimiThink( gentity_t *ent, usercmd_t *ucmd ) {
	gclient_t *client = ent->client;
	const int  actions = BUTTON_ATTACK | BUTTON_ALT_ATTACK | BUTTON_USE | BUTTON_USE_HOLDABLE
		| BUTTON_FORCEPOWER | BUTTON_FORCEGRIP | BUTTON_FORCE_LIGHTNING | BUTTON_FORCE_DRAIN | BUTTON_GESTURE;

	if ( client->shinobi.kawarimiPhase && !client->shinobi.kawarimiEnd && !client->shinobi.kawarimiPending
		&& !Shinobi_InsideBody( ent ) ) {
		client->shinobi.kawarimiPhase = qfalse;
		Shinobi_SetSolid( ent, qtrue );
	}

	if ( client->shinobi.kawarimiPending && !Shinobi_Decoy( ent ) ) {
		Shinobi_Reveal( ent ); // the double never made it
		return;
	}

	if ( !client->shinobi.kawarimiEnd && !client->shinobi.kawarimiPending ) {
		if ( SHINOBI_HIDDEN( &client->ps ) )
			SHINOBI_SET_HIDDEN( &client->ps, 0 ); // stale bit (respawn)
		return;
	}

	if ( ( ucmd->buttons & actions ) || ucmd->upmove > 0
		|| ( client->shinobi.kawarimiEnd && level.time >= client->shinobi.kawarimiEnd ) ) {
		Shinobi_Reveal( ent );
	}
}

// move a client instantly (no telefrag, no teleporter effects)
void Shinobi_TeleportClient( gentity_t *ent, vec3_t origin ) {
	gclient_t *client = ent->client;

	trap->UnlinkEntity( (sharedEntity_t *)ent );
	VectorCopy( origin, client->ps.origin );
	if ( client->ps.velocity[2] < 0 )
		client->ps.velocity[2] = 0; // don't arrive mid-fall at full speed
	client->ps.eFlags ^= EF_TELEPORT_BIT; // tell clients not to lerp
	BG_PlayerStateToEntityState( &client->ps, &ent->s, qtrue );
	VectorCopy( client->ps.origin, ent->r.currentOrigin );
	trap->LinkEntity( (sharedEntity_t *)ent );
}
