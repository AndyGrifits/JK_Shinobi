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

	if ( !client || ent->s.number >= MAX_CLIENTS || ent->health <= 0 )
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

	if ( !client || ent->s.number >= MAX_CLIENTS || ent->health <= 0 )
		return;
	if ( client->shinobi.flinchEnd < level.time + ms )
		client->shinobi.flinchEnd = level.time + ms;
	client->ps.stats[STAT_SHINOBI2] |= SHINOBI_STAT2_FLINCH;
	Shinobi_HitText( ent, STX_FLINCH, 0 );
	if ( client->ps.weaponTime < ms )
		client->ps.weaponTime = ms;
	if ( client->ps.weapon == WP_SABER && BG_SaberInAttack( client->ps.saberMove ) )
		client->ps.saberBlocked = BLOCKED_ATK_BOUNCE;
	if ( client->shinobi.chargeJutsu != SJ_NONE && !SHINOBI_JUTSU_HOLD( client->shinobi.chargeJutsu ) ) {
		client->shinobi.chargeJutsu = SJ_NONE;
		Shinobi_StopCharging( client );
	}
}

static void Shinobi_GuardBreak( gentity_t *ent ) {
	gclient_t *client = ent->client;

	client->ps.stats[STAT_ARMOR] = 0;
	client->shinobi.blocking = qfalse;
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
		return 0;
	}
	if ( taijutsu && Shinobi_WindEdgeActive( attacker ) ) {
		*dflags &= ~DAMAGE_NO_KNOCKBACK;
		Shinobi_BlockFeedback( targ, src, qfalse );
		return damage; // the Wind Edge cuts straight through the guard
	}

	absorb = taijutsu ? damage : ( damage + 1 ) / 2;
	cost = absorb;
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
		return damage - held;
	}
	client->ps.stats[STAT_ARMOR] = guard - cost;
	Shinobi_ShieldShell( targ, src, absorb );
	Shinobi_HitText( targ, STX_BLOCK, absorb );
	shinobiBlockResult = SBR_BLOCKED;
	return damage - absorb;
}
