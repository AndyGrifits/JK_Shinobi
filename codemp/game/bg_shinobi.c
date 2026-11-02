/*
===========================================================================
JK_Shinobi - ninja build: parsing, validation and serialisation shared by
the game (which enforces it) and the cgame (build screen, jutsu list).
===========================================================================
*/
#include "qcommon/q_shared.h"
#include "bg_public.h"
#include "bg_shinobi.h"

void Shinobi_BuildDefault( shinobiBuild_t *b ) {
	memset( b, 0, sizeof( *b ) );
	b->numJutsu = 5;
	b->jutsu[0] = SJ_SUITON;
	b->jutsu[1] = SJ_WAVE;
	b->jutsu[2] = SJ_SUIRO;
	b->jutsu[3] = SJ_FUTON;
	b->jutsu[4] = SJ_BLADE;
	b->stats[SST_CHAKRA] = 2;
	b->stats[SST_HEALTH] = 2;
	b->stats[SST_TAIJUTSU] = 2;
	b->stats[SST_CAST] = 2;
	b->stats[SST_REGEN] = 2;
	b->perk = SP_NONE;
}

static qboolean Shinobi_Selectable( int jutsu ) {
	return ( jutsu > SJ_NONE && jutsu < SJ_COUNT && shinobiJutsuSchool[jutsu] != SS_BASE ) ? qtrue : qfalse;
}

qboolean Shinobi_BuildHas( const shinobiBuild_t *b, int jutsu ) {
	int i;
	if ( jutsu > SJ_NONE && jutsu < SJ_COUNT && shinobiJutsuSchool[jutsu] == SS_BASE )
		return qtrue;
	for ( i = 0; i < b->numJutsu; i++ ) {
		if ( b->jutsu[i] == jutsu )
			return qtrue;
	}
	return qfalse;
}

int Shinobi_BuildSchools( const shinobiBuild_t *b, int *schools ) {
	int seen[SS_COUNT] = { 0 }, n = 0, i;
	for ( i = 0; i < b->numJutsu; i++ ) {
		const int s = shinobiJutsuSchool[b->jutsu[i]];
		if ( !seen[s] ) {
			seen[s] = 1;
			if ( schools )
				schools[n] = s;
			n++;
		}
	}
	return n;
}

int Shinobi_BuildPoints( const shinobiBuild_t *b ) {
	int i, n = 0;
	for ( i = 0; i < SST_COUNT; i++ )
		n += b->stats[i];
	return n;
}

qboolean Shinobi_BuildValid( const shinobiBuild_t *b, char *why, int whySize ) {
	int i;

	if ( why && whySize )
		why[0] = 0;
	if ( b->numJutsu > SHINOBI_BUILD_JUTSU ) {
		if ( why ) Com_sprintf( why, whySize, "at most %i techniques", SHINOBI_BUILD_JUTSU );
		return qfalse;
	}
	if ( Shinobi_BuildSchools( b, NULL ) > SHINOBI_BUILD_SCHOOLS ) {
		if ( why ) Com_sprintf( why, whySize, "techniques from at most %i schools", SHINOBI_BUILD_SCHOOLS );
		return qfalse;
	}
	for ( i = 0; i < SST_COUNT; i++ ) {
		if ( b->stats[i] < 0 || b->stats[i] > SHINOBI_STAT_MAX ) {
			if ( why ) Com_sprintf( why, whySize, "at most %i points in one stat", SHINOBI_STAT_MAX );
			return qfalse;
		}
	}
	if ( Shinobi_BuildPoints( b ) > SHINOBI_STAT_POINTS ) {
		if ( why ) Com_sprintf( why, whySize, "only %i stat points", SHINOBI_STAT_POINTS );
		return qfalse;
	}
	if ( b->perk < 0 || b->perk >= SP_COUNT || !shinobiPerkReady[b->perk] ) {
		if ( why ) Q_strncpyz( why, "that special ability isn't ready yet", whySize );
		return qfalse;
	}
	return qtrue;
}

void Shinobi_BuildFix( shinobiBuild_t *b ) {
	shinobiBuild_t fixed;
	int            i;

	memset( &fixed, 0, sizeof( fixed ) );
	// techniques: keep them in order while they fit
	for ( i = 0; i < b->numJutsu && fixed.numJutsu < SHINOBI_BUILD_JUTSU; i++ ) {
		fixed.jutsu[fixed.numJutsu++] = b->jutsu[i];
		if ( Shinobi_BuildSchools( &fixed, NULL ) > SHINOBI_BUILD_SCHOOLS )
			fixed.numJutsu--;
	}
	// stats: clamp, then take points back from the end while over budget
	for ( i = 0; i < SST_COUNT; i++ )
		fixed.stats[i] = b->stats[i] < 0 ? 0 : ( b->stats[i] > SHINOBI_STAT_MAX ? SHINOBI_STAT_MAX : b->stats[i] );
	for ( i = SST_COUNT - 1; i >= 0 && Shinobi_BuildPoints( &fixed ) > SHINOBI_STAT_POINTS; ) {
		if ( fixed.stats[i] > 0 )
			fixed.stats[i]--;
		else
			i--;
	}
	fixed.perk = ( b->perk > SP_NONE && b->perk < SP_COUNT && shinobiPerkReady[b->perk] ) ? b->perk : SP_NONE;
	*b = fixed;
}

void Shinobi_BuildParse( const char *str, shinobiBuild_t *b ) {
	char buf[256], *part[3] = { NULL, NULL, NULL }, *p, *tok;
	int  i, n = 0;

	memset( b, 0, sizeof( *b ) );
	if ( !str || !str[0] ) {
		Shinobi_BuildDefault( b );
		return;
	}
	Q_strncpyz( buf, str, sizeof( buf ) );
	part[0] = buf;
	for ( p = buf; *p && n < 2; p++ ) {
		if ( *p == '/' ) {
			*p = 0;
			part[++n] = p + 1;
		}
	}

	// techniques: names separated by commas
	for ( tok = part[0]; tok && *tok; ) {
		char *comma = strchr( tok, ',' );
		int   j;
		if ( comma )
			*comma = 0;
		j = Shinobi_JutsuForName( tok );
		if ( Shinobi_Selectable( j ) && !Shinobi_BuildHas( b, j ) && b->numJutsu < SHINOBI_BUILD_JUTSU )
			b->jutsu[b->numJutsu++] = j;
		tok = comma ? comma + 1 : NULL;
	}
	// stats: numbers separated by commas
	for ( tok = part[1], i = 0; tok && *tok && i < SST_COUNT; i++ ) {
		char *comma = strchr( tok, ',' );
		if ( comma )
			*comma = 0;
		b->stats[i] = atoi( tok );
		tok = comma ? comma + 1 : NULL;
	}
	// perk by name
	b->perk = SP_NONE;
	if ( part[2] ) {
		for ( i = 0; i < SP_COUNT; i++ ) {
			if ( !Q_stricmp( part[2], shinobiPerkNames[i] ) )
				b->perk = i;
		}
	}
}

void Shinobi_BuildString( const shinobiBuild_t *b, char *out, int size ) {
	char buf[256] = "";
	int  i;

	for ( i = 0; i < b->numJutsu; i++ )
		Q_strcat( buf, sizeof( buf ), va( "%s%s", i ? "," : "", shinobiJutsuNames[b->jutsu[i]] ) );
	Q_strcat( buf, sizeof( buf ), "/" );
	for ( i = 0; i < SST_COUNT; i++ )
		Q_strcat( buf, sizeof( buf ), va( "%s%i", i ? "," : "", b->stats[i] ) );
	Q_strcat( buf, sizeof( buf ), va( "/%s", shinobiPerkNames[b->perk >= 0 && b->perk < SP_COUNT ? b->perk : 0] ) );
	Q_strncpyz( out, buf, size );
}

/*
===========================================================================
Ninja tools: the stock pickups, reworked (see bg_shinobi.h)
===========================================================================
*/
typedef struct {
	const char *classname;
	int         tool;
	int         amount;   // health / chakra for pills, count for throwables
} shinobiItemMap_t;

static const shinobiItemMap_t shinobiItemMap[] = {
	{ "item_medpak_instant",        STOOL_HEAL,     30 },
	{ "item_medpac",                STOOL_HEAL,     30 },
	{ "item_medpac_big",            STOOL_HEAL,     50 },
	{ "ammo_force",                 STOOL_CHAKRA,   40 },
	{ "item_force_boon",            STOOL_CHAKRA,   60 },
	{ "item_force_enlighten_light", STOOL_CHAKRA,   60 },
	{ "item_force_enlighten_dark",  STOOL_CHAKRA,   60 },
	{ "item_ysalimari",             STOOL_CHAKRA,   40 },
	{ "ammo_blaster",               STOOL_CHAKRA,   30 },
	{ "ammo_powercell",             STOOL_CHAKRA,   30 },
	{ "ammo_metallic_bolts",        STOOL_CHAKRA,   30 },
	{ "ammo_rockets",               STOOL_CHAKRA,   30 },
	{ "ammo_all",                   STOOL_CHAKRA,   50 },
	{ "item_jetpack",               STOOL_CHAKRA,   40 },
	{ "item_cloak",                 STOOL_CHAKRA,   40 },
	{ "item_binoculars",            STOOL_CHAKRA,   30 },
	{ "item_healthdisp",            STOOL_HEAL,     30 },
	{ "item_ammodisp",              STOOL_CHAKRA,   30 },
	{ "weapon_thermal",             STOOL_EXKUNAI,  SHINOBI_TOOL_PICKUP },
	{ "ammo_thermal",               STOOL_EXKUNAI,  SHINOBI_TOOL_PICKUP },
	{ "weapon_blaster_pistol",      STOOL_EXKUNAI,  SHINOBI_TOOL_PICKUP },
	{ "weapon_bryar_pistol",        STOOL_EXKUNAI,  SHINOBI_TOOL_PICKUP },
	{ "weapon_blaster",             STOOL_EXKUNAI,  SHINOBI_TOOL_PICKUP },
	{ "weapon_stun_baton",          STOOL_EXKUNAI,  SHINOBI_TOOL_PICKUP },
	{ "weapon_trip_mine",           STOOL_TAG,      SHINOBI_TOOL_PICKUP },
	{ "ammo_tripmine",              STOOL_TAG,      SHINOBI_TOOL_PICKUP },
	{ "weapon_det_pack",            STOOL_SHURIKEN, SHINOBI_TOOL_PICKUP },
	{ "ammo_detpack",               STOOL_SHURIKEN, SHINOBI_TOOL_PICKUP },
	{ "item_seeker",                STOOL_TAG,      SHINOBI_TOOL_PICKUP },
	{ "item_sentry_gun",            STOOL_TAG,      SHINOBI_TOOL_PICKUP },
	{ "item_shield",                STOOL_TAG,      SHINOBI_TOOL_PICKUP },
	{ "item_eweb_holdable",         STOOL_TAG,      SHINOBI_TOOL_PICKUP },
	{ "weapon_rocket_launcher",     STOOL_SHURIKEN, SHINOBI_TOOL_PICKUP },
	{ "weapon_repeater",            STOOL_SHURIKEN, SHINOBI_TOOL_PICKUP },
	{ "weapon_flechette",           STOOL_SHURIKEN, SHINOBI_TOOL_PICKUP },
	{ "weapon_concussion_rifle",    STOOL_SHURIKEN, SHINOBI_TOOL_PICKUP },
	{ "weapon_disruptor",           STOOL_SHURIKEN, SHINOBI_TOOL_PICKUP },
	{ "weapon_bowcaster",           STOOL_SHURIKEN, SHINOBI_TOOL_PICKUP },
};

static const char * const shinobiToolNames[STOOL_COUNT] = {
	"", "Healing Pill", "Chakra Pill", "2 Explosive Kunai", "2 Explosive Tags", "2 Fuma Shuriken"
};
static const char * const shinobiToolModels[STOOL_COUNT] = {
	"", "models/shinobi/pill_heal.md3", "models/shinobi/pill_chakra.md3", "models/shinobi/exkunai.md3",
	"models/shinobi/tag.md3", "models/shinobi/shuriken.md3"
};
static const char * const shinobiToolIcons[STOOL_COUNT] = {
	"", "gfx/shinobi/icon_heal", "gfx/shinobi/icon_chakra", "gfx/shinobi/icon_exkunai",
	"gfx/shinobi/icon_tag", "gfx/shinobi/icon_shuriken"
};

int Shinobi_ToolForItem( const char *classname, int *amount ) {
	int i;
	if ( !classname )
		return STOOL_NONE;
	for ( i = 0; i < (int)ARRAY_LEN( shinobiItemMap ); i++ ) {
		if ( !Q_stricmp( classname, shinobiItemMap[i].classname ) ) {
			if ( amount )
				*amount = shinobiItemMap[i].amount;
			return shinobiItemMap[i].tool;
		}
	}
	return STOOL_NONE;
}

const char *Shinobi_ToolName( int tool ) {
	return ( tool > STOOL_NONE && tool < STOOL_COUNT ) ? shinobiToolNames[tool] : "";
}

const char *Shinobi_ToolModel( int tool ) {
	return ( tool > STOOL_NONE && tool < STOOL_COUNT ) ? shinobiToolModels[tool] : "";
}

const char *Shinobi_ToolTitle( int tool ) {
	static const char * const titles[STOOL_COUNT] = { "", "Healing Pill", "Chakra Pill", "Explosive Kunai", "Explosive Tag", "Fuma Shuriken" };
	return ( tool > STOOL_NONE && tool < STOOL_COUNT ) ? titles[tool] : "";
}

// once per module load: new looks, and types that never hand out a gun
void Shinobi_PatchItems( void ) {
	static qboolean done = qfalse;
	int i;

	if ( done )
		return;
	done = qtrue;
	ammoData[SHINOBI_AMMO_EXKUNAI].max = SHINOBI_TOOL_MAX;
	ammoData[SHINOBI_AMMO_TAG].max = SHINOBI_TOOL_MAX;
	ammoData[SHINOBI_AMMO_SHURIKEN].max = SHINOBI_TOOL_MAX;
	for ( i = 1; i < bg_numItems; i++ ) {
		gitem_t  *item = &bg_itemlist[i];
		const int tool = Shinobi_ToolForItem( item->classname, NULL );
		if ( tool == STOOL_NONE )
			continue;
		item->world_model[0] = (char *)shinobiToolModels[tool];
		// (the cgame draws thermals, trip mines and det packs lying around with world_model[1];
		// for health and powerups it would be a second, spinning copy)
		item->world_model[1] = item->giType == IT_WEAPON ? (char *)shinobiToolModels[tool] : NULL;
		item->world_model[2] = item->world_model[3] = NULL;
		// (the view model stays: the cgame builds weapon file names from it)
		item->icon = (char *)shinobiToolIcons[tool];
		item->pickup_sound = tool == STOOL_HEAL ? "sound/player/pickuphealth.wav" : "sound/player/pickupenergy.wav";
		// (types and tags stay: the game and the cgame look weapons / holdables up by them;
		// Touch_Item hands out the tool instead, BG_CanItemBeGrabbed asks Shinobi_CanGrabTool)
		Shinobi_ToolForItem( item->classname, &item->quantity );
	}
}

// one of ours: may this player take it? (-1: not ours)
int Shinobi_CanGrabTool( const char *classname, const playerState_t *ps ) {
	const int tool = Shinobi_ToolForItem( classname, NULL );
	switch ( tool ) {
	case STOOL_HEAL:     return ps->stats[STAT_HEALTH] < ps->stats[STAT_MAX_HEALTH];
	case STOOL_CHAKRA:   return ps->fd.forcePower < ps->fd.forcePowerMax;
	case STOOL_EXKUNAI:  return ps->ammo[SHINOBI_AMMO_EXKUNAI] < SHINOBI_TOOL_MAX;
	case STOOL_TAG:      return ps->ammo[SHINOBI_AMMO_TAG] < SHINOBI_TOOL_MAX;
	case STOOL_SHURIKEN: return ps->ammo[SHINOBI_AMMO_SHURIKEN] < SHINOBI_TOOL_MAX;
	default:             return -1;
	}
}

/*
===========================================================================
Accessories
===========================================================================
*/
void Shinobi_AccDefault( shinobiAcc_t *a ) {
	memset( a, 0, sizeof( *a ) );
	a->scale = 100;
}

static int Shinobi_AccClamp( int v, int lo, int hi ) {
	return v < lo ? lo : ( v > hi ? hi : v );
}

int Shinobi_AccParse( const char *s, shinobiAcc_t *acc, int max ) {
	int n = 0;

	while ( s && *s && n < max ) {
		shinobiAcc_t *a = &acc[n];
		int           v[7], got = 0, k = 0;
		const char   *p = s;

		Shinobi_AccDefault( a );
		while ( *p && *p != ',' && *p != ';' && k < (int)sizeof( a->name ) - 1 ) {
			const char c = *p++;
			if ( ( c >= 'a' && c <= 'z' ) || ( c >= '0' && c <= '9' ) || c == '_' )
				a->name[k++] = c;
		}
		a->name[k] = 0;
		while ( *p && *p != ',' && *p != ';' )
			p++;
		while ( *p == ',' && got < 7 ) {
			p++;
			v[got++] = atoi( p );
			while ( *p && *p != ',' && *p != ';' )
				p++;
		}
		if ( got >= 1 ) a->scale = Shinobi_AccClamp( v[0], SHINOBI_ACC_SCALE_MIN, SHINOBI_ACC_SCALE_MAX );
		for ( k = 0; k < 3; k++ ) {
			if ( got >= 2 + k ) a->ofs[k] = Shinobi_AccClamp( v[1 + k], -SHINOBI_ACC_OFS_MAX, SHINOBI_ACC_OFS_MAX );
			if ( got >= 5 + k ) a->ang[k] = Shinobi_AccClamp( v[4 + k], -180, 180 );
		}
		while ( *p && *p != ';' )
			p++;
		if ( *p == ';' )
			p++;
		s = p;
		if ( a->name[0] )
			n++;
	}
	return n;
}

void Shinobi_AccString( const shinobiAcc_t *acc, int n, char *out, int size ) {
	int i;

	out[0] = 0;
	for ( i = 0; i < n; i++ ) {
		const shinobiAcc_t *a = &acc[i];
		char                one[96];
		if ( !a->name[0] )
			continue;
		if ( a->scale == 100 && !a->ofs[0] && !a->ofs[1] && !a->ofs[2] && !a->ang[0] && !a->ang[1] && !a->ang[2] )
			Com_sprintf( one, sizeof( one ), "%s", a->name ); // as it was modelled: just the name
		else
			Com_sprintf( one, sizeof( one ), "%s,%i,%i,%i,%i,%i,%i,%i", a->name, a->scale,
				a->ofs[0], a->ofs[1], a->ofs[2], a->ang[0], a->ang[1], a->ang[2] );
		if ( out[0] )
			Q_strcat( out, size, ";" );
		Q_strcat( out, size, one );
	}
}

// a bone's BasePoseMatInv from the start of a .gla (the skeleton comes before the frames)
qboolean Shinobi_GlaBoneInv( const unsigned char *buf, int len, const char *bone, float inv[3][4] ) {
	int numBones, ofsSkel, pass, i;

	if ( len < 100 || memcmp( buf, "2LGA", 4 ) )
		return qfalse;
	// mdxaHeader_t: ident, version, name[64], fScale, numFrames, ofsFrames, numBones, ofsCompBonePool, ofsSkel, ofsEnd
	numBones = *(const int *)( buf + 84 );
	ofsSkel  = *(const int *)( buf + 92 );
	for ( pass = 0; pass < 2; pass++ ) {
		// the bone offsets follow the 100-byte header, each counting from there (as Ghoul2 reads it)
		const int base = pass ? ofsSkel : 100;
		if ( pass && ofsSkel == 100 )
			break;
		for ( i = 0; i < numBones && base + i * 4 + 4 <= len; i++ ) {
			const int            o = base + *(const int *)( buf + base + i * 4 );
			const unsigned char *b = buf + o;
			if ( o <= 0 || o + 176 > len )
				break;
			// mdxaSkel_t: name[64], flags, parent, BasePoseMat[3][4], BasePoseMatInv[3][4], ...
			if ( !Q_stricmp( (const char *)b, bone ) ) {
				memcpy( inv, b + 64 + 4 + 4 + 48, sizeof( float ) * 12 );
				return qtrue;
			}
		}
	}
	return qfalse;
}

/*
Where an accessory goes this frame. bolt = the cranium bolt matrix (world),
inv = the cranium's BasePoseMatInv. bolt * inv maps the model space only up
to a fixed turn and scale (the gla's frames are not the glm's: model X -> up,
Y -> back, Z -> right, x1.56, measured), and the glm faces -Y. So:
  world = bolt * inv * G,  G v = p + L ( v - p ) + s Pt ofs,
  L = s Pt U k Q
p the cranium's bind position, s the scale fix, Pt the axis fix, Q the
quarter turn (glm facing -Y -> forward +X), U the player's turn (pitch yaw
roll about the head) and k the player's scale; ofs is along the head's
forward / left / up.
*/
void Shinobi_AccPlace( const float bolt[3][4], const float B[3][4], const shinobiAcc_t *a, float modelScale,
	vec3_t origin, vec3_t axis[3] ) {
	static const float Pt[3][3] = { { 0, 0, 1 }, { -1, 0, 0 }, { 0, -1, 0 } };
	static const float Q[3][3]  = { { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } };
	float  inv3[3][3], L[3][3], M1[3][3], M2[3][3], G[3][4], C[3][4], det, len = 0, sc, k;
	vec3_t pivot, ang, U[3], ofs;
	int    r, c, j;

	det = B[0][0] * ( B[1][1] * B[2][2] - B[1][2] * B[2][1] ) - B[0][1] * ( B[1][0] * B[2][2] - B[1][2] * B[2][0] )
		+ B[0][2] * ( B[1][0] * B[2][1] - B[1][1] * B[2][0] );
	if ( fabs( det ) < 1e-6f ) {
		VectorCopy( bolt[0], origin ); // (never happens with a sane gla)
		AxisClear( axis );
		return;
	}
	inv3[0][0] = ( B[1][1] * B[2][2] - B[1][2] * B[2][1] ) / det;
	inv3[0][1] = ( B[0][2] * B[2][1] - B[0][1] * B[2][2] ) / det;
	inv3[0][2] = ( B[0][1] * B[1][2] - B[0][2] * B[1][1] ) / det;
	inv3[1][0] = ( B[1][2] * B[2][0] - B[1][0] * B[2][2] ) / det;
	inv3[1][1] = ( B[0][0] * B[2][2] - B[0][2] * B[2][0] ) / det;
	inv3[1][2] = ( B[0][2] * B[1][0] - B[0][0] * B[1][2] ) / det;
	inv3[2][0] = ( B[1][0] * B[2][1] - B[1][1] * B[2][0] ) / det;
	inv3[2][1] = ( B[0][1] * B[2][0] - B[0][0] * B[2][1] ) / det;
	inv3[2][2] = ( B[0][0] * B[1][1] - B[0][1] * B[1][0] ) / det;
	for ( r = 0; r < 3; r++ )
		pivot[r] = -( inv3[r][0] * B[0][3] + inv3[r][1] * B[1][3] + inv3[r][2] * B[2][3] );

	for ( c = 0; c < 3; c++ ) { // the scale bolt * inv carries
		vec3_t col;
		for ( r = 0; r < 3; r++ )
			col[r] = bolt[r][0] * B[0][c] + bolt[r][1] * B[1][c] + bolt[r][2] * B[2][c];
		len += VectorLength( col ) / 3.0f;
	}
	if ( len < 1e-4f )
		len = 1;
	sc = ( modelScale > 0.01f ? modelScale : 1.0f ) / len;
	k = ( a ? a->scale : 100 ) / 100.0f;

	VectorSet( ang, a ? (float)a->ang[0] : 0, a ? (float)a->ang[1] : 0, a ? (float)a->ang[2] : 0 );
	AnglesToAxis( ang, U ); // columns: the turned forward / left / up
	for ( r = 0; r < 3; r++ ) // M1 = U k Q
		for ( c = 0; c < 3; c++ ) {
			M1[r][c] = 0;
			for ( j = 0; j < 3; j++ )
				M1[r][c] += U[j][r] * Q[j][c];
			M1[r][c] *= k;
		}
	for ( r = 0; r < 3; r++ ) // M2 = Pt M1, L = s M2
		for ( c = 0; c < 3; c++ ) {
			M2[r][c] = 0;
			for ( j = 0; j < 3; j++ )
				M2[r][c] += Pt[r][j] * M1[j][c];
			L[r][c] = sc * M2[r][c];
		}
	VectorSet( ofs, a ? a->ofs[0] * 0.1f : 0, a ? a->ofs[1] * 0.1f : 0, a ? a->ofs[2] * 0.1f : 0 );
	for ( r = 0; r < 3; r++ ) {
		for ( c = 0; c < 3; c++ )
			G[r][c] = L[r][c];
		G[r][3] = pivot[r] - ( L[r][0] * pivot[0] + L[r][1] * pivot[1] + L[r][2] * pivot[2] )
			+ sc * ( Pt[r][0] * ofs[0] + Pt[r][1] * ofs[1] + Pt[r][2] * ofs[2] );
	}
	for ( r = 0; r < 3; r++ ) // C = B G
		for ( c = 0; c < 4; c++ ) {
			C[r][c] = ( c == 3 ) ? B[r][3] : 0;
			for ( j = 0; j < 3; j++ )
				C[r][c] += B[r][j] * G[j][c];
		}
	for ( r = 0; r < 3; r++ ) { // world = bolt C; MD3: origin + v.x axis[0] + v.y axis[1] + v.z axis[2]
		for ( c = 0; c < 3; c++ )
			axis[c][r] = bolt[r][0] * C[0][c] + bolt[r][1] * C[1][c] + bolt[r][2] * C[2][c];
		origin[r] = bolt[r][0] * C[0][3] + bolt[r][1] * C[1][3] + bolt[r][2] * C[2][3] + bolt[r][3];
	}
}
