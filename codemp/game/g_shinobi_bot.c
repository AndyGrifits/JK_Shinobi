/*
===========================================================================
JK_Shinobi - bots. Bots get a random ninja build every spawn (or the one in
the "shinobi_botbuild" cvar), and a spread of force powers they never
really use: whenever the bot AI decides to use a force power, the nearest
jutsu of its build is cast instead (push -> wind / wave, grip -> water
prison / shadow, lightning -> fireball / fire breath / bullets, ...). So the
stock bot logic decides *when*; we only translate *what*.
===========================================================================
*/
#include "g_shinobi_local.h"

void Cmd_JutsuRelease_f( gentity_t *ent );

// what each force power turns into (the first ones the build has are tried in random order)
static const int botPush[]      = { SJ_FUTON, SJ_WAVE, SJ_BLADE, SJ_WINDWALL, SJ_QUAKE, SJ_SUITON, 0 };
static const int botPull[]      = { SJ_SUIRO, SJ_SHADOW, SJ_PILLAR, SJ_HIRAISHIN, 0 };
static const int botGrip[]      = { SJ_SUIRO, SJ_SHADOW, SJ_PILLAR, SJ_BURROW, 0 };
static const int botLightning[] = { SJ_RAIDASH, SJ_KATON, SJ_FLAME, SJ_SUITON, SJ_BLADE, SJ_CHIDORI, SJ_RASENGAN, SJ_PILLAR, SJ_ASH, SJ_RAITRAP, SJ_QUAKE, 0 };
static const int botDrain[]     = { SJ_FLAME, SJ_ASH, SJ_MIST, SJ_WAVE, SJ_CHIDORI, SJ_RASENGAN, 0 };
static const int botRage[]      = { SJ_RAICLONE, SJ_WINDEDGE, SJ_RAIEDGE, SJ_ARMOR, SJ_EARTHCLONE, SJ_BUNSHIN, SJ_WATERCLONE, 0 };
static const int botProtect[]   = { SJ_FLASH, SJ_ARMOR, SJ_WINDWALL, SJ_KAWARIMI, SJ_DOTON, SJ_MIST, SJ_EARTHCLONE, 0 };
static const int botTrick[]     = { SJ_SHADOWHIDE, SJ_FLASH, SJ_RAICLONE, SJ_KAWARIMI, SJ_BUNSHIN, SJ_WATERCLONE, SJ_EARTHCLONE, SJ_MIST, SJ_BURROW, 0 };
static const int botSpeed[]     = { SJ_RAIDASH, SJ_SHUNSHIN, SJ_HIRAISHIN, SJ_BURROW, 0 };

// the powers a bot "knows" so that the bot AI ever reaches for them
static const int botPowers[] = { FP_PUSH, FP_PULL, FP_SPEED, FP_SEE, FP_GRIP, FP_LIGHTNING, FP_RAGE, FP_DRAIN,
	FP_ABSORB, FP_PROTECT, FP_TELEPATHY };

static void Shinobi_BotRandomBuild( shinobiBuild_t *b ) {
	char forced[256];
	int  schools[2], pool[SJ_COUNT], n = 0, i, k, points;

	trap->Cvar_VariableStringBuffer( "shinobi_botbuild", forced, sizeof( forced ) );
	if ( forced[0] ) { // for testing one thing against bots
		Shinobi_BuildParse( forced, b );
		Shinobi_BuildFix( b );
		return;
	}
	memset( b, 0, sizeof( *b ) );
	schools[0] = Q_irand( SS_KATON, SS_YINYANG );
	do {
		schools[1] = Q_irand( SS_KATON, SS_YINYANG );
	} while ( schools[1] == schools[0] );
	for ( i = 0; i < SHINOBI_CYCLE_COUNT; i++ ) {
		const int j = shinobiJutsuCycle[i];
		if ( shinobiJutsuSchool[j] == schools[0] || shinobiJutsuSchool[j] == schools[1] )
			pool[n++] = j;
	}
	for ( i = n - 1; i > 0; i-- ) { // shuffle
		const int r = Q_irand( 0, i ), t = pool[i];
		pool[i] = pool[r];
		pool[r] = t;
	}
	for ( i = 0; i < n && b->numJutsu < SHINOBI_BUILD_JUTSU; i++ )
		b->jutsu[b->numJutsu++] = pool[i];
	for ( points = 0; points < SHINOBI_STAT_POINTS; ) {
		k = Q_irand( 0, SST_COUNT - 1 );
		if ( b->stats[k] < SHINOBI_STAT_MAX ) {
			b->stats[k]++;
			points++;
		}
	}
	do {
		b->perk = Q_irand( SP_NONE, SP_COUNT - 1 );
	} while ( !shinobiPerkReady[b->perk] );
	Shinobi_BuildFix( b );
}

// called from Shinobi_ApplyBuild before the build is copied in
void Shinobi_BotPrepare( gentity_t *ent ) {
	gclient_t *client = ent->client;
	char       str[256];

	if ( !( ent->r.svFlags & SVF_BOT ) )
		return;
	Shinobi_BotRandomBuild( &client->pers.shinobiBuildNext );
	client->pers.shinobiBuildSet = qtrue;
	Shinobi_BuildString( &client->pers.shinobiBuildNext, str, sizeof( str ) );
	G_LogPrintf( "[shinobi] bot %s build: %s\n", client->pers.netname, str );
}

// after the build is applied: the force powers the bot AI will "use"
void Shinobi_BotForcePowers( gentity_t *ent ) {
	gclient_t *client = ent->client;
	int        i;

	if ( !( ent->r.svFlags & SVF_BOT ) )
		return;
	for ( i = 0; i < (int)ARRAY_LEN( botPowers ); i++ ) {
		client->ps.fd.forcePowersKnown |= ( 1 << botPowers[i] );
		if ( client->ps.fd.forcePowerLevel[botPowers[i]] < FORCE_LEVEL_1 )
			client->ps.fd.forcePowerLevel[botPowers[i]] = FORCE_LEVEL_1;
	}
	if ( Shinobi_BuildHas( &client->pers.shinobiBuild, SJ_HEAL ) ) { // the AI heals when hurt (needs level 2)
		client->ps.fd.forcePowersKnown |= ( 1 << FP_HEAL );
		if ( client->ps.fd.forcePowerLevel[FP_HEAL] < FORCE_LEVEL_2 )
			client->ps.fd.forcePowerLevel[FP_HEAL] = FORCE_LEVEL_2;
	}
	else {
		client->ps.fd.forcePowersKnown &= ~( 1 << FP_HEAL );
	}
	// dark: grip / lightning / rage / drain (offence); light: absorb / protect / trick (tricks)
	client->ps.fd.forceSide = Q_irand( 0, 9 ) < 6 ? FORCE_DARKSIDE : FORCE_LIGHTSIDE;
}

static int Shinobi_BotPick( gentity_t *ent, const int *list ) {
	const shinobiBuild_t *b = &ent->client->pers.shinobiBuild;
	int have[16], n = 0, i;

	for ( i = 0; list[i] && n < (int)ARRAY_LEN( have ); i++ ) {
		if ( Shinobi_BuildHas( b, list[i] ) && !SHINOBI_JUTSU_PASSIVE( list[i] ) )
			have[n++] = list[i];
	}
	return n ? have[Q_irand( 0, n - 1 )] : SJ_NONE;
}

/*
From WP_DoSpecificPower, for bots: cast a jutsu instead of the force power.
Returns qtrue when the force power must not run (always, but for jumping).
*/
qboolean Shinobi_BotForce( gentity_t *ent, int forcePower ) {
	gclient_t  *client = ent->client;
	const int  *list = NULL;
	int         jutsu, lvl;

	if ( !( ent->r.svFlags & SVF_BOT ) || forcePower == FP_LEVITATION )
		return qfalse;
	if ( !Shinobi_CanAct( ent ) || client->shinobi.chargeJutsu != SJ_NONE || client->shinobi.botJutsuNext > level.time )
		return qtrue;

	switch ( forcePower ) {
	case FP_PUSH:      list = botPush; break;
	case FP_PULL:      list = botPull; break;
	case FP_GRIP:      list = botGrip; break;
	case FP_LIGHTNING: list = botLightning; break;
	case FP_DRAIN:     list = botDrain; break;
	case FP_RAGE:
		if ( client->pers.shinobiBuild.perk == SP_GATES && client->shinobi.gatesNext <= level.time ) {
			Cmd_Perk_f( ent );
			client->shinobi.botJutsuNext = level.time + 1000;
			return qtrue;
		}
		list = botRage;
		break;
	case FP_ABSORB:
	case FP_PROTECT:   list = botProtect; break;
	case FP_TELEPATHY: list = botTrick; break;
	case FP_SPEED:     list = botSpeed; break;
	case FP_SEE:
		if ( ( client->pers.shinobiBuild.perk == SP_SHARINGAN || client->pers.shinobiBuild.perk == SP_BYAKUGAN )
			&& !client->shinobi.eyes )
			Cmd_Perk_f( ent );
		client->shinobi.botJutsuNext = level.time + 1000;
		return qtrue;
	case FP_HEAL:
		if ( !Shinobi_BuildHas( &client->pers.shinobiBuild, SJ_HEAL ) )
			return qtrue;
		{
			static const int heal[] = { SJ_HEAL, 0 };
			list = heal;
		}
		break;
	default:
		return qtrue; // the rest: not for ninja
	}

	jutsu = Shinobi_BotPick( ent, list );
	if ( jutsu == SJ_NONE ) {
		client->shinobi.botJutsuNext = level.time + 400;
		return qtrue;
	}
	lvl = client->ps.fd.forcePower > 70 ? 3 : ( client->ps.fd.forcePower > 40 ? 2 : 1 );
	if ( lvl > 1 && Q_irand( 0, 2 ) == 0 )
		lvl--;
	if ( jutsu == SJ_SHUNSHIN )
		client->shinobi.shunshinBack = ent->health < 40; // hurt: get away

	if ( Shinobi_Cast( ent, jutsu, lvl ) ) {
		if ( ( jutsu == SJ_SUIRO && client->shinobi.prisonTarget ) || ( jutsu == SJ_FLAME && client->shinobi.flameEnt )
			|| ( jutsu == SJ_SHADOW && client->shinobi.shadowEnt ) || ( jutsu == SJ_BURROW && client->shinobi.burrowEnd )
			|| ( jutsu == SJ_SHADOWHIDE && client->shinobi.shadowHideEnd ) || ( jutsu == SJ_RAIDASH && client->shinobi.raiDashEnd ) ) {
			client->shinobi.chargeJutsu = jutsu; // held: let go a bit later (Shinobi_BotThink)
			client->shinobi.botHoldEnd = level.time + ( jutsu == SJ_RAIDASH ? Q_irand( 500, 1100 ) : Q_irand( 1200, 2800 ) );
		}
		client->shinobi.botJutsuNext = level.time + Q_irand( 1500, 3000 );
		Shinobi_Debug( "bot %i: force %i -> %s Lv%i", ent->s.number, forcePower, shinobiJutsuTitles[jutsu], lvl );
	}
	else {
		client->shinobi.botJutsuNext = level.time + 600;
	}
	return qtrue;
}

// every frame for bots: let go of held jutsu in time
void Shinobi_BotThink( gentity_t *ent ) {
	gclient_t *client = ent->client;

	if ( !( ent->r.svFlags & SVF_BOT ) )
		return;
	if ( client->shinobi.chargeJutsu != SJ_NONE && SHINOBI_JUTSU_HOLD( client->shinobi.chargeJutsu )
		&& level.time >= client->shinobi.botHoldEnd )
		Cmd_JutsuRelease_f( ent );
}
