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

	Shinobi_PatchItems(); // pickups -> pills and ninja tools (before the map spawns them)
	m->fxStockExplode      = G_EffectIndex( "thermal/explosion" );
	m->fxStockExplodeSmall = G_EffectIndex( "env/small_explode" );
	m->fxWaterImpact       = G_EffectIndex( "env/water_impact" );
	m->fxRockMed           = G_EffectIndex( "chunks/rockbreakmed" );
	m->fxRockLg            = G_EffectIndex( "chunks/rockbreaklg" );
	m->fxEmp               = G_EffectIndex( "demp2/altDetonate" );
	m->fxSparkBurst        = G_EffectIndex( "sparks/spark_explosion" );
	m->exKunaiModel  = G_ModelIndex( "models/shinobi/exkunai.md3" );
	m->tagModel      = G_ModelIndex( "models/shinobi/tag.md3" );
	m->shurikenModel = G_ModelIndex( "models/shinobi/shuriken.md3" );
	m->starModel     = G_ModelIndex( "models/shinobi/star.md3" );
	m->smokeBombModel = G_ModelIndex( "models/shinobi/smoke_bomb.md3" );
	m->weightModel    = G_ModelIndex( "models/shinobi/leg_weight.md3" );

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
	if ( e && e->client && e->s.number < MAX_CLIENTS && e->client->shinobi.armorEnd > level.time )
		return qtrue; // Stone Skin: nothing knocks you over
	if ( Shinobi_KaitenSpinning( e ) )
		return qtrue; // Kaiten: nothing takes hold
	return ( e && e->client && e->s.number < MAX_CLIENTS && e->client->shinobi.ccImmuneEnd > level.time ) ? qtrue : qfalse;
}

// lightning paralysis: rooted, no attacks, crackling (players: Shinobi_StatusThink holds them)
void Shinobi_Paralyse( gentity_t *v, int ms ) {
	if ( !v || !v->client || v->health <= 0 || Shinobi_CCImmune( v ) )
		return;
	v->client->ps.electrifyTime = level.time + ms;
	Shinobi_HitText( v, STX_STUN, 0 );
	shinobiTest.stuns++;
	if ( v->client->ps.weaponTime < ms )
		v->client->ps.weaponTime = ms;
	if ( v->s.number < MAX_CLIENTS ) {
		if ( v->client->shinobi.stunEnd < level.time + ms )
			v->client->shinobi.stunEnd = level.time + ms;
		v->client->ps.velocity[0] = v->client->ps.velocity[1] = 0;
	}
	else {
		v->client->ps.pm_flags |= PMF_TIME_KNOCKBACK;
		v->client->ps.pm_time = ms;
	}
	Shinobi_CCGrant( v, level.time + ms );
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
	shinobiGuardBreaker = ( li == SHINOBI_MAX_LEVEL - 1 ); // a full one smashes through any guard
	shinobiBlockResult = SBR_NONE;
	G_Damage( victim, ent, ent, dir, point, damage, DAMAGE_NO_KNOCKBACK, MOD_MELEE );
	shinobiGuardBreaker = 0;
	shinobiJutsuDamage = 0;
	shinobiHitElement = SE_NONE;
	if ( chidori )
		Shinobi_ElectrifyAt( point, ent->s.number + 1 );

	if ( shinobiBlockResult == SBR_BLOCKED || shinobiBlockResult == SBR_PARRIED ) {
		// caught on the guard: no throw, no knockdown
	}
	else if ( victim->inuse && victim->client && victim->health > 0 ) {
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
	if ( chidori )
		Shinobi_EffectDir( shinobiMedia.fxSparkBurst, point, dir );
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
void Shinobi_Debug( const char *fmt, ... ) {
	va_list argptr;
	char    text[1024];

	if ( !trap->Cvar_VariableIntegerValue( "shinobi_debug" ) )
		return;
	va_start( argptr, fmt );
	Q_vsnprintf( text, sizeof( text ), fmt, argptr );
	va_end( argptr );
	trap->Print( "[shinobi] %s\n", text );
}

// the chakra a cast costs (a copied jutsu: dearer)
static int Shinobi_Price( int jutsu, int lvl, qboolean copied ) {
	return copied ? Shinobi_CopyCost( jutsu, lvl ) : Shinobi_JutsuCost( jutsu, lvl );
}

qboolean Shinobi_Cast( gentity_t *ent, int jutsu, int wanted ) {
	gclient_t *client = ent->client;
	int        lvl = wanted, cost, costLvl;
	qboolean   cast = qfalse, copied;

	if ( jutsu <= SJ_NONE || jutsu >= SJ_COUNT )
		return qfalse;
	if ( !Shinobi_CanAct( ent ) && !( jutsu == SJ_KAWARIMI && client->ps.forceHandExtend == HANDEXTEND_KNOCKDOWN
		&& ent->health > 0 && client->sess.sessionTeam != TEAM_SPECTATOR ) )
		return qfalse; // (Kawarimi works lying on the floor too)
	if ( client->shinobi.flinchEnd > level.time && jutsu != SJ_KAWARIMI )
		return qfalse; // flinching from a combo punch: only Kawarimi breaks out
	if ( !Shinobi_HasJutsu( ent, jutsu ) ) {
		trap->SendServerCommand( ent - g_entities, va( "cp \"%s is not in your build\"", shinobiJutsuTitles[jutsu] ) );
		return qfalse;
	}
	if ( client->shinobi.blocking && ent->s.number < MAX_CLIENTS )
		return qfalse; // guard up: no jutsu
	if ( ( client->shinobi.burrowEnd && jutsu != SJ_BURROW ) || ( client->shinobi.shadowHideEnd && jutsu != SJ_SHADOWHIDE )
		|| ( client->shinobi.raiDashEnd && jutsu != SJ_RAIDASH ) )
		return qfalse; // under the ground / in the shadow / a bolt of lightning: come out first
	if ( jutsu != SJ_KAWARIMI ) {
		if ( client->shinobi.prisonCaster || client->shinobi.shadowBy || client->shinobi.shadowTrapEnt )
			return qfalse; // in a Water Prison, a Shadow Bind or a Shadow Trap only Kawarimi works
		Shinobi_Reveal( ent ); // any jutsu ends a substitution
	}

	if ( lvl < 1 )
		lvl = 1;
	if ( lvl > SHINOBI_MAX_LEVEL )
		lvl = SHINOBI_MAX_LEVEL;
	copied = ent->s.number < MAX_CLIENTS && client->shinobi.copyJutsu == jutsu
		&& !Shinobi_BuildHas( &client->pers.shinobiBuild, jutsu );
	if ( copied && lvl > SHINOBI_COPY_MAX_LEVEL )
		lvl = SHINOBI_COPY_MAX_LEVEL; // the copy is never as good as the real thing
	if ( lvl < SHINOBI_JUTSU_MIN_LEVEL( jutsu ) ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		trap->SendServerCommand( ent - g_entities, va( "cp \"%s needs a longer charge\"", shinobiJutsuTitles[jutsu] ) );
		return qfalse;
	}
	if ( !client->shinobi.costLevel ) {
		while ( lvl > SHINOBI_JUTSU_MIN_LEVEL( jutsu ) && client->ps.fd.forcePower < Shinobi_Price( jutsu, lvl, copied ) )
			lvl--;
	}
	costLvl = client->shinobi.costLevel ? client->shinobi.costLevel : lvl; // (Shift + F: the top price)
	if ( copied && costLvl > SHINOBI_COPY_MAX_LEVEL )
		costLvl = SHINOBI_COPY_MAX_LEVEL;
	cost = Shinobi_Price( jutsu, costLvl, copied );
	if ( shinobiJutsuSchool[jutsu] == SS_SUITON && ( client->ps.pm_flags & SHINOBI_PMF_ONWATER )
		&& client->shinobi.iceEnd <= level.time )
		cost = (int)( cost * SHINOBI_WATER_SUITON_COST + 0.5f ); // standing on water (and paying for it): Suiton is cheaper
	if ( client->ps.fd.forcePower < cost ) {
		Shinobi_NoChakra( ent, cost );
		Shinobi_Debug( "%i casts %s Lv%i: no chakra (%i)", ent->s.number, shinobiJutsuTitles[jutsu], lvl, client->ps.fd.forcePower );
		return qfalse;
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
	case SJ_MIST:     cast = Shinobi_CastMist( ent, lvl ); break;
	case SJ_QUAKE:    cast = Shinobi_CastQuake( ent, lvl ); break;
	case SJ_WINDWALL: cast = Shinobi_CastWindWall( ent, lvl ); break;
	case SJ_WINDEDGE: cast = Shinobi_CastEdge( ent, SS_FUTON ); break;
	case SJ_RAIEDGE:  cast = Shinobi_CastEdge( ent, SS_RAITON ); break;
	case SJ_BURROW:   cast = Shinobi_CastBurrow( ent ); break;
	case SJ_PILLAR:   cast = Shinobi_CastPillar( ent, lvl ); break;
	case SJ_ARMOR:    cast = Shinobi_CastArmor( ent ); break;
	case SJ_EARTHCLONE: cast = Shinobi_CastEarthClone( ent ); break;
	case SJ_RAICLONE: cast = Shinobi_CastRaiClone( ent ); break;
	case SJ_RAIDASH:  cast = Shinobi_CastRaiDash( ent ); break;
	case SJ_SHADOWHIDE: cast = Shinobi_CastShadowHide( ent ); break;
	case SJ_FLASH:    cast = Shinobi_CastFlash( ent ); break;
	case SJ_HEAL:     cast = Shinobi_CastHeal( ent, lvl ); break;
	case SJ_PALMS:    cast = Shinobi_CastPalms( ent ); break;
	case SJ_LOTUS:    cast = Shinobi_CastLotus( ent ); break;
	case SJ_ENTRY:    cast = Shinobi_CastEntry( ent ); break;
	case SJ_SKYSTEP:  trap->SendServerCommand( ent - g_entities, "cp \"Sky Step is passive\"" ); break;
	case SJ_SHADOW:   cast = Shinobi_CastShadow( ent ); break;
	case SJ_WATERCLONE: cast = Shinobi_CastWaterClone( ent ); break;
	case SJ_HIRAISHIN: cast = Shinobi_CastHiraishin( ent ); break;
	case SJ_RAITRAP:  cast = Shinobi_CastRaiTrap( ent ); break;
	case SJ_SHADOWTRAP: cast = Shinobi_CastShadowTrap( ent ); break;
	case SJ_KAITEN:   cast = Shinobi_CastKaiten( ent ); break;
	default: break;
	}

	if ( cast ) {
		client->ps.fd.forcePower -= cost;
		Shinobi_StopCharging( client );
		Shinobi_CopyWitness( ent, jutsu ); // a Copy Sharingan watching takes it
	}
	Shinobi_Debug( "%i casts %s Lv%i: %s (chakra %i)", ent->s.number, shinobiJutsuTitles[jutsu], lvl,
		cast ? "ok" : "FAILED", client->ps.fd.forcePower );
	return cast;
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

// "<jutsu name> [level]" for any jutsu without a command of its own
qboolean Shinobi_JutsuCommand( gentity_t *ent, const char *cmd ) {
	int j;
	if ( !ent || !ent->client || ent->health <= 0 )
		return qfalse;
	for ( j = SJ_NONE + 1; j < SJ_COUNT; j++ ) {
		if ( !Q_stricmp( cmd, shinobiJutsuNames[j] ) ) {
			Shinobi_CastCommand( ent, j );
			return qtrue;
		}
	}
	return qfalse;
}

// "chakra" (cheat): a full chakra pool, for tests
void Cmd_Chakra_f( gentity_t *ent ) {
	ent->client->ps.fd.forcePower = ent->client->ps.fd.forcePowerMax;
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
void Cmd_Kirigakure_f( gentity_t *ent )  { Shinobi_CastCommand( ent, SJ_MIST ); }
void Cmd_Jishin_f( gentity_t *ent )      { Shinobi_CastCommand( ent, SJ_QUAKE ); }
void Cmd_Kazekabe_f( gentity_t *ent )    { Shinobi_CastCommand( ent, SJ_WINDWALL ); }
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

	if ( client->shinobi.blocking )
		return; // guard up: no seals
	if ( client->shinobi.flinchEnd > level.time && jutsu != SJ_KAWARIMI )
		return; // flinching from a combo punch
	if ( SHINOBI_JUTSU_INSTANT( jutsu ) ) {
		Shinobi_Cast( ent, jutsu, 1 );
		return;
	}
	if ( client->shinobi.shadowBy || client->shinobi.prisonCaster || client->shinobi.shadowTrapEnt )
		return; // bound or imprisoned: no seals
	if ( !Shinobi_HasJutsu( ent, jutsu ) ) {
		trap->SendServerCommand( ent - g_entities, va( "cp \"%s is not in your build\"", shinobiJutsuTitles[jutsu] ) );
		return;
	}
	if ( jutsu == SJ_DOTON && !client->shinobi.prisonCaster && Shinobi_PushWall( ent ) )
		return; // our wall was right in front: F shoves it forward instead of raising a new one
	if ( SHINOBI_MODIFIER( &client->pers.cmd ) && !SHINOBI_JUTSU_HOLD( jutsu ) && jutsu != SJ_SHUNSHIN ) {
		// Shift + the key: no charging - the first level at once, for the price of the top one
		// (holding back too: its alternative form)
		client->shinobi.castBack = client->pers.cmd.forwardmove < 0;
		client->shinobi.costLevel = SHINOBI_MAX_LEVEL;
		Shinobi_Cast( ent, jutsu, SHINOBI_JUTSU_MIN_LEVEL( jutsu ) );
		client->shinobi.costLevel = 0;
		return;
	}
	if ( SHINOBI_JUTSU_HOLD( jutsu ) ) { // starts now, lasts until the key comes up
		Shinobi_Cast( ent, jutsu, 1 );
		if ( ( jutsu == SJ_SUIRO && client->shinobi.prisonTarget ) || ( jutsu == SJ_FLAME && client->shinobi.flameEnt )
			|| ( jutsu == SJ_SHADOW && client->shinobi.shadowEnt ) || ( jutsu == SJ_BURROW && client->shinobi.burrowEnd )
			|| ( jutsu == SJ_SHADOWHIDE && client->shinobi.shadowHideEnd ) || ( jutsu == SJ_RAIDASH && client->shinobi.raiDashEnd )
			|| ( jutsu == SJ_HEAL && client->shinobi.healing ) || ( jutsu == SJ_KAITEN && client->shinobi.kaitenStart )
			|| ( jutsu == SJ_PALMS && client->shinobi.palmsStart ) )
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
		else if ( jutsu == SJ_BURROW && client->shinobi.burrowEnd )
			Shinobi_BurrowEnd( ent, qtrue );
		else if ( jutsu == SJ_SHADOWHIDE && client->shinobi.shadowHideEnd )
			Shinobi_ShadowHideEnd( ent );
		else if ( jutsu == SJ_SHADOW && client->shinobi.shadowEnt )
			Shinobi_ShadowRelease( ent );
		else if ( jutsu == SJ_RAIDASH && client->shinobi.raiDashEnd )
			Shinobi_RaiDashEnd( ent );
		else if ( jutsu == SJ_HEAL && client->shinobi.healing )
			Shinobi_HealStop( ent );
		else if ( jutsu == SJ_KAITEN && client->shinobi.kaitenStart )
			Shinobi_KaitenStop( ent );
		else if ( jutsu == SJ_PALMS && client->shinobi.palmsStart )
			Shinobi_PalmsStop( ent, qfalse ); // let go before the field is full: lost
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
	client->shinobi.castBack = client->pers.cmd.forwardmove < 0; // the alternative cast (Storm Wind pulls)
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

	Shinobi_AutotestDrive( ent, ucmd ); // a test bot: the test's keys, not its AI's
	Shinobi_StripGuns( ent ); // no blasters for anyone
	if ( client->shinobi.kaitenStart ) // Kaiten: no blows, no guard while spinning
		ucmd->buttons &= ~( BUTTON_ATTACK | BUTTON_ALT_ATTACK );
	Shinobi_BotDefend( ent, ucmd ); // bots: guard, parry, Kawarimi out of combos
	BG_ShinobiBlockAttack( &client->ps, ucmd ); // block + attack = the blade's special attack, not a guard
	Shinobi_TaijutsuThink( ent ); // the diving kick's landing
	Shinobi_BladeSwingUpdate( ent ); // blades: one blow per swing
	Shinobi_BladeStance( ent ); // blades: each in its own stance
	Shinobi_SwordThink( ent ); // blade finishers: Kurosawa's extra cuts, bleeding
	Shinobi_LockThink( ent ); // lock-on: let go of a target that is gone
	if ( ent->health <= 0 || client->sess.sessionTeam == TEAM_SPECTATOR )
		Shinobi_StatusClear( ent );
	else
		Shinobi_StatusThink( ent, ucmd ); // fire, water, mud, Water Prison (may freeze ucmd)
	Shinobi_WaterWalkThink( ent ); // chakra in the feet on map water
	Shinobi_PerkThink( ent ); // eyes, gates and seal also run while bound or knocked down
	Shinobi_ChakraTextThink( ent ); // "+N chakra" for what the fight gave
	Shinobi_BlockThink( ent, ucmd );
	Shinobi_ChargesToStats( client ); // Kawarimi / Shunshin charges for the HUD
	Shinobi_EarthThink( ent, ucmd ); // burrow, stone skin
	Shinobi_YinYangThink( ent, ucmd ); // shadow hide, blindness
	Shinobi_BotThink( ent );

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
		// our own: both hands - tiger, snake, dog, ram, over and over; a single blade in the right
		// hand - the left alone (like Haku). (Each level bar filling: a gust in your own ears - cgame)
		const qboolean oneHand = client->ps.weapon == WP_SABER && client->saber[0].model[0] && !client->saber[1].model[0]
			&& client->saber[0].numBlades <= 1;
		client->ps.forceHandExtend = HANDEXTEND_TAUNT;
		client->ps.forceDodgeAnim = oneHand ? BOTH_SHINOBI_SEALS_1H : BOTH_SHINOBI_SEALS;
		client->ps.forceHandExtendTime = level.time + 150;
	}

	attackPressed = ( ucmd->buttons & BUTTON_ATTACK ) && !( client->buttons & BUTTON_ATTACK );

	// --- Chakra charging: hold USE while standing still on the ground ---
	if ( ( ucmd->buttons & BUTTON_USE ) && !client->shinobi.shadowBy && !client->shinobi.shadowTrapEnt && !client->shinobi.prisonCaster && !client->shinobi.blocking
		&& !client->shinobi.burrowEnd && !client->shinobi.shadowHideEnd
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
// charges that come back one by one: how many are ready / take one (qfalse: none)
static int Shinobi_ChargesReady( const int *ready, int n ) {
	int i, k = 0;
	for ( i = 0; i < n; i++ )
		if ( ready[i] <= level.time )
			k++;
	return k;
}

static int Shinobi_ChargeWait( const int *ready, int n ) {
	int i, best = 0;
	for ( i = 0; i < n; i++ )
		if ( ready[i] > level.time && ( !best || ready[i] < best ) )
			best = ready[i];
	return best ? best - level.time : 0;
}

static qboolean Shinobi_ChargeTake( int *ready, int n, int recharge ) {
	int i;
	for ( i = 0; i < n; i++ ) {
		if ( ready[i] <= level.time ) {
			ready[i] = level.time + recharge;
			return qtrue;
		}
	}
	return qfalse;
}

// the charges the HUD shows
void Shinobi_ChargesToStats( gclient_t *client ) {
	const int k = Shinobi_ChargesReady( client->shinobi.kawarimiReady, SHINOBI_KAWARIMI_CHARGES );
	const int s = Shinobi_ChargesReady( client->shinobi.shunshinReady, SHINOBI_SHUNSHIN_CHARGES );
	client->ps.stats[STAT_SHINOBI2] = ( client->ps.stats[STAT_SHINOBI2] & ~( ( 3 << SHINOBI_STAT2_KAWA_SHIFT ) | ( 3 << SHINOBI_STAT2_SHUN_SHIFT ) ) )
		| ( k << SHINOBI_STAT2_KAWA_SHIFT ) | ( s << SHINOBI_STAT2_SHUN_SHIFT );
	client->ps.stats[STAT_SHINOBI2] = ( client->ps.stats[STAT_SHINOBI2] & ~( 3 << SHINOBI_STAT2_FIST_SHIFT ) )
		| ( ( client->pers.shinobiFistStyle % SFS_COUNT ) << SHINOBI_STAT2_FIST_SHIFT );
}

static qboolean Shinobi_CastShunshin( gentity_t *ent, int lvl, qboolean back ) {
	gclient_t   *client = ent->client;
	const float  range = (float)shinobiShunshinDist[lvl - 1];
	vec3_t       angles, dir, start, end;
	trace_t      tr;
	float        dist;

	if ( client->shinobi.gate < 4 && !Shinobi_ChargesReady( client->shinobi.shunshinReady, SHINOBI_SHUNSHIN_CHARGES ) ) { // (the fourth Inner Gate: no charges)
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		trap->SendServerCommand( ent - g_entities, va( "cp \"Shunshin ready in %.1f s\"",
			Shinobi_ChargeWait( client->shinobi.shunshinReady, SHINOBI_SHUNSHIN_CHARGES ) / 1000.0f ) );
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

	if ( client->shinobi.gate < 4 ) // (the fourth Inner Gate: the blink is free)
		Shinobi_ChargeTake( client->shinobi.shunshinReady, SHINOBI_SHUNSHIN_CHARGES, SHINOBI_SHUNSHIN_RECHARGE );
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

// solid again (as soon as we are not standing inside someone)
void Shinobi_RestoreSolid( gentity_t *ent ) {
	if ( ent->health <= 0 )
		return;
	ent->client->shinobi.kawarimiPhase = Shinobi_InsideBody( ent );
	Shinobi_SetSolid( ent, !ent->client->shinobi.kawarimiPhase );
}

qboolean Shinobi_CastKawarimi( gentity_t *ent ) {
	gclient_t *client = ent->client;
	gentity_t *decoy;

	if ( client->shinobi.kawarimiEnd || client->shinobi.kawarimiPending )
		return qfalse; // already substituted
	if ( client->shinobi.burrowEnd || client->shinobi.shadowHideEnd )
		return qfalse; // under the ground / in the shadow
	if ( !Shinobi_ChargesReady( client->shinobi.kawarimiReady, SHINOBI_KAWARIMI_CHARGES ) ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		trap->SendServerCommand( ent - g_entities, va( "cp \"Kawarimi ready in %.1f s\"",
			Shinobi_ChargeWait( client->shinobi.kawarimiReady, SHINOBI_KAWARIMI_CHARGES ) / 1000.0f ) );
		return qfalse;
	}

	// substitution is the way out of a Water Prison or a Shadow Bind (no gasping afterwards)
	if ( client->shinobi.prisonCaster )
		Shinobi_PrisonRelease( ent );
	if ( client->shinobi.shadowBy )
		Shinobi_ShadowRelease( ent );
	if ( client->shinobi.shadowTrapEnt )
		Shinobi_ShadowTrapRelease( ent );
	client->shinobi.stunEnd = 0;
	client->shinobi.flinchEnd = client->shinobi.juggleEnd = client->shinobi.juggleBy = 0; // out of a combo too
	if ( client->ps.forceHandExtend == HANDEXTEND_KNOCKDOWN ) { // off the floor: the double lies there instead
		client->ps.forceHandExtend = HANDEXTEND_NONE;
		client->ps.forceHandExtendTime = 0;
		client->ps.legsTimer = client->ps.torsoTimer = 0;
	}

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
	// whatever was held at the moment (a combo's attack, the guard) doesn't count as "an action" - only a new press
	client->shinobi.kawarimiHeld = client->pers.cmd.buttons | ( client->pers.cmd.upmove > 0 ? ( 1 << 30 ) : 0 );
	client->shinobi.kawarimiPending = qtrue;
	client->shinobi.kawarimiPhase = qtrue;
	Shinobi_ChargeTake( client->shinobi.kawarimiReady, SHINOBI_KAWARIMI_CHARGES, SHINOBI_KAWARIMI_RECHARGE );
	return qtrue;
}

// the double is in place: vanish
void Shinobi_Hide( gentity_t *ent ) {
	gclient_t *client = ent->client;

	client->shinobi.kawarimiPending = qfalse;
	client->shinobi.kawarimiEnd = level.time + SHINOBI_KAWARIMI_MAX_MS;
	shinobiTest.kawarimiHides++;
	SHINOBI_SET_HIDDEN( &client->ps, 1 );
	{ // already unseen, a hop back from where the double stands
		vec3_t  yaw, fwd, start, end;
		trace_t tr;
		VectorSet( yaw, 0, client->ps.viewangles[YAW], 0 );
		AngleVectors( yaw, fwd, NULL, NULL );
		VectorCopy( client->ps.origin, start );
		start[2] += 1;
		VectorMA( start, -SHINOBI_KAWARIMI_HOP, fwd, end );
		trap->Trace( &tr, start, ent->r.mins, ent->r.maxs, end, ent->s.number, MASK_PLAYERSOLID & ~CONTENTS_BODY, qfalse, 0, 0 );
		if ( !tr.startsolid && !tr.allsolid && tr.fraction > 0.2f )
			Shinobi_TeleportClient( ent, tr.endpos );
	}
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

// the seals of a jutsu being charged are broken (a held jutsu already going is not)
void Shinobi_InterruptCharge( gentity_t *ent ) {
	gclient_t *client = ent->client;

	if ( !client || ent->s.number >= MAX_CLIENTS || client->shinobi.chargeJutsu == SJ_NONE
		|| SHINOBI_JUTSU_HOLD( client->shinobi.chargeJutsu ) )
		return;
	client->shinobi.chargeJutsu = SJ_NONE;
	Shinobi_StopCharging( client );
	G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
	Shinobi_HitText( ent, STX_INTERRUPT, 0 );
	if ( !( ent->r.svFlags & SVF_BOT ) )
		trap->SendServerCommand( ent - g_entities, "sjc" ); // the cgame drops its charge bars
}

// a hit landed on targ: a direct one (not burning, drowning, ash, falling...) breaks his seals
void Shinobi_HitInterrupt( gentity_t *targ, gentity_t *attacker, int mod ) {
	if ( !targ || !targ->client || !attacker || attacker == targ )
		return;
	switch ( mod ) {
	case MOD_LAVA: case MOD_WATER: case MOD_SLIME: case MOD_FALLING: case MOD_TRIGGER_HURT:
	case MOD_SUICIDE: case MOD_UNKNOWN: case MOD_TELEFRAG:
		return;
	default:
		break;
	}
	Shinobi_InterruptCharge( targ );
	Shinobi_KaitenInterrupt( targ ); // caught winding up a Kaiten
}

void Shinobi_Damaged( gentity_t *targ, gentity_t *attacker ) {
	if ( !targ || !targ->client || targ->s.number >= MAX_CLIENTS )
		return;
	Shinobi_Reveal( targ );
	if ( targ->client->shinobi.shadowTrapEnt )
		Shinobi_ShadowTrapRelease( targ ); // any hit breaks a Shadow Trap
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

	{ // keys still held from before the substitution: ignored (and kept from the body) until let go
		const int now = ucmd->buttons | ( ucmd->upmove > 0 ? ( 1 << 30 ) : 0 );
		client->shinobi.kawarimiHeld &= now;
		ucmd->buttons &= ~( client->shinobi.kawarimiHeld & actions );
		if ( client->shinobi.kawarimiHeld & ( 1 << 30 ) )
			ucmd->upmove = 0;
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

/*
==============
Player size

models/players/<model>/scale.cfg ("scale 85") makes a character smaller (or bigger):
the model is drawn scaled (ps.iModelScale, which the cgame already honours, feet kept on
the floor), and the hitbox, the eyes, the ghoul2 traces and the bolts follow. Read once
per model name.
==============
*/
#define SHINOBI_SIZE_CACHE 64
static struct { char model[MAX_QPATH]; int pct; } shinobiSizes[SHINOBI_SIZE_CACHE];
static int shinobiNumSizes;

int Shinobi_ModelSize( const char *model )
{
	char dir[MAX_QPATH], path[MAX_QPATH], buf[512];
	char *slash;
	const char *p, *tok;
	fileHandle_t f;
	int i, len, pct = 100;

	if ( !model || !model[0] )
		return 100;
	Q_strncpyz( dir, model, sizeof( dir ) );
	if ( ( slash = strchr( dir, '/' ) ) != NULL )
		*slash = 0;
	for ( i = 0; i < shinobiNumSizes; i++ )
		if ( !Q_stricmp( shinobiSizes[i].model, dir ) )
			return shinobiSizes[i].pct;

	Com_sprintf( path, sizeof( path ), "models/players/%s/scale.cfg", dir );
	len = trap->FS_Open( path, &f, FS_READ );
	if ( f ) {
		if ( len > 0 ) {
			if ( len > (int)sizeof( buf ) - 1 )
				len = sizeof( buf ) - 1;
			trap->FS_Read( buf, len, f );
			buf[len] = 0;
			p = buf;
			COM_BeginParseSession( "Shinobi_ModelSize" );
			while ( ( tok = COM_ParseExt( &p, qtrue ) ) != NULL && tok[0] ) {
				if ( !Q_stricmp( tok, "scale" ) ) {
					tok = COM_ParseExt( &p, qfalse );
					if ( tok && tok[0] )
						pct = atoi( tok );
					break;
				}
			}
		}
		trap->FS_Close( f );
	}
	if ( pct < SHINOBI_SIZE_MIN || pct > SHINOBI_SIZE_MAX )
		pct = 100;
	if ( shinobiNumSizes < SHINOBI_SIZE_CACHE ) {
		Q_strncpyz( shinobiSizes[shinobiNumSizes].model, dir, sizeof( shinobiSizes[0].model ) );
		shinobiSizes[shinobiNumSizes].pct = pct;
		shinobiNumSizes++;
	}
	if ( pct != 100 )
		Com_Printf( "[shinobi] model %s: size %d%%\n", dir, pct );
	return pct;
}

// set an entity (player or clone) to a size in % of the stock one
void Shinobi_SetSize( gentity_t *ent, int pct )
{
	gclient_t *cl = ent->client;
	float f, w;

	if ( !cl )
		return;
	if ( pct < SHINOBI_SIZE_MIN || pct > SHINOBI_SIZE_MAX )
		pct = 100;
	cl->ps.iModelScale = ent->s.iModelScale = ( pct == 100 ) ? 0 : pct;
	f = pct / 100.0f;
	if ( pct == 100 )
		VectorClear( ent->modelScale );
	else
		VectorSet( ent->modelScale, f, f, f );
	cl->ps.standheight = BG_ShinobiSized( &cl->ps, DEFAULT_MAXS_2 );
	cl->ps.crouchheight = BG_ShinobiSized( &cl->ps, CROUCH_MAXS_2 );
	if ( ent->s.number < MAX_CLIENTS ) {
		w = 15.0f * f;
		VectorSet( ent->r.mins, -w, -w, DEFAULT_MINS_2 );
		ent->r.maxs[0] = ent->r.maxs[1] = w;
	}
	ent->r.maxs[2] = ( cl->ps.pm_flags & PMF_DUCKED ) ? cl->ps.crouchheight : cl->ps.standheight;
	cl->ps.viewheight = BG_ShinobiSized( &cl->ps, ( cl->ps.pm_flags & PMF_DUCKED ) ? CROUCH_VIEWHEIGHT : DEFAULT_VIEWHEIGHT );
}

// a player's size from the model in his userinfo (model NULL: read the userinfo)
void Shinobi_ApplySize( gentity_t *ent, const char *model )
{
	char userinfo[MAX_INFO_STRING];

	if ( !ent->client )
		return;
	if ( !model ) {
		trap->GetUserinfo( ent->s.number, userinfo, sizeof( userinfo ) );
		model = Info_ValueForKey( userinfo, "model" );
	}
	Shinobi_SetSize( ent, Shinobi_ModelSize( model ) );
}

/*
===========================================================================
Lock-on (see bg_shinobi.h): by itself in close combat, "lockon" (T) the next
one close by; Pmove keeps the view on him (PM_ShinobiLockOn).
===========================================================================
*/
static qboolean Shinobi_LockTarget( gentity_t *ent, gentity_t *t ) {
	if ( !t || t == ent || !t->inuse || !t->client || t->health <= 0 || !t->takedamage )
		return qfalse;
	if ( t->s.number < MAX_CLIENTS && ( t->client->pers.connected != CON_CONNECTED
		|| t->client->sess.sessionTeam == TEAM_SPECTATOR || SHINOBI_UNSEEN( &t->client->ps ) ) )
		return qfalse;
	if ( t->s.number >= MAX_CLIENTS && !t->shinobiCloneOwner )
		return qfalse; // (other NPCs: not here)
	if ( t->shinobiCloneOwner && Shinobi_CloneOwner( t ) == ent )
		return qfalse; // our own clone
	if ( level.gametype >= GT_TEAM && t->s.number < MAX_CLIENTS && OnSameTeam( ent, t ) )
		return qfalse;
	return qtrue;
}

static qboolean Shinobi_LockSees( gentity_t *ent, gentity_t *t ) {
	trace_t tr;
	vec3_t  eye, at;
	VectorCopy( ent->client->ps.origin, eye );
	eye[2] += ent->client->ps.viewheight;
	VectorCopy( t->r.currentOrigin, at );
	at[2] += 8;
	trap->Trace( &tr, eye, NULL, NULL, at, ent->s.number, MASK_SOLID, qfalse, 0, 0 );
	return tr.fraction >= 1.0f;
}

// close combat: fists or a blade in hand (with a tool you aim yourself), and eyes that see:
// not blinded, not in a mist or an ash cloud (the Byakugan sees through those)
static qboolean Shinobi_LockHands( gentity_t *ent ) {
	gclient_t *c = ent->client;
	const int  w = c->ps.weapon;
	if ( !( w == WP_MELEE || w == WP_SABER ) || c->ps.pm_type != PM_NORMAL )
		return qfalse;
	if ( c->shinobi.blindEnd > level.time )
		return qfalse;
	if ( ( c->shinobi.mistEnd > level.time || c->shinobi.ashEnd > level.time ) && c->shinobi.eyes != SP_BYAKUGAN )
		return qfalse;
	return qtrue;
}

/*
Who could be locked on: in plain sight within range; in front (or close by anywhere).
Their angle off the view (-180..180, right is negative) for the T cycle; the best is
the one nearest the aim (then the nearest).
*/
static int Shinobi_LockCandidates( gentity_t *ent, gentity_t **list, float *yaw, int max ) {
	gclient_t *c = ent->client;
	vec3_t     fwd, eye, ang;
	int        i, n = 0;

	VectorSet( ang, 0, c->ps.viewangles[YAW], 0 );
	AngleVectors( ang, fwd, NULL, NULL );
	VectorCopy( c->ps.origin, eye );
	eye[2] += c->ps.viewheight;
	for ( i = 0; i < level.num_entities && n < max; i++ ) {
		gentity_t *t = &g_entities[i];
		vec3_t     d;
		float      dist;
		if ( !Shinobi_LockTarget( ent, t ) )
			continue;
		VectorSubtract( t->r.currentOrigin, eye, d );
		d[2] = 0;
		dist = VectorNormalize( d );
		if ( dist > SHINOBI_LOCK_RANGE )
			continue;
		if ( DotProduct( d, fwd ) < SHINOBI_LOCK_FOV && dist > 150 )
			continue;
		if ( !Shinobi_LockSees( ent, t ) )
			continue;
		list[n] = t;
		yaw[n] = AngleNormalize180( vectoyaw( d ) - c->ps.viewangles[YAW] );
		n++;
	}
	return n;
}

static void Shinobi_LockSet( gentity_t *ent, gentity_t *t ) {
	ent->client->ps.stats[STAT_SHINOBI_LOCK] = t ? t->s.number + 1 : 0;
	ent->client->shinobi.lockSeen = level.time;
}

// T: the next one close by (to the right of the one locked, round again); nothing locked: the best
void Cmd_LockOn_f( gentity_t *ent ) {
	gclient_t *c = ent->client;
	gentity_t *list[MAX_CLIENTS], *pick = NULL;
	float      yaw[MAX_CLIENTS], curYaw = 0, bestGap = 999;
	const int  cur = c->ps.stats[STAT_SHINOBI_LOCK] - 1;
	int        i, n;

	if ( !Shinobi_LockHands( ent ) ) {
		trap->SendServerCommand( ent - g_entities, "cp \"You can't lock on now\"" );
		return;
	}
	n = Shinobi_LockCandidates( ent, list, yaw, MAX_CLIENTS );
	for ( i = 0; i < n; i++ )
		if ( list[i]->s.number == cur )
			curYaw = yaw[i];
	for ( i = 0; i < n; i++ ) { // the next one clockwise (to the right) from the locked one
		float gap;
		if ( list[i]->s.number == cur )
			continue;
		gap = curYaw - yaw[i];
		if ( cur < 0 )
			gap = fabs( yaw[i] ); // none locked: the one nearest the aim
		else if ( gap <= 0 )
			gap += 360;
		if ( gap < bestGap ) {
			bestGap = gap;
			pick = list[i];
		}
	}
	if ( !pick ) {
		trap->SendServerCommand( ent - g_entities, cur >= 0 ? "cp \"No one else close\"" : "cp \"No one close to lock on to\"" );
		return;
	}
	Shinobi_LockSet( ent, pick );
}

void Shinobi_LockThink( gentity_t *ent ) {
	gclient_t *c = ent->client;
	const int  n = c->ps.stats[STAT_SHINOBI_LOCK] - 1;
	gentity_t *t;

	if ( n >= 0 ) {
		t = &g_entities[n];
		if ( n < ENTITYNUM_WORLD && t->inuse && t->client && t->s.number < MAX_CLIENTS && t->health > 0
			&& SHINOBI_UNSEEN( &t->client->ps ) )
			c->shinobi.lockNoAuto = level.time + SHINOBI_LOCK_HIDE_MS; // he vanished: lost him
		if ( ent->health <= 0 || n >= ENTITYNUM_WORLD || !Shinobi_LockTarget( ent, t ) || !Shinobi_LockHands( ent )
			|| Distance( t->r.currentOrigin, c->ps.origin ) > SHINOBI_LOCK_KEEP ) {
			Shinobi_LockSet( ent, NULL );
			return;
		}
		if ( Shinobi_LockSees( ent, t ) )
			c->shinobi.lockSeen = level.time;
		else if ( level.time - c->shinobi.lockSeen > SHINOBI_LOCK_LOST_MS )
			Shinobi_LockSet( ent, NULL );
		return;
	}
	// nothing locked: close combat finds its own (not bots - their AI aims)
	if ( shinobi_autolock.integer && ent->health > 0 && !( ent->r.svFlags & SVF_BOT ) && Shinobi_LockHands( ent )
		&& level.time >= c->shinobi.lockNoAuto && c->sess.sessionTeam != TEAM_SPECTATOR ) {
		gentity_t *list[MAX_CLIENTS], *best = NULL;
		float      yaw[MAX_CLIENTS], bestScore = -999;
		int        i, k = Shinobi_LockCandidates( ent, list, yaw, MAX_CLIENTS );
		for ( i = 0; i < k; i++ ) {
			const float score = -fabs( yaw[i] ) / 90.0f - Distance( list[i]->r.currentOrigin, c->ps.origin ) / SHINOBI_LOCK_RANGE;
			if ( score > bestScore ) {
				bestScore = score;
				best = list[i];
			}
		}
		if ( best )
			Shinobi_LockSet( ent, best );
	}
}
