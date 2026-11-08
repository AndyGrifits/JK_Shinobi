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

// the tactics (further down) that the force-power mapping asks too
static qboolean   BotChance( int pct );
static int        BotSmart( void );
static qboolean   BotIsEnemy( gentity_t *ent, gentity_t *e );
static qboolean   BotFacesUs( gentity_t *ent, gentity_t *e, float dot );
static gentity_t *BotNearest( gentity_t *ent, float maxDist, gentity_t *skip, float *distOut );
gentity_t        *Shinobi_BotEnemyOf( int clientNum ); // ai_main.c

// a jutsu that flies at him: a spinning Kaiten glances it off, a Wind Wall throws it back
static qboolean BotProjectileJutsu( int j ) {
	return j == SJ_KATON || j == SJ_SUITON || j == SJ_BLADE || j == SJ_WAVE;
}
static qboolean BotWindWallNear( gentity_t *foe ) {
	int i;
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *w = &g_entities[i];
		if ( w->inuse && w->classname && w->r.ownerNum == foe->s.number && !strcmp( w->classname, "shinobi_windwall" )
			&& Distance( w->r.currentOrigin, foe->r.currentOrigin ) < 220 )
			return qtrue;
	}
	return qfalse;
}

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
	do { // the special first: it decides how many points (and techniques) are left
		b->perk = Q_irand( SP_NONE, SP_COUNT - 1 );
	} while ( !shinobiPerkReady[b->perk] );
	for ( i = 0; i < n && b->numJutsu < Shinobi_JutsuCap( b ); i++ )
		b->jutsu[b->numJutsu++] = pool[i];
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
		if ( client->pers.shinobiBuild.perk == SP_GATES && client->shinobi.gate < SHINOBI_GATE_MAX
			&& ( client->shinobi.gate ? client->shinobi.gateDealt >= SHINOBI_GATE_NEED( client->shinobi.gate ) : client->shinobi.gateNext <= level.time ) ) {
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
	if ( BotSmart() > 0 && jutsu == SJ_KAWARIMI ) { // kept for the way out of traps and blows (the tactics): not at random
		client->shinobi.botJutsuNext = level.time + 400;
		return qtrue;
	}
	if ( BotSmart() > 0 ) {
		gentity_t *foe = Shinobi_BotEnemyOf( ent->s.number );
		gentity_t *close = BotNearest( ent, 140, NULL, NULL );
		// seals formed right in his face: the first blow breaks them - not now
		if ( close && !SHINOBI_JUTSU_INSTANT( jutsu ) && !SHINOBI_JUTSU_HOLD( jutsu ) && jutsu != SJ_SHUNSHIN
			&& BotFacesUs( ent, close, 0.5f ) && BotChance( 80 ) ) {
			client->shinobi.botJutsuNext = level.time + 500;
			return qtrue;
		}
		// into a spinning Kaiten / a Wind Wall it only glances off or comes back
		if ( BotProjectileJutsu( jutsu ) && BotIsEnemy( ent, foe ) && ( Shinobi_KaitenSpinning( foe ) || BotWindWallNear( foe ) )
			&& BotChance( 85 ) ) {
			client->shinobi.botJutsuNext = level.time + 500;
			return qtrue;
		}
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

/*
===========================================================================
Bot tactics: reading the fight the way a player who knows the mechanics does
(shinobi_botsmart 0-100: how often and how fast they see it; 0 = the stock AI
and the defence below only). Every client frame, before the guard and the
status code read the buttons:
- caught (Water Prison, Shadow Bind, Shadow Trap, juggled in an air combo):
  Kawarimi out after a short reaction, while a charge is ready;
- on fire: roll the flames out;
- holding a jutsu (prison, bind, healing, burrow, shadow hide, fire breath,
  Kaiten, 64 Seals, lightning dash): no stray key that would end it, and let go
  when it pays - the prison when the chakra runs low, someone else comes or it
  has drowned him long enough (then hit him while he gasps), healing when full
  or someone is close, the burrow right under him, the shadow hide when he
  walks past, the fire breath and Kaiten when nobody is left in them;
- someone forming seals: break them - any direct hit does (Dynamic Entry or
  running in with blows close by, a kunai from afar);
- a projectile or a Rasengan / Chidori about to hit: sidestep and jump, or
  Kawarimi when it is too late for that;
- an enemy spinning a Kaiten or standing in his 64 Seals field: keep off;
- a teammate held in a prison / bind: go for the one holding him;
- openings: Reverse Lotus and Dynamic Entry when he is where they reach,
  64 Seals when he is close, healing when hurt and nobody is near.
===========================================================================
*/
void       Shinobi_BotFocus( int clientNum, gentity_t *enemy ); // ai_main.c
gentity_t *Shinobi_BotEnemyOf( int clientNum );

static int BotSmart( void ) {
	return shinobi_botsmart.integer < 0 ? 0 : shinobi_botsmart.integer > 100 ? 100 : shinobi_botsmart.integer;
}
// a chance scaled by shinobi_botsmart
static qboolean BotChance( int pct ) {
	return Q_irand( 0, 99 ) < pct * BotSmart() / 100;
}
// a reaction time: fastMs for the sharpest bot, slowMs for a dull one
static int BotReact( int fastMs, int slowMs ) {
	return slowMs - ( slowMs - fastMs ) * BotSmart() / 100;
}

static int BotCharges( const int *ready, int n ) {
	int i, k = 0;
	for ( i = 0; i < n; i++ )
		if ( ready[i] <= level.time )
			k++;
	return k;
}

static qboolean BotIsEnemy( gentity_t *ent, gentity_t *e ) {
	if ( !e || e == ent || !e->inuse || !e->client || e->s.number >= MAX_CLIENTS || e->health <= 0 )
		return qfalse;
	if ( e->client->sess.sessionTeam == TEAM_SPECTATOR || e->client->tempSpectate >= level.time )
		return qfalse;
	if ( level.gametype >= GT_TEAM && OnSameTeam( ent, e ) )
		return qfalse;
	if ( SHINOBI_UNSEEN( &e->client->ps ) && !ent->client->shinobi.eyes )
		return qfalse; // substituted away, in the shadow, under the ground: gone (open eyes still see him)
	return qtrue;
}

// he can be seen: not hidden (substituted, in the shadow, under the ground) unless our eyes are on, and in plain view
static qboolean BotSees( gentity_t *ent, gentity_t *e ) {
	vec3_t  a, b;
	trace_t tr;

	if ( SHINOBI_UNSEEN( &e->client->ps ) && !ent->client->shinobi.eyes )
		return qfalse;
	VectorCopy( ent->client->ps.origin, a );
	a[2] += ent->client->ps.viewheight;
	VectorCopy( e->client->ps.origin, b );
	b[2] += e->client->ps.viewheight;
	trap->Trace( &tr, a, NULL, NULL, b, ent->s.number, MASK_SOLID, qfalse, 0, 0 );
	return ( tr.fraction >= 1.0f || tr.entityNum == e->s.number ) ? qtrue : qfalse;
}

// the nearest enemy within maxDist (but skip), seen or not
static gentity_t *BotNearest( gentity_t *ent, float maxDist, gentity_t *skip, float *distOut ) {
	gentity_t *best = NULL;
	float      bestDist = maxDist;
	int        i;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		gentity_t *e = &g_entities[i];
		float      d;
		if ( e == skip || !BotIsEnemy( ent, e ) )
			continue;
		d = Distance( e->client->ps.origin, ent->client->ps.origin );
		if ( d < bestDist ) {
			bestDist = d;
			best = e;
		}
	}
	if ( distOut )
		*distOut = bestDist;
	return best;
}

// he is in front of us (flat), within cosine 'dot'
static qboolean BotAhead( gentity_t *ent, gentity_t *e, float dot ) {
	vec3_t yaw, fwd, d;
	VectorSet( yaw, 0, ent->client->ps.viewangles[YAW], 0 );
	AngleVectors( yaw, fwd, NULL, NULL );
	VectorSubtract( e->client->ps.origin, ent->client->ps.origin, d );
	d[2] = 0;
	VectorNormalize( d );
	return DotProduct( d, fwd ) >= dot ? qtrue : qfalse;
}

// he faces us (flat), within cosine 'dot'
static qboolean BotFacesUs( gentity_t *ent, gentity_t *e, float dot ) {
	return BotAhead( e, ent, dot );
}

// turn to look at him (lift: above his feet)
static void BotFace( gentity_t *ent, gentity_t *e, float lift ) {
	vec3_t eye, to, dir, ang;
	VectorCopy( ent->client->ps.origin, eye );
	eye[2] += ent->client->ps.viewheight;
	VectorCopy( e->client->ps.origin, to );
	to[2] += lift;
	VectorSubtract( to, eye, dir );
	vectoangles( dir, ang );
	ang[ROLL] = 0;
	SetClientViewAngle( ent, ang );
}

// move along a world direction (flat), whichever way the bot looks
static void BotSteer( gentity_t *ent, usercmd_t *ucmd, const vec3_t worldDir, float speed ) {
	vec3_t yaw, fwd, right, d;
	VectorSet( yaw, 0, ent->client->ps.viewangles[YAW], 0 );
	AngleVectors( yaw, fwd, right, NULL );
	VectorCopy( worldDir, d );
	d[2] = 0;
	if ( VectorNormalize( d ) < 0.001f )
		return;
	ucmd->forwardmove = (signed char)Com_Clamp( -127, 127, DotProduct( d, fwd ) * speed );
	ucmd->rightmove = (signed char)Com_Clamp( -127, 127, DotProduct( d, right ) * speed );
}

static qboolean BotFlying( gentity_t *e ) { // a projectile still on its way
	return e->s.pos.trType == TR_LINEAR || e->s.pos.trType == TR_GRAVITY;
}

/*
The soonest enemy projectile that will pass close by us within 'within' seconds: its time
to the closest point and where we are from that point (to step further that way).
*/
static const char *botMissiles[] = { "shinobi_fireball", "shinobi_waterball", "shinobi_windslash", "shinobi_blade",
	"shinobi_wave", "shinobi_kunai", "shinobi_exkunai", "shinobi_shuriken", "shinobi_star", NULL };

static gentity_t *BotIncoming( gentity_t *ent, float within, float *tOut, vec3_t awayOut ) {
	gentity_t *best = NULL;
	vec3_t     mid;
	float      bestT = within;
	int        i, k;

	VectorCopy( ent->client->ps.origin, mid );
	mid[2] += 8;
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *m = &g_entities[i];
		vec3_t     p, v, rel, miss;
		float      speed2, t;
		if ( !m->inuse || !m->classname || !BotFlying( m ) )
			continue;
		for ( k = 0; botMissiles[k] && strcmp( m->classname, botMissiles[k] ); k++ )
			;
		if ( !botMissiles[k] || m->r.ownerNum >= MAX_CLIENTS || !BotIsEnemy( ent, &g_entities[m->r.ownerNum] ) )
			continue;
		BG_EvaluateTrajectory( &m->s.pos, level.time, p );
		BG_EvaluateTrajectoryDelta( &m->s.pos, level.time, v );
		speed2 = DotProduct( v, v );
		if ( speed2 < 150.0f * 150.0f )
			continue;
		VectorSubtract( mid, p, rel );
		t = DotProduct( rel, v ) / speed2;
		if ( t <= 0 || t >= bestT )
			continue;
		VectorMA( rel, -t, v, miss );
		if ( VectorLength( miss ) > 60 )
			continue;
		best = m;
		bestT = t;
		if ( awayOut )
			VectorCopy( miss, awayOut );
	}
	if ( tOut )
		*tOut = bestT;
	return best;
}

static qboolean BotBigMissile( gentity_t *m ) { // worth a substitution when it is too late to step aside
	return !strcmp( m->classname, "shinobi_fireball" ) || !strcmp( m->classname, "shinobi_wave" )
		|| !strcmp( m->classname, "shinobi_blade" ) || !strcmp( m->classname, "shinobi_exkunai" );
}

// caught in something only Kawarimi gets you out of
static qboolean BotCaught( gclient_t *c ) {
	return ( c->shinobi.prisonCaster || c->shinobi.shadowBy || c->shinobi.shadowTrapEnt
		|| ( c->shinobi.juggleEnd > level.time && c->shinobi.juggleBy ) ) ? qtrue : qfalse;
}

static qboolean BotEscape( gentity_t *ent ) {
	gclient_t *c = ent->client;

	if ( !BotCaught( c ) ) {
		c->shinobi.botCaughtAt = c->shinobi.botEscapeAt = 0;
		return qfalse;
	}
	if ( !c->shinobi.botCaughtAt ) { // just caught: will it see the way out, and when
		c->shinobi.botCaughtAt = level.time;
		c->shinobi.botEscapeAt = BotChance( 95 ) ? level.time + BotReact( 250, 1400 ) + Q_irand( 0, 150 ) : 0;
	}
	if ( c->shinobi.botEscapeAt && level.time >= c->shinobi.botEscapeAt ) {
		c->shinobi.botEscapeAt = 0;
		if ( BotCharges( c->shinobi.kawarimiReady, SHINOBI_KAWARIMI_CHARGES ) > 0 && Shinobi_Cast( ent, SJ_KAWARIMI, 1 ) ) {
			shinobiTest.botEscapes++;
			Shinobi_Debug( "bot %i: Kawarimi out of a trap after %i ms", ent->s.number, level.time - c->shinobi.botCaughtAt );
			return qtrue;
		}
	}
	return qfalse;
}

// on fire: run a step and duck - the roll smothers the flames
static qboolean BotRollFire( gentity_t *ent, usercmd_t *ucmd ) {
	gclient_t *c = ent->client;

	if ( c->shinobi.burnEnd > level.time && c->shinobi.botRollUntil <= level.time && c->shinobi.botTacticNext <= level.time
		&& c->ps.groundEntityNum != ENTITYNUM_NONE && ( c->ps.weapon == WP_SABER || c->ps.weapon == WP_MELEE ) ) {
		c->shinobi.botTacticNext = level.time + 500;
		if ( BotChance( 80 ) ) {
			c->shinobi.botRollUntil = level.time + 750;
			shinobiTest.botRolls++;
		}
	}
	if ( c->shinobi.botRollUntil <= level.time )
		return qfalse;
	if ( c->shinobi.burnEnd <= level.time && c->shinobi.botRollUntil - level.time < 450 ) {
		c->shinobi.botRollUntil = 0; // out
		return qfalse;
	}
	ucmd->forwardmove = 127;
	ucmd->rightmove = 0;
	ucmd->upmove = ( c->shinobi.botRollUntil - level.time < 480 ) ? -127 : 0;
	ucmd->buttons &= ~( BUTTON_ATTACK | BUTTON_ALT_ATTACK | SHINOBI_BUTTON_BLOCK );
	return qtrue;
}

/*
A held jutsu: keep the keys off that would end it, let go when it pays. Returns qtrue while
it is held (nothing else this frame).
*/
static qboolean BotHeld( gentity_t *ent, usercmd_t *ucmd ) {
	gclient_t *c = ent->client;
	gentity_t *n = NULL, *victim = NULL;
	qboolean   let = qfalse;
	float      d;
	int        held = SJ_NONE, ms;

	if ( c->shinobi.prisonTarget )        held = SJ_SUIRO;
	else if ( c->shinobi.shadowEnt )      held = SJ_SHADOW;
	else if ( c->shinobi.healing )        held = SJ_HEAL;
	else if ( c->shinobi.burrowEnd )      held = SJ_BURROW;
	else if ( c->shinobi.shadowHideEnd )  held = SJ_SHADOWHIDE;
	else if ( c->shinobi.flameEnt )       held = SJ_FLAME;
	else if ( c->shinobi.kaitenStart )    held = SJ_KAITEN;
	else if ( c->shinobi.palmsStart )     held = SJ_PALMS;
	else if ( c->shinobi.raiDashEnd )     held = SJ_RAIDASH;
	if ( held == SJ_NONE ) {
		c->shinobi.botHoldStart = 0;
		return qfalse;
	}
	c->shinobi.chargeJutsu = held; // (the release goes through the key, whoever cast it)
	if ( !c->shinobi.botHoldStart ) {
		c->shinobi.botHoldStart = c->shinobi.botQuietSince = level.time;
		c->shinobi.botHoldHealth = ent->health;
	}
	ms = level.time - c->shinobi.botHoldStart;

	switch ( held ) {
	case SJ_SUIRO: // drown him while it is safe; then the free hit while he gasps
		victim = &g_entities[c->shinobi.prisonTarget - 1];
		n = BotNearest( ent, 280, victim, NULL );
		let = c->ps.fd.forcePower < 20 || n || ent->health < c->shinobi.botHoldHealth - 4 || ms > 3500 + 25 * BotSmart();
		if ( let )
			shinobiTest.botPrisonHeldMs += ms;
		break;
	case SJ_SHADOW:
		victim = c->shinobi.shadowTarget ? &g_entities[c->shinobi.shadowTarget - 1] : NULL;
		n = BotNearest( ent, 280, victim, NULL );
		let = c->ps.fd.forcePower < 15 || n || ent->health < c->shinobi.botHoldHealth - 4
			|| ( victim ? ms > 2500 + 15 * BotSmart() : ms > 1600 );
		break;
	case SJ_HEAL:
		n = BotNearest( ent, 350, NULL, NULL );
		let = ent->health >= c->ps.stats[STAT_MAX_HEALTH] || n || c->ps.fd.forcePower < 6;
		break;
	case SJ_BURROW: // under the ground straight to him, out right under him
		n = BotNearest( ent, 1500, NULL, NULL );
		if ( n ) {
			vec3_t dir;
			VectorSubtract( n->client->ps.origin, c->ps.origin, dir );
			dir[2] = 0;
			BotSteer( ent, ucmd, dir, 127 );
			let = VectorLength( dir ) < 40;
		}
		else {
			let = ms > 1500;
		}
		break;
	case SJ_SHADOWHIDE: // wait in the shadow until he walks by
		n = BotNearest( ent, 110, NULL, NULL );
		let = n != NULL;
		if ( let )
			c->shinobi.botPunishUntil = level.time + 500;
		break;
	case SJ_FLAME:
		n = BotNearest( ent, SHINOBI_FLAME_RANGE + 40, NULL, &d );
		if ( n && BotAhead( ent, n, 0.75f ) )
			c->shinobi.botQuietSince = level.time;
		let = c->ps.fd.forcePower < 5 || ( ms > 500 && level.time - c->shinobi.botQuietSince > 400 );
		break;
	case SJ_KAITEN:
		n = BotNearest( ent, SHINOBI_KAITEN_RADIUS + 80, NULL, NULL );
		if ( n || BotIncoming( ent, 0.6f, NULL, NULL ) )
			c->shinobi.botQuietSince = level.time;
		let = c->ps.fd.forcePower < 5 || ( ms > SHINOBI_KAITEN_WINDUP_MS + 400 && level.time - c->shinobi.botQuietSince > 350 );
		break;
	case SJ_PALMS: // the field fills, strikes, then guards while someone is in it
		n = BotNearest( ent, SHINOBI_PALMS_RADIUS, NULL, NULL );
		if ( n )
			c->shinobi.botQuietSince = level.time;
		let = c->ps.fd.forcePower < 4 || level.time - c->shinobi.botQuietSince > ( c->shinobi.palmsFull ? 600 : 1000 );
		break;
	case SJ_RAIDASH:
		n = BotNearest( ent, 90, NULL, NULL );
		let = n != NULL || ms > 900;
		break;
	}

	if ( let ) {
		Shinobi_Debug( "bot %i: lets go of %s after %i ms", ent->s.number, shinobiJutsuTitles[held], ms );
		Cmd_JutsuRelease_f( ent );
		c->shinobi.botHoldStart = 0;
		if ( victim && BotIsEnemy( ent, victim ) ) { // gasping / just let go: go in and hit him now
			Shinobi_BotFocus( ent->s.number, victim );
			c->shinobi.botPunishUntil = level.time + SHINOBI_PRISON_STUN_MS + 300;
			c->shinobi.botPunishTarget = victim->s.number + 1;
		}
		return qfalse;
	}
	c->shinobi.botHoldEnd = level.time + 200; // (Shinobi_BotThink must not let go either)
	ucmd->buttons &= ~( BUTTON_ATTACK | BUTTON_ALT_ATTACK | BUTTON_USE | SHINOBI_BUTTON_BLOCK );
	if ( ucmd->upmove > 0 )
		ucmd->upmove = 0; // a jump ends a Water Prison
	return qtrue;
}

/*
Something about to hit: a projectile (step aside and jump, Kawarimi if it is a big one and
too late), a Rasengan / Chidori rushing in (Kawarimi, or aside). The sidestep goes on for a
moment (botDodgeUntil).
*/
static qboolean BotDodge( gentity_t *ent, usercmd_t *ucmd ) {
	gclient_t *c = ent->client;
	gentity_t *m;
	vec3_t     away, yaw, fwd, right;
	float      t;
	int        i;

	if ( c->shinobi.botDodgeUntil > level.time ) {
		ucmd->rightmove = c->shinobi.botDodgeRight > 0 ? 127 : -127;
		ucmd->forwardmove = 0;
		if ( c->shinobi.botDodgeUntil - level.time > 260 && c->shinobi.botDodgeRight & 2 )
			ucmd->upmove = 127;
		return qfalse;
	}
	if ( c->shinobi.botDodgeNext > level.time )
		return qfalse;

	// a hand jutsu rushing at us
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		gentity_t *e = &g_entities[i];
		if ( !BotIsEnemy( ent, e ) || !SHINOBI_HAND_DASH( &e->client->ps ) )
			continue;
		if ( Distance( e->client->ps.origin, c->ps.origin ) > 240 || !BotFacesUs( ent, e, 0.8f ) )
			continue;
		c->shinobi.botDodgeNext = level.time + 600;
		if ( !BotChance( 70 ) )
			return qfalse;
		if ( BotCharges( c->shinobi.kawarimiReady, SHINOBI_KAWARIMI_CHARGES ) > 0 && Shinobi_Cast( ent, SJ_KAWARIMI, 1 ) ) {
			shinobiTest.botDodges++;
			Shinobi_Debug( "bot %i: Kawarimi from %i's %s", ent->s.number, e->s.number,
				SHINOBI_HAND_IS_CHIDORI( &e->client->ps ) ? "Chidori" : "Rasengan" );
			return qtrue;
		}
		c->shinobi.botDodgeRight = Q_irand( 0, 1 ) ? 1 : -1;
		c->shinobi.botDodgeUntil = level.time + 400;
		shinobiTest.botDodges++;
		return qfalse;
	}

	m = BotIncoming( ent, 0.6f, &t, away );
	if ( !m )
		return qfalse;
	c->shinobi.botDodgeNext = level.time + 450;
	if ( t * 1000 < BotReact( 60, 300 ) || !BotChance( BotBigMissile( m ) ? 90 : 50 ) )
		return qfalse; // didn't see it in time
	if ( t < 0.3f && BotBigMissile( m ) && BotCharges( c->shinobi.kawarimiReady, SHINOBI_KAWARIMI_CHARGES ) >= 2
		&& Shinobi_Cast( ent, SJ_KAWARIMI, 1 ) ) { // (one charge kept for traps)
		shinobiTest.botDodges++;
		Shinobi_Debug( "bot %i: Kawarimi from a %s", ent->s.number, m->classname );
		return qtrue;
	}
	// aside, further the way we already are from its line (or either way when it comes dead on)
	VectorSet( yaw, 0, c->ps.viewangles[YAW], 0 );
	AngleVectors( yaw, fwd, right, NULL );
	away[2] = 0;
	if ( VectorLength( away ) < 6 )
		c->shinobi.botDodgeRight = Q_irand( 0, 1 ) ? 1 : -1;
	else
		c->shinobi.botDodgeRight = DotProduct( away, right ) >= 0 ? 1 : -1;
	if ( BotBigMissile( m ) )
		c->shinobi.botDodgeRight |= 2; // and a jump
	c->shinobi.botDodgeUntil = level.time + ( BotBigMissile( m ) ? 750 : 420 ); // (a big ball: far aside, its blast is wide)
	shinobiTest.botDodges++;
	Shinobi_Debug( "bot %i: sidestep from a %s (%.2f s off)", ent->s.number, m->classname, t );
	return qfalse;
}

// an enemy spinning a Kaiten or standing in his 64 Seals field: keep off, don't feed it blows
static qboolean BotKeepOff( gentity_t *ent, usercmd_t *ucmd ) {
	gclient_t *c = ent->client;
	gentity_t *e;
	vec3_t     dir;
	int        i;

	if ( c->shinobi.botAwayUntil <= level.time && c->shinobi.botTacticNext <= level.time ) {
		for ( i = 0; i < MAX_CLIENTS; i++ ) {
			float d;
			e = &g_entities[i];
			if ( !BotIsEnemy( ent, e ) )
				continue;
			d = Distance( e->client->ps.origin, c->ps.origin );
			if ( ( Shinobi_KaitenSpinning( e ) && d < SHINOBI_KAITEN_RADIUS + 70 )
				|| ( e->client->shinobi.palmsStart && d < SHINOBI_PALMS_RADIUS + 20
					&& ( e->client->shinobi.palmsFull || level.time - e->client->shinobi.palmsStart > 800 ) ) ) {
				c->shinobi.botTacticNext = level.time + 300;
				if ( BotChance( 90 ) ) {
					c->shinobi.botAwayUntil = level.time + 500;
					c->shinobi.botAwayFrom = e->s.number + 1;
					shinobiTest.botKeptOff++;
				}
				break;
			}
		}
	}
	if ( c->shinobi.botAwayUntil <= level.time || !c->shinobi.botAwayFrom )
		return qfalse;
	e = &g_entities[c->shinobi.botAwayFrom - 1];
	if ( !BotIsEnemy( ent, e ) ) {
		c->shinobi.botAwayUntil = 0;
		return qfalse;
	}
	VectorSubtract( c->ps.origin, e->client->ps.origin, dir );
	BotSteer( ent, ucmd, dir, 127 );
	ucmd->buttons &= ~( BUTTON_ATTACK | BUTTON_ALT_ATTACK );
	return qtrue;
}

// a teammate in a Water Prison / Shadow Bind: go for the one holding him
static void BotTeamHelp( gentity_t *ent ) {
	gclient_t *c = ent->client;
	int        i;

	if ( level.gametype < GT_TEAM || c->shinobi.botTacticNext > level.time )
		return;
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		gentity_t *mate = &g_entities[i], *holder = NULL;
		if ( mate == ent || !mate->inuse || !mate->client || mate->health <= 0 || !OnSameTeam( ent, mate ) )
			continue;
		if ( mate->client->shinobi.prisonCaster )
			holder = &g_entities[mate->client->shinobi.prisonCaster - 1];
		else if ( mate->client->shinobi.shadowBy )
			holder = &g_entities[mate->client->shinobi.shadowBy - 1];
		if ( holder && BotIsEnemy( ent, holder ) && Distance( holder->client->ps.origin, c->ps.origin ) < 1200 ) {
			c->shinobi.botTacticNext = level.time + 500;
			if ( BotChance( 85 ) )
				Shinobi_BotFocus( ent->s.number, holder );
			return;
		}
	}
}

/*
Someone forming seals (a charged jutsu on its way): break them - any direct hit does. Close:
run in and hit (Dynamic Entry when it reaches); far: a kunai. Each charge is looked at once.
*/
static void BotInterrupt( gentity_t *ent, usercmd_t *ucmd ) {
	gclient_t *c = ent->client;
	gentity_t *best = NULL;
	float      bestDist = 900, d;
	int        i;

	if ( c->shinobi.botRushUntil > level.time ) { // going for him
		gentity_t *t = &g_entities[c->shinobi.botRushTarget - 1];
		int        j;
		if ( !BotIsEnemy( ent, t ) || ( j = t->client->shinobi.chargeJutsu ) == SJ_NONE || SHINOBI_JUTSU_HOLD( j ) ) {
			c->shinobi.botRushUntil = 0; // broken (by us or anyone) or cast
			return;
		}
		Shinobi_BotFocus( ent->s.number, t );
		{
			vec3_t dir;
			VectorSubtract( t->client->ps.origin, c->ps.origin, dir );
			BotSteer( ent, ucmd, dir, 127 );
			d = VectorLength( dir );
		}
		if ( d < SHINOBI_PUNCH_RANGE + 40 ) {
			BotFace( ent, t, 10 );
			ucmd->buttons |= BUTTON_ATTACK;
			ucmd->buttons &= ~SHINOBI_BUTTON_BLOCK;
		}
		return;
	}
	if ( c->shinobi.botTacticNext > level.time || c->shinobi.chargeJutsu != SJ_NONE )
		return;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		gentity_t *e = &g_entities[i];
		int        j;
		if ( !BotIsEnemy( ent, e ) )
			continue;
		j = e->client->shinobi.chargeJutsu;
		if ( j == SJ_NONE || SHINOBI_JUTSU_HOLD( j ) || SHINOBI_JUTSU_INSTANT( j ) || j == SJ_SHUNSHIN
			|| c->shinobi.botRushSeen == e->client->shinobi.chargeStart
			|| level.time - e->client->shinobi.chargeStart < BotReact( 120, 700 ) )
			continue;
		d = Distance( e->client->ps.origin, c->ps.origin );
		if ( d < bestDist && BotSees( ent, e ) ) {
			bestDist = d;
			best = e;
		}
	}
	if ( !best )
		return;
	c->shinobi.botRushSeen = best->client->shinobi.chargeStart;
	c->shinobi.botTacticNext = level.time + 300;
	if ( !BotChance( 85 ) )
		return;

	if ( bestDist > 160 && bestDist < 430 && Shinobi_HasJutsu( ent, SJ_ENTRY ) && c->shinobi.entryNext <= level.time
		&& fabs( best->client->ps.origin[2] - c->ps.origin[2] ) < 64 ) {
		BotFace( ent, best, 0 );
		if ( Shinobi_Cast( ent, SJ_ENTRY, 1 ) ) {
			shinobiTest.botInterrupts++;
			Shinobi_Debug( "bot %i: Dynamic Entry into %i's seals", ent->s.number, best->s.number );
			c->shinobi.botJutsuNext = level.time + 800;
			return;
		}
	}
	if ( bestDist < 520 ) {
		c->shinobi.botRushTarget = best->s.number + 1;
		c->shinobi.botRushUntil = level.time + 1800;
		shinobiTest.botInterrupts++;
		Shinobi_Debug( "bot %i: runs at %i forming seals (%.0f away)", ent->s.number, best->s.number, bestDist );
		return;
	}
	if ( c->shinobi.kunaiNext <= level.time && !c->shinobi.blocking ) { // a kunai drops on its way: aim over him
		const float flight = bestDist / SHINOBI_KUNAI_SPEED;
		BotFace( ent, best, best->client->ps.viewheight * 0.6f + 0.5f * g_gravity.value * flight * flight );
		Cmd_Kunai_f( ent );
		if ( c->shinobi.kunaiNext > level.time ) {
			shinobiTest.botKunaiInterrupts++;
			Shinobi_Debug( "bot %i: kunai at %i forming seals (%.0f away)", ent->s.number, best->s.number, bestDist );
		}
	}
}

// openings: the techniques the force-power mapping never reaches for, cast where they work
static void BotOpenings( gentity_t *ent ) {
	gclient_t *c = ent->client;
	gentity_t *e;
	float      d, dz;

	if ( c->shinobi.botTacticNext > level.time || c->shinobi.botJutsuNext > level.time || c->shinobi.chargeJutsu != SJ_NONE
		|| c->shinobi.kawarimiEnd || SHINOBI_HAND_ANY( &c->ps ) || c->shinobi.botPunishUntil > level.time )
		return; // (punishing: blows, not a new jutsu)
	c->shinobi.botTacticNext = level.time + 450;

	// hurt and alone: heal
	if ( Shinobi_HasJutsu( ent, SJ_HEAL ) && ent->health * 100 < c->ps.stats[STAT_MAX_HEALTH] * 45
		&& c->ps.fd.forcePower >= 30 && !BotNearest( ent, 550, NULL, NULL ) && BotChance( 80 ) ) {
		if ( Shinobi_Cast( ent, SJ_HEAL, 1 ) ) {
			c->shinobi.chargeJutsu = SJ_HEAL;
			c->shinobi.botJutsuNext = level.time + 1500;
			return;
		}
	}

	e = Shinobi_BotEnemyOf( ent->s.number );
	if ( !BotIsEnemy( ent, e ) )
		e = BotNearest( ent, 600, NULL, NULL );
	if ( !e || !BotSees( ent, e ) || Shinobi_KaitenSpinning( e ) || e->client->shinobi.palmsStart )
		return;
	d = Distance( e->client->ps.origin, c->ps.origin );
	dz = fabs( e->client->ps.origin[2] - c->ps.origin[2] );

	if ( Shinobi_HasJutsu( ent, SJ_LOTUS ) && d < SHINOBI_LOTUS_BLINK + SHINOBI_LOTUS_REACH - 10 && dz < 48
		&& c->ps.fd.forcePower >= Shinobi_JutsuCost( SJ_LOTUS, 1 ) && BotChance( e->client->shinobi.blocking ? 75 : 45 ) ) { // (a grab: best on a guard)
		BotFace( ent, e, 0 );
		if ( Shinobi_Cast( ent, SJ_LOTUS, 1 ) ) {
			c->shinobi.botJutsuNext = level.time + 2500;
			Shinobi_Debug( "bot %i: Reverse Lotus on %i", ent->s.number, e->s.number );
			return;
		}
	}
	if ( Shinobi_HasJutsu( ent, SJ_ENTRY ) && c->shinobi.entryNext <= level.time && d > 150 && d < 430 && dz < 64
		&& BotChance( 30 ) ) {
		BotFace( ent, e, 0 );
		if ( Shinobi_Cast( ent, SJ_ENTRY, 1 ) ) {
			c->shinobi.botJutsuNext = level.time + 1200;
			Shinobi_Debug( "bot %i: Dynamic Entry at %i", ent->s.number, e->s.number );
			return;
		}
	}
	if ( Shinobi_HasJutsu( ent, SJ_PALMS ) && d < 170 && c->ps.fd.forcePower >= 45 && BotChance( 20 ) ) {
		if ( Shinobi_Cast( ent, SJ_PALMS, 1 ) ) {
			c->shinobi.chargeJutsu = SJ_PALMS;
			c->shinobi.botJutsuNext = level.time + 2500;
			Shinobi_Debug( "bot %i: 64 Seals with %i close", ent->s.number, e->s.number );
		}
	}
}

// qtrue: it took this frame over (no guard / punishing / fists after it)
static qboolean Shinobi_BotTactics( gentity_t *ent, usercmd_t *ucmd ) {
	gclient_t *c = ent->client;

	if ( BotSmart() <= 0 )
		return qfalse;
	if ( BotEscape( ent ) )
		return qtrue;
	if ( BotCaught( c ) )
		return qfalse;
	if ( BotHeld( ent, ucmd ) )
		return qtrue;
	if ( !Shinobi_CanAct( ent ) || c->shinobi.lotusPhase || c->shinobi.lotusBy || c->shinobi.entryEnd )
		return qfalse;
	if ( BotRollFire( ent, ucmd ) )
		return qtrue;
	if ( BotDodge( ent, ucmd ) )
		return qtrue;
	if ( BotKeepOff( ent, ucmd ) )
		return qtrue;
	if ( c->shinobi.botAirUntil > level.time )
		return qfalse; // our own air combo goes on (Shinobi_BotFists)
	BotTeamHelp( ent );
	BotInterrupt( ent, ucmd );
	BotOpenings( ent );
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
	if ( Shinobi_BotTactics( ent, ucmd ) )
		return; // escaped, holding a jutsu, rolling, keeping off...

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
		if ( e != ent && e->inuse && e->client && e->health > 0
			&& ( ( e->client->ps.stats[STAT_SHINOBI] & SHINOBI_STAT_STAGGER ) || e->client->shinobi.stunEnd > level.time )
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
	if ( client->shinobi.botPunishUntil > level.time ) {
		ucmd->buttons |= BUTTON_ATTACK;
		if ( client->shinobi.botPunishTarget ) { // a set victim (gasping after our prison): in to him, facing him
			gentity_t *v = &g_entities[client->shinobi.botPunishTarget - 1];
			if ( BotIsEnemy( ent, v ) ) {
				vec3_t dir;
				VectorSubtract( v->client->ps.origin, client->ps.origin, dir );
				if ( VectorLength( dir ) > 36 )
					BotSteer( ent, ucmd, dir, 127 );
				BotFace( ent, v, 20 );
			}
		}
	}
	else {
		client->shinobi.botPunishTarget = 0;
	}

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
	int        i;
	qboolean   ready;

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
		if ( client->ps.groundEntityNum != ENTITYNUM_NONE && level.time > client->shinobi.botAirUntil - 1700
			&& level.time < client->shinobi.botAirUntil - 1200 )
			ucmd->upmove = 127; // the jump right after the launcher (a Shunshin up to him)
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
	ready = Shinobi_BladeFinisherReady( &client->ps ); // (fists count as the blade: blows landed or blocked)
	if ( !ready )
		client->shinobi.botFinisher = -1;
	if ( ready ) { // the next blow is the finisher: its direction
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

/*
Combat 2 (bg_shinobi.h), a bot with a blade: the stock AI's clicks are the light chain; with 3
steps landed it goes heavy half the time (the heavy finisher), with 4 most of the time (else the
light push) - the direction picked per chain as with fists.
*/
void Shinobi_BotC2( gentity_t *ent, usercmd_t *ucmd ) {
	gclient_t *client = ent->client;
	const int  have = client->ps.stats[STAT_SHINOBI_BLADECOMBO] & 7;

	if ( !bgShinobiCombat2 || !( ent->r.svFlags & SVF_BOT ) || client->ps.weapon != WP_SABER || Shinobi_AutotestControls( ent ) )
		return;
	if ( have < SHINOBI_C2_HEAVY_FROM ) {
		client->shinobi.botC2Step = 0;
		client->shinobi.botFinisher = -1;
		return;
	}
	if ( !( ucmd->buttons & BUTTON_ATTACK ) || ( ucmd->buttons & SHINOBI_BUTTON_BLOCK ) )
		return;
	if ( client->shinobi.botC2Step != have + 1 ) {
		client->shinobi.botC2Step = have + 1;
		client->shinobi.botC2Heavy = Q_irand( 0, 99 ) < ( have >= SHINOBI_C2_CHAIN - 1 ? 70 : 50 );
	}
	if ( !client->shinobi.botC2Heavy )
		return;
	if ( client->shinobi.botFinisher < 0 ) {
		const int r = Q_irand( 0, 99 );
		client->shinobi.botFinisher = r < 30 ? 0 : r < 60 ? 1 : r < 80 ? 2 : 3;
	}
	ucmd->buttons |= SHINOBI_BUTTON_HEAVY;
	ucmd->forwardmove = ucmd->rightmove = 0;
	switch ( client->shinobi.botFinisher ) {
	case 1: ucmd->forwardmove = 127; break;  // shoved down
	case 2: ucmd->forwardmove = -127; break; // the sweep
	case 3: ucmd->rightmove = Q_irand( 0, 1 ) ? 127 : -127; break; // the spin
	default: break;                          // up he goes
	}
}

// every frame for bots: let go of held jutsu in time
void Shinobi_BotThink( gentity_t *ent ) {
	gclient_t *client = ent->client;

	if ( !( ent->r.svFlags & SVF_BOT ) || Shinobi_AutotestControls( ent ) )
		return;
	if ( client->shinobi.chargeJutsu != SJ_NONE && level.time >= client->shinobi.botHoldEnd )
		Cmd_JutsuRelease_f( ent ); // a held jutsu let go / a charged one cast at its bar
}
