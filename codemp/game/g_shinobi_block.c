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
	if ( client->shinobi.chargeJutsu == SJ_QUAKE && ent->health > 0 )
		client->ps.stats[STAT_SHINOBI2] |= SHINOBI_STAT2_ROOTED; // the Earthquake charges standing still
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
void Shinobi_BladeFinisher( gentity_t *attacker, gentity_t *victim, float *dmg );

static int Shinobi_BladeStyle( gclient_t *c ) {
	const int style = c->ps.fd.saberAnimLevel;
	return ( style < 0 || style > 7 ) ? 2 : style;
}

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
	if ( level.time - c->shinobi.bladeChainTime > shinobiSwordWindow[Shinobi_BladeStyle( c )] )
		return qfalse;
	v = &g_entities[c->shinobi.bladeChainVictim - 1];
	return v->inuse && v->client && v->health > 0;
}

qboolean Shinobi_BladeChainLivePublic( gclient_t *c ) { return Shinobi_BladeChainLive( c ); }

// the trench knives' finisher: the swing after the chain throws a chakra blade, whether he is in reach or not
static void Shinobi_ReachFinisher( gentity_t *ent ) {
	gclient_t *c = ent->client;
	const int  style = Shinobi_BladeStyle( c );
	gentity_t *v;

	if ( c->saber[0].shinobiFinisher != SBF_REACH || !Shinobi_BladeChainLive( c )
		|| c->shinobi.bladeChain != shinobiSwordChain[style] - 1 )
		return;
	if ( c->pers.cmd.rightmove || c->pers.cmd.forwardmove < 0 || c->pers.cmd.upmove > 0 )
		return; // a direction held: one of the common finishers instead (on the blow itself)
	c->shinobi.bladeReachSwing = c->shinobi.bladeSwing;
	c->shinobi.bladeChain = 0;
	v = Shinobi_BladeSlash( ent, (int)( shinobiSwordDamage[style] * SHINOBI_SWORD_FINISH ), SHINOBI_SWORD_REACH, qtrue );
	shinobiTest.bladeFinishers[SBF_REACH]++;
	(void)v;
}

// a new swing: a new attack move, another anim, or the same one started over
void Shinobi_BladeSwingUpdate( gentity_t *ent ) {
	gclient_t *c = ent->client;
	const playerState_t *ps = &c->ps;

	if ( BG_SaberInAttack( ps->saberMove ) || BG_SaberInSpecial( ps->saberMove ) ) {
		if ( ps->saberMove != c->shinobi.bladeMove || ps->torsoAnim != c->shinobi.bladeAnim
			|| ps->torsoTimer > c->shinobi.bladeTimer + 50 ) {
			c->shinobi.bladeSwing++;
			if ( ps->weapon == WP_SABER && ent->s.number < MAX_CLIENTS )
				Shinobi_ReachFinisher( ent );
		}
	}
	c->shinobi.bladeMove = ps->saberMove;
	c->shinobi.bladeAnim = ps->torsoAnim;
	c->shinobi.bladeTimer = ps->torsoTimer;
}

float Shinobi_BladeDamage( gentity_t *attacker, gentity_t *victim, float stockDamage ) {
	gclient_t *ac = attacker->client, *vc = victim->client;
	const int  move = ac->ps.saberMove;
	const qboolean special = BG_SaberInSpecial( move ) || BG_SaberInKata( move );
	int        style = ac->ps.fd.saberAnimLevel, k, slot = -1, oldest = 0;
	float      dmg;

	(void)stockDamage;
	Shinobi_BladeSwingUpdate( attacker );
	if ( !BG_SaberInAttack( move ) && !special )
		return 0; // a touch with a blade at rest
	if ( ac->shinobi.bladeReachSwing == ac->shinobi.bladeSwing )
		return 0; // this swing threw the knives' chakra blade: that was its blow
	for ( k = 0; k < 4; k++ ) { // has this swing cut him already?
		if ( vc->shinobi.bladeHitBy[k] == attacker->s.number + 1 ) {
			slot = k;
			break;
		}
		if ( vc->shinobi.bladeHitTime[k] < vc->shinobi.bladeHitTime[oldest] )
			oldest = k;
	}
	if ( slot >= 0 && vc->shinobi.bladeHitSwing[slot] == ac->shinobi.bladeSwing
		&& ( !special || level.time - vc->shinobi.bladeHitTime[slot] < SHINOBI_BLADE_SPECIAL_MS ) )
		return 0;
	if ( slot < 0 )
		slot = oldest;
	vc->shinobi.bladeHitBy[slot] = attacker->s.number + 1;
	vc->shinobi.bladeHitSwing[slot] = ac->shinobi.bladeSwing;
	vc->shinobi.bladeHitTime[slot] = level.time;

	if ( style < 0 || style > 7 )
		style = 2;
	dmg = (float)shinobiSwordDamage[style];
	if ( special )
		dmg *= SHINOBI_BLADE_SPECIAL;
	if ( ac->saber[0].damageScale > 0 )
		dmg *= ac->saber[0].damageScale;

	// the combo: the same one, soon after the last blow
	if ( special ) {
		ac->shinobi.bladeChain = 0; // (a special move stands on its own)
	}
	else {
		if ( ac->shinobi.bladeChainVictim == victim->s.number + 1 && Shinobi_BladeChainLive( ac ) )
			ac->shinobi.bladeChain++;
		else
			ac->shinobi.bladeChain = 1;
		ac->shinobi.bladeChainVictim = victim->s.number + 1;
		ac->shinobi.bladeChainTime = level.time;
		if ( ac->shinobi.bladeChain >= shinobiSwordChain[style] ) {
			ac->shinobi.bladeChain = 0;
			dmg *= SHINOBI_SWORD_FINISH;
			Shinobi_BladeFinisher( attacker, victim, &dmg );
		}
		else if ( attacker->s.number < MAX_CLIENTS ) { // held until the next blow can land, as with fists
			int ms = ac->ps.weaponTime + SHINOBI_COMBO_FLINCH_MS;
			if ( ms < SHINOBI_COMBO_FLINCH_MIN )
				ms = SHINOBI_COMBO_FLINCH_MIN;
			if ( ms > SHINOBI_COMBO_FLINCH_MAX )
				ms = SHINOBI_COMBO_FLINCH_MAX;
			Shinobi_Flinch( victim, ms );
		}
	}
	return dmg;
}

/*
The finisher (the blow already counts x1.5): by what is held as it lands - back: a sweep;
left / right: a spin cutting everyone around; jump: a rising cut; nothing / forward: the
weapon's own.
*/
void Shinobi_BladeFinisher( gentity_t *attacker, gentity_t *victim, float *dmg ) {
	gclient_t *ac = attacker->client, *vc = victim->client;
	const usercmd_t *cmd = &ac->pers.cmd;
	vec3_t dir;
	int    i;

	VectorSubtract( victim->r.currentOrigin, attacker->r.currentOrigin, dir );
	dir[2] = 0;
	VectorNormalize( dir );
	vc->ps.otherKiller = attacker->s.number;
	vc->ps.otherKillerTime = level.time + 5000;
	vc->ps.otherKillerDebounceTime = level.time + 100;
	vc->shinobi.flinchEnd = 0; // (the finisher lets go)
	Shinobi_GainChakra( attacker, SHINOBI_GAIN_COMBO, qtrue );

	if ( cmd->upmove > 0 ) { // up he goes
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
		return;
	}
	if ( cmd->forwardmove < 0 ) { // low, at the legs: down
		shinobiTest.bladeFinishers[0]++;
		VectorScale( dir, 200, vc->ps.velocity );
		vc->ps.velocity[2] = 120;
		if ( !Shinobi_CCImmune( victim ) )
			G_Knockdown( victim );
		Shinobi_CCGrant( victim, level.time + 1500 );
		Shinobi_FinisherFx( attacker, victim, SFX_SWEEP );
		return;
	}
	if ( cmd->rightmove ) { // a full turn: everyone close takes it too
		shinobiTest.bladeFinishers[0]++;
		for ( i = 0; i < level.num_entities; i++ ) {
			gentity_t *o = &g_entities[i];
			vec3_t     d;
			if ( o == victim || o == attacker || !o->inuse || !o->client || o->health <= 0 || !o->takedamage )
				continue;
			if ( level.gametype >= GT_TEAM && OnSameTeam( attacker, o ) )
				continue;
			if ( o->shinobiCloneOwner && o->shinobiCloneOwner == attacker->s.number + 1 )
				continue;
			VectorSubtract( o->r.currentOrigin, attacker->r.currentOrigin, d );
			if ( fabs( d[2] ) > 48 )
				continue;
			d[2] = 0;
			if ( VectorLength( d ) > SHINOBI_SWORD_SPIN_RADIUS )
				continue;
			G_Damage( o, attacker, attacker, d, o->r.currentOrigin, (int)*dmg, DAMAGE_NO_HIT_LOC, MOD_SABER );
		}
		Shinobi_FinisherFx( attacker, victim, SFX_SPIN );
		return;
	}

	shinobiTest.bladeFinishers[ac->saber[0].shinobiFinisher]++;
	switch ( ac->saber[0].shinobiFinisher ) {
	case SBF_DRAIN: { // Samehada eats his chakra
		const int take = vc->ps.fd.forcePower < SHINOBI_SWORD_DRAIN ? vc->ps.fd.forcePower : SHINOBI_SWORD_DRAIN;
		vc->ps.fd.forcePower -= take;
		if ( take > 0 )
			Shinobi_GainChakra( attacker, (float)take, qtrue );
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
	default: // (the knives' finisher is the thrown blade: Shinobi_ReachFinisher)
		break;
	}
}

// every client frame: Kurosawa's extra cuts (his), a bleeding wound (on him)
void Shinobi_SwordThink( gentity_t *ent ) {
	gclient_t *c = ent->client;

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
		Shinobi_ShieldShell( targ, src, dmg );
		Shinobi_GainChakra( targ, SHINOBI_GAIN_PARRY, qtrue );
		shinobiTest.parries++;
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
			if ( attacker->client->shinobi.gatesEnd > level.time )
				f *= SHINOBI_GATES_TAIJUTSU;
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

	if ( shinobiGuardBreaker ) { // a full Rasengan / Chidori goes straight through
		Shinobi_GuardBreak( targ );
		Shinobi_GainChakra( attacker, SHINOBI_GAIN_GUARDBREAK, qtrue );
		shinobiBlockResult = SBR_BROKEN;
		return damage;
	}

	taijutsu = ( mod == MOD_MELEE || mod == MOD_SABER ) && !shinobiJutsuDamage;
	perfect = level.time - client->shinobi.blockPress <= SHINOBI_PARRY_MS
		&& client->shinobi.blockPress - client->shinobi.blockPrevPress >= SHINOBI_PARRY_SPAM_MS;

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
		Shinobi_ShieldShell( targ, src, damage );
		Shinobi_GainChakra( targ, SHINOBI_GAIN_PARRY, qtrue );
		return 0;
	}
	if ( taijutsu && Shinobi_WindEdgeActive( attacker ) ) {
		*dflags &= ~DAMAGE_NO_KNOCKBACK;
		Shinobi_BlockFeedback( targ, src, qfalse );
		return damage; // the Wind Edge cuts straight through the guard
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
