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
qboolean Shinobi_CastKawarimi( gentity_t *ent );
void     Shinobi_Hide( gentity_t *ent );

// g_shinobi_elements.c
void     Shinobi_ElementsInit( void );
qboolean Shinobi_CastKaton( gentity_t *ent, int level );
qboolean Shinobi_CastDoton( gentity_t *ent, int level );

// g_shinobi_clones.c
qboolean Shinobi_CastKageBunshin( gentity_t *ent, int level );
gentity_t *Shinobi_SpawnDecoy( gentity_t *owner );
gentity_t *Shinobi_Decoy( gentity_t *owner );
void     Shinobi_DecoyToLog( gentity_t *decoy );
