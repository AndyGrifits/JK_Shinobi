/*
===========================================================================
JK_Shinobi - taijutsu combo, the server half (the moves themselves run in
Pmove: PM_ShinobiComboPre / PM_ShinobiMeleeCombo in bg_pmove.c).

Punches hit through WP_FireMelee, kicks through the stock kick traces
(G_KickTrace); both call Shinobi_ComboHit afterwards to add what the move does
to whoever it hit. The diving kick's landing is resolved here.
===========================================================================
*/
#include "g_shinobi_local.h"

extern void G_Knockdown( gentity_t *victim ); // g_combat.c
extern qboolean BG_InKnockDown( int anim ); // bg_pmove.c

static qboolean Shinobi_ComboTarget( gentity_t *ent, gentity_t *v ) {
	if ( !v || !v->inuse || !v->client || v == ent || v->health <= 0 || !v->takedamage )
		return qfalse;
	if ( v->s.number < MAX_CLIENTS && v->client->sess.sessionTeam == TEAM_SPECTATOR )
		return qfalse;
	if ( Shinobi_DamageBlocked( v, ent ) || ( level.gametype >= GT_TEAM && OnSameTeam( ent, v ) ) )
		return qfalse;
	return qtrue;
}

static void Shinobi_ComboPush( gentity_t *v, const vec3_t dir, float speed, float up ) {
	vec3_t d;
	VectorCopy( dir, d );
	d[2] = 0;
	if ( VectorNormalize( d ) < 0.01f )
		return;
	VectorScale( d, speed, v->client->ps.velocity );
	v->client->ps.velocity[2] = up;
	v->client->ps.groundEntityNum = ENTITYNUM_NONE;
}

// after a combo hit landed on v (damage already dealt)
void Shinobi_ComboHit( gentity_t *ent, gentity_t *v ) {
	gclient_t *client = ent->client;
	vec3_t     dir;
	int        kind;

	if ( !client || ent->s.number >= MAX_CLIENTS || client->ps.weapon != WP_MELEE )
		return;
	if ( !Shinobi_ComboTarget( ent, v ) )
		return;
	if ( shinobiBlockResult == SBR_BLOCKED || shinobiBlockResult == SBR_PARRIED ) {
		client->ps.stats[STAT_SHINOBI_COMBO] = 0; // the guard stops the chain
		client->ps.stats[STAT_SHINOBI_COMBOT] = 0;
		return;
	}
	kind = SHINOBI_COMBO_KIND( &client->ps );
	VectorSubtract( v->r.currentOrigin, ent->r.currentOrigin, dir );
	v->client->ps.otherKiller = ent->s.number;
	v->client->ps.otherKillerTime = level.time + 5000;
	v->client->ps.otherKillerDebounceTime = level.time + 100;

	switch ( kind ) {
	case SCK_HIT: // a chain punch: a flinch - no hitting back before the next one (the finisher has none)
		Shinobi_Flinch( v, SHINOBI_COMBO_FLINCH_MS );
		break;
	case SCK_LAUNCH: // up into the air - jump after him and dive
		Shinobi_ComboPush( v, dir, 60, 520 );
		Shinobi_CCGrant( v, level.time + 1100 );
		break;
	case SCK_KICK: // knocked down and away
		Shinobi_ComboPush( v, dir, 480, 220 );
		if ( !Shinobi_CCImmune( v ) )
			G_Knockdown( v );
		Shinobi_CCGrant( v, level.time + 1500 );
		break;
	default:
		break;
	}
	G_Sound( v, CHAN_AUTO, G_SoundIndex( va( "sound/weapons/melee/punch%d", Q_irand( 1, 4 ) ) ) );
}

// the diving kick came down: hits everyone around the landing spot
static void Shinobi_DiveImpact( gentity_t *ent ) {
	vec3_t up = { 0, 0, 1 }, feet;
	int    i, dmg;

	VectorCopy( ent->client->ps.origin, feet );
	feet[2] += ent->r.mins[2] + 2;
	G_PlayEffectID( shinobiMedia.fxQuakeDust, feet, up );
	G_PlayEffectID( shinobiMedia.fxRockMed, feet, up );
	G_Sound( ent, CHAN_AUTO, shinobiMedia.sndWallBreak );
	dmg = SHINOBI_DIVE_DAMAGE;
	for ( i = 0; i < level.num_entities; i++ ) {
		gentity_t *v = &g_entities[i];
		vec3_t     d;

		if ( !Shinobi_ComboTarget( ent, v ) )
			continue;
		VectorSubtract( v->r.currentOrigin, ent->r.currentOrigin, d );
		if ( fabs( d[2] ) > 64 )
			continue;
		d[2] = 0;
		if ( VectorLength( d ) > SHINOBI_DIVE_RADIUS )
			continue;
		G_Damage( v, ent, ent, up, v->r.currentOrigin, dmg, DAMAGE_NO_KNOCKBACK, MOD_MELEE ); // just damage
	}
}

// the downward blow lands on whoever lies in front
static void Shinobi_GroundBlow( gentity_t *ent ) {
	vec3_t fwd, ang, down = { 0, 0, -1 };
	int    i;

	VectorSet( ang, 0, ent->client->ps.viewangles[YAW], 0 );
	AngleVectors( ang, fwd, NULL, NULL );
	for ( i = 0; i < level.num_entities; i++ ) {
		gentity_t *v = &g_entities[i];
		vec3_t     d;
		float      dist;

		if ( !Shinobi_ComboTarget( ent, v ) || !BG_InKnockDown( v->client->ps.legsAnim ) )
			continue;
		VectorSubtract( v->r.currentOrigin, ent->r.currentOrigin, d );
		if ( fabs( d[2] ) > 64 )
			continue;
		d[2] = 0;
		dist = VectorNormalize( d );
		if ( dist > SHINOBI_GROUND_RANGE + 24 || ( dist > 16 && DotProduct( d, fwd ) < 0.5f ) )
			continue;
		G_Damage( v, ent, ent, down, v->r.currentOrigin, SHINOBI_GROUND_DAMAGE, DAMAGE_NO_KNOCKBACK, MOD_MELEE );
		G_Sound( v, CHAN_AUTO, G_SoundIndex( va( "sound/weapons/melee/punch%d", Q_irand( 1, 4 ) ) ) );
	}
}

// a combo punch: its damage by the step of the chain (0: not a combo punch)
int Shinobi_ComboPunchDamage( gentity_t *ent ) {
	int hit;
	if ( !ent->client || ent->s.number >= MAX_CLIENTS || ent->client->ps.weapon != WP_MELEE
		|| SHINOBI_COMBO_KIND( &ent->client->ps ) != SCK_HIT )
		return 0;
	hit = SHINOBI_COMBO_HIT( &ent->client->ps );
	return ( hit >= 1 && hit <= 3 ) ? shinobiComboPunch[hit - 1] : 0;
}

// a combo kick (finisher): its damage (0: a stock kick)
int Shinobi_ComboKickDamage( gentity_t *ent ) {
	if ( !ent->client || ent->s.number >= MAX_CLIENTS || ent->client->ps.weapon != WP_MELEE )
		return 0;
	switch ( SHINOBI_COMBO_KIND( &ent->client->ps ) ) {
	case SCK_KICK:   return SHINOBI_COMBO_KICK_DAMAGE;
	case SCK_LAUNCH: return SHINOBI_COMBO_LAUNCH_DAMAGE;
	case SCK_SPIN:   return SHINOBI_COMBO_SPIN_DAMAGE;
	default:         return 0;
	}
}

// the spin kick (left / right finisher) only does damage: no stock kick knockdown
qboolean Shinobi_ComboPlainKick( gentity_t *ent ) {
	return ( ent->client && ent->s.number < MAX_CLIENTS && ent->client->ps.weapon == WP_MELEE
		&& SHINOBI_COMBO_KIND( &ent->client->ps ) == SCK_SPIN ) ? qtrue : qfalse;
}

// the rolling sweep (back finisher): bowls over whoever it rolls into
static void Shinobi_SweepRoll( gentity_t *ent ) {
	vec3_t up = { 0, 0, 1 };
	int    i;

	for ( i = 0; i < level.num_entities; i++ ) {
		gentity_t *v = &g_entities[i];
		vec3_t     d;

		if ( !Shinobi_ComboTarget( ent, v ) || BG_InKnockDown( v->client->ps.legsAnim )
			|| v->client->ps.forceHandExtend == HANDEXTEND_KNOCKDOWN )
			continue;
		VectorSubtract( v->r.currentOrigin, ent->r.currentOrigin, d );
		if ( fabs( d[2] ) > 48 )
			continue;
		d[2] = 0;
		if ( VectorLength( d ) > SHINOBI_SWEEP_RADIUS + 16 )
			continue;
		G_Damage( v, ent, ent, up, v->r.currentOrigin, SHINOBI_SWEEP_DAMAGE, DAMAGE_NO_KNOCKBACK, MOD_MELEE );
		if ( !v->inuse || v->health <= 0 || !v->client || shinobiBlockResult == SBR_BLOCKED || shinobiBlockResult == SBR_PARRIED )
			continue;
		VectorClear( v->client->ps.velocity );
		if ( !Shinobi_CCImmune( v ) )
			G_Knockdown( v );
		Shinobi_CCGrant( v, level.time + 1500 );
	}
}

// every client frame, before Pmove
void Shinobi_TaijutsuThink( gentity_t *ent ) {
	gclient_t *client = ent->client;

	if ( client->ps.weapon == WP_MELEE && SHINOBI_COMBO_KIND( &client->ps ) == SCK_SWEEP
		&& client->ps.legsAnim == BOTH_ROLL_F && client->ps.legsTimer > 0 && ent->health > 0 )
		Shinobi_SweepRoll( ent );

	if ( client->ps.weapon == WP_MELEE && SHINOBI_COMBO_KIND( &client->ps ) == SCK_GROUND
		&& SHINOBI_COMBO_HIT( &client->ps ) == 0 && client->ps.legsAnim == BOTH_STABDOWN
		&& client->ps.legsTimer <= BG_AnimLength( ent->localAnimIndex, BOTH_STABDOWN ) / 2
		&& ent->health > 0 ) {
		client->ps.stats[STAT_SHINOBI_COMBO] |= 1; // landed (once)
		Shinobi_GroundBlow( ent );
	}

	if ( client->ps.stats[STAT_SHINOBI2] & SHINOBI_STAT2_DIVELAND ) {
		client->ps.stats[STAT_SHINOBI2] &= ~SHINOBI_STAT2_DIVELAND;
		if ( ent->health > 0 && client->sess.sessionTeam != TEAM_SPECTATOR )
			Shinobi_DiveImpact( ent );
	}
}
