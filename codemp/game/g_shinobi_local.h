/*
===========================================================================
JK_Shinobi - private header shared by the g_shinobi*.c files.
===========================================================================
*/
#pragma once

#include "g_local.h"
#include "bg_shinobi.h"

// Effect (G_EffectIndex) and sound (G_SoundIndex) handles, set in Shinobi_Init
typedef struct {
	int fxRasenganHit, fxRasenganFizzle;
	int fxChidoriHit, fxChidoriFizzle;
	int fxShunshin, fxClonePoof;
	int fxFireballBoom, fxWallRise, fxWallBreak;

	int sndChargeStart, sndFizzle, sndPoof;
	int sndRasenganForm, sndRasenganDash, sndRasenganHit;
	int sndChidoriForm, sndChidoriDash, sndChidoriHit;
	int sndFireballCast, sndFireballBoom;
	int sndWallRise, sndWallBreak;

	int wallModels[SHINOBI_WALL_MODELS];
} shinobiMedia_t;

extern shinobiMedia_t shinobiMedia;

// g_shinobi.c
qboolean Shinobi_CanAct( gentity_t *ent );
void     Shinobi_StopCharging( gclient_t *client );
qboolean Shinobi_SpendChakra( gentity_t *ent, int cost );

// g_shinobi_elements.c
void     Shinobi_ElementsInit( void );
