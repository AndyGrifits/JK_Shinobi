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
