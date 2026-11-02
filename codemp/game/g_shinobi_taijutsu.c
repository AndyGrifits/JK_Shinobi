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

	switch ( kind ) { // chain hits, the ram and the launcher are plain damage
	case SCK_KICK: // knocked down and away
		Shinobi_ComboPush( v, dir, 480, 220 );
		if ( !Shinobi_CCImmune( v ) )
			G_Knockdown( v );
		Shinobi_CCGrant( v, level.time + 1500 );
		break;
	case SCK_SWEEP: // down where he stands
		VectorClear( v->client->ps.velocity );
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

// a combo kick that only does damage: the stock kick knockdown is skipped
// (only the kick and the sweep finishers knock down)
qboolean Shinobi_ComboPlainKick( gentity_t *ent ) {
	int kind;
	if ( !ent->client || ent->s.number >= MAX_CLIENTS || ent->client->ps.weapon != WP_MELEE )
		return qfalse;
	kind = SHINOBI_COMBO_KIND( &ent->client->ps );
	return ( kind != SCK_KICK && kind != SCK_SWEEP ) ? qtrue : qfalse;
}

// every client frame, before Pmove
void Shinobi_TaijutsuThink( gentity_t *ent ) {
	gclient_t *client = ent->client;

	if ( client->ps.stats[STAT_SHINOBI2] & SHINOBI_STAT2_DIVELAND ) {
		client->ps.stats[STAT_SHINOBI2] &= ~SHINOBI_STAT2_DIVELAND;
		if ( ent->health > 0 && client->sess.sessionTeam != TEAM_SPECTATOR )
			Shinobi_DiveImpact( ent );
	}
}
