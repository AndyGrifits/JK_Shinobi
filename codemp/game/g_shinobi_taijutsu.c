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

qboolean Shinobi_ComboTarget( gentity_t *ent, gentity_t *v ) {
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

/*
The fist combo counts as the blade's does (Shinobi_BladeChainAdd): each chain blow that lands
on the same one - or on his guard - within shinobiFistWindow of the last; a miss counts
nothing, a parry stops it (he staggers you). With shinobiFistChain of them the finisher is
ready (the HUD's gold pip, STAT_SHINOBI_BLADECOMBO) and waits SHINOBI_SWORD_READY_MS for it.
*/
qboolean Shinobi_FistChainLive( gclient_t *c ) {
	const int  style = SHINOBI_FIST_STYLE( &c->ps );
	gentity_t *v;
	if ( !c->shinobi.fistChain || !c->shinobi.fistChainVictim )
		return qfalse;
	if ( level.time - c->shinobi.fistChainTime > ( c->shinobi.fistChain >= shinobiFistChain[style]
		? SHINOBI_SWORD_READY_MS : shinobiFistWindow[style] ) )
		return qfalse;
	v = &g_entities[c->shinobi.fistChainVictim - 1];
	return v->inuse && v->client && v->health > 0;
}

static void Shinobi_FistChainAdd( gentity_t *ent, gentity_t *v, const char *how ) {
	gclient_t *c = ent->client;
	const int  style = SHINOBI_FIST_STYLE( &c->ps );
	if ( c->shinobi.fistChainVictim == v->s.number + 1 && Shinobi_FistChainLive( c ) )
		c->shinobi.fistChain++;
	else
		c->shinobi.fistChain = 1;
	if ( c->shinobi.fistChain > shinobiFistChain[style] )
		c->shinobi.fistChain = shinobiFistChain[style]; // (ready: the next attack is the finisher)
	c->shinobi.fistChainVictim = v->s.number + 1;
	c->shinobi.fistChainTime = level.time;
	Shinobi_Debug( "fists %i -> %i: %s %i of %i", ent->s.number, v->s.number, how, c->shinobi.fistChain, shinobiFistChain[style] );
}

// every frame: a finisher begun (Pmove) uses the combo up, as the blade's finisher swing does
static void Shinobi_FistChainSpent( gclient_t *c ) {
	const int cs = c->ps.stats[STAT_SHINOBI_COMBO], kind = ( cs >> 3 ) & 7;
	if ( cs != c->shinobi.fistComboSeen && !( cs & SHINOBI_COMBO_AIR )
		&& ( kind == SCK_KICK || kind == SCK_LAUNCH || kind == SCK_SPIN || kind == SCK_SWEEP ) )
		c->shinobi.fistChain = 0;
	c->shinobi.fistComboSeen = cs;
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
	kind = SHINOBI_COMBO_KIND( &client->ps );
	if ( shinobiBlockResult == SBR_PARRIED ) {
		client->ps.stats[STAT_SHINOBI_COMBO] = 0; // a parry stops the chain (and staggers you)
		client->ps.stats[STAT_SHINOBI_COMBOT] = 0;
		client->shinobi.fistChain = 0;
		return;
	}
	if ( shinobiBlockResult == SBR_BLOCKED ) { // the guard held: no effect, but it counts for the combo (as with a blade)
		if ( kind == SCK_HIT && !( client->ps.stats[STAT_SHINOBI_COMBO] & SHINOBI_COMBO_AIR ) )
			Shinobi_FistChainAdd( ent, v, "blocked blow" );
		return;
	}
	VectorSubtract( v->r.currentOrigin, ent->r.currentOrigin, dir );
	v->client->ps.otherKiller = ent->s.number;
	v->client->ps.otherKillerTime = level.time + 5000;
	v->client->ps.otherKillerDebounceTime = level.time + 100;

	switch ( kind ) {
	case SCK_HIT: { // a chain punch: held until the next one can land (the finisher has none)
		int ms = client->ps.weaponTime + SHINOBI_COMBO_FLINCH_MS;
		if ( !( client->ps.stats[STAT_SHINOBI_COMBO] & SHINOBI_COMBO_AIR ) )
			Shinobi_FistChainAdd( ent, v, "blow" );
		if ( ( client->ps.stats[STAT_SHINOBI_COMBO] & SHINOBI_COMBO_AIR ) && v->client->shinobi.juggleEnd > level.time ) {
			shinobiTest.airHits++;
			// up in the air: the punch keeps him up there
			if ( v->client->ps.velocity[2] < SHINOBI_AIR_LIFT )
				v->client->ps.velocity[2] = SHINOBI_AIR_LIFT;
			v->client->shinobi.juggleEnd = level.time + 1500;
		}
		if ( ms < SHINOBI_COMBO_FLINCH_MIN )
			ms = SHINOBI_COMBO_FLINCH_MIN;
		if ( ms > SHINOBI_COMBO_FLINCH_MAX )
			ms = SHINOBI_COMBO_FLINCH_MAX;
		Shinobi_Flinch( v, ms );
		break;
	}
	case SCK_LAUNCH: // up into the air - jump after him (the air combo) or dive
		shinobiTest.launches++;
		Shinobi_GainChakra( ent, SHINOBI_GAIN_COMBO, qtrue );
		v->client->shinobi.juggleStart = level.time;
		v->client->shinobi.juggleEnd = level.time + 2500;
		v->client->shinobi.juggleBy = ent->s.number + 1;
		Shinobi_HitText( v, STX_LAUNCH, 0 );
		Shinobi_ComboPush( v, dir, 60, 520 );
		Shinobi_CCGrant( v, level.time + 1100 );
		break;
	case SCK_SPIN:
		Shinobi_GainChakra( ent, SHINOBI_GAIN_COMBO, qtrue );
		break;
	case SCK_KICK: // knocked down and away
		Shinobi_GainChakra( ent, SHINOBI_GAIN_COMBO, qtrue );
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
	vec3_t         up = { 0, 0, 1 }, feet;
	int            i, dmg;
	const qboolean sword = ent->client->ps.weapon == WP_SABER;

	VectorCopy( ent->client->ps.origin, feet );
	feet[2] += ent->r.mins[2] + 2;
	G_PlayEffectID( shinobiMedia.fxQuakeDust, feet, up );
	G_PlayEffectID( shinobiMedia.fxRockMed, feet, up );
	G_Sound( ent, CHAN_AUTO, shinobiMedia.sndWallBreak );
	dmg = SHINOBI_DIVE_DAMAGE;
	if ( sword ) { // the blade's plunge: the stance's blow, a little more
		dmg = (int)( shinobiSwordDamage[Shinobi_BladeStylePublic( ent )] * SHINOBI_SWORD_DIVE_SCALE + 0.5f );
		shinobiTest.swordDives++;
	}
	for ( i = 0; i < level.num_entities; i++ ) {
		gentity_t *v = &g_entities[i];
		vec3_t     d;

		if ( !Shinobi_ComboTarget( ent, v ) )
			continue;
		VectorSubtract( v->r.currentOrigin, ent->r.currentOrigin, d );
		if ( fabs( d[2] ) > 64 )
			continue;
		d[2] = 0;
		if ( VectorLength( d ) > ( sword ? SHINOBI_SWORD_DIVE_RADIUS : SHINOBI_DIVE_RADIUS ) )
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

// L with fists: the next taijutsu style (kept in the stats, for prediction and the HUD)
void Shinobi_CycleFistStyle( gentity_t *ent ) {
	gclient_t *c = ent->client;
	const int  next = ( SHINOBI_FIST_STYLE( &c->ps ) + 1 ) % SFS_COUNT;

	if ( c->ps.weaponTime > 0 || ( SHINOBI_COMBO_KIND( &c->ps ) != SCK_NONE && c->ps.stats[STAT_SHINOBI_COMBOT] > 0 ) )
		return; // not in the middle of a chain
	c->pers.shinobiFistStyle = next; // (kept over respawns; Shinobi_ChargesToStats puts it in the stats)
	c->ps.stats[STAT_SHINOBI2] = ( c->ps.stats[STAT_SHINOBI2] & ~( 3 << SHINOBI_STAT2_FIST_SHIFT ) ) | ( next << SHINOBI_STAT2_FIST_SHIFT );
	trap->SendServerCommand( ent - g_entities, va( "cp \"Taijutsu: %s\"", shinobiFistStyleNames[next] ) );
}

// a combo punch: its damage by the step of the chain (0: not a combo punch)
int Shinobi_ComboPunchDamage( gentity_t *ent ) {
	int hit;
	if ( !ent->client || ent->s.number >= MAX_CLIENTS || ent->client->ps.weapon != WP_MELEE
		|| SHINOBI_COMBO_KIND( &ent->client->ps ) != SCK_HIT )
		return 0;
	hit = SHINOBI_COMBO_HIT( &ent->client->ps );
	return ( hit >= 1 && hit <= 3 ) ? shinobiFistHit[SHINOBI_FIST_STYLE( &ent->client->ps )][hit - 1] : 0;
}

// a combo kick (finisher): its damage (0: a stock kick)
int Shinobi_ComboKickDamage( gentity_t *ent ) {
	float f;
	if ( !ent->client || ent->s.number >= MAX_CLIENTS || ent->client->ps.weapon != WP_MELEE )
		return 0;
	f = shinobiFistFinisher[SHINOBI_FIST_STYLE( &ent->client->ps )];
	switch ( SHINOBI_COMBO_KIND( &ent->client->ps ) ) {
	case SCK_HIT:    return Shinobi_ComboPunchDamage( ent ); // a kick or a ram of the chain (medium / strong)
	case SCK_KICK:   return (int)( SHINOBI_COMBO_KICK_DAMAGE * f + 0.5f );
	case SCK_LAUNCH: return (int)( SHINOBI_COMBO_LAUNCH_DAMAGE * f + 0.5f );
	case SCK_SPIN:   return (int)( SHINOBI_COMBO_SPIN_DAMAGE * f + 0.5f );
	default:         return 0;
	}
}

// the spin kick (left / right finisher) only does damage: no stock kick knockdown
qboolean Shinobi_ComboPlainKick( gentity_t *ent ) {
	return ( ent->client && ent->s.number < MAX_CLIENTS && ent->client->ps.weapon == WP_MELEE
		&& ( SHINOBI_COMBO_KIND( &ent->client->ps ) == SCK_SPIN || SHINOBI_COMBO_KIND( &ent->client->ps ) == SCK_HIT ) ) ? qtrue : qfalse;
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
		G_Damage( v, ent, ent, up, v->r.currentOrigin, (int)( SHINOBI_SWEEP_DAMAGE * shinobiFistFinisher[SHINOBI_FIST_STYLE( &ent->client->ps )] + 0.5f ),
			DAMAGE_NO_KNOCKBACK, MOD_MELEE );
		if ( !v->inuse || v->health <= 0 || !v->client || shinobiBlockResult == SBR_BLOCKED || shinobiBlockResult == SBR_PARRIED )
			continue;
		VectorClear( v->client->ps.velocity );
		if ( !Shinobi_CCImmune( v ) )
			G_Knockdown( v );
		Shinobi_CCGrant( v, level.time + 1500 );
		Shinobi_GainChakra( ent, SHINOBI_GAIN_COMBO, qtrue );
	}
}

// the air combo's diving kick: whoever we keep up there is slammed down
static void Shinobi_AirSlam( gentity_t *ent ) {
	int i, dmg;

	if ( ent->client->ps.weapon == WP_SABER ) { // the blade's plunge onto whoever we launched
		dmg = (int)( shinobiSwordDamage[Shinobi_BladeStylePublic( ent )] * SHINOBI_SWORD_FINISH + 0.5f );
	} else
		dmg = (int)( SHINOBI_SLAM_DAMAGE * shinobiFistFinisher[SHINOBI_FIST_STYLE( &ent->client->ps )] + 0.5f );

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		gentity_t *v = &g_entities[i];
		vec3_t     dir;
		if ( !v->inuse || !v->client || v->health <= 0 || v->client->shinobi.juggleBy != ent->s.number + 1
			|| v->client->shinobi.juggleEnd <= level.time
			|| Distance( v->r.currentOrigin, ent->r.currentOrigin ) > SHINOBI_SLAM_REACH )
			continue;
		VectorSubtract( v->r.currentOrigin, ent->r.currentOrigin, dir );
		VectorNormalize( dir );
		v->client->shinobi.juggleEnd = 0;
		v->client->shinobi.juggleBy = 0;
		v->client->shinobi.slamNoFall = level.time + 2000; // the slam hurts, not the floor
		shinobiTest.slams++;
		G_Damage( v, ent, ent, dir, v->r.currentOrigin, dmg, DAMAGE_NO_KNOCKBACK, MOD_MELEE );
		if ( !v->inuse || v->health <= 0 )
			continue;
		v->client->ps.velocity[0] = dir[0] * 120;
		v->client->ps.velocity[1] = dir[1] * 120;
		v->client->ps.velocity[2] = -900;
		G_Knockdown( v );
		Shinobi_GainChakra( ent, SHINOBI_GAIN_COMBO, qtrue );
		G_Sound( v, CHAN_AUTO, shinobiMedia.sndWallBreak );
	}
}

// every client frame, before Pmove
void Shinobi_TaijutsuThink( gentity_t *ent ) {
	gclient_t *client = ent->client;

	Shinobi_FistChainSpent( client );

	// launched: helpless until we land (a flinch that lasts); the air combo's slam
	if ( client->shinobi.juggleEnd ) {
		if ( client->shinobi.juggleEnd <= level.time || ent->health <= 0
			|| ( client->ps.groundEntityNum != ENTITYNUM_NONE && level.time - client->shinobi.juggleStart > 300 ) )
			client->shinobi.juggleEnd = client->shinobi.juggleBy = 0;
		else if ( client->shinobi.flinchEnd < level.time + 150 )
			client->shinobi.flinchEnd = level.time + 150;
	}
	if ( ( client->ps.stats[STAT_SHINOBI2] & SHINOBI_STAT2_DIVE )
		&& ( ( client->ps.stats[STAT_SHINOBI_COMBO] & SHINOBI_COMBO_AIR ) || client->ps.weapon == WP_SABER ) )
		Shinobi_AirSlam( ent ); // (a blade: the plunge slams whoever it launched)

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
