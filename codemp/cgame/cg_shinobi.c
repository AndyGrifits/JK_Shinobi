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

#define SHINOBI_HAND_FX_INTERVAL  60   // ms between hand spark effects
#define SHINOBI_AURA_FX_INTERVAL  70
#define SHINOBI_BALL_FX_INTERVAL  40

static struct {
	qhandle_t glow, swirl, bolt, fire, spark;
	qhandle_t puddle, mud, scorch, waterSphere, ring;
} shinobiShaders;

// loose-particle effects for elements (one-shots come from the server)
static struct {
	fxHandle_t fireGround, fireBody, steam, waterTrail, drip, bubbles, zoneSpark, waveCrest;
} shinobiFx;

// the jutsu key being held on this client (for the charge bars)
static struct {
	int      jutsu;
	int      start;
	qboolean fromJump; // Shunshin charged with Shift + jump
} shinobiCharge;

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

	memset( &shinobiCharge, 0, sizeof( shinobiCharge ) );
	shinobiSel.jumpHeld = qfalse;
	shinobiSel.changeTime = 0;
	if ( shinobiSel.index < 0 || shinobiSel.index >= SHINOBI_CYCLE_COUNT )
		shinobiSel.index = 0;

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
	CG_ShinobiJutsuDown( shinobiJutsuCycle[shinobiSel.index] );
}

void CG_ShinobiCastUp_f( void ) {
	if ( shinobiCharge.jutsu && !shinobiCharge.fromJump )
		CG_ShinobiJutsuUp( shinobiCharge.jutsu ); // whatever this key started, even if the selection moved
}

static void CG_ShinobiSelect( int step ) {
	shinobiSel.index = ( shinobiSel.index + step + SHINOBI_CYCLE_COUNT ) % SHINOBI_CYCLE_COUNT;
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
	default:
		break;
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
	const int    lvl = Shinobi_ChargeLevelFor( shinobiCharge.jutsu, held );
	int          shown = lvl, i;
	const char  *label;
	vec4_t       textColor = { 1, 1, 1, 1 };

	while ( shown > 1 && chakra < Shinobi_JutsuCost( shinobiCharge.jutsu, shown ) )
		shown--;

	for ( i = 0; i < SHINOBI_MAX_LEVEL; i++ ) {
		const int   begin = i ? Shinobi_BarEnd( shinobiCharge.jutsu, i - 1 ) : 0;
		const float fill = Com_Clamp( 0, 1, (float)( held - begin ) / (float)( Shinobi_BarEnd( shinobiCharge.jutsu, i ) - begin ) );
		const float x = x0 + i * ( barW + gap );
		const qboolean affordable = chakra >= Shinobi_JutsuCost( shinobiCharge.jutsu, i + 1 );

		CG_FillRect( x, y, barW, barH, back );
		if ( fill > 0 )
			CG_FillRect( x, y, barW * fill, barH, affordable ? barColor[i] : locked );
		CG_DrawRect( x, y, barW, barH, 1, frame );
	}

	if ( held < Shinobi_BarEnd( shinobiCharge.jutsu, 0 ) ) {
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

	for ( i = 0; i < SHINOBI_CYCLE_COUNT; i++ )
		total += CG_Text_Width( shinobiJutsuTitles[shinobiJutsuCycle[i]], scale, FONT_MEDIUM ) + ( i ? gap : 0 );
	x = ( SCREEN_WIDTH - total ) * 0.5f;

	for ( i = 0; i < SHINOBI_CYCLE_COUNT; i++ ) {
		const int    j = shinobiJutsuCycle[i];
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
		const int   j = shinobiJutsuCycle[shinobiSel.index];
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

	CG_ShinobiDrawJutsuList();

	if ( shinobiCharge.jutsu && !SHINOBI_JUTSU_HOLD( shinobiCharge.jutsu ) ) {
		CG_ShinobiDrawChargeBars();
	}

	if ( SHINOBI_STUNNED( ps ) ) {
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
