/*
===========================================================================
JK_Shinobi - automatic tests in a running game ("shinobi_autotest", cheats).

Two bots join (TestA, TestB). Their AI is switched off while a test runs:
the test writes their usercmds every frame (moves, buttons, weapon, where
they look) and calls the real game code (jutsu, kunai, tools), so the
mechanics run exactly as in a fight. Each test then reads the state it
expects (counters in shinobiTest, the damage log, client fields) and prints

  [autotest] PASS <name>: <what it measured>
  [autotest] FAIL <name>: <what went wrong>

to the console and to shinobi_autotest.txt in the mod folder, with a summary
at the end. "shinobi_autotest" runs them all, "shinobi_autotest <name> ..."
only those, "shinobi_autotest list" lists them, "shinobi_autotest stop" ends.
The human player is best a spectator meanwhile (assets/shinobi_autotest.cfg).
===========================================================================
*/
#include "g_shinobi_local.h"

shinobiTest_t shinobiTest;
gentity_t    *shinobiTestInflictor;

extern void G_Knockdown( gentity_t *victim );
extern stringID_table_t animTable[MAX_ANIMATIONS + 1];
void Shinobi_BotRandomBuildPublic( shinobiBuild_t *b );
qboolean G_SetSaber( gentity_t *ent, int saberNum, char *saberName, qboolean siegeOverride );
qboolean G_SaberModelSetup( gentity_t *ent );
extern qboolean BG_SaberInAttack( int move );
extern qboolean BG_InKnockDown( int anim );

#define AT_MAX_TESTS   48
#define AT_BOT_A       "TestA"
#define AT_BOT_B       "TestB"

typedef struct {
	qboolean on;
	int      forward, right, up, buttons, weapon;
	vec3_t   angles;
	qboolean aimAt;      // look at the other bot every frame
	qboolean free;       // the real AI drives (and defends) this one
} atDrive_t;

typedef enum { ATR_RUN, ATR_PASS, ATR_FAIL } atResult_t;

typedef struct {
	const char *name;
	const char *what;
	void       ( *setup )( void );
	atResult_t ( *step )( int t ); // t = ms since the test began
	int         timeout;
} atTest_t;

static struct {
	int      phase;              // 0 off, 1 waiting for the bots, 2 running, 3 done
	int      queue[AT_MAX_TESTS], queueLen, cur;
	int      start, phaseStart, stage, stageTime;
	int      a, b;               // client numbers
	atDrive_t drive[2];
	vec3_t   base, fwd, right;
	float    baseYaw;
	qboolean haveBase;
	int      pass, fail;
	char     msg[512];
	int      v[16];              // scratch for the tests
	float    f[8];
	vec3_t   vec[2];
	fileHandle_t file;
} at;

extern char *modNames[MOD_MAX];

#define A ( &g_entities[at.a] )
#define B ( &g_entities[at.b] )

static void AT_Print( const char *fmt, ... ) {
	va_list argptr;
	char    text[1024];

	va_start( argptr, fmt );
	Q_vsnprintf( text, sizeof( text ), fmt, argptr );
	va_end( argptr );
	Com_Printf( "%s", text );
	G_LogPrintf( "%s", text );
	if ( at.file )
		trap->FS_Write( text, strlen( text ), at.file );
}

static void AT_Msg( const char *fmt, ... ) {
	va_list argptr;
	va_start( argptr, fmt );
	Q_vsnprintf( at.msg, sizeof( at.msg ), fmt, argptr );
	va_end( argptr );
}

// ---------------------------------------------------------------------------
// control of the two bots
// ---------------------------------------------------------------------------
/*
Punching bags (shinobi_dummy 1, JK_Shinobi_Dummy.bat): bots named "Dummy..." do nothing - no
AI, no jutsu - stand facing you with bare hands and after each death come back on their feet
right in front of you, side by side: "Dummy" takes everything (no guard), "DummyGuard" holds
its guard up all the time (it breaks and comes back as anyone's does).
*/
static qboolean Shinobi_IsDummy( gentity_t *ent ) {
	return shinobi_dummy.integer && ent && ent->client && ( ent->r.svFlags & SVF_BOT )
		&& !Q_stricmpn( ent->client->pers.netname, "Dummy", 5 );
}

// the first human in the game (the one the bag stands in front of)
static gentity_t *Shinobi_DummyPlayer( void ) {
	int i;
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		gentity_t *e = &g_entities[i];
		if ( e->inuse && e->client && !( e->r.svFlags & SVF_BOT ) && e->client->pers.connected == CON_CONNECTED
			&& e->client->sess.sessionTeam != TEAM_SPECTATOR && e->health > 0 )
			return e;
	}
	return NULL;
}

static void Shinobi_DummyDrive( gentity_t *ent, usercmd_t *ucmd ) {
	gclient_t *c = ent->client;
	const qboolean guard = !Q_stricmpn( c->pers.netname, "DummyGuard", 10 );
	gentity_t *p = Shinobi_DummyPlayer();
	vec3_t     ang = { 0, 0, 0 };
	int        i;

	ucmd->forwardmove = ucmd->rightmove = ucmd->upmove = 0;
	ucmd->buttons = 0;
	ucmd->generic_cmd = 0;
	ucmd->forcesel = ucmd->invensel = 0;
	ucmd->weapon = WP_MELEE;
	if ( ent->health <= 0 ) { // back up at once
		if ( level.time > c->respawnTime )
			ucmd->buttons = BUTTON_ATTACK;
		return;
	}
	if ( p && !c->shinobi.dummyPlaced ) { // just spawned: in front of him (the guarding one to his right), facing him
		vec3_t fwd, right, spot, yaw;
		trace_t tr;
		VectorSet( yaw, 0, p->client->ps.viewangles[YAW], 0 );
		AngleVectors( yaw, fwd, right, NULL );
		VectorMA( p->client->ps.origin, 110, fwd, spot );
		VectorMA( spot, guard ? 45 : -45, right, spot );
		trap->Trace( &tr, p->client->ps.origin, ent->r.mins, ent->r.maxs, spot, p->s.number, MASK_PLAYERSOLID, qfalse, 0, 0 );
		if ( !tr.startsolid && !tr.allsolid ) {
			Shinobi_TeleportClient( ent, tr.endpos );
			VectorClear( c->ps.velocity );
		}
		c->shinobi.dummyPlaced = qtrue;
		c->ps.eFlags &= ~EF_INVULNERABLE; // (no spawn shield)
		c->invulnerableTimer = 0;
	}
	if ( p ) { // it looks at you
		vec3_t d;
		VectorSubtract( p->client->ps.origin, c->ps.origin, d );
		vectoangles( d, ang );
		ang[PITCH] = 0;
	}
	for ( i = 0; i < 3; i++ )
		ucmd->angles[i] = ANGLE2SHORT( ang[i] ) - c->ps.delta_angles[i];
	if ( guard )
		ucmd->buttons |= SHINOBI_BUTTON_BLOCK; // guard up, always
	else
		c->ps.stats[STAT_ARMOR] = 0; // no guard to soak the blows
}

qboolean Shinobi_AutotestControls( gentity_t *ent ) {
	if ( Shinobi_IsDummy( ent ) )
		return qtrue; // (the bag: no bot AI of ours either)
	if ( at.phase != 2 || !ent || ent->s.number >= MAX_CLIENTS )
		return qfalse;
	if ( ent->s.number == at.a )
		return at.drive[0].on && !at.drive[0].free;
	if ( ent->s.number == at.b )
		return at.drive[1].on && !at.drive[1].free;
	return qfalse;
}

// from Shinobi_ClientThink, first thing: the test's usercmd instead of the AI's
void Shinobi_AutotestDrive( gentity_t *ent, usercmd_t *ucmd ) {
	atDrive_t *d;
	vec3_t     ang;
	int        i;

	if ( Shinobi_IsDummy( ent ) ) {
		Shinobi_DummyDrive( ent, ucmd );
		return;
	}
	if ( !Shinobi_AutotestControls( ent ) )
		return;
	d = &at.drive[ent->s.number == at.a ? 0 : 1];
	VectorCopy( d->angles, ang );
	if ( d->aimAt ) {
		gentity_t *o = ent->s.number == at.a ? B : A;
		vec3_t     dir, eye, mid;
		VectorCopy( ent->client->ps.origin, eye );
		eye[2] += ent->client->ps.viewheight;
		VectorCopy( o->client->ps.origin, mid );
		mid[2] += 10;
		VectorSubtract( mid, eye, dir );
		vectoangles( dir, ang );
	}
	ucmd->forwardmove = d->forward;
	ucmd->rightmove = d->right;
	ucmd->upmove = d->up;
	ucmd->buttons = d->buttons;
	ucmd->weapon = d->weapon;
	ucmd->generic_cmd = 0;
	ucmd->forcesel = ucmd->invensel = 0;
	for ( i = 0; i < 3; i++ )
		ucmd->angles[i] = ANGLE2SHORT( ang[i] ) - ent->client->ps.delta_angles[i];
}

static void AT_Drive( int who, int forward, int right, int up, int buttons ) {
	at.drive[who].forward = forward;
	at.drive[who].right = right;
	at.drive[who].up = up;
	at.drive[who].buttons = buttons;
}

// put a bot somewhere, facing yaw, standing still
static void AT_Place( gentity_t *e, const vec3_t org, float yaw ) {
	vec3_t ang = { 0, 0, 0 };
	ang[YAW] = yaw;
	Shinobi_TeleportClient( e, (float *)org );
	VectorClear( e->client->ps.velocity );
	SetClientViewAngle( e, ang );
	if ( e->s.number == at.a || e->s.number == at.b ) // (the drive sends its own angles every frame: face that way too)
		at.drive[e->s.number == at.a ? 0 : 1].angles[YAW] = yaw;
}

// a point along the test line: 'ahead' units forward from the base, 'side' to the right
static void AT_Spot( float ahead, float side, vec3_t out ) {
	VectorMA( at.base, ahead, at.fwd, out );
	VectorMA( out, side, at.right, out );
}

static void AT_Weapon( int who, int weapon ) {
	gentity_t *e = who ? B : A;
	e->client->ps.stats[STAT_WEAPONS] |= 1 << weapon;
	at.drive[who].weapon = weapon;
	e->client->ps.weapon = weapon;
	if ( weapon == WP_SABER )
		e->client->ps.saberHolstered = 0;
}

static void AT_SaberStyle( gentity_t *e, int style ) {
	e->client->ps.fd.saberAnimLevel = e->client->ps.fd.saberAnimLevelBase = e->client->ps.fd.saberDrawAnimLevel = style;
}

static void AT_FreeTheirStuff( void ) {
	int i;
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *e = &g_entities[i];
		if ( !e->inuse || !e->classname )
			continue;
		if ( ( e->r.ownerNum == at.a || e->r.ownerNum == at.b )
			&& ( !strcmp( e->classname, "shinobi_kunai" ) || !strcmp( e->classname, "shinobi_rainet" )
				|| !strcmp( e->classname, "shinobi_shuriken" ) || !strcmp( e->classname, "shinobi_windwall" ) ) )
			G_FreeEntity( e );
	}
}

// a clean start for one bot: full health, chakra and guard, no statuses, charges back
static void AT_Reset( gentity_t *e ) {
	gclient_t *c = e->client;
	int        i;

	Shinobi_StatusClear( e );
	Shinobi_Reveal( e );
	Shinobi_KaitenStop( e );
	c->shinobi.chargeJutsu = SJ_NONE;
	c->shinobi.flinchEnd = c->shinobi.staggerEnd = c->shinobi.stunEnd = c->shinobi.ccImmuneEnd = 0;
	c->shinobi.juggleStart = c->shinobi.juggleEnd = c->shinobi.juggleBy = c->shinobi.slamNoFall = 0;
	c->shinobi.kunaiNext = c->shinobi.toolNext = 0;
	c->shinobi.guardSwingBy = c->shinobi.guardSwingUntil = 0;
	c->shinobi.blockPress = c->shinobi.blockPrevPress = 0;
	c->shinobi.raiNetNext = 0;
	c->shinobi.healing = 0;
	for ( i = 0; i < SHINOBI_KAWARIMI_CHARGES; i++ )
		c->shinobi.kawarimiReady[i] = 0;
	for ( i = 0; i < SHINOBI_SHUNSHIN_CHARGES; i++ )
		c->shinobi.shunshinReady[i] = 0;
	c->ps.forceHandExtend = HANDEXTEND_NONE;
	c->ps.forceHandExtendTime = 0;
	c->ps.weaponTime = 0;
	c->ps.electrifyTime = 0;
	c->ps.stats[STAT_SHINOBI2] &= ~( SHINOBI_STAT2_FLINCH | SHINOBI_STAT2_KAITEN | SHINOBI_STAT2_ROOTED );
	c->ps.stats[STAT_SHINOBI_COMBO] = c->ps.stats[STAT_SHINOBI_COMBOT] = 0;
	c->ps.stats[STAT_ARMOR] = SHINOBI_GUARD_MAX;
	e->health = c->ps.stats[STAT_HEALTH] = 500; // (well over the max: a test's real blows must not kill)
	c->ps.fd.forcePower = c->ps.fd.forcePowerMax;
	c->pers.shinobiFistStyle = SFS_FAST;
	c->pers.shinobiBuild.perk = SP_NONE; // no eyes, seal or gates in the way
	c->shinobi.eyes = 0;
	c->shinobi.copyJutsu = 0;
	c->shinobi.infuseUntil = 0; // no Wind / Lightning Edge left over
	c->ps.eFlags &= ~EF_INVULNERABLE; // no spawn shield: the first blow must count
	c->invulnerableTimer = 0;
	c->shinobi.mistEnd = 0;
	c->shinobi.curseAwake = qfalse;
	c->shinobi.gate = c->shinobi.gateDealt = c->shinobi.gateNext = 0;
	c->shinobi.weightsOff = qfalse;
	c->shinobi.entryNext = c->shinobi.palmsHitNext = 0;
	if ( c->shinobi.palmsStart || c->shinobi.lotusPhase || c->shinobi.lotusBy )
		Shinobi_TaiClear( e );
	VectorClear( c->ps.velocity );
}

// ---------------------------------------------------------------------------
// helpers the tests read
// ---------------------------------------------------------------------------
static int AT_DamageTo( gentity_t *targ, int since, const char *inflictorClass, int mod, int *hits, int *last ) {
	int i, total = 0, n = 0;
	for ( i = 0; i < SHINOBI_TEST_DMGLOG; i++ ) {
		const shinobiTestDamage_t *d = &shinobiTest.dmg[i];
		if ( !d->time || d->time < since || d->targ != targ->s.number )
			continue;
		if ( mod >= 0 && d->mod != mod )
			continue;
		if ( inflictorClass && Q_stricmp( d->inflictor, inflictorClass ) )
			continue;
		total += d->take;
		n++;
		if ( last )
			*last = d->take;
	}
	if ( hits )
		*hits = n;
	return total;
}

static int AT_CountClass( const char *classname, int owner ) {
	int i, n = 0;
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *e = &g_entities[i];
		if ( e->inuse && e->classname && !strcmp( e->classname, classname ) && ( owner < 0 || e->r.ownerNum == owner ) )
			n++;
	}
	return n;
}

static gentity_t *AT_FindClass( const char *classname, int owner ) {
	int i;
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *e = &g_entities[i];
		if ( e->inuse && e->classname && !strcmp( e->classname, classname ) && ( owner < 0 || e->r.ownerNum == owner ) )
			return e;
	}
	return NULL;
}

static const char *AT_AnimName( int anim ) {
	int i;
	for ( i = 0; animTable[i].name; i++ )
		if ( animTable[i].id == anim )
			return animTable[i].name;
	return "?";
}

#define STAGE( n )     ( at.stage == (n) )
#define NEXT()         ( at.stage++, at.stageTime = level.time )
#define SINCE()        ( level.time - at.stageTime )

// ---------------------------------------------------------------------------
// the tests
// ---------------------------------------------------------------------------

// Block: real blade swings (strong style) on a blade guard: each blocked swing must cost the
// guard its style's price, and enough of them break it
static void T_GuardSwing_Setup( void ) {
	vec3_t p;
	AT_Weapon( 0, WP_SABER );
	AT_Weapon( 1, WP_SABER );
	AT_SaberStyle( A, SS_STRONG );
	AT_SaberStyle( B, SS_MEDIUM );
	AT_Spot( 56, 0, p );
	AT_Place( B, p, at.baseYaw + 180 );
	at.drive[0].aimAt = at.drive[1].aimAt = qtrue;
	AT_Drive( 1, 0, 0, 0, SHINOBI_BUTTON_BLOCK );
}
static atResult_t T_GuardSwing( int t ) {
	if ( STAGE( 0 ) && t >= 400 ) {
		at.v[0] = shinobiTest.guardHits; at.v[1] = shinobiTest.guardCost; at.v[2] = shinobiTest.guardBreaks;
		at.v[3] = B->health;
		NEXT();
	}
	if ( STAGE( 1 ) ) {
		AT_Drive( 0, 0, 0, 0, ( ( t / 600 ) % 2 ) ? 0 : BUTTON_ATTACK ); // swing, let go, swing...
		if ( shinobiTest.guardBreaks > at.v[2] || t >= 6000 ) {
			const int hits = shinobiTest.guardHits - at.v[0], cost = shinobiTest.guardCost - at.v[1];
			const int broke = shinobiTest.guardBreaks - at.v[2];
			if ( hits <= 0 ) {
				AT_Msg( "no blocked swing in 6 s (B lost %i health: the guard never caught the blade)", at.v[3] - B->health );
				return ATR_FAIL;
			}
			AT_Msg( "%i blocked swings cost %i guard (avg %.1f, strong style should be %i+), guard break: %s, B lost %i health",
				hits, cost, cost / (float)hits, SHINOBI_GUARD_SWING_STRONG, broke ? "yes" : "no", at.v[3] - B->health );
			return ( cost / (float)hits >= SHINOBI_GUARD_SWING_STRONG * 0.9f && broke ) ? ATR_PASS : ATR_FAIL;
		}
	}
	return ATR_RUN;
}

// Block: the swing that empties the guard breaks it - a 1 s stagger
static void T_GuardBreak_Setup( void ) {
	AT_Weapon( 0, WP_SABER );
	AT_Weapon( 1, WP_SABER );
	AT_SaberStyle( A, SS_STRONG );
}
static atResult_t T_GuardBreak( int t ) {
	if ( t < 200 )
		return ATR_RUN;
	{
		const int before = shinobiTest.guardBreaks;
		qboolean  held;
		B->client->ps.stats[STAT_ARMOR] = 20;
		held = Shinobi_SaberGuard( B, A, 30, B->r.currentOrigin );
		if ( held || shinobiTest.guardBreaks == before ) {
			AT_Msg( "a strong swing on a guard of 20 was held (guard now %i)", B->client->ps.stats[STAT_ARMOR] );
			return ATR_FAIL;
		}
		AT_Msg( "guard broke, B reels %i ms (want ~%i)", B->client->shinobi.staggerEnd - level.time, SHINOBI_GUARD_BREAK_MS );
		return ( B->client->shinobi.staggerEnd - level.time >= SHINOBI_GUARD_BREAK_MS - 100 ) ? ATR_PASS : ATR_FAIL;
	}
}

// Block: a perfect parry costs nothing and staggers the attacker long enough to answer
static void T_Parry_Setup( void ) {
	vec3_t p;
	AT_Weapon( 0, WP_SABER );
	AT_Weapon( 1, WP_SABER );
	AT_Spot( 60, 0, p );
	AT_Place( B, p, at.baseYaw + 180 );
}
static atResult_t T_Parry( int t ) {
	if ( t < 200 )
		return ATR_RUN;
	{
		const int guard = B->client->ps.stats[STAT_ARMOR], before = shinobiTest.parries;
		B->client->shinobi.blockPrevPress = level.time - 3000;
		B->client->shinobi.blockPress = level.time - 50;
		Shinobi_SaberGuard( B, A, 30, B->r.currentOrigin );
		AT_Msg( "parries %i, guard %i -> %i, attacker reels %i ms (want %i)", shinobiTest.parries - before, guard,
			B->client->ps.stats[STAT_ARMOR], A->client->shinobi.staggerEnd - level.time, SHINOBI_PARRY_STAGGER_MS );
		return ( shinobiTest.parries > before && B->client->ps.stats[STAT_ARMOR] == guard
			&& A->client->shinobi.staggerEnd - level.time >= SHINOBI_PARRY_STAGGER_MS - 100 ) ? ATR_PASS : ATR_FAIL;
	}
}

// Fuma Shuriken thrown with attack: out and back, the same damage both times, leaves the hand
static void T_Fuma_Setup( void ) {
	vec3_t p;
	AT_Spot( 300, 0, p );
	AT_Place( B, p, at.baseYaw + 180 );
	A->client->ps.ammo[SHINOBI_AMMO_SHURIKEN] = SHINOBI_SHURIKEN_MAX;
	AT_Weapon( 0, SHINOBI_WP_SHURIKEN );
	AT_Weapon( 1, WP_MELEE );
	at.drive[0].aimAt = qtrue;
}
static atResult_t T_Fuma( int t ) {
	if ( STAGE( 0 ) && t >= 600 ) {
		AT_Drive( 0, 0, 0, 0, BUTTON_ATTACK );
		NEXT();
	}
	if ( STAGE( 1 ) && SINCE() >= 150 ) {
		gentity_t *s = AT_FindClass( "shinobi_shuriken", at.a );
		AT_Drive( 0, 0, 0, 0, 0 );
		if ( !s ) {
			AT_Msg( "attack with the shuriken in hand threw nothing" );
			return ATR_FAIL;
		}
		at.f[0] = VectorLength( s->s.pos.trDelta );
		at.v[0] = ( A->client->ps.stats[STAT_WEAPONS] & ( 1 << SHINOBI_WP_SHURIKEN ) ) != 0;
		NEXT();
	}
	if ( STAGE( 2 ) && ( SINCE() >= 4500 || ( !AT_FindClass( "shinobi_shuriken", at.a ) && SINCE() > 300 ) ) ) {
		int hits, last = 0;
		const int total = AT_DamageTo( B, at.start, "shinobi_shuriken", -1, &hits, &last );
		const qboolean caught = A->client->ps.ammo[SHINOBI_AMMO_SHURIKEN] == SHINOBI_SHURIKEN_MAX;
		AT_Msg( "speed %.0f (want %i), weapon left the hand: %s, %i hits on B, %i damage (%s), caught back: %s",
			at.f[0], SHINOBI_SHURIKEN_SPEED, at.v[0] ? "NO" : "yes", hits, total,
			hits == 2 && total == 2 * last ? "equal" : "differ", caught ? "yes" : "no" );
		return ( !at.v[0] && hits == 2 && total == 2 * last && caught && fabs( at.f[0] - SHINOBI_SHURIKEN_SPEED ) < 5 ) ? ATR_PASS : ATR_FAIL;
	}
	return ATR_RUN;
}

// Poison Smoke Bomb: thrown at B's feet, it bursts into the cloud; B is poisoned (the thrower would be too, in it),
// and stays poisoned after he has left it; the pickups: a shuriken pickup gives 3 throws; every
// item a pickup may come back as is registered (else the cgame draws an axis for it)
static void T_Smoke_Setup( void ) {
	vec3_t p;
	AT_Spot( 420, 0, p ); // (lobbed level it comes down about 480 out: on him or just past him)
	AT_Place( B, p, at.baseYaw + 180 );
	A->client->ps.ammo[SHINOBI_AMMO_SMOKE] = 1;
	AT_Weapon( 0, SHINOBI_WP_SMOKE );
	AT_Weapon( 1, WP_MELEE );
	at.drive[0].aimAt = qtrue;
	at.v[0] = shinobiTest.smokeBursts;
	at.v[1] = shinobiTest.poisonTicks;
}
static atResult_t T_Smoke( int t ) {
	if ( STAGE( 0 ) && t >= 600 ) {
		AT_Drive( 0, 0, 0, 0, BUTTON_ATTACK );
		NEXT();
	}
	if ( STAGE( 1 ) ) {
		AT_Drive( 0, 0, 0, 0, 0 );
		if ( shinobiTest.smokeBursts > at.v[0] ) {
			at.v[2] = AT_CountClass( "shinobi_smokebomb", at.a );
			NEXT();
		}
		else if ( SINCE() > 3500 ) {
			AT_Msg( "the bomb never burst (thrown: %s)", AT_FindClass( "shinobi_smokebomb", at.a ) ? "yes" : "NO" );
			return ATR_FAIL;
		}
	}
	if ( STAGE( 2 ) && SINCE() >= 2000 ) { // in it a while: poisoned; then out of it
		vec3_t p;
		at.v[3] = B->client->shinobi.poisonEnd > level.time;
		at.v[4] = A->client->shinobi.poisonEnd > level.time;
		at.v[5] = shinobiTest.poisonTicks - at.v[1];
		AT_Spot( -200, 0, p ); // (behind A, far from the cloud)
		AT_Place( B, p, at.baseYaw + 180 );
		NEXT();
	}
	if ( STAGE( 3 ) && SINCE() >= 3000 ) {
		int amt = 0, i, unreg = 0;
		char cs[MAX_STRING_CHARS];
		const int after = shinobiTest.poisonTicks - at.v[1] - at.v[5];
		const qboolean still = B->client->shinobi.poisonEnd > level.time;
		static const char *pool[] = { "item_medpac", "ammo_force", "weapon_thermal", "weapon_bryar_pistol",
			"weapon_trip_mine", "weapon_det_pack", "weapon_rocket_launcher" };
		Shinobi_ToolForItem( "weapon_bryar_pistol", &amt );
		trap->GetConfigstring( CS_ITEMS, cs, sizeof( cs ) );
		for ( i = 0; i < (int)ARRAY_LEN( pool ); i++ ) {
			gitem_t *it = BG_FindItem( pool[i] );
			const int n = it ? (int)( it - bg_itemlist ) : -1;
			if ( n < 0 || n >= (int)strlen( cs ) || cs[n] != '1' )
				unreg++;
		}
		AT_Msg( "burst: yes; B poisoned in it: %s (ticks %i), A (the thrower, outside it): %s; 3 s after leaving it B still poisoned: %s (ticks %i); "
			"a shuriken pickup gives %i throws; pool items unregistered: %i",
			at.v[3] ? "yes" : "NO", at.v[5], at.v[4] ? "YES" : "no", still ? "yes" : "NO", after, amt, unreg );
		return ( at.v[3] && at.v[5] >= 2 && still && after >= 3 && amt == SHINOBI_STAR_PICKUP && !unreg ) ? ATR_PASS : ATR_FAIL;
	}
	return ATR_RUN;
}

// Kawarimi in the middle of our own combo (attack still held): it must hide us, not just eat a charge
static void T_KawaCombo_Setup( void ) {
	vec3_t p;
	AT_Weapon( 0, WP_MELEE );
	AT_Weapon( 1, WP_MELEE );
	AT_Spot( 48, 0, p );
	AT_Place( B, p, at.baseYaw + 180 );
	at.drive[0].aimAt = qtrue;
	AT_Drive( 0, 0, 0, 0, BUTTON_ATTACK );
}
static atResult_t T_KawaCombo( int t ) {
	if ( STAGE( 0 ) && t >= 500 ) {
		at.v[0] = shinobiTest.kawarimiHides;
		Shinobi_Cast( A, SJ_KAWARIMI, 1 );
		NEXT();
	}
	if ( STAGE( 1 ) && SINCE() >= 700 ) { // attack still held all along
		at.v[1] = SHINOBI_HIDDEN( &A->client->ps );
		at.v[2] = shinobiTest.kawarimiHides - at.v[0];
		AT_Drive( 0, 0, 0, 0, 0 );
		NEXT();
	}
	if ( STAGE( 2 ) && SINCE() >= 200 ) {
		AT_Drive( 0, 0, 0, 0, BUTTON_ATTACK ); // a new press: out
		NEXT();
	}
	if ( STAGE( 3 ) && SINCE() >= 200 ) {
		const qboolean out = !SHINOBI_HIDDEN( &A->client->ps );
		AT_Drive( 0, 0, 0, 0, 0 );
		AT_Msg( "hidden 0.7 s after casting with attack held: %s (hides %i); a new attack press reveals: %s",
			at.v[1] ? "yes" : "NO", at.v[2], out ? "yes" : "NO" );
		return ( at.v[1] && at.v[2] == 1 && out ) ? ATR_PASS : ATR_FAIL;
	}
	return ATR_RUN;
}

// Kawarimi from the floor, and out of a combo / juggle
static void T_KawaFloor_Setup( void ) {
	AT_Weapon( 0, WP_MELEE );
}
static atResult_t T_KawaFloor( int t ) {
	if ( STAGE( 0 ) && t >= 100 ) {
		G_Knockdown( A );
		NEXT();
	}
	if ( STAGE( 1 ) && SINCE() >= 150 ) {
		at.v[0] = A->client->ps.forceHandExtend == HANDEXTEND_KNOCKDOWN;
		Shinobi_Cast( A, SJ_KAWARIMI, 1 );
		NEXT();
	}
	if ( STAGE( 2 ) && SINCE() >= 500 ) {
		at.v[1] = SHINOBI_HIDDEN( &A->client->ps ) && A->client->ps.forceHandExtend != HANDEXTEND_KNOCKDOWN;
		Shinobi_Reveal( A );
		A->client->shinobi.ccImmuneEnd = 0;
		Shinobi_Flinch( A, 1500 );
		A->client->shinobi.juggleStart = level.time;
		A->client->shinobi.juggleEnd = level.time + 1500;
		A->client->shinobi.juggleBy = at.b + 1;
		NEXT();
	}
	if ( STAGE( 3 ) && SINCE() >= 100 ) {
		Shinobi_Cast( A, SJ_KAWARIMI, 1 );
		NEXT();
	}
	if ( STAGE( 4 ) && SINCE() >= 400 ) {
		const qboolean freed = A->client->shinobi.flinchEnd <= level.time && !A->client->shinobi.juggleEnd && SHINOBI_HIDDEN( &A->client->ps );
		AT_Msg( "was lying: %s, hidden from the floor: %s; out of flinch + juggle: %s",
			at.v[0] ? "yes" : "no", at.v[1] ? "yes" : "NO", freed ? "yes" : "NO" );
		return ( at.v[0] && at.v[1] && freed ) ? ATR_PASS : ATR_FAIL;
	}
	return ATR_RUN;
}

// any direct hit breaks a jutsu being charged; falling, lava (= burning) do not
static void T_Interrupt_Setup( void ) {
	AT_Weapon( 0, WP_MELEE );
}
static atResult_t T_Interrupt( int t ) {
	if ( t < 200 )
		return ATR_RUN;
	{
		qboolean hit, fall, lava;
		A->client->shinobi.chargeJutsu = SJ_KATON;
		A->client->shinobi.chargeStart = level.time;
		G_Damage( A, B, B, NULL, A->r.currentOrigin, 5, 0, MOD_MELEE );
		hit = A->client->shinobi.chargeJutsu == SJ_NONE;
		A->client->shinobi.chargeJutsu = SJ_KATON;
		G_Damage( A, NULL, NULL, NULL, NULL, 5, 0, MOD_FALLING );
		fall = A->client->shinobi.chargeJutsu == SJ_KATON;
		G_Damage( A, B, B, NULL, NULL, 5, DAMAGE_NO_ARMOR, MOD_LAVA );
		lava = A->client->shinobi.chargeJutsu == SJ_KATON;
		A->client->shinobi.chargeJutsu = SJ_NONE;
		AT_Msg( "a blow breaks the charge: %s; falling keeps it: %s; burning keeps it: %s",
			hit ? "yes" : "NO", fall ? "yes" : "NO", lava ? "yes" : "NO" );
		return ( hit && fall && lava ) ? ATR_PASS : ATR_FAIL;
	}
}

// knocked down: up on his own after ~0.7 s; with a direction held - the roll that way
static void T_Getup_Setup( void ) {
	AT_Weapon( 1, WP_MELEE );
}
static atResult_t T_Getup( int t ) {
	gclient_t *c = B->client;
	if ( STAGE( 0 ) && t >= 100 ) {
		G_Knockdown( B );
		at.v[0] = level.time;
		NEXT();
	}
	if ( STAGE( 1 ) && c->ps.forceHandExtend != HANDEXTEND_KNOCKDOWN ) {
		at.v[1] = level.time - at.v[0];
		at.v[2] = c->ps.legsAnim;
		NEXT();
	}
	if ( STAGE( 2 ) && SINCE() >= 1500 ) { // the roll: right held from the start
		c->shinobi.ccImmuneEnd = 0;
		G_Knockdown( B );
		at.v[3] = level.time;
		AT_Drive( 1, 0, 127, 0, 0 );
		NEXT();
	}
	if ( STAGE( 3 ) && ( c->ps.forceHandExtend != HANDEXTEND_KNOCKDOWN || SINCE() >= 3000 ) ) {
		at.v[4] = level.time - at.v[3];
		NEXT();
	}
	if ( STAGE( 4 ) && SINCE() >= 100 ) {
		const char *roll = AT_AnimName( c->ps.legsAnim );
		AT_Drive( 1, 0, 0, 0, 0 );
		AT_Msg( "lay %i ms with no keys (want ~%i..%i), then %s; with right held up after %i ms as %s",
			at.v[1], SHINOBI_KNOCKDOWN_MS, SHINOBI_KNOCKDOWN_MS + SHINOBI_GETUP_AUTO_MS + 300, AT_AnimName( at.v[2] ), at.v[4], roll );
		return ( at.v[1] >= SHINOBI_KNOCKDOWN_MS - 100 && at.v[1] <= SHINOBI_KNOCKDOWN_MS + SHINOBI_GETUP_AUTO_MS + 400
			&& at.v[4] < 2500 && strstr( roll, "ROLL" ) ) ? ATR_PASS : ATR_FAIL;
	}
	if ( t > 9000 ) {
		AT_Msg( "never got up (stage %i)", at.stage );
		return ATR_FAIL;
	}
	return ATR_RUN;
}

// the fight feeds chakra, and "+N chakra" shows
static void T_Chakra_Setup( void ) {
	vec3_t p;
	AT_Weapon( 0, WP_MELEE );
	AT_Weapon( 1, WP_MELEE );
	AT_Spot( 48, 0, p );
	AT_Place( B, p, at.baseYaw + 180 );
	at.drive[0].aimAt = qtrue;
	A->client->ps.fd.forcePower = A->client->ps.fd.forcePowerMax / 2;
}
static atResult_t T_Chakra( int t ) {
	if ( STAGE( 0 ) ) {
		at.v[0] = shinobiTest.chakraGained[at.a];
		at.v[1] = shinobiTest.chakraTexts[at.a];
		AT_Drive( 0, 0, 0, 0, BUTTON_ATTACK );
		NEXT();
	}
	if ( STAGE( 1 ) && SINCE() >= 2500 ) {
		const int gained = shinobiTest.chakraGained[at.a] - at.v[0], texts = shinobiTest.chakraTexts[at.a] - at.v[1];
		const int dealt = AT_DamageTo( B, at.start, NULL, -1, NULL, NULL );
		AT_Drive( 0, 0, 0, 0, 0 );
		AT_Msg( "dealt %i with fists, earned %i chakra (~%i%%), %i \"+N chakra\" texts", dealt, gained,
			dealt ? gained * 100 / dealt : 0, texts );
		return ( dealt > 0 && gained > 0 && texts > 0 ) ? ATR_PASS : ATR_FAIL;
	}
	return ATR_RUN;
}

// Gentle Fist: fists with the Byakugan open burn more of the target's chakra than being hit gives him
static void T_Gentle_Setup( void ) {
	vec3_t p;
	AT_Spot( 48, 0, p );
	AT_Place( B, p, at.baseYaw + 180 );
	at.drive[0].aimAt = qtrue;
	A->client->pers.shinobiBuild.perk = SP_BYAKUGAN;
	A->client->shinobi.eyes = SP_BYAKUGAN;
	B->client->ps.fd.forcePower = B->client->ps.fd.forcePowerMax * 6 / 10;
}
static atResult_t T_Gentle( int t ) {
	A->client->shinobi.eyesDrain = level.time + 100000; // (the eyes stay open)
	A->client->ps.fd.forcePower = A->client->ps.fd.forcePowerMax;
	if ( STAGE( 0 ) ) {
		at.v[0] = B->client->ps.fd.forcePower;
		at.v[1] = shinobiTest.chakraGained[at.b];
		AT_Drive( 0, 0, 0, 0, BUTTON_ATTACK );
		NEXT();
	}
	if ( STAGE( 1 ) && SINCE() >= 2500 ) {
		const int dealt = AT_DamageTo( B, at.start, NULL, MOD_MELEE, NULL, NULL );
		const int gained = shinobiTest.chakraGained[at.b] - at.v[1];
		const int now = B->client->ps.fd.forcePower;
		AT_Drive( 0, 0, 0, 0, 0 );
		AT_Msg( "fists dealt %i, the target's chakra %i -> %i (being hit gave him %i back)", dealt, at.v[0], now, gained );
		return ( dealt > 0 && now < at.v[0] && at.v[0] - now > gained ) ? ATR_PASS : ATR_FAIL;
	}
	return ATR_RUN;
}

// Wind Edge: a swing throws an air slash that cuts someone out of the fists' reach
static void T_WindEdge_Setup( void ) {
	vec3_t p;
	AT_Spot( 120, 0, p );
	AT_Place( B, p, at.baseYaw + 180 );
	at.drive[0].aimAt = qtrue;
}
static atResult_t T_WindEdge( int t ) {
	if ( STAGE( 0 ) && t >= 300 ) {
		Shinobi_CastEdge( A, SS_FUTON );
		NEXT();
	}
	if ( STAGE( 1 ) && SINCE() >= 200 ) {
		AT_Drive( 0, 0, 0, 0, BUTTON_ATTACK );
		NEXT();
	}
	if ( STAGE( 2 ) && SINCE() >= 100 ) {
		at.v[0] = AT_CountClass( "shinobi_windslash", at.a );
		AT_Drive( 0, 0, 0, 0, 0 );
		NEXT();
	}
	if ( STAGE( 3 ) && SINCE() >= 400 ) {
		int hits = 0;
		const int dealt = AT_DamageTo( B, at.start, "shinobi_windslash", -1, &hits, NULL );
		A->client->shinobi.infuseUntil = 0;
		AT_Msg( "slash drawn %s, cut %i time(s) for %i at 120 units", at.v[0] ? "yes" : "NO", hits, dealt );
		return ( at.v[0] && hits > 0 && dealt > 0 ) ? ATR_PASS : ATR_FAIL;
	}
	return ATR_RUN;
}

/*
saber_dps: every blade and stance swings at a still target (fists in hand, no guard) for
SDPS_MS holding attack: damage, hits, swings, the spread of one hit, damage a second.
A measurement for balancing (it always passes if every case hit at all).
*/
#define SDPS_MS     8000
#define SDPS_GAP    600
#define SDPS_FIRST  1500  // before the first case: the bots settle after spawning
typedef struct { const char *s1, *s2; int style, fwd; } sdpsCase_t;
static const sdpsCase_t sdpsCases[] = { // (each fights in its own stance: the .sab's saberStyle)
	{ "shinobi_katana", "none", 0 }, { "shinobi_kurosawa", "none", 0 }, { "shinobi_kubikiribocho", "none", 0 },
	{ "shinobi_samehada", "none", 0 }, { "shinobi_knife", "shinobi_knife_l", 0 },
	{ "shinobi_shibuki", "none", 0 }, { "shinobi_nuibari", "none", 0 },
	{ "shinobi_uchiha_blade", "shinobi_uchiha_blade", 0 }, { "shinobi_cloud_tanto", "shinobi_cloud_tanto", 0 },
	{ "shinobi_knife", "shinobi_knife_l", 0, 127 }, // forward held: the pair's cuts chain without returns
};
#define SDPS_COUNT ( (int)( sizeof( sdpsCases ) / sizeof( sdpsCases[0] ) ) )
static const char *sdpsStyleNames[] = { "-", "fast", "medium", "strong", "desann", "tavion", "dual", "staff" };
static struct {
	int  idx, phaseStart, swings, lastMove, hitsBefore, minHit, maxHit, knocked;
	int  dealt[SDPS_COUNT], hits[SDPS_COUNT];
	vec3_t spot;
} sdps;

static void SDPS_Arm( void ) {
	const sdpsCase_t *c = &sdpsCases[sdps.idx];
	gentity_t        *e = A;
	G_SetSaber( e, 0, (char *)c->s1, qfalse );
	G_SetSaber( e, 1, (char *)c->s2, qfalse );
	G_SaberModelSetup( e );
	AT_Weapon( 0, WP_SABER );
	e->client->ps.saberHolstered = 0;
	if ( c->style )
		AT_SaberStyle( e, c->style );
	Shinobi_BladeStance( e );
}
static void T_SaberDps_Setup( void ) {
	memset( &sdps, 0, sizeof( sdps ) );
	AT_Spot( 44, 0, sdps.spot );
	AT_Place( B, sdps.spot, at.baseYaw + 180 );
	at.drive[0].aimAt = qtrue;
	SDPS_Arm();
	sdps.phaseStart = level.time;
	sdps.minHit = 99999;
	A->client->pers.shinobiBuild.stats[SST_TAIJUTSU] = 0; // the blade's own numbers (no taijutsu points on top)
	AT_Print( "[autotest]   %-22s %-16s %-7s %6s %5s %6s %7s %9s %5s\n", "blade", "left", "stance",
		"dealt", "hits", "swings", "hit", "dmg/s", "down" );
}
static atResult_t T_SaberDps( int t ) {
	gclient_t *ac = A->client, *bc = B->client;
	const int  since = level.time - sdps.phaseStart;
	int        i;

	// the target stays put, alive, standing and fresh
	if ( Distance( B->client->ps.origin, sdps.spot ) > 6 )
		AT_Place( B, sdps.spot, at.baseYaw + 180 );
	VectorClear( bc->ps.velocity );
	bc->ps.eFlags &= ~EF_INVULNERABLE; // (a respawn would bring the spawn shield back)
	bc->invulnerableTimer = 0;
	if ( B->health < 200 )
		B->health = bc->ps.stats[STAT_HEALTH] = 500;
	if ( bc->ps.forceHandExtend == HANDEXTEND_KNOCKDOWN ) {
		sdps.knocked++;
		bc->ps.forceHandExtend = HANDEXTEND_NONE;
		bc->ps.forceHandExtendTime = 0;
	}
	bc->shinobi.flinchEnd = bc->shinobi.staggerEnd = 0;
	ac->ps.fd.forcePower = ac->ps.fd.forcePowerMax; // (specials and kata cost force)

	if ( since < ( sdps.idx ? SDPS_GAP : SDPS_FIRST ) ) { // between cases: hands off
		AT_Drive( 0, 0, 0, 0, 0 );
		return ATR_RUN;
	}
	if ( since < ( sdps.idx ? SDPS_GAP : SDPS_FIRST ) + SDPS_MS ) {
		AT_Drive( 0, sdpsCases[sdps.idx].fwd, 0, 0, BUTTON_ATTACK );
		if ( !sdps.lastMove )
			sdps.lastMove = ac->shinobi.bladeSwing + 1; // (the count when this case began, + 1)
		sdps.swings = ac->shinobi.bladeSwing + 1 - sdps.lastMove;
		return ATR_RUN;
	}
	{ // the case is over: what it did
		const sdpsCase_t *c = &sdpsCases[sdps.idx];
		const int from = sdps.phaseStart + ( sdps.idx ? SDPS_GAP : SDPS_FIRST );
		int k, hits = 0, dealt = 0, mn = 99999, mx = 0;
		for ( k = 0; k < SHINOBI_TEST_DMGLOG; k++ ) {
			const shinobiTestDamage_t *d = &shinobiTest.dmg[k];
			if ( !d->time || d->time < from || d->targ != at.b || d->attacker != at.a || d->mod != MOD_SABER )
				continue;
			hits++;
			dealt += d->take;
			if ( d->take < mn ) mn = d->take;
			if ( d->take > mx ) mx = d->take;
		}
		sdps.dealt[sdps.idx] = dealt;
		sdps.hits[sdps.idx] = hits;
		AT_Print( "[autotest]   %-22s %-16s %-7s %6i %5i %6i %3i-%-3i %9.1f %5i\n", c->s1, c->s2,
			c->fwd ? va( "%s+fwd", sdpsStyleNames[ac->ps.fd.saberAnimLevel & 7] ) : sdpsStyleNames[ac->ps.fd.saberAnimLevel & 7],
			dealt, hits, sdps.swings, hits ? mn : 0, hits ? mx : 0, dealt * 1000.0f / SDPS_MS, sdps.knocked );
	}
	AT_Drive( 0, 0, 0, 0, 0 );
	if ( ++sdps.idx >= SDPS_COUNT ) {
		int bad = 0;
		for ( i = 0; i < SDPS_COUNT; i++ )
			bad += !sdps.hits[i];
		AT_Msg( "%i blade / stance cases measured, %i never hit", SDPS_COUNT, bad );
		return bad ? ATR_FAIL : ATR_PASS;
	}
	sdps.swings = sdps.knocked = 0;
	sdps.lastMove = 0;
	sdps.phaseStart = level.time;
	SDPS_Arm();
	return ATR_RUN;
}

/*
blade_combo: each weapon's own finisher (forward held) and the three common ones (katana,
back / side / nothing held): the chain lands, the finisher comes, its effect shows.
*/
typedef struct { const char *s1, *s2; int fwd, right, up, kind; const char *what; } bcCase_t;
static const bcCase_t bcCases[] = {
	{ "shinobi_katana", "none", 127, 0, 0, SFX_IAI, "katana: iai (stagger)" },
	{ "shinobi_kurosawa", "none", 127, 0, 0, SFX_FLURRY, "kurosawa: flurry (2 more cuts)" },
	{ "shinobi_kubikiribocho", "none", 127, 0, 0, SFX_BLEED, "kubikiribocho: bleed" },
	{ "shinobi_samehada", "none", 127, 0, 0, SFX_DRAIN, "samehada: drain chakra" },
	{ "shinobi_knife", "shinobi_knife_l", 127, 0, 0, -1, "knives: wide arc far ahead" },
	{ "shinobi_uchiha_blade", "shinobi_uchiha_blade", 127, 0, 0, SFX_BURN, "uchiha blades: set alight" },
	{ "shinobi_cloud_tanto", "shinobi_cloud_tanto", 127, 0, 0, SFX_BLEED, "cloud short swords: bleed" },
	{ "shinobi_shibuki", "none", 127, 0, 0, SFX_BLAST, "shibuki: blast, thrown down" },
	{ "shinobi_nuibari", "none", 127, 0, 0, SFX_STUN, "nuibari: stun 1.5 s" },
	{ "shinobi_uchiha_blade", "shinobi_uchiha_blade", 0, 0, 0, SFX_LAUNCH, "uchiha blades: rising cut (pair)" },
	{ "shinobi_katana", "none", -127, 0, 0, SFX_SWEEP, "back: sweep (knockdown)" },
	{ "shinobi_katana", "none", 0, 127, 0, SFX_SPIN, "side: spin" },
	{ "shinobi_katana", "none", 0, 0, 0, SFX_LAUNCH, "nothing held: rising cut (launch)" },
};
#define BC_COUNT  ( (int)( sizeof( bcCases ) / sizeof( bcCases[0] ) ) )
#define BC_MS     12000
static struct {
	int idx, start, found, foundAt, before, chakra0, ok, okCount, reach0, arc0;
	vec3_t spotA, spotB;
} bc;

static void BC_Arm( void ) {
	const bcCase_t *c = &bcCases[bc.idx];
	G_SetSaber( A, 0, (char *)c->s1, qfalse );
	G_SetSaber( A, 1, (char *)c->s2, qfalse );
	G_SaberModelSetup( A );
	AT_Weapon( 0, WP_SABER );
	A->client->ps.saberHolstered = 0;
	Shinobi_BladeStance( A );
	A->client->shinobi.bladeChain = 0;
	AT_Reset( B );
	B->client->ps.fd.forcePower = B->client->ps.fd.forcePowerMax;
	bc.chakra0 = B->client->ps.fd.forcePower;
	bc.start = level.time;
	bc.found = bc.foundAt = bc.ok = 0;
	bc.before = shinobiTest.bladeFinishers[SBF_REACH];
	bc.reach0 = shinobiTest.bladeFinisherReach;
	bc.arc0 = shinobiTest.arcDamage;
}
static void T_BladeCombo_Setup( void ) {
	memset( &bc, 0, sizeof( bc ) );
	VectorCopy( A->client->ps.origin, bc.spotA );
	AT_Spot( 44, 0, bc.spotB );
	AT_Place( B, bc.spotB, at.baseYaw + 180 );
	at.drive[0].aimAt = qtrue;
	A->client->pers.shinobiBuild.stats[SST_TAIJUTSU] = 0;
	BC_Arm();
}
static gentity_t *BC_FinisherFx( int kind ) {
	int i;
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *e = &g_entities[i];
		if ( e->inuse && e->classname && !strcmp( e->classname, "shinobi_finisher" ) && e->s.frame == kind
			&& e->s.time >= bc.start )
			return e;
	}
	return NULL;
}
static atResult_t T_BladeCombo( int t ) {
	const bcCase_t *c = &bcCases[bc.idx];
	gclient_t *ac = A->client, *bcl = B->client;
	const int need = shinobiSwordChain[Shinobi_BladeStylePublic( A )];

	// both stay put; the target never runs dry or dies
	if ( Distance( ac->ps.origin, bc.spotA ) > 6 && !bc.found )
		AT_Place( A, bc.spotA, at.baseYaw );
	if ( B->health < 200 )
		B->health = bcl->ps.stats[STAT_HEALTH] = 500;
	bcl->ps.eFlags &= ~EF_INVULNERABLE;
	bcl->invulnerableTimer = 0;
	ac->ps.fd.forcePower = ac->ps.fd.forcePowerMax / 2; // (room for the drain to show)

	if ( !bc.found ) {
		if ( Distance( bcl->ps.origin, bc.spotB ) > 6 )
			AT_Place( B, bc.spotB, at.baseYaw + 180 );
		VectorClear( bcl->ps.velocity );
		if ( bcl->ps.forceHandExtend == HANDEXTEND_KNOCKDOWN && c->kind != SFX_SWEEP && c->kind != SFX_BLAST ) {
			bcl->ps.forceHandExtend = HANDEXTEND_NONE;
			bcl->ps.forceHandExtendTime = 0;
		}
		// the direction only for the finishing blow (held all along it would move or jump him)
		if ( ac->shinobi.bladeChain >= need - 1 && Shinobi_BladeChainLivePublic( ac ) )
			AT_Drive( 0, c->fwd, c->right, c->up, BUTTON_ATTACK );
		else
			AT_Drive( 0, 0, 0, 0, BUTTON_ATTACK );
		if ( c->kind >= 0 ? BC_FinisherFx( c->kind ) != NULL : ( AT_CountClass( "shinobi_windslash", at.a ) && shinobiTest.bladeFinishers[SBF_REACH] > bc.before ) ) {
			bc.found = 1;
			bc.foundAt = level.time;
			switch ( c->kind ) { // what can be seen right away
			case SFX_IAI:    bc.ok = bcl->shinobi.staggerEnd > level.time; break;
			case SFX_DRAIN:  bc.ok = bcl->ps.fd.forcePower < bc.chakra0 - 10; break;
			case SFX_SWEEP:  bc.ok = bcl->ps.forceHandExtend == HANDEXTEND_KNOCKDOWN || BG_InKnockDown( bcl->ps.legsAnim ); break;
			case SFX_LAUNCH: bc.ok = bcl->shinobi.juggleEnd > level.time; break;
			case SFX_SPIN:   bc.ok = 1; break;
			case SFX_BURN:   bc.ok = bcl->shinobi.burnEnd > level.time; break;
			case SFX_BLAST:  bc.ok = bcl->ps.forceHandExtend == HANDEXTEND_KNOCKDOWN || BG_InKnockDown( bcl->ps.legsAnim ); break;
			case SFX_STUN:   bc.ok = bcl->shinobi.stunEnd > level.time + 1000; break;
			case -1:         bc.ok = shinobiTest.arcDamage > bc.arc0; break; // (the arc did hurt him)
			default:         bc.ok = 0; break;
			}
		}
		else if ( level.time - bc.start > BC_MS ) {
			AT_Print( "[autotest]   %-34s NO FINISHER in %i s (chain %i of %i)\n", c->what, BC_MS / 1000, ac->shinobi.bladeChain, need );
			goto next;
		}
		return ATR_RUN;
	}
	AT_Drive( 0, 0, 0, 0, 0 );
	if ( level.time - bc.foundAt < 700 )
		return ATR_RUN;
	{ // what takes a moment: Kurosawa's extra cuts, the bleeding
		int k, extra = 0, bleed = 0;
		for ( k = 0; k < SHINOBI_TEST_DMGLOG; k++ ) {
			const shinobiTestDamage_t *d = &shinobiTest.dmg[k];
			if ( !d->time || d->time <= bc.foundAt || d->targ != at.b || d->attacker != at.a )
				continue;
			if ( d->mod == MOD_SABER )
				extra++;
			if ( d->mod == MOD_TRIGGER_HURT )
				bleed++;
		}
		if ( c->kind == SFX_FLURRY )
			bc.ok = extra >= SHINOBI_SWORD_FLURRY;
		if ( c->kind == SFX_BLEED )
			bc.ok = bleed >= 1 && bcl->shinobi.bleedEnd > level.time;
		AT_Print( "[autotest]   %-34s finisher after %.1f s: %s%s%s\n", c->what, ( bc.foundAt - bc.start ) / 1000.0f,
			bc.ok ? "works" : "EFFECT MISSING", shinobiTest.bladeFinisherReach > bc.reach0 ? " - landed by reach (the blade missed)" : " - the blade cut",
			c->kind == SFX_FLURRY ? va( " (%i extra cuts)", extra ) : c->kind == SFX_BLEED ? va( " (%i bleed ticks so far)", bleed )
			: c->kind < 0 ? va( " (the arc: %i damage)", shinobiTest.arcDamage - bc.arc0 ) : "" );
		if ( bc.ok )
			bc.okCount++;
	}
next:
	AT_Drive( 0, 0, 0, 0, 0 );
	if ( ++bc.idx >= BC_COUNT ) {
		AT_Msg( "%i of %i finishers work", bc.okCount, BC_COUNT );
		return bc.okCount == BC_COUNT ? ATR_PASS : ATR_FAIL;
	}
	BC_Arm();
	return ATR_RUN;
}

/*
blade_clicks: the combo with single clicks (a press every 0.7 s) and with attack held, on a
still target: every blow counted, the finisher's swing and which click started it, written
down as it happens - no finisher may start without a new press after the combo got ready.
*/
static struct { int stage, start, clicks, lastChain, readyAt, readyClick, finAt, finClick, held, moveWas, bad; } bk;
static void T_BladeClicks_Setup( void ) {
	vec3_t p;
	memset( &bk, 0, sizeof( bk ) );
	AT_Spot( 44, 0, p );
	AT_Place( B, p, at.baseYaw + 180 );
	at.drive[0].aimAt = qtrue;
	G_SetSaber( A, 0, "shinobi_katana", qfalse );
	G_SetSaber( A, 1, "none", qfalse );
	G_SaberModelSetup( A );
	AT_Weapon( 0, WP_SABER );
	A->client->ps.saberHolstered = 0;
	Shinobi_BladeStance( A );
	bk.start = level.time;
}
static atResult_t T_BladeClicks( int t ) {
	gclient_t *ac = A->client, *bcl = B->client;
	const int  v = ac->ps.stats[STAT_SHINOBI_BLADECOMBO], have = v & 7, need = ( v >> 3 ) & 7;
	const int  phase = t < 9000 ? 0 : 1; // clicks, then held
	int        buttons = 0;

	if ( B->health < 200 )
		B->health = bcl->ps.stats[STAT_HEALTH] = 500;
	bcl->ps.eFlags &= ~EF_INVULNERABLE;
	if ( phase == 0 ) {
		const int ph = ( t % 700 );
		buttons = ph < 80 ? BUTTON_ATTACK : 0;
		if ( buttons && !bk.held )
			bk.clicks++; // (a click: the press, counted once)
		bk.held = buttons;
	}
	else
		buttons = t < 17000 ? BUTTON_ATTACK : 0;
	AT_Drive( 0, 0, 0, 0, buttons );
	if ( have != bk.lastChain ) {
		AT_Print( "[autotest]     %5i ms %s: blows %i of %i%s\n", t, phase ? "held " : va( "click %i", bk.clicks ), have, need,
			Shinobi_BladeFinisherReady( &ac->ps ) ? " - READY" : "" );
		if ( Shinobi_BladeFinisherReady( &ac->ps ) ) {
			bk.readyAt = t;
			bk.readyClick = bk.clicks;
		}
		bk.lastChain = have;
	}
	if ( ac->ps.saberMove != bk.moveWas && Shinobi_FinisherKindOfMove( ac->ps.saberMove ) ) {
		AT_Print( "[autotest]     %5i ms %s: FINISHER swing starts (move %i)%s\n", t, phase ? "held " : va( "click %i", bk.clicks ),
			ac->ps.saberMove, ( phase == 0 && bk.clicks == bk.readyClick ) ? " - WITHOUT A NEW CLICK" : "" );
		if ( phase == 0 && bk.clicks == bk.readyClick )
			bk.bad++;
		bk.finAt = t;
	}
	bk.moveWas = ac->ps.saberMove;
	if ( t >= 17800 ) {
		AT_Msg( "%i finisher swing(s) started without a new click", bk.bad );
		return bk.bad ? ATR_FAIL : ATR_PASS;
	}
	return ATR_RUN;
}

// natural regen: normal up to 30%, slower above
static void T_Regen_Setup( void ) {
	A->client->ps.fd.forcePower = 5;
}
static atResult_t T_Regen( int t ) {
	gclient_t *c = A->client;
	if ( STAGE( 0 ) && t >= 500 ) {
		at.v[0] = c->ps.fd.forcePower;
		NEXT();
	}
	if ( STAGE( 1 ) && SINCE() >= 1500 ) {
		at.v[1] = c->ps.fd.forcePower - at.v[0];
		c->ps.fd.forcePower = c->ps.fd.forcePowerMax * 60 / 100;
		NEXT();
	}
	if ( STAGE( 2 ) && SINCE() >= 500 ) {
		at.v[2] = c->ps.fd.forcePower;
		NEXT();
	}
	if ( STAGE( 3 ) && SINCE() >= 3000 ) {
		const float low = at.v[1] / 1.5f, high = ( c->ps.fd.forcePower - at.v[2] ) / 3.0f;
		AT_Msg( "regen below 30%%: %.1f/s, above: %.1f/s (ratio %.1f, want ~%i)", low, high, high > 0 ? low / high : 99.0f,
			(int)SHINOBI_REGEN_SLOW_ABOVE );
		return ( low > 0 && high > 0 && low / high > 2.5f && low / high < 6.0f ) ? ATR_PASS : ATR_FAIL;
	}
	return ATR_RUN;
}

// a combo keeps its target in place, flinching, until the finisher
static void T_Flinch_Setup( void ) {
	vec3_t p;
	AT_Weapon( 0, WP_MELEE );
	AT_Weapon( 1, WP_MELEE );
	AT_Spot( 48, 0, p );
	AT_Place( B, p, at.baseYaw + 180 );
	at.drive[0].aimAt = qtrue;
	AT_Drive( 0, 0, 0, 0, BUTTON_ATTACK );
	VectorCopy( p, at.f + 0 ); // (f[0..2]: where B stood)
}
static atResult_t T_Flinch( int t ) {
	int hits;
	AT_DamageTo( B, at.start, NULL, MOD_MELEE, &hits, NULL );
	if ( STAGE( 0 ) && hits >= 1 ) {
		at.v[0] = at.v[1] = 0;
		NEXT();
	}
	if ( STAGE( 1 ) ) { // between the first and the third blow: flinching? standing?
		at.v[0]++;
		if ( B->client->ps.stats[STAT_SHINOBI2] & SHINOBI_STAT2_FLINCH )
			at.v[1]++;
		if ( hits >= 3 ) {
			vec3_t d;
			VectorSubtract( B->client->ps.origin, at.f, d );
			d[2] = 0;
			at.f[4] = VectorLength( d );
			NEXT();
		}
	}
	if ( STAGE( 2 ) || t > 4000 ) {
		AT_Drive( 0, 0, 0, 0, 0 );
		if ( !STAGE( 2 ) ) {
			AT_Msg( "only %i blows landed in 4 s", hits );
			return ATR_FAIL;
		}
		AT_Msg( "flinching %i of %i frames between blows 1 and 3, B moved %.0f u", at.v[1], at.v[0], at.f[4] );
		return ( at.v[0] && at.v[1] * 10 >= at.v[0] * 8 && at.f[4] < 40 ) ? ATR_PASS : ATR_FAIL;
	}
	return ATR_RUN;
}

// the blade's air game: the rising cut (nothing held), jump = Shunshin up to him, attack = a plunge straight down that slams him
static void T_SwordAir_Setup( void ) {
	T_BladeCombo_Setup();
	bc.idx = BC_COUNT - 1; // the katana's rising cut
	BC_Arm();
	at.v[0] = shinobiTest.slams;
	at.v[1] = shinobiTest.swordDives;
}
static atResult_t T_SwordAir( int t ) {
	gclient_t *ac = A->client, *bcl = B->client;
	const int  need = shinobiSwordChain[Shinobi_BladeStylePublic( A )];
	const int  slams = shinobiTest.slams - at.v[0], dives = shinobiTest.swordDives - at.v[1];

	if ( B->health < 200 )
		B->health = bcl->ps.stats[STAT_HEALTH] = 500;
	bcl->ps.eFlags &= ~EF_INVULNERABLE;
	bcl->invulnerableTimer = 0;
	if ( STAGE( 0 ) ) { // the chain, then the rising cut
		if ( Distance( ac->ps.origin, bc.spotA ) > 6 )
			AT_Place( A, bc.spotA, at.baseYaw );
		if ( Distance( bcl->ps.origin, bc.spotB ) > 6 )
			AT_Place( B, bc.spotB, at.baseYaw + 180 );
		VectorClear( bcl->ps.velocity );
		AT_Drive( 0, 0, 0, 0, BUTTON_ATTACK );
		if ( bcl->shinobi.juggleEnd > level.time && bcl->shinobi.juggleBy == A->s.number + 1 ) {
			AT_Drive( 0, 0, 0, 0, 0 );
			NEXT();
		}
		else if ( t > 9000 ) {
			AT_Msg( "no rising cut in 9 s (chain %i of %i)", ac->shinobi.bladeChain, need );
			return ATR_FAIL;
		}
	}
	if ( STAGE( 1 ) && SINCE() >= 100 ) { // jump: Shunshin up to him
		AT_Drive( 0, 0, 0, 127, 0 );
		NEXT();
	}
	if ( STAGE( 2 ) ) {
		if ( !at.v[2] && ac->ps.groundEntityNum == ENTITYNUM_NONE && ac->ps.origin[2] > bc.spotA[2] + 40 )
			at.v[2] = 1; // up there
		if ( SINCE() >= 250 )
			AT_Drive( 0, 0, 0, 0, ( ( SINCE() / 120 ) % 2 ) ? 0 : BUTTON_ATTACK );
		if ( ( ac->ps.stats[STAT_SHINOBI2] & SHINOBI_STAT2_DIVE )
			&& ac->ps.legsAnim == BOTH_A1_T__B_ + ( ac->ps.fd.saberAnimLevel - SS_FAST ) * SABER_ANIM_GROUP_SIZE )
			at.v[3] = 1; // the plunge played (the stance's overhead cut)
		if ( ( slams && dives ) || SINCE() >= 3000 ) {
			at.v[4] = bcl->ps.forceHandExtend == HANDEXTEND_KNOCKDOWN || BG_InKnockDown( bcl->ps.legsAnim );
			AT_Drive( 0, 0, 0, 0, 0 );
			NEXT();
		}
	}
	if ( STAGE( 3 ) && SINCE() >= 1000 ) {
		int fallHits;
		AT_DamageTo( B, at.start, NULL, MOD_FALLING, &fallHits, NULL );
		AT_Msg( "A up to him: %s, plunge anim: %s, slams: %i, plunge landings: %i, B down: %s, falling damage on B: %i",
			at.v[2] ? "yes" : "NO", at.v[3] ? "yes" : "NO", slams, dives, at.v[4] ? "yes" : "no", fallHits );
		return ( at.v[2] && at.v[3] && slams && dives && !fallHits ) ? ATR_PASS : ATR_FAIL;
	}
	return ATR_RUN;
}

// the air combo: launcher (forward on the finisher) -> jump after him -> two blows -> the slam
static void T_AirCombo_Setup( void ) {
	vec3_t p;
	AT_Weapon( 0, WP_MELEE );
	AT_Weapon( 1, WP_MELEE );
	AT_Spot( 46, 0, p );
	AT_Place( B, p, at.baseYaw + 180 );
	at.drive[0].aimAt = qtrue;
	at.v[0] = shinobiTest.launches;
	at.v[1] = shinobiTest.airHits;
	at.v[2] = shinobiTest.slams;
	AT_Drive( 0, 0, 0, 0, BUTTON_ATTACK );
}
static atResult_t T_AirCombo( int t ) {
	const int launches = shinobiTest.launches - at.v[0], air = shinobiTest.airHits - at.v[1], slams = shinobiTest.slams - at.v[2];
	if ( STAGE( 0 ) ) { // the chain, forward held for the finisher
		AT_Drive( 0, SHINOBI_COMBO_HIT( &A->client->ps ) >= shinobiFistChain[SFS_FAST] - 1 ? 127 : 0, 0, 0, BUTTON_ATTACK );
		if ( launches ) {
			AT_Drive( 0, 0, 0, 0, 0 );
			at.v[3] = t;
			NEXT();
		}
		else if ( t > 4000 ) {
			AT_Msg( "no launcher in 4 s (combo hit %i)", SHINOBI_COMBO_HIT( &A->client->ps ) );
			return ATR_FAIL;
		}
	}
	if ( STAGE( 1 ) && SINCE() >= 80 ) { // jump after him
		AT_Drive( 0, 0, 0, 127, 0 );
		NEXT();
	}
	if ( STAGE( 2 ) ) {
		if ( SINCE() >= 200 )
			AT_Drive( 0, 0, 0, 0, ( ( SINCE() / 120 ) % 2 ) ? 0 : BUTTON_ATTACK ); // tap, tap...
		if ( !at.v[4] && A->client->ps.groundEntityNum == ENTITYNUM_NONE )
			at.v[4] = 1; // he did leave the ground
		if ( slams || SINCE() >= 2500 ) {
			at.v[5] = B->client->ps.forceHandExtend == HANDEXTEND_KNOCKDOWN;
			NEXT();
		}
	}
	if ( STAGE( 3 ) && SINCE() >= 1200 ) {
		int fallHits;
		AT_DamageTo( B, at.start, NULL, MOD_FALLING, &fallHits, NULL );
		AT_Drive( 0, 0, 0, 0, 0 );
		AT_Msg( "launched: %i, A in the air: %s, air blows: %i (want 2), slam: %i, B down after it: %s, falling damage on B: %i",
			launches, at.v[4] ? "yes" : "NO", air, slams, at.v[5] ? "yes" : "no", fallHits );
		return ( launches && at.v[4] && air >= 2 && slams && !fallHits ) ? ATR_PASS : ATR_FAIL;
	}
	return ATR_RUN;
}

// build rules: specials cost points, the body goes to 7 without one, old builds are trimmed, bots stay legal
static atResult_t T_Build( int t ) {
	shinobiBuild_t b;
	char           why[128], str[256];
	int            i, bad = 0, ok = 0, n = 0;

	Shinobi_BuildParse( "suiton,futon/2,2,2,2,2/sharingan", &b );
	Shinobi_BuildFix( &b );
	n = Shinobi_BuildPoints( &b );
	ok += ( n == SHINOBI_STAT_POINTS - shinobiPerkCost[SP_SHARINGAN] );
	memset( &b, 0, sizeof( b ) );
	b.perk = SP_NONE; b.stats[SST_HEALTH] = 7; b.stats[SST_TAIJUTSU] = 3;
	ok += Shinobi_BuildValid( &b, why, sizeof( why ) ) ? 1 : 0;
	b.perk = SP_SHARINGAN;
	ok += !Shinobi_BuildValid( &b, why, sizeof( why ) );
	memset( &b, 0, sizeof( b ) );
	b.perk = SP_NONE; b.stats[SST_CAST] = 6;
	ok += !Shinobi_BuildValid( &b, why, sizeof( why ) );
	for ( i = SP_NONE; i < SP_COUNT; i++ ) {
		b.perk = i;
		ok += Shinobi_StatBudget( &b ) == SHINOBI_STAT_POINTS - shinobiPerkCost[i];
	}
	for ( i = 0; i < 300; i++ ) {
		Shinobi_BotRandomBuildPublic( &b );
		if ( !Shinobi_BuildValid( &b, why, sizeof( why ) ) ) {
			bad++;
			Shinobi_BuildString( &b, str, sizeof( str ) );
		}
	}
	AT_Msg( "rules %i/%i (old Sharingan build trimmed to %i points), random bot builds: %i of 300 illegal%s%s",
		ok, 4 + SP_COUNT, n, bad, bad ? " e.g. " : "", bad ? str : "" );
	return ( ok == 4 + SP_COUNT && !bad ) ? ATR_PASS : ATR_FAIL;
}

// 64 Seals: rooted in its pose while the field grows; full, the palm strikes B (at 200, inside it) and throws
// him out; held on, B back in (150) is struck again; let go: over
static void T_Palms_Setup( void ) {
	vec3_t p;
	AT_Weapon( 0, WP_MELEE );
	AT_Weapon( 1, WP_MELEE );
	AT_Spot( 200, 0, p );
	AT_Place( B, p, at.baseYaw + 180 );
	at.v[0] = shinobiTest.palmsHits;
}
static atResult_t T_Palms( int t ) {
	if ( STAGE( 0 ) && t >= 200 ) {
		if ( Shinobi_Cast( A, SJ_PALMS, 1 ) )
			A->client->shinobi.chargeJutsu = SJ_PALMS;
		VectorCopy( A->client->ps.origin, at.vec[0] );
		at.v[1] = A->client->ps.fd.forcePower;
		AT_Drive( 0, 127, 0, 0, 0 ); // (trying to walk: rooted)
		NEXT();
	}
	if ( STAGE( 1 ) ) {
		if ( SINCE() >= 1000 && !at.v[2] ) {
			at.v[2] = 1;
			at.f[0] = Distance( A->client->ps.origin, at.vec[0] );
			at.v[3] = A->client->ps.forceHandExtend == HANDEXTEND_TAUNT; // in the stance
		}
		if ( SINCE() >= SHINOBI_PALMS_GROW_MS + 400 ) {
			vec3_t p;
			AT_DamageTo( B, at.start, NULL, MOD_FORCE_DARK, &at.v[4], NULL );
			at.f[1] = Distance( B->client->ps.origin, A->client->ps.origin );
			at.v[5] = A->client->shinobi.palmsStart != 0; // still held: the field guards
			AT_Spot( 150, 0, p ); // B walks back in
			AT_Place( B, p, at.baseYaw + 180 );
			NEXT();
		}
	}
	if ( STAGE( 2 ) && SINCE() >= 1300 ) {
		int hits;
		AT_DamageTo( B, at.start, NULL, MOD_FORCE_DARK, &hits, NULL );
		at.f[2] = Distance( A->client->ps.origin, at.vec[0] );
		Cmd_JutsuRelease_f( A );
		AT_Drive( 0, 0, 0, 0, 0 );
		AT_Msg( "A moved %.0f while it grew, %.0f in all (rooted), stance pose: %s, chakra spent %i; full: B hit %i times, thrown to %.0f; "
			"still held: %s, B back in: hit %i times in all; let go: %s",
			at.f[0], at.f[2], at.v[3] ? "yes" : "NO", at.v[1] - A->client->ps.fd.forcePower, at.v[4], at.f[1], at.v[5] ? "yes" : "NO",
			hits, A->client->shinobi.palmsStart ? "NO" : "yes" );
		return ( at.f[2] < 8 && at.v[3] && at.v[4] == 1 && at.v[5] && hits == 2 && !A->client->shinobi.palmsStart
			&& at.v[1] - A->client->ps.fd.forcePower >= 40 ) ? ATR_PASS : ATR_FAIL;
	}
	return ATR_RUN;
}

// Reverse Lotus: B 110 ahead - the blink, up, the hang, the slam: damage and down
static void T_Lotus_Setup( void ) {
	vec3_t p;
	AT_Weapon( 0, WP_MELEE );
	AT_Weapon( 1, WP_MELEE );
	AT_Spot( 110, 0, p );
	AT_Place( B, p, at.baseYaw + 180 );
	at.v[0] = shinobiTest.lotusSlams;
	at.f[0] = B->client->ps.origin[2];
	at.f[1] = -99999;
	at.drive[0].aimAt = qtrue;
}
static atResult_t T_Lotus( int t ) {
	if ( STAGE( 0 ) && t >= 200 ) {
		at.v[1] = Shinobi_Cast( A, SJ_LOTUS, 1 );
		NEXT();
	}
	if ( STAGE( 1 ) ) {
		if ( B->client->ps.origin[2] - at.f[0] > at.f[1] )
			at.f[1] = B->client->ps.origin[2] - at.f[0];
		if ( shinobiTest.lotusSlams > at.v[0] || SINCE() >= 3000 ) {
			at.v[2] = B->client->ps.forceHandExtend == HANDEXTEND_KNOCKDOWN || BG_InKnockDown( B->client->ps.legsAnim );
			NEXT();
		}
	}
	if ( STAGE( 2 ) && SINCE() >= 300 ) {
		int hits;
		const int dmg = AT_DamageTo( B, at.start, NULL, MOD_MELEE, &hits, NULL );
		AT_Msg( "cast: %s, B carried up %.0f, slammed: %s, damage %i, down: %s", at.v[1] ? "yes" : "NO", at.f[1],
			shinobiTest.lotusSlams > at.v[0] ? "yes" : "NO", dmg, at.v[2] ? "yes" : "no" );
		return ( at.v[1] && at.f[1] > 60 && shinobiTest.lotusSlams > at.v[0] && dmg >= SHINOBI_LOTUS_DAMAGE * 7 / 10 && at.v[2] )
			? ATR_PASS : ATR_FAIL;
	}
	return ATR_RUN;
}

// Reverse Lotus, logged: B holds a blade and swings all along; every 100 ms where both are, how fast
// they fall, their health and pose, then every blow either of them took. A must take nothing.
static int at_lotusLast;
static void T_LotusLog_Setup( void ) {
	vec3_t p;
	AT_Weapon( 0, WP_MELEE );
	AT_Weapon( 1, WP_SABER );
	AT_Spot( 110, 0, p );
	AT_Place( B, p, at.baseYaw + 180 );
	at.drive[0].aimAt = at.drive[1].aimAt = qtrue;
	AT_Drive( 1, 0, 0, 0, BUTTON_ATTACK ); // B swings the whole time
	at.v[0] = shinobiTest.lotusSlams;
	at.v[1] = shinobiTest.lotusShielded;
	at.f[0] = B->client->ps.origin[2];
	at.f[1] = -99999;
	at_lotusLast = 0;
}
static void AT_LotusRow( int t ) {
	gclient_t *a = A->client, *b = B->client;
	AT_Print( "[lotus] %5i ms  phase %i | A z %+5.0f vz %+5.0f hp %3i %s %-16s | B z %+5.0f vz %+5.0f hp %3i %s %-16s held %s\n",
		t, a->shinobi.lotusPhase, a->ps.origin[2] - at.f[0], a->ps.velocity[2], A->health,
		a->ps.groundEntityNum == ENTITYNUM_NONE ? "air" : "gnd", AT_AnimName( a->ps.legsAnim ),
		b->ps.origin[2] - at.f[0], b->ps.velocity[2], B->health, b->ps.groundEntityNum == ENTITYNUM_NONE ? "air" : "gnd",
		AT_AnimName( b->ps.legsAnim ), b->shinobi.lotusBy ? "yes" : "no" );
}
static atResult_t T_LotusLog( int t ) {
	if ( STAGE( 0 ) && t >= 600 ) { // (B swinging already)
		at.v[2] = A->client->ps.fd.forcePower;
		at.v[3] = Shinobi_Cast( A, SJ_LOTUS, 1 );
		at.v[4] = at.v[2] - A->client->ps.fd.forcePower;
		at.v[5] = level.time;
		AT_Print( "[lotus] cast: %s, chakra %i -> %i (spent %i), B caught: %s; B's force powers on: 0x%x (protect lvl %i)\n", at.v[3] ? "yes" : "NO", at.v[2],
			A->client->ps.fd.forcePower, at.v[4], B->client->shinobi.lotusBy ? "yes" : "NO", B->client->ps.fd.forcePowersActive,
			B->client->ps.fd.forcePowerLevel[FP_PROTECT] );
		at.v[6] = 0; // (the slam's time)
		NEXT();
	}
	if ( STAGE( 1 ) ) {
		if ( B->client->ps.origin[2] - at.f[0] > at.f[1] )
			at.f[1] = B->client->ps.origin[2] - at.f[0];
		if ( !at.v[6] && shinobiTest.lotusSlams > at.v[0] ) {
			at.v[6] = level.time;
			AT_Print( "[lotus] slam at %i ms\n", level.time - at.v[5] );
		}
		if ( level.time - at_lotusLast >= 100 ) {
			at_lotusLast = level.time;
			AT_LotusRow( level.time - at.v[5] );
		}
		if ( SINCE() >= 3600 ) {
			AT_Drive( 1, 0, 0, 0, 0 );
			NEXT();
		}
	}
	if ( STAGE( 2 ) ) {
		int i, toA = 0, toB = 0, byA = 0;
		for ( i = 0; i < SHINOBI_TEST_DMGLOG; i++ ) {
			const shinobiTestDamage_t *d = &shinobiTest.dmg[i];
			if ( !d->time || d->time < at.v[5] - 600 || ( d->targ != at.a && d->targ != at.b ) )
				continue;
			AT_Print( "[lotus] blow at %5i ms: %s took %i from %s (%s, via %s)\n", d->time - at.v[5], d->targ == at.a ? "A" : "B",
				d->take, d->attacker == at.a ? "A" : d->attacker == at.b ? "B" : d->attacker == ENTITYNUM_WORLD ? "world" : va( "#%i", d->attacker ),
				d->mod >= 0 && d->mod < MOD_MAX ? modNames[d->mod] : "?", d->inflictor );
			if ( d->time < at.v[5] || ( at.v[6] && d->time > at.v[6] + 300 ) )
				continue; // (before the cast: B's swings at the start; after the slam: B up and fighting again)
			if ( d->targ == at.a )
				toA += d->take;
			else {
				toB += d->take;
				if ( d->attacker == at.a )
					byA += d->take;
			}
		}
		AT_Msg( "cast %s, spent %i chakra, B carried up %.0f, slammed: %s; from the cast to the slam A took %i, B took %i (%i from A); "
			"B's blows stopped by the lotus: %i", at.v[3] ? "yes" : "NO", at.v[4], at.f[1], shinobiTest.lotusSlams > at.v[0] ? "yes" : "NO",
			toA, toB, byA, shinobiTest.lotusShielded - at.v[1] );
		return ( at.v[3] && at.v[4] >= 70 && shinobiTest.lotusSlams > at.v[0] && toA == 0 && byA >= SHINOBI_LOTUS_DAMAGE * 7 / 10 )
			? ATR_PASS : ATR_FAIL;
	}
	return ATR_RUN;
}

// Dynamic Entry: B 260 ahead - A flies to him, he is hurt and knocked down
static void T_Entry_Setup( void ) {
	vec3_t p;
	AT_Weapon( 0, WP_MELEE );
	AT_Weapon( 1, WP_MELEE );
	AT_Spot( 260, 0, p );
	AT_Place( B, p, at.baseYaw + 180 );
	at.v[0] = shinobiTest.entryHits;
	at.drive[0].aimAt = qtrue;
}
static atResult_t T_Entry( int t ) {
	if ( STAGE( 0 ) && t >= 200 ) {
		VectorCopy( A->client->ps.origin, at.vec[0] );
		at.v[1] = Shinobi_Cast( A, SJ_ENTRY, 1 );
		NEXT();
	}
	if ( STAGE( 1 ) && ( shinobiTest.entryHits > at.v[0] || SINCE() >= SHINOBI_ENTRY_MS + 200 ) ) {
		at.v[2] = B->client->ps.forceHandExtend == HANDEXTEND_KNOCKDOWN || BG_InKnockDown( B->client->ps.legsAnim );
		at.f[0] = Distance( A->client->ps.origin, at.vec[0] );
		NEXT();
	}
	if ( STAGE( 2 ) && SINCE() >= 300 ) {
		int hits;
		const int dmg = AT_DamageTo( B, at.start, NULL, MOD_MELEE, &hits, NULL );
		AT_Msg( "cast: %s, A flew %.0f, the kick hit him %i times, melee hits on B %i (%i damage, a stock kick anim hit may add one), down: %s",
			at.v[1] ? "yes" : "NO", at.f[0], shinobiTest.entryHits - at.v[0], hits, dmg, at.v[2] ? "yes" : "no" );
		return ( at.v[1] && at.f[0] > 150 && shinobiTest.entryHits == at.v[0] + 1 && hits >= 1 && at.v[2] ) ? ATR_PASS : ATR_FAIL;
	}
	return ATR_RUN;
}

// Inner Gates: the weights drop (and shake the ground), speed and the drain at gate 1, the damage
// that readies the next, gate 2 heals and stops the drain, 3 gives Reverse Lotus, 4 free Shunshin, 5 +20%
static void T_Gates_Setup( void ) {
	AT_Weapon( 0, WP_MELEE );
	AT_Weapon( 1, WP_MELEE );
	A->client->pers.shinobiBuild.perk = SP_GATES;
	at.v[0] = shinobiTest.weightQuakes;
}
static atResult_t T_Gates( int t ) {
	gclient_t *c = A->client;
	if ( STAGE( 0 ) && t >= 300 ) {
		at.v[1] = ( A->s.frame & SHINOBI_PFRAME_WEIGHTS ) != 0; // weights on before
		A->health = c->ps.stats[STAT_HEALTH] = 30;
		Cmd_Perk_f( A );
		at.v[2] = c->shinobi.gate;
		at.v[3] = AT_CountClass( "shinobi_weight", at.a );
		NEXT();
	}
	if ( STAGE( 1 ) && SINCE() >= 1600 ) { // gate 1: fast, draining; the weights landed
		at.v[4] = ( c->ps.stats[STAT_SHINOBI] & SHINOBI_STAT_GATES ) != 0;
		at.v[5] = 30 - A->health; // drained
		at.v[6] = shinobiTest.weightQuakes - at.v[0];
		Cmd_Perk_f( A ); // not ready yet: nothing
		at.v[7] = c->shinobi.gate;
		c->shinobi.gateDealt = SHINOBI_GATE_NEED( c->shinobi.gate );
		NEXT();
	}
	if ( STAGE( 2 ) && SINCE() >= 100 ) { // gate 2: +70, no drain
		const int before = A->health;
		at.v[8] = ( c->ps.stats[STAT_SHINOBI_BLADECOMBO] & SHINOBI_BC_GATE_READY ) != 0;
		Cmd_Perk_f( A );
		at.v[9] = A->health - before;
		at.v[10] = A->health;
		NEXT();
	}
	if ( STAGE( 3 ) && SINCE() >= 1200 ) {
		at.v[11] = at.v[10] - A->health; // drain after gate 2 (want 0)
		c->shinobi.gateDealt = SHINOBI_GATE_NEED( c->shinobi.gate );
		Cmd_Perk_f( A ); // 3
		at.v[12] = Shinobi_HasJutsu( A, SJ_LOTUS );
		c->shinobi.gateDealt = SHINOBI_GATE_NEED( c->shinobi.gate );
		Cmd_Perk_f( A ); // 4
		at.f[1] = (float)Shinobi_BuildDamage( B, A, 100, MOD_MELEE ); // (his own taijutsu in it)
		c->shinobi.gateDealt = SHINOBI_GATE_NEED( c->shinobi.gate );
		Cmd_Perk_f( A ); // 5
		at.v[13] = c->shinobi.gate;
		at.v[14] = Shinobi_BuildDamage( B, A, 100, MOD_MELEE ); // taijutsu 0: 100 x 1.2
		at.v[15] = A->health;
		NEXT();
	}
	if ( STAGE( 4 ) && SINCE() >= SHINOBI_GATE_LATE_DRAIN_MS + 300 ) {
		at.f[0] = (float)( at.v[15] - A->health ); // the slow drain from the third gate on
		AT_Msg( "weights on: %s, gate %i after C (weights dropped %i, quakes %i), fast: %s, drained %i in 1.6 s; "
			"C before the damage: gate %i; ready: %s, gate 2 healed %i, drain after it %i; Reverse Lotus at 3: %s; last gate %i, a 100 blow -> %i (at gate 4: %.0f); "
			"drained at gate 5 in 1.8 s: %.0f",
			at.v[1] ? "yes" : "NO", at.v[2], at.v[3], at.v[6], at.v[4] ? "yes" : "NO", at.v[5], at.v[7], at.v[8] ? "yes" : "NO",
			at.v[9], at.v[11], at.v[12] ? "yes" : "NO", at.v[13], at.v[14], at.f[1], at.f[0] );
		return ( at.v[1] && at.v[2] == 1 && at.v[3] == 2 && at.v[6] >= 1 && at.v[4] && at.v[5] >= 2 && at.v[7] == 1 && at.v[8]
			&& at.v[9] >= SHINOBI_GATE2_HEAL - 1 && at.v[11] == 0 && at.v[12] && at.v[13] == 5 && fabs( at.v[14] - at.f[1] * SHINOBI_GATE5_TAIJUTSU ) <= 1 && at.f[0] >= 1 && at.f[0] <= 2 ) ? ATR_PASS : ATR_FAIL;
	}
	return ATR_RUN;
}

// Kaiten: a hit in the wind-up breaks it; spinning - no damage, missiles glance off, the close one is thrown
static void T_Kaiten_Setup( void ) {
	vec3_t p;
	AT_Weapon( 0, WP_MELEE );
	AT_Weapon( 1, WP_MELEE );
	AT_Spot( 220, 0, p );
	AT_Place( B, p, at.baseYaw + 180 );
	at.drive[1].aimAt = qtrue;
}
static void AT_KaitenCast( void ) {
	if ( Shinobi_Cast( A, SJ_KAITEN, 1 ) )
		A->client->shinobi.chargeJutsu = SJ_KAITEN;
}
static atResult_t T_Kaiten( int t ) {
	if ( STAGE( 0 ) && t >= 200 ) {
		AT_KaitenCast();
		NEXT();
	}
	if ( STAGE( 1 ) && SINCE() >= 200 ) { // winding up: a blow breaks it
		G_Damage( A, B, B, NULL, A->r.currentOrigin, 5, 0, MOD_MELEE );
		at.v[0] = !A->client->shinobi.kaitenStart;
		A->client->shinobi.ccImmuneEnd = 0;
		NEXT();
	}
	if ( STAGE( 2 ) && SINCE() >= 300 ) {
		AT_KaitenCast();
		NEXT();
	}
	if ( STAGE( 3 ) && SINCE() >= SHINOBI_KAITEN_WINDUP_MS + 200 ) { // spinning
		const int h = A->health;
		at.v[1] = Shinobi_KaitenSpinning( A );
		G_Damage( A, B, B, NULL, A->r.currentOrigin, 30, 0, MOD_SABER );
		at.v[2] = A->health == h;
		at.v[3] = shinobiTest.kaitenDeflects;
		at.v[6] = A->health;
		{ // a kunai at him: B looks straight at his middle and throws
			vec3_t d, ang, eye;
			VectorCopy( B->client->ps.origin, eye );
			eye[2] += B->client->ps.viewheight;
			VectorSubtract( A->client->ps.origin, eye, d );
			vectoangles( d, ang );
			SetClientViewAngle( B, ang );
			B->client->shinobi.kunaiNext = 0;
			B->client->pers.cmd.buttons &= ~BUTTON_WALKING;
			Cmd_Kunai_f( B );
			at.v[8] = AT_CountClass( "shinobi_kunai", at.b );
		}
		NEXT();
	}
	if ( STAGE( 4 ) && SINCE() >= 700 ) {
		vec3_t p, d;
		at.v[4] = shinobiTest.kaitenDeflects - at.v[3];
		at.v[7] = at.v[6] - A->health;
		AT_Spot( 50, 0, p );
		AT_Place( B, p, at.baseYaw + 180 ); // too close
		NEXT();
		VectorSubtract( B->client->ps.origin, A->client->ps.origin, d );
		at.f[0] = VectorLength( d );
	}
	if ( STAGE( 5 ) && SINCE() >= 600 ) {
		vec3_t d;
		VectorSubtract( B->client->ps.origin, A->client->ps.origin, d );
		d[2] = 0;
		at.f[1] = VectorLength( d );
		at.v[5] = B->client->ps.forceHandExtend == HANDEXTEND_KNOCKDOWN;
		Cmd_JutsuRelease_f( A );
		NEXT();
	}
	if ( STAGE( 6 ) && SINCE() >= 200 ) {
		const qboolean off = !A->client->shinobi.kaitenStart && !( A->client->ps.stats[STAT_SHINOBI2] & SHINOBI_STAT2_KAITEN )
			&& !( A->client->ps.eFlags & SHINOBI_EF_KAITEN );
		AT_Msg( "wind-up broken by a blow: %s; spinning: %s; blade blow blocked: %s; kunai thrown %i, glanced off: %i (A lost %i); "
			"B at 50 u thrown to %.0f u, knocked down: %s; let go - all off: %s",
			at.v[0] ? "yes" : "NO", at.v[1] ? "yes" : "NO", at.v[2] ? "yes" : "NO", at.v[8], at.v[4], at.v[7], at.f[1],
			at.v[5] ? "yes" : "no", off ? "yes" : "NO" );
		return ( at.v[0] && at.v[1] && at.v[2] && at.v[4] >= 1 && !at.v[7] && at.f[1] > SHINOBI_KAITEN_RADIUS && off ) ? ATR_PASS : ATR_FAIL;
	}
	return ATR_RUN;
}

// marks: three at most, a fourth replaces the oldest
static void AT_ThrowMark( gentity_t *e, float pitch, float yaw ) {
	vec3_t ang;
	VectorSet( ang, pitch, yaw, 0 );
	SetClientViewAngle( e, ang );
	at.drive[e->s.number == at.a ? 0 : 1].aimAt = qfalse;
	VectorCopy( ang, at.drive[e->s.number == at.a ? 0 : 1].angles );
	e->client->shinobi.kunaiNext = 0;
	e->client->pers.cmd.buttons |= BUTTON_WALKING;
	e->client->ps.fd.forcePower = e->client->ps.fd.forcePowerMax;
	Cmd_Kunai_f( e );
	e->client->pers.cmd.buttons &= ~BUTTON_WALKING;
}
static void T_Marks_Setup( void ) {
	AT_Weapon( 0, WP_MELEE );
}
static atResult_t T_Marks( int t ) {
	if ( t >= 200 && at.stage < 4 && SINCE() >= 250 ) {
		AT_ThrowMark( A, 70, at.baseYaw + at.stage * 40 );
		NEXT();
	}
	if ( STAGE( 4 ) && SINCE() >= 600 ) {
		const int n = AT_CountClass( "shinobi_kunai", at.a );
		AT_Msg( "4 marked kunai thrown, %i stay (want %i)", n, SHINOBI_MARK_MAX );
		return n == SHINOBI_MARK_MAX ? ATR_PASS : ATR_FAIL;
	}
	return ATR_RUN;
}

// Lightning Trap: one mark fizzles; two make a line that stuns whoever crosses it - its maker too
static void T_RaiNet_Setup( void ) {
	vec3_t p;
	AT_Weapon( 0, WP_MELEE );
	AT_Weapon( 1, WP_MELEE );
	AT_Spot( 400, 120, p );
	AT_Place( B, p, at.baseYaw + 180 ); // out of the way for now
}
static atResult_t T_RaiNet( int t ) {
	if ( STAGE( 0 ) && t >= 200 ) {
		AT_ThrowMark( A, 60, at.baseYaw - 30 );
		NEXT();
	}
	if ( STAGE( 1 ) && SINCE() >= 400 ) { // one mark: nothing
		Shinobi_Cast( A, SJ_RAITRAP, 1 );
		at.v[0] = AT_CountClass( "shinobi_rainet", at.a );
		AT_ThrowMark( A, 60, at.baseYaw + 30 );
		NEXT();
	}
	if ( STAGE( 2 ) && SINCE() >= 400 ) {
		vec3_t p;
		AT_Spot( -160, 0, p );
		AT_Place( A, p, at.baseYaw ); // step back from the line
		NEXT();
	}
	if ( STAGE( 3 ) && SINCE() >= 150 ) {
		Shinobi_Cast( A, SJ_RAITRAP, 1 );
		NEXT();
	}
	if ( STAGE( 4 ) && SINCE() >= 250 ) {
		gentity_t *net = AT_FindClass( "shinobi_rainet", at.a );
		vec3_t     mid;
		at.v[1] = net != NULL;
		at.v[2] = net ? ( net->s.frame >> 2 ) & 1 : 0; // the side lit
		if ( !net ) {
			AT_Msg( "one mark made a net: %s; two marks made no net", at.v[0] ? "YES" : "no" );
			return ATR_FAIL;
		}
		VectorAdd( net->r.currentOrigin, net->s.origin2, mid );
		VectorScale( mid, 0.5f, mid );
		VectorCopy( mid, at.f );
		mid[2] += 30;
		at.v[3] = B->health;
		AT_Place( B, mid, at.baseYaw + 90 ); // right on the line
		NEXT();
	}
	if ( STAGE( 5 ) && SINCE() >= 400 ) {
		vec3_t p, mid;
		at.v[4] = B->client->shinobi.stunEnd > level.time;
		at.v[5] = at.v[3] - B->health;
		{
			gentity_t *net = AT_FindClass( "shinobi_rainet", at.a );
			at.f[5] = net ? Distance( B->r.currentOrigin, at.f ) : -1;
			at.v[9] = B->takedamage;
			at.v[10] = B->client->invulnerableTimer > level.time;
		}
		AT_Spot( 400, 120, p );
		AT_Place( B, p, at.baseYaw + 180 );
		VectorCopy( at.f, mid );
		mid[2] += 30;
		at.v[6] = A->health;
		AT_Place( A, mid, at.baseYaw ); // and its maker
		NEXT();
	}
	if ( STAGE( 6 ) && SINCE() >= 400 ) {
		const qboolean selfStun = A->client->shinobi.stunEnd > level.time, selfHurt = A->health < at.v[6];
		AT_Msg( "one mark: net %s; two marks: net %s, side lit %s; B on the line (%.0f u off its middle, takedamage %i, spawn shield %i): stunned %s, lost %i; "
			"its maker on it: stunned %s, hurt %s",
			at.v[0] ? "YES (wrong)" : "none", at.v[1] ? "yes" : "NO", at.v[2] ? "yes" : "NO", at.f[5], at.v[9], at.v[10], at.v[4] ? "yes" : "NO", at.v[5],
			selfStun ? "yes" : "NO", selfHurt ? "yes" : "NO" );
		return ( !at.v[0] && at.v[1] && at.v[2] && at.v[4] && at.v[5] > 0 && selfStun && selfHurt ) ? ATR_PASS : ATR_FAIL;
	}
	return ATR_RUN;
}

// bots with their own defence on: Kawarimi out of a combo, guard / parry
// Copy Sharingan: copies what it sees, not Yin / Yang, not behind its back, not with the eye shut;
// a copied cast costs 25% more
static void T_CopyEye_Setup( void ) {
	gclient_t *c = A->client;
	c->pers.shinobiBuild.perk = SP_COPYEYE;
	c->pers.shinobiBuild.numJutsu = 0; // (nothing of its own to skip)
	c->shinobi.eyes = SP_COPYEYE;
	c->shinobi.eyesDrain = level.time + 100000;
	at.drive[0].aimAt = qtrue;
	at.drive[1].aimAt = qtrue;
}
static atResult_t T_CopyEye( int t ) {
	gclient_t *c = A->client;
	c->shinobi.eyesDrain = level.time + 100000; // (no drain in the way of the price check)
	if ( STAGE( 0 ) && t >= 300 ) {
		B->client->ps.fd.forcePower = B->client->ps.fd.forcePowerMax;
		Shinobi_Cast( B, SJ_SUITON, 1 );
		at.v[0] = c->shinobi.copyJutsu == SJ_SUITON;
		NEXT();
	}
	if ( STAGE( 1 ) && SINCE() >= 400 ) { // Yin / Yang: no
		Shinobi_Cast( B, SJ_KAITEN, 1 );
		at.v[1] = c->shinobi.copyJutsu == SJ_SUITON;
		Shinobi_KaitenStop( B );
		NEXT();
	}
	if ( STAGE( 2 ) && SINCE() >= 1200 ) { // looking away: no
		at.drive[0].aimAt = qfalse;
		at.drive[0].angles[YAW] = at.baseYaw + 180;
		NEXT();
	}
	if ( STAGE( 3 ) && SINCE() >= 300 ) {
		B->client->ps.fd.forcePower = B->client->ps.fd.forcePowerMax;
		B->client->shinobi.armorNext = 0;
		Shinobi_Cast( B, SJ_ARMOR, 1 );
		at.v[2] = c->shinobi.copyJutsu == SJ_SUITON;
		B->client->shinobi.armorEnd = 0;
		at.drive[0].aimAt = qtrue;
		NEXT();
	}
	if ( STAGE( 4 ) && SINCE() >= 1700 ) { // facing it again: Stone Skin replaces the copy
		B->client->ps.fd.forcePower = B->client->ps.fd.forcePowerMax;
		B->client->shinobi.armorNext = 0;
		Shinobi_Cast( B, SJ_ARMOR, 1 );
		at.v[3] = c->shinobi.copyJutsu == SJ_ARMOR;
		B->client->shinobi.armorEnd = 0;
		NEXT();
	}
	if ( STAGE( 5 ) && SINCE() >= 200 ) { // cast from the copy slot: the dearer price
		int before;
		c->ps.fd.forcePower = c->ps.fd.forcePowerMax;
		before = c->ps.fd.forcePower;
		c->shinobi.armorNext = 0;
		at.v[4] = Shinobi_Cast( A, SJ_ARMOR, 1 );
		at.v[5] = before - c->ps.fd.forcePower;
		c->shinobi.armorEnd = 0;
		NEXT();
	}
	if ( STAGE( 6 ) && SINCE() >= 1700 ) { // the eye shut: the copy is kept
		c->shinobi.eyes = 0;
		B->client->ps.fd.forcePower = B->client->ps.fd.forcePowerMax;
		Shinobi_Cast( B, SJ_SUITON, 1 );
		at.v[6] = c->shinobi.copyJutsu == SJ_ARMOR;
		AT_Msg( "copied Water Bullets %s, Kaiten ignored %s, behind its back ignored %s, Stone Skin replaced it %s, "
			"cast from the slot %s for %i chakra (want %i), eye shut keeps it %s",
			at.v[0] ? "yes" : "NO", at.v[1] ? "yes" : "NO", at.v[2] ? "yes" : "NO", at.v[3] ? "yes" : "NO",
			at.v[4] ? "ok" : "FAILED", at.v[5], Shinobi_CopyCost( SJ_ARMOR, 1 ), at.v[6] ? "yes" : "NO" );
		return ( at.v[0] && at.v[1] && at.v[2] && at.v[3] && at.v[4] && at.v[5] == Shinobi_CopyCost( SJ_ARMOR, 1 ) && at.v[6] )
			? ATR_PASS : ATR_FAIL;
	}
	return ATR_RUN;
}

static void T_BotDefend_Setup( void ) {
	vec3_t p;
	AT_Weapon( 0, WP_MELEE );
	AT_Weapon( 1, WP_MELEE );
	AT_Spot( 48, 0, p );
	AT_Place( B, p, at.baseYaw + 180 );
	at.drive[0].aimAt = qtrue;
	at.drive[1].aimAt = qtrue;
	at.v[0] = shinobiTest.kawarimiHides;
	at.v[1] = shinobiTest.botGuards;
	at.v[2] = shinobiTest.parries;
	AT_Drive( 0, 0, 0, 0, BUTTON_ATTACK );
}
static atResult_t T_BotDefend( int t ) {
	if ( t >= 6000 ) {
		const int kawa = shinobiTest.kawarimiHides - at.v[0], guards = shinobiTest.botGuards - at.v[1], parries = shinobiTest.parries - at.v[2];
		AT_Drive( 0, 0, 0, 0, 0 );
		AT_Msg( "6 s of combos on a defending bot: Kawarimi %i, guard raised %i, parries %i (shinobi_botguard %i)",
			kawa, guards, parries, shinobi_botguard.integer );
		return ( kawa > 0 || guards > 0 ) ? ATR_PASS : ATR_FAIL;
	}
	if ( ( t / 1500 ) % 2 == 1 ) // pauses: let it recover and try again
		AT_Drive( 0, 0, 0, 0, 0 );
	else
		AT_Drive( 0, 0, 0, 0, BUTTON_ATTACK );
	return ATR_RUN;
}

// a bot on a bare-handed life (its own AI) comes and fights with the combo
static void T_BotFists_Setup( void ) {
	vec3_t p;
	AT_Weapon( 0, WP_SABER );
	AT_Spot( 160, 0, p );
	AT_Place( B, p, at.baseYaw + 180 );
	B->client->shinobi.botFists = qtrue;
	B->client->shinobi.botFinisher = -1;
	B->client->pers.shinobiFistStyle = SFS_FAST;
	at.v[0] = shinobiTest.launches;
	at.v[1] = shinobiTest.airHits;
	at.v[2] = shinobiTest.slams;
}
static atResult_t T_BotFists( int t ) {
	A->health = A->client->ps.stats[STAT_HEALTH] = A->client->ps.stats[STAT_MAX_HEALTH]; // (a punching bag that lasts)
	if ( B->client->ps.weapon == WP_MELEE )
		at.v[3]++;
	at.v[4]++;
	if ( t >= 12000 ) {
		int hits = 0, kicks = 0, i;
		for ( i = 0; i < SHINOBI_TEST_DMGLOG; i++ ) {
			const shinobiTestDamage_t *d = &shinobiTest.dmg[i];
			if ( d->time >= at.start && d->targ == at.a && d->attacker == at.b && d->mod == MOD_MELEE )
				hits++;
		}
		kicks = shinobiTest.launches - at.v[0];
		AT_Msg( "12 s with its own AI: fists in hand %i%% of the time, %i blows landed, launchers %i, air blows %i, slams %i",
			at.v[4] ? at.v[3] * 100 / at.v[4] : 0, hits, kicks, shinobiTest.airHits - at.v[1], shinobiTest.slams - at.v[2] );
		return ( at.v[3] * 10 >= at.v[4] * 8 && hits >= 4 ) ? ATR_PASS : ATR_FAIL;
	}
	return ATR_RUN;
}

/*
Bot tactics (Shinobi_BotTactics; B's own AI, shinobi_botsmart): out of a Water Prison with
Kawarimi, its own prison held (and the gasping victim hit), seals broken close by and from
afar, fireballs stepped away from, a spinning Kaiten kept off.
*/
static qboolean AT_SmartOff( void ) {
	if ( shinobi_botsmart.integer > 0 )
		return qfalse;
	AT_Msg( "shinobi_botsmart is 0: bots use no tactics" );
	return qtrue;
}

// A holds B in a Water Prison four times: B must Kawarimi out (at least twice, quickly)
static void AT_EscapeTry( void ) {
	vec3_t p;
	AT_Reset( B );
	AT_Reset( A );
	AT_Spot( 100, 0, p );
	AT_Place( B, p, at.baseYaw + 180 );
	A->client->shinobi.prisonNext = 0;
	at.v[4] = level.time;
	at.v[5] = 0;
}
static void T_BotEscape_Setup( void ) {
	at.drive[0].aimAt = qtrue;
	at.v[0] = shinobiTest.botEscapes;
	at.v[1] = at.v[2] = at.v[3] = 0;
	AT_EscapeTry();
}
static atResult_t T_BotEscape( int t ) {
	if ( AT_SmartOff() )
		return ATR_FAIL;
	if ( !at.v[5] && level.time - at.v[4] >= 300 ) { // in the water
		if ( Shinobi_Cast( A, SJ_SUIRO, 1 ) && A->client->shinobi.prisonTarget == at.b + 1 ) {
			A->client->shinobi.chargeJutsu = SJ_SUIRO;
			at.v[5] = level.time;
			at.v[1]++;
		}
		else {
			at.v[4] = level.time; // try again
		}
	}
	if ( at.v[5] > 0 && !B->client->shinobi.prisonCaster ) { // out
		const int ms = level.time - at.v[5];
		if ( ms < 2500 ) {
			at.v[2]++;
			at.v[3] += ms;
		}
		at.v[5] = -1;
		at.v[6] = level.time;
	}
	if ( ( at.v[5] > 0 && level.time - at.v[5] > 3000 ) || ( at.v[5] < 0 && level.time - at.v[6] > 600 ) ) {
		if ( A->client->shinobi.prisonTarget )
			Shinobi_PrisonRelease( A );
		if ( at.v[1] >= 4 ) {
			AT_Msg( "Water Prison x%i: Kawarimi out %i times (%i in all), %i ms on average (shinobi_botsmart %i)",
				at.v[1], at.v[2], shinobiTest.botEscapes - at.v[0], at.v[2] ? at.v[3] / at.v[2] : 0, shinobi_botsmart.integer );
			return at.v[2] >= 2 ? ATR_PASS : ATR_FAIL;
		}
		AT_EscapeTry();
	}
	return ATR_RUN;
}

// B catches A in its own Water Prison: held for seconds (its AI's blows don't burst it), then hits him
static void T_BotPrison_Setup( void ) {
	vec3_t p;
	AT_Spot( 100, 0, p );
	AT_Place( B, p, at.baseYaw + 180 );
	at.v[0] = at.v[1] = at.v[2] = 0;
}
static atResult_t T_BotPrison( int t ) {
	if ( AT_SmartOff() )
		return ATR_FAIL;
	A->client->shinobi.kawarimiReady[0] = A->client->shinobi.kawarimiReady[1] = A->client->shinobi.kawarimiReady[2] = level.time + 60000;
	if ( STAGE( 0 ) && t >= 300 ) {
		vec3_t d, ang;
		VectorSubtract( A->client->ps.origin, B->client->ps.origin, d );
		vectoangles( d, ang );
		ang[PITCH] = 0;
		SetClientViewAngle( B, ang );
		B->client->shinobi.prisonNext = 0;
		if ( Shinobi_Cast( B, SJ_SUIRO, 1 ) && B->client->shinobi.prisonTarget == at.a + 1 ) {
			B->client->shinobi.chargeJutsu = SJ_SUIRO;
			B->client->shinobi.botHoldEnd = level.time + 1500; // (what the force mapping would give it)
			at.v[0] = level.time;
			NEXT();
		}
		else if ( t > 2000 ) {
			AT_Msg( "B could not cast the Water Prison on A" );
			return ATR_FAIL;
		}
	}
	if ( STAGE( 1 ) && !B->client->shinobi.prisonTarget ) {
		at.v[1] = level.time - at.v[0];
		NEXT();
	}
	if ( STAGE( 1 ) && SINCE() > 9000 ) {
		AT_Msg( "B held the prison over 9 s" );
		return ATR_FAIL;
	}
	if ( STAGE( 2 ) && SINCE() >= 1500 ) {
		int hits = 0, k;
		for ( k = 0; k < SHINOBI_TEST_DMGLOG; k++ ) {
			const shinobiTestDamage_t *d = &shinobiTest.dmg[k];
			if ( d->time >= at.stageTime && d->targ == at.a && d->attacker == at.b && d->mod != MOD_WATER )
				hits++;
		}
		AT_Msg( "B held its Water Prison %i ms (its AI attacking around it), then %i blows on A in 1.5 s", at.v[1], hits );
		return at.v[1] >= 3000 ? ATR_PASS : ATR_FAIL;
	}
	return ATR_RUN;
}

// A forms the seals of a Katon close (260) and far (560): B must break them both times
static void AT_InterruptTry( float dist ) {
	vec3_t p;
	AT_Reset( A );
	AT_Reset( B );
	AT_Spot( dist, 0, p );
	AT_Place( B, p, at.baseYaw + 180 );
	A->client->shinobi.chargeJutsu = SJ_KATON;
	A->client->shinobi.chargeStart = level.time;
	at.v[4] = level.time;
}
static void T_BotInterrupt_Setup( void ) {
	at.drive[0].aimAt = qtrue;
	at.v[0] = at.v[1] = 0;
	at.v[2] = shinobiTest.botInterrupts;
	at.v[3] = shinobiTest.botKunaiInterrupts;
	AT_InterruptTry( 260 );
}
static atResult_t T_BotInterrupt( int t ) {
	if ( AT_SmartOff() )
		return ATR_FAIL;
	if ( STAGE( 0 ) || STAGE( 1 ) ) {
		const qboolean broken = A->client->shinobi.chargeJutsu == SJ_NONE;
		if ( broken || level.time - at.v[4] > 4000 ) {
			at.v[at.stage] = broken ? level.time - at.v[4] : -1;
			if ( STAGE( 0 ) )
				AT_InterruptTry( 560 );
			NEXT();
		}
	}
	if ( STAGE( 2 ) ) {
		A->client->shinobi.chargeJutsu = SJ_NONE;
		AT_Msg( "seals broken at 260 u after %i ms, at 560 u after %i ms (-1: not); runs / Entries %i, kunai %i",
			at.v[0], at.v[1], shinobiTest.botInterrupts - at.v[2], shinobiTest.botKunaiInterrupts - at.v[3] );
		return ( at.v[0] > 0 && at.v[1] > 0 ) ? ATR_PASS : ATR_FAIL;
	}
	return ATR_RUN;
}

// A throws four great fireballs at B from 480: B steps / jumps aside (or Kawarimis) - few direct hits
static void T_BotDodge_Setup( void ) {
	vec3_t p;
	AT_Spot( 480, 0, p );
	AT_Place( B, p, at.baseYaw + 180 );
	at.drive[0].aimAt = qtrue;
	at.v[0] = shinobiTest.botDodges;
	at.v[1] = 0;
	at.v[2] = level.time;
}
static atResult_t T_BotDodge( int t ) {
	if ( AT_SmartOff() )
		return ATR_FAIL;
	B->health = B->client->ps.stats[STAT_HEALTH] = 500;
	if ( at.v[1] < 4 && t >= 600 + at.v[1] * 1800 ) {
		A->client->shinobi.fireballNext = 0;
		A->client->ps.fd.forcePower = A->client->ps.fd.forcePowerMax;
		if ( Shinobi_Cast( A, SJ_KATON, SHINOBI_MAX_LEVEL ) ) // (only the full charge casts)
			at.v[1]++;
	}
	if ( t >= 600 + 4 * 1800 + 500 ) {
		int direct = 0, splash = 0;
		AT_DamageTo( B, at.v[2], NULL, MOD_ROCKET, &direct, NULL );
		AT_DamageTo( B, at.v[2], NULL, MOD_ROCKET_SPLASH, &splash, NULL );
		AT_Msg( "%i fireballs: %i direct hits, %i blasts, dodges %i", at.v[1], direct, splash, shinobiTest.botDodges - at.v[0] );
		return ( at.v[1] >= 4 && direct <= 1 && shinobiTest.botDodges - at.v[0] >= 2 ) ? ATR_PASS : ATR_FAIL;
	}
	return ATR_RUN;
}

// A spins a Kaiten 220 from B (outside its throw): B keeps off it instead of walking into it
static void T_BotKeepOff_Setup( void ) {
	vec3_t p;
	AT_Spot( 220, 0, p );
	AT_Place( B, p, at.baseYaw + 180 );
	at.v[0] = shinobiTest.botKeptOff;
	at.v[1] = shinobiTest.kaitenBlocks;
}
static atResult_t T_BotKeepOff( int t ) {
	if ( AT_SmartOff() )
		return ATR_FAIL;
	A->client->ps.fd.forcePower = A->client->ps.fd.forcePowerMax;
	if ( STAGE( 0 ) && t >= 200 ) {
		AT_KaitenCast();
		NEXT();
	}
	if ( STAGE( 1 ) && SINCE() >= 2500 ) {
		const float d = Distance( A->client->ps.origin, B->client->ps.origin );
		if ( A->client->shinobi.kaitenStart )
			Shinobi_KaitenStop( A );
		A->client->shinobi.chargeJutsu = SJ_NONE;
		AT_Msg( "2.5 s next to a spinning Kaiten: kept off %i times, %.0f u away at the end, blows blocked by it %i",
			shinobiTest.botKeptOff - at.v[0], d, shinobiTest.kaitenBlocks - at.v[1] );
		return ( d > SHINOBI_KAITEN_RADIUS + 20 && shinobiTest.kaitenBlocks == at.v[1] ) ? ATR_PASS : ATR_FAIL;
	}
	return ATR_RUN;
}

// a map without bot routes (as all of ours): B hunts A down instead of hopping in place
extern int gWPNum;
static void T_BotNav_Setup( void ) {
	vec3_t p;
	AT_Spot( 480, 0, p );
	AT_Place( B, p, at.baseYaw ); // facing away
	at.v[0] = gWPNum; // the routes of this map, back after the test
	at.v[1] = at.v[2] = 0;
	at.v[3] = -1;
	gWPNum = 0;
}
static atResult_t T_BotNav( int t ) {
	const float d = Distance( A->client->ps.origin, B->client->ps.origin );
	A->client->ps.fd.forcePower = A->client->ps.fd.forcePowerMax;
	at.v[1]++;
	if ( B->client->ps.groundEntityNum == ENTITYNUM_NONE )
		at.v[2]++;
	if ( d < 150 && at.v[3] < 0 )
		at.v[3] = t;
	if ( t >= 5000 ) {
		gWPNum = at.v[0];
		AT_Msg( "no routes: B (480 u off, facing away) reached A after %i ms (-1: never), in the air %i%% of the time",
			at.v[3], at.v[1] ? at.v[2] * 100 / at.v[1] : 0 );
		return ( at.v[3] > 0 && at.v[3] < 4000 && at.v[2] * 100 < at.v[1] * 45 ) ? ATR_PASS : ATR_FAIL;
	}
	return ATR_RUN;
}

/*
blade_reach: A cuts at a still B with single clicks (a press every 0.75 s) from 30, 48 and 66 u
(centre to centre), five cuts the blade alone (shinobi_bladereach 0), five with the reach -
how many cuts landed each way.
*/
#define BR_CUTS   5
#define BR_MS     ( BR_CUTS * 750 + 300 )
static const float brDist[3] = { 30, 48, 66 };
static void BR_Place( int k ) {
	vec3_t p;
	AT_Spot( brDist[k / 2], 0, p );
	AT_Place( B, p, at.baseYaw + 180 );
	B->client->shinobi.flinchEnd = 0;
	shinobi_bladereach.integer = k % 2;
	at.v[8] = level.time;
	at.v[9] = A->client->shinobi.bladeSwing;
}
static void T_BladeReach_Setup( void ) {
	G_SetSaber( A, 0, "shinobi_katana", qfalse );
	G_SetSaber( A, 1, "none", qfalse );
	G_SaberModelSetup( A );
	AT_Weapon( 0, WP_SABER );
	A->client->ps.saberHolstered = 0;
	Shinobi_BladeStance( A );
	at.drive[0].aimAt = qtrue;
	at.v[10] = shinobi_bladereach.integer;
	at.v[11] = 0;
	BR_Place( 0 );
}
static atResult_t T_BladeReach( int t ) {
	const int k = at.v[11], since = level.time - at.v[8];
	if ( B->health < 200 )
		B->health = B->client->ps.stats[STAT_HEALTH] = 500;
	B->client->ps.eFlags &= ~EF_INVULNERABLE;
	AT_Drive( 0, 0, 0, 0, ( since > 150 && since < BR_CUTS * 750 && since % 750 < 80 ) ? BUTTON_ATTACK : 0 );
	if ( since >= BR_MS ) {
		int hits = 0, j;
		for ( j = 0; j < SHINOBI_TEST_DMGLOG; j++ ) {
			const shinobiTestDamage_t *d = &shinobiTest.dmg[j];
			if ( d->time >= at.v[8] && d->targ == at.b && d->attacker == at.a && d->mod == MOD_SABER )
				hits++;
		}
		at.v[k] = hits * 100 + ( A->client->shinobi.bladeSwing - at.v[9] );
		AT_Print( "[autotest]     %2.0f u, %s: %i of %i cuts landed\n", brDist[k / 2], k % 2 ? "with the reach" : "the blade alone",
			hits, A->client->shinobi.bladeSwing - at.v[9] );
		if ( ++at.v[11] >= 6 ) {
			int ok = 1;
			shinobi_bladereach.integer = at.v[10];
			for ( j = 1; j < 6; j += 2 )
				if ( at.v[j] / 100 * 10 < ( at.v[j] % 100 ) * 8 )
					ok = 0; // with the reach: 80 % of the cuts or better at every distance
			AT_Msg( "cuts landed, blade alone / with reach: 30u %i/%i of %i, 48u %i/%i of %i, 66u %i/%i of %i",
				at.v[0] / 100, at.v[1] / 100, at.v[1] % 100, at.v[2] / 100, at.v[3] / 100, at.v[3] % 100,
				at.v[4] / 100, at.v[5] / 100, at.v[5] % 100 );
			return ok ? ATR_PASS : ATR_FAIL;
		}
		BR_Place( at.v[11] );
	}
	return ATR_RUN;
}

static atTest_t atTests[] = {
	{ "guard_swing",  "blade swings on a blade guard cost by style and break it", T_GuardSwing_Setup, T_GuardSwing, 8000 },
	{ "guard_break",  "the emptying swing breaks the guard: 1 s stagger",        T_GuardBreak_Setup, T_GuardBreak, 1000 },
	{ "parry",        "perfect parry: free, attacker staggered long",              T_Parry_Setup, T_Parry, 1000 },
	{ "fuma",         "fuma shuriken: speed, two equal hits, leaves the hand, caught", T_Fuma_Setup, T_Fuma, 7000 },
	{ "kawa_combo",   "Kawarimi with attack held mid-combo hides",                 T_KawaCombo_Setup, T_KawaCombo, 3000 },
	{ "kawa_floor",   "Kawarimi from the floor and out of flinch / juggle",       T_KawaFloor_Setup, T_KawaFloor, 3000 },
	{ "interrupt",    "a blow breaks a charge, falling / burning don't",          T_Interrupt_Setup, T_Interrupt, 1000 },
	{ "getup",        "knockdown: auto getup time, the roll with a direction",    T_Getup_Setup, T_Getup, 10000 },
	{ "chakra_fight", "fists earn chakra and show +N chakra",                     T_Chakra_Setup, T_Chakra, 4000 },
	{ "regen",        "regen: normal below 30%, slower above",                    T_Regen_Setup, T_Regen, 7000 },
	{ "flinch",       "a combo holds its target flinching in place",              T_Flinch_Setup, T_Flinch, 5000 },
	{ "air_combo",    "launcher, jump after, two air blows, slam",                T_AirCombo_Setup, T_AirCombo, 9000 },
	{ "build",        "special costs, caps 7, old builds trimmed, bot builds legal", NULL, T_Build, 500 },
	{ "kaiten",       "Kaiten: wind-up breaks, spin blocks, kunai glance, throw, release", T_Kaiten_Setup, T_Kaiten, 5000 },
	{ "marks",        "three marked kunai at most",                               T_Marks_Setup, T_Marks, 3000 },
	{ "rainet",       "Lightning Trap: one mark fizzles, two make a stunning line, self too", T_RaiNet_Setup, T_RaiNet, 5000 },
	{ "gentle_fist",  "Byakugan fists burn more chakra than the hits give back",   T_Gentle_Setup, T_Gentle, 4000 },
	{ "wind_edge",    "Wind Edge: a swing's air slash cuts someone 120 units away", T_WindEdge_Setup, T_WindEdge, 3000 },
	{ "saber_dps",    "every blade and stance on a still target: damage, hits, swings, dmg/s", T_SaberDps_Setup, T_SaberDps, ( SDPS_MS + SDPS_GAP ) * SDPS_COUNT + SDPS_FIRST + 3000 },
	{ "blade_combo",  "blade finishers: each weapon's own (forward) and back sweep / side spin / rising cut (nothing held)", T_BladeCombo_Setup, T_BladeCombo, BC_COUNT * ( BC_MS + 1500 ) },
	{ "palms",        "64 Seals: rooted in the pose, full: the palm strikes and throws; held, the field strikes again", T_Palms_Setup, T_Palms, 5500 },
	{ "lotus",        "Reverse Lotus: blink, carried up, slammed down (damage, knockdown)", T_Lotus_Setup, T_Lotus, 5000 },
	{ "lotus_log",    "Reverse Lotus, logged step by step, B swinging a blade: A must take nothing", T_LotusLog_Setup, T_LotusLog, 6000 },
	{ "entry",        "Dynamic Entry: flies to him, hurts and knocks him down",   T_Entry_Setup, T_Entry, 3000 },
	{ "gates",        "Inner Gates: weights, speed and drain, damage readies the next, heal, Lotus, +20%", T_Gates_Setup, T_Gates, 6000 },
	{ "smoke_bomb",   "poison smoke bomb: bursts, poisons, lingers; shuriken pickup 3; pool items registered", T_Smoke_Setup, T_Smoke, 9000 },
	{ "sword_air",    "blade: rising cut, jump (Shunshin up), attack = the plunge that slams him", T_SwordAir_Setup, T_SwordAir, 12000 },
	{ "blade_clicks", "blade combo by clicks and held: blows counted, the finisher waits for a new swing", T_BladeClicks_Setup, T_BladeClicks, 19000 },
	{ "copyeye",      "Copy Sharingan: copies the cast it sees (not Yin/Yang, not behind), dearer from the slot", T_CopyEye_Setup, T_CopyEye, 9000 },
	{ "bot_defend",   "a defending bot guards / Kawarimis out of combos",         T_BotDefend_Setup, T_BotDefend, 7000 },
	{ "bot_fists",    "a bare-handed bot (its own AI) fights with the combo",     T_BotFists_Setup, T_BotFists, 13000 },
	{ "bot_escape",   "a bot Kawarimis out of a Water Prison (tactics)",          T_BotEscape_Setup, T_BotEscape, 16000 },
	{ "bot_prison",   "a bot holds its Water Prison, then hits the gasping victim", T_BotPrison_Setup, T_BotPrison, 12000 },
	{ "bot_interrupt","a bot breaks seals formed close by and far off",           T_BotInterrupt_Setup, T_BotInterrupt, 9500 },
	{ "bot_dodge",    "a bot steps away from fireballs",                          T_BotDodge_Setup, T_BotDodge, 9000 },
	{ "bot_keepoff",  "a bot keeps off a spinning Kaiten",                        T_BotKeepOff_Setup, T_BotKeepOff, 4000 },
	{ "blade_reach",  "plain cuts at 30 / 48 / 66 u: the blade alone and with the reach", T_BladeReach_Setup, T_BladeReach, 6 * BR_MS + 1000 },
	{ "bot_nav",      "on a map without bot routes a bot hunts instead of hopping", T_BotNav_Setup, T_BotNav, 6000 },
};
#define AT_COUNT ( (int)ARRAY_LEN( atTests ) )

// ---------------------------------------------------------------------------
// the run
// ---------------------------------------------------------------------------
static gentity_t *AT_FindBot( const char *name ) {
	int i;
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		gentity_t *e = &g_entities[i];
		if ( e->inuse && e->client && e->client->pers.connected == CON_CONNECTED && ( e->r.svFlags & SVF_BOT )
			&& !Q_stricmp( e->client->pers.netname, name ) )
			return e;
	}
	return NULL;
}

// an open spot for the tests: from A's spawn, the direction with the most room
static void AT_ChooseBase( void ) {
	float   bestLen = -1, yaw;
	vec3_t  org, end, f, mins = { -16, -16, -8 }, maxs = { 16, 16, 40 };
	trace_t tr;

	VectorCopy( A->client->ps.origin, org );
	for ( yaw = 0; yaw < 360; yaw += 30 ) {
		vec3_t ang = { 0, 0, 0 };
		ang[YAW] = yaw;
		AngleVectors( ang, f, NULL, NULL );
		VectorMA( org, 600, f, end );
		trap->Trace( &tr, org, mins, maxs, end, at.a, MASK_PLAYERSOLID, qfalse, 0, 0 );
		if ( tr.fraction * 600 > bestLen ) {
			bestLen = tr.fraction * 600;
			at.baseYaw = yaw;
		}
	}
	{ // start a little back so there is room behind A too
		vec3_t ang = { 0, 0, 0 };
		ang[YAW] = at.baseYaw;
		AngleVectors( ang, at.fwd, at.right, NULL );
		VectorCopy( org, at.base );
		vec3_t top;
		VectorCopy( org, top );
		top[2] += 600;
		trap->Trace( &tr, org, mins, maxs, top, at.a, MASK_PLAYERSOLID, qfalse, 0, 0 );
		AT_Print( "[autotest] test spot %.0f %.0f %.0f facing %.0f, %.0f u of room ahead, %.0f u up to the ceiling\n", org[0], org[1], org[2],
			at.baseYaw, bestLen, tr.fraction * 600 );
	}
	at.haveBase = qtrue;
}

static void AT_BeginTest( void ) {
	atTest_t *t = &atTests[at.queue[at.cur]];

	memset( at.drive, 0, sizeof( at.drive ) );
	at.drive[0].on = at.drive[1].on = qtrue;
	memset( at.v, 0, sizeof( at.v ) );
	memset( at.f, 0, sizeof( at.f ) );
	at.msg[0] = 0;
	at.stage = 0;
	AT_FreeTheirStuff();
	AT_Reset( A );
	AT_Reset( B );
	AT_Weapon( 0, WP_MELEE );
	AT_Weapon( 1, WP_MELEE );
	{
		vec3_t p;
		AT_Place( A, at.base, at.baseYaw );
		AT_Spot( 300, 0, p );
		AT_Place( B, p, at.baseYaw + 180 );
	}
	if ( !strncmp( t->name, "bot_", 4 ) )
		at.drive[1].free = qtrue; // its own defence decides (Shinobi_BotDefend runs for it)
	if ( t->setup )
		t->setup();
	at.start = at.stageTime = level.time;
}

static void AT_Finish( void ) {
	AT_Print( "[autotest] done: %i passed, %i failed\n", at.pass, at.fail );
	if ( at.file ) {
		trap->FS_Close( at.file );
		at.file = 0;
	}
	AT_FreeTheirStuff();
	at.phase = 3;
	trap->SendServerCommand( -1, va( "cp \"^3Autotest done: ^2%i passed^3, ^1%i failed\"", at.pass, at.fail ) );
	{ // a script waiting for the end (the self-test: dump the console and quit)
		char done[MAX_CVAR_VALUE_STRING];
		trap->Cvar_VariableStringBuffer( "shinobi_autotest_done", done, sizeof( done ) );
		if ( done[0] ) {
			trap->Cvar_Set( "shinobi_autotest_done", "" );
			trap->SendConsoleCommand( EXEC_APPEND, "wait 60; vstr shinobi_autotest_done_run\n" );
			trap->Cvar_Set( "shinobi_autotest_done_run", done );
		}
	}
}

// G_RunFrame (Shinobi_RunFrame)
void Shinobi_AutotestFrame( void ) {
	if ( at.phase == 1 ) {
		gentity_t *a = AT_FindBot( AT_BOT_A ), *b = AT_FindBot( AT_BOT_B );
		if ( a && b && a->health > 0 && b->health > 0 && a->client->sess.sessionTeam != TEAM_SPECTATOR
			&& b->client->sess.sessionTeam != TEAM_SPECTATOR && level.time - at.phaseStart > 2000 ) {
			at.a = a->s.number;
			at.b = b->s.number;
			if ( !at.haveBase )
				AT_ChooseBase();
			at.phase = 2;
			at.cur = 0;
			AT_BeginTest();
			AT_Print( "[autotest] %s: %s\n", atTests[at.queue[0]].name, atTests[at.queue[0]].what );
		}
		else if ( level.time - at.phaseStart > 20000 ) {
			AT_Print( "[autotest] FAIL setup: the bots %s / %s never joined (bot_enable? a map with bot routes?)\n", AT_BOT_A, AT_BOT_B );
			at.fail++;
			AT_Finish();
		}
		return;
	}
	if ( at.phase != 2 )
		return;
	if ( !A->inuse || !B->inuse || !A->client || !B->client ) {
		AT_Print( "[autotest] FAIL: a test bot left\n" );
		at.fail++;
		AT_Finish();
		return;
	}
	if ( A->health <= 0 || B->health <= 0 ) { // (a test that kills would be a bug: say so, go on after respawn)
		AT_Msg( "a bot died" );
	}
	{
		atTest_t  *t = &atTests[at.queue[at.cur]];
		atResult_t r = ( A->health > 0 && B->health > 0 ) ? t->step( level.time - at.start ) : ATR_FAIL;
		if ( r == ATR_RUN && level.time - at.start > t->timeout ) {
			if ( !at.msg[0] )
				AT_Msg( "timed out at stage %i", at.stage );
			else
				Q_strcat( at.msg, sizeof( at.msg ), va( " (timed out at stage %i)", at.stage ) );
			r = ATR_FAIL;
		}
		if ( r == ATR_RUN )
			return;
		if ( r == ATR_PASS )
			at.pass++;
		else
			at.fail++;
		AT_Print( "[autotest] %s %s: %s\n", r == ATR_PASS ? "PASS" : "FAIL", t->name, at.msg );
		if ( ++at.cur >= at.queueLen ) {
			AT_Finish();
			return;
		}
		AT_BeginTest();
		AT_Print( "[autotest] %s: %s\n", atTests[at.queue[at.cur]].name, atTests[at.queue[at.cur]].what );
	}
}

// "shinobi_openspot" (cheats): step out into the most open place around, facing the open way
// (the pose screenshots want room in front for the camera)
void Cmd_ShinobiOpenSpot_f( gentity_t *ent ) {
	float   bestLen = -1, bestYaw = 0, yaw;
	vec3_t  org, end, f, ang = { 0, 0, 0 }, mins = { -16, -16, -8 }, maxs = { 16, 16, 40 };
	trace_t tr;

	if ( !sv_cheats.integer || !ent->client )
		return;
	VectorCopy( ent->client->ps.origin, org );
	for ( yaw = 0; yaw < 360; yaw += 15 ) {
		ang[YAW] = yaw;
		AngleVectors( ang, f, NULL, NULL );
		VectorMA( org, 800, f, end );
		trap->Trace( &tr, org, mins, maxs, end, ent->s.number, MASK_PLAYERSOLID, qfalse, 0, 0 );
		if ( tr.fraction * 800 > bestLen ) {
			bestLen = tr.fraction * 800;
			bestYaw = yaw;
		}
	}
	ang[YAW] = bestYaw;
	AngleVectors( ang, f, NULL, NULL );
	VectorMA( org, bestLen * 0.4f, f, end ); // well clear of the wall behind, room ahead
	Shinobi_TeleportClient( ent, end );
	VectorClear( ent->client->ps.velocity );
	SetClientViewAngle( ent, ang );
	trap->SendServerCommand( ent - g_entities, va( "print \"open spot: %.0f u of room facing %.0f\n\"", bestLen, bestYaw ) );
}

// "shinobi_autotest [list | stop | name ...]"
void Cmd_ShinobiAutotest_f( gentity_t *ent ) {
	char arg[64];
	int  i, j, argc = trap->Argc();

	if ( !sv_cheats.integer ) {
		trap->SendServerCommand( ent - g_entities, "print \"shinobi_autotest needs cheats (devmap)\n\"" );
		return;
	}
	trap->Argv( 1, arg, sizeof( arg ) );
	if ( !Q_stricmp( arg, "list" ) ) {
		for ( i = 0; i < AT_COUNT; i++ )
			trap->SendServerCommand( ent - g_entities, va( "print \"%-13s %s\n\"", atTests[i].name, atTests[i].what ) );
		return;
	}
	if ( !Q_stricmp( arg, "stop" ) ) {
		if ( at.phase == 1 || at.phase == 2 )
			AT_Finish();
		return;
	}
	if ( at.phase == 1 || at.phase == 2 ) {
		trap->SendServerCommand( ent - g_entities, "print \"autotest already running (shinobi_autotest stop)\n\"" );
		return;
	}
	at.queueLen = 0;
	if ( argc < 2 || !Q_stricmp( arg, "all" ) ) {
		for ( i = 0; i < AT_COUNT; i++ )
			at.queue[at.queueLen++] = i;
	}
	else {
		for ( j = 1; j < argc; j++ ) {
			trap->Argv( j, arg, sizeof( arg ) );
			for ( i = 0; i < AT_COUNT; i++ )
				if ( !Q_stricmp( arg, atTests[i].name ) && at.queueLen < AT_MAX_TESTS )
					at.queue[at.queueLen++] = i;
		}
	}
	if ( !at.queueLen ) {
		trap->SendServerCommand( ent - g_entities, "print \"no such test (shinobi_autotest list)\n\"" );
		return;
	}
	at.pass = at.fail = 0;
	at.haveBase = qfalse;
	trap->FS_Open( "shinobi_autotest.txt", &at.file, FS_WRITE );
	AT_Print( "[autotest] %i tests, map %s\n", at.queueLen, level.mapname );
	if ( !AT_FindBot( AT_BOT_A ) )
		trap->SendConsoleCommand( EXEC_APPEND, "addbot alora 1 free 0 " AT_BOT_A "\n" ); // (alora is in every install)
	if ( !AT_FindBot( AT_BOT_B ) )
		trap->SendConsoleCommand( EXEC_APPEND, "addbot alora 1 free 0 " AT_BOT_B "\n" );
	at.phase = 1;
	at.phaseStart = level.time;
}
