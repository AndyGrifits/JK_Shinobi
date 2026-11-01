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
#include "fx_local.h"
#include "ui/keycodes.h"

#define SHINOBI_HAND_FX_INTERVAL  60   // ms between hand spark effects
#define SHINOBI_AURA_FX_INTERVAL  70
#define SHINOBI_BALL_FX_INTERVAL  40

static struct {
	qhandle_t glow, swirl, bolt, fire, spark;
	qhandle_t puddle, mud, scorch, waterSphere, ring, shadowBand, fogScreen, mistShell;
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
	fxHandle_t fireGround, fireBody, steam, waterTrail, drip, bubbles, zoneSpark, waveCrest, flame, ash, mist;
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
	shinobiFx.mist       = CG_ShinobiRegisterFx( SHINOBI_FX_MIST );
	shinobiShaders.shadowBand = trap->R_RegisterShader( "gfx/shinobi/shadowband" );
	shinobiShaders.fogScreen  = trap->R_RegisterShader( "gfx/shinobi/fogscreen" );
	shinobiShaders.mistShell  = trap->R_RegisterShader( "gfx/shinobi/mistshell" );

	memset( &shinobiCharge, 0, sizeof( shinobiCharge ) );
	shinobiSel.jumpHeld = qfalse;
	shinobiSel.changeTime = 0;
	shinobiSel.index = 0;
	shinobiActiveSet = qfalse;
	trap->Cvar_Register( &shinobiBuildCvar, "shinobi_build", "", CVAR_USERINFO | CVAR_ARCHIVE );

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
	shinobiCharge.jutsu = jutsu;
	shinobiCharge.start = cg.time;
	shinobiCharge.fromJump = qfalse;
	trap->SendClientCommand( va( "jutsu_begin %s", shinobiJutsuNames[jutsu] ) );
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
	const shinobiBuild_t *b = CG_ShinobiBuild();
	if ( !b->numJutsu )
		return;
	if ( shinobiSel.index >= b->numJutsu )
		shinobiSel.index = 0;
	CG_ShinobiJutsuDown( b->jutsu[shinobiSel.index] );
}

// "sbuild <string>": the server applied our build
void CG_ShinobiSetBuild_f( void ) {
	Shinobi_BuildParse( CG_Argv( 1 ), &shinobiActive );
	Shinobi_BuildFix( &shinobiActive );
	shinobiActiveSet = qtrue;
	if ( shinobiSel.index >= shinobiActive.numJutsu )
		shinobiSel.index = 0;
}

void CG_ShinobiCastUp_f( void ) {
	if ( shinobiCharge.jutsu && !shinobiCharge.fromJump )
		CG_ShinobiJutsuUp( shinobiCharge.jutsu ); // whatever this key started, even if the selection moved
}

static void CG_ShinobiSelect( int step ) {
	const int n = CG_ShinobiBuild()->numJutsu;
	if ( !n )
		return;
	shinobiSel.index = ( shinobiSel.index + step + n ) % n;
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
	CG_ShinobiMenuAuto();
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
	else {
		cent->shinobiTrailTime = 0;
	}

	// --- element statuses (timers share shinobiAuraFxTime with the chakra aura) ---
	if ( SHINOBI_IMPRISONED( s ) ) { // floating in a ball of water
		const float t = cg.time * 0.001f;
		VectorCopy( cent->lerpOrigin, org );
		org[2] += 4;
		CG_ShinobiSpriteA( org, SHINOBI_PRISON_RADIUS, t * 20.0f, shinobiShaders.waterSphere, 0.55f, 0.75f, 1.0f, 0.55f );
		CG_ShinobiSpriteA( org, SHINOBI_PRISON_RADIUS * 0.92f, -t * 35.0f, shinobiShaders.waterSphere, 0.4f, 0.6f, 0.95f, 0.35f );
		CG_ShinobiSpriteA( org, SHINOBI_PRISON_RADIUS * 1.05f, t * 50.0f, shinobiShaders.swirl, 0.25f, 0.4f, 0.6f, 1.0f );
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

	CG_ShinobiSpriteA( cent->lerpOrigin, R * 1.15f, t * 90.0f, shinobiShaders.waterSphere, 0.6f, 0.8f, 1.0f, 0.85f );
	CG_ShinobiSpriteA( cent->lerpOrigin, R * 0.95f, -t * 160.0f, shinobiShaders.swirl, 0.3f, 0.5f, 0.8f, 1.0f );
	CG_ShinobiSprite( cent->lerpOrigin, R * 0.5f, 0, shinobiShaders.glow, 0.25f, 0.4f, 0.6f );

	VectorCopy( cent->currentState.pos.trDelta, dir );
	if ( VectorNormalize( dir ) == 0 )
		VectorSet( dir, 0, 0, 1 );
	if ( cent->shinobiHandFxTime <= cg.time || cent->shinobiHandFxTime > cg.time + 1000 ) {
		trap->FX_PlayEffectID( shinobiFx.waterTrail, cent->lerpOrigin, dir, -1, -1, qfalse );
		cent->shinobiHandFxTime = cg.time + SHINOBI_BALL_FX_INTERVAL;
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
	entityState_t *s = &cent->currentState;
	const float    half = s->angles2[0], H = (float)SHINOBI_WAVE_HEIGHT, t = cg.time * 0.001f;
	vec3_t         angles, fwd, right, pos;
	float          x;
	int            k = 0;

	VectorSet( angles, 0, s->angles2[1], 0 );
	AngleVectors( angles, fwd, right, NULL );

	for ( x = -half; x <= half + 0.1f; x += 22.0f, k++ ) {
		const float bob = 4.0f * sinf( t * 9.0f + k * 1.7f );
		VectorMA( cent->lerpOrigin, x, right, pos );
		pos[2] += H * 0.4f + bob;
		CG_ShinobiSpriteA( pos, H * 0.6f, t * 60.0f + k * 40, shinobiShaders.waterSphere, 0.45f, 0.65f, 0.95f, 0.6f );
		VectorMA( pos, 6, fwd, pos );
		pos[2] += H * 0.35f;
		CG_ShinobiSpriteA( pos, H * 0.4f, -t * 120.0f + k * 70, shinobiShaders.swirl, 0.3f, 0.45f, 0.65f, 1.0f );
	}

	if ( cent->shinobiHandFxTime <= cg.time || cent->shinobiHandFxTime > cg.time + 1000 ) {
		VectorMA( cent->lerpOrigin, flrand( -half, half ), right, pos );
		pos[2] += H * 0.85f;
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
		CG_ShinobiGroundDecal( org, R, rot, shinobiShaders.puddle, 0.45f, 0.65f, 0.9f, 0.85f * a );
		break;
	case SZ_ELECTRIC: {
		const float f = flrand( 0.5f, 1.0f );
		CG_ShinobiGroundDecal( org, R, rot, shinobiShaders.puddle, 0.6f, 0.85f, 1.0f, a );
		p[0] = org[0]; p[1] = org[1]; p[2] = org[2] + 16;
		trap->R_AddLightToScene( p, R * 1.4f * f * a, 0.5f, 0.7f, 1.0f );
		fx = shinobiFx.zoneSpark;
		interval = (int)( 70 / ( area < 0.4f ? 0.4f : area ) );
		break;
	}
	case SZ_MUD:
		CG_ShinobiGroundDecal( org, R, rot, shinobiShaders.mud, 1, 1, 1, a );
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
		trap->FX_PlayEffectID( fx, p, up, -1, -1, qfalse );
		cent->shinobiHandFxTime = cg.time + ( interval < 15 ? 15 : interval );
	}
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
	if ( s->generic1 >= SHINOBI_ENT_BLADE && s->generic1 < SHINOBI_ENT_BLADE + SHINOBI_MAX_LEVEL ) {
		CG_ShinobiBlade( cent );
		return qtrue;
	}
	if ( s->generic1 >= SHINOBI_ENT_WATERBALL && s->generic1 < SHINOBI_ENT_WATERBALL + SHINOBI_MAX_LEVEL ) {
		CG_ShinobiWaterball( cent );
		return qtrue;
	}
	if ( s->generic1 < SHINOBI_ENT_FIREBALL || s->generic1 >= SHINOBI_ENT_FIREBALL + SHINOBI_MAX_LEVEL ) {
		return qfalse;
	}
	li = s->generic1 - SHINOBI_ENT_FIREBALL;
	R = shinobiFireballVisual[li];
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

	while ( shown > SHINOBI_JUTSU_MIN_LEVEL( shinobiCharge.jutsu ) && chakra < Shinobi_JutsuCost( shinobiCharge.jutsu, shown ) )
		shown--;

	for ( i = 0; i < SHINOBI_MAX_LEVEL; i++ ) {
		const int   begin = i ? Shinobi_BarEndCast( shinobiCharge.jutsu, i - 1, castLvl ) : 0;
		const float fill = Com_Clamp( 0, 1, (float)( held - begin ) / (float)( Shinobi_BarEndCast( shinobiCharge.jutsu, i, castLvl ) - begin ) );
		const float x = x0 + i * ( barW + gap );
		const qboolean affordable = chakra >= Shinobi_JutsuCost( shinobiCharge.jutsu, i + 1 );

		CG_FillRect( x, y, barW, barH, back );
		if ( fill > 0 )
			CG_FillRect( x, y, barW * fill, barH, affordable ? barColor[i] : locked );
		CG_DrawRect( x, y, barW, barH, 1, frame );
	}

	if ( held < Shinobi_BarEndCast( shinobiCharge.jutsu, SHINOBI_JUTSU_MIN_LEVEL( shinobiCharge.jutsu ) - 1, castLvl ) ) {
		label = va( "%s - forming seals...", shinobiJutsuTitles[shinobiCharge.jutsu] );
		VectorSet( textColor, 0.8f, 0.8f, 0.8f );
	}
	else if ( chakra < Shinobi_JutsuCost( shinobiCharge.jutsu, 1 ) ) {
		label = va( "%s - not enough chakra (%i)", shinobiJutsuTitles[shinobiCharge.jutsu], Shinobi_JutsuCost( shinobiCharge.jutsu, 1 ) );
		VectorSet( textColor, 1, 0.4f, 0.4f );
	}
	else {
		label = va( "%s  Lv %i  (%i chakra)", shinobiJutsuTitles[shinobiCharge.jutsu], shown, Shinobi_JutsuCost( shinobiCharge.jutsu, shown ) );
		VectorCopy( barColor[shown - 1], textColor );
	}
	{
		const float scale = 0.6f;
		const int w = CG_Text_Width( label, scale, FONT_MEDIUM );
		CG_Text_Paint( ( SCREEN_WIDTH - w ) * 0.5f, y - 18, scale, textColor, label, 0, 0, ITEM_TEXTSTYLE_SHADOWED, FONT_MEDIUM );
	}
}

// the row of selectable jutsu at the bottom of the screen
static void CG_ShinobiDrawJutsuList( void ) {
	const float scale = 0.42f, gap = 10, y = 448;
	const int   chakra = cg.predictedPlayerState.fd.forcePower;
	const float recent = Com_Clamp( 0, 1, 1.0f - ( cg.time - shinobiSel.changeTime - 1500 ) / 500.0f );
	float       total = 0, x;
	int         i;

	const shinobiBuild_t *b = CG_ShinobiBuild();

	if ( !b->numJutsu )
		return;
	if ( shinobiSel.index >= b->numJutsu )
		shinobiSel.index = 0;
	for ( i = 0; i < b->numJutsu; i++ )
		total += CG_Text_Width( shinobiJutsuTitles[b->jutsu[i]], scale, FONT_MEDIUM ) + ( i ? gap : 0 );
	x = ( SCREEN_WIDTH - total ) * 0.5f;

	for ( i = 0; i < b->numJutsu; i++ ) {
		const int    j = b->jutsu[i];
		const char  *title = shinobiJutsuTitles[j];
		const float  w = CG_Text_Width( title, scale, FONT_MEDIUM );
		const qboolean selected = ( i == shinobiSel.index );
		const qboolean affordable = chakra >= Shinobi_JutsuCost( j, 1 );
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
		CG_Text_Paint( x, y, scale, color, title, 0, 0, ITEM_TEXTSTYLE_SHADOWED, FONT_MEDIUM );
		x += w + gap;
	}

	if ( recent > 0 ) { // just picked: show what the levels cost
		const int   j = b->jutsu[shinobiSel.index];
		const char *info = SHINOBI_JUTSU_INSTANT( j )
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
	if ( cg.snap->ps.eFlags2 & SHINOBI_EF2_SHARINGAN )
		return SP_SHARINGAN;
	if ( cg.snap->ps.eFlags2 & SHINOBI_EF2_BYAKUGAN )
		return SP_BYAKUGAN;
	return 0;
}

// a fog sheet in front of the camera inside a Hidden Mist (3D, so shapes can be drawn over it)
void CG_ShinobiSceneOverlay( void ) {
	refEntity_t ent;
	float       a;

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
void CG_ShinobiEyeShell( centity_t *cent, refEntity_t *body ) {
	const shinobiSeen_t *seen;
	refEntity_t shell;

	if ( cent->currentState.number == cg.snap->ps.clientNum || ( cent->currentState.eFlags & EF_DEAD ) )
		return;
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
	const int eyes = CG_ShinobiMyEyes();
	int i;

	if ( !eyes )
		return;
	if ( eyes == SP_SHARINGAN && shinobiMist.lastTime >= cg.time - 100 && shinobiMist.lastAmount > 0.3f )
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
		if ( !CG_WorldCoordToScreenCoordFloat( top, &x, &y ) )
			continue;
		y0 = y;

		if ( s->flags & SHINOBI_SEEN_HEALTH ) {
			vec4_t c = { 0.9f, 0.15f, 0.1f, 0.9f };
			frac = s->maxhp > 0 ? Com_Clamp( 0, 1, (float)s->hp / s->maxhp ) : 0;
			CG_FillRect( x - 26, y, 52, 6, back );
			CG_FillRect( x - 25, y + 1, 50 * frac, 4, c );
			y += 7;
		}
		if ( s->flags & SHINOBI_SEEN_CHAKRA ) {
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
		if ( ( s->flags & SHINOBI_SEEN_HEALTH ) && s->jutsu > SJ_NONE && s->jutsu < SJ_COUNT ) {
			vec4_t      c = { 1.0f, 0.3f, 0.25f, 1.0f };
			const char *t = shinobiJutsuTitles[s->jutsu];
			const float w = CG_Text_Width( t, 0.45f, FONT_MEDIUM );
			CG_Text_Paint( x - w * 0.5f, y0 - 18, 0.45f, c, t, 0, 0, ITEM_TEXTSTYLE_SHADOWED, FONT_MEDIUM );
		}
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

	if ( ps->stats[STAT_SHINOBI] & SHINOBI_STAT_CURSE ) { // the seal clouds the mind
		vec4_t dark = { 0.08f, 0.0f, 0.1f, 0.0f };
		dark[3] = 0.28f + 0.08f * sinf( cg.time * 0.004f );
		CG_FillRect( 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, dark );
	}

	if ( CG_ShinobiMyEyes() == SP_BYAKUGAN )
		CG_ShinobiDrawEyeInfo();

	CG_ShinobiDrawJutsuList();

	{ // Shift held with fists or the blade: which nature the next hit carries
		usercmd_t cmd;
		if ( ( ps->weapon == WP_MELEE || ps->weapon == WP_SABER )
			&& trap->GetUserCmd( trap->GetCurrentCmdNumber(), &cmd ) && SHINOBI_MODIFIER( &cmd ) ) {
			static const char * const nature[SS_COUNT] = { "chakra", "fire", "water", "wind", "earth", "lightning", "chakra" };
			const char  *label = va( "Shift+attack: %s (%i chakra)", nature[Shinobi_BuildElement( CG_ShinobiBuild() )], SHINOBI_INFUSE_COST );
			const float  w = CG_Text_Width( label, 0.45f, FONT_MEDIUM );
			vec4_t       c = { 0.9f, 0.85f, 0.5f, 0.85f };
			CG_Text_Paint( ( SCREEN_WIDTH - w ) * 0.5f, 262, 0.45f, c, label, 0, 0, ITEM_TEXTSTYLE_SHADOWED, FONT_MEDIUM );
		}
	}

	if ( shinobiCharge.jutsu && !SHINOBI_JUTSU_HOLD( shinobiCharge.jutsu ) ) {
		CG_ShinobiDrawChargeBars();
	}

	if ( ( ps->stats[STAT_SHINOBI] & SHINOBI_STAT_GATES ) && !( ps->stats[STAT_SHINOBI] & SHINOBI_STAT_BOUND ) ) {
		text = "Inner Gates open - strike now!";
		VectorSet( color, 0.4f, 1.0f, 0.5f );
		color[3] = 0.7f + 0.3f * sinf( cg.time * 0.012f );
	}
	else if ( ps->stats[STAT_SHINOBI] & SHINOBI_STAT_BOUND ) {
		text = "Shadow Bind! You copy his moves - Kawarimi (X) to escape";
		VectorSet( color, 0.7f, 0.7f, 0.75f );
		color[3] = 0.7f + 0.3f * sinf( cg.time * 0.01f );
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
		text = "Gasping for air!";
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

	if ( text ) {
		const int w = CG_Text_Width( text, scale, FONT_MEDIUM );
		CG_Text_Paint( ( SCREEN_WIDTH - w ) * 0.5f, 340, scale, color, text, 0, 0, ITEM_TEXTSTYLE_SHADOWED, FONT_MEDIUM );
	}
}

/*
===========================================================================
Build screen ("shinobi_build_menu", B): techniques, stats and a special
ability, all on one screen, driven by the mouse. Saving writes the
"shinobi_build" userinfo cvar; the server applies it on the next spawn.
===========================================================================
*/
static struct {
	qboolean       open;
	qboolean       autoShown;
	shinobiBuild_t edit;
	float          cx, cy;           // cursor (640x480)
	qboolean       click;
	float          clickX, clickY;
	char           msg[128];
	int            msgTime;
	qboolean       msgBad;
	const char    *hover;
} sm;

static const vec4_t smSchoolColor[SS_COUNT] = {
	{ 0.8f, 0.8f, 0.8f, 1 },
	{ 1.0f, 0.5f, 0.2f, 1 },  // fire
	{ 0.4f, 0.7f, 1.0f, 1 },  // water
	{ 0.6f, 0.95f, 0.6f, 1 }, // wind
	{ 0.85f, 0.65f, 0.4f, 1 },// earth
	{ 0.8f, 0.85f, 1.0f, 1 }, // lightning
	{ 0.85f, 0.6f, 1.0f, 1 }, // yin / yang
};

static void SM_Message( qboolean bad, const char *text ) {
	Q_strncpyz( sm.msg, text, sizeof( sm.msg ) );
	sm.msgTime = cg.time;
	sm.msgBad = bad;
}

static void CG_ShinobiMenuClose( void ) {
	sm.open = qfalse;
	sm.click = qfalse;
	trap->Key_SetCatcher( trap->Key_GetCatcher() & ~KEYCATCH_CGAME );
}

void CG_ShinobiMenu_f( void ) {
	char buf[256];

	if ( sm.open ) {
		CG_ShinobiMenuClose();
		return;
	}
	trap->Cvar_VariableStringBuffer( "shinobi_build", buf, sizeof( buf ) );
	Shinobi_BuildParse( buf, &sm.edit );
	Shinobi_BuildFix( &sm.edit );
	sm.open = qtrue;
	sm.autoShown = qtrue;
	sm.cx = 320;
	sm.cy = 240;
	sm.click = qfalse;
	sm.msg[0] = 0;
	trap->Key_SetCatcher( trap->Key_GetCatcher() | KEYCATCH_CGAME );
}

static void CG_ShinobiMenuSave( void ) {
	char why[128], str[256];

	if ( !Shinobi_BuildValid( &sm.edit, why, sizeof( why ) ) ) {
		SM_Message( qtrue, why );
		return;
	}
	Shinobi_BuildString( &sm.edit, str, sizeof( str ) );
	trap->Cvar_Set( "shinobi_build", str );
	CG_ShinobiMenuClose();
	trap->Print( "[shinobi] build saved: %s\n", str );
}

// key and mouse input while the screen is open (qtrue = eaten)
qboolean CG_ShinobiMenuKey( int key, qboolean down ) {
	if ( !sm.open )
		return qfalse;
	if ( !down )
		return qtrue;
	if ( key == A_MOUSE1 ) {
		sm.click = qtrue;
		sm.clickX = sm.cx;
		sm.clickY = sm.cy;
	}
	else if ( key == A_ENTER || key == A_KP_ENTER ) {
		CG_ShinobiMenuSave();
	}
	else if ( key == A_ESCAPE || key == 'b' || key == 'B' ) {
		CG_ShinobiMenuClose();
	}
	return qtrue;
}

qboolean CG_ShinobiMenuMouse( int dx, int dy ) {
	// raw mouse counts ~ screen pixels: convert to the 640x480 layout so the
	// pointer moves like the desktop cursor on any resolution / aspect
	const float sx = cgs.glconfig.vidWidth > 0 ? (float)SCREEN_WIDTH / cgs.glconfig.vidWidth : 1.0f;
	const float sy = cgs.glconfig.vidHeight > 0 ? (float)SCREEN_HEIGHT / cgs.glconfig.vidHeight : 1.0f;

	if ( !sm.open )
		return qfalse;
	sm.cx = Com_Clamp( 0, SCREEN_WIDTH - 1, sm.cx + dx * sx * 1.5f );
	sm.cy = Com_Clamp( 0, SCREEN_HEIGHT - 1, sm.cy + dy * sy * 1.5f );
	return qtrue;
}

static qboolean SM_Over( float x, float y, float w, float h ) {
	return ( sm.cx >= x && sm.cx < x + w && sm.cy >= y && sm.cy < y + h ) ? qtrue : qfalse;
}

static qboolean SM_Clicked( float x, float y, float w, float h ) {
	if ( sm.click && sm.clickX >= x && sm.clickX < x + w && sm.clickY >= y && sm.clickY < y + h ) {
		sm.click = qfalse;
		return qtrue;
	}
	return qfalse;
}

static void SM_Text( float x, float y, float scale, const vec4_t color, const char *text ) {
	CG_Text_Paint( x, y, scale, (float *)color, text, 0, 0, ITEM_TEXTSTYLE_SHADOWED, FONT_MEDIUM );
}

static qboolean SM_Button( float x, float y, float w, float h, const char *label, qboolean enabled ) {
	const vec4_t bg = { 0.12f, 0.25f, 0.4f, 0.9f }, bgHot = { 0.2f, 0.4f, 0.65f, 0.95f };
	const vec4_t frame = { 1, 1, 1, 0.4f }, white = { 1, 1, 1, 1 }, grey = { 0.5f, 0.5f, 0.5f, 1 };
	const float  tw = CG_Text_Width( label, 0.45f, FONT_MEDIUM );

	CG_FillRect( x, y, w, h, enabled && SM_Over( x, y, w, h ) ? bgHot : bg );
	CG_DrawRect( x, y, w, h, 1, frame );
	SM_Text( x + ( w - tw ) * 0.5f, y + 2, 0.45f, enabled ? white : grey, label );
	return ( SM_Clicked( x, y, w, h ) && enabled ) ? qtrue : qfalse;
}

static void SM_ToggleJutsu( int j ) {
	shinobiBuild_t test;
	int            i;

	for ( i = 0; i < sm.edit.numJutsu; i++ ) {
		if ( sm.edit.jutsu[i] == j ) { // remove
			for ( ; i < sm.edit.numJutsu - 1; i++ )
				sm.edit.jutsu[i] = sm.edit.jutsu[i + 1];
			sm.edit.numJutsu--;
			return;
		}
	}
	if ( sm.edit.numJutsu >= SHINOBI_BUILD_JUTSU ) {
		SM_Message( qtrue, va( "At most %i techniques - remove one first", SHINOBI_BUILD_JUTSU ) );
		return;
	}
	test = sm.edit;
	test.jutsu[test.numJutsu++] = j;
	if ( Shinobi_BuildSchools( &test, NULL ) > SHINOBI_BUILD_SCHOOLS ) {
		SM_Message( qtrue, va( "Techniques from at most %i schools", SHINOBI_BUILD_SCHOOLS ) );
		return;
	}
	sm.edit = test;
}

void CG_ShinobiDrawMenu( void ) {
	const vec4_t panel = { 0, 0, 0, 0.85f }, frame = { 0.5f, 0.7f, 1, 0.5f };
	const vec4_t white = { 1, 1, 1, 1 }, dim = { 0.7f, 0.7f, 0.7f, 1 }, title = { 0.55f, 0.85f, 1, 1 };
	const vec4_t on = { 0.25f, 0.55f, 0.85f, 0.9f }, off = { 0.15f, 0.15f, 0.15f, 0.9f }, hot = { 1, 1, 1, 0.08f };
	const vec4_t good = { 0.5f, 1, 0.5f, 1 }, bad = { 1, 0.45f, 0.4f, 1 };
	const float  s = 0.45f, rowH = 14;
	float        x, y;
	int          sc, i, k, schoolsUsed;

	if ( !sm.open )
		return;
	if ( !( trap->Key_GetCatcher() & KEYCATCH_CGAME ) ) { // closed from outside (ESC)
		sm.open = qfalse;
		return;
	}
	sm.hover = NULL;

	CG_FillRect( 16, 20, 608, 440, panel );
	CG_DrawRect( 16, 20, 608, 440, 1, frame );
	SM_Text( 30, 26, 0.75f, title, "NINJA BUILD" );
	SM_Text( 330, 32, 0.4f, dim, "Enter: save   B / Esc: close   applies on respawn" );

	// --- techniques -----------------------------------------------------
	schoolsUsed = Shinobi_BuildSchools( &sm.edit, NULL );
	x = 30;
	y = 58;
	SM_Text( x, y, 0.5f, white, va( "TECHNIQUES  %i/%i  schools %i/%i", sm.edit.numJutsu, SHINOBI_BUILD_JUTSU,
		schoolsUsed, SHINOBI_BUILD_SCHOOLS ) );
	y += 18;
	for ( sc = SS_KATON; sc < SS_COUNT; sc++ ) {
		SM_Text( x, y, s, smSchoolColor[sc], shinobiSchoolTitles[sc] );
		y += 15;
		for ( k = 0; k < SHINOBI_CYCLE_COUNT; k++ ) {
			const int      j = shinobiJutsuCycle[k];
			const qboolean have = Shinobi_BuildHas( &sm.edit, j );

			if ( shinobiJutsuSchool[j] != sc )
				continue;
			if ( SM_Over( x + 8, y, 240, rowH ) ) {
				CG_FillRect( x + 8, y, 240, rowH, hot );
				sm.hover = shinobiJutsuInfo[j];
			}
			CG_FillRect( x + 10, y + 3, 9, 9, have ? on : off );
			CG_DrawRect( x + 10, y + 3, 9, 9, 1, frame );
			SM_Text( x + 26, y, s, have ? white : dim, shinobiJutsuTitles[j] );
			SM_Text( x + 150, y, 0.38f, dim, va( "%i/%i/%i", Shinobi_JutsuCost( j, 1 ), Shinobi_JutsuCost( j, 2 ), Shinobi_JutsuCost( j, 3 ) ) );
			if ( SM_Clicked( x + 8, y, 240, rowH ) )
				SM_ToggleJutsu( j );
			y += rowH;
		}
		y += 3;
	}
	SM_Text( x, y + 2, 0.38f, dim, "always: Shunshin (Shift+Space), Kawarimi (X), chakra (R), fists" );

	// --- stats ----------------------------------------------------------
	x = 300;
	y = 58;
	SM_Text( x, y, 0.5f, white, va( "STATS  points left %i", SHINOBI_STAT_POINTS - Shinobi_BuildPoints( &sm.edit ) ) );
	y += 22;
	for ( i = 0; i < SST_COUNT; i++ ) {
		if ( SM_Over( x, y, 160, 32 ) )
			sm.hover = shinobiStatInfo[i];
		SM_Text( x, y, s, white, shinobiStatTitles[i] );
		if ( SM_Button( x, y + 15, 16, 13, "-", sm.edit.stats[i] > 0 ) )
			sm.edit.stats[i]--;
		for ( k = 0; k < SHINOBI_STAT_MAX; k++ ) {
			CG_FillRect( x + 22 + k * 20, y + 17, 16, 9, k < sm.edit.stats[i] ? on : off );
			CG_DrawRect( x + 22 + k * 20, y + 17, 16, 9, 1, frame );
		}
		if ( SM_Button( x + 126, y + 15, 16, 13, "+", sm.edit.stats[i] < SHINOBI_STAT_MAX
			&& Shinobi_BuildPoints( &sm.edit ) < SHINOBI_STAT_POINTS ) )
			sm.edit.stats[i]++;
		y += 38;
	}

	// --- special --------------------------------------------------------
	x = 476;
	y = 58;
	SM_Text( x, y, 0.5f, white, "SPECIAL" );
	y += 22;
	for ( i = 0; i < SP_COUNT; i++ ) {
		const qboolean chosen = ( sm.edit.perk == i );
		const qboolean ready = shinobiPerkReady[i];

		if ( SM_Over( x, y, 140, rowH ) ) {
			CG_FillRect( x, y, 140, rowH, hot );
			sm.hover = shinobiPerkInfo[i];
		}
		CG_FillRect( x + 2, y + 3, 9, 9, chosen ? on : off );
		CG_DrawRect( x + 2, y + 3, 9, 9, 1, frame );
		SM_Text( x + 18, y, s, ready ? ( chosen ? white : dim ) : off, ready ? shinobiPerkTitles[i] : va( "%s (soon)", shinobiPerkTitles[i] ) );
		if ( SM_Clicked( x, y, 140, rowH ) ) {
			if ( ready )
				sm.edit.perk = i;
			else
				SM_Message( qtrue, va( "%s isn't ready yet", shinobiPerkTitles[i] ) );
		}
		y += rowH + 2;
	}

	// --- bottom: description, message, buttons -------------------------
	CG_FillRect( 24, 368, 592, 18, off );
	if ( sm.hover )
		SM_Text( 30, 370, 0.42f, title, sm.hover );
	if ( sm.msg[0] && cg.time - sm.msgTime < 4000 )
		SM_Text( 30, 392, 0.45f, sm.msgBad ? bad : good, sm.msg );

	if ( SM_Button( 30, 420, 100, 18, "Save", qtrue ) )
		CG_ShinobiMenuSave();
	if ( SM_Button( 140, 420, 100, 18, "Default", qtrue ) ) {
		Shinobi_BuildDefault( &sm.edit );
		SM_Message( qfalse, "Default build - Save to keep it" );
	}
	if ( SM_Button( 250, 420, 100, 18, "Close", qtrue ) )
		CG_ShinobiMenuClose();
	SM_Text( 370, 422, 0.38f, dim, "saved builds take effect when you respawn (/kill)" );

	sm.click = qfalse; // a click on nothing is dropped

	// pointer: a small dot exactly on the hot spot, with a short cross around it
	{
		const vec4_t ink = { 1, 1, 1, 1 }, edge = { 0, 0, 0, 0.9f };
		CG_FillRect( sm.cx - 1.5f, sm.cy - 1.5f, 3, 3, edge );
		CG_FillRect( sm.cx - 1, sm.cy - 1, 2, 2, ink );
		CG_FillRect( sm.cx - 6, sm.cy - 0.25f, 3, 0.5f, ink );
		CG_FillRect( sm.cx + 3, sm.cy - 0.25f, 3, 0.5f, ink );
		CG_FillRect( sm.cx - 0.25f, sm.cy - 6, 0.5f, 3, ink );
		CG_FillRect( sm.cx - 0.25f, sm.cy + 3, 0.5f, 3, ink );
	}
}

// open the screen by itself the first time, while no build was ever saved
void CG_ShinobiMenuAuto( void ) {
	char buf[256];

	if ( sm.autoShown || sm.open || !cg.snap || cg.demoPlayback )
		return;
	if ( cg.snap->ps.pm_type != PM_NORMAL || cg.snap->ps.persistant[PERS_TEAM] == TEAM_SPECTATOR )
		return;
	sm.autoShown = qtrue;
	trap->Cvar_VariableStringBuffer( "shinobi_build", buf, sizeof( buf ) );
	if ( !buf[0] && !( trap->Key_GetCatcher() & KEYCATCH_UI ) )
		CG_ShinobiMenu_f();
}
