/*
===========================================================================
JK_Shinobi - client-side jutsu visuals and HUD.

Reads the jutsu flags that the server puts in entityState/playerState user
fields (see game/bg_shinobi.h) and plays the matching effects. Also draws
the fireball entity, which has no model of its own.
===========================================================================
*/
#include "cg_local.h"
#include "game/bg_shinobi.h"
#include "ui/menudef.h"

#define SHINOBI_HAND_FX_INTERVAL  40   // ms between hand effect spawns (effects live ~80ms)
#define SHINOBI_AURA_FX_INTERVAL  70
#define SHINOBI_BALL_FX_INTERVAL  25

/*
===========================================================================
Diagnostics: everything is printed with a "[shinobi]" prefix so it can be
pulled out of qconsole.log (launchers set logfile 2).
===========================================================================
*/
// "shinobi_debug 1" prints state changes and asset checks (the self-test sets it)
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
	CG_ShinobiCheckFile( "gfx/shinobi/glow.tga" );
	CG_ShinobiCheckFile( "effects/shinobi/rasengan_hand.efx" );
	CG_ShinobiCheckFile( "effects/shinobi/chakra_aura.efx" );
	CG_ShinobiCheckFile( "models/shinobi/earth_pillar1.md3" );
	CG_ShinobiCheckFile( "ext_data/NPCs/shinobi.npc" );
	CG_ShinobiCheckFile( "shinobi_binds.cfg" );

	trap->Print( "[shinobi] fx handles: rasenganHand %i rasenganForm %i chidoriHand %i chidoriForm %i aura %i fireball %i\n",
		cgs.effects.shinobiRasenganHand, cgs.effects.shinobiRasenganForm,
		cgs.effects.shinobiChidoriHand, cgs.effects.shinobiChidoriForm,
		cgs.effects.shinobiChakraAura, cgs.effects.shinobiFireball );
	trap->Print( "[shinobi] local player flags: userInt1 %i userInt2 %i userInt3 %i, legsAnim %i torsoAnim %i\n",
		cg.predictedPlayerState.userInt1, cg.predictedPlayerState.userInt2, cg.predictedPlayerState.userInt3,
		cg.predictedPlayerState.legsAnim, cg.predictedPlayerState.torsoAnim );
}

void CG_ShinobiRegisterMedia( void ) {
	cgs.effects.shinobiRasenganHand = CG_ShinobiRegisterFx( SHINOBI_FX_RASENGAN_HAND );
	cgs.effects.shinobiRasenganForm = CG_ShinobiRegisterFx( SHINOBI_FX_RASENGAN_FORM );
	cgs.effects.shinobiChidoriHand  = CG_ShinobiRegisterFx( SHINOBI_FX_CHIDORI_HAND );
	cgs.effects.shinobiChidoriForm  = CG_ShinobiRegisterFx( SHINOBI_FX_CHIDORI_FORM );
	cgs.effects.shinobiChakraAura   = CG_ShinobiRegisterFx( SHINOBI_FX_CHAKRA_AURA );
	cgs.effects.shinobiFireball     = CG_ShinobiRegisterFx( SHINOBI_FX_FIREBALL );
	// one-shot effects played by the server are registered through configstrings

	if ( CG_ShinobiDebug() )
		CG_ShinobiDiag_f();
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

void CG_ShinobiPlayerEffects( centity_t *cent, clientInfo_t *ci ) {
	entityState_t *s = &cent->currentState;
	vec3_t org, up = { 0, 0, 1 };

	if ( s->eFlags & EF_DEAD ) {
		return;
	}

	if ( SHINOBI_HAND_ANY( s ) ) {
		const qboolean chidori = SHINOBI_HAND_IS_CHIDORI( s );

		CG_ShinobiHandOrigin( cent, ci, org );

		if ( cent->shinobiHandFxTime <= cg.time || cent->shinobiHandFxTime > cg.time + 1000 ) {
			fxHandle_t fx;

			if ( SHINOBI_HAND_FORMING( s ) )
				fx = chidori ? cgs.effects.shinobiChidoriForm : cgs.effects.shinobiRasenganForm;
			else
				fx = chidori ? cgs.effects.shinobiChidoriHand : cgs.effects.shinobiRasenganHand;

			trap->FX_PlayEffectID( fx, org, up, -1, -1, qfalse );
			cent->shinobiHandFxTime = cg.time + SHINOBI_HAND_FX_INTERVAL;
		}

		if ( chidori ) { // flickering lightning glow every frame
			const float f = 0.6f + 0.4f * flrand( 0.0f, 1.0f );
			trap->R_AddLightToScene( org, 120 + 80 * f, 0.55f * f, 0.75f * f, 1.0f * f );
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
==================
*/
qboolean CG_ShinobiEntity( centity_t *cent ) {
	entityState_t *s = &cent->currentState;

	if ( s->eType != ET_GENERAL || s->generic1 != SHINOBI_ENT_FIREBALL ) {
		return qfalse;
	}

	{
		vec3_t dir;
		const float f = 0.8f + 0.2f * flrand( 0.0f, 1.0f );

		VectorCopy( s->pos.trDelta, dir );
		if ( VectorNormalize( dir ) == 0 ) {
			VectorSet( dir, 0, 0, 1 );
		}

		if ( cent->shinobiHandFxTime <= cg.time || cent->shinobiHandFxTime > cg.time + 1000 ) {
			trap->FX_PlayEffectID( cgs.effects.shinobiFireball, cent->lerpOrigin, dir, -1, -1, qfalse );
			cent->shinobiHandFxTime = cg.time + SHINOBI_BALL_FX_INTERVAL;
		}
		trap->R_AddLightToScene( cent->lerpOrigin, 260 * f, 1.0f, 0.55f, 0.15f );
	}
	return qtrue;
}

void CG_ShinobiDrawHUD( void ) {
	playerState_t *ps = &cg.predictedPlayerState;
	const char    *text = NULL;
	vec4_t         color = { 0.55f, 0.85f, 1.0f, 1.0f };
	const float    scale = 0.7f;
	const qboolean chidori = SHINOBI_HAND_IS_CHIDORI( ps );

	{ // log every change of the local jutsu flags (cheap: only on change)
		static int lastFlags = -1;
		const int flags = ( ps->userInt1 ? 1 : 0 ) | ( ps->userInt2 ? 2 : 0 ) | ( ps->userInt3 ? 4 : 0 );
		if ( flags != lastFlags && CG_ShinobiDebug() ) {
			trap->Print( "[shinobi] local flags %i%i%i charging %i hand %i chidori %i forming %i armed %i dash %i chakra %i\n",
				ps->userInt1 ? 1 : 0, ps->userInt2 ? 1 : 0, ps->userInt3 ? 1 : 0,
				SHINOBI_CHARGING( ps ) ? 1 : 0, SHINOBI_HAND_ANY( ps ) ? 1 : 0, SHINOBI_HAND_IS_CHIDORI( ps ) ? 1 : 0,
				SHINOBI_HAND_FORMING( ps ) ? 1 : 0, SHINOBI_HAND_ARMED( ps ) ? 1 : 0, SHINOBI_HAND_DASH( ps ) ? 1 : 0,
				ps->fd.forcePower );
			lastFlags = flags;
		}
	}

	if ( ps->pm_type == PM_SPECTATOR || ps->stats[STAT_HEALTH] <= 0 ) {
		return;
	}

	if ( SHINOBI_CHARGING( ps ) ) {
		text = va( "Gathering chakra... %i", ps->fd.forcePower );
	}
	else if ( SHINOBI_HAND_FORMING( ps ) ) {
		text = chidori ? "Chidori forming..." : "Rasengan forming...";
	}
	else if ( SHINOBI_HAND_ARMED( ps ) ) {
		text = chidori ? "CHIDORI - ATTACK!" : "RASENGAN - ATTACK!";
		color[3] = 0.65f + 0.35f * sinf( cg.time * 0.012f );
	}

	if ( text ) {
		const int w = CG_Text_Width( text, scale, FONT_MEDIUM );
		CG_Text_Paint( ( SCREEN_WIDTH - w ) * 0.5f, 360, scale, color, text, 0, 0, ITEM_TEXTSTYLE_SHADOWED, FONT_MEDIUM );
	}
}
