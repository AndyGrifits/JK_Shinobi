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
	int sndFireLoop, sndCrackleLoop;
	int fxSteamBurst, fxWaterSplash[SHINOBI_MAX_LEVEL], fxWindGust[SHINOBI_MAX_LEVEL], fxStorm[SHINOBI_MAX_LEVEL];

	int wallModels[SHINOBI_WALL_MODELS];
	int logModel;
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
void     Shinobi_Hide( gentity_t *ent );

// g_shinobi_elements.c
void     Shinobi_ElementsInit( void );
qboolean Shinobi_CastKaton( gentity_t *ent, int level );
qboolean Shinobi_CastDoton( gentity_t *ent, int level );
qboolean Shinobi_PushWall( gentity_t *ent ); // qtrue if a wall of ours was in front to push

// g_shinobi_clones.c
qboolean Shinobi_CastKageBunshin( gentity_t *ent, int level );
gentity_t *Shinobi_SpawnDecoy( gentity_t *owner );
gentity_t *Shinobi_Decoy( gentity_t *owner );
void     Shinobi_DecoyToLog( gentity_t *decoy );

// g_shinobi_nature.c (elements)
extern int shinobiHitElement; // element of the damage being dealt right now (shinobiElement_t)
void      Shinobi_NatureInit( void );
void      Shinobi_StatusThink( gentity_t *ent, usercmd_t *ucmd );
void      Shinobi_StatusClear( gentity_t *ent );
gentity_t *Shinobi_SpawnZone( int type, vec3_t origin, float radius, int lifeMs, int ownerNum );
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

// g_shinobi_build.c
extern int shinobiJutsuDamage; // set while a jutsu deals damage (no taijutsu bonus)
qboolean  Shinobi_HasJutsu( gentity_t *ent, int jutsu );
int       Shinobi_CastStat( gentity_t *ent );
void      Shinobi_PerkThink( gentity_t *ent );
