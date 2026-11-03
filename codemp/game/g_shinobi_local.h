/*
===========================================================================
JK_Shinobi - private header shared by the g_shinobi*.c files.
===========================================================================
*/
#pragma once

#include "g_local.h"
#include "bg_shinobi.h"

// Effect (G_EffectIndex) and sound (G_SoundIndex) handles, set in Shinobi_Init.
// Arrays indexed [level - 1] hold per-level (scaled) versions of an effect.
typedef struct {
	int fxRasenganHit[SHINOBI_MAX_LEVEL], fxRasenganFizzle;
	int fxChidoriHit[SHINOBI_MAX_LEVEL], fxChidoriFizzle;
	int fxShunshin, fxClonePoof, fxCloneSmoke;
	int fxFireballBoom[SHINOBI_MAX_LEVEL], fxWallRise, fxWallBreak;

	int sndChargeStart, sndFizzle, sndPoof, sndJutsuCharge;
	int sndRasenganForm, sndRasenganDash, sndRasenganHit;
	int sndChidoriForm, sndChidoriDash, sndChidoriHit;
	int sndFireballCast, sndFireballBoom, sndFireballBoom2;
	int sndWallRise, sndWallBreak;

	int sndLog;
	int sndHiss, sndSplash, sndWaterCast, sndWind, sndWind2, sndIgnite, sndPrison, sndGurgle;
	int sndFireLoop, sndCrackleLoop, sndCurse, sndShadow;
	int fxCurse, fxGates;
	int sndEyes, sndGates;
	int sndBlockSaber[3], sndBlockFist[3], sndParry, sndGuardBreak, fxBlock;
	int fxWindEdge, fxRaiEdge, fxQuakeDust, fxFlash;
	int sndFlash, sndHeal;
	// stock effects of the base game (effects/*.efx in its assets)
	int fxStockExplode, fxStockExplodeSmall, fxWaterImpact, fxRockMed, fxRockLg, fxEmp, fxSparkBurst;
	int fxSteamBurst, fxWaterSplash[SHINOBI_MAX_LEVEL], fxWindGust[SHINOBI_MAX_LEVEL], fxStorm[SHINOBI_MAX_LEVEL];

	int wallModels[SHINOBI_WALL_MODELS];
	int logModel, exKunaiModel, tagModel, shurikenModel;
	int rockBlockModel[3], rockLedgeModel;
} shinobiMedia_t;

extern shinobiMedia_t shinobiMedia;

// g_shinobi.c
qboolean Shinobi_CanAct( gentity_t *ent );
void     Shinobi_StopCharging( gclient_t *client );
qboolean Shinobi_SpendChakra( gentity_t *ent, int cost );
void     Shinobi_NoChakra( gentity_t *ent, int cost );
int      Shinobi_LevelEffect( const char *base, int level );
void     Shinobi_TeleportClient( gentity_t *ent, vec3_t origin );
void     Shinobi_EffectDir( int fx, vec3_t org, vec3_t dir ); // G_PlayEffectID wants angles, not a direction
qboolean Shinobi_CastKawarimi( gentity_t *ent );
qboolean Shinobi_Cast( gentity_t *ent, int jutsu, int wanted ); // highest affordable level up to 'wanted'
void     Shinobi_Debug( const char *fmt, ... ); // printed with shinobi_debug 1
void     Shinobi_Hide( gentity_t *ent );

// g_shinobi_elements.c
void     Shinobi_ElementsInit( void );
qboolean Shinobi_CastKaton( gentity_t *ent, int level );
qboolean Shinobi_CastDoton( gentity_t *ent, int level );
qboolean Shinobi_PushWall( gentity_t *ent ); // qtrue if a wall of ours was in front to push

// g_shinobi_clones.c
qboolean Shinobi_CastKageBunshin( gentity_t *ent, int level );
gentity_t *Shinobi_SpawnDecoy( gentity_t *owner );
qboolean  Shinobi_CastWaterClone( gentity_t *ent );
gentity_t *Shinobi_Decoy( gentity_t *owner );
void     Shinobi_DecoyToLog( gentity_t *decoy );

// g_shinobi_nature.c (elements)
extern int shinobiHitElement; // element of the damage being dealt right now (shinobiElement_t)
void      Shinobi_NatureInit( void );
void      Shinobi_StatusThink( gentity_t *ent, usercmd_t *ucmd );
void      Shinobi_StatusClear( gentity_t *ent );
gentity_t *Shinobi_SpawnZone( int type, vec3_t origin, float radius, int lifeMs, int ownerNum );
void      Shinobi_Paralyse( gentity_t *v, int ms );
void      Shinobi_FreezeZones( vec3_t center, float radius, const vec3_t face, float halfW );
void      Shinobi_DustAt( vec3_t pos, float radius, int ownerNum );
qboolean  Shinobi_FloorBelow( vec3_t pos, float maxDrop, vec3_t floor );
void      Shinobi_ElectrifyAt( vec3_t point, int ownerNum );
void      Shinobi_EarthOnWater( vec3_t center, float radius );
void      Shinobi_SteamAt( vec3_t pos, float radius );
qboolean  Shinobi_InMapWater( vec3_t pos );
void      Shinobi_FireLands( vec3_t pos, int lvl, int ownerNum );
void      Shinobi_FireballSetLevel( gentity_t *ball, int lvl, float speedScale );
qboolean  Shinobi_CastSuiton( gentity_t *ent, int lvl );
qboolean  Shinobi_CastFuton( gentity_t *ent, int lvl );
qboolean  Shinobi_CastWave( gentity_t *ent, int lvl );
qboolean  Shinobi_CastBlade( gentity_t *ent, int lvl );
void      Shinobi_SmotherFire( vec3_t pos, float radius, qboolean steam );
qboolean  Shinobi_CastSuiro( gentity_t *ent, int lvl );
void      Shinobi_PrisonRelease( gentity_t *ent );
void      Shinobi_PrisonCasterThink( gentity_t *caster ); // for NPC casters (Water Clone)

// g_shinobi.c (crowd control)
qboolean  Shinobi_CCImmune( gentity_t *e );
void      Shinobi_CCGrant( gentity_t *e, int ccEnds );

// g_shinobi_nature.c (Katon additions)
qboolean  Shinobi_CastFlame( gentity_t *ent );
void      Shinobi_FlameStop( gentity_t *ent );
qboolean  Shinobi_CastAsh( gentity_t *ent, int lvl );
void      Shinobi_IgniteAshAt( vec3_t pos, float radius, int igniterNum );

// g_shinobi_shadow.c
qboolean  Shinobi_CastShadow( gentity_t *ent );
void      Shinobi_ShadowRelease( gentity_t *ent );
void      Shinobi_ShadowVictimThink( gentity_t *ent, usercmd_t *ucmd );

// g_shinobi_kunai.c
void      Shinobi_KunaiInit( void );
qboolean  Shinobi_CastHiraishin( gentity_t *ent );
qboolean  Shinobi_CastRaiTrap( gentity_t *ent );
qboolean  Shinobi_CastShadowTrap( gentity_t *ent );
void      Shinobi_HealStop( gentity_t *ent );
void      Shinobi_ChargesToStats( gclient_t *client );
void      Shinobi_BotDefend( gentity_t *ent, usercmd_t *ucmd );
void      Shinobi_ShadowTrapRelease( gentity_t *victim );
void      Shinobi_ShadowTrapVictimThink( gentity_t *ent, usercmd_t *ucmd );
void      Shinobi_InfuseThink( gentity_t *ent, usercmd_t *ucmd );
int       Shinobi_InfuseHit( gentity_t *targ, gentity_t *attacker, int damage );
qboolean  Shinobi_CastEdge( gentity_t *ent, int school );
qboolean  Shinobi_WindEdgeActive( gentity_t *ent );
void      Shinobi_Ignite( gentity_t *ent, int attackerNum );
void      Shinobi_Wet( gentity_t *ent );

// g_shinobi_build.c
extern int shinobiJutsuDamage; // set while a jutsu deals damage (no taijutsu bonus)
qboolean  Shinobi_HasJutsu( gentity_t *ent, int jutsu );
int       Shinobi_CastStat( gentity_t *ent );
void      Shinobi_PerkThink( gentity_t *ent );
void      Shinobi_BlockThink( gentity_t *ent, usercmd_t *ucmd );
void      Shinobi_Stagger( gentity_t *ent, int ms );
void      Shinobi_Flinch( gentity_t *ent, int ms );
void      Shinobi_HitText( gentity_t *targ, int kind, int value );
void      Shinobi_TaijutsuThink( gentity_t *ent );
#define SBR_NONE    0
#define SBR_BLOCKED 1
#define SBR_PARRIED 2
#define SBR_BROKEN  3
qboolean  Shinobi_CastMist( gentity_t *ent, int lvl );
qboolean  Shinobi_CastQuake( gentity_t *ent, int lvl );
qboolean  Shinobi_CastWindWall( gentity_t *ent, int lvl );
// g_shinobi_earth.c
qboolean  Shinobi_CastBurrow( gentity_t *ent );
void      Shinobi_BurrowEnd( gentity_t *ent, qboolean strike );
qboolean  Shinobi_Burrowed( gentity_t *ent );
qboolean  Shinobi_CastArmor( gentity_t *ent );
qboolean  Shinobi_Armored( gentity_t *ent );
void      Shinobi_EarthThink( gentity_t *ent, usercmd_t *ucmd );
qboolean  Shinobi_CastPillar( gentity_t *ent, int lvl );
qboolean  Shinobi_CastEarthClone( gentity_t *ent );
void      Shinobi_RestoreSolid( gentity_t *ent );
// g_shinobi_yinyang.c
qboolean  Shinobi_ShadowHidden( gentity_t *ent );
qboolean  Shinobi_CastShadowHide( gentity_t *ent );
void      Shinobi_ShadowHideEnd( gentity_t *ent );
qboolean  Shinobi_CastFlash( gentity_t *ent );
qboolean  Shinobi_CastHeal( gentity_t *ent, int lvl );
qboolean  Shinobi_CastRaiDash( gentity_t *ent );
void      Shinobi_RaiDashEnd( gentity_t *ent );
void      Shinobi_YinYangThink( gentity_t *ent, usercmd_t *ucmd );
qboolean  Shinobi_CastRaiClone( gentity_t *ent );
void      Shinobi_TeleportClient( gentity_t *ent, vec3_t origin );
// g_shinobi_bot.c
void      Shinobi_BotPrepare( gentity_t *ent );
void      Shinobi_BotForcePowers( gentity_t *ent );
void      Shinobi_BotThink( gentity_t *ent );
void      Shinobi_KunaiReflect( gentity_t *k, gentity_t *newOwner );
gentity_t *Shinobi_CloneOwner( gentity_t *ent );
