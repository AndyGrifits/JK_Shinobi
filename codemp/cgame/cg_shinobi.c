/*
===========================================================================
JK_Shinobi - client side: jutsu hold input and charge bars, per-frame
jutsu visuals, HUD text and diagnostics.

Visuals that must stay glued to something (the ball in a hand, the flying
fireball) are drawn as sprites every frame; effect files (.efx) are only
used for loose particles (sparks, smoke, embers) and one-shot bursts.
===========================================================================
*/
#include "cg_local.h"
#include "game/bg_shinobi.h"
#include "ui/menudef.h"
#include "ui/ui_public.h"

static void CG_ShinobiDumpWaterShaders( void );
static void CG_ShinobiRaiDashForm( centity_t *cent );
static void CG_ShinobiIceShard( centity_t *cent );
static int  CG_ShinobiMyEyes( void );
#include "fx_local.h"

static void CG_ShinobiPrecacheModels( void );
#include "ui/keycodes.h"

#define SHINOBI_HAND_FX_INTERVAL  60   // ms between hand spark effects
#define SHINOBI_AURA_FX_INTERVAL  70
#define SHINOBI_BALL_FX_INTERVAL  40

static struct {
	qhandle_t glow, swirl, bolt, fire, spark;
	qhandle_t puddle, mud, scorch, waterSphere, ring, shadowBand, fogScreen, mistShell, windArc;
	qhandle_t sand, puddleWater, stoneShell, ice, iceFloe, guardShell, vignette, tomoe, sharinganRim, outline[3], kaitenShell, gateShell, legWeight;
} shinobiShaders;

// standing in a Hidden Mist this frame (and the last)
static struct {
	int      time, lastTime;
	float    amount, lastAmount;
	qboolean mine, lastMine; // inside our own mist: only its maker sees the shapes
} shinobiMist;

// thick smoke around the local player this frame (drawn over the screen)
static struct {
	int   time;
	int   type;   // SZ_ASH, SZ_STEAM or SZ_MIST
	float amount; // 0..1
} shinobiFog;

// loose-particle effects for elements (one-shots come from the server)
static struct {
	fxHandle_t fireGround, fireBody, steam, waterTrail, drip, bubbles, zoneSpark, waveCrest, flame, ash, mist, quakeDust, poison;
} shinobiFx;

// the jutsu key being held on this client (for the charge bars)
static struct {
	int      jutsu;
	int      start;
	qboolean fromJump; // Shunshin charged with Shift + jump
} shinobiCharge;

// the build in effect (sent by the server on spawn: "sbuild <string>")
static shinobiBuild_t shinobiActive;
static qboolean       shinobiActiveSet;
static vmCvar_t       shinobiBuildCvar;

static const shinobiBuild_t *CG_ShinobiBuild( void ) {
	if ( !shinobiActiveSet ) {
		Shinobi_BuildDefault( &shinobiActive );
		shinobiActiveSet = qtrue;
	}
	return &shinobiActive;
}

// Copy Sharingan: the jutsu in the copy slot ("scopy <jutsu>"; cleared with each build)
static int shinobiCopy;

void CG_ShinobiCopy_f( void ) {
	const int j = atoi( CG_Argv( 1 ) );
	shinobiCopy = ( j > SJ_NONE && j < SJ_COUNT ) ? j : SJ_NONE;
}

// is this jutsu only ours through the copy slot?
static qboolean CG_ShinobiIsCopy( int jutsu ) {
	return shinobiCopy && jutsu == shinobiCopy && !Shinobi_BuildHas( CG_ShinobiBuild(), jutsu );
}

// what the list offers: the build, then the copy (if it is not in the build already)
static int CG_ShinobiSlots( int *out ) {
	const shinobiBuild_t *b = CG_ShinobiBuild();
	int i, n = 0;
	for ( i = 0; i < b->numJutsu && i < SHINOBI_BUILD_JUTSU; i++ )
		out[n++] = b->jutsu[i];
	if ( CG_ShinobiIsCopy( shinobiCopy ) )
		out[n++] = shinobiCopy;
	if ( SHINOBI_GATE_OF( &cg.predictedPlayerState ) >= 3 && !Shinobi_BuildHas( b, SJ_LOTUS ) && n < SHINOBI_BUILD_JUTSU + 1 )
		out[n++] = SJ_LOTUS; // the third Inner Gate: Reverse Lotus
	return n;
}

// the chakra a level costs us (a copy: dearer, and no level above its top: "unaffordable")
static int CG_ShinobiPrice( int jutsu, int level ) {
	if ( CG_ShinobiIsCopy( jutsu ) ) {
		const int c = Shinobi_CopyCost( jutsu, level );
		return c ? c : 99999;
	}
	return Shinobi_JutsuCost( jutsu, level );
}

// jutsu selection (cycled with jutsu_next / jutsu_prev, cast with +jutsu)
static struct {
	int      index;       // into shinobiJutsuCycle
	int      changeTime;
	qboolean jumpHeld;    // jump was down last frame
} shinobiSel;

/*
===========================================================================
Diagnostics: "[shinobi]" lines in qconsole.log; "shinobi_debug 1" for more.
===========================================================================
*/
static qboolean CG_ShinobiDebug( void ) {
	char buf[16];
	trap->Cvar_VariableStringBuffer( "shinobi_debug", buf, sizeof( buf ) );
	return atoi( buf ) ? qtrue : qfalse;
}

static fxHandle_t CG_ShinobiRegisterFx( const char *name ) {
	fxHandle_t h = trap->FX_RegisterEffect( name );
	if ( !h ) {
		trap->Print( S_COLOR_RED "[shinobi] cgame: effect FAILED to load: effects/%s.efx\n", name );
	}
	return h;
}

static void CG_ShinobiCheckFile( const char *path ) {
	fileHandle_t f;
	int len = trap->FS_Open( path, &f, FS_READ );

	if ( len > 0 ) {
		trap->FS_Close( f );
		trap->Print( "[shinobi] file ok      %6i  %s\n", len, path );
	}
	else {
		trap->Print( S_COLOR_RED "[shinobi] file MISSING        %s\n", path );
	}
}

void CG_ShinobiDiag_f( void ) {
	char fsGame[MAX_QPATH];
	char renderer[MAX_QPATH];

	trap->Cvar_VariableStringBuffer( "fs_game", fsGame, sizeof( fsGame ) );
	trap->Cvar_VariableStringBuffer( "cl_renderer", renderer, sizeof( renderer ) );
	trap->Print( "[shinobi] cgame module built " __DATE__ " " __TIME__ ", fs_game '%s', renderer '%s'\n", fsGame, renderer );

	CG_ShinobiCheckFile( "shaders/shinobi.shader" );
	CG_ShinobiCheckFile( "gfx/shinobi/fire.tga" );
	CG_ShinobiCheckFile( "effects/shinobi/rasengan_hit_2.efx" );
	CG_ShinobiCheckFile( "effects/shinobi/clone_smoke.efx" );
	CG_ShinobiCheckFile( "models/shinobi/earth_pillar1.md3" );
	CG_ShinobiCheckFile( "ext_data/NPCs/shinobi.npc" );

	trap->Print( "[shinobi] handles: fx hand %i/%i aura %i trail %i, shaders glow %i swirl %i bolt %i fire %i\n",
		cgs.effects.shinobiRasenganHand, cgs.effects.shinobiChidoriHand, cgs.effects.shinobiChakraAura,
		cgs.effects.shinobiFireballTrail, shinobiShaders.glow, shinobiShaders.swirl, shinobiShaders.bolt, shinobiShaders.fire );
	trap->Print( "[shinobi] local player: userInt %i%i%i hand level %i, chakra %i\n",
		cg.predictedPlayerState.userInt1, cg.predictedPlayerState.userInt2, cg.predictedPlayerState.userInt3,
		SHINOBI_HAND_LEVEL( &cg.predictedPlayerState ), cg.predictedPlayerState.fd.forcePower );
}

void CG_ShinobiRegisterMedia( void ) {
	Shinobi_PatchItems(); // pickups -> pills and ninja tools (must match the server)
	cgs.effects.shinobiRasenganHand  = CG_ShinobiRegisterFx( SHINOBI_FX_RASENGAN_HAND );
	cgs.effects.shinobiRasenganForm  = CG_ShinobiRegisterFx( SHINOBI_FX_RASENGAN_FORM );
	cgs.effects.shinobiChidoriHand   = CG_ShinobiRegisterFx( SHINOBI_FX_CHIDORI_HAND );
	cgs.effects.shinobiChidoriForm   = CG_ShinobiRegisterFx( SHINOBI_FX_CHIDORI_FORM );
	cgs.effects.shinobiChakraAura    = CG_ShinobiRegisterFx( SHINOBI_FX_CHAKRA_AURA );
	cgs.effects.shinobiFireballTrail = CG_ShinobiRegisterFx( SHINOBI_FX_FIREBALL_TRAIL );
	// one-shot effects played by the server are registered through configstrings

	shinobiShaders.glow  = trap->R_RegisterShader( "gfx/shinobi/glow" );
	shinobiShaders.swirl = trap->R_RegisterShader( "gfx/shinobi/swirl" );
	shinobiShaders.bolt  = trap->R_RegisterShader( "gfx/shinobi/bolt" );
	shinobiShaders.fire  = trap->R_RegisterShader( "gfx/shinobi/fire" );
	shinobiShaders.spark = trap->R_RegisterShader( "gfx/shinobi/spark" );
	shinobiShaders.puddle      = trap->R_RegisterShader( "gfx/shinobi/puddle" );
	shinobiShaders.mud         = trap->R_RegisterShader( "gfx/shinobi/mud" );
	shinobiShaders.scorch      = trap->R_RegisterShader( "gfx/shinobi/scorch" );
	shinobiShaders.waterSphere = trap->R_RegisterShader( "gfx/shinobi/watersphere" );
	shinobiShaders.ring        = trap->R_RegisterShader( "gfx/shinobi/ring" );

	shinobiFx.fireGround = CG_ShinobiRegisterFx( SHINOBI_FX_FIRE_GROUND );
	shinobiFx.fireBody   = CG_ShinobiRegisterFx( SHINOBI_FX_FIRE_BODY );
	shinobiFx.steam      = CG_ShinobiRegisterFx( SHINOBI_FX_STEAM );
	shinobiFx.waterTrail = CG_ShinobiRegisterFx( SHINOBI_FX_WATER_TRAIL );
	shinobiFx.drip       = CG_ShinobiRegisterFx( SHINOBI_FX_DRIP );
	shinobiFx.bubbles    = CG_ShinobiRegisterFx( SHINOBI_FX_BUBBLES );
	shinobiFx.zoneSpark  = CG_ShinobiRegisterFx( SHINOBI_FX_ZONE_SPARK );
	shinobiFx.waveCrest  = CG_ShinobiRegisterFx( SHINOBI_FX_WAVE_CREST );
	shinobiFx.flame      = CG_ShinobiRegisterFx( SHINOBI_FX_FLAME );
	shinobiFx.ash        = CG_ShinobiRegisterFx( SHINOBI_FX_ASH );
	shinobiFx.poison     = CG_ShinobiRegisterFx( SHINOBI_FX_POISON );
	shinobiFx.mist       = CG_ShinobiRegisterFx( SHINOBI_FX_MIST );
	shinobiFx.quakeDust  = CG_ShinobiRegisterFx( SHINOBI_FX_QUAKE_DUST );
	shinobiShaders.shadowBand = trap->R_RegisterShader( "gfx/shinobi/shadowband" );
	shinobiShaders.windArc    = trap->R_RegisterShader( "gfx/shinobi/windarc" );
	shinobiShaders.fogScreen  = trap->R_RegisterShader( "gfx/shinobi/fogscreen" );
	shinobiShaders.mistShell  = trap->R_RegisterShader( "gfx/shinobi/mistshell" );
	shinobiShaders.sand        = trap->R_RegisterShader( "gfx/shinobi/sand" );
	shinobiShaders.stoneShell  = trap->R_RegisterShader( "gfx/shinobi/stoneshell" );
	shinobiShaders.gateShell   = trap->R_RegisterShader( "gfx/shinobi/gateshell" );
	shinobiShaders.legWeight   = trap->R_RegisterModel( "models/shinobi/leg_weight.md3" );
	shinobiShaders.ice         = trap->R_RegisterShader( "gfx/shinobi/ice" );
	shinobiShaders.iceFloe     = trap->R_RegisterShader( "gfx/shinobi/icefloe" );
	shinobiShaders.guardShell  = trap->R_RegisterShader( "gfx/shinobi/guardshell" );
	shinobiShaders.kaitenShell = trap->R_RegisterShader( "gfx/shinobi/kaitenshell" );
	shinobiShaders.vignette    = trap->R_RegisterShader( "gfx/shinobi/vignette" );
	shinobiShaders.tomoe       = trap->R_RegisterShader( "gfx/shinobi/tomoe" );
	shinobiShaders.sharinganRim = trap->R_RegisterShader( "gfx/shinobi/sharinganrim" );
	shinobiShaders.outline[0]  = trap->R_RegisterShader( "gfx/shinobi/outline1" );
	shinobiShaders.outline[1]  = trap->R_RegisterShader( "gfx/shinobi/outline2" );
	shinobiShaders.outline[2]  = trap->R_RegisterShader( "gfx/shinobi/outline3" );
	{ // puddles and the wave: the game's own Bespin water when it is there, ours otherwise
		// (shinobi_puddleShader names any other shader)
		char         name[MAX_QPATH];
		fileHandle_t f = 0;
		trap->Cvar_VariableStringBuffer( "shinobi_puddleShader", name, sizeof( name ) );
		shinobiShaders.puddleWater = name[0] ? trap->R_RegisterShader( name ) : 0;
		if ( !shinobiShaders.puddleWater ) {
			static const char * const tex[] = { "textures/bespin/water1.jpg", "textures/bespin/water1.tga" };
			qboolean stock = qfalse;
			int      k;
			for ( k = 0; k < 2 && !stock; k++ ) {
				f = 0;
				if ( trap->FS_Open( tex[k], &f, FS_READ ) > 0 && f )
					stock = qtrue;
				if ( f )
					trap->FS_Close( f );
			}
			shinobiShaders.puddleWater = trap->R_RegisterShader( stock ? "gfx/shinobi/puddlewater_stock" : "gfx/shinobi/puddlewater" );
			trap->Print( "[shinobi] water surface: %s\n", stock ? "stock Bespin water" : "own texture" );
		}
	}
	CG_ShinobiDumpWaterShaders();

	memset( &shinobiCharge, 0, sizeof( shinobiCharge ) );
	shinobiSel.jumpHeld = qfalse;
	shinobiSel.changeTime = 0;
	shinobiSel.index = 0;
	shinobiActiveSet = qfalse;
	trap->Cvar_Register( &shinobiBuildCvar, "shinobi_build", "", CVAR_USERINFO | CVAR_ARCHIVE );
	trap->Cvar_Register( NULL, "shinobi_weapon", "one", CVAR_USERINFO | CVAR_ARCHIVE ); // fists / one / two / staff
	CG_ShinobiPrecacheModels();

	if ( CG_ShinobiDebug() )
		CG_ShinobiDiag_f();
}

/*
===========================================================================
Hold-to-charge input: "+rasengan" / "-rasengan" etc. (bound to keys)
===========================================================================
*/
static qboolean CG_ShinobiCanCharge( void ) {
	const playerState_t *ps = &cg.predictedPlayerState;

	if ( !cg.snap || cg.demoPlayback )
		return qfalse;
	if ( ps->pm_type == PM_SPECTATOR || ps->pm_type == PM_DEAD || ps->stats[STAT_HEALTH] <= 0 )
		return qfalse;
	return qtrue;
}

static void CG_ShinobiJutsuDown( int jutsu ) {
	if ( shinobiCharge.jutsu || !CG_ShinobiCanCharge() )
		return;
	if ( SHINOBI_JUTSU_INSTANT( jutsu ) ) { // the server casts it on the spot
		trap->SendClientCommand( va( "jutsu_begin %s", shinobiJutsuNames[jutsu] ) );
		return;
	}
	if ( ( jutsu == SJ_RASENGAN || jutsu == SJ_CHIDORI || jutsu == SJ_KATON )
		&& SHINOBI_HAND_ANY( &cg.predictedPlayerState ) )
		return; // hands busy
	if ( jutsu != SJ_SHUNSHIN && !SHINOBI_JUTSU_HOLD( jutsu ) ) {
		usercmd_t cmd;
		if ( trap->GetUserCmd( trap->GetCurrentCmdNumber(), &cmd ) && SHINOBI_MODIFIER( &cmd ) ) {
			// Shift + the key: the server casts it at once (level 1, the top price) - no bars
			trap->SendClientCommand( va( "jutsu_begin %s", shinobiJutsuNames[jutsu] ) );
			return;
		}
	}
	shinobiCharge.jutsu = jutsu;
	shinobiCharge.start = cg.time;
	shinobiCharge.fromJump = qfalse;
	trap->SendClientCommand( va( "jutsu_begin %s", shinobiJutsuNames[jutsu] ) );
}

// "sjc": a hit broke our seals - the bars go (the key must be pressed again)
void CG_ShinobiChargeCancel_f( void ) {
	shinobiCharge.jutsu = SJ_NONE;
}

static void CG_ShinobiJutsuUp( int jutsu ) {
	if ( shinobiCharge.jutsu != jutsu )
		return;
	shinobiCharge.jutsu = SJ_NONE;
	trap->SendClientCommand( "jutsu_release" );
}

void CG_ShinobiRasenganDown_f( void )    { CG_ShinobiJutsuDown( SJ_RASENGAN ); }
void CG_ShinobiRasenganUp_f( void )      { CG_ShinobiJutsuUp( SJ_RASENGAN ); }
void CG_ShinobiChidoriDown_f( void )     { CG_ShinobiJutsuDown( SJ_CHIDORI ); }
void CG_ShinobiChidoriUp_f( void )       { CG_ShinobiJutsuUp( SJ_CHIDORI ); }
void CG_ShinobiKatonDown_f( void )       { CG_ShinobiJutsuDown( SJ_KATON ); }
void CG_ShinobiKatonUp_f( void )         { CG_ShinobiJutsuUp( SJ_KATON ); }
void CG_ShinobiDotonDown_f( void )       { CG_ShinobiJutsuDown( SJ_DOTON ); }
void CG_ShinobiDotonUp_f( void )         { CG_ShinobiJutsuUp( SJ_DOTON ); }
void CG_ShinobiKageBunshinDown_f( void ) { CG_ShinobiJutsuDown( SJ_BUNSHIN ); }
void CG_ShinobiKageBunshinUp_f( void )   { CG_ShinobiJutsuUp( SJ_BUNSHIN ); }

/*
===========================================================================
Jutsu selection: one cast key ("+jutsu") for the jutsu picked with
"jutsu_next" / "jutsu_prev", like force powers in the base game.
===========================================================================
*/
void CG_ShinobiCastDown_f( void ) {
	int slots[SHINOBI_BUILD_JUTSU + 1];
	const int n = CG_ShinobiSlots( slots );
	if ( !n )
		return;
	if ( shinobiSel.index >= n )
		shinobiSel.index = 0;
	if ( SHINOBI_JUTSU_PASSIVE( slots[shinobiSel.index] ) )
		return; // works by itself
	CG_ShinobiJutsuDown( slots[shinobiSel.index] );
}

// "sbuild <string>": the server applied our build
void CG_ShinobiSetBuild_f( void ) {
	Shinobi_BuildParse( CG_Argv( 1 ), &shinobiActive );
	Shinobi_BuildFix( &shinobiActive );
	shinobiActiveSet = qtrue;
	shinobiCopy = SJ_NONE; // a new life: nothing copied yet
	if ( shinobiSel.index >= shinobiActive.numJutsu )
		shinobiSel.index = 0;
}

void CG_ShinobiCastUp_f( void ) {
	if ( shinobiCharge.jutsu && !shinobiCharge.fromJump )
		CG_ShinobiJutsuUp( shinobiCharge.jutsu ); // whatever this key started, even if the selection moved
}

static void CG_ShinobiSelect( int step ) {
	int slots[SHINOBI_BUILD_JUTSU + 1];
	const int n = CG_ShinobiSlots( slots );
	int tries;
	if ( !n )
		return;
	if ( shinobiSel.index >= n )
		shinobiSel.index = 0;
	for ( tries = 0; tries < n; tries++ ) { // passives are skipped
		shinobiSel.index = ( shinobiSel.index + step + n ) % n;
		if ( !SHINOBI_JUTSU_PASSIVE( slots[shinobiSel.index] ) )
			break;
	}
	shinobiSel.changeTime = cg.time;
	trap->S_StartLocalSound( cgs.media.selectSound, CHAN_LOCAL_SOUND );
}

void CG_ShinobiNext_f( void ) { CG_ShinobiSelect( 1 ); }
void CG_ShinobiPrev_f( void ) { CG_ShinobiSelect( -1 ); }

// "shinobi_melee": switch between fists and the saber
void CG_ShinobiMelee_f( void ) {
	int want;


	if ( !cg.snap || cg.snap->ps.pm_type == PM_SPECTATOR )
		return;
	want = ( cg.snap->ps.weapon == WP_MELEE ) ? WP_SABER : WP_MELEE;
	if ( !( cg.snap->ps.stats[STAT_WEAPONS] & ( 1 << want ) ) )
		return;
	cg.weaponSelectTime = cg.time;
	cg.weaponSelect = want;
}

/*
==================
CG_ShinobiPrecacheModels

Every weapon (ext_data/sabers/shinobi_*.sab -> models/weapons2/<name>/model.glm) and
every character (models/players/<name> with a shinobi.char), loaded by the client while
the map loads, so they count as this level's and don't wait to be first loaded by the
server (bots, their weapons).
==================
*/
static void CG_ShinobiPrecacheModels( void ) {
	static char list[8192];
	char       *f;
	int         n, i, k = 0;

	n = trap->FS_GetFileList( "ext_data/sabers", ".sab", list, sizeof( list ) );
	for ( i = 0, f = list; i < n; i++, f += strlen( f ) + 1 ) {
		char name[64], *dot;
		if ( Q_stricmpn( f, SHINOBI_WEAPON_PREFIX, strlen( SHINOBI_WEAPON_PREFIX ) ) )
			continue;
		Q_strncpyz( name, f, sizeof( name ) );
		if ( ( dot = strrchr( name, '.' ) ) != NULL )
			*dot = 0;
		if ( trap->R_RegisterModel( va( "models/weapons2/%s/model.glm", name ) ) )
			k++;
	}
	n = trap->FS_GetFileList( "models/players", "/", list, sizeof( list ) );
	for ( i = 0, f = list; i < n; i++, f += strlen( f ) + 1 ) {
		char         name[64];
		int          len = strlen( f );
		fileHandle_t fh;
		Q_strncpyz( name, f, sizeof( name ) );
		if ( len && ( name[len - 1] == '/' || name[len - 1] == '\\' ) )
			name[len - 1] = 0;
		if ( !name[0] || name[0] == '.' || name[0] == '_' )
			continue;
		trap->FS_Open( va( "models/players/%s/shinobi.char", name ), &fh, FS_READ );
		if ( !fh )
			continue;
		trap->FS_Close( fh );
		if ( trap->R_RegisterModel( va( "models/players/%s/model.glm", name ) ) )
			k++;
	}
	Com_Printf( "[shinobi] %i weapon / character models precached\n", k );
}

/*
==================
CG_ShinobiFrame

Every frame: Shift (the chakra modifier) + jump charges Shunshin; letting go
of jump blinks (backward if back is held - the server checks that).
==================
*/
void CG_ShinobiFrame( void ) {
	usercmd_t cmd;
	qboolean  jump;

	if ( !CG_ShinobiCanCharge() ) {
		shinobiSel.jumpHeld = qfalse;
		return;
	}
	if ( !trap->GetUserCmd( trap->GetCurrentCmdNumber(), &cmd ) )
		return;
	jump = cmd.upmove > 0;

	if ( shinobiCharge.jutsu == SJ_SHUNSHIN && shinobiCharge.fromJump ) {
		if ( !jump )
			CG_ShinobiJutsuUp( SJ_SHUNSHIN );
	}
	else if ( jump && !shinobiSel.jumpHeld && SHINOBI_MODIFIER( &cmd ) && !shinobiCharge.jutsu ) {
		CG_ShinobiJutsuDown( SJ_SHUNSHIN );
		shinobiCharge.fromJump = qtrue;
	}
	shinobiSel.jumpHeld = jump;
}

/*
===========================================================================
Sprite helpers
===========================================================================
*/
static void CG_ShinobiDrawEyes( centity_t *cent, clientInfo_t *ci );
qboolean CG_WorldCoordToScreenCoordFloat( vec3_t worldCoord, float *x, float *y );

static void CG_ShinobiSpriteA( const vec3_t org, float radius, float rotation, qhandle_t shader, float r, float g, float b, float a ) {
	refEntity_t ent;

	if ( !shader || radius <= 0 || a <= 0 )
		return;
	memset( &ent, 0, sizeof( ent ) );
	ent.reType = RT_SPRITE;
	VectorCopy( org, ent.origin );
	ent.radius = radius;
	ent.rotation = rotation;
	ent.customShader = shader;
	ent.shaderRGBA[0] = (byte)( Com_Clamp( 0, 1, r ) * 255 );
	ent.shaderRGBA[1] = (byte)( Com_Clamp( 0, 1, g ) * 255 );
	ent.shaderRGBA[2] = (byte)( Com_Clamp( 0, 1, b ) * 255 );
	ent.shaderRGBA[3] = (byte)( Com_Clamp( 0, 1, a ) * 255 );
	ent.renderfx = RF_NOSHADOW;
	trap->R_AddRefEntityToScene( &ent );
}

/*
Air that bends the light: heat haze, a lens of water, a shockwave. The
stock Force Push trick - a sphere drawn with the refraction shader that
copies and warps what lies behind it (needs cg_renderToTextureFX 1).
*/
static float shinobiRefractModelR;

static void CG_ShinobiRefract( const vec3_t org, float radius, float alpha ) {
	refEntity_t ent;
	vec3_t      d, ang;
	float       vLen, s;

	if ( !cg_renderToTextureFX.integer || !cgs.media.halfShieldModel || !cgs.media.refractionShader
		|| radius < 2 || alpha < 1 )
		return;
	if ( shinobiRefractModelR <= 0 ) {
		vec3_t mins, maxs;
		trap->R_ModelBounds( cgs.media.halfShieldModel, mins, maxs );
		shinobiRefractModelR = 0.5f * ( maxs[0] - mins[0] );
		if ( shinobiRefractModelR < 1 )
			shinobiRefractModelR = 32;
	}
	VectorSubtract( org, cg.refdef.vieworg, d );
	vLen = VectorLength( d );
	if ( vLen < radius * 0.5f || vLen > 3000 )
		return; // inside it, or too far to matter
	memset( &ent, 0, sizeof( ent ) );
	VectorCopy( org, ent.origin );
	vectoangles( d, ang );
	ang[ROLL] += 180.0f;
	AnglesToAxis( ang, ent.axis );
	// the captured texture size (a power of two)
	ent.radius = vLen < 128 ? 256 : ( vLen < 256 ? 128 : ( vLen < 512 ? 64 : 32 ) );
	s = radius / shinobiRefractModelR;
	VectorScale( ent.axis[0], s, ent.axis[0] );
	VectorScale( ent.axis[1], s, ent.axis[1] );
	VectorScale( ent.axis[2], s, ent.axis[2] );
	ent.nonNormalizedAxes = qtrue;
	ent.hModel = cgs.media.halfShieldModel;
	ent.customShader = cgs.media.refractionShader;
	ent.renderfx = RF_DISTORTION | RF_FORCE_ENT_ALPHA;
	ent.shaderRGBA[0] = ent.shaderRGBA[1] = ent.shaderRGBA[2] = 255;
	ent.shaderRGBA[3] = (byte)( alpha > 255 ? 255 : alpha );
	trap->R_AddRefEntityToScene( &ent );
}

// expanding shockwaves of bent air (played by jutsu impacts, see CG_ShinobiEffectPlayed)
typedef struct {
	int    start, end;
	vec3_t org;
	float  r0, r1, alpha;
} shinobiShock_t;
static shinobiShock_t shinobiShocks[24];

static void CG_ShinobiShock( const vec3_t org, float r0, float r1, int ms, float alpha ) {
	int i, best = 0;
	for ( i = 0; i < (int)ARRAY_LEN( shinobiShocks ); i++ ) {
		if ( shinobiShocks[i].end <= cg.time ) {
			best = i;
			break;
		}
		if ( shinobiShocks[i].end < shinobiShocks[best].end )
			best = i;
	}
	shinobiShocks[best].start = cg.time;
	shinobiShocks[best].end = cg.time + ms;
	VectorCopy( org, shinobiShocks[best].org );
	shinobiShocks[best].r0 = r0;
	shinobiShocks[best].r1 = r1;
	shinobiShocks[best].alpha = alpha;
}

static void CG_ShinobiDrawShocks( void ) {
	int i;
	for ( i = 0; i < (int)ARRAY_LEN( shinobiShocks ); i++ ) {
		const shinobiShock_t *w = &shinobiShocks[i];
		float f, e;
		if ( w->end <= cg.time || w->start > cg.time )
			continue;
		f = (float)( cg.time - w->start ) / ( w->end - w->start );
		e = 1.0f - ( 1.0f - f ) * ( 1.0f - f ); // fast out, slow at the end
		CG_ShinobiRefract( w->org, w->r0 + ( w->r1 - w->r0 ) * e, w->alpha * ( 1.0f - f ) );
	}
}

// a server effect just played: the big ones also bend the air
void CG_ShinobiEffectPlayed( const char *name, const vec3_t org, const vec3_t dir ) {
	const char *p;
	int         lvl = 1;

	if ( !name || !( p = strstr( name, "shinobi/" ) ) )
		return;
	p += 8;
	if ( strlen( p ) > 2 && p[strlen( p ) - 2] == '_' )
		lvl = atoi( p + strlen( p ) - 1 );
	if ( lvl < 1 || lvl > 3 )
		lvl = 1;
	// (kept to water only: elsewhere the bent air looked worse than it helped)
	if ( !Q_strncmp( p, "water_splash", 12 ) )
		CG_ShinobiShock( org, 10, 30 + 18 * lvl, 320, 150 );
}

// a flat square on the floor (puddle, mud, scorch mark)
static void CG_ShinobiGroundDecal( const vec3_t org, float radius, float rotation, qhandle_t shader,
	float r, float g, float b, float a ) {
	static const float st[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
	polyVert_t v[4];
	int        i;

	if ( !shader || radius <= 0 || a <= 0 )
		return;
	for ( i = 0; i < 4; i++ ) {
		const float ang = DEG2RAD( rotation + 45.0f + 90.0f * i );
		v[i].xyz[0] = org[0] + cosf( ang ) * radius * 1.414f;
		v[i].xyz[1] = org[1] + sinf( ang ) * radius * 1.414f;
		v[i].xyz[2] = org[2] + 1.0f;
		v[i].st[0] = st[i][0];
		v[i].st[1] = st[i][1];
		v[i].modulate[0] = (byte)( Com_Clamp( 0, 1, r ) * 255 );
		v[i].modulate[1] = (byte)( Com_Clamp( 0, 1, g ) * 255 );
		v[i].modulate[2] = (byte)( Com_Clamp( 0, 1, b ) * 255 );
		v[i].modulate[3] = (byte)( Com_Clamp( 0, 1, a ) * 255 );
	}
	trap->R_AddPolysToScene( shader, 4, v, 1 );
}

/*
A puddle: a low dome of water over a ragged outline (rim at the floor, the
middle a few units up), so it reads as water lying on the ground.
*/
static void CG_ShinobiPuddle( const vec3_t org, float R, int seed, float a, float bright ) {
	enum { N = 18 };
	polyVert_t  fan[N * 3], ring[N * 4];
	vec3_t      rim[N], mid[N], top;
	float       rimR[N];
	int         i, k;
	const byte  ca = (byte)( Com_Clamp( 0, 1, a * 0.85f ) * 255 );
	const byte  ea = (byte)( Com_Clamp( 0, 1, a * 0.35f ) * 255 );
	const byte  shine = (byte)( Com_Clamp( 0, 1, 0.35f * bright * a ) * 255 ); // the additive sheen

	if ( R <= 0 || a <= 0 || !shinobiShaders.puddleWater )
		return;
	for ( i = 0; i < N; i++ ) {
		const float ang = 2 * M_PI * i / N;
		const int   h = ( seed * 7919 + i * 104729 ) & 1023;
		rimR[i] = R * ( 0.82f + 0.18f * ( h / 1023.0f ) );
		VectorSet( rim[i], org[0] + cosf( ang ) * rimR[i], org[1] + sinf( ang ) * rimR[i], org[2] + 0.8f );
		VectorSet( mid[i], org[0] + cosf( ang ) * rimR[i] * 0.6f, org[1] + sinf( ang ) * rimR[i] * 0.6f, org[2] + 2.6f );
	}
	VectorSet( top, org[0], org[1], org[2] + 3.4f );

#define PV( v, p, alpha ) do { VectorCopy( p, (v).xyz ); (v).st[0] = (p)[0] / 96.0f; (v).st[1] = (p)[1] / 96.0f; \
	(v).modulate[0] = (v).modulate[1] = (v).modulate[2] = shine; (v).modulate[3] = alpha; } while ( 0 )
	for ( i = 0, k = 0; i < N; i++ ) {
		const int j = ( i + 1 ) % N;
		PV( fan[k], top, ca ); k++;
		PV( fan[k], mid[i], ca ); k++;
		PV( fan[k], mid[j], ca ); k++;
	}
	trap->R_AddPolysToScene( shinobiShaders.puddleWater, 3, fan, N );
	for ( i = 0, k = 0; i < N; i++ ) {
		const int j = ( i + 1 ) % N;
		PV( ring[k], mid[i], ca ); k++;
		PV( ring[k], rim[i], ea ); k++;
		PV( ring[k], rim[j], ea ); k++;
		PV( ring[k], mid[j], ca ); k++;
	}
	trap->R_AddPolysToScene( shinobiShaders.puddleWater, 4, ring, N );
#undef PV
}

/*
Once per install: list the stock water shaders (name + body) into
shinobi_water_shaders.txt, so a real water surface can be picked for the
puddles (shinobi_puddleShader).
*/
static void CG_ShinobiDumpWaterShaders( void ) {
	static char  list[16384], text[262144];
	fileHandle_t out, f;
	int          n, i, len, total = 0;
	char        *name;

	if ( trap->FS_Open( "shinobi_water_shaders.txt", &out, FS_READ ) >= 0 && out ) {
		trap->FS_Close( out );
		return; // done before
	}
	if ( trap->FS_Open( "shinobi_water_shaders.txt", &out, FS_WRITE ) < 0 || !out )
		return;
	n = trap->FS_GetFileList( "shaders", ".shader", list, sizeof( list ) );
	for ( i = 0, name = list; i < n && total < 400000; i++, name += strlen( name ) + 1 ) {
		char *p, *start;
		int   depth = 0;

		len = trap->FS_Open( va( "shaders/%s", name ), &f, FS_READ );
		if ( len <= 0 || !f )
			continue;
		if ( len >= (int)sizeof( text ) )
			len = sizeof( text ) - 1;
		trap->FS_Read( text, len, f );
		trap->FS_Close( f );
		text[len] = 0;

		// top-level blocks whose name mentions water
		for ( p = start = text; *p; p++ ) {
			if ( p[0] == '/' && p[1] == '/' ) { // comment
				while ( *p && *p != '\n' )
					p++;
				if ( !*p )
					break;
				continue;
			}
			if ( *p == '{' ) {
				depth++; // a top-level block's name lies in start..p
			}
			else if ( *p == '}' && depth > 0 && !--depth ) {
				char head[128];
				int  hl = 0;
				char *q = start;
				while ( q < p && hl < (int)sizeof( head ) - 1 && *q != '{' )
					head[hl++] = *q++;
				head[hl] = 0;
				if ( Q_stristr( head, "water" ) || Q_stristr( head, "liquid" ) ) {
					const int bl = (int)( p + 1 - start );
					const char *hdr = va( "\n// ---- %s\n", name );
					trap->FS_Write( hdr, strlen( hdr ), out );
					trap->FS_Write( start, bl, out );
					total += bl;
				}
				start = p + 1;
			}
		}
	}
	trap->FS_Close( out );
	trap->Print( "[shinobi] stock water shaders listed in shinobi_water_shaders.txt (%i files scanned)\n", n );
}

// random point on the floor inside a zone
static void CG_ShinobiZonePoint( const vec3_t org, float radius, vec3_t out ) {
	const float ang = flrand( 0, 2 * M_PI );
	const float d = radius * sqrtf( flrand( 0, 1 ) ) * 0.9f;
	out[0] = org[0] + cosf( ang ) * d;
	out[1] = org[1] + sinf( ang ) * d;
	out[2] = org[2] + 2;
}

static void CG_ShinobiSprite( const vec3_t org, float radius, float rotation, qhandle_t shader, float r, float g, float b ) {
	refEntity_t ent;

	if ( !shader || radius <= 0 )
		return;
	memset( &ent, 0, sizeof( ent ) );
	ent.reType = RT_SPRITE;
	VectorCopy( org, ent.origin );
	ent.radius = radius;
	ent.rotation = rotation;
	ent.customShader = shader;
	ent.shaderRGBA[0] = (byte)( Com_Clamp( 0, 1, r ) * 255 );
	ent.shaderRGBA[1] = (byte)( Com_Clamp( 0, 1, g ) * 255 );
	ent.shaderRGBA[2] = (byte)( Com_Clamp( 0, 1, b ) * 255 );
	ent.shaderRGBA[3] = 255;
	ent.renderfx = RF_NOSHADOW;
	trap->R_AddRefEntityToScene( &ent );
}

static void CG_ShinobiHandOrigin( centity_t *cent, clientInfo_t *ci, vec3_t out ) {
	mdxaBone_t m;

	if ( cent->ghoul2 && ci->bolt_lhand
		&& trap->G2API_GetBoltMatrix( cent->ghoul2, 0, ci->bolt_lhand, &m, cent->turAngles, cent->lerpOrigin, cg.time, cgs.gameModels, cent->modelScale ) ) {
		out[0] = m.matrix[0][3];
		out[1] = m.matrix[1][3];
		out[2] = m.matrix[2][3];
		return;
	}
	// fallback: in front of the chest
	{
		vec3_t fwd, yaw;
		VectorSet( yaw, 0, cent->lerpAngles[YAW], 0 );
		AngleVectors( yaw, fwd, NULL, NULL );
		VectorMA( cent->lerpOrigin, 16, fwd, out );
		out[2] += 8;
	}
}

/*
Rasengan: a dense, spinning ball of chakra. Several swirl layers turning in
opposite directions at different speeds, a white-hot core and a soft halo.
*/
static void CG_ShinobiDrawRasengan( const vec3_t org, float radius, qboolean forming ) {
	const float t = cg.time * 0.001f;

	const float pulse = 1.0f + 0.06f * sinf( t * 25.0f );
	float r = radius * pulse;

	if ( forming )
		r *= 0.55f + 0.1f * sinf( t * 40.0f );

	CG_ShinobiSprite( org, r * 2.2f, 0, shinobiShaders.glow, 0.10f, 0.30f, 0.80f );          // halo
	CG_ShinobiSprite( org, r * 1.35f, t * 900.0f, shinobiShaders.swirl, 0.25f, 0.55f, 1.0f );  // outer swirl
	CG_ShinobiSprite( org, r * 1.20f, -t * 1300.0f, shinobiShaders.swirl, 0.30f, 0.65f, 1.0f );
	CG_ShinobiSprite( org, r * 1.00f, t * 1700.0f + 60, shinobiShaders.swirl, 0.45f, 0.75f, 1.0f );
	CG_ShinobiSprite( org, r * 0.85f, -t * 2300.0f + 120, shinobiShaders.swirl, 0.55f, 0.85f, 1.0f );
	CG_ShinobiSprite( org, r * 0.70f, 0, shinobiShaders.glow, 0.85f, 0.95f, 1.0f );            // core
	CG_ShinobiSprite( org, r * 0.40f, 0, shinobiShaders.glow, 1.0f, 1.0f, 1.0f );
}

/*
Chidori: a crackling knot of lightning. Bolts are re-rolled every frame so
the ball never looks the same twice.
*/
static void CG_ShinobiDrawChidori( const vec3_t org, float radius, qboolean forming ) {
	const int bolts = forming ? 3 : 7;
	const float r = forming ? radius * 0.6f : radius;
	int i;

	CG_ShinobiSprite( org, r * 2.4f, 0, shinobiShaders.glow, 0.15f, 0.30f, 0.75f );
	CG_ShinobiSprite( org, r * 0.8f, 0, shinobiShaders.glow, 0.75f, 0.85f, 1.0f );
	CG_ShinobiSprite( org, r * 0.45f, 0, shinobiShaders.glow, 1.0f, 1.0f, 1.0f );
	for ( i = 0; i < bolts; i++ ) {
		const float k = flrand( 0.6f, 1.0f );
		vec3_t p;
		VectorSet( p, org[0] + flrand( -1.0f, 1.0f ) * r * 0.3f, org[1] + flrand( -1.0f, 1.0f ) * r * 0.3f, org[2] + flrand( -1.0f, 1.0f ) * r * 0.3f );
		CG_ShinobiSprite( p, r * flrand( 1.2f, 2.4f ), flrand( 0, 360 ), shinobiShaders.bolt, 0.55f * k, 0.75f * k, 1.0f * k );
	}
}

// Chidori dash: a fading streak of lightning along the path of the hand
static void CG_ShinobiChidoriTrail( centity_t *cent, const vec3_t org, float radius ) {
	vec3_t white = { 1, 1, 1 }, blue = { 0.45f, 0.65f, 1.0f };

	if ( cent->shinobiTrailTime && cg.time - cent->shinobiTrailTime < 200
		&& Distance( cent->shinobiTrailPrev, org ) > 1.0f ) {
		trap->FX_AddLine( cent->shinobiTrailPrev, (float *)org, radius * 1.6f, radius * 0.2f, 0.0f,
			1.0f, 0.0f, 0.0f, blue, blue, 0.0f, 260, shinobiShaders.bolt, FX_SIZE_LINEAR | FX_ALPHA_LINEAR );
		trap->FX_AddLine( cent->shinobiTrailPrev, (float *)org, radius * 0.5f, 0.1f, 0.0f,
			1.0f, 0.0f, 0.0f, white, blue, 0.0f, 180, shinobiShaders.glow, FX_SIZE_LINEAR | FX_ALPHA_LINEAR );
	}
	VectorCopy( org, cent->shinobiTrailPrev );
	cent->shinobiTrailTime = cg.time;
}

void CG_ShinobiPlayerEffects( centity_t *cent, clientInfo_t *ci ) {
	entityState_t *s = &cent->currentState;
	vec3_t org, up = { 0, 0, 1 };

	if ( s->eFlags & EF_DEAD ) {
		cent->shinobiTrailTime = 0;
		return;
	}

	CG_ShinobiDrawEyes( cent, ci );

	if ( s->eFlags & SHINOBI_EF_RAIDASH )
		CG_ShinobiRaiDashForm( cent );

	if ( SHINOBI_HAND_ANY( s ) ) {
		const qboolean chidori = SHINOBI_HAND_IS_CHIDORI( s );
		const qboolean forming = SHINOBI_HAND_FORMING( s );
		int li = SHINOBI_HAND_LEVEL( s ) - 1;
		float radius;

		if ( li < 0 || li >= SHINOBI_MAX_LEVEL )
			li = 0;
		radius = chidori ? shinobiChidoriRadius[li] : shinobiRasenganRadius[li];

		CG_ShinobiHandOrigin( cent, ci, org );

		if ( chidori ) {
			const float f = 0.6f + 0.4f * flrand( 0.0f, 1.0f );
			CG_ShinobiDrawChidori( org, radius, forming );
			trap->R_AddLightToScene( org, ( 100 + 60 * li ) * f, 0.55f * f, 0.75f * f, 1.0f * f );
			if ( SHINOBI_HAND_DASH( s ) )
				CG_ShinobiChidoriTrail( cent, org, radius );
			else
				cent->shinobiTrailTime = 0;
		}
		else {
			CG_ShinobiDrawRasengan( org, radius, forming );
			trap->R_AddLightToScene( org, 90 + 50 * li, 0.3f, 0.6f, 1.0f );
		}

		// loose sparks around the hand
		if ( cent->shinobiHandFxTime <= cg.time || cent->shinobiHandFxTime > cg.time + 1000 ) {
			fxHandle_t fx;
			if ( forming )
				fx = chidori ? cgs.effects.shinobiChidoriForm : cgs.effects.shinobiRasenganForm;
			else
				fx = chidori ? cgs.effects.shinobiChidoriHand : cgs.effects.shinobiRasenganHand;
			trap->FX_PlayEffectID( fx, org, up, -1, -1, qfalse );
			cent->shinobiHandFxTime = cg.time + SHINOBI_HAND_FX_INTERVAL;
		}
	}
	else if ( !( s->eFlags & SHINOBI_EF_RAIDASH ) ) {
		cent->shinobiTrailTime = 0;
	}

	// --- element statuses (timers share shinobiAuraFxTime with the chakra aura) ---
	if ( SHINOBI_IMPRISONED( s ) ) { // floating in a ball of water
		const float t = cg.time * 0.001f;
		VectorCopy( cent->lerpOrigin, org );
		org[2] += 4;
		CG_ShinobiSpriteA( org, SHINOBI_PRISON_RADIUS, t * 20.0f, shinobiShaders.waterSphere, 0.55f, 0.75f, 1.0f, 0.55f );
		CG_ShinobiSpriteA( org, SHINOBI_PRISON_RADIUS * 0.92f, -t * 35.0f, shinobiShaders.waterSphere, 0.4f, 0.6f, 0.95f, 0.35f );
		trap->R_AddLightToScene( org, 120, 0.3f, 0.5f, 0.9f );
		if ( cent->shinobiAuraFxTime <= cg.time || cent->shinobiAuraFxTime > cg.time + 1000 ) {
			trap->FX_PlayEffectID( shinobiFx.bubbles, org, up, -1, -1, qfalse );
			cent->shinobiAuraFxTime = cg.time + 150;
		}
	}
	else if ( SHINOBI_BURNING( s ) ) {
		const float f = 0.7f + 0.3f * flrand( 0, 1 );
		VectorCopy( cent->lerpOrigin, org );
		trap->R_AddLightToScene( org, 110 * f, 1.0f * f, 0.5f * f, 0.15f * f );
		if ( cent->shinobiAuraFxTime <= cg.time || cent->shinobiAuraFxTime > cg.time + 1000 ) {
			org[2] += flrand( -18, 14 );
			trap->FX_PlayEffectID( shinobiFx.fireBody, org, up, -1, -1, qfalse );
			cent->shinobiAuraFxTime = cg.time + 60;
		}
	}
	else if ( SHINOBI_WET( s ) ) {
		if ( cent->shinobiAuraFxTime <= cg.time || cent->shinobiAuraFxTime > cg.time + 1000 ) {
			VectorCopy( cent->lerpOrigin, org );
			org[2] += flrand( -10, 20 );
			trap->FX_PlayEffectID( shinobiFx.drip, org, up, -1, -1, qfalse );
			cent->shinobiAuraFxTime = cg.time + 220;
		}
	}

	if ( SHINOBI_CHARGING( s ) ) {
		if ( cent->shinobiAuraFxTime <= cg.time || cent->shinobiAuraFxTime > cg.time + 1000 ) {
			VectorCopy( cent->lerpOrigin, org );
			org[2] -= 20; // roughly at the waist of a sitting player
			trap->FX_PlayEffectID( cgs.effects.shinobiChakraAura, org, up, -1, -1, qfalse );
			cent->shinobiAuraFxTime = cg.time + SHINOBI_AURA_FX_INTERVAL;
		}
	}
}

/*
==================
CG_ShinobiEntity

Draws mod entities that have no model. Returns qtrue if it handled the entity.

Great fireball: a dense churning ball - stacked layers of the fire texture
spinning against each other around a white-hot core, inside an orange
glow - plus a short trail of embers and smoke from an effect file.
==================
*/
/*
Water bullet: a wobbling ball of water with a spray trail.
*/
static void CG_ShinobiWaterball( centity_t *cent ) {
	const int   li = cent->currentState.generic1 - SHINOBI_ENT_WATERBALL;
	const float R = shinobiWaterVisual[li], t = cg.time * 0.001f;
	vec3_t      dir;

	CG_ShinobiRefract( cent->lerpOrigin, R * 1.35f, 210 );        // a lens of clear water
	CG_ShinobiSpriteA( cent->lerpOrigin, R * 1.1f, t * 90.0f, shinobiShaders.waterSphere, 0.6f, 0.8f, 1.0f, 0.45f );
	CG_ShinobiSprite( cent->lerpOrigin, R * 0.4f, 0, shinobiShaders.glow, 0.15f, 0.25f, 0.4f );

	VectorCopy( cent->currentState.pos.trDelta, dir );
	if ( VectorNormalize( dir ) == 0 )
		VectorSet( dir, 0, 0, 1 );
	if ( cent->shinobiHandFxTime <= cg.time || cent->shinobiHandFxTime > cg.time + 1000 ) {
		trap->FX_PlayEffectID( shinobiFx.waterTrail, cent->lerpOrigin, dir, -1, -1, qfalse );
		cent->shinobiHandFxTime = cg.time + SHINOBI_BALL_FX_INTERVAL;
	}
}

// a water bullet frozen by its caster's Wind Wall: a long white-blue icicle with a frosty
// trail; it flashes and rings out where it froze (the server restarts its trajectory there)
static void CG_ShinobiIceShard( centity_t *cent ) {
	const int   li = cent->currentState.generic1 - SHINOBI_ENT_ICESHARD;
	const float R = shinobiWaterVisual[li] * 1.3f, len = R * 4.5f;
	const int   age = cg.time - cent->currentState.pos.trTime;
	vec3_t      dir, p, tail, end, frost = { 0.8f, 0.95f, 1.0f };
	int         k;

	VectorCopy( cent->currentState.pos.trDelta, dir );
	if ( VectorNormalize( dir ) == 0 )
		VectorSet( dir, 0, 0, 1 );
	for ( k = 0; k <= 8; k++ ) { // thick at the back, a point at the front
		const float f = k / 8.0f;
		VectorMA( cent->lerpOrigin, ( f - 0.65f ) * len, dir, p );
		CG_ShinobiSpriteA( p, R * ( 0.9f - 0.8f * f ), 0, shinobiShaders.glow, 0.85f, 0.97f, 1.0f, 1.0f );
	}
	VectorMA( cent->lerpOrigin, 0.25f * len, dir, p );
	CG_ShinobiSpriteA( p, R * 0.35f, 0, shinobiShaders.glow, 1.0f, 1.0f, 1.0f, 1.0f ); // the bright tip
	CG_ShinobiSpriteA( cent->lerpOrigin, R * 1.1f, cg.time * 0.2f, shinobiShaders.waterSphere, 0.8f, 0.95f, 1.0f, 0.55f );
	// a frosty streak behind it
	VectorMA( cent->lerpOrigin, -0.65f * len, dir, tail );
	VectorMA( tail, -70.0f, dir, end );
	trap->FX_AddLine( tail, end, R * 1.2f, 0.5f, 0.0f, 0.55f, 0.0f, 0.0f, frost, frost, 0.0f, 120,
		shinobiShaders.glow, FX_SIZE_LINEAR | FX_ALPHA_LINEAR );
	trap->R_AddLightToScene( cent->lerpOrigin, 110, 0.6f, 0.85f, 1.0f );
	if ( age >= 0 && age < 350 ) { // just froze: a cold flash and a ring of frost
		const float f = age / 350.0f;
		CG_ShinobiSpriteA( cent->lerpOrigin, R * ( 2.5f + 6.0f * f ), 0, shinobiShaders.ring, 0.75f, 0.92f, 1.0f, 1.0f - f );
		CG_ShinobiSpriteA( cent->lerpOrigin, R * 3.0f * ( 1.0f - f ), 0, shinobiShaders.glow, 0.9f, 1.0f, 1.0f, 1.0f - f );
		trap->R_AddLightToScene( cent->lerpOrigin, 220 * ( 1.0f - f ), 0.7f, 0.9f, 1.0f );
	}
}

/*
Fire Breath: a widening cone of flame sprites along the stream, gushing particles.
*/
static void CG_ShinobiFlame( centity_t *cent ) {
	entityState_t *s = &cent->currentState;
	const float    len = s->angles2[ROLL], t = cg.time * 0.001f;
	vec3_t         angles, fwd, p;
	float          d;
	int            k = 0;

	VectorSet( angles, s->angles2[PITCH], s->angles2[YAW], 0 );
	AngleVectors( angles, fwd, NULL, NULL );

	for ( d = 8; d < len; d += 18, k++ ) {
		const float r = 5 + d * 0.3f;
		const float f = 0.75f + 0.25f * flrand( 0, 1 );
		VectorMA( cent->lerpOrigin, d, fwd, p );
		CG_ShinobiSprite( p, r * f, t * 300.0f + k * 57, shinobiShaders.fire, 0.85f, 0.45f + 0.2f * ( 1 - d / len ), 0.12f );
		if ( k % 2 == 0 )
			CG_ShinobiSprite( p, r * 0.6f, 0, shinobiShaders.glow, 0.6f, 0.35f, 0.08f );
	}
	CG_ShinobiSprite( cent->lerpOrigin, 8, 0, shinobiShaders.glow, 1.0f, 0.85f, 0.5f );
	VectorMA( cent->lerpOrigin, len * 0.5f, fwd, p );
	trap->R_AddLightToScene( p, 200 * ( 0.85f + 0.15f * flrand( 0, 1 ) ), 1.0f, 0.55f, 0.15f );

	if ( cent->shinobiHandFxTime <= cg.time || cent->shinobiHandFxTime > cg.time + 1000 ) {
		trap->FX_PlayEffectID( shinobiFx.flame, cent->lerpOrigin, fwd, -1, -1, qfalse );
		cent->shinobiHandFxTime = cg.time + 30;
	}
}

/*
Shadow Bind: a dark band along the floor from the binder's feet to the tip.
*/
// Lightning net (Lightning Trap with 2-3 marks): crackling strands between the kunai
/*
Wind Edge air slash: a thin bright crescent flying from the hand to where it stopped
(the first one it cut, a wall, or its reach), a faint streak behind it; a white cross
flash where it cut someone.
*/
static void CG_ShinobiWindSlash( centity_t *cent ) {
	const entityState_t *s = &cent->currentState;
	const qboolean hit = ( s->frame & 1 ) != 0;
	const float    age = (float)( cg.time - s->time );
	const float    flyMs = SHINOBI_WINDSLASH_FLY_MS * 2.2f; // (all in the knives' arc look: big, flying out)
	const float    fly = Com_Clamp( 0, 1, age / flyMs );
	const float    fade = age <= flyMs ? 1.0f
		: Com_Clamp( 0, 1, 1.0f - ( age - flyMs ) / ( SHINOBI_WINDSLASH_LIFE_MS - SHINOBI_WINDSLASH_FLY_MS - 60 ) );
	const qboolean wide = ( s->frame & 8 ) != 0; // the trench knives' finisher (no cross: it cuts many)
	const float    R = 70.0f; // (the Wind Edge in the knives' arc look; its roll is its swing's plane)
	vec3_t         start, stop, dir, ang, axis[3], pos, prev, p, core = { 0.92f, 1.0f, 1.0f }, haze = { 0.45f, 0.85f, 1.0f };
	float          len;
	int            k;
	const int      N = 14;

	if ( age < 0 || fade <= 0 )
		return;
	VectorCopy( s->pos.trBase, start );
	VectorCopy( s->origin2, stop );
	VectorSubtract( stop, start, dir );
	len = VectorNormalize( dir );
	if ( len < 1 )
		AngleVectors( cent->lerpAngles, dir, NULL, NULL );
	vectoangles( dir, ang );
	ang[ROLL] = s->angles2[2];
	AnglesToAxis( ang, axis ); // [0] along the flight, [1] left, [2] up - tilted
	VectorMA( start, len * fly, dir, pos );

	// the streak it leaves
	trap->FX_AddLine( start, pos, 1.2f, 0.4f, 0.0f, 0.22f * fade, 0.0f, 0.0f, haze, haze, 0.0f, 1,
		shinobiShaders.glow, FX_SIZE_LINEAR | FX_ALPHA_LINEAR );

	// the crescent: a dark, dense band across the flight, its tips swept back (a sword trail
	// cut loose), two fainter ones following it; a fine light line along its cutting edge
	{
		static const float ghostBack[3] = { 0.0f, 9.0f, 18.0f }, ghostAlpha[3] = { 1.0f, 0.45f, 0.2f };
		int g;
		(void)haze;
		for ( g = 2; g >= 0; g-- ) {
			const float r = R * ( 1.0f - 0.08f * g ), thick = r * 0.5f;
			const int   alpha = (int)( 235.0f * fade * ghostAlpha[g] );
			vec3_t      c, outPrev, inPrev, out, in;
			if ( alpha <= 0 )
				continue;
			VectorMA( pos, -ghostBack[g] * fly, dir, c );
			for ( k = 0; k <= N; k++ ) {
				const float a = DEG2RAD( -75.0f + 150.0f * k / N );
				const float edge = Com_Clamp( 0.15f, 1.0f, 1.0f - fabsf( (float)k / N - 0.5f ) * 1.7f ); // thick in the middle
				float       nf = -cosf( a ), nl = -0.9f * sinf( a ), nn = sqrtf( nf * nf + nl * nl );
				VectorMA( c, -r * ( 1.0f - cosf( a ) ) * 0.9f, axis[0], out );
				VectorMA( out, r * sinf( a ), axis[1], out );
				nf /= nn; nl /= nn; // inward, across the band
				VectorMA( out, thick * edge * nf, axis[0], in );
				VectorMA( in, thick * edge * nl, axis[1], in );
				if ( k ) {
					polyVert_t v[4];
					int        m;
					VectorCopy( outPrev, v[0].xyz ); VectorCopy( out, v[1].xyz );
					VectorCopy( in, v[2].xyz );      VectorCopy( inPrev, v[3].xyz );
					v[0].st[0] = v[3].st[0] = (float)( k - 1 ) / N;
					v[1].st[0] = v[2].st[0] = (float)k / N;
					v[0].st[1] = v[1].st[1] = 0.0f;
					v[2].st[1] = v[3].st[1] = 1.0f;
					for ( m = 0; m < 4; m++ ) {
						v[m].modulate[0] = 16; v[m].modulate[1] = 22; v[m].modulate[2] = 30;
						v[m].modulate[3] = (byte)alpha;
					}
					trap->R_AddPolysToScene( shinobiShaders.windArc, 4, v, 1 );
					if ( !g )
						trap->FX_AddLine( outPrev, out, 0.9f * edge + 0.3f, 0.5f, 0.0f, 0.55f * fade, 0.0f, 0.0f, core, core, 0.0f, 1,
							shinobiShaders.glow, FX_SIZE_LINEAR | FX_ALPHA_LINEAR );
				}
				VectorCopy( out, outPrev );
				VectorCopy( in, inPrev );
			}
		}
		(void)prev; (void)p;
	}

	if ( hit && fly >= 1.0f && !wide ) { // it cut someone: a white cross where it struck
		const float f = Com_Clamp( 0, 1, 1.0f - ( age - SHINOBI_WINDSLASH_FLY_MS ) / 220.0f );
		vec3_t      a0, a1, b0, b1, c, white = { 1, 1, 1 };
		if ( f > 0 ) {
			VectorMA( stop, -6, dir, c );
			VectorMA( c, 14 * f + 6, axis[1], a0 ); VectorMA( a0, 14 * f + 6, axis[2], a0 );
			VectorMA( c, -( 14 * f + 6 ), axis[1], a1 ); VectorMA( a1, -( 14 * f + 6 ), axis[2], a1 );
			VectorMA( c, 14 * f + 6, axis[1], b0 ); VectorMA( b0, -( 14 * f + 6 ), axis[2], b0 );
			VectorMA( c, -( 14 * f + 6 ), axis[1], b1 ); VectorMA( b1, 14 * f + 6, axis[2], b1 );
			trap->FX_AddLine( a0, a1, 2.0f, 1.0f, 0.0f, f, 0.0f, 0.0f, white, white, 0.0f, 1, shinobiShaders.glow, FX_SIZE_LINEAR | FX_ALPHA_LINEAR );
			trap->FX_AddLine( b0, b1, 2.0f, 1.0f, 0.0f, f, 0.0f, 0.0f, white, white, 0.0f, 1, shinobiShaders.glow, FX_SIZE_LINEAR | FX_ALPHA_LINEAR );
			CG_ShinobiSpriteA( c, 10 * f + 4, 0, shinobiShaders.glow, 0.8f, 0.95f, 1.0f, 0.8f * f );
		}
	}
}

/*
Blade combo finishers (SHINOBI_ENT_FINISHER): what each one looks like, for
SHINOBI_FINISHER_FX_MS, on the one hit (and the attacker, for the drain).
*/
static void CG_ShinobiFxLine( const vec3_t a, const vec3_t b, float w, float alpha, const vec3_t rgb ) {
	trap->FX_AddLine( (float *)a, (float *)b, w, w * 0.5f, 0.0f, alpha, 0.0f, 0.0f, (float *)rgb, (float *)rgb, 0.0f, 1,
		shinobiShaders.glow, FX_SIZE_LINEAR | FX_ALPHA_LINEAR );
}

static void CG_ShinobiFinisher( centity_t *cent ) {
	const entityState_t *s = &cent->currentState;
	const float age = (float)( cg.time - s->time ), f = age / SHINOBI_FINISHER_FX_MS, a = 1.0f - f;
	vec3_t      vic, att, dir, side, up = { 0, 0, 1 }, p, q;
	int         k;

	if ( age < 0 || f >= 1.0f )
		return;
	VectorCopy( s->pos.trBase, vic );
	VectorCopy( s->origin2, att );
	if ( s->otherEntityNum < MAX_GENTITIES && cg_entities[s->otherEntityNum].currentValid )
		VectorCopy( cg_entities[s->otherEntityNum].lerpOrigin, vic );
	if ( s->otherEntityNum2 < MAX_GENTITIES && cg_entities[s->otherEntityNum2].currentValid )
		VectorCopy( cg_entities[s->otherEntityNum2].lerpOrigin, att );
	VectorSubtract( vic, att, dir );
	dir[2] = 0;
	if ( VectorNormalize( dir ) < 0.01f )
		VectorSet( dir, 1, 0, 0 );
	VectorSet( side, -dir[1], dir[0], 0 );

	switch ( s->frame ) {
	case SFX_DRAIN: { // chakra pulled out of him into the attacker: blue motes along the line
		const vec3_t blue = { 0.3f, 0.6f, 1.0f };
		vec3_t from, to;
		VectorCopy( vic, from ); from[2] += 20;
		VectorCopy( att, to ); to[2] += 20;
		CG_ShinobiFxLine( from, to, 1.5f, 0.35f * a, blue );
		for ( k = 0; k < 12; k++ ) {
			const float t = fmodf( f * 1.8f + k / 12.0f, 1.0f );
			VectorSubtract( to, from, q );
			VectorMA( from, t, q, p );
			p[2] += 6 * sinf( ( t * 6 + k ) * 2.1f );
			VectorMA( p, 5 * cosf( ( t * 5 + k ) * 1.7f ), side, p );
			CG_ShinobiSpriteA( p, 4.0f + 2.0f * t, 0, shinobiShaders.glow, blue[0], blue[1], blue[2], 0.9f * a );
		}
		trap->R_AddLightToScene( to, 120 * a, 0.3f, 0.5f, 1.0f );
		break;
	}
	case SFX_BLEED: { // a spray of blood out of the wound
		for ( k = 0; k < 10; k++ ) {
			const float ang = DEG2RAD( k * 36.0f + s->number * 13 );
			const float r = 34.0f * f;
			VectorCopy( vic, p );
			p[0] += cosf( ang ) * r;
			p[1] += sinf( ang ) * r;
			p[2] += 14 + 24 * f - 46 * f * f;
			CG_ShinobiSpriteA( p, 3.0f + ( k & 1 ), 0, shinobiShaders.glow, 0.7f, 0.02f, 0.02f, 0.95f * a );
		}
		VectorCopy( vic, p ); p[2] += 14;
		CG_ShinobiSpriteA( p, 14.0f * a + 4.0f, 0, shinobiShaders.glow, 0.6f, 0.0f, 0.0f, 0.6f * a );
		break;
	}
	case SFX_IAI: { // one clean white cut through him, then a cross
		const vec3_t white = { 1, 1, 1 };
		const float  L = 46.0f * ( f < 0.2f ? f / 0.2f : 1.0f );
		VectorCopy( vic, p ); p[2] += 16;
		VectorMA( p, L, side, q ); q[2] += L * 0.5f;
		VectorMA( p, -L, side, p ); p[2] -= L * 0.5f;
		CG_ShinobiFxLine( p, q, 3.0f, a, white );
		if ( f > 0.15f ) {
			VectorCopy( vic, p ); p[2] += 16;
			VectorMA( p, L, side, q ); q[2] -= L * 0.5f;
			VectorMA( p, -L, side, p ); p[2] += L * 0.5f;
			CG_ShinobiFxLine( p, q, 2.0f, a, white );
		}
		VectorCopy( vic, p ); p[2] += 16;
		if ( f < 0.4f )
			CG_ShinobiSpriteA( p, 22.0f * ( 1.0f - f / 0.4f ), 0, shinobiShaders.glow, 1, 1, 1, 0.9f );
		break;
	}
	case SFX_FLURRY: { // three quick cuts one after another, at odd angles
		const vec3_t white = { 0.95f, 0.98f, 1.0f };
		for ( k = 0; k < 3; k++ ) {
			const float t0 = k * SHINOBI_SWORD_FLURRY_MS, t = ( age - t0 ) / 220.0f;
			const float ang = DEG2RAD( ( s->number * 61 + k * 70 ) % 180 - 90 );
			if ( t < 0 || t > 1 )
				continue;
			VectorCopy( vic, p ); p[2] += 16;
			VectorMA( p, 28 * cosf( ang ), side, q ); q[2] += 28 * sinf( ang );
			VectorMA( p, -28 * cosf( ang ), side, p ); p[2] -= 28 * sinf( ang );
			CG_ShinobiFxLine( p, q, 2.2f, 1.0f - t, white );
		}
		break;
	}
	case SFX_SWEEP: { // a low arc at his feet
		const vec3_t c = { 0.85f, 0.95f, 1.0f };
		vec3_t prev;
		for ( k = 0; k <= 8; k++ ) {
			const float ang = DEG2RAD( -80 + 160.0f * k / 8 );
			VectorCopy( vic, p );
			p[2] += DEFAULT_MINS_2 + 6;
			VectorMA( p, -30 * cosf( ang ), dir, p );
			VectorMA( p, 30 * sinf( ang ), side, p );
			if ( k )
				CG_ShinobiFxLine( prev, p, 2.4f, a, c );
			VectorCopy( p, prev );
		}
		break;
	}
	case SFX_SPIN: { // a full circle around the attacker, opening out
		const vec3_t c = { 0.85f, 0.95f, 1.0f };
		const float  r = 40 + ( SHINOBI_SWORD_SPIN_RADIUS - 40 ) * ( f < 0.4f ? f / 0.4f : 1.0f );
		vec3_t prev;
		for ( k = 0; k <= 16; k++ ) {
			const float ang = DEG2RAD( 360.0f * k / 16 );
			VectorSet( p, att[0] + cosf( ang ) * r, att[1] + sinf( ang ) * r, att[2] + 4 );
			if ( k )
				CG_ShinobiFxLine( prev, p, 2.0f, 0.9f * a, c );
			VectorCopy( p, prev );
		}
		break;
	}
	case SFX_LAUNCH: { // a rising streak under him
		const vec3_t c = { 0.85f, 0.95f, 1.0f };
		VectorCopy( vic, p ); p[2] += DEFAULT_MINS_2;
		VectorMA( p, 70 * ( f < 0.3f ? f / 0.3f : 1.0f ), up, q );
		CG_ShinobiFxLine( p, q, 3.0f, a, c );
		break;
	}
	case SFX_BURN: { // a red cut that bursts into flame on him
		const vec3_t red = { 1.0f, 0.35f, 0.1f };
		VectorCopy( vic, p ); p[2] += 16;
		VectorMA( p, 30, side, q ); q[2] += 18;
		VectorMA( p, -30, side, p ); p[2] -= 18;
		CG_ShinobiFxLine( p, q, 3.0f, a, red );
		for ( k = 0; k < 8; k++ ) {
			const float ang = DEG2RAD( k * 45.0f + s->number * 7 ), r = 10 + 22 * f;
			VectorSet( p, vic[0] + cosf( ang ) * r, vic[1] + sinf( ang ) * r, vic[2] + 4 + 40 * f );
			CG_ShinobiSpriteA( p, 6.0f * a + 2.0f, 0, shinobiShaders.glow, 1.0f, 0.45f + 0.3f * ( k & 1 ), 0.1f, 0.9f * a );
		}
		trap->R_AddLightToScene( vic, 160 * a, 1.0f, 0.5f, 0.15f );
		break;
	}
	case SFX_BLAST: { // the blast (the game plays the explosion): a flash and a ring of heat
		const vec3_t c = { 1.0f, 0.7f, 0.3f };
		vec3_t prev;
		const float r = 20 + 70 * ( f < 0.5f ? f / 0.5f : 1.0f );
		for ( k = 0; k <= 16; k++ ) {
			const float ang = DEG2RAD( 360.0f * k / 16 );
			VectorSet( p, vic[0] + cosf( ang ) * r, vic[1] + sinf( ang ) * r, vic[2] + DEFAULT_MINS_2 + 4 );
			if ( k )
				CG_ShinobiFxLine( prev, p, 3.0f, a, c );
			VectorCopy( p, prev );
		}
		trap->R_AddLightToScene( vic, 260 * a, 1.0f, 0.6f, 0.2f );
		break;
	}
	case SFX_STUN: { // a fine needle through him, then little stars circling his head
		const vec3_t white = { 1, 1, 1 };
		if ( f < 0.25f ) {
			VectorCopy( vic, p ); p[2] += 18;
			VectorMA( p, -40 + 80 * ( f / 0.25f ), dir, q );
			VectorMA( p, -40, dir, p );
			CG_ShinobiFxLine( p, q, 1.5f, 1.0f, white );
		}
		for ( k = 0; k < 3; k++ ) {
			const float ang = cg.time * 0.008f + k * 2.094f;
			VectorSet( p, vic[0] + cosf( ang ) * 14, vic[1] + sinf( ang ) * 14, vic[2] + DEFAULT_MAXS_2 + 8 );
			CG_ShinobiSpriteA( p, 3.0f, 0, shinobiShaders.glow, 1.0f, 0.95f, 0.5f, a );
		}
		break;
	}
	default:
		break;
	}
}

static void CG_ShinobiRaiNet( centity_t *cent ) {
	static const int sides[3][2] = { { 0, 1 }, { 1, 2 }, { 2, 0 } };
	const int count = cent->currentState.frame & 3, lit = cent->currentState.frame >> 2;
	vec3_t    node[3], rgb = { 0.55f, 0.75f, 1.0f };
	qboolean  spark = qfalse;
	int       i, strand;

	VectorCopy( cent->lerpOrigin, node[0] );
	VectorCopy( cent->currentState.origin2, node[1] );
	VectorCopy( cent->currentState.angles2, node[2] );
	if ( cg.time >= cent->trailTime ) { // new strands ~25 times a second, each living a little longer
		cent->trailTime = cg.time + 40;
		spark = qtrue;
	}
	for ( i = 0; i < ( count == 3 ? 3 : 1 ); i++ ) {
		const float *a = node[sides[i][0]], *b = node[sides[i][1]];
		vec3_t mid;
		if ( !( lit & ( 1 << i ) ) )
			continue;
		VectorAdd( a, b, mid );
		VectorScale( mid, 0.5f, mid );
		trap->R_AddLightToScene( mid, 90 + 30 * sinf( cg.time * 0.05f + i ), 0.45f, 0.6f, 1.0f );
		if ( !spark )
			continue;
		for ( strand = 0; strand < 2; strand++ ) {
			addElectricityArgStruct_t p;
			VectorCopy( a, p.start );
			VectorCopy( b, p.end );
			p.size1 = strand ? 1.5f : 3.0f;
			p.size2 = strand ? 1.0f : 2.0f;
			p.sizeParm = 0.0f;
			p.alpha1 = 1.0f;
			p.alpha2 = 0.4f;
			p.alphaParm = 0.0f;
			VectorCopy( rgb, p.sRGB );
			VectorCopy( rgb, p.eRGB );
			p.rgbParm = 0.0f;
			p.chaos = strand ? 4.0f : 1.5f;
			p.killTime = 90;
			p.shader = cgs.media.boltShader;
			p.flags = 0x00000001 | 0x00000100 | ( strand ? 0x02000000 : 0 ); // alpha / size linear, branching
			trap->FX_AddElectricity( &p );
		}
	}
}

static void CG_ShinobiShadow( centity_t *cent ) {
	const float *tip = cent->currentState.angles2;
	vec3_t       d, side, a, b;
	float        len;
	int          n, i;

	VectorSubtract( tip, cent->lerpOrigin, d );
	len = VectorLength( d );
	if ( len < 4 ) {
		CG_ShinobiGroundDecal( cent->lerpOrigin, 16, 0, shinobiShaders.scorch, 0, 0, 0, 0.8f );
		return;
	}
	VectorSet( side, -d[1], d[0], 0 );
	VectorNormalize( side );
	VectorScale( side, SHINOBI_SHADOW_WIDTH * 0.5f, side );
	n = (int)( len / 24 ) + 1;

	for ( i = 0; i < n; i++ ) {
		polyVert_t v[4];
		int        k;
		VectorMA( cent->lerpOrigin, (float)i / n, d, a );
		VectorMA( cent->lerpOrigin, (float)( i + 1 ) / n, d, b );
		VectorSubtract( a, side, v[0].xyz );
		VectorAdd( a, side, v[1].xyz );
		VectorAdd( b, side, v[2].xyz );
		VectorSubtract( b, side, v[3].xyz );
		v[0].st[0] = v[1].st[0] = (float)i / n * len / 64.0f;
		v[2].st[0] = v[3].st[0] = (float)( i + 1 ) / n * len / 64.0f;
		v[0].st[1] = v[3].st[1] = 0;
		v[1].st[1] = v[2].st[1] = 1;
		for ( k = 0; k < 4; k++ ) {
			v[k].xyz[2] += 1.5f;
			v[k].modulate[0] = v[k].modulate[1] = v[k].modulate[2] = 0;
			v[k].modulate[3] = 230;
		}
		trap->R_AddPolysToScene( shinobiShaders.shadowBand, 4, v, 1 );
	}
	CG_ShinobiGroundDecal( tip, 20, 0, shinobiShaders.scorch, 0, 0, 0, 0.9f );
}

/*
Water Wave: a row of churning water blobs across its width, spray on the crest.
*/
static void CG_ShinobiWave( centity_t *cent ) {
	// a rolling wall of water: a breaking-wave profile swept across the width,
	// bobbing, lower at the ends; its crest throws spray
	static const float prof[][2] = { // forward offset, height (fraction of H), alpha
		{ -72, 0.0f }, { -46, 0.3f }, { -22, 0.68f }, { -4, 0.95f }, { 9, 1.0f },
		{ 19, 0.84f }, { 23, 0.56f }, { 16, 0.22f }, { 5, 0.0f } };
	enum { NP = ARRAY_LEN( prof ), MAXSEG = 24 };
	static polyVert_t quads[MAXSEG * ( NP - 1 ) * 4];
	entityState_t *s = &cent->currentState;
	const float    half = s->angles2[0], H = (float)SHINOBI_WAVE_HEIGHT, t = cg.time * 0.001f;
	vec3_t         angles, fwd, right, pos, grid[MAXSEG + 1][NP];
	float          arc[NP], alpha[NP];
	int            nseg, i, j, q = 0;

	VectorSet( angles, 0, s->angles2[1], 0 );
	AngleVectors( angles, fwd, right, NULL );
	nseg = (int)( 2 * half / 20.0f );
	if ( nseg < 4 ) nseg = 4;
	if ( nseg > MAXSEG ) nseg = MAXSEG;

	for ( i = 0, arc[0] = 0; i < NP; i++ ) {
		if ( i )
			arc[i] = arc[i - 1] + sqrtf( Square( prof[i][0] - prof[i - 1][0] ) + Square( ( prof[i][1] - prof[i - 1][1] ) * H ) );
		alpha[i] = 0.45f + 0.45f * prof[i][1]; // see-through at the foot, solid at the crest
	}
	for ( j = 0; j <= nseg; j++ ) {
		const float x = -half + 2 * half * j / nseg;
		const float e = fabs( x ) / ( half > 1 ? half : 1 );
		const float taper = 1.0f - 0.85f * e * e * e * e;
		const float bob = 1.0f + 0.1f * sinf( t * 7.0f + j * 0.9f );
		for ( i = 0; i < NP; i++ ) {
			VectorMA( cent->lerpOrigin, x, right, pos );
			VectorMA( pos, prof[i][0] * ( 0.55f + 0.45f * taper ), fwd, pos );
			pos[2] += prof[i][1] * H * taper * bob + 1.0f;
			VectorCopy( pos, grid[j][i] );
		}
	}
	for ( j = 0; j < nseg; j++ ) {
		for ( i = 0; i < NP - 1; i++ ) {
			const int   jj[4] = { j, j + 1, j + 1, j }, ii[4] = { i, i, i + 1, i + 1 };
			int         k;
			for ( k = 0; k < 4; k++ ) {
				polyVert_t *v = &quads[q++];
				const float x = -half + 2 * half * jj[k] / nseg;
				VectorCopy( grid[jj[k]][ii[k]], v->xyz );
				v->st[0] = x / 96.0f;
				v->st[1] = arc[ii[k]] / 64.0f - t * 0.6f; // the water runs up the face
				v->modulate[0] = v->modulate[1] = v->modulate[2] = (byte)( 45 + 60 * prof[ii[k]][1] ); // the sheen
				v->modulate[3] = (byte)( alpha[ii[k]] * 255 );
			}
		}
	}
	trap->R_AddPolysToScene( shinobiShaders.puddleWater, 4, quads, nseg * ( NP - 1 ) );

	if ( cent->shinobiHandFxTime <= cg.time || cent->shinobiHandFxTime > cg.time + 1000 ) {
		VectorMA( cent->lerpOrigin, flrand( -half * 0.8f, half * 0.8f ), right, pos );
		VectorMA( pos, 6, fwd, pos );
		pos[2] += H * 0.9f;
		trap->FX_PlayEffectID( shinobiFx.waveCrest, pos, fwd, -1, -1, qfalse );
		cent->shinobiHandFxTime = cg.time + 30;
	}
}

/*
Wind Blade: a pale arc of wind spreading out from where it was cast.
*/
static void CG_ShinobiBlade( centity_t *cent ) {
	entityState_t *s = &cent->currentState;
	const int      li = s->generic1 - SHINOBI_ENT_BLADE;
	const float    range = (float)shinobiBladeRange[li];
	const float    R = ( cg.time - s->time ) * ( SHINOBI_BLADE_SPEED / 1000.0f );
	const float    halfAng = RAD2DEG( acosf( SHINOBI_BLADE_ARC ) );
	const float    fade = 1.0f - R / range;
	vec3_t         core = { 0.85f, 0.95f, 1.0f }, haze = { 0.5f, 0.6f, 0.65f };
	int            k;
	const int      N = 12;

	if ( R <= 8 || R > range )
		return;
	for ( k = 0; k < N; k++ ) {
		const float a0 = DEG2RAD( s->angles2[0] - halfAng + 2 * halfAng * k / N );
		const float a1 = DEG2RAD( s->angles2[0] - halfAng + 2 * halfAng * ( k + 1 ) / N );
		const float edge = ( k == 0 || k == N - 1 ) ? 0.5f : 1.0f; // thinner at the tips
		vec3_t      p0, p1, q0, q1;

		VectorSet( p0, cent->lerpOrigin[0] + cosf( a0 ) * R, cent->lerpOrigin[1] + sinf( a0 ) * R, cent->lerpOrigin[2] );
		VectorSet( p1, cent->lerpOrigin[0] + cosf( a1 ) * R, cent->lerpOrigin[1] + sinf( a1 ) * R, cent->lerpOrigin[2] );
		trap->FX_AddLine( p0, p1, ( 8 + 3 * li ) * edge, 2.0f, 0.0f, 0.9f * fade, 0.0f, 0.0f,
			core, core, 0.0f, 60, shinobiShaders.glow, FX_SIZE_LINEAR | FX_ALPHA_LINEAR );
		trap->FX_AddLine( p0, p1, ( 34 + 8 * li ) * edge, 10.0f, 0.0f, 0.35f * fade, 0.0f, 0.0f,
			haze, haze, 0.0f, 90, shinobiShaders.glow, FX_SIZE_LINEAR | FX_ALPHA_LINEAR );

		// a fainter echo just behind the edge
		VectorSet( q0, cent->lerpOrigin[0] + cosf( a0 ) * ( R - 28 ), cent->lerpOrigin[1] + sinf( a0 ) * ( R - 28 ), cent->lerpOrigin[2] + 6 );
		VectorSet( q1, cent->lerpOrigin[0] + cosf( a1 ) * ( R - 28 ), cent->lerpOrigin[1] + sinf( a1 ) * ( R - 28 ), cent->lerpOrigin[2] + 6 );
		trap->FX_AddLine( q0, q1, ( 5 + 2 * li ) * edge, 1.0f, 0.0f, 0.45f * fade, 0.0f, 0.0f,
			haze, haze, 0.0f, 60, shinobiShaders.glow, FX_SIZE_LINEAR | FX_ALPHA_LINEAR );
	}
}

/*
Ground zones: a decal fading in and out, plus particles spawned over the
area at a rate that grows with its size.
*/
static void CG_ShinobiZone( centity_t *cent ) {
	entityState_t *s = &cent->currentState;
	const int      type = s->generic1 - SHINOBI_ENT_ZONE;
	const float    R = s->angles2[0];
	const float    fadeIn = Com_Clamp( 0, 1, ( cg.time - s->time ) / 400.0f );
	const float    fadeOut = s->time2 ? Com_Clamp( 0, 1, ( s->time2 - cg.time ) / 800.0f ) : 1.0f;
	const float    a = fadeIn * fadeOut;
	const float    rot = (float)( s->number * 47 % 360 ); // stable per zone
	const float    area = ( R * R ) / ( 100.0f * 100.0f );
	vec3_t         up = { 0, 0, 1 }, org, p;
	int            interval = 0;
	fxHandle_t     fx = 0;

	if ( R <= 0 || a <= 0 )
		return;
	VectorCopy( cent->lerpOrigin, org );

	switch ( type ) {
	case SZ_FIRE: {
		const float f = 0.75f + 0.25f * flrand( 0, 1 );
		CG_ShinobiGroundDecal( org, R, rot, shinobiShaders.scorch, 1, 1, 1, a );
		p[0] = org[0]; p[1] = org[1]; p[2] = org[2] + 24;
		trap->R_AddLightToScene( p, R * 1.6f * f * a, 1.0f, 0.5f, 0.15f );
		fx = shinobiFx.fireGround;
		interval = (int)( 40 / ( area < 0.4f ? 0.4f : area ) );
		break;
	}
	case SZ_WATER:
		CG_ShinobiPuddle( org, R, s->number, a, 0.6f );
		break;
	case SZ_ELECTRIC: {
		const float f = flrand( 0.5f, 1.0f );
		CG_ShinobiPuddle( org, R, s->number, a, 1.0f );
		p[0] = org[0]; p[1] = org[1]; p[2] = org[2] + 16;
		trap->R_AddLightToScene( p, R * 1.4f * f * a, 0.5f, 0.7f, 1.0f );
		fx = shinobiFx.zoneSpark;
		interval = (int)( 70 / ( area < 0.4f ? 0.4f : area ) );
		break;
	}
	case SZ_MUD:
		CG_ShinobiGroundDecal( org, R, rot, shinobiShaders.mud, 1, 1, 1, a );
		break;
	case SZ_ICE: // a frozen puddle: pale, glinting
		VectorSet( p, org[0], org[1], org[2] - 4 );
		if ( CG_PointContents( p, ENTITYNUM_NONE ) & ( CONTENTS_WATER | CONTENTS_SLIME ) ) {
			// a floe on a lake: lifted clear of the (waving) surface and drawn after it, or the
			// water covers it and the two flicker
			org[2] += 3;
			CG_ShinobiGroundDecal( org, R, rot, shinobiShaders.iceFloe, 0.85f, 0.95f, 1.0f, 0.95f * a );
		}
		else
			CG_ShinobiGroundDecal( org, R, rot, shinobiShaders.ice, 0.85f, 0.95f, 1.0f, 0.95f * a );
		{ // a few glints that come and go
			int k;
			for ( k = 0; k < 4; k++ ) {
				const int   seed = s->number * 31 + k * 97 + cg.time / 600;
				const float ang = ( seed * 2.399963f ), d = R * 0.8f * ( ( seed * 7919 ) % 100 ) / 100.0f;
				const float tw = 0.5f + 0.5f * sinf( cg.time * 0.012f + k * 1.7f );
				VectorSet( p, org[0] + cosf( ang ) * d, org[1] + sinf( ang ) * d, org[2] + 2 );
				CG_ShinobiSpriteA( p, 2.5f * tw, 0, shinobiShaders.glow, 0.9f, 0.95f, 1.0f, 0.8f * tw * a );
			}
		}
		break;
	case SZ_DUST: // loose earth: sand on the floor, a puff of dust now and then
		CG_ShinobiGroundDecal( org, R, rot, shinobiShaders.sand, 1, 1, 1, 0.9f * a );
		fx = shinobiFx.quakeDust;
		interval = (int)( 900 / ( area < 0.3f ? 0.3f : area ) );
		if ( interval < 160 )
			interval = 160; // a wide quake field: still only a few puffs
		break;
	case SZ_STEAM:
		fx = shinobiFx.steam;
		interval = (int)( 110 / ( area < 0.4f ? 0.4f : area ) );
		break;
	case SZ_MIST:
		fx = shinobiFx.mist;
		interval = (int)( 400 / area );
		break;
	case SZ_ASH:
		CG_ShinobiGroundDecal( org, R * 0.8f, rot, shinobiShaders.scorch, 1, 1, 1, 0.5f * a );
		fx = shinobiFx.ash;
		interval = (int)( 45 / ( area < 0.4f ? 0.4f : area ) );
		break;
	case SZ_POISON: // a tall, thick dark violet cloud (no fog on the screen: it hurts, it doesn't hide)
		CG_ShinobiGroundDecal( org, R * 0.85f, rot, shinobiShaders.mud, 0.35f, 0.2f, 0.45f, 0.5f * a );
		fx = shinobiFx.poison;
		interval = (int)( 28 / ( area < 0.4f ? 0.4f : area ) );
		break;
	default:
		break;
	}

	// standing in ash or steam: you can hardly see
	if ( type == SZ_ASH || type == SZ_STEAM || type == SZ_MIST ) {
		vec3_t d;
		VectorSubtract( cg.refdef.vieworg, org, d );
		if ( d[2] > -16 && d[2] < SHINOBI_ZONE_HEIGHT + ( type == SZ_MIST ? 96 : 48 ) ) {
			float dist;
			d[2] = 0;
			dist = VectorLength( d );
			if ( dist < R ) {
				const float amt = a * Com_Clamp( 0, 1, ( R - dist ) / ( type == SZ_MIST ? 90.0f : 40.0f ) );
				if ( type == SZ_MIST ) { // drawn in the 3D scene (CG_ShinobiSceneOverlay), so shapes can show over it
					const qboolean mine = s->otherEntityNum == cg.snap->ps.clientNum;
					if ( shinobiMist.time != cg.time ) {
						shinobiMist.mine = qfalse;
						shinobiMist.amount = 0;
					}
					shinobiMist.time = cg.time;
					if ( amt > shinobiMist.amount )
						shinobiMist.amount = amt;
					if ( mine )
						shinobiMist.mine = qtrue;
				}
				else if ( shinobiFog.time != cg.time || amt > shinobiFog.amount ) {
					shinobiFog.time = cg.time;
					shinobiFog.type = type;
					shinobiFog.amount = amt;
				}
			}
		}
	}

	if ( fx && fadeOut > 0.3f
		&& ( cent->shinobiHandFxTime <= cg.time || cent->shinobiHandFxTime > cg.time + 1000 ) ) {
		CG_ShinobiZonePoint( org, R, p );
		if ( type == SZ_POISON )
			p[2] += flrand( 0, SHINOBI_POISON_HEIGHT * 0.8f ); // all the way up
		trap->FX_PlayEffectID( fx, p, up, -1, -1, qfalse );
		cent->shinobiHandFxTime = cg.time + ( interval < 15 ? 15 : interval );
	}
}

/*
Earthquake: a faint circle shows the whole area at once; cracks (a ring of
dust) race out to it; then the ground heaves, the camera shakes for those
standing in it, and the dust settles.
*/
static void CG_ShinobiQuake( centity_t *cent ) {
	entityState_t *s = &cent->currentState;
	const float    R = s->angles2[0];
	const int      t0 = s->time, t1 = s->time2;
	vec3_t         up = { 0, 0, 1 }, p;

	if ( R <= 0 || t1 <= t0 )
		return;
	if ( cg.time < t1 ) {
		const float f = Com_Clamp( 0.05f, 1, (float)( cg.time - t0 ) / ( t1 - t0 ) );
		CG_ShinobiGroundDecal( cent->lerpOrigin, R, 0, shinobiShaders.ring, 0.35f, 0.25f, 0.12f, 0.5f );       // the area
		CG_ShinobiGroundDecal( cent->lerpOrigin, R * f, cg.time * 0.05f, shinobiShaders.ring, 0.8f, 0.55f, 0.25f, 1.0f ); // the cracks
		CG_ShinobiGroundDecal( cent->lerpOrigin, R * f, 0, shinobiShaders.mud, 0.7f, 0.6f, 0.5f, 0.6f * f );
		if ( cent->shinobiHandFxTime <= cg.time || cent->shinobiHandFxTime > cg.time + 1000 ) {
			const float a = flrand( 0, 2 * M_PI );
			VectorSet( p, cent->lerpOrigin[0] + cosf( a ) * R * f, cent->lerpOrigin[1] + sinf( a ) * R * f, cent->lerpOrigin[2] + 4 );
			trap->FX_PlayEffectID( shinobiFx.quakeDust, p, up, -1, -1, qfalse );
			cent->shinobiHandFxTime = cg.time + 25;
		}
		return;
	}
	{ // the strike: cracked ground fading, dust settling
		const float fade = Com_Clamp( 0, 1, 1.0f - (float)( cg.time - t1 ) / SHINOBI_QUAKE_LINGER );
		CG_ShinobiGroundDecal( cent->lerpOrigin, R, 0, shinobiShaders.mud, 0.75f, 0.65f, 0.55f, 0.8f * fade );
		if ( cent->shinobiAuraFxTime != t1 ) { // once: shake whoever stands in it
			vec3_t d;
			cent->shinobiAuraFxTime = t1;
			VectorSubtract( cg.predictedPlayerState.origin, cent->lerpOrigin, d );
			if ( fabs( d[2] ) < 160 ) {
				d[2] = 0;
				if ( VectorLength( d ) < R * 1.4f )
					CGCam_Shake( 2.0f + 4.0f * R / 480.0f, 700 );
			}
		}
		if ( fade > 0.4f && ( cent->shinobiHandFxTime <= cg.time || cent->shinobiHandFxTime > cg.time + 1000 ) ) {
			const float a = flrand( 0, 2 * M_PI ), r = flrand( 0, R );
			VectorSet( p, cent->lerpOrigin[0] + cosf( a ) * r, cent->lerpOrigin[1] + sinf( a ) * r, cent->lerpOrigin[2] + 4 );
			trap->FX_PlayEffectID( shinobiFx.quakeDust, p, up, -1, -1, qfalse );
			cent->shinobiHandFxTime = cg.time + 20;
		}
	}
}

/*
Wind Wall: pale rising streaks of air standing across the caster's front.
*/
static void CG_ShinobiWindWall( centity_t *cent ) {
	entityState_t *s = &cent->currentState;
	const float    halfW = s->angles2[1];
	const float    life = (float)( s->time2 - s->time );
	const float    fadeIn = Com_Clamp( 0, 1, ( cg.time - s->time ) / 200.0f );
	const float    fadeOut = Com_Clamp( 0, 1, ( s->time2 - cg.time ) / 300.0f );
	const float    a = fadeIn * fadeOut;
	const float    t = cg.time * 0.001f;
	const int      count = (int)( halfW * 0.4f ); // ~40 at the widest
	vec3_t         yaw, fwd, right, p, q, haze = { 0.75f, 0.9f, 0.95f };
	int            k;

	if ( halfW <= 0 || life <= 0 || a <= 0 )
		return;
	VectorSet( yaw, 0, s->angles2[0], 0 );
	AngleVectors( yaw, fwd, right, NULL );

	// streaks of air racing up the wall
	for ( k = 0; k < count; k++ ) {
		const float ph = fmodf( t * 1.6f + k * 0.37f, 1.0f );
		const float along = -halfW + 2 * halfW * fmodf( k * 0.618f + t * 0.3f, 1.0f );
		VectorMA( cent->lerpOrigin, along, right, p );
		p[2] += SHINOBI_WINDWALL_HEIGHT * ph;
		VectorCopy( p, q );
		q[2] += 30;
		trap->FX_AddLine( p, q, 3.0f, 1.0f, 0.0f, 0.6f * a, 0.0f, 0.0f, haze, haze, 0.0f, 50,
			shinobiShaders.glow, FX_SIZE_LINEAR | FX_ALPHA_LINEAR );
	}
}

// someone hidden in their shadow: a dark patch sliding over the ground
static void CG_ShinobiShadowPatch( centity_t *cent ) {
	const float wob = 1.0f + 0.06f * sinf( cg.time * 0.006f + cent->currentState.number );
	CG_ShinobiGroundDecal( cent->lerpOrigin, 30 * wob, cg.time * 0.02f, shinobiShaders.puddle, 0.02f, 0.02f, 0.04f, 0.9f );
	CG_ShinobiGroundDecal( cent->lerpOrigin, 22 * wob, -cg.time * 0.03f, shinobiShaders.puddle, 0.0f, 0.0f, 0.0f, 0.8f );
}

// Rock Pillar about to erupt: the ground trembles in a small circle
static void CG_ShinobiPillarCast( centity_t *cent ) {
	entityState_t *s = &cent->currentState;
	const float    pulse = 0.75f + 0.25f * sinf( cg.time * 0.05f );
	vec3_t         up = { 0, 0, 1 }, p;

	if ( cg.time >= s->time2 + 200 )
		return;
	if ( s->angles2[1] ) { // out of a wall: stone dust bursting from it
		if ( cent->shinobiHandFxTime <= cg.time || cent->shinobiHandFxTime > cg.time + 1000 ) {
			vec3_t dir, right;
			VectorCopy( s->origin2, dir );
			PerpendicularVector( right, dir );
			VectorMA( cent->lerpOrigin, flrand( -SHINOBI_ROCK_HALF, SHINOBI_ROCK_HALF ), right, p );
			p[2] += flrand( -SHINOBI_ROCK_LEDGE_HALF, SHINOBI_ROCK_LEDGE_HALF );
			trap->FX_PlayEffectID( shinobiFx.quakeDust, p, dir, -1, -1, qfalse );
			cent->shinobiHandFxTime = cg.time + 40;
		}
		return;
	}
	CG_ShinobiGroundDecal( cent->lerpOrigin, SHINOBI_PILLAR_RADIUS * pulse, cg.time * 0.1f, shinobiShaders.ring, 0.8f, 0.55f, 0.25f, 1.0f );
	CG_ShinobiGroundDecal( cent->lerpOrigin, SHINOBI_PILLAR_RADIUS, 0, shinobiShaders.mud, 0.7f, 0.6f, 0.5f, 0.7f );
	if ( cent->shinobiHandFxTime <= cg.time || cent->shinobiHandFxTime > cg.time + 1000 ) {
		const float a = flrand( 0, 2 * M_PI ), r = flrand( 0, SHINOBI_PILLAR_RADIUS );
		VectorSet( p, cent->lerpOrigin[0] + cosf( a ) * r, cent->lerpOrigin[1] + sinf( a ) * r, cent->lerpOrigin[2] + 4 );
		trap->FX_PlayEffectID( shinobiFx.quakeDust, p, up, -1, -1, qfalse );
		cent->shinobiHandFxTime = cg.time + 40;
	}
}

/*
Protective Stance: a glowing field on the ground growing out from the caster (the eight trigrams
round its rim), the full circle faint ahead of it; full, it stays lit while held, flashing out to
the rim at each strike (s.apos.trTime); let go: it fades.
*/
static void CG_ShinobiPalms( centity_t *cent ) {
	const entityState_t *s = &cent->currentState;
	const float full = s->angles2[0];
	const float f = s->time2 > s->time ? Com_Clamp( 0, 1, (float)( cg.time - s->time ) / ( s->time2 - s->time ) ) : 1.0f;
	vec3_t      org, p, q, c = { 0.75f, 0.9f, 1.0f };
	int         k;

	VectorCopy( cent->lerpOrigin, org );
	org[2] += 1;
	if ( s->frame & 1 ) { // struck
		const float a = Com_Clamp( 0, 1, 1.0f - ( cg.time - s->time2 ) / 450.0f );
		CG_ShinobiGroundDecal( org, full, 0, shinobiShaders.ring, 0.9f * a, 1.0f * a, 1.0f * a, a );
		CG_ShinobiGroundDecal( org, full * 0.85f, 0, shinobiShaders.glow, 0.5f * a, 0.65f * a, 0.9f * a, a );
		return;
	}
	CG_ShinobiGroundDecal( org, full, 0, shinobiShaders.ring, 0.18f, 0.22f, 0.3f, 0.6f ); // where it will reach
	CG_ShinobiGroundDecal( org, full * f, cg.time * 0.02f, shinobiShaders.ring, 0.6f, 0.8f, 1.0f, 1.0f );
	CG_ShinobiGroundDecal( org, full * f * 0.95f, 0, shinobiShaders.glow, 0.15f, 0.22f, 0.35f, 0.8f );
	for ( k = 0; k < 8; k++ ) { // the trigrams: short bars round the rim
		const float ang = DEG2RAD( k * 45.0f + 22.5f ), r = full * f;
		vec3_t      side;
		VectorSet( side, -sinf( ang ), cosf( ang ), 0 );
		VectorSet( p, org[0] + cosf( ang ) * r * 0.82f, org[1] + sinf( ang ) * r * 0.82f, org[2] + 1 );
		VectorMA( p, -r * 0.08f, side, q );
		VectorMA( p, r * 0.08f, side, p );
		trap->FX_AddLine( p, q, 1.6f, 1.0f, 0.0f, 0.9f, 0.0f, 0.0f, c, c, 0.0f, 1, shinobiShaders.glow, FX_SIZE_LINEAR | FX_ALPHA_LINEAR );
	}
	if ( ( s->frame & 2 ) && s->apos.trTime && cg.time - s->apos.trTime < 450 ) { // a strike: the flash
		const float a = Com_Clamp( 0, 1, 1.0f - ( cg.time - s->apos.trTime ) / 450.0f );
		CG_ShinobiGroundDecal( org, full, 0, shinobiShaders.ring, 0.9f * a, 1.0f * a, 1.0f * a, a );
		CG_ShinobiGroundDecal( org, full * 0.85f, 0, shinobiShaders.glow, 0.5f * a, 0.65f * a, 0.9f * a, a );
	}
	trap->R_AddLightToScene( org, 60 + 180 * f, 0.5f, 0.7f, 1.0f );
}

qboolean CG_ShinobiEntity( centity_t *cent ) {
	entityState_t *s = &cent->currentState;
	const float    t = cg.time * 0.001f;
	vec3_t         dir;
	float          R, flick;
	int            li;

	if ( s->eType != ET_GENERAL )
		return qfalse;
	if ( s->generic1 >= SHINOBI_ENT_ZONE && s->generic1 < SHINOBI_ENT_ZONE + SZ_COUNT ) {
		CG_ShinobiZone( cent );
		return qtrue;
	}
	if ( s->generic1 >= SHINOBI_ENT_WAVE && s->generic1 < SHINOBI_ENT_WAVE + SHINOBI_MAX_LEVEL ) {
		CG_ShinobiWave( cent );
		return qtrue;
	}
	if ( s->generic1 == SHINOBI_ENT_EXKUNAI ) { // the tag on it smoulders, then sizzles
		const qboolean lit = s->time2 != 0;
		const float    f = lit ? 0.6f + 0.4f * flrand( 0, 1 ) : 0.5f + 0.2f * sinf( cg.time * 0.01f );
		CG_ShinobiSprite( cent->lerpOrigin, ( lit ? 8 : 4 ) * f, 0, shinobiShaders.glow, 1.0f * f, 0.45f * f, 0.1f * f );
		if ( lit ) {
			trap->R_AddLightToScene( cent->lerpOrigin, 70 * f, 1.0f, 0.5f, 0.15f );
			if ( cent->shinobiHandFxTime <= cg.time || cent->shinobiHandFxTime > cg.time + 1000 ) {
				vec3_t up = { 0, 0, 1 };
				trap->FX_PlayEffectID( shinobiFx.zoneSpark, cent->lerpOrigin, up, -1, -1, qfalse );
				cent->shinobiHandFxTime = cg.time + 90;
			}
		}
		return qfalse;
	}
	if ( s->generic1 == SHINOBI_ENT_TAG ) { // armed: a faint red seal; triggered: blinking fast
		if ( s->time2 ) {
			const float f = ( ( cg.time / 60 ) & 1 ) ? 1.0f : 0.3f;
			CG_ShinobiSprite( cent->lerpOrigin, 10 * f, 0, shinobiShaders.glow, 1.0f * f, 0.25f * f, 0.1f * f );
			trap->R_AddLightToScene( cent->lerpOrigin, 80 * f, 1.0f, 0.3f, 0.1f );
		}
		else if ( cg.time >= s->time ) {
			const float f = 0.25f + 0.15f * sinf( cg.time * 0.004f );
			CG_ShinobiSprite( cent->lerpOrigin, 4, 0, shinobiShaders.glow, 0.9f * f, 0.15f * f, 0.05f * f );
		}
		return qfalse;
	}
	if ( s->generic1 == SHINOBI_ENT_KUNAI_MARKED ) { // the seal glows; the model is drawn as usual
		const float pulse = 0.7f + 0.3f * sinf( cg.time * 0.01f );
		CG_ShinobiSprite( cent->lerpOrigin, 7 * pulse, 0, shinobiShaders.glow, 1.0f * pulse, 0.8f * pulse, 0.25f * pulse );
		trap->R_AddLightToScene( cent->lerpOrigin, 60 * pulse, 1.0f, 0.8f, 0.3f );
		return qfalse;
	}
	if ( s->generic1 == SHINOBI_ENT_FLAME ) {
		CG_ShinobiFlame( cent );
		return qtrue;
	}
	if ( s->generic1 == SHINOBI_ENT_SHADOW ) {
		CG_ShinobiShadow( cent );
		return qtrue;
	}
	if ( s->generic1 == SHINOBI_ENT_RAINET ) {
		CG_ShinobiRaiNet( cent );
		return qtrue;
	}
	if ( s->generic1 == SHINOBI_ENT_WINDSLASH ) {
		CG_ShinobiWindSlash( cent );
		return qtrue;
	}
	if ( s->generic1 == SHINOBI_ENT_FINISHER ) {
		CG_ShinobiFinisher( cent );
		return qtrue;
	}
	if ( s->generic1 == SHINOBI_ENT_PALMS ) {
		CG_ShinobiPalms( cent );
		return qtrue;
	}
	if ( s->generic1 == SHINOBI_ENT_QUAKE ) {
		CG_ShinobiQuake( cent );
		return qtrue;
	}
	if ( s->generic1 == SHINOBI_ENT_WINDWALL ) {
		CG_ShinobiWindWall( cent );
		return qtrue;
	}
	if ( s->generic1 == SHINOBI_ENT_PILLARCAST ) {
		CG_ShinobiPillarCast( cent );
		return qtrue;
	}
	if ( s->generic1 == SHINOBI_ENT_SHADOWPATCH ) {
		CG_ShinobiShadowPatch( cent );
		return qtrue;
	}
	if ( s->generic1 >= SHINOBI_ENT_BLADE && s->generic1 < SHINOBI_ENT_BLADE + SHINOBI_MAX_LEVEL ) {
		CG_ShinobiBlade( cent );
		return qtrue;
	}
	if ( s->generic1 >= SHINOBI_ENT_WATERBALL && s->generic1 < SHINOBI_ENT_WATERBALL + SHINOBI_MAX_LEVEL ) {
		CG_ShinobiWaterball( cent );
		return qtrue;
	}
	if ( s->generic1 >= SHINOBI_ENT_ICESHARD && s->generic1 < SHINOBI_ENT_ICESHARD + SHINOBI_MAX_LEVEL ) {
		CG_ShinobiIceShard( cent );
		return qtrue;
	}
	if ( s->generic1 < SHINOBI_ENT_FIREBALL || s->generic1 >= SHINOBI_ENT_FIREBALL + SHINOBI_MAX_LEVEL ) {
		return qfalse;
	}
	li = s->generic1 - SHINOBI_ENT_FIREBALL;
	R = shinobiFireballVisual[li] * ( s->iModelScale ? s->iModelScale / 100.0f : 1.0f ); // (grown by its caster's wind)
	flick = 0.9f + 0.1f * flrand( 0.0f, 1.0f );

	CG_ShinobiSprite( cent->lerpOrigin, R * 2.3f * flick, 0, shinobiShaders.glow, 0.45f, 0.16f, 0.02f );           // heat haze glow
	CG_ShinobiSprite( cent->lerpOrigin, R * 1.30f, t * 260.0f, shinobiShaders.fire, 0.80f, 0.35f, 0.10f );         // outer flames
	CG_ShinobiSprite( cent->lerpOrigin, R * 1.15f, -t * 380.0f + 90, shinobiShaders.fire, 0.85f, 0.45f, 0.12f );
	CG_ShinobiSprite( cent->lerpOrigin, R * 0.95f, t * 520.0f + 200, shinobiShaders.fire, 0.80f, 0.50f, 0.20f );
	CG_ShinobiSprite( cent->lerpOrigin, R * 0.80f, -t * 700.0f + 300, shinobiShaders.fire, 0.70f, 0.50f, 0.25f );
	CG_ShinobiSprite( cent->lerpOrigin, R * 0.45f * flick, 0, shinobiShaders.glow, 0.90f, 0.75f, 0.45f );          // core
	CG_ShinobiSprite( cent->lerpOrigin, R * 0.25f, 0, shinobiShaders.glow, 1.0f, 0.95f, 0.80f );

	VectorCopy( s->pos.trDelta, dir );
	if ( VectorNormalize( dir ) == 0 ) {
		VectorSet( dir, 0, 0, 1 );
	}

	if ( cent->shinobiHandFxTime <= cg.time || cent->shinobiHandFxTime > cg.time + 1000 ) {
		trap->FX_PlayEffectID( cgs.effects.shinobiFireballTrail, cent->lerpOrigin, dir, -1, -1, qfalse );
		cent->shinobiHandFxTime = cg.time + SHINOBI_BALL_FX_INTERVAL;
	}
	trap->R_AddLightToScene( cent->lerpOrigin, ( 180 + 90 * li ) * flick, 1.0f, 0.55f, 0.15f );
	return qtrue;
}

/*
===========================================================================
HUD: status text and the three charge bars
===========================================================================
*/
static void CG_ShinobiDrawChargeBars( void ) {
	static const vec4_t barColor[SHINOBI_MAX_LEVEL] = {
		{ 0.35f, 0.80f, 1.00f, 0.95f },  // level 1: blue
		{ 1.00f, 0.85f, 0.25f, 0.95f },  // level 2: yellow
		{ 1.00f, 0.35f, 0.15f, 0.95f },  // level 3: red
	};
	const vec4_t back = { 0.0f, 0.0f, 0.0f, 0.55f };
	const vec4_t locked = { 0.45f, 0.12f, 0.12f, 0.8f };
	const vec4_t frame = { 1.0f, 1.0f, 1.0f, 0.35f };
	const float  barW = 64, barH = 8, gap = 6, y = 384;
	const float  x0 = ( SCREEN_WIDTH - ( barW * 3 + gap * 2 ) ) * 0.5f;
	const int    chakra = cg.predictedPlayerState.fd.forcePower;
	const int    held = cg.time - shinobiCharge.start;
	const int    castLvl = CG_ShinobiBuild()->stats[SST_CAST];
	const int    lvl = Shinobi_ChargeLevelCast( shinobiCharge.jutsu, held, castLvl );
	int          shown = lvl, i;
	const char  *label;
	vec4_t       textColor = { 1, 1, 1, 1 };

	while ( shown > SHINOBI_JUTSU_MIN_LEVEL( shinobiCharge.jutsu ) && chakra < CG_ShinobiPrice( shinobiCharge.jutsu, shown ) )
		shown--;

	for ( i = 0; i < SHINOBI_MAX_LEVEL; i++ ) {
		const int   begin = i ? Shinobi_BarEndCast( shinobiCharge.jutsu, i - 1, castLvl ) : 0;
		const float fill = Com_Clamp( 0, 1, (float)( held - begin ) / (float)( Shinobi_BarEndCast( shinobiCharge.jutsu, i, castLvl ) - begin ) );
		const float x = x0 + i * ( barW + gap );
		const qboolean affordable = chakra >= CG_ShinobiPrice( shinobiCharge.jutsu, i + 1 );

		CG_FillRect( x, y, barW, barH, back );
		if ( fill > 0 )
			CG_FillRect( x, y, barW * fill, barH, affordable ? barColor[i] : locked );
		CG_DrawRect( x, y, barW, barH, 1, frame );
	}

	if ( held < Shinobi_BarEndCast( shinobiCharge.jutsu, SHINOBI_JUTSU_MIN_LEVEL( shinobiCharge.jutsu ) - 1, castLvl ) ) {
		label = va( "%s - forming seals...", shinobiJutsuTitles[shinobiCharge.jutsu] );
		VectorSet( textColor, 0.8f, 0.8f, 0.8f );
	}
	else if ( chakra < CG_ShinobiPrice( shinobiCharge.jutsu, 1 ) ) {
		label = va( "%s - not enough chakra (%i)", shinobiJutsuTitles[shinobiCharge.jutsu], CG_ShinobiPrice( shinobiCharge.jutsu, 1 ) );
		VectorSet( textColor, 1, 0.4f, 0.4f );
	}
	else {
		label = va( "%s  Lv %i  (%i chakra)", shinobiJutsuTitles[shinobiCharge.jutsu], shown, CG_ShinobiPrice( shinobiCharge.jutsu, shown ) );
		VectorCopy( barColor[shown - 1], textColor );
	}
	{
		const float scale = 0.6f;
		const int w = CG_Text_Width( label, scale, FONT_MEDIUM );
		CG_Text_Paint( ( SCREEN_WIDTH - w ) * 0.5f, y - 18, scale, textColor, label, 0, 0, ITEM_TEXTSTYLE_SHADOWED, FONT_MEDIUM );
	}
}

// the row of selectable jutsu at the bottom of the screen
// Kawarimi / Shunshin charges: a row of pips over the jutsu list
static void CG_ShinobiDrawCharges( void ) {
	const playerState_t *ps = &cg.predictedPlayerState;
	const int   have[2] = { SHINOBI_KAWA_CHARGES( ps ), SHINOBI_SHUN_CHARGES( ps ) };
	const int   max[2] = { SHINOBI_KAWARIMI_CHARGES, SHINOBI_SHUNSHIN_CHARGES };
	const char *names[2] = { "Kawarimi", "Shunshin" };
	const float scale = 0.36f, y = 432;
	float       x = SCREEN_WIDTH * 0.5f - 120;
	int         g, i;

	if ( ps->stats[STAT_HEALTH] <= 0 || ps->pm_type == PM_SPECTATOR )
		return;
	if ( ps->weapon == WP_MELEE ) { // the taijutsu style (L)
		const vec4_t c = { 1.0f, 0.85f, 0.45f, 0.9f };
		const char *t = va( "Taijutsu: %s", shinobiFistStyleNames[SHINOBI_FIST_STYLE( ps )] );
		CG_Text_Paint( SCREEN_WIDTH * 0.5f - CG_Text_Width( t, scale, FONT_MEDIUM ) * 0.5f, y - 16, scale, (float *)c, t, 0, 0,
			ITEM_TEXTSTYLE_SHADOWED, FONT_MEDIUM );
	}
	for ( g = 0; g < 2; g++ ) {
		const vec4_t label = { 0.8f, 0.85f, 0.9f, 0.85f };
		const vec4_t on = { 0.45f, 0.8f, 1.0f, 0.95f }, off = { 0.2f, 0.25f, 0.3f, 0.7f };
		CG_Text_Paint( x, y - 2, scale, (float *)label, names[g], 0, 0, ITEM_TEXTSTYLE_SHADOWED, FONT_MEDIUM );
		x += CG_Text_Width( names[g], scale, FONT_MEDIUM ) + 6;
		for ( i = 0; i < max[g]; i++ ) {
			CG_FillRect( x, y + 2, 8, 8, i < have[g] ? on : off );
			x += 11;
		}
		x += 28;
	}
}

/*
Inner Gates: the gate open and the way to the next (a bar filling with the damage dealt);
ready: "Gate of ... ready - C", pulsing.
*/
static void CG_ShinobiDrawGates( void ) {
	const playerState_t *ps = &cg.predictedPlayerState;
	const int   gate = SHINOBI_GATE_OF( ps );
	const int   prog = ( ps->stats[STAT_SHINOBI_BLADECOMBO] >> SHINOBI_BC_PROG_SHIFT ) & 15;
	const qboolean ready = ( ps->stats[STAT_SHINOBI_BLADECOMBO] & SHINOBI_BC_GATE_READY ) != 0;
	const float scale = 0.38f, y = 406, w = 160;
	vec4_t      c, back = { 0, 0, 0, 0.55f }, fill;
	const char *t;
	float       x;

	if ( gate <= 0 || gate > SHINOBI_GATE_MAX || ps->stats[STAT_HEALTH] <= 0 || ps->pm_type == PM_SPECTATOR )
		return;
	if ( gate >= 5 )
		VectorSet4( c, 1.0f, 0.3f, 0.25f, 0.95f );
	else if ( gate >= 4 )
		VectorSet4( c, 0.35f, 0.85f, 0.4f, 0.95f );
	else if ( gate >= 2 )
		VectorSet4( c, 1.0f, 0.85f, 0.35f, 0.95f );
	else
		VectorSet4( c, 0.6f, 1.0f, 0.65f, 0.95f );
	t = va( "Gate %i: %s", gate, shinobiGateNames[gate] );
	x = SCREEN_WIDTH * 0.5f - w * 0.5f;
	CG_Text_Paint( SCREEN_WIDTH * 0.5f - CG_Text_Width( t, scale, FONT_MEDIUM ) * 0.5f, y - 15, scale, c, t, 0, 0,
		ITEM_TEXTSTYLE_SHADOWED, FONT_MEDIUM );
	if ( gate >= SHINOBI_GATE_MAX )
		return;
	CG_FillRect( x, y, w, 6, back );
	VectorCopy4( c, fill );
	if ( ready )
		fill[3] = 0.6f + 0.4f * sinf( cg.time * 0.012f );
	CG_FillRect( x + 1, y + 1, ( w - 2 ) * ( ready ? 1.0f : prog / 15.0f ), 4, fill );
	if ( ready ) {
		t = va( "%s ready - C", shinobiGateNames[gate + 1] );
		CG_Text_Paint( SCREEN_WIDTH * 0.5f - CG_Text_Width( t, 0.34f, FONT_MEDIUM ) * 0.5f, y + 8, 0.34f, fill, t, 0, 0,
			ITEM_TEXTSTYLE_SHADOWED, FONT_MEDIUM );
	}
}

/*
The blade combo under the crosshair: a white pip for each blow landed, then the finisher's
gold one - hollow until the combo stands one short, then lit and pulsing: NOW the next
swing is the finisher (what you hold picks which; a hint says what does what).
*/
static void CG_ShinobiDrawBladeCombo( void ) {
	const playerState_t *ps = &cg.predictedPlayerState;
	const int v = ps->stats[STAT_SHINOBI_BLADECOMBO];
	const int have = v & 7, need = ( v >> 3 ) & 7;
	const vec4_t on = { 0.9f, 0.95f, 1.0f, 0.95f }, off = { 0.2f, 0.22f, 0.26f, 0.7f };
	vec4_t gold = { 1.0f, 0.78f, 0.2f, 0.95f };
	const qboolean ready = Shinobi_BladeFinisherReady( ps );
	float x;
	int   k;

	if ( !have || need < 2 || cg.snap->ps.pm_type == PM_SPECTATOR )
		return;
	x = SCREEN_WIDTH * 0.5f - ( need * 14 - 4 ) * 0.5f;
	for ( k = 0; k < need - 1; k++ ) { // white: lit for each blow landed, empty (a dark box with a white rim) for those to come
		if ( k < have )
			CG_FillRect( x + k * 14, 262, 10, 10, (float *)on );
		else {
			const vec4_t rim = { 0.85f, 0.9f, 1.0f, 0.7f };
			CG_FillRect( x + k * 14, 262, 10, 10, (float *)off );
			CG_DrawRect( x + k * 14, 262, 10, 10, 1, (float *)rim );
		}
	}
	x += ( need - 1 ) * 14;
	if ( ready ) { // now: the gold one lit and pulsing
		const float p = 0.5f + 0.5f * sinf( cg.time * 0.02f );
		gold[3] = 0.6f + 0.4f * p;
		CG_FillRect( x - 1, 261, 12, 12, gold );
	}
	else { // empty: a dark box with a gold rim
		CG_FillRect( x, 262, 10, 10, (float *)off );
		CG_DrawRect( x, 262, 10, 10, 1, gold );
	}
}

// lock-on (T): red brackets round the target, turning slowly
static void CG_ShinobiDrawLock( void ) {
	const int n = cg.predictedPlayerState.stats[STAT_SHINOBI_LOCK] - 1;
	centity_t *t;
	vec3_t     at;
	float      x, y, r;
	int        k;
	const vec4_t c = { 1.0f, 0.2f, 0.15f, 0.9f };

	if ( n < 0 || n >= MAX_GENTITIES || cg.snap->ps.pm_type == PM_SPECTATOR )
		return;
	t = &cg_entities[n];
	if ( !t->currentValid )
		return;
	VectorCopy( t->lerpOrigin, at );
	at[2] += 8;
	if ( !CG_WorldCoordToScreenCoordFloat( at, &x, &y ) )
		return;
	r = 3000.0f / ( Distance( at, cg.refdef.vieworg ) + 60.0f ) + 10.0f;
	if ( r > 46 )
		r = 46;
	for ( k = 0; k < 4; k++ ) { // four corners
		const float sx = ( k & 1 ) ? 1.0f : -1.0f, sy = ( k & 2 ) ? 1.0f : -1.0f;
		const float cx = x + sx * r, cy = y + sy * r;
		CG_FillRect( sx > 0 ? cx - 8 : cx, cy - 1, 8, 2, (float *)c );
		CG_FillRect( cx - 1, sy > 0 ? cy - 8 : cy, 2, 8, (float *)c );
	}
	CG_FillRect( x - 1.5f, y - 1.5f, 3, 3, (float *)c );
}

static void CG_ShinobiDrawJutsuList( void ) {
	const float scale = 0.42f, gap = 10, y = 448;
	const int   chakra = cg.predictedPlayerState.fd.forcePower;
	const float recent = Com_Clamp( 0, 1, 1.0f - ( cg.time - shinobiSel.changeTime - 1500 ) / 500.0f );
	float       total = 0, x;
	int         i, slots[SHINOBI_BUILD_JUTSU + 1];
	const int   n = CG_ShinobiSlots( slots );

	if ( !n )
		return;
	if ( shinobiSel.index >= n )
		shinobiSel.index = 0;
	for ( i = 0; i < n; i++ )
		total += CG_Text_Width( shinobiJutsuTitles[slots[i]], scale, FONT_MEDIUM ) + ( i ? gap : 0 );
	x = ( SCREEN_WIDTH - total ) * 0.5f;

	for ( i = 0; i < n; i++ ) {
		const int    j = slots[i];
		const char  *title = shinobiJutsuTitles[j];
		const float  w = CG_Text_Width( title, scale, FONT_MEDIUM );
		const qboolean selected = ( i == shinobiSel.index ) && !SHINOBI_JUTSU_PASSIVE( j );
		const qboolean affordable = chakra >= CG_ShinobiPrice( j, SHINOBI_JUTSU_MIN_LEVEL( j ) );
		const qboolean copy = CG_ShinobiIsCopy( j );
		vec4_t       color;

		if ( selected ) {
			const vec4_t back = { 0.1f, 0.35f, 0.6f, 0.55f + 0.3f * recent };
			CG_FillRect( x - 4, y - 1, w + 8, 14, back );
			VectorSet( color, 1.0f, 1.0f, 1.0f );
			color[3] = 1.0f;
		}
		else {
			VectorSet( color, 0.75f, 0.75f, 0.75f );
			color[3] = 0.55f;
		}
		if ( !affordable ) {
			color[1] *= 0.5f;
			color[2] *= 0.5f;
		}
		if ( SHINOBI_JUTSU_PASSIVE( j ) ) { // always on: a dim green
			VectorSet( color, 0.5f, 0.8f, 0.55f );
			color[3] = 0.5f;
		}
		if ( copy ) { // the copy slot: red, a tomoe line under it
			const vec4_t mark = { 0.85f, 0.1f, 0.08f, selected ? 0.95f : 0.6f };
			color[0] = 1.0f; color[1] *= 0.45f; color[2] *= 0.4f;
			CG_FillRect( x, y + 13, w, 2, mark );
		}
		CG_Text_Paint( x, y, scale, color, title, 0, 0, ITEM_TEXTSTYLE_SHADOWED, FONT_MEDIUM );
		x += w + gap;
	}

	if ( recent > 0 ) { // just picked: show what the levels cost
		const int   j = slots[shinobiSel.index];
		const char *info = CG_ShinobiIsCopy( j )
			? ( SHINOBI_JUTSU_INSTANT( j ) || SHINOBI_JUTSU_HOLD( j )
				? va( "Copied %s: %i chakra", shinobiJutsuTitles[j], CG_ShinobiPrice( j, 1 ) )
				: va( "Copied %s: Lv1 %i / Lv2 %i chakra (Lv2 at most)", shinobiJutsuTitles[j],
					CG_ShinobiPrice( j, 1 ), CG_ShinobiPrice( j, 2 ) ) )
			: SHINOBI_JUTSU_INSTANT( j )
			? va( "%s: %i chakra", shinobiJutsuTitles[j], Shinobi_JutsuCost( j, 1 ) )
			: SHINOBI_JUTSU_HOLD( j )
			? va( "%s: %i chakra, then %i/s while you hold the key", shinobiJutsuTitles[j], Shinobi_JutsuCost( j, 1 ),
				SHINOBI_PRISON_DRAIN * 1000 / SHINOBI_PRISON_TICK_MS )
			: va( "%s: Lv1 %i / Lv2 %i / Lv3 %i chakra (hold to charge)", shinobiJutsuTitles[j],
				Shinobi_JutsuCost( j, 1 ), Shinobi_JutsuCost( j, 2 ), Shinobi_JutsuCost( j, 3 ) );
		const float w = CG_Text_Width( info, 0.45f, FONT_MEDIUM );
		vec4_t      c = { 0.8f, 0.9f, 1.0f, 1.0f };
		c[3] = recent;
		CG_Text_Paint( ( SCREEN_WIDTH - w ) * 0.5f, y - 16, 0.45f, c, info, 0, 0, ITEM_TEXTSTYLE_SHADOWED, FONT_MEDIUM );
	}
}

/*
===========================================================================
Sharingan / Byakugan: what the server tells our eyes ("sinfo"), bars over
heads, Byakugan shells through walls, glowing eyes on whoever uses them.
===========================================================================
*/
typedef struct {
	int   time;     // cg.time of the update (0 = never)
	short hp, maxhp, chakra, chakraMax;
	byte  jutsu, flags;
} shinobiSeen_t;
static shinobiSeen_t shinobiSeen[MAX_GENTITIES];
static int           shinobiSeenTime;

void CG_ShinobiEyeInfo_f( void ) {
	const int argc = trap->Cmd_Argc();
	int i;

	shinobiSeenTime = cg.time;
	for ( i = 0; i < MAX_GENTITIES; i++ )
		shinobiSeen[i].time = 0;
	for ( i = 1; i + 6 < argc; i += 7 ) {
		char arg[16];
		int  num;
		shinobiSeen_t *s;

		trap->Cmd_Argv( i, arg, sizeof( arg ) );
		num = atoi( arg );
		if ( num < 0 || num >= MAX_GENTITIES )
			continue;
		s = &shinobiSeen[num];
		s->time = cg.time;
		trap->Cmd_Argv( i + 1, arg, sizeof( arg ) ); s->hp = atoi( arg );
		trap->Cmd_Argv( i + 2, arg, sizeof( arg ) ); s->maxhp = atoi( arg );
		trap->Cmd_Argv( i + 3, arg, sizeof( arg ) ); s->chakra = atoi( arg );
		trap->Cmd_Argv( i + 4, arg, sizeof( arg ) ); s->chakraMax = atoi( arg );
		trap->Cmd_Argv( i + 5, arg, sizeof( arg ) ); s->jutsu = (byte)atoi( arg );
		trap->Cmd_Argv( i + 6, arg, sizeof( arg ) ); s->flags = (byte)atoi( arg );
	}
}

static const shinobiSeen_t *CG_ShinobiSeen( int num ) {
	const shinobiSeen_t *s;
	if ( num < 0 || num >= MAX_GENTITIES )
		return NULL;
	s = &shinobiSeen[num];
	if ( !s->time || cg.time - s->time > SHINOBI_PERK_INFO_MS * 3 || cg.time < s->time )
		return NULL;
	return s;
}

static int CG_ShinobiMyEyes( void ) {
	if ( !cg.snap || cg.snap->ps.pm_type == PM_SPECTATOR || ( cg.snap->ps.pm_flags & PMF_FOLLOW ) )
		return 0;
	if ( cg.snap->ps.eFlags2 & SHINOBI_EF2_SHARINGAN ) // (the same red eyes: our build tells which)
		return CG_ShinobiBuild()->perk == SP_COPYEYE ? SP_COPYEYE : SP_SHARINGAN;
	if ( cg.snap->ps.eFlags2 & SHINOBI_EF2_BYAKUGAN )
		return SP_BYAKUGAN;
	return 0;
}

// a fog sheet in front of the camera inside a Hidden Mist (3D, so shapes can be drawn over it)
void CG_ShinobiSceneOverlay( void ) {
	refEntity_t ent;
	float       a;

	CG_ShinobiDrawShocks();

	shinobiMist.lastTime = shinobiMist.time;
	shinobiMist.lastAmount = shinobiMist.time == cg.time ? shinobiMist.amount : 0;
	shinobiMist.lastMine = shinobiMist.time == cg.time && shinobiMist.mine;
	if ( shinobiMist.time != cg.time || shinobiMist.amount <= 0 || !shinobiShaders.fogScreen )
		return;
	a = 0.93f * shinobiMist.amount * ( CG_ShinobiMyEyes() == SP_BYAKUGAN ? 0.3f : 1.0f );
	memset( &ent, 0, sizeof( ent ) );
	ent.reType = RT_SPRITE;
	VectorMA( cg.refdef.vieworg, 12, cg.refdef.viewaxis[0], ent.origin );
	ent.radius = 80;
	ent.customShader = shinobiShaders.fogScreen;
	ent.shaderRGBA[0] = 205; ent.shaderRGBA[1] = 212; ent.shaderRGBA[2] = 220;
	ent.shaderRGBA[3] = (byte)( Com_Clamp( 0, 1, a ) * 255 );
	ent.renderfx = RF_NOSHADOW | RF_FIRST_PERSON;
	trap->R_AddRefEntityToScene( &ent );
}

// shapes of people inside the Hidden Mist: dark silhouettes over the fog
static void CG_ShinobiMistShell( centity_t *cent, refEntity_t *body ) {
	refEntity_t shell;
	trace_t     tr;
	float       dist, f;

	if ( shinobiMist.lastTime < cg.time - 100 || shinobiMist.lastAmount <= 0.05f || !shinobiShaders.mistShell
		|| !shinobiMist.lastMine )
		return;
	if ( cent->currentState.eType == ET_PLAYER && SHINOBI_HIDDEN( &cent->currentState ) )
		return;
	dist = Distance( cent->lerpOrigin, cg.refdef.vieworg );
	if ( dist > SHINOBI_MIST_SHAPES )
		return;
	CG_Trace( &tr, cg.refdef.vieworg, NULL, NULL, cent->lerpOrigin, cg.snap->ps.clientNum, CONTENTS_SOLID );
	if ( tr.fraction < 1.0f )
		return;
	f = dist < 120 ? 1.0f : 1.0f - ( dist - 120 ) / ( SHINOBI_MIST_SHAPES - 120 );
	shell = *body;
	shell.shaderRGBA[0] = 60; shell.shaderRGBA[1] = 68; shell.shaderRGBA[2] = 82;
	shell.shaderRGBA[3] = (byte)( 255 * Com_Clamp( 0, 1, 0.75f * f * shinobiMist.lastAmount ) );
	shell.renderfx &= ~( RF_RGB_TINT | RF_FORCE_ENT_ALPHA | RF_MINLIGHT );
	shell.renderfx |= RF_NODEPTH | RF_FORCEPOST;
	shell.customShader = shinobiShaders.mistShell;
	trap->R_AddRefEntityToScene( &shell );
}

// called from CG_Player with the finished body: Byakugan sees it through walls,
// anyone sees shapes in the Hidden Mist
/*
Anime outline (cg_shinobiOutline 1-3): the model once more, pushed out along its normals,
black, only its back faces - the front of the body hides it but at the silhouette a dark
line shows. Characters, their accessories and the tool in hand; not the map.
*/
void CG_ShinobiOutline( centity_t *cent, const refEntity_t *src ) {
	refEntity_t o;
	int         lvl = cg_shinobiOutline.integer;

	if ( lvl <= 0 || !src || ( !src->hModel && !src->ghoul2 ) )
		return;
	if ( lvl > 3 )
		lvl = 3;
	if ( !shinobiShaders.outline[lvl - 1] )
		return;
	if ( ( src->renderfx & RF_FORCE_ENT_ALPHA ) || ( src->shaderRGBA[3] && src->shaderRGBA[3] < 255 ) )
		return; // see-through (Kawarimi, cloaks, fading): no hard line around it
	if ( cent && ( cent->currentState.eFlags & ( EF_NODRAW | SHINOBI_EF_RAIDASH ) ) )
		return;
	o = *src;
	o.customShader = shinobiShaders.outline[lvl - 1];
	o.renderfx &= ~( RF_RGB_TINT | RF_FORCE_ENT_ALPHA | RF_MINLIGHT | RF_NODEPTH | RF_FORCEPOST );
	o.shaderRGBA[0] = o.shaderRGBA[1] = o.shaderRGBA[2] = 0;
	o.shaderRGBA[3] = 255;
	trap->R_AddRefEntityToScene( &o );
}

/*
Sharingan: the eye reads where an enemy is aiming - a thin red line from his eyes
along his look to the first thing it meets (only for those in plain sight).
*/
#define SHINOBI_AIM_RANGE    1600
static void CG_ShinobiAimRay( centity_t *cent ) {
	vec3_t  eye, fwd, end, mid, red = { 1.0f, 0.12f, 0.08f };
	trace_t tr;

	VectorCopy( cent->lerpOrigin, eye );
	eye[2] += 30;
	CG_Trace( &tr, cg.refdef.vieworg, NULL, NULL, eye, cg.snap->ps.clientNum, MASK_SOLID );
	if ( tr.fraction < 1.0f )
		return; // not in sight
	AngleVectors( cent->lerpAngles, fwd, NULL, NULL );
	VectorMA( eye, 10, fwd, eye );
	VectorMA( eye, SHINOBI_AIM_RANGE, fwd, end );
	CG_Trace( &tr, eye, NULL, NULL, end, cent->currentState.number, MASK_SHOT );
	VectorCopy( tr.endpos, end );
	VectorAdd( eye, end, mid );
	VectorScale( mid, 0.5f, mid );
	trap->FX_AddLine( eye, mid, 0.9f, 0.9f, 0.0f, 0.7f, 0.5f, 0.0f, red, red, 0.0f, 1,
		shinobiShaders.glow, FX_SIZE_LINEAR | FX_ALPHA_LINEAR );
	trap->FX_AddLine( mid, end, 0.9f, 0.9f, 0.0f, 0.5f, 0.25f, 0.0f, red, red, 0.0f, 1,
		shinobiShaders.glow, FX_SIZE_LINEAR | FX_ALPHA_LINEAR );
	if ( tr.fraction < 1.0f ) // where it points
		CG_ShinobiSpriteA( end, 4.0f, 0, shinobiShaders.ring, 1.0f, 0.15f, 0.1f, 0.8f );
}

// Sharingan: a thin red rim of chakra around everyone in sight (not through walls)
static void CG_ShinobiSharinganRim( centity_t *cent, refEntity_t *body ) {
	refEntity_t shell;
	const int   num = cent->currentState.number;

	if ( !shinobiShaders.sharinganRim || ( cent->currentState.eFlags & ( EF_NODRAW | SHINOBI_EF_RAIDASH ) )
		|| DistanceSquared( cent->lerpOrigin, cg.snap->ps.origin ) > Square( SHINOBI_SHARINGAN_RANGE ) )
		return;
	if ( num < MAX_CLIENTS && cgs.gametype >= GT_TEAM && cgs.clientinfo[num].team == cg.snap->ps.persistant[PERS_TEAM] )
		return; // a friend
	shell = *body;
	shell.customShader = shinobiShaders.sharinganRim;
	shell.renderfx &= ~( RF_RGB_TINT | RF_FORCE_ENT_ALPHA | RF_MINLIGHT );
	shell.shaderRGBA[0] = 210; shell.shaderRGBA[1] = 30; shell.shaderRGBA[2] = 22; shell.shaderRGBA[3] = 255;
	trap->R_AddRefEntityToScene( &shell );
	if ( num < MAX_CLIENTS )
		CG_ShinobiAimRay( cent );
}

void CG_ShinobiEyeShell( centity_t *cent, refEntity_t *body ) {
	const shinobiSeen_t *seen;
	refEntity_t shell;

	if ( cent->currentState.number == cg.snap->ps.clientNum || ( cent->currentState.eFlags & EF_DEAD ) )
		return;
	if ( CG_ShinobiMyEyes() == SP_SHARINGAN )
		CG_ShinobiSharinganRim( cent, body );
	if ( CG_ShinobiMyEyes() != SP_BYAKUGAN || !cgs.media.sightShell
		|| DistanceSquared( cent->lerpOrigin, cg.snap->ps.origin ) > Square( SHINOBI_BYAKUGAN_RANGE )
		|| !( seen = CG_ShinobiSeen( cent->currentState.number ) ) ) {
		CG_ShinobiMistShell( cent, body );
		return;
	}

	shell = *body;
	if ( seen->flags & SHINOBI_SEEN_CLONE ) { // a hollow copy: barely any chakra inside
		shell.shaderRGBA[0] = 70; shell.shaderRGBA[1] = 70; shell.shaderRGBA[2] = 90;
	}
	else {
		shell.shaderRGBA[0] = 170; shell.shaderRGBA[1] = 215; shell.shaderRGBA[2] = 255;
	}
	shell.shaderRGBA[3] = 255;
	shell.renderfx &= ~( RF_RGB_TINT | RF_FORCE_ENT_ALPHA );
	shell.renderfx |= RF_MINLIGHT | RF_NODEPTH | RF_FORCEPOST; // over walls and over the mist
	shell.customShader = cgs.media.sightShell;
	trap->R_AddRefEntityToScene( &shell );
}

// Inner Gates: the third gate on, the body glows crimson (a player's s.frame: the gate open)
static void CG_ShinobiGateShell( centity_t *cent, refEntity_t *body ) {
	refEntity_t shell;
	const int   gate = cent->currentState.frame & SHINOBI_PFRAME_GATE_MASK;
	float       pulse;

	if ( cent->currentState.eType != ET_PLAYER || gate < 3 || ( cent->currentState.eFlags & ( EF_DEAD | EF_NODRAW | SHINOBI_EF_RAIDASH ) )
		|| !shinobiShaders.gateShell )
		return;
	pulse = 0.75f + 0.25f * sinf( cg.time * 0.008f );
	shell = *body;
	shell.customShader = shinobiShaders.gateShell;
	shell.renderfx &= ~( RF_RGB_TINT | RF_FORCE_ENT_ALPHA );
	shell.shaderRGBA[0] = (byte)( 200 * pulse );
	shell.shaderRGBA[1] = (byte)( 18 * pulse );
	shell.shaderRGBA[2] = (byte)( 30 * pulse );
	shell.shaderRGBA[3] = 255;
	trap->R_AddRefEntityToScene( &shell );
}

// Inner Gates: the weights on the shins (until the first gate drops them) - at each foot bolt,
// along the shin (towards the lower back), a little above the foot
static void CG_ShinobiLegWeights( centity_t *cent ) {
	static const char * const feet[2] = { "*r_leg_foot", "*l_leg_foot" };
	mdxaBone_t m;
	vec3_t     lumbar, yaw, fwd;
	int        k, bolt;

	if ( cent->currentState.eType != ET_PLAYER || !( cent->currentState.frame & SHINOBI_PFRAME_WEIGHTS )
		|| ( cent->currentState.eFlags & ( EF_DEAD | EF_NODRAW | SHINOBI_EF_RAIDASH ) ) || !cent->ghoul2 || !shinobiShaders.legWeight )
		return;
	bolt = trap->G2API_AddBolt( cent->ghoul2, 0, "lower_lumbar" );
	if ( bolt < 0 || !trap->G2API_GetBoltMatrix( cent->ghoul2, 0, bolt, &m, cent->turAngles, cent->lerpOrigin, cg.time, cgs.gameModels, cent->modelScale ) )
		return;
	lumbar[0] = m.matrix[0][3]; lumbar[1] = m.matrix[1][3]; lumbar[2] = m.matrix[2][3];
	VectorSet( yaw, 0, cent->lerpAngles[YAW], 0 );
	AngleVectors( yaw, fwd, NULL, NULL );
	for ( k = 0; k < 2; k++ ) {
		refEntity_t ent;
		vec3_t      foot, shin;
		bolt = trap->G2API_AddBolt( cent->ghoul2, 0, feet[k] );
		if ( bolt < 0 || !trap->G2API_GetBoltMatrix( cent->ghoul2, 0, bolt, &m, cent->turAngles, cent->lerpOrigin, cg.time, cgs.gameModels, cent->modelScale ) )
			continue;
		foot[0] = m.matrix[0][3]; foot[1] = m.matrix[1][3]; foot[2] = m.matrix[2][3];
		VectorSubtract( lumbar, foot, shin );
		if ( VectorNormalize( shin ) < 1.0f )
			VectorSet( shin, 0, 0, 1 );
		memset( &ent, 0, sizeof( ent ) );
		ent.hModel = shinobiShaders.legWeight;
		VectorMA( foot, 7.0f * cent->modelScale[2], shin, ent.origin ); // (the ankle, just above the foot)
		VectorCopy( shin, ent.axis[2] );
		CrossProduct( ent.axis[2], fwd, ent.axis[1] );
		if ( VectorNormalize( ent.axis[1] ) < 0.01f )
			VectorSet( ent.axis[1], 0, 1, 0 );
		CrossProduct( ent.axis[1], ent.axis[2], ent.axis[0] );
		VectorScale( ent.axis[0], cent->modelScale[0], ent.axis[0] );
		VectorScale( ent.axis[1], cent->modelScale[1], ent.axis[1] );
		VectorScale( ent.axis[2], cent->modelScale[2], ent.axis[2] );
		ent.nonNormalizedAxes = qtrue;
		VectorCopy( ent.origin, ent.oldorigin );
		VectorCopy( cent->lerpOrigin, ent.lightingOrigin );
		ent.renderfx = RF_LIGHTING_ORIGIN | ( cent->currentState.number == cg.snap->ps.clientNum && !cg.renderingThirdPerson ? RF_THIRD_PERSON : 0 );
		ent.shaderRGBA[0] = ent.shaderRGBA[1] = ent.shaderRGBA[2] = ent.shaderRGBA[3] = 255;
		trap->R_AddRefEntityToScene( &ent );
	}
}

// Stone Skin / Earth Clone: the body turned to rough stone
void CG_ShinobiStoneShell( centity_t *cent, refEntity_t *body ) {
	refEntity_t shell;

	if ( !( cent->currentState.eFlags & SHINOBI_EF_STONE ) || ( cent->currentState.eFlags & ( EF_DEAD | SHINOBI_EF_RAIDASH ) )
		|| !shinobiShaders.stoneShell )
		return;
	shell = *body;
	shell.customShader = shinobiShaders.stoneShell;
	shell.renderfx &= ~( RF_RGB_TINT | RF_FORCE_ENT_ALPHA | RF_MINLIGHT );
	shell.shaderRGBA[0] = shell.shaderRGBA[1] = shell.shaderRGBA[2] = shell.shaderRGBA[3] = 255;
	trap->R_AddRefEntityToScene( &shell );
}

// the Inner Gates' look on a player: the weights, the crimson glow (from CG_Player, after the body)
void CG_ShinobiGatesLook( centity_t *cent, refEntity_t *body ) {
	CG_ShinobiGateShell( cent, body );
	CG_ShinobiLegWeights( cent );
}

/*
Head accessories (models/accessories/head/<name>.md3): modelled in the bind
pose of the _humanoid skeleton, kept rigid on the "cranium" bone. World =
cranium bolt now * inverse of its bind pose (from _humanoid.gla) * vertex.
*/
static struct {
	qboolean   tried, ok;
	float      inv[3][4]; // cranium BasePoseMatInv
} shinobiCranium;

static void CG_ShinobiLoadCranium( void ) {
	static byte  buf[131072];
	fileHandle_t f;
	int          len;

	shinobiCranium.tried = qtrue;
	len = trap->FS_Open( "models/players/_humanoid/_humanoid.gla", &f, FS_READ );
	if ( len <= 0 || !f ) {
		trap->Print( S_COLOR_YELLOW "[shinobi] no _humanoid.gla: accessories off\n" );
		return;
	}
	if ( len > (int)sizeof( buf ) )
		len = sizeof( buf ); // the skeleton is at the start, the frames come after
	trap->FS_Read( buf, len, f );
	trap->FS_Close( f );
	shinobiCranium.ok = Shinobi_GlaBoneInv( buf, len, "cranium", shinobiCranium.inv );
	if ( !shinobiCranium.ok )
		trap->Print( S_COLOR_YELLOW "[shinobi] no cranium bone in _humanoid.gla: accessories off\n" );
}

void CG_ShinobiAccessory( centity_t *cent, clientInfo_t *ci, refEntity_t *body ) {
	mdxaBone_t  m;
	refEntity_t ent;
	int         i;

	if ( cent->currentState.eType == ET_NPC && cent->currentState.owner >= 0 && cent->currentState.owner < MAX_CLIENTS
		&& cgs.clientinfo[cent->currentState.owner].infoValid )
		ci = &cgs.clientinfo[cent->currentState.owner]; // a clone / Kawarimi double: its maker's accessories
	if ( !ci || !ci->shinobiAccCount || !cent->ghoul2 )
		return;
	if ( cent->currentState.eFlags & ( SHINOBI_EF_RAIDASH | EF_NODRAW ) || ( cent->currentState.powerups & ( 1 << PW_CLOAKED ) ) )
		return;
	if ( cent->currentState.number == cg.snap->ps.clientNum && !cg.renderingThirdPerson )
		return; // our own hat would fill the first-person view
	if ( !shinobiCranium.tried )
		CG_ShinobiLoadCranium();
	if ( !shinobiCranium.ok )
		return;
	if ( cent->shinobiAccG2 != cent->ghoul2 ) {
		cent->shinobiAccG2 = cent->ghoul2;
		cent->shinobiAccBolt = trap->G2API_AddBolt( cent->ghoul2, 0, "cranium" );
	}
	if ( cent->shinobiAccBolt < 0 )
		return;
	if ( !trap->G2API_GetBoltMatrix( cent->ghoul2, 0, cent->shinobiAccBolt, &m, cent->turAngles, cent->lerpOrigin,
		cg.time, cgs.gameModels, cent->modelScale ) )
		return;

	for ( i = 0; i < ci->shinobiAccCount; i++ ) {
		if ( !ci->shinobiAccModel[i] )
			continue;
		memset( &ent, 0, sizeof( ent ) );
		ent.hModel = ci->shinobiAccModel[i];
		Shinobi_AccPlace( (const float (*)[4])m.matrix, (const float (*)[4])shinobiCranium.inv, &ci->shinobiAcc[i],
			cent->modelScale[0], ent.origin, ent.axis );
		VectorCopy( ent.origin, ent.oldorigin );
		ent.nonNormalizedAxes = qtrue;
		VectorCopy( cent->lerpOrigin, ent.lightingOrigin );
		ent.renderfx = RF_LIGHTING_ORIGIN | ( body->renderfx & ( RF_THIRD_PERSON | RF_FORCE_ENT_ALPHA ) );
		ent.shaderRGBA[0] = ent.shaderRGBA[1] = ent.shaderRGBA[2] = 255;
		ent.shaderRGBA[3] = body->shaderRGBA[3] ? body->shaderRGBA[3] : 255;
		if ( cent->currentState.eFlags & SHINOBI_EF_STONE )
			ent.customShader = shinobiShaders.stoneShell; // stone all over
		trap->R_AddRefEntityToScene( &ent );
		CG_ShinobiOutline( cent, &ent );
	}
}

/*
Guard bubble: while someone holds block a faint sphere stands around him, blue while
the guard is strong, yellow at half, red when it is about to give; each blocked hit
(the stock shield-hit event) makes it flare.
*/
static void CG_ShinobiGuardColor( int lvl, byte rgb[3], float k ) {
	static const float col[4][3] = { { 1.0f, 0.25f, 0.2f }, { 0.3f, 0.55f, 1.0f }, { 1.0f, 0.82f, 0.25f }, { 1.0f, 0.25f, 0.2f } };
	int i;
	if ( lvl < 0 || lvl > 3 )
		lvl = 0;
	for ( i = 0; i < 3; i++ )
		rgb[i] = (byte)Com_Clamp( 0, 255, col[lvl][i] * 255.0f * k );
}

static void CG_ShinobiGuardDraw( centity_t *cent, int lvl, float k, const vec3_t facing ) {
	refEntity_t ent;
	vec3_t      d, ang;
	float       s;

	if ( !cgs.media.halfShieldModel || !shinobiShaders.guardShell || k <= 0.01f )
		return;
	memset( &ent, 0, sizeof( ent ) );
	VectorCopy( cent->lerpOrigin, ent.origin );
	ent.origin[2] += 10.0f;
	if ( facing )
		VectorCopy( facing, d );
	else
		VectorSubtract( cg.refdef.vieworg, ent.origin, d ); // the half sphere turned to whoever looks
	vectoangles( d, ang );
	AnglesToAxis( ang, ent.axis );
	s = 1.15f;
	VectorScale( ent.axis[0], s, ent.axis[0] );
	VectorScale( ent.axis[1], s, ent.axis[1] );
	VectorScale( ent.axis[2], s, ent.axis[2] );
	ent.nonNormalizedAxes = qtrue;
	ent.hModel = cgs.media.halfShieldModel;
	ent.customShader = shinobiShaders.guardShell;
	CG_ShinobiGuardColor( lvl, ent.shaderRGBA, k );
	ent.shaderRGBA[3] = 255;
	trap->R_AddRefEntityToScene( &ent );
}

/*
Kaiten: a dome of chakra around the spinner (two half shells, one turned to the
viewer, one away - a whole sphere), swirling; the model itself spins (CG_ShinobiKaitenSpin).
*/
static void CG_ShinobiKaitenDome( centity_t *cent ) {
	refEntity_t ent;
	vec3_t      d, ang;
	float       s;
	int         side, i;

	if ( !cgs.media.halfShieldModel || !shinobiShaders.kaitenShell )
		return;
	if ( cent->currentState.number == cg.snap->ps.clientNum && !cg.renderingThirdPerson )
		return; // (from inside it would fill the view)
	for ( side = 0; side < 2; side++ ) {
		memset( &ent, 0, sizeof( ent ) );
		VectorCopy( cent->lerpOrigin, ent.origin );
		ent.origin[2] += DEFAULT_MINS_2 + 2.0f; // the middle at the feet: a dome over the ground
		VectorSubtract( cg.refdef.vieworg, ent.origin, d );
		if ( side )
			VectorScale( d, -1.0f, d );
		vectoangles( d, ang );
		ang[ROLL] = cg.time * 0.4f * ( side ? -1.0f : 1.0f ); // turning
		AnglesToAxis( ang, ent.axis );
		s = 2.9f + 0.06f * sinf( cg.time * 0.02f ); // ~ the throw radius across
		for ( i = 0; i < 3; i++ ) {
			VectorScale( ent.axis[i], s, ent.axis[i] );
			ent.axis[i][2] *= 0.84f; // a little squashed - still well over the head
		}
		ent.nonNormalizedAxes = qtrue;
		ent.hModel = cgs.media.halfShieldModel;
		ent.customShader = shinobiShaders.kaitenShell;
		ent.shaderRGBA[0] = 150;
		ent.shaderRGBA[1] = 200;
		ent.shaderRGBA[2] = 255;
		ent.shaderRGBA[3] = 255;
		trap->R_AddRefEntityToScene( &ent );
	}
}

/*
Clinging to a wall (the stock wall grab): the third-person camera turns to look away from
the wall, the way the jump off it will go (the body faces as the hold animation wants;
the jump pushes straight off the wall). Eased in and out.
*/
float CG_ShinobiWallCamYaw( void ) {
	static float cur, lastYaw;
	static qboolean wasStuck;
	const playerState_t *ps = &cg.predictedPlayerState;
	const qboolean stuck = ( ps->pm_flags & PMF_STUCK_TO_WALL ) ? qtrue : qfalse;
	float target = 0, d, step;

	if ( wasStuck && !stuck ) // the jump off turned the body round (bg_pmove): the camera already looks that way
		cur -= AngleNormalize180( ps->viewangles[YAW] - lastYaw );
	wasStuck = stuck;
	lastYaw = ps->viewangles[YAW];

	if ( stuck ) {
		switch ( ps->legsAnim ) { // the view is the wall's normal + this turn (PM_AdjustAngleForWallJump): undo it
		case BOTH_FORCEWALLHOLD_RIGHT: case BOTH_FORCEWALLREBOUND_RIGHT: target = 90; break;
		case BOTH_FORCEWALLHOLD_LEFT:  case BOTH_FORCEWALLREBOUND_LEFT:  target = -90; break;
		case BOTH_FORCEWALLHOLD_FORWARD: case BOTH_FORCEWALLREBOUND_FORWARD: target = 180; break;
		default: target = 0; break;
		}
	}
	d = AngleNormalize180( target - cur );
	step = cg.frametime * 0.6f; // degrees per ms
	if ( fabs( d ) <= step )
		cur = target;
	else
		cur += d > 0 ? step : -step;
	cur = AngleNormalize180( cur );
	return cur;
}

// the model turns round and round (after the stock angles; the bolts follow via turAngles)
void CG_ShinobiKaitenSpin( centity_t *cent, matrix3_t legs, vec3_t rootAngles ) {
	if ( !( cent->currentState.eFlags & SHINOBI_EF_KAITEN ) || ( cent->currentState.eFlags & EF_DEAD ) )
		return;
	rootAngles[YAW] = AngleMod( rootAngles[YAW] + (float)( cg.time % 3600 ) * SHINOBI_KAITEN_SPIN );
	AnglesToAxis( rootAngles, legs );
	cent->turAngles[YAW] = rootAngles[YAW];
}

void CG_ShinobiGuardBubble( centity_t *cent ) {
	const int lvl = SHINOBI_EF_GUARD( cent->currentState.eFlags );
	if ( ( cent->currentState.eFlags & SHINOBI_EF_KAITEN ) && !( cent->currentState.eFlags & EF_DEAD ) )
		CG_ShinobiKaitenDome( cent );
	if ( !lvl || ( cent->currentState.eFlags & EF_DEAD ) )
		return;
	if ( cent->currentState.number == cg.snap->ps.clientNum && !cg.renderingThirdPerson )
		return; // (from inside it would fill the view)
	CG_ShinobiGuardDraw( cent, lvl, 0.35f + 0.05f * sinf( cg.time * 0.006f ), NULL );
}

// a blocked hit: the stock shield flash, in the guard's colour (qtrue = drawn here)
qboolean CG_ShinobiGuardShell( centity_t *cent, refEntity_t *ent, int alpha ) {
	vec3_t fwd;
	if ( !shinobiShaders.guardShell )
		return qfalse;
	VectorCopy( ent->axis[0], fwd ); // facing the blow
	CG_ShinobiGuardDraw( cent, SHINOBI_EF_GUARD( cent->currentState.eFlags ), 0.4f + 0.8f * alpha / 255.0f, fwd );
	return qtrue;
}

// the tool in hand (the stock weapon model is emptied for it)
void CG_ShinobiHandTool( centity_t *cent, clientInfo_t *ci, refEntity_t *body ) {
	static qhandle_t models[STOOL_COUNT];
	const int        tool = SHINOBI_TOOL_FOR_WEAPON( cent->currentState.weapon );
	mdxaBone_t       m;
	refEntity_t      ent;
	int              r;

	if ( tool == STOOL_NONE || !ci || !cent->ghoul2 || ci->bolt_rhand < 0 )
		return;
	if ( cent->currentState.eFlags & ( EF_DEAD | EF_NODRAW | SHINOBI_EF_RAIDASH ) )
		return;
	if ( cent->currentState.number == cg.snap->ps.clientNum && !cg.renderingThirdPerson )
		return;
	if ( !models[tool] )
		models[tool] = trap->R_RegisterModel( Shinobi_ToolModel( tool ) );
	if ( !trap->G2API_GetBoltMatrix( cent->ghoul2, 0, ci->bolt_rhand, &m, cent->turAngles, cent->lerpOrigin,
		cg.time, cgs.gameModels, cent->modelScale ) )
		return;
	memset( &ent, 0, sizeof( ent ) );
	ent.hModel = models[tool];
	for ( r = 0; r < 3; r++ ) {
		ent.origin[r] = m.matrix[r][3];
		ent.axis[0][r] = m.matrix[r][0];
		ent.axis[1][r] = m.matrix[r][1];
		ent.axis[2][r] = m.matrix[r][2];
	}
	VectorCopy( ent.origin, ent.oldorigin );
	ent.nonNormalizedAxes = qtrue;
	VectorCopy( cent->lerpOrigin, ent.lightingOrigin );
	ent.renderfx = RF_LIGHTING_ORIGIN | ( body->renderfx & ( RF_THIRD_PERSON | RF_FORCE_ENT_ALPHA ) );
	ent.shaderRGBA[0] = ent.shaderRGBA[1] = ent.shaderRGBA[2] = 255;
	ent.shaderRGBA[3] = body->shaderRGBA[3] ? body->shaderRGBA[3] : 255;
	trap->R_AddRefEntityToScene( &ent );
	CG_ShinobiOutline( cent, &ent );
}

// Lightning Dash: a crackling bolt where the body would be
static void CG_ShinobiRaiDashForm( centity_t *cent ) {
	vec3_t org, head, feet, a, b;
	vec3_t blue = { 0.6f, 0.8f, 1.0f };
	int    i;

	VectorCopy( cent->lerpOrigin, org );
	VectorCopy( org, head );
	head[2] += 28;
	VectorCopy( org, feet );
	feet[2] -= 20;
	CG_ShinobiDrawChidori( org, 26, qfalse );
	CG_ShinobiChidoriTrail( cent, org, 26 );
	trap->R_AddLightToScene( org, 220 * flrand( 0.7f, 1.0f ), 0.55f, 0.75f, 1.0f );
	for ( i = 0; i < 3; i++ ) { // the body's outline flickering as forks of lightning
		VectorSet( a, feet[0] + flrand( -10, 10 ), feet[1] + flrand( -10, 10 ), feet[2] + flrand( 0, 16 ) );
		VectorSet( b, head[0] + flrand( -10, 10 ), head[1] + flrand( -10, 10 ), head[2] + flrand( -8, 8 ) );
		trap->FX_AddLine( a, b, 2.5f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, blue, blue, 0.0f, 60,
			shinobiShaders.bolt, FX_SIZE_LINEAR | FX_ALPHA_LINEAR );
	}
}

// Sharingan red / Byakugan pale glints at the eyes, seen by everyone
static void CG_ShinobiDrawEyes( centity_t *cent, clientInfo_t *ci ) {
	const int   f = cent->currentState.eFlags2;
	const qboolean red = ( f & SHINOBI_EF2_SHARINGAN ) != 0;
	vec3_t      head, fwd, right, ang;
	mdxaBone_t  m;
	int         side;

	if ( !( f & ( SHINOBI_EF2_SHARINGAN | SHINOBI_EF2_BYAKUGAN ) ) )
		return;
	if ( cent->currentState.number == cg.snap->ps.clientNum && !cg.renderingThirdPerson )
		return;

	VectorSet( ang, 0, cent->lerpAngles[YAW], 0 );
	AngleVectors( ang, fwd, right, NULL );
	if ( cent->ghoul2 && ci && ci->bolt_head
		&& trap->G2API_GetBoltMatrix( cent->ghoul2, 0, ci->bolt_head, &m, cent->turAngles, cent->lerpOrigin, cg.time, cgs.gameModels, cent->modelScale ) ) {
		VectorSet( head, m.matrix[0][3], m.matrix[1][3], m.matrix[2][3] );
		head[2] -= 5;
	}
	else {
		VectorCopy( cent->lerpOrigin, head );
		head[2] += 30;
	}
	VectorMA( head, 4, fwd, head );

	for ( side = -1; side <= 1; side += 2 ) {
		vec3_t eye;
		VectorMA( head, 1.6f * side, right, eye );
		if ( red )
			CG_ShinobiSpriteA( eye, 1.6f, 0, shinobiShaders.glow, 1.0f, 0.1f, 0.05f, 1.0f );
		else
			CG_ShinobiSpriteA( eye, 1.5f, 0, shinobiShaders.glow, 0.85f, 0.9f, 1.0f, 0.9f );
	}
	trap->R_AddLightToScene( head, 24, red ? 1.0f : 0.7f, red ? 0.1f : 0.8f, red ? 0.05f : 1.0f );
}

// bars over the heads of whoever our eyes see
static void CG_ShinobiDrawEyeInfo( void ) {
	const int eyes = CG_ShinobiMyEyes(); // (0: only the one we are locked on to - his health)
	int i;

	if ( ( eyes == SP_SHARINGAN || eyes == SP_COPYEYE ) && shinobiMist.lastTime >= cg.time - 100 && shinobiMist.lastAmount > 0.3f )
		return; // the Sharingan can't see through the Hidden Mist
	for ( i = 0; i < MAX_GENTITIES; i++ ) {
		const shinobiSeen_t *s = CG_ShinobiSeen( i );
		centity_t *cent;
		vec3_t     top;
		float      x, y, y0, frac;
		vec4_t     back = { 0, 0, 0, 0.55f };

		if ( !s )
			continue;
		cent = &cg_entities[i];
		if ( !cent->currentValid || ( cent->currentState.eFlags & EF_DEAD ) )
			continue;
		VectorCopy( cent->lerpOrigin, top );
		top[2] += 48;
		if ( eyes == SP_SHARINGAN || eyes == SP_COPYEYE ) { // the Sharingan only reads who is in sight (the server sends updates now and then)
			trace_t tr;
			vec3_t  mid;
			CG_Trace( &tr, cg.refdef.vieworg, NULL, NULL, top, cg.snap->ps.clientNum, MASK_SOLID );
			if ( tr.fraction < 1.0f ) {
				VectorCopy( cent->lerpOrigin, mid );
				CG_Trace( &tr, cg.refdef.vieworg, NULL, NULL, mid, cg.snap->ps.clientNum, MASK_SOLID );
				if ( tr.fraction < 1.0f )
					continue;
			}
		}
		if ( !CG_WorldCoordToScreenCoordFloat( top, &x, &y ) )
			continue;
		y0 = y;

		if ( ( s->flags & SHINOBI_SEEN_HEALTH ) && ( s->flags & SHINOBI_SEEN_CLONE ) && eyes == SP_SHARINGAN ) { // the Sharingan sees through it
			vec4_t      c = { 1.0f, 0.55f, 0.5f, 0.95f };
			const float w = CG_Text_Width( "clone", 0.4f, FONT_SMALL );
			CG_Text_Paint( x - w * 0.5f, y - 4, 0.4f, c, "clone", 0, 0, ITEM_TEXTSTYLE_SHADOWED, FONT_SMALL );
			y += 10;
		}
		else if ( s->flags & SHINOBI_SEEN_HEALTH ) {
			vec4_t c = { 0.9f, 0.15f, 0.1f, 0.9f };
			frac = s->maxhp > 0 ? Com_Clamp( 0, 1, (float)s->hp / s->maxhp ) : 0;
			CG_FillRect( x - 26, y, 52, 6, back );
			CG_FillRect( x - 25, y + 1, 50 * frac, 4, c );
			y += 7;
		}
		if ( ( s->flags & SHINOBI_SEEN_CHAKRA ) && !( eyes == SP_SHARINGAN && ( s->flags & SHINOBI_SEEN_CLONE ) ) ) {
			vec4_t c = { 0.3f, 0.6f, 1.0f, 0.9f };
			frac = ( s->flags & SHINOBI_SEEN_CLONE ) ? 0.04f
				: ( s->chakraMax > 0 ? Com_Clamp( 0, 1, (float)s->chakra / s->chakraMax ) : 0 );
			CG_FillRect( x - 26, y, 52, 6, back );
			CG_FillRect( x - 25, y + 1, 50 * frac, 4, c );
			if ( s->flags & SHINOBI_SEEN_CLONE ) {
				vec4_t g = { 0.6f, 0.6f, 0.7f, 0.9f };
				const float w = CG_Text_Width( "clone", 0.35f, FONT_SMALL );
				CG_Text_Paint( x - w * 0.5f, y + 7, 0.35f, g, "clone", 0, 0, ITEM_TEXTSTYLE_SHADOWED, FONT_SMALL );
			}
		}
		if ( ( s->flags & ( SHINOBI_SEEN_HEALTH | SHINOBI_SEEN_JUTSU ) ) && s->jutsu > SJ_NONE && s->jutsu < SJ_COUNT ) {
			vec4_t      c = { 1.0f, 0.3f, 0.25f, 1.0f };
			const char *t = shinobiJutsuTitles[s->jutsu];
			const float w = CG_Text_Width( t, 0.45f, FONT_MEDIUM );
			CG_Text_Paint( x - w * 0.5f, y0 - 18, 0.45f, c, t, 0, 0, ITEM_TEXTSTYLE_SHADOWED, FONT_MEDIUM );
		}
	}
}

/*
Floating combat text: damage numbers and what a hit did (Flinch, Knockdown...) rising
over the target for a second ("stx <ent> <kind> <value>" from the server).
*/
#define SHINOBI_HITTEXT_MAX   48
#define SHINOBI_HITTEXT_MS    1100
static struct {
	int   ent, kind, value, time;
	float xofs;
} shinobiHitText[SHINOBI_HITTEXT_MAX];
static int shinobiHitTextNext;

void CG_ShinobiHitText_f( void ) {
	const int ent = atoi( CG_Argv( 1 ) ), kind = atoi( CG_Argv( 2 ) ), value = atoi( CG_Argv( 3 ) );
	int       i, n = 0;

	if ( ent < 0 || ent >= MAX_GENTITIES || kind < 0 || kind >= STX_COUNT )
		return;
	for ( i = 0; i < SHINOBI_HITTEXT_MAX; i++ ) // a few at once over the same head: spread them out
		if ( shinobiHitText[i].ent == ent && cg.time - shinobiHitText[i].time < 250 )
			n++;
	i = shinobiHitTextNext;
	shinobiHitTextNext = ( shinobiHitTextNext + 1 ) % SHINOBI_HITTEXT_MAX;
	shinobiHitText[i].ent = ent;
	shinobiHitText[i].kind = kind;
	shinobiHitText[i].value = value;
	shinobiHitText[i].time = cg.time;
	shinobiHitText[i].xofs = ( n % 2 ? 1.0f : -1.0f ) * ( 6.0f + 14.0f * n ) + flrand( -4, 4 );
}

static void CG_ShinobiDrawHitText( void ) {
	int i;

	for ( i = 0; i < SHINOBI_HITTEXT_MAX; i++ ) {
		const int   age = cg.time - shinobiHitText[i].time;
		const int   ent = shinobiHitText[i].ent;
		const int   kind = shinobiHitText[i].kind;
		const float f = age / (float)SHINOBI_HITTEXT_MS;
		const qboolean me = ent == cg.snap->ps.clientNum;
		vec4_t      color;
		vec3_t      org;
		float       x, y, scale, w;
		const char *text;

		if ( !shinobiHitText[i].time || age < 0 || age >= SHINOBI_HITTEXT_MS )
			continue;
		if ( kind == STX_DAMAGE ) {
			text = va( "-%i", shinobiHitText[i].value );
			if ( me )
				VectorSet( color, 1.0f, 0.25f, 0.2f );
			else
				VectorSet( color, 1.0f, 0.85f, 0.3f );
			scale = 0.55f + 0.25f * ( shinobiHitText[i].value >= 30 ? 1.0f : shinobiHitText[i].value / 30.0f );
		}
		else {
			text = kind == STX_BLOCK && shinobiHitText[i].value > 0
				? va( "%s %i", shinobiHitTextNames[kind], shinobiHitText[i].value )
				: ( kind == STX_CHAKRA ? va( "+%i chakra", shinobiHitText[i].value ) : shinobiHitTextNames[kind] );
			if ( kind == STX_CHAKRA )
				VectorSet( color, 0.4f, 0.75f, 1.0f );
			else if ( kind == STX_BLOCK || kind == STX_PARRY )
				VectorSet( color, 0.5f, 1.0f, 0.55f );
			else if ( kind == STX_GUARDBREAK )
				VectorSet( color, 1.0f, 0.45f, 0.2f );
			else
				VectorSet( color, 0.75f, 0.9f, 1.0f );
			scale = 0.5f;
		}
		color[3] = f < 0.7f ? 1.0f : 1.0f - ( f - 0.7f ) / 0.3f;

		if ( me && !cg.renderingThirdPerson ) { // our own: under the crosshair
			x = 320 + shinobiHitText[i].xofs;
			y = ( kind == STX_DAMAGE ? 290 : 270 ) - 40 * f;
		}
		else {
			centity_t *cent = &cg_entities[ent];
			if ( !cent->currentValid && !me )
				continue;
			VectorCopy( me ? cg.predictedPlayerState.origin : cent->lerpOrigin, org );
			org[2] += ( kind == STX_DAMAGE ? 52 : 66 ) + 36 * f;
			if ( !CG_WorldCoordToScreenCoordFloat( org, &x, &y ) )
				continue;
			x += shinobiHitText[i].xofs;
		}
		w = CG_Text_Width( text, scale, FONT_MEDIUM );
		CG_Text_Paint( x - w * 0.5f, y, scale, color, text, 0, 0, ITEM_TEXTSTYLE_SHADOWED, FONT_MEDIUM );
	}
}

void CG_ShinobiDrawHUD( void ) {
	playerState_t *ps = &cg.predictedPlayerState;
	const char    *text = NULL;
	vec4_t         color = { 0.55f, 0.85f, 1.0f, 1.0f };
	const float    scale = 0.7f;
	const qboolean chidori = SHINOBI_HAND_IS_CHIDORI( ps );

	{ // with shinobi_debug 1, log every change of the local jutsu flags
		static int lastFlags = -1;
		const int flags = ( ps->userInt1 ? 1 : 0 ) | ( ps->userInt2 ? 2 : 0 ) | ( ps->userInt3 ? 4 : 0 );
		if ( flags != lastFlags && CG_ShinobiDebug() ) {
			trap->Print( "[shinobi] local flags %i%i%i charging %i hand %i chidori %i level %i forming %i armed %i dash %i chakra %i\n",
				ps->userInt1 ? 1 : 0, ps->userInt2 ? 1 : 0, ps->userInt3 ? 1 : 0,
				SHINOBI_CHARGING( ps ) ? 1 : 0, SHINOBI_HAND_ANY( ps ) ? 1 : 0, chidori ? 1 : 0, SHINOBI_HAND_LEVEL( ps ),
				SHINOBI_HAND_FORMING( ps ) ? 1 : 0, SHINOBI_HAND_ARMED( ps ) ? 1 : 0, SHINOBI_HAND_DASH( ps ) ? 1 : 0,
				ps->fd.forcePower );
			lastFlags = flags;
		}
	}

	if ( ( CG_ShinobiMyEyes() == SP_SHARINGAN || CG_ShinobiMyEyes() == SP_COPYEYE ) && shinobiShaders.vignette ) { // the eyes are open: a dark rim (red read as low health), the eye in the corner
		vec4_t c = { 0, 0, 0, 0.8f };
		trap->R_SetColor( c );
		CG_DrawPic( 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, shinobiShaders.vignette );
		c[0] = c[1] = c[2] = 1.0f; // (the tomoe in its own colours)
		c[3] = 0.7f;
		trap->R_SetColor( c );
		if ( shinobiShaders.tomoe )
			CG_DrawRotatePic2( 30, 30, 40, 40, cg.time * 0.03f, shinobiShaders.tomoe );
		trap->R_SetColor( NULL );
	}
	CG_ShinobiDrawHitText(); // damage numbers and hit effects over heads
	if ( !CG_ShinobiCanCharge() ) {
		shinobiCharge.jutsu = SJ_NONE;
		return;
	}

	if ( CG_ShinobiMyEyes() != SP_BYAKUGAN )
		CG_ShinobiDrawEyeInfo(); // under the fog: the Sharingan can't see through mist

	if ( shinobiFog.time == cg.time && shinobiFog.amount > 0 ) {
		vec4_t fog;
		if ( shinobiFog.type == SZ_ASH )
			VectorSet4( fog, 0.12f, 0.11f, 0.1f, 0.85f * shinobiFog.amount );
		else
			VectorSet4( fog, 0.85f, 0.88f, 0.9f, 0.7f * shinobiFog.amount );
		CG_FillRect( 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, fog );
	}

	{ // blinded by a Flash: white, fading out once it is over
		static int blindSeen;
		if ( ps->stats[STAT_SHINOBI] & SHINOBI_STAT_BLIND )
			blindSeen = cg.time;
		if ( blindSeen && cg.time - blindSeen < 700 && blindSeen <= cg.time ) {
			vec4_t white = { 1.0f, 1.0f, 0.95f, 0.0f };
			white[3] = ( ps->stats[STAT_SHINOBI] & SHINOBI_STAT_BLIND ) ? 0.93f : 0.93f * ( 1.0f - ( cg.time - blindSeen ) / 700.0f );
			CG_FillRect( 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, white );
		}
	}

	if ( ps->stats[STAT_SHINOBI] & SHINOBI_STAT_CURSE ) { // the seal clouds the mind
		vec4_t dark = { 0.08f, 0.0f, 0.1f, 0.0f };
		dark[3] = 0.28f + 0.08f * sinf( cg.time * 0.004f );
		CG_FillRect( 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, dark );
	}

	if ( CG_ShinobiMyEyes() == SP_BYAKUGAN )
		CG_ShinobiDrawEyeInfo();

	CG_ShinobiDrawLock();
	CG_ShinobiDrawBladeCombo();
	CG_ShinobiDrawJutsuList();
	CG_ShinobiDrawCharges();
	CG_ShinobiDrawGates();


	if ( shinobiCharge.jutsu && !SHINOBI_JUTSU_HOLD( shinobiCharge.jutsu ) ) {
		CG_ShinobiDrawChargeBars();
	}

	if ( ps->stats[STAT_SHINOBI2] & SHINOBI_STAT2_SHADOWTRAP ) {
		text = "Shadow Trap! Held in place - any hit frees you, Kawarimi (X) to escape";
		VectorSet( color, 0.7f, 0.7f, 0.75f );
		color[3] = 0.7f + 0.3f * sinf( cg.time * 0.01f );
	}
	else if ( ps->stats[STAT_SHINOBI] & SHINOBI_STAT_BOUND ) {
		text = "Shadow Bind! You copy his moves - Kawarimi (X) to escape";
		VectorSet( color, 0.7f, 0.7f, 0.75f );
		color[3] = 0.7f + 0.3f * sinf( cg.time * 0.01f );
	}
	else if ( ps->stats[STAT_SHINOBI] & SHINOBI_STAT_SHADOWHIDE ) {
		text = "In your shadow - let go of F (or attack) to rise; fire, lightning and light drive you out";
		VectorSet( color, 0.6f, 0.55f, 0.75f );
		color[3] = 0.7f + 0.3f * sinf( cg.time * 0.006f );
	}
	else if ( ps->stats[STAT_SHINOBI] & SHINOBI_STAT_BURROW ) {
		text = "Under the ground - let go of F (or attack) to burst out";
		VectorSet( color, 0.8f, 0.65f, 0.45f );
		color[3] = 0.7f + 0.3f * sinf( cg.time * 0.008f );
	}
	else if ( ps->stats[STAT_SHINOBI] & SHINOBI_STAT_STAGGER ) {
		text = "Staggered!";
		VectorSet( color, 1.0f, 0.6f, 0.3f );
	}
	else if ( ps->stats[STAT_SHINOBI] & SHINOBI_STAT_CURSEWEAK ) {
		text = "The Curse Seal stirs - too weak to awaken";
		VectorSet( color, 0.55f, 0.35f, 0.65f );
		color[3] = 0.8f;
	}
	else if ( ps->stats[STAT_SHINOBI] & SHINOBI_STAT_CURSE ) {
		text = "Curse Seal awakened - it feeds on your health";
		VectorSet( color, 0.8f, 0.3f, 1.0f );
		color[3] = 0.65f + 0.35f * sinf( cg.time * 0.008f );
	}
	else if ( SHINOBI_STUNNED( ps ) ) {
		const qboolean shocked = ps->electrifyTime > cg.time;
		text = shocked ? "Paralysed by lightning!" : "Gasping for air!";
		VectorSet( color, 0.6f, 0.8f, 1.0f );
	}
	else if ( SHINOBI_IMPRISONED( ps ) ) {
		text = "Water Prison! Kawarimi (X) to escape";
		VectorSet( color, 0.5f, 0.75f, 1.0f );
		color[3] = 0.7f + 0.3f * sinf( cg.time * 0.01f );
	}
	else if ( SHINOBI_PRISON_CASTER( ps ) ) {
		text = "Holding the Water Prison - attack or jump to let go";
		VectorSet( color, 0.5f, 0.75f, 1.0f );
	}
	else if ( SHINOBI_BURNING( ps ) ) {
		text = "Burning! Water puts it out";
		VectorSet( color, 1.0f, 0.55f, 0.2f );
		color[3] = 0.7f + 0.3f * sinf( cg.time * 0.015f );
	}
	else if ( ps->stats[STAT_SHINOBI] & SHINOBI_STAT_TIRED ) {
		text = "Exhausted after the Gates - no chakra regen";
		VectorSet( color, 0.7f, 0.7f, 0.5f );
	}
	else if ( SHINOBI_HIDDEN( ps ) ) {
		text = "Hidden - move slowly, any action reveals you";
		color[0] = 0.7f; color[1] = 0.7f; color[2] = 0.7f;
		color[3] = 0.6f + 0.25f * sinf( cg.time * 0.004f );
	}
	else if ( SHINOBI_CHARGING( ps ) ) {
		text = va( "Gathering chakra... %i", ps->fd.forcePower );
	}
	else if ( SHINOBI_HAND_FORMING( ps ) ) {
		text = chidori ? "Chidori forming..." : "Rasengan forming...";
	}
	else if ( SHINOBI_HAND_ARMED( ps ) ) {
		text = va( "%s Lv %i - ATTACK!", chidori ? "CHIDORI" : "RASENGAN", SHINOBI_HAND_LEVEL( ps ) );
		color[3] = 0.65f + 0.35f * sinf( cg.time * 0.012f );
	}
	else if ( ps->stats[STAT_SHINOBI] & SHINOBI_STAT_ARMOR ) {
		text = "Stone Skin - heavy and hard to hurt";
		VectorSet( color, 0.8f, 0.7f, 0.5f );
		color[3] = 0.7f;
	}
	else if ( ps->stats[STAT_SHINOBI] & SHINOBI_STAT_WINDEDGE ) {
		text = "Wind Edge - slashes fly ahead, cut through guards";
		VectorSet( color, 0.6f, 0.9f, 0.9f );
		color[3] = 0.7f;
	}
	else if ( ps->stats[STAT_SHINOBI] & SHINOBI_STAT_RAIEDGE ) {
		text = "Lightning Edge - hits 20% harder, 3x on stone";
		VectorSet( color, 0.6f, 0.75f, 1.0f );
		color[3] = 0.7f;
	}

	if ( text ) {
		const int w = CG_Text_Width( text, scale, FONT_MEDIUM );
		CG_Text_Paint( ( SCREEN_WIDTH - w ) * 0.5f, 340, scale, color, text, 0, 0, ITEM_TEXTSTYLE_SHADOWED, FONT_MEDIUM );
	}
}

/*
===========================================================================
Build screen ("shinobi_build_menu", B): it lives in the UI module now
(ui_shinobi.c), in place of the stock profile / force-rank menus.
===========================================================================
*/
void CG_ShinobiMenu_f( void ) {
	if ( cg.demoPlayback )
		return;
	trap->OpenUIMenu( UIMENU_PLAYERFORCE );
}
