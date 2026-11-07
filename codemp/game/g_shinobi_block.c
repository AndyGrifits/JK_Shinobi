/*
===========================================================================
JK_Shinobi - block (hold the right mouse button or Ctrl): guard, perfect parry, guard break.
The guard is the shield stat (STAT_ARMOR); it only soaks hits while
blocking. The stance and the slow steps live in PM_ShinobiPreMove.
===========================================================================
*/
#include "g_shinobi_local.h"

extern qboolean BG_SabersOff( playerState_t *ps );

int shinobiGuardBreaker = 0;
int shinobiBlockResult = SBR_NONE;

void Shinobi_Stagger( gentity_t *ent, int ms ) {
	gclient_t *client = ent->client;

	if ( !client || ent->s.number >= MAX_CLIENTS || ent->health <= 0 || Shinobi_KaitenSpinning( ent ) )
		return;
	if ( client->shinobi.staggerEnd < level.time + ms )
		client->shinobi.staggerEnd = level.time + ms;
	client->ps.stats[STAT_SHINOBI] |= SHINOBI_STAT_STAGGER;
	Shinobi_HitText( ent, STX_STAGGER, 0 );
	if ( client->ps.weaponTime < ms )
		client->ps.weaponTime = ms;
	if ( client->ps.weapon == WP_SABER && BG_SaberInAttack( client->ps.saberMove ) )
		client->ps.saberBlocked = BLOCKED_ATK_BOUNCE; // the swing bounces off
	VectorClear( client->ps.velocity );
	if ( client->shinobi.chargeJutsu != SJ_NONE )
		client->shinobi.chargeJutsu = SJ_NONE;
}

// a combo punch landed: rooted, no hitting back for a moment (a swing in progress
// bounces, a charge is dropped); raising the guard and Kawarimi still work
void Shinobi_Flinch( gentity_t *ent, int ms ) {
	gclient_t *client = ent->client;

	if ( !client || ent->s.number >= MAX_CLIENTS || ent->health <= 0 || Shinobi_KaitenSpinning( ent ) )
		return;
	if ( client->shinobi.flinchEnd < level.time + ms )
		client->shinobi.flinchEnd = level.time + ms;
	if ( level.time - client->shinobi.botFlinchSeen > 1200 ) // (bots count the blows of a combo)
		client->shinobi.botFlinches = 0;
	client->shinobi.botFlinches++;
	client->shinobi.botFlinchSeen = level.time;
	client->ps.stats[STAT_SHINOBI2] |= SHINOBI_STAT2_FLINCH;
	Shinobi_HitText( ent, STX_FLINCH, 0 );
	if ( client->ps.weaponTime < ms )
		client->ps.weaponTime = ms;
	if ( client->ps.weapon == WP_SABER && BG_SaberInAttack( client->ps.saberMove ) )
		client->ps.saberBlocked = BLOCKED_ATK_BOUNCE;
	Shinobi_InterruptCharge( ent );
}

// a perfect parry: nothing of the parried blow pushes him (attacking out of it: the general rule in Pmove)
static void Shinobi_ParryHold( gentity_t *ent ) {
	VectorClear( ent->client->ps.velocity );
}

static void Shinobi_GuardBreak( gentity_t *ent ) {
	gclient_t *client = ent->client;

	client->ps.stats[STAT_ARMOR] = 0;
	client->shinobi.blocking = qfalse;
	shinobiTest.guardBreaks++;
	client->shinobi.guardRegen = level.time + SHINOBI_GUARD_BREAK_MS + SHINOBI_GUARD_REGEN_DELAY;
	Shinobi_Stagger( ent, SHINOBI_GUARD_BREAK_MS );
	G_Sound( ent, CHAN_AUTO, shinobiMedia.sndGuardBreak );
	Shinobi_HitText( ent, STX_GUARDBREAK, 0 );
	trap->SendServerCommand( ent - g_entities, "cp \"^1Guard broken!\"" );
}

// every client frame, before Pmove
void Shinobi_BlockThink( gentity_t *ent, usercmd_t *ucmd ) {
	gclient_t *client = ent->client;
	const qboolean pressed = ( ucmd->buttons & SHINOBI_BUTTON_BLOCK ) && !( client->buttons & SHINOBI_BUTTON_BLOCK );
	const qboolean was = client->shinobi.blocking;

	if ( client->shinobi.staggerEnd && client->shinobi.staggerEnd <= level.time )
		client->shinobi.staggerEnd = 0;
	if ( client->shinobi.staggerEnd )
		client->ps.stats[STAT_SHINOBI] |= SHINOBI_STAT_STAGGER;
	else
		client->ps.stats[STAT_SHINOBI] &= ~SHINOBI_STAT_STAGGER;
	{ // guard up: tell everyone how much is left (the bubble's colour)
		int lvl = 0;
		if ( client->shinobi.blocking && ent->health > 0 ) {
			const int g = client->ps.stats[STAT_ARMOR];
			lvl = g * 3 > SHINOBI_GUARD_MAX * 2 ? 1 : ( g * 3 > SHINOBI_GUARD_MAX ? 2 : 3 );
		}
		client->ps.eFlags = ( client->ps.eFlags & ~SHINOBI_EF_GUARD_MASK ) | ( lvl << SHINOBI_EF_GUARD_SHIFT );
	}
	if ( client->shinobi.flinchEnd > level.time && ent->health > 0 )
		client->ps.stats[STAT_SHINOBI2] |= SHINOBI_STAT2_FLINCH;
	else
		client->ps.stats[STAT_SHINOBI2] &= ~SHINOBI_STAT2_FLINCH;
	if ( ( client->shinobi.chargeJutsu == SJ_QUAKE || client->shinobi.chargeJutsu == SJ_CHIDORI || client->shinobi.palmsStart )
		&& ent->health > 0 ) // (Chidori too: the lightning gathers standing)
		client->ps.stats[STAT_SHINOBI2] |= SHINOBI_STAT2_ROOTED; // the Earthquake charges standing still (and the 64 Palms' field grows)
	else
		client->ps.stats[STAT_SHINOBI2] &= ~SHINOBI_STAT2_ROOTED;

	if ( ent->health <= 0 || client->sess.sessionTeam == TEAM_SPECTATOR ) {
		client->shinobi.blocking = qfalse;
		client->shinobi.staggerEnd = 0;
		return;
	}

	if ( pressed ) {
		client->shinobi.blockPrevPress = client->shinobi.blockPress;
		client->shinobi.blockPress = level.time;
	}
	client->shinobi.blocking = BG_ShinobiCanBlock( &client->ps, ucmd->buttons );

	if ( client->shinobi.blocking && !was ) {
		// raising the guard lets go of whatever was held
		if ( client->shinobi.flameEnt )
			Shinobi_FlameStop( ent );
		if ( client->shinobi.shadowEnt )
			Shinobi_ShadowRelease( ent );
		client->shinobi.chargeJutsu = SJ_NONE;
		Shinobi_StopCharging( client );
	}
	if ( was && !client->shinobi.blocking && client->shinobi.guardRegen < level.time + SHINOBI_GUARD_REGEN_DELAY / 2 )
		client->shinobi.guardRegen = level.time + SHINOBI_GUARD_REGEN_DELAY / 2;

	// the guard comes back while it is down
	if ( client->ps.stats[STAT_ARMOR] > SHINOBI_GUARD_MAX )
		client->ps.stats[STAT_ARMOR] = SHINOBI_GUARD_MAX;
	if ( !client->shinobi.blocking && client->ps.stats[STAT_ARMOR] < SHINOBI_GUARD_MAX ) {
		if ( client->shinobi.guardRegen < level.time - 1000 )
			client->shinobi.guardRegen = level.time; // (long idle: no burst)
		while ( client->shinobi.guardRegen <= level.time && client->ps.stats[STAT_ARMOR] < SHINOBI_GUARD_MAX ) {
			client->ps.stats[STAT_ARMOR]++;
			client->shinobi.guardRegen += SHINOBI_GUARD_REGEN_MS;
		}
	}
}

// facing the hit? (src: where it comes from)
static qboolean Shinobi_FacingHit( gentity_t *targ, const vec3_t src ) {
	vec3_t d, fwd, yaw;

	VectorSubtract( src, targ->client->ps.origin, d );
	d[2] = 0;
	if ( VectorNormalize( d ) < 1.0f )
		return qtrue; // right on top of us
	VectorSet( yaw, 0, targ->client->ps.viewangles[YAW], 0 );
	AngleVectors( yaw, fwd, NULL, NULL );
	return DotProduct( d, fwd ) >= SHINOBI_BLOCK_ARC;
}

static void Shinobi_BlockFeedback( gentity_t *targ, const vec3_t src, qboolean parry ) {
	vec3_t org, dir, ang;

	VectorSubtract( src, targ->client->ps.origin, dir );
	dir[2] = 0;
	VectorNormalize( dir );
	VectorMA( targ->client->ps.origin, 18, dir, org );
	org[2] += 24;
	vectoangles( dir, ang );
	G_PlayEffectID( shinobiMedia.fxBlock, org, ang );
	if ( parry )
		G_Sound( targ, CHAN_AUTO, shinobiMedia.sndParry );
	else if ( targ->client->ps.weapon == WP_SABER && !BG_SabersOff( &targ->client->ps ) )
		G_Sound( targ, CHAN_AUTO, shinobiMedia.sndBlockSaber[Q_irand( 0, 2 )] );
	else
		G_Sound( targ, CHAN_AUTO, shinobiMedia.sndBlockFist[Q_irand( 0, 2 )] );
}

// the stock green shield shell, seen by everyone around (blocker and attacker)
static void Shinobi_ShieldShell( gentity_t *targ, const vec3_t src, int amount ) {
	gentity_t *ev;
	vec3_t     dir;

	if ( amount <= 0 )
		return;
	VectorSubtract( targ->r.currentOrigin, src, dir );
	if ( VectorNormalize( dir ) < 0.01f )
		VectorSet( dir, 0, 0, -1 );
	ev = G_TempEntity( targ->r.currentOrigin, EV_SHIELD_HIT );
	ev->s.otherEntityNum = targ->s.number;
	ev->s.eventParm = DirToByte( dir );
	ev->s.time2 = amount;
}

/*
Guard up with a blade in hand: the stock saber block (w_saber.c) does the parrying -
the ready stance turns toward each blow, the attacker bounces off, a strong enough
blow crushes the parry. Here the guard pays for every blocked swing (as much as it
would have hurt) and may give; a perfect parry costs nothing and staggers him.
*/
qboolean Shinobi_SaberGuardUp( gentity_t *ent ) {
	return ( ent && ent->client && ent->s.number < MAX_CLIENTS && ent->client->shinobi.blocking
		&& ent->client->ps.weapon == WP_SABER && ent->client->ps.stats[STAT_ARMOR] > 0 ) ? qtrue : qfalse;
}

// a blade swing of 'attacker' is about to be blocked by targ's blade: qfalse = the guard gave, it goes through
/*
===========================================================================
One blade hit per swing (see shinobiSwordDamage)
===========================================================================
*/
extern qboolean BG_SaberInAttack( int move );
extern qboolean BG_SaberInKata( int saberMove );
extern qboolean BG_SaberInSpecial( int move );

extern void G_Knockdown( gentity_t *victim );
void Shinobi_BladeFinisher( gentity_t *attacker, gentity_t *victim, int kind, float *dmg );
static void Shinobi_FinisherReach( gentity_t *ent );
static void Shinobi_ThrustReach( gentity_t *ent );
static void Shinobi_SwingReach( gentity_t *ent );
qboolean Shinobi_SaberGuard( gentity_t *targ, gentity_t *attacker, int dmg, const vec3_t at );

// the stance whose numbers a blade fights by (shinobiSword*): its own, but a pair (the two-blade
// moves) by its pace - played faster (the knives) a fast one, else a medium one
static int Shinobi_BladeStyle( gclient_t *c ) {
	const int style = c->ps.fd.saberAnimLevel;
	if ( style == SS_DUAL )
		return c->saber[0].animSpeedScale >= SHINOBI_PAIR_FAST_SCALE ? SS_FAST : SS_MEDIUM;
	return ( style < 0 || style > 7 ) ? 2 : style;
}
int Shinobi_BladeStylePublic( gentity_t *ent ) { return Shinobi_BladeStyle( ent->client ); }

// the look of a finisher (cgame: CG_ShinobiFinisher)
static void Shinobi_FinisherFx( gentity_t *attacker, gentity_t *victim, int kind ) {
	gentity_t *fx = G_Spawn();
	if ( !fx )
		return;
	fx->classname = "shinobi_finisher";
	fx->s.eType = ET_GENERAL;
	fx->s.generic1 = SHINOBI_ENT_FINISHER;
	fx->r.contents = 0;
	G_SetOrigin( fx, victim->r.currentOrigin );
	VectorCopy( attacker->r.currentOrigin, fx->s.origin2 );
	fx->s.frame = kind;
	fx->s.time = level.time;
	fx->s.otherEntityNum = victim->s.number; // (to follow him)
	fx->s.otherEntityNum2 = attacker->s.number;
	fx->think = G_FreeEntity;
	fx->nextthink = level.time + SHINOBI_FINISHER_FX_MS;
	trap->LinkEntity( (sharedEntity_t *)fx );
}

// can the combo still go on against him?
static qboolean Shinobi_BladeChainLive( gclient_t *c ) {
	gentity_t *v;
	if ( !c->shinobi.bladeChain || !c->shinobi.bladeChainVictim )
		return qfalse;
	if ( level.time - c->shinobi.bladeChainTime > ( c->shinobi.bladeChain >= shinobiSwordChain[Shinobi_BladeStyle( c )] - 1
		? SHINOBI_SWORD_READY_MS : shinobiSwordWindow[Shinobi_BladeStyle( c )] ) )
		return qfalse; // (the finisher once ready waits a good while)
	v = &g_entities[c->shinobi.bladeChainVictim - 1];
	return v->inuse && v->client && v->health > 0;
}

qboolean Shinobi_BladeChainLivePublic( gclient_t *c ) { return Shinobi_BladeChainLive( c ); }

// a new swing: a new attack move, another anim, or the same one started over. A finisher move
// begun with the combo one blow short is the finisher swing: its blow (on each one it cuts)
// finishes; the knives throw their chakra blade right as it starts
// a finisher swing reaches as far as a big blade (SHINOBI_SWORD_FINISH_LEN); put back after it
static void Shinobi_BladeLength( gclient_t *c, qboolean on ) {
	int s, b;
	for ( s = 0; s < 2; s++ ) {
		saberInfo_t *sab = &c->saber[s];
		if ( sab->numBlades <= 0 )
			continue;
		for ( b = 0; b < sab->numBlades && b < 2; b++ ) {
			bladeInfo_t *bl = &sab->blade[b];
			if ( on ) {
				c->shinobi.bladeLenSave[s][b] = bl->lengthMax;
				if ( bl->lengthMax < SHINOBI_SWORD_FINISH_LEN ) {
					bl->lengthMax = SHINOBI_SWORD_FINISH_LEN;
					if ( bl->length > 0 )
						bl->length = SHINOBI_SWORD_FINISH_LEN;
				}
			} else if ( c->shinobi.bladeLenSave[s][b] > 0 && bl->lengthMax == SHINOBI_SWORD_FINISH_LEN ) {
				bl->lengthMax = c->shinobi.bladeLenSave[s][b];
				if ( bl->length > bl->lengthMax )
					bl->length = bl->lengthMax;
			}
		}
	}
}

void Shinobi_BladeSwingUpdate( gentity_t *ent ) {
	gclient_t *c = ent->client;
	const playerState_t *ps = &c->ps;

	if ( c->shinobi.bladeLenSwing && ( c->shinobi.bladeLenSwing != c->shinobi.finisherSwing
		|| !( BG_SaberInAttack( ps->saberMove ) || BG_SaberInSpecial( ps->saberMove ) ) || ps->weapon != WP_SABER ) ) {
		Shinobi_BladeLength( c, qfalse );
		c->shinobi.bladeLenSwing = 0;
		memset( c->shinobi.bladeLenSave, 0, sizeof( c->shinobi.bladeLenSave ) );
	}

	if ( BG_SaberInAttack( ps->saberMove ) || BG_SaberInSpecial( ps->saberMove ) ) {
		if ( ps->saberMove != c->shinobi.bladeMove || ps->torsoAnim != c->shinobi.bladeAnim ) {
			const int kind = Shinobi_FinisherKindOfMove( ps->saberMove );
			const int style = Shinobi_BladeStyle( c );
			c->shinobi.bladeSwing++;
			c->shinobi.thrustSwing = 0;
			c->shinobi.reachSwing = 0;
			if ( BG_SaberInAttack( ps->saberMove ) && !BG_SaberInSpecial( ps->saberMove ) && !BG_SaberInKata( ps->saberMove )
				&& ent->s.number < MAX_CLIENTS ) { // a plain cut: its reach by mid-swing (Shinobi_SwingReach)
				c->shinobi.reachSwing = c->shinobi.bladeSwing;
				c->shinobi.reachStart = level.time;
				c->shinobi.reachDur = ps->torsoTimer > 0 ? ps->torsoTimer : 400;
			}
			if ( c->saber[0].shinobiThrust && ps->torsoAnim == BOTH_PULL_IMPALE_STAB && ent->s.number < MAX_CLIENTS ) {
				c->shinobi.thrustSwing = c->shinobi.bladeSwing; // a thrust: its reach is checked part way (Shinobi_ThrustReach)
				c->shinobi.thrustStart = level.time;
				c->shinobi.thrustDur = ps->torsoTimer > 0 ? ps->torsoTimer : 400;
			}
			if ( kind && ps->weapon == WP_SABER && ent->s.number < MAX_CLIENTS && Shinobi_BladeChainLive( c )
				&& c->shinobi.bladeChain == shinobiSwordChain[style] - 1 ) {
				c->shinobi.finisherSwing = c->shinobi.bladeSwing;
				c->shinobi.finisherKind = kind;
				c->shinobi.bladeChain = 0;
				if ( !c->shinobi.bladeLenSwing ) {
					Shinobi_BladeLength( c, qtrue );
					c->shinobi.bladeLenSwing = c->shinobi.bladeSwing;
				}
				c->shinobi.finisherStart = level.time;
				c->shinobi.finisherDur = ps->torsoTimer > 0 ? ps->torsoTimer : 400;
				c->shinobi.finisherHit = 0;
				if ( kind == SFK_OWN && c->saber[0].shinobiFinisher == SBF_REACH ) { // the knives: a wide arc thrown far ahead
					c->shinobi.bladeReachSwing = c->shinobi.bladeSwing;
					c->shinobi.finisherHit = 1; // (its blow is the arc's)
					Shinobi_BladeArc( ent, (int)( shinobiSwordDamage[style] * SHINOBI_SWORD_FINISH + 0.5f ) );
					shinobiTest.bladeFinishers[SBF_REACH]++;
				}
			}
		}
	}
	if ( c->shinobi.thrustSwing == c->shinobi.bladeSwing && c->shinobi.swingBlow != c->shinobi.bladeSwing
		&& BG_SaberInAttack( ps->saberMove ) && ps->weapon == WP_SABER
		&& level.time - c->shinobi.thrustStart >= c->shinobi.thrustDur * SHINOBI_THRUST_REACH_AT ) {
		c->shinobi.thrustSwing = 0; // (tried once)
		Shinobi_ThrustReach( ent );
	}
	if ( c->shinobi.reachSwing == c->shinobi.bladeSwing && c->shinobi.swingBlow != c->shinobi.bladeSwing
		&& c->shinobi.thrustSwing != c->shinobi.bladeSwing && c->shinobi.finisherSwing != c->shinobi.bladeSwing
		&& c->shinobi.bladeReachSwing != c->shinobi.bladeSwing
		&& BG_SaberInAttack( ps->saberMove ) && ps->weapon == WP_SABER && shinobi_bladereach.integer
		&& level.time - c->shinobi.reachStart >= c->shinobi.reachDur * SHINOBI_SWING_REACH_AT ) {
		c->shinobi.reachSwing = 0; // (tried once)
		Shinobi_SwingReach( ent );
	}
	if ( c->shinobi.finisherKind && c->shinobi.finisherSwing == c->shinobi.bladeSwing && !c->shinobi.finisherHit
		&& ( BG_SaberInAttack( ps->saberMove ) || BG_SaberInSpecial( ps->saberMove ) ) && ps->weapon == WP_SABER
		&& level.time - c->shinobi.finisherStart >= c->shinobi.finisherDur * SHINOBI_SWORD_FINISH_AT ) {
		c->shinobi.finisherHit = 1; // (tried once)
		Shinobi_FinisherReach( ent );
	}
	c->shinobi.bladeMove = ps->saberMove;
	c->shinobi.bladeAnim = ps->torsoAnim;
	c->shinobi.bladeTimer = ps->torsoTimer;
}

// has this swing of attacker's cut him already? qfalse: no new blow; qtrue: a new one (noted)
static qboolean Shinobi_BladeNewBlow( gentity_t *attacker, gentity_t *victim, qboolean special, qboolean finisher ) {
	gclient_t *ac = attacker->client, *vc = victim->client;
	const int  style = Shinobi_BladeStyle( ac );
	int        k, slot = -1, oldest = 0;

	for ( k = 0; k < 4; k++ ) {
		if ( vc->shinobi.bladeHitBy[k] == attacker->s.number + 1 ) {
			slot = k;
			break;
		}
		if ( vc->shinobi.bladeHitTime[k] < vc->shinobi.bladeHitTime[oldest] )
			oldest = k;
	}
	if ( slot >= 0 ) {
		const int since = level.time - vc->shinobi.bladeHitTime[slot];
		if ( vc->shinobi.bladeHitSwing[slot] == ac->shinobi.bladeSwing
			&& ( !special || finisher || since < SHINOBI_BLADE_SPECIAL_MS ) )
			return qfalse; // this swing cut him already (a kata again after a while)
		if ( since < shinobiSwordRehitMs[ac->ps.fd.saberAnimLevel == SS_DUAL ? SS_DUAL : style] && !finisher )
			return qfalse; // (a cut seen as two swings: still one blow)
	}
	if ( slot < 0 )
		slot = oldest;
	vc->shinobi.bladeHitBy[slot] = attacker->s.number + 1;
	vc->shinobi.bladeHitSwing[slot] = ac->shinobi.bladeSwing;
	vc->shinobi.bladeHitTime[slot] = level.time;
	return qtrue;
}

// the combo: the same one, soon after the last blow - up to one short of the finisher
static void Shinobi_BladeChainAdd( gentity_t *attacker, gentity_t *victim, const char *how ) {
	gclient_t *ac = attacker->client;
	const int  style = Shinobi_BladeStyle( ac );

	if ( ac->shinobi.bladeChainVictim == victim->s.number + 1 && Shinobi_BladeChainLive( ac ) )
		ac->shinobi.bladeChain++;
	else
		ac->shinobi.bladeChain = 1;
	if ( ac->shinobi.bladeChain > shinobiSwordChain[style] - 1 )
		ac->shinobi.bladeChain = shinobiSwordChain[style] - 1; // (ready: the next swing is the finisher)
	ac->shinobi.bladeChainVictim = victim->s.number + 1;
	ac->shinobi.bladeChainTime = level.time;
	Shinobi_Debug( "blade %i -> %i: %s %i of %i (swing %i, move %i, anim %i)", attacker->s.number, victim->s.number, how,
		ac->shinobi.bladeChain, shinobiSwordChain[style], ac->shinobi.bladeSwing, ac->ps.saberMove, ac->ps.torsoAnim );
}

// a swing that his blade held off (the guard held): it counts for the combo all the same
void Shinobi_BladeBlocked( gentity_t *attacker, gentity_t *victim ) {
	gclient_t *ac;
	if ( !attacker || !attacker->client || attacker->s.number >= MAX_CLIENTS || attacker->client->ps.weapon != WP_SABER
		|| !victim || !victim->client )
		return;
	ac = attacker->client;
	Shinobi_BladeSwingUpdate( attacker );
	if ( !BG_SaberInAttack( ac->ps.saberMove ) || BG_SaberInSpecial( ac->ps.saberMove ) || BG_SaberInKata( ac->ps.saberMove )
		|| ( ac->shinobi.finisherSwing == ac->shinobi.bladeSwing && ac->shinobi.finisherKind ) )
		return;
	if ( Shinobi_BladeNewBlow( attacker, victim, qfalse, qfalse ) )
		Shinobi_BladeChainAdd( attacker, victim, "blocked blow" );
}

// the finisher swing by mid-swing hasn't cut anyone: it lands on whoever stands in reach in front
// (the moves swing wide or close - the blade alone often missed someone the plain cuts did reach)
extern qboolean Shinobi_ComboTarget( gentity_t *ent, gentity_t *v );
static void Shinobi_FinisherReach( gentity_t *ent ) {
	gclient_t *c = ent->client;
	const int  kind = c->shinobi.finisherKind, style = Shinobi_BladeStyle( c );
	vec3_t     fwd, yaw;
	int        i;

	VectorSet( yaw, 0, c->ps.viewangles[YAW], 0 );
	AngleVectors( yaw, fwd, NULL, NULL );
	for ( i = 0; i < level.num_entities; i++ ) {
		gentity_t *v = &g_entities[i];
		vec3_t     d;
		float      dist, dmg;

		if ( !Shinobi_ComboTarget( ent, v ) )
			continue;
		VectorSubtract( v->r.currentOrigin, ent->r.currentOrigin, d );
		if ( fabs( d[2] ) > 64 )
			continue;
		d[2] = 0;
		dist = VectorNormalize( d );
		if ( dist > SHINOBI_SWORD_FINISH_REACH || ( kind != SFK_SPIN && dist > 16 && DotProduct( d, fwd ) < 0.4f ) )
			continue;
		if ( !Shinobi_BladeNewBlow( ent, v, qfalse, qtrue ) )
			continue;
		dmg = (float)shinobiSwordDamage[style] * ( c->saber[0].damageScale > 0 ? c->saber[0].damageScale : 1.0f )
			* ( kind == SFK_SPIN ? 1.0f : SHINOBI_SWORD_FINISH ); // (the spin cuts all round: no bonus)
		if ( Shinobi_SaberGuardUp( v ) && Shinobi_FacingHit( v, ent->r.currentOrigin )
			&& Shinobi_SaberGuard( v, ent, (int)dmg, v->r.currentOrigin ) )
			continue; // his blade held it
		Shinobi_Debug( "blade %i -> %i: FINISHER kind %i by reach (%.0f u)", ent->s.number, v->s.number, kind, dist );
		shinobiTest.bladeFinisherReach++;
		Shinobi_BladeFinisher( ent, v, kind, &dmg );
		G_Damage( v, ent, ent, d, v->r.currentOrigin, (int)( dmg + 0.5f ), DAMAGE_NO_HIT_LOC | DAMAGE_NO_KNOCKBACK, MOD_SABER );
	}
}

/*
A thrusting blade (Nuibari) part way into a thrust that hasn't cut anyone: the needle goes into
whoever stands straight ahead within its length - the stock stab swings its blade close and low,
the plain thrust missed nearly everyone. A blow like the blade's own (combo, flinch, guard).
*/
static void Shinobi_ThrustReach( gentity_t *ent ) {
	gclient_t *c = ent->client;
	const float reach = ( c->saber[0].blade[0].lengthMax > 0 ? c->saber[0].blade[0].lengthMax : 40 ) + 24;
	gentity_t *best = NULL;
	float      bestDist = reach + 1;
	vec3_t     fwd, yaw, bestDir;
	int        i;

	VectorSet( yaw, 0, c->ps.viewangles[YAW], 0 );
	AngleVectors( yaw, fwd, NULL, NULL );
	for ( i = 0; i < level.num_entities; i++ ) {
		gentity_t *v = &g_entities[i];
		vec3_t     d;
		float      dist;
		if ( !Shinobi_ComboTarget( ent, v ) )
			continue;
		VectorSubtract( v->r.currentOrigin, ent->r.currentOrigin, d );
		if ( fabs( d[2] ) > 56 )
			continue;
		d[2] = 0;
		dist = VectorNormalize( d );
		if ( dist > reach || dist >= bestDist || ( dist > 16 && DotProduct( d, fwd ) < SHINOBI_THRUST_CONE ) )
			continue;
		best = v;
		bestDist = dist;
		VectorCopy( d, bestDir );
	}
	if ( !best )
		return;
	if ( Shinobi_SaberGuardUp( best ) && Shinobi_FacingHit( best, ent->r.currentOrigin )
		&& Shinobi_SaberGuard( best, ent, shinobiSwordDamage[Shinobi_BladeStyle( c )], best->r.currentOrigin ) )
		return; // his blade held it (it counts for the combo there)
	{
		const float dmg = Shinobi_BladeDamage( ent, best, 0 );
		Shinobi_Debug( "thrust %i -> %i by reach (%.0f u): %.0f", ent->s.number, best->s.number, bestDist, dmg );
		if ( dmg > 0 ) {
			shinobiTest.thrustReach++;
			G_Damage( best, ent, ent, bestDir, best->r.currentOrigin, (int)( dmg + 0.5f ), DAMAGE_NO_HIT_LOC, MOD_SABER );
		}
	}
}

/*
A plain cut that by mid-swing hasn't cut anyone: it lands on whoever stands in its reach in front
(the nearest). The blade's own trace runs on the model's mesh frame by frame and at fighting
range missed about half of the cuts that plainly went through him. A blow like the blade's own
(combo, flinch, guard).
*/
static void Shinobi_SwingReach( gentity_t *ent ) {
	gclient_t  *c = ent->client;
	const float len = c->saber[0].blade[0].lengthMax > 0 ? c->saber[0].blade[0].lengthMax : 40;
	const float reach = Com_Clamp( 56, 90, len + SHINOBI_SWING_REACH_PAD );
	gentity_t  *best = NULL;
	float       bestDist = reach + 1;
	vec3_t      fwd, yaw, bestDir;
	int         i;

	VectorSet( yaw, 0, c->ps.viewangles[YAW], 0 );
	AngleVectors( yaw, fwd, NULL, NULL );
	for ( i = 0; i < level.num_entities; i++ ) {
		gentity_t *v = &g_entities[i];
		vec3_t     d;
		float      dist;
		trace_t    tr;
		if ( !Shinobi_ComboTarget( ent, v ) )
			continue;
		VectorSubtract( v->r.currentOrigin, ent->r.currentOrigin, d );
		if ( fabs( d[2] ) > 56 )
			continue;
		d[2] = 0;
		dist = VectorNormalize( d );
		if ( dist > reach || dist >= bestDist || ( dist > 16 && DotProduct( d, fwd ) < SHINOBI_SWING_REACH_CONE ) )
			continue;
		trap->Trace( &tr, ent->r.currentOrigin, NULL, NULL, v->r.currentOrigin, ent->s.number, MASK_SOLID, qfalse, 0, 0 );
		if ( tr.fraction < 1.0f && tr.entityNum != v->s.number )
			continue; // a wall between
		best = v;
		bestDist = dist;
		VectorCopy( d, bestDir );
	}
	if ( !best ) {
		shinobiTest.swingReachMissed++;
		return;
	}
	if ( Shinobi_SaberGuardUp( best ) && Shinobi_FacingHit( best, ent->r.currentOrigin )
		&& Shinobi_SaberGuard( best, ent, shinobiSwordDamage[Shinobi_BladeStyle( c )], best->r.currentOrigin ) )
		return; // his blade held it (it counts for the combo there)
	{
		const float dmg = Shinobi_BladeDamage( ent, best, 0 );
		Shinobi_Debug( "cut %i -> %i by reach (%.0f u of %.0f): %.0f", ent->s.number, best->s.number, bestDist, reach, dmg );
		if ( dmg > 0 ) {
			shinobiTest.swingReach++;
			G_Damage( best, ent, ent, bestDir, best->r.currentOrigin, (int)( dmg + 0.5f ), DAMAGE_NO_HIT_LOC, MOD_SABER );
		}
	}
}

float Shinobi_BladeDamage( gentity_t *attacker, gentity_t *victim, float stockDamage ) {
	gclient_t *ac = attacker->client;
	const int  move = ac->ps.saberMove;
	const qboolean special = BG_SaberInSpecial( move ) || BG_SaberInKata( move );
	const int  style = Shinobi_BladeStyle( ac );
	qboolean   finisher;
	float      dmg;

	(void)stockDamage;
	Shinobi_BladeSwingUpdate( attacker );
	if ( !BG_SaberInAttack( move ) && !special )
		return 0; // a touch with a blade at rest
	if ( ac->shinobi.bladeReachSwing == ac->shinobi.bladeSwing )
		return 0; // this swing threw the knives' chakra blade: that was its blow
	finisher = ac->shinobi.finisherSwing == ac->shinobi.bladeSwing && ac->shinobi.finisherKind;
	if ( !Shinobi_BladeNewBlow( attacker, victim, special, finisher ) )
		return 0;
	ac->shinobi.swingBlow = ac->shinobi.bladeSwing;

	dmg = (float)shinobiSwordDamage[style];
	if ( ac->saber[0].damageScale > 0 )
		dmg *= ac->saber[0].damageScale;

	if ( finisher ) { // the finisher swing: each one it cuts
		Shinobi_Debug( "blade %i -> %i: FINISHER kind %i (swing %i)", attacker->s.number, victim->s.number,
			ac->shinobi.finisherKind, ac->shinobi.bladeSwing );
		if ( ac->shinobi.finisherKind != SFK_SPIN ) // (the spin cuts all round, easy to land: no bonus)
			dmg *= SHINOBI_SWORD_FINISH;
		ac->shinobi.finisherHit = 1;
		Shinobi_BladeFinisher( attacker, victim, ac->shinobi.finisherKind, &dmg );
		return dmg;
	}
	if ( special ) {
		ac->shinobi.bladeChain = 0; // (another special move stands on its own)
		return dmg * SHINOBI_BLADE_SPECIAL;
	}

	Shinobi_BladeChainAdd( attacker, victim, "blow" );
	if ( attacker->s.number < MAX_CLIENTS ) { // held until the next blow can land, as with fists
		int ms = ac->ps.weaponTime + SHINOBI_COMBO_FLINCH_MS;
		if ( ms < SHINOBI_COMBO_FLINCH_MIN )
			ms = SHINOBI_COMBO_FLINCH_MIN;
		if ( ms > SHINOBI_COMBO_FLINCH_MAX )
			ms = SHINOBI_COMBO_FLINCH_MAX;
		Shinobi_Flinch( victim, ms );
	}
	return dmg;
}

/*
The finisher's blow (already x1.5), by the finisher swing it came from - the sweep knocks
down, the spin (a full turn: everyone it cuts gets this), the rising cut throws up, the
weapon's own does its thing.
*/
void Shinobi_BladeFinisher( gentity_t *attacker, gentity_t *victim, int kind, float *dmg ) {
	gclient_t *ac = attacker->client, *vc = victim->client;
	vec3_t dir;

	VectorSubtract( victim->r.currentOrigin, attacker->r.currentOrigin, dir );
	dir[2] = 0;
	VectorNormalize( dir );
	vc->ps.otherKiller = attacker->s.number;
	vc->ps.otherKillerTime = level.time + 5000;
	vc->ps.otherKillerDebounceTime = level.time + 100;
	vc->shinobi.flinchEnd = 0; // (the finisher lets go)
	Shinobi_GainChakra( attacker, SHINOBI_GAIN_COMBO, qtrue );
	Shinobi_HitText( victim, STX_FINISHER, 0 );

	if ( kind == SFK_RISE ) { // up he goes
		shinobiTest.bladeFinishers[0]++;
		vc->shinobi.juggleStart = level.time;
		vc->shinobi.juggleEnd = level.time + 2500;
		vc->shinobi.juggleBy = attacker->s.number + 1;
		VectorScale( dir, 60, vc->ps.velocity );
		vc->ps.velocity[2] = SHINOBI_SWORD_LAUNCH_UP;
		vc->ps.groundEntityNum = ENTITYNUM_NONE;
		Shinobi_HitText( victim, STX_LAUNCH, 0 );
		Shinobi_CCGrant( victim, level.time + 1100 );
		Shinobi_FinisherFx( attacker, victim, SFX_LAUNCH );
		ac->shinobi.bladeAirUntil = level.time + SHINOBI_SWORD_AIR_MS; // jump now: Shunshin up after him
		ac->shinobi.bladeAirVictim = victim->s.number + 1;
		ac->shinobi.bladeAirJumpWas = ac->pers.cmd.upmove > 0;
		return;
	}
	if ( kind == SFK_SWEEP ) { // low, at the legs: down
		shinobiTest.bladeFinishers[0]++;
		VectorScale( dir, 200, vc->ps.velocity );
		vc->ps.velocity[2] = 120;
		if ( !Shinobi_CCImmune( victim ) )
			G_Knockdown( victim );
		Shinobi_CCGrant( victim, level.time + 1500 );
		Shinobi_FinisherFx( attacker, victim, SFX_SWEEP );
		return;
	}
	if ( kind == SFK_SPIN ) { // a full turn (the blade cuts each one round him)
		shinobiTest.bladeFinishers[0]++;
		Shinobi_FinisherFx( attacker, victim, SFX_SPIN );
		return;
	}

	shinobiTest.bladeFinishers[ac->saber[0].shinobiFinisher]++;
	switch ( ac->saber[0].shinobiFinisher ) {
	case SBF_DRAIN: { // Samehada eats his chakra, and throws him back
		const int take = vc->ps.fd.forcePower < SHINOBI_SWORD_DRAIN ? vc->ps.fd.forcePower : SHINOBI_SWORD_DRAIN;
		vc->ps.fd.forcePower -= take;
		if ( take > 0 )
			Shinobi_GainChakra( attacker, (float)take, qtrue );
		if ( !Shinobi_CCImmune( victim ) ) {
			VectorScale( dir, SHINOBI_SWORD_DRAIN_PUSH, vc->ps.velocity );
			vc->ps.velocity[2] = 160;
			vc->ps.groundEntityNum = ENTITYNUM_NONE;
		}
		Shinobi_FinisherFx( attacker, victim, SFX_DRAIN );
		break;
	}
	case SBF_BLEED: // the Executioner's Blade opens a wound
		vc->shinobi.bleedEnd = level.time + SHINOBI_SWORD_BLEED_MS;
		vc->shinobi.bleedNext = level.time + SHINOBI_SWORD_BLEED_TICK;
		vc->shinobi.bleedBy = attacker->s.number + 1;
		Shinobi_FinisherFx( attacker, victim, SFX_BLEED );
		break;
	case SBF_IAI: // a clean draw-cut: he reels
		Shinobi_Stagger( victim, SHINOBI_SWORD_IAI_MS );
		Shinobi_FinisherFx( attacker, victim, SFX_IAI );
		break;
	case SBF_FLURRY: // two more cuts right after
		ac->shinobi.flurryVictim = victim->s.number + 1;
		ac->shinobi.flurryLeft = SHINOBI_SWORD_FLURRY;
		ac->shinobi.flurryNext = level.time + SHINOBI_SWORD_FLURRY_MS;
		ac->shinobi.flurryDmg = (int)( *dmg * SHINOBI_SWORD_FLURRY_SCALE + 0.5f );
		Shinobi_FinisherFx( attacker, victim, SFX_FLURRY );
		break;
	case SBF_BURN: // the Uchiha blade: he catches fire
		Shinobi_Ignite( victim, attacker->s.number + 1 );
		Shinobi_FinisherFx( attacker, victim, SFX_BURN );
		break;
	case SBF_BLAST: { // Shibuki: the tags on it go off - thrown back and down
		vec3_t up = { 0, 0, 1 };
		G_PlayEffectID( shinobiMedia.fxFireballBoom[0], victim->r.currentOrigin, up );
		G_PlayEffectID( shinobiMedia.fxStockExplode, victim->r.currentOrigin, up );
		G_SoundAtLoc( victim->r.currentOrigin, CHAN_AUTO, shinobiMedia.sndFireballBoom );
		G_ScreenShake( victim->r.currentOrigin, NULL, 2.0f, 300, qfalse );
		{ // a little fire where it went off: it lights ash clouds, burns whoever stands in it, steams in water
			vec3_t floor, pos;
			VectorCopy( victim->r.currentOrigin, pos );
			if ( Shinobi_FloorBelow( pos, 96, floor ) )
				Shinobi_SpawnZone( SZ_FIRE, floor, SHINOBI_SWORD_BLAST_FIRE, 4000, attacker->s.number + 1 );
			else
				Shinobi_IgniteAshAt( pos, SHINOBI_SWORD_BLAST_FIRE * 2, attacker->s.number + 1 );
		}
		if ( !Shinobi_CCImmune( victim ) ) {
			VectorScale( dir, SHINOBI_SWORD_BLAST_PUSH, vc->ps.velocity );
			vc->ps.velocity[2] = 220;
			vc->ps.groundEntityNum = ENTITYNUM_NONE;
			G_Knockdown( victim );
		}
		Shinobi_CCGrant( victim, level.time + 1500 );
		Shinobi_FinisherFx( attacker, victim, SFX_BLAST );
		break;
	}
	case SBF_STUN: // Nuibari: a thrust to a nerve - he stands there, helpless
		if ( !Shinobi_CCImmune( victim ) ) {
			if ( vc->shinobi.stunEnd < level.time + SHINOBI_SWORD_STUN_MS )
				vc->shinobi.stunEnd = level.time + SHINOBI_SWORD_STUN_MS;
			VectorClear( vc->ps.velocity );
			if ( vc->ps.weaponTime < SHINOBI_SWORD_STUN_MS )
				vc->ps.weaponTime = SHINOBI_SWORD_STUN_MS;
			Shinobi_HitText( victim, STX_STUN, 0 );
			shinobiTest.stuns++;
		}
		Shinobi_CCGrant( victim, level.time + SHINOBI_SWORD_STUN_MS );
		Shinobi_FinisherFx( attacker, victim, SFX_STUN );
		break;
	default: // (the knives' finisher is the arc: Shinobi_BladeArc)
		break;
	}
}

// after the rising cut: a new jump press Shunshins you up to him (smoke where you were and where you appear)
static void Shinobi_BladeAirShunshin( gentity_t *ent ) {
	gclient_t *c = ent->client;
	const qboolean jump = c->pers.cmd.upmove > 0;
	gentity_t *v;
	vec3_t     d, spot, ang;
	trace_t    tr;

	if ( !c->shinobi.bladeAirUntil )
		return;
	if ( level.time > c->shinobi.bladeAirUntil || ent->health <= 0 ) {
		c->shinobi.bladeAirUntil = 0;
		return;
	}
	if ( !jump || c->shinobi.bladeAirJumpWas ) {
		c->shinobi.bladeAirJumpWas = jump;
		return;
	}
	c->shinobi.bladeAirUntil = 0;
	v = &g_entities[c->shinobi.bladeAirVictim - 1];
	if ( !v->inuse || !v->client || v->health <= 0 )
		return;
	VectorSubtract( v->r.currentOrigin, c->ps.origin, d );
	d[2] = 0;
	if ( VectorNormalize( d ) < 1 )
		VectorSet( d, 1, 0, 0 );
	VectorMA( v->r.currentOrigin, -SHINOBI_SWORD_AIR_BEHIND, d, spot );
	trap->Trace( &tr, v->r.currentOrigin, ent->r.mins, ent->r.maxs, spot, v->s.number, MASK_PLAYERSOLID, qfalse, 0, 0 );
	if ( tr.allsolid )
		return;
	Shinobi_EffectDir( shinobiMedia.fxShunshin, c->ps.origin, d );
	G_SoundAtLoc( c->ps.origin, CHAN_AUTO, shinobiMedia.sndPoof );
	Shinobi_TeleportClient( ent, tr.endpos );
	VectorCopy( v->client->ps.velocity, c->ps.velocity ); // (rising with him)
	c->ps.groundEntityNum = ENTITYNUM_NONE;
	c->ps.stats[STAT_SHINOBI_COMBO] |= SHINOBI_COMBO_AIR; // up with him: the plunge from any height (PM_ShinobiSwordDive)
	VectorSet( ang, 0, vectoyaw( d ), 0 );
	SetClientViewAngle( ent, ang );
	Shinobi_EffectDir( shinobiMedia.fxShunshin, c->ps.origin, d );
	shinobiTest.bladeFinishers[SHINOBI_BLADE_SHUNSHIN_STAT]++;
}

// every client frame: the combo for the HUD; Kurosawa's extra cuts (his), a bleeding wound (on him)
void Shinobi_SwordThink( gentity_t *ent ) {
	gclient_t *c = ent->client;

	Shinobi_BladeAirShunshin( ent );

	if ( c->ps.weapon == WP_SABER && Shinobi_BladeChainLive( c ) ) {
		const int need = shinobiSwordChain[Shinobi_BladeStyle( c )];
		c->ps.stats[STAT_SHINOBI_BLADECOMBO] = ( c->ps.stats[STAT_SHINOBI_BLADECOMBO] & ~SHINOBI_BC_LOW )
			| ( c->shinobi.bladeChain & 7 ) | ( ( need & 7 ) << 3 ); // (the upper bits: the Gates, Dynamic Entry)
	}
	else if ( c->ps.weapon == WP_MELEE && Shinobi_FistChainLive( c ) ) { // fists: the same pips (and Pmove's finisher)
		const int need = shinobiFistChain[SHINOBI_FIST_STYLE( &c->ps )] + 1;
		c->ps.stats[STAT_SHINOBI_BLADECOMBO] = ( c->ps.stats[STAT_SHINOBI_BLADECOMBO] & ~SHINOBI_BC_LOW )
			| ( c->shinobi.fistChain & 7 ) | ( ( need & 7 ) << 3 );
	}
	else
		c->ps.stats[STAT_SHINOBI_BLADECOMBO] &= ~SHINOBI_BC_LOW;

	if ( c->shinobi.flurryLeft > 0 && c->shinobi.flurryNext <= level.time ) {
		gentity_t *v = &g_entities[c->shinobi.flurryVictim - 1];
		c->shinobi.flurryLeft--;
		c->shinobi.flurryNext = level.time + SHINOBI_SWORD_FLURRY_MS;
		if ( ent->health > 0 && c->shinobi.flurryVictim && v->inuse && v->client && v->health > 0
			&& Distance( v->r.currentOrigin, ent->r.currentOrigin ) < 140 ) {
			vec3_t d;
			VectorSubtract( v->r.currentOrigin, ent->r.currentOrigin, d );
			G_Damage( v, ent, ent, d, v->r.currentOrigin, c->shinobi.flurryDmg, DAMAGE_NO_HIT_LOC | DAMAGE_NO_KNOCKBACK, MOD_SABER );
			G_Sound( v, CHAN_AUTO, G_SoundIndex( va( "sound/weapons/sword/stab%d.mp3", Q_irand( 1, 3 ) ) ) );
		}
		else
			c->shinobi.flurryLeft = 0;
	}

	if ( c->shinobi.bleedEnd ) {
		if ( ent->health <= 0 || c->shinobi.bleedEnd <= level.time ) {
			c->shinobi.bleedEnd = 0;
		}
		else if ( c->shinobi.bleedNext <= level.time ) {
			gentity_t *by = c->shinobi.bleedBy ? &g_entities[c->shinobi.bleedBy - 1] : ent;
			vec3_t     up = { 0, 0, 1 }, at;
			c->shinobi.bleedNext = level.time + SHINOBI_SWORD_BLEED_TICK;
			if ( !by->inuse || !by->client )
				by = ent;
			VectorCopy( ent->r.currentOrigin, at );
			at[2] += 12;
			G_PlayEffectID( shinobiMedia.fxBlood, at, up );
			G_Damage( ent, NULL, by, NULL, NULL, SHINOBI_SWORD_BLEED_DMG,
				DAMAGE_NO_ARMOR | DAMAGE_NO_KNOCKBACK | DAMAGE_NO_HIT_LOC, MOD_TRIGGER_HURT );
		}
	}
}

qboolean Shinobi_SaberGuard( gentity_t *targ, gentity_t *attacker, int dmg, const vec3_t at ) {
	gclient_t *client;
	qboolean   perfect;
	vec3_t     src;
	int        guard;

	shinobiBlockResult = SBR_NONE;
	if ( !targ || !targ->client || targ->s.number >= MAX_CLIENTS )
		return qtrue; // NPCs: the stock block as it is
	client = targ->client;
	if ( dmg <= 1 )
		return qtrue; // the blade just touching: no cost
	VectorCopy( attacker ? attacker->r.currentOrigin : at, src );
	perfect = level.time - client->shinobi.blockPress <= SHINOBI_PARRY_MS
		&& client->shinobi.blockPress - client->shinobi.blockPrevPress >= SHINOBI_PARRY_SPAM_MS;
	if ( perfect ) {
		if ( attacker && attacker->client && attacker->s.number < MAX_CLIENTS
			&& Distance( attacker->r.currentOrigin, targ->r.currentOrigin ) <= SHINOBI_PARRY_REACH ) {
			Shinobi_Stagger( attacker, SHINOBI_PARRY_STAGGER_MS );
			trap->SendServerCommand( attacker - g_entities, "cp \"^3Parried!\"" );
		}
		trap->SendServerCommand( targ - g_entities, "cp \"^2Perfect parry!\"" );
		G_Sound( targ, CHAN_AUTO, shinobiMedia.sndParry );
		Shinobi_HitText( targ, STX_PARRY, 0 );
		Shinobi_ParryHold( targ );
		Shinobi_ShieldShell( targ, src, dmg );
		Shinobi_GainChakra( targ, SHINOBI_GAIN_PARRY, qtrue );
		shinobiTest.parries++;
		shinobiBlockResult = SBR_PARRIED; // (w_saber.c: no stock knockaway for him - he stays free to strike)
		return qtrue;
	}
	guard = client->ps.stats[STAT_ARMOR];
	client->shinobi.guardRegen = level.time + SHINOBI_GUARD_REGEN_DELAY;
	if ( attacker && attacker->client ) { // one swing, one price - by its style
		if ( client->shinobi.guardSwingBy == attacker->s.number + 1 && level.time < client->shinobi.guardSwingUntil ) {
			client->shinobi.guardSwingUntil = level.time + SHINOBI_GUARD_SWING_MS;
			return qtrue; // the same swing still touching
		}
		client->shinobi.guardSwingBy = attacker->s.number + 1;
		client->shinobi.guardSwingUntil = level.time + SHINOBI_GUARD_SWING_MS;
		switch ( attacker->client->ps.fd.saberAnimLevel ) {
		case SS_FAST:   dmg = SHINOBI_GUARD_SWING_FAST; break;
		case SS_MEDIUM: dmg = SHINOBI_GUARD_SWING_MEDIUM; break;
		case SS_STRONG: dmg = SHINOBI_GUARD_SWING_STRONG; break;
		default:        dmg = SHINOBI_GUARD_SWING_OTHER; break;
		}
		if ( attacker->s.number < MAX_CLIENTS ) { // taijutsu stat, Gates
			float f = 1.0f + 0.1f * attacker->client->pers.shinobiBuild.stats[SST_TAIJUTSU];
			if ( attacker->client->shinobi.gate >= 5 )
				f *= SHINOBI_GATE5_TAIJUTSU;
			dmg = (int)( dmg * f + 0.5f );
		}
	}
	shinobiTest.guardHits++;
	shinobiTest.guardCost += dmg < guard ? dmg : guard;
	if ( dmg >= guard ) { // the guard gives
		Shinobi_GuardBreak( targ );
		Shinobi_ShieldShell( targ, src, guard );
		Shinobi_GainChakra( attacker, SHINOBI_GAIN_GUARDBREAK, qtrue );
		return qfalse;
	}
	client->ps.stats[STAT_ARMOR] = guard - dmg;
	Shinobi_BladeBlocked( attacker, targ ); // a blow on the guard counts for his combo
	Shinobi_GainChakra( attacker, SHINOBI_GAIN_BLOCKED, qfalse );
	Shinobi_HitText( targ, STX_BLOCK, dmg );
	Shinobi_ShieldShell( targ, src, dmg );
	return qtrue;
}

/*
From G_Damage (after element and build modifiers). Returns what still goes
to health; 0 means the hit is fully stopped.
*/
int Shinobi_BlockDamage( gentity_t *targ, gentity_t *inflictor, gentity_t *attacker, int damage, int *dflags, int mod ) {
	gclient_t     *client;
	vec3_t         src;
	qboolean       taijutsu, perfect;
	int            absorb, cost, guard;

	shinobiBlockResult = SBR_NONE;
	if ( !targ || !targ->client || targ->s.number >= MAX_CLIENTS || damage <= 0 )
		return damage;
	client = targ->client;
	if ( !client->shinobi.blocking || !attacker || attacker == targ )
		return damage;
	if ( mod == MOD_SABER && inflictor == attacker && attacker->client && attacker->client->ps.weapon == WP_SABER
		&& client->ps.weapon == WP_SABER && !shinobiJutsuDamage )
		return damage; // blade on blade: the stock saber block decided already (Shinobi_SaberGuard) - this one got through
	switch ( mod ) { // the world, statuses and zones: no guard against those
	case MOD_FALLING: case MOD_LAVA: case MOD_WATER: case MOD_SLIME: case MOD_CRUSH:
	case MOD_TRIGGER_HURT: case MOD_SUICIDE: case MOD_TELEFRAG: case MOD_UNKNOWN:
		return damage;
	default:
		break;
	}
	if ( ( *dflags & DAMAGE_NO_ARMOR ) && mod != MOD_MELEE && mod != MOD_SABER )
		return damage; // burning, drowning, choking ash...

	if ( inflictor && inflictor != attacker && inflictor->inuse )
		VectorCopy( inflictor->r.currentOrigin, src );
	else
		VectorCopy( attacker->r.currentOrigin, src );
	if ( !Shinobi_FacingHit( targ, src ) )
		return damage; // from the side or behind

	taijutsu = ( mod == MOD_MELEE || mod == MOD_SABER ) && !shinobiJutsuDamage;
	perfect = level.time - client->shinobi.blockPress <= SHINOBI_PARRY_MS
		&& client->shinobi.blockPress - client->shinobi.blockPrevPress >= SHINOBI_PARRY_SPAM_MS;

	if ( shinobiGuardBreaker && !perfect ) { // a full Rasengan / Chidori goes straight through a guard (not a perfect parry: that stops all)
		Shinobi_GuardBreak( targ );
		Shinobi_GainChakra( attacker, SHINOBI_GAIN_GUARDBREAK, qtrue );
		shinobiBlockResult = SBR_BROKEN;
		return damage;
	}

	*dflags |= DAMAGE_NO_KNOCKBACK;
	if ( perfect ) {
		shinobiBlockResult = SBR_PARRIED;
		Shinobi_BlockFeedback( targ, src, qtrue );
		Shinobi_HitText( targ, STX_PARRY, 0 );
		if ( attacker->client && attacker->s.number < MAX_CLIENTS
			&& Distance( attacker->r.currentOrigin, targ->r.currentOrigin ) <= SHINOBI_PARRY_REACH ) {
			Shinobi_Stagger( attacker, SHINOBI_PARRY_STAGGER_MS );
			trap->SendServerCommand( attacker - g_entities, "cp \"^3Parried!\"" );
		}
		trap->SendServerCommand( targ - g_entities, "cp \"^2Perfect parry!\"" );
		Shinobi_ParryHold( targ );
		Shinobi_ShieldShell( targ, src, damage );
		Shinobi_GainChakra( targ, SHINOBI_GAIN_PARRY, qtrue );
		return 0;
	}
	absorb = taijutsu ? damage : ( damage + 1 ) / 2;
	cost = absorb;
	if ( mod == MOD_MELEE && attacker->client && attacker->s.number < MAX_CLIENTS && attacker->client->ps.weapon == WP_MELEE )
		cost = (int)( cost * shinobiFistGuard[SHINOBI_FIST_STYLE( &attacker->client->ps )] + 0.5f ); // heavier styles eat the guard
	if ( mod == MOD_MELEE && attacker->client && attacker->s.number < MAX_CLIENTS
		&& attacker->client->shinobi.eyes == SP_BYAKUGAN )
		cost *= 2; // the Gentle Fist (Byakugan open) goes through guards
	guard = client->ps.stats[STAT_ARMOR];
	client->shinobi.guardRegen = level.time + SHINOBI_GUARD_REGEN_DELAY;
	Shinobi_BlockFeedback( targ, src, qfalse );

	if ( cost >= guard ) { // the guard gives: what it couldn't hold goes through
		const int held = cost > 0 ? absorb * guard / cost : 0;
		Shinobi_GuardBreak( targ );
		shinobiBlockResult = SBR_BROKEN;
		Shinobi_ShieldShell( targ, src, held );
		Shinobi_GainChakra( attacker, SHINOBI_GAIN_GUARDBREAK, qtrue );
		return damage - held;
	}
	client->ps.stats[STAT_ARMOR] = guard - cost;
	Shinobi_ShieldShell( targ, src, absorb );
	Shinobi_HitText( targ, STX_BLOCK, absorb );
	Shinobi_GainChakra( attacker, SHINOBI_GAIN_BLOCKED, qfalse );
	shinobiBlockResult = SBR_BLOCKED;
	return damage - absorb;
}
