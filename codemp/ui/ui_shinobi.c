/*
===========================================================================
JK_Shinobi - the ninja build screen, in the UI module.

It takes the place of the stock profile / force-power menus
("ingame_player", "ingame_playerforce", UIMENU_PLAYERCONFIG and
UIMENU_PLAYERFORCE): it comes up when you enter a server (as a spectator,
before the first spawn), on B, and wherever the game used to show the
force-rank screen. Drawn immediate-mode on top of the menus; the mouse and
keys come straight from UI_KeyEvent while it is open.

The build goes to the "shinobi_build" userinfo cvar; the server applies it
on the next spawn.
===========================================================================
*/
#include "ui_local.h"
#include "game/bg_shinobi.h"

int  Text_Width( const char *text, float scale, int iMenuFont );
void Text_Paint( float x, float y, float scale, vec4_t color, const char *text, float adjust, int limit, int style, int iMenuFont );

static struct {
	qboolean       open;
	qboolean       bypass;          // let one stock menu through (Appearance)
	shinobiBuild_t edit;
	int            tab;             // school shown in the list
	qboolean       click;
	float          clickX, clickY;
	char           msg[128];
	int            msgTime;
	qboolean       msgBad;
	const char    *descTitle;       // description panel: what the mouse points at
	const char    *descText;
	int            descSchool;
} sb;

static vmCvar_t ui_shinobiBuild, ui_shinobiAcc;
static qboolean sbChar; // the character screen is up (Appearance)

qboolean UI_ShinobiCharKey( int key, qboolean down );
qboolean UI_ShinobiCharDraw( void );
void     UI_ShinobiCharOpen( void );
void     UI_ShinobiWeaponOpen( void );


static const vec4_t sbSchoolColor[SS_COUNT] = {
	{ 0.8f, 0.8f, 0.8f, 1 },
	{ 1.0f, 0.5f, 0.2f, 1 },   // fire
	{ 0.4f, 0.7f, 1.0f, 1 },   // water
	{ 0.6f, 0.95f, 0.6f, 1 },  // wind
	{ 0.85f, 0.65f, 0.4f, 1 }, // earth
	{ 0.8f, 0.85f, 1.0f, 1 },  // lightning
	{ 0.85f, 0.6f, 1.0f, 1 },  // yin / yang
};

#define SB_NOW ( uiInfo.uiDC.realTime )
#define SB_CX  ( (float)uiInfo.uiDC.cursorx )
#define SB_CY  ( (float)uiInfo.uiDC.cursory )

static void SB_Message( qboolean bad, const char *text ) {
	Q_strncpyz( sb.msg, text, sizeof( sb.msg ) );
	sb.msgTime = SB_NOW;
	sb.msgBad = bad;
}

static int SB_MyTeam( void ) {
	uiClientState_t cs;
	char            info[MAX_INFO_STRING];

	trap->GetClientState( &cs );
	if ( cs.connState != CA_ACTIVE )
		return TEAM_SPECTATOR;
	trap->GetConfigString( CS_PLAYERS + cs.clientNum, info, sizeof( info ) );
	if ( !info[0] )
		return TEAM_SPECTATOR;
	return atoi( Info_ValueForKey( info, "t" ) );
}

qboolean UI_ShinobiMenuIsOpen( void ) {
	return sb.open;
}

void UI_ShinobiMenuOpen( void ) {
	char buf[256];
	int  i;

	trap->Cvar_Register( &ui_shinobiBuild, "shinobi_build", "", CVAR_USERINFO | CVAR_ARCHIVE );
	trap->Cvar_Register( &ui_shinobiAcc, "shinobi_acc", "", CVAR_USERINFO | CVAR_ARCHIVE );
	trap->Cvar_Register( NULL, "shinobi_weapon", "one", CVAR_USERINFO | CVAR_ARCHIVE );
	Menus_CloseAll();
	trap->Key_SetCatcher( KEYCATCH_UI );

	trap->Cvar_VariableStringBuffer( "shinobi_build", buf, sizeof( buf ) );
	if ( buf[0] )
		Shinobi_BuildParse( buf, &sb.edit );
	else
		Shinobi_BuildDefault( &sb.edit );
	Shinobi_BuildFix( &sb.edit );

	// open on the school of the first technique
	sb.tab = SS_KATON;
	for ( i = 0; i < sb.edit.numJutsu; i++ ) {
		if ( shinobiJutsuSchool[sb.edit.jutsu[i]] != SS_BASE ) {
			sb.tab = shinobiJutsuSchool[sb.edit.jutsu[i]];
			break;
		}
	}
	sb.open = qtrue;
	sb.click = qfalse;
	sb.msg[0] = 0;
	sb.descTitle = sb.descText = NULL;
	if ( !buf[0] )
		SB_Message( qfalse, "No build saved yet - this is the default one, change it or just press Join" );
}

void UI_ShinobiMenuClose( qboolean releaseKeys ) {
	if ( !sb.open )
		return;
	sb.open = qfalse;
	sb.click = qfalse;
	if ( releaseKeys ) {
		Menus_CloseAll();
		trap->Key_SetCatcher( trap->Key_GetCatcher() & ~KEYCATCH_UI );
		trap->Key_ClearStates();
		trap->Cvar_Set( "cl_paused", "0" );
	}
}

// Menus_ActivateByName: the stock profile / force screens open this one instead
qboolean UI_ShinobiMenuRedirect( const char *name ) {
	if ( sb.bypass || !name )
		return qfalse;
	if ( Q_stricmp( name, "ingame_player" ) && Q_stricmp( name, "ingame_playerforce" ) )
		return qfalse;
	UI_ShinobiMenuOpen();
	return qtrue;
}


static qboolean SB_Save( void ) {
	char why[128], str[256];

	if ( !Shinobi_BuildValid( &sb.edit, why, sizeof( why ) ) ) {
		SB_Message( qtrue, why );
		return qfalse;
	}
	Shinobi_BuildString( &sb.edit, str, sizeof( str ) );
	trap->Cvar_Set( "shinobi_build", str );
	Com_Printf( "[shinobi] build saved: %s\n", str );
	return qtrue;
}

static void SB_Join( void ) {
	if ( !SB_Save() )
		return;
	UI_ShinobiMenuClose( qtrue );
	// the new userinfo goes out at the end of this frame: join after it
	trap->Cmd_ExecuteText( EXEC_APPEND, "wait; wait; team free\n" );
}

static void SB_Apply( void ) {
	if ( !SB_Save() )
		return;
	UI_ShinobiMenuClose( qtrue );
	Com_Printf( "[shinobi] the new build takes effect when you respawn\n" );
}

qboolean UI_ShinobiMenuKey( int key, qboolean down ) {
	if ( !sb.open )
		return qfalse;
	if ( sbChar ) {
		if ( UI_ShinobiCharKey( key, down ) )
			sbChar = qfalse;
		return qtrue;
	}
	if ( !down )
		return qtrue;
	switch ( key ) {
	case A_MOUSE1:
		sb.click = qtrue;
		sb.clickX = SB_CX;
		sb.clickY = SB_CY;
		break;
	case A_ENTER:
	case A_KP_ENTER:
		if ( SB_MyTeam() == TEAM_SPECTATOR )
			SB_Join();
		else
			SB_Apply();
		break;
	case A_ESCAPE:
	case 'b':
	case 'B':
		UI_ShinobiMenuClose( qtrue );
		break;
	case A_TAB:
	case A_CURSOR_RIGHT:
		sb.tab = sb.tab + 1 >= SS_COUNT ? SS_KATON : sb.tab + 1;
		break;
	case A_CURSOR_LEFT:
		sb.tab = sb.tab - 1 <= SS_BASE ? SS_COUNT - 1 : sb.tab - 1;
		break;
	default:
		break;
	}
	return qtrue;
}

/*
---------------------------------------------------------------------------
drawing helpers
---------------------------------------------------------------------------
*/
static qboolean SB_Over( float x, float y, float w, float h ) {
	return ( SB_CX >= x && SB_CX < x + w && SB_CY >= y && SB_CY < y + h ) ? qtrue : qfalse;
}

static qboolean SB_Clicked( float x, float y, float w, float h ) {
	if ( sb.click && sb.clickX >= x && sb.clickX < x + w && sb.clickY >= y && sb.clickY < y + h ) {
		sb.click = qfalse;
		trap->S_StartLocalSound( uiInfo.uiDC.Assets.itemFocusSound, CHAN_LOCAL_SOUND );
		return qtrue;
	}
	return qfalse;
}

static void SB_Text( float x, float y, float scale, const vec4_t color, const char *text ) {
	Text_Paint( x, y, scale, (float *)color, text, 0, 0, ITEM_TEXTSTYLE_SHADOWED, FONT_MEDIUM );
}

static void SB_Rect( float x, float y, float w, float h, const vec4_t color ) {
	_UI_DrawRect( x, y, w, h, 1, color );
}

static qboolean SB_Button( float x, float y, float w, float h, const char *label, qboolean enabled, qboolean main ) {
	const vec4_t bg = { 0.12f, 0.25f, 0.4f, 0.9f }, bgHot = { 0.2f, 0.4f, 0.65f, 0.95f };
	const vec4_t bgMain = { 0.15f, 0.45f, 0.2f, 0.9f }, bgMainHot = { 0.25f, 0.65f, 0.3f, 0.95f };
	const vec4_t frame = { 1, 1, 1, 0.4f }, white = { 1, 1, 1, 1 }, grey = { 0.5f, 0.5f, 0.5f, 1 };
	const float  ts = 0.5f, tw = Text_Width( label, ts, FONT_MEDIUM );
	const qboolean hot = enabled && SB_Over( x, y, w, h );

	UI_FillRect( x, y, w, h, main ? ( hot ? bgMainHot : bgMain ) : ( hot ? bgHot : bg ) );
	SB_Rect( x, y, w, h, frame );
	SB_Text( x + ( w - tw ) * 0.5f, y + ( h - 14 ) * 0.5f, ts, enabled ? white : grey, label );
	return ( SB_Clicked( x, y, w, h ) && enabled ) ? qtrue : qfalse;
}

static void SB_Describe( const char *title, const char *text, int school ) {
	sb.descTitle = title;
	sb.descText = text;
	sb.descSchool = school;
}

// word-wrapped text in a box; returns the y below it
static float SB_Wrapped( float x, float y, float w, float scale, float lineH, const vec4_t color, const char *text ) {
	char        line[256];
	const char *p = text;

	while ( *p ) {
		int len = 0, lastSpace = -1;

		while ( p[len] && p[len] != '\n' && len < (int)sizeof( line ) - 1 ) {
			line[len] = p[len];
			line[len + 1] = 0;
			if ( p[len] == ' ' )
				lastSpace = len;
			if ( Text_Width( line, scale, FONT_MEDIUM ) > w && lastSpace > 0 ) {
				len = lastSpace;
				break;
			}
			len++;
		}
		line[len] = 0;
		SB_Text( x, y, scale, color, line );
		y += lineH;
		p += len;
		while ( *p == ' ' || *p == '\n' )
			p++;
	}
	return y;
}

static void SB_ToggleJutsu( int j ) {
	shinobiBuild_t test;
	int            i;

	for ( i = 0; i < sb.edit.numJutsu; i++ ) {
		if ( sb.edit.jutsu[i] == j ) { // remove
			for ( ; i < sb.edit.numJutsu - 1; i++ )
				sb.edit.jutsu[i] = sb.edit.jutsu[i + 1];
			sb.edit.numJutsu--;
			return;
		}
	}
	if ( sb.edit.numJutsu >= SHINOBI_BUILD_JUTSU ) {
		SB_Message( qtrue, va( "At most %i techniques - remove one first (right column)", SHINOBI_BUILD_JUTSU ) );
		return;
	}
	test = sb.edit;
	test.jutsu[test.numJutsu++] = j;
	if ( Shinobi_BuildSchools( &test, NULL ) > SHINOBI_BUILD_SCHOOLS ) {
		SB_Message( qtrue, va( "Techniques from at most %i schools", SHINOBI_BUILD_SCHOOLS ) );
		return;
	}
	sb.edit = test;
}

static int SB_SchoolCount( int school ) {
	int i, n = 0;
	for ( i = 0; i < sb.edit.numJutsu; i++ ) {
		if ( shinobiJutsuSchool[sb.edit.jutsu[i]] == school )
			n++;
	}
	return n;
}

static const char *SB_Kind( int j ) {
	if ( SHINOBI_JUTSU_PASSIVE( j ) )
		return "passive";
	if ( SHINOBI_JUTSU_HOLD( j ) )
		return "hold";
	if ( SHINOBI_JUTSU_INSTANT( j ) )
		return "instant";
	return "charge";
}

/*
---------------------------------------------------------------------------
the screen
---------------------------------------------------------------------------
*/
void UI_ShinobiMenuDraw( void ) {
	const vec4_t back = { 0.02f, 0.03f, 0.05f, 0.88f }, frame = { 0.5f, 0.7f, 1, 0.5f };
	const vec4_t white = { 1, 1, 1, 1 }, dim = { 0.7f, 0.7f, 0.7f, 1 }, title = { 0.55f, 0.85f, 1, 1 };
	const vec4_t on = { 0.25f, 0.55f, 0.85f, 0.95f }, off = { 0.15f, 0.15f, 0.15f, 0.9f }, hot = { 1, 1, 1, 0.08f };
	const vec4_t good = { 0.5f, 1, 0.5f, 1 }, bad = { 1, 0.45f, 0.4f, 1 }, box = { 0, 0, 0, 0.5f };
	const int    team = SB_MyTeam();
	int          schools[SS_COUNT], schoolsUsed, sc, i, k;
	float        x, y;

	if ( !sb.open )
		return;
	if ( !( trap->Key_GetCatcher() & KEYCATCH_UI ) ) { // closed from outside
		sb.open = qfalse;
		sbChar = qfalse;
		return;
	}
	if ( sbChar ) {
		if ( UI_ShinobiCharDraw() )
			sbChar = qfalse;
		return;
	}
	memset( schools, 0, sizeof( schools ) );
	schoolsUsed = Shinobi_BuildSchools( &sb.edit, NULL );
	for ( i = 0; i < sb.edit.numJutsu; i++ )
		schools[shinobiJutsuSchool[sb.edit.jutsu[i]]] = 1;
	sb.descTitle = NULL;

	UI_FillRect( 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, back );
	SB_Text( 20, 8, 0.8f, title, "NINJA BUILD" );
	SB_Text( 220, 16, 0.42f, dim, team == TEAM_SPECTATOR
		? "Choose your techniques, then Join the battle"
		: "Changes take effect when you respawn" );

	// --- school tabs ------------------------------------------------------
	y = 44;
	for ( sc = SS_KATON; sc < SS_COUNT; sc++ ) {
		const float    tx = 20 + ( sc - SS_KATON ) * 101, tw = 98;
		const int      n = SB_SchoolCount( sc );
		const qboolean closed = !schools[sc] && schoolsUsed >= SHINOBI_BUILD_SCHOOLS;
		vec4_t         fill, text;

		VectorScale( sbSchoolColor[sc], sc == sb.tab ? 0.45f : 0.12f, fill );
		fill[3] = 0.95f;
		VectorCopy( sbSchoolColor[sc], text );
		text[3] = closed ? 0.45f : 1.0f;
		UI_FillRect( tx, y, tw, 22, fill );
		if ( SB_Over( tx, y, tw, 22 ) )
			UI_FillRect( tx, y, tw, 22, hot );
		SB_Rect( tx, y, tw, 22, sc == sb.tab ? white : frame );
		SB_Text( tx + 6, y + 3, 0.5f, text, n ? va( "%s (%i)", shinobiSchoolTitles[sc], n ) : shinobiSchoolTitles[sc] );
		if ( SB_Clicked( tx, y, tw, 22 ) )
			sb.tab = sc;
	}

	// --- technique list of the tab -----------------------------------------
	x = 20;
	y = 76;
	UI_FillRect( x, y - 4, 360, 172, box );
	for ( k = 0; k < SHINOBI_CYCLE_COUNT; k++ ) {
		const int      j = shinobiJutsuCycle[k];
		const qboolean have = Shinobi_BuildHas( &sb.edit, j );

		if ( shinobiJutsuSchool[j] != sb.tab )
			continue;
		if ( SB_Over( x, y, 360, 20 ) ) {
			UI_FillRect( x, y, 360, 20, hot );
			SB_Describe( shinobiJutsuTitles[j], shinobiJutsuInfo[j], sb.tab );
		}
		UI_FillRect( x + 6, y + 5, 10, 10, have ? on : off );
		SB_Rect( x + 6, y + 5, 10, 10, frame );
		SB_Text( x + 24, y + 1, 0.52f, have ? white : dim, shinobiJutsuTitles[j] );
		SB_Text( x + 190, y + 3, 0.4f, dim, SB_Kind( j ) );
		if ( SHINOBI_JUTSU_PASSIVE( j ) )
			SB_Text( x + 250, y + 3, 0.4f, dim, "-" );
		else if ( SHINOBI_JUTSU_INSTANT( j ) || SHINOBI_JUTSU_HOLD( j ) )
			SB_Text( x + 250, y + 3, 0.4f, dim, va( "%i chakra", Shinobi_JutsuCost( j, 1 ) ) );
		else
			SB_Text( x + 250, y + 3, 0.4f, dim, va( "%i/%i/%i chakra", Shinobi_JutsuCost( j, 1 ), Shinobi_JutsuCost( j, 2 ), Shinobi_JutsuCost( j, 3 ) ) );
		if ( SB_Clicked( x, y, 360, 20 ) )
			SB_ToggleJutsu( j );
		y += 20;
	}

	// --- your techniques -----------------------------------------------------
	x = 396;
	y = 72;
	SB_Text( x, y, 0.5f, white, va( "YOUR TECHNIQUES %i/%i", sb.edit.numJutsu, SHINOBI_BUILD_JUTSU ) );
	SB_Text( x + 170, y + 3, 0.4f, dim, va( "schools %i/%i", schoolsUsed, SHINOBI_BUILD_SCHOOLS ) );
	y += 20;
	for ( i = 0; i < SHINOBI_BUILD_JUTSU; i++ ) {
		UI_FillRect( x, y, 224, 18, off );
		if ( i < sb.edit.numJutsu ) {
			const int j = sb.edit.jutsu[i];
			if ( SB_Over( x, y, 176, 18 ) )
				SB_Describe( shinobiJutsuTitles[j], shinobiJutsuInfo[j], shinobiJutsuSchool[j] );
			SB_Text( x + 4, y + 1, 0.47f, sbSchoolColor[shinobiJutsuSchool[j]], va( "%i. %s", i + 1, shinobiJutsuTitles[j] ) );
			if ( SB_Clicked( x, y, 176, 18 ) )
				sb.tab = shinobiJutsuSchool[j];
			if ( i > 0 && SB_Button( x + 180, y + 1, 20, 16, "^", qtrue, qfalse ) ) { // earlier in the cycle
				sb.edit.jutsu[i] = sb.edit.jutsu[i - 1];
				sb.edit.jutsu[i - 1] = j;
			}
			if ( SB_Button( x + 203, y + 1, 20, 16, "x", qtrue, qfalse ) )
				SB_ToggleJutsu( j );
		}
		else {
			SB_Text( x + 4, y + 1, 0.47f, off, va( "%i. -", i + 1 ) );
		}
		y += 20;
	}

	// --- stats -----------------------------------------------------------------
	y += 8;
	SB_Text( x, y, 0.5f, white, va( "STATS  points left %i of %i", Shinobi_StatBudget( &sb.edit ) - Shinobi_BuildPoints( &sb.edit ),
		Shinobi_StatBudget( &sb.edit ) ) );
	y += 20;
	for ( i = 0; i < SST_COUNT; i++ ) {
		if ( SB_Over( x, y, 224, 18 ) )
			SB_Describe( shinobiStatTitles[i], shinobiStatInfo[i], SS_BASE );
		SB_Text( x, y + 1, 0.45f, white, shinobiStatTitles[i] );
		if ( SB_Button( x + 100, y + 1, 16, 16, "-", sb.edit.stats[i] > 0, qfalse ) )
			sb.edit.stats[i]--;
		for ( k = 0; k < Shinobi_StatCap( &sb.edit, i ); k++ ) { // 5 boxes, 7 for the body without a special
			UI_FillRect( x + 121 + k * 11, y + 5, 9, 8, k < sb.edit.stats[i] ? on : off );
			SB_Rect( x + 121 + k * 11, y + 5, 9, 8, frame );
		}
		if ( SB_Button( x + 204, y + 1, 16, 16, "+", sb.edit.stats[i] < Shinobi_StatCap( &sb.edit, i )
			&& Shinobi_BuildPoints( &sb.edit ) < Shinobi_StatBudget( &sb.edit ), qfalse ) )
			sb.edit.stats[i]++;
		y += 19;
	}

	// --- special ---------------------------------------------------------------
	y += 8;
	SB_Text( x, y, 0.5f, white, "SPECIAL (C)  costs stat points" );
	y += 20;
	for ( i = 0; i < SP_COUNT; i++ ) {
		const qboolean chosen = ( sb.edit.perk == i );
		const qboolean ready = shinobiPerkReady[i];

		if ( SB_Over( x, y, 224, 17 ) ) {
			UI_FillRect( x, y, 224, 17, hot );
			SB_Describe( shinobiPerkTitles[i], shinobiPerkInfo[i], SS_BASE );
		}
		UI_FillRect( x + 4, y + 4, 9, 9, chosen ? on : off );
		SB_Rect( x + 4, y + 4, 9, 9, frame );
		SB_Text( x + 20, y + 1, 0.45f, ready ? ( chosen ? white : dim ) : off,
			ready ? shinobiPerkTitles[i] : va( "%s (soon)", shinobiPerkTitles[i] ) );
		SB_Text( x + 176, y + 1, 0.45f, ready ? ( chosen ? white : dim ) : off,
			i == SP_NONE ? "7 max" : va( "-%i", shinobiPerkCost[i] ) );
		if ( SB_Clicked( x, y, 224, 17 ) ) {
			if ( ready && sb.edit.perk != i ) {
				const int before = Shinobi_BuildPoints( &sb.edit );
				sb.edit.perk = i;
				Shinobi_BuildFix( &sb.edit ); // the new price / caps: points come back off the end
				if ( Shinobi_BuildPoints( &sb.edit ) < before )
					SB_Message( qtrue, va( "%s costs %i stat points: %i taken back", shinobiPerkTitles[i], shinobiPerkCost[i],
						before - Shinobi_BuildPoints( &sb.edit ) ) );
			}
			else if ( ready )
				; // already chosen
			else
				SB_Message( qtrue, va( "%s isn't ready yet", shinobiPerkTitles[i] ) );
		}
		y += 18;
	}

	// --- description panel ----------------------------------------------------
	x = 20;
	y = 256;
	UI_FillRect( x, y, 360, 150, box );
	SB_Rect( x, y, 360, 150, frame );
	if ( sb.descTitle ) {
		SB_Text( x + 8, y + 4, 0.6f, sbSchoolColor[sb.descSchool], sb.descTitle );
		SB_Wrapped( x + 8, y + 28, 344, 0.45f, 15, white, sb.descText ? sb.descText : "" );
	}
	else {
		SB_Wrapped( x + 8, y + 8, 344, 0.45f, 15, dim,
			"Point at a technique, stat or special to read about it. Click a technique to take it or drop it. "
			"Up to 5 techniques from at most 2 schools; the order on the right is the order of your selection cycle.\n"
			"Always yours: Shunshin (Shift+Space), Kawarimi (X), chakra (R), kunai (Z), block (right mouse) and your fists." );
	}

	if ( sb.msg[0] && SB_NOW - sb.msgTime < 5000 )
		SB_Text( 20, 414, 0.45f, sb.msgBad ? bad : good, sb.msg );

	// --- buttons -----------------------------------------------------------------
	y = 446;
	if ( team == TEAM_SPECTATOR ) {
		if ( SB_Button( 20, y, 140, 24, "Join battle", qtrue, qtrue ) )
			SB_Join();
	}
	else if ( SB_Button( 20, y, 140, 24, "Apply", qtrue, qtrue ) ) {
		SB_Apply();
	}
	if ( sb.open && SB_Button( 166, y, 80, 24, "Default", qtrue, qfalse ) ) {
		Shinobi_BuildDefault( &sb.edit );
		SB_Message( qfalse, "Default build - Join / Apply to keep it" );
	}
	if ( sb.open && SB_Button( 252, y, 106, 24, "Appearance", qtrue, qfalse ) ) {
		UI_ShinobiCharOpen();
		sbChar = qtrue;
	}
	if ( sb.open && SB_Button( 364, y, 86, 24, "Weapon", qtrue, qfalse ) ) {
		UI_ShinobiWeaponOpen();
		sbChar = qtrue;
	}
	if ( sb.open && team != TEAM_SPECTATOR && SB_Button( 456, y, 84, 24, "Spectate", qtrue, qfalse ) ) {
		UI_ShinobiMenuClose( qtrue );
		trap->Cmd_ExecuteText( EXEC_APPEND, "team spectator\n" );
	}
	if ( sb.open && SB_Button( 546, y, 74, 24, "Close", qtrue, qfalse ) )
		UI_ShinobiMenuClose( qtrue );

	sb.click = qfalse; // a click on nothing is dropped
}
