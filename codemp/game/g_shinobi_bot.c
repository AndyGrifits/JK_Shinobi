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
static const int botProtect[]   = { SJ_FLASH, SJ_ARMOR, SJ_WINDWALL, SJ_KAWARIMI, SJ_DOTON, SJ_MIST, SJ_EARTHCLONE, SJ_KAITEN, 0 };
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
	do { // the special first: it decides how many points are left
		b->perk = Q_irand( SP_NONE, SP_COUNT - 1 );
	} while ( !shinobiPerkReady[b->perk] );
	for ( points = 0; points < Shinobi_StatBudget( b ); ) {
		k = Q_irand( 0, SST_COUNT - 1 );
		if ( b->stats[k] < Shinobi_StatCap( b, k ) ) {
			b->stats[k]++;
			points++;
		}
	}
	Shinobi_BuildFix( b );
}

/*
Bots wear our characters (every models/players/<name> with a shinobi.char, as on the
character screen) instead of the stock ones from bots.txt: one nobody else wears yet if
there is one, the sex from the file. Returns qtrue when it changed the userinfo.
*/
#define BOT_MAX_CHARS 64
static struct {
	char     dir[BOT_MAX_CHARS][32];
	qboolean female[BOT_MAX_CHARS];
	int      n;
	qboolean listed;
} botChars;

static void Shinobi_BotListChars( void ) {
	static char dirs[8192];
	char       *d;
	int         nd, i;

	botChars.listed = qtrue;
	botChars.n = 0;
	nd = trap->FS_GetFileList( "models/players", "/", dirs, sizeof( dirs ) );
	for ( i = 0, d = dirs; i < nd && botChars.n < BOT_MAX_CHARS; i++, d += strlen( d ) + 1 ) {
		char         name[32], text[128];
		fileHandle_t fh;
		int          len = 0;
		Q_strncpyz( name, d, sizeof( name ) );
		len = strlen( name );
		if ( len && ( name[len - 1] == '/' || name[len - 1] == '\\' ) )
			name[--len] = 0;
		if ( !name[0] || name[0] == '.' || name[0] == '_' )
			continue;
		len = trap->FS_Open( va( "models/players/%s/shinobi.char", name ), &fh, FS_READ );
		if ( !fh )
			continue;
		text[0] = 0;
		if ( len > 0 ) {
			if ( len > (int)sizeof( text ) - 1 )
				len = sizeof( text ) - 1;
			trap->FS_Read( text, len, fh );
			text[len] = 0;
			Q_strlwr( text );
		}
		trap->FS_Close( fh );
		Q_strncpyz( botChars.dir[botChars.n], name, sizeof( botChars.dir[0] ) );
		botChars.female[botChars.n] = strstr( text, "\nfemale" ) != NULL;
		botChars.n++;
	}
	G_LogPrintf( "[shinobi] %i characters for the bots\n", botChars.n );
}

qboolean Shinobi_BotModel( gentity_t *ent, char *userinfo, int size ) {
	char model[MAX_QPATH], *slash;
	int  i, j, pick, free[BOT_MAX_CHARS], nfree = 0;

	if ( !botChars.listed )
		Shinobi_BotListChars();
	if ( !botChars.n )
		return qfalse;
	Q_strncpyz( model, Info_ValueForKey( userinfo, "model" ), sizeof( model ) );
	if ( ( slash = strchr( model, '/' ) ) != NULL )
		*slash = 0;
	for ( i = 0; i < botChars.n; i++ )
		if ( !Q_stricmp( model, botChars.dir[i] ) )
			return qfalse; // one of ours already
	for ( i = 0; i < botChars.n; i++ ) { // not worn by anyone else yet
		qboolean taken = qfalse;
		for ( j = 0; j < MAX_CLIENTS && !taken; j++ ) {
			gentity_t *o = &g_entities[j];
			char       m[MAX_QPATH], info[MAX_INFO_STRING], *sl;
			if ( o == ent || !o->inuse || !o->client || o->client->pers.connected == CON_DISCONNECTED )
				continue;
			trap->GetUserinfo( j, info, sizeof( info ) );
			Q_strncpyz( m, Info_ValueForKey( info, "model" ), sizeof( m ) );
			if ( ( sl = strchr( m, '/' ) ) != NULL )
				*sl = 0;
			taken = !Q_stricmp( m, botChars.dir[i] );
		}
		if ( !taken )
			free[nfree++] = i;
	}
	pick = nfree ? free[Q_irand( 0, nfree - 1 )] : Q_irand( 0, botChars.n - 1 );
	Info_SetValueForKey( userinfo, "model", botChars.dir[pick] );
	Info_SetValueForKey( userinfo, "sex", botChars.female[pick] ? "female" : "male" );
	Info_SetValueForKey( userinfo, "char_color_red", "255" );
	Info_SetValueForKey( userinfo, "char_color_green", "255" );
	Info_SetValueForKey( userinfo, "char_color_blue", "255" );
	G_LogPrintf( "[shinobi] bot %s wears %s\n", ent->client ? ent->client->pers.netname : "?", botChars.dir[pick] );
	return qtrue;
}

void Shinobi_BotRandomBuildPublic( shinobiBuild_t *b ) { Shinobi_BotRandomBuild( b ); } // (the autotest)

// called from Shinobi_ApplyBuild before the build is copied in
void Shinobi_BotPrepare( gentity_t *ent ) {
	gclient_t *client = ent->client;
	char       str[256];

	if ( !( ent->r.svFlags & SVF_BOT ) )
		return;
	Shinobi_BotRandomBuild( &client->pers.shinobiBuildNext );
	client->pers.shinobiBuildSet = qtrue;
	// this life: blade or fists (and which fist style)
	client->shinobi.botFists = Q_irand( 0, 99 ) < shinobi_botfists.integer;
	client->shinobi.botFinisher = -1;
	client->shinobi.botAirUntil = 0;
	if ( client->shinobi.botFists )
		client->pers.shinobiFistStyle = Q_irand( 0, SFS_COUNT - 1 );
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
	if ( Shinobi_AutotestControls( ent ) )
		return qtrue; // a test drives it
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
		if ( ( client->pers.shinobiBuild.perk == SP_SHARINGAN || client->pers.shinobiBuild.perk == SP_BYAKUGAN
				|| client->pers.shinobiBuild.perk == SP_COPYEYE )
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

	if ( !SHINOBI_JUTSU_INSTANT( jutsu ) && !SHINOBI_JUTSU_HOLD( jutsu ) && jutsu != SJ_SHUNSHIN ) {
		// a charged jutsu: form the seals like a player (the Sharingan sees it coming), let go at the bar
		if ( lvl < SHINOBI_JUTSU_MIN_LEVEL( jutsu ) )
			lvl = SHINOBI_JUTSU_MIN_LEVEL( jutsu );
		if ( client->ps.fd.forcePower < Shinobi_JutsuCost( jutsu, lvl ) ) {
			client->shinobi.botJutsuNext = level.time + 600;
			return qtrue;
		}
		Shinobi_Reveal( ent );
		Shinobi_StopCharging( client );
		client->shinobi.chargeJutsu = jutsu;
		client->shinobi.chargeStart = level.time;
		client->shinobi.botHoldEnd = level.time + Shinobi_BarEndCast( jutsu, lvl - 1, Shinobi_CastStat( ent ) ) + 40;
		client->shinobi.botJutsuNext = client->shinobi.botHoldEnd + Q_irand( 1500, 3000 );
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndJutsuCharge );
		Shinobi_Debug( "bot %i: force %i -> charging %s to Lv%i", ent->s.number, forcePower, shinobiJutsuTitles[jutsu], lvl );
		return qtrue;
	}

	client->shinobi.castBack = 0;
	if ( Shinobi_Cast( ent, jutsu, lvl ) ) {
		if ( ( jutsu == SJ_SUIRO && client->shinobi.prisonTarget ) || ( jutsu == SJ_FLAME && client->shinobi.flameEnt )
			|| ( jutsu == SJ_SHADOW && client->shinobi.shadowEnt ) || ( jutsu == SJ_BURROW && client->shinobi.burrowEnd )
			|| ( jutsu == SJ_SHADOWHIDE && client->shinobi.shadowHideEnd ) || ( jutsu == SJ_RAIDASH && client->shinobi.raiDashEnd )
			|| ( jutsu == SJ_HEAL && client->shinobi.healing ) || ( jutsu == SJ_KAITEN && client->shinobi.kaitenStart ) ) {
			client->shinobi.chargeJutsu = jutsu; // held: let go a bit later (Shinobi_BotThink)
			client->shinobi.botHoldEnd = level.time + ( jutsu == SJ_RAIDASH ? Q_irand( 500, 1100 )
				: jutsu == SJ_KAITEN ? Q_irand( 1100, 1900 ) : Q_irand( 1200, 2800 ) );
		}
		client->shinobi.botJutsuNext = level.time + Q_irand( 1500, 3000 );
		Shinobi_Debug( "bot %i: force %i -> %s Lv%i", ent->s.number, forcePower, shinobiJutsuTitles[jutsu], lvl );
	}
	else {
		client->shinobi.botJutsuNext = level.time + 600;
	}
	return qtrue;
}

/*
Bots defend themselves like players: they raise the guard against a blow coming
at them (shinobi_botguard % of the time, held a moment - a press right before the
hit is a perfect parry), let it down when it is almost gone, Kawarimi out of a
combo once they are caught in it, and hit back at someone reeling (parried,
guard broken). Every client frame, before the guard code reads the buttons.
*/
static qboolean Shinobi_BotThreat( gentity_t *ent, gentity_t *e ) {
	const playerState_t *eps;
	vec3_t d, fwd, yaw;

	if ( !e->inuse || !e->client || e == ent || e->health <= 0 || e->client->sess.sessionTeam == TEAM_SPECTATOR )
		return qfalse;
	if ( level.gametype >= GT_TEAM && OnSameTeam( ent, e ) )
		return qfalse;
	eps = &e->client->ps;
	VectorSubtract( ent->r.currentOrigin, e->r.currentOrigin, d );
	if ( VectorLength( d ) > ( eps->weapon == WP_SABER ? 150 : 110 ) )
		return qfalse;
	VectorNormalize( d );
	VectorSet( yaw, 0, eps->viewangles[YAW], 0 );
	AngleVectors( yaw, fwd, NULL, NULL );
	if ( DotProduct( d, fwd ) < 0.5f )
		return qfalse; // not turned at us
	if ( eps->weapon == WP_SABER && ( BG_SaberInAttack( eps->saberMove ) || BG_SaberInSpecial( eps->saberMove ) ) )
		return qtrue;
	if ( eps->weapon == WP_MELEE && eps->weaponTime > 0 )
		return qtrue; // a punch or a kick on its way
	if ( BG_KickingAnim( eps->legsAnim ) )
		return qtrue;
	return qfalse;
}

static void Shinobi_BotFists( gentity_t *ent, usercmd_t *ucmd );

void Shinobi_BotDefend( gentity_t *ent, usercmd_t *ucmd ) {
	gclient_t *client = ent->client;
	gentity_t *threat = NULL, *reeling = NULL;
	int        i;

	if ( !( ent->r.svFlags & SVF_BOT ) || ent->health <= 0 || client->sess.sessionTeam == TEAM_SPECTATOR
		|| Shinobi_AutotestControls( ent ) )
		return;
	if ( client->shinobi.botFists && !SHINOBI_IS_TOOL_WEAPON( ucmd->weapon ) )
		ucmd->weapon = WP_MELEE; // a bare-handed life: whatever the AI would pick (Shinobi_BotFists)

	// caught in a combo: after the second blow (counted in Shinobi_Flinch), maybe Kawarimi out
	if ( client->shinobi.flinchEnd > level.time && client->shinobi.botFlinches >= 2 ) {
		client->shinobi.botFlinches = 0;
		if ( Q_irand( 0, 99 ) < 50 ) {
			Shinobi_Cast( ent, SJ_KAWARIMI, 1 );
			return;
		}
	}
	// down on the floor with someone over us: now and then Kawarimi
	if ( client->ps.forceHandExtend == HANDEXTEND_KNOCKDOWN && client->shinobi.botGuardNext <= level.time ) {
		client->shinobi.botGuardNext = level.time + 600;
		for ( i = 0; i < MAX_CLIENTS; i++ ) {
			gentity_t *e = &g_entities[i];
			if ( e != ent && e->inuse && e->client && e->health > 0 && !( level.gametype >= GT_TEAM && OnSameTeam( ent, e ) )
				&& Distance( e->r.currentOrigin, ent->r.currentOrigin ) < 120 && Q_irand( 0, 99 ) < 25 ) {
				Shinobi_Cast( ent, SJ_KAWARIMI, 1 );
				return;
			}
		}
	}

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		gentity_t *e = &g_entities[i];
		if ( Shinobi_BotThreat( ent, e ) ) {
			threat = e;
			break;
		}
		if ( e != ent && e->inuse && e->client && e->health > 0 && ( e->client->ps.stats[STAT_SHINOBI] & SHINOBI_STAT_STAGGER )
			&& Distance( e->r.currentOrigin, ent->r.currentOrigin ) < 120 && !( level.gametype >= GT_TEAM && OnSameTeam( ent, e ) ) )
			reeling = e;
	}

	// a blow coming: maybe the guard (decided once per blow, held a moment)
	if ( threat && client->shinobi.botGuardNext <= level.time && client->shinobi.botGuardUntil <= level.time ) {
		client->shinobi.botGuardNext = level.time + 450;
		if ( client->ps.stats[STAT_ARMOR] * 4 > SHINOBI_GUARD_MAX && Q_irand( 0, 99 ) < shinobi_botguard.integer ) {
			client->shinobi.botGuardUntil = level.time + Q_irand( 350, 800 );
			shinobiTest.botGuards++;
		}
	}
	if ( client->shinobi.botGuardUntil > level.time ) {
		if ( client->ps.stats[STAT_ARMOR] * 6 < SHINOBI_GUARD_MAX ) {
			client->shinobi.botGuardUntil = 0; // almost gone: let it down
		}
		else {
			ucmd->buttons |= SHINOBI_BUTTON_BLOCK;
			ucmd->buttons &= ~( BUTTON_ATTACK | BUTTON_ALT_ATTACK );
			ucmd->upmove = 0;
			return;
		}
	}

	// someone reeling in front of us: hit him
	if ( reeling && !threat ) {
		client->shinobi.botPunishUntil = level.time + 400;
	}
	if ( client->shinobi.botPunishUntil > level.time )
		ucmd->buttons |= BUTTON_ATTACK;

	Shinobi_BotFists( ent, ucmd );
}

/*
A bot fighting bare-handed this life (shinobi_botfists % of lives): fists in hand
(whatever the AI picks), the attack held while an enemy is in reach so the chain runs,
a finisher picked per combo - nothing (front kick), forward (launcher), back (sweep) or a
side (spin kick); after its own launcher it jumps after the victim and hits on (the air
combo ends in the slam). Lying enemies get the ground blow (attack next to them).
*/
static void Shinobi_BotFists( gentity_t *ent, usercmd_t *ucmd ) {
	gclient_t *client = ent->client;
	gentity_t *target = NULL;
	float      best = SHINOBI_PUNCH_RANGE + 46;
	vec3_t     fwd, yaw = { 0, 0, 0 };
	int        i, hit, chain;

	if ( !client->shinobi.botFists || ent->health <= 0 || !Shinobi_CanAct( ent ) )
		return;
	if ( !( client->ps.stats[STAT_WEAPONS] & ( 1 << WP_MELEE ) ) )
		client->ps.stats[STAT_WEAPONS] |= 1 << WP_MELEE;
	if ( SHINOBI_IS_TOOL_WEAPON( ucmd->weapon ) && client->ps.weapon == ucmd->weapon )
		return; // throwing a tool right now: let it
	ucmd->weapon = WP_MELEE;
	if ( client->ps.weapon != WP_MELEE )
		return; // still switching

	yaw[YAW] = client->ps.viewangles[YAW];
	AngleVectors( yaw, fwd, NULL, NULL );
	for ( i = 0; i < MAX_CLIENTS; i++ ) { // the nearest enemy in reach, roughly in front
		gentity_t *e = &g_entities[i];
		vec3_t     d;
		float      dist;
		if ( e == ent || !e->inuse || !e->client || e->health <= 0 || e->client->sess.sessionTeam == TEAM_SPECTATOR )
			continue;
		if ( level.gametype >= GT_TEAM && OnSameTeam( ent, e ) )
			continue;
		VectorSubtract( e->client->ps.origin, client->ps.origin, d );
		if ( fabs( d[2] ) > 80 && client->shinobi.botAirUntil <= level.time )
			continue;
		dist = VectorNormalize( d );
		if ( dist > best || DotProduct( d, fwd ) < 0.5f )
			continue;
		best = dist;
		target = e;
	}

	// the air combo: up after the one we launched, hitting on
	if ( client->shinobi.botAirUntil > level.time ) {
		if ( client->ps.groundEntityNum != ENTITYNUM_NONE && level.time + 300 > client->shinobi.botAirUntil - 450 )
			ucmd->upmove = 127; // the jump right after the launcher
		else
			ucmd->upmove = 0;
		ucmd->buttons = ( ucmd->buttons & ~( BUTTON_ATTACK | SHINOBI_BUTTON_BLOCK ) ) | ( ( level.time / 120 ) % 2 ? BUTTON_ATTACK : 0 );
		ucmd->forwardmove = ucmd->rightmove = 0;
		return;
	}
	if ( !target ) {
		client->shinobi.botFinisher = -1;
		return;
	}
	if ( ucmd->buttons & SHINOBI_BUTTON_BLOCK )
		return; // guarding (Shinobi_BotDefend)

	// hold attack: the chain goes on by itself
	ucmd->buttons |= BUTTON_ATTACK;
	ucmd->buttons &= ~BUTTON_ALT_ATTACK;
	hit = SHINOBI_COMBO_HIT( &client->ps );
	chain = shinobiFistChain[SHINOBI_FIST_STYLE( &client->ps )];
	if ( hit == 0 && client->ps.stats[STAT_SHINOBI_COMBOT] <= 0 )
		client->shinobi.botFinisher = -1;
	if ( hit >= chain - 1 ) { // the next blow is the finisher: its direction
		if ( client->shinobi.botFinisher < 0 ) {
			const int r = Q_irand( 0, 99 );
			client->shinobi.botFinisher = r < 30 ? 0 : r < 60 ? 1 : r < 80 ? 2 : 3;
		}
		ucmd->forwardmove = ucmd->rightmove = 0;
		switch ( client->shinobi.botFinisher ) {
		case 1: ucmd->forwardmove = 127; break;
		case 2: ucmd->forwardmove = -127; break;
		case 3: ucmd->rightmove = Q_irand( 0, 1 ) ? 127 : -127; break;
		default: break;
		}
	}
	else if ( best < SHINOBI_PUNCH_RANGE + 8 )
		ucmd->forwardmove = 0; // in reach: stand and hit
	// our launcher just went up: the air combo
	if ( target->client->shinobi.juggleBy == ent->s.number + 1 && target->client->shinobi.juggleEnd > level.time
		&& level.time - target->client->shinobi.juggleStart < 200 )
		client->shinobi.botAirUntil = level.time + 1800;
}

// every frame for bots: let go of held jutsu in time
void Shinobi_BotThink( gentity_t *ent ) {
	gclient_t *client = ent->client;

	if ( !( ent->r.svFlags & SVF_BOT ) || Shinobi_AutotestControls( ent ) )
		return;
	if ( client->shinobi.chargeJutsu != SJ_NONE && level.time >= client->shinobi.botHoldEnd )
		Cmd_JutsuRelease_f( ent ); // a held jutsu let go / a charged one cast at its bar
}
