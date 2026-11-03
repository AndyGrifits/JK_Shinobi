/*
===========================================================================
JK_Shinobi - the character screen (Appearance on the build screen).

Instead of the stock profile menu: a character (every player model folder with a
shinobi.char file - models/players/<name>/shinobi.char: its title on the first line,
"female" on the second for a woman; tools/blend2glm.py writes it), its look (the
model_<look>.skin files there: default, props...), and up to SHINOBI_ACC_MAX of our
accessories, each with its own scale, offset and turn. A live preview of the whole
character, turned by dragging it. Every change goes out at once through the "model",
"sex" and "shinobi_acc" userinfo, so everyone sees it.
===========================================================================
*/
#include "ui_local.h"
#include "game/bg_shinobi.h"
#include "ghoul2/G2.h"

int  Text_Width( const char *text, float scale, int iMenuFont );
void Text_Paint( float x, float y, float scale, vec4_t color, const char *text, float adjust, int limit, int style, int iMenuFont );
int  UI_ParseAnimationFile( const char *filename, animation_t *animset, qboolean isHumanoid );
void UI_InsertG2Pointer( void *ghoul2 );
void UI_ClearG2Pointer( void *ghoul2 );

#define SC_MAX_CHARS 40
#define SC_MAX_LOOKS 8
#define SC_MAX_ACC   64
#define SC_PAGE      10   // character tiles on one page (2 rows of 5)

typedef struct {
	char      dir[32], title[40];
	qboolean  female;
	char      looks[SC_MAX_LOOKS][24];   // "default", "props"...
	int       numLooks;
	qhandle_t icon;
} scChar_t;

static struct {
	qboolean     ready;
	scChar_t     chars[SC_MAX_CHARS];
	int          numChars;
	int          cur, look;                  // the character worn (-1: none of ours), its look
	int          page;
	shinobiAcc_t acc[SHINOBI_ACC_MAX];      // name "" = empty slot
	int          slot;                       // slot being edited
	char         accList[SC_MAX_ACC][32];
	qhandle_t    accModel[SC_MAX_ACC];
	int          accCount;

	void        *g2;
	int          g2Char;                     // the character g2 was made for (-1 none)
	qhandle_t    skin;
	char         skinName[MAX_QPATH];
	int          bolt;
	qboolean     invTried, invOk;
	float        inv[3][4];

	float        yaw;
	qboolean     click, mouseDown;
	float        clickX, clickY;
	int         *drag;                       // slider being dragged
	int          dragMin, dragMax;
	float        dragBarX, dragBarW;
	qboolean     dragAcc;                    // the slider was an accessory's: send on release
	qboolean     rotating;
	float        rotX;
} sc;

#define SC_CX ( (float)uiInfo.uiDC.cursorx )
#define SC_CY ( (float)uiInfo.uiDC.cursory )

/*
---------------------------------------------------------------------------
lists and cvars
---------------------------------------------------------------------------
*/
// every models/players/<dir> with a shinobi.char, and its looks (model_*.skin)
static void SC_ListChars( void ) {
	static char dirs[8192], skins[2048];
	char       *d;
	int         nd, i;

	sc.numChars = 0;
	nd = trap->FS_GetFileList( "models/players", "/", dirs, sizeof( dirs ) );
	for ( i = 0, d = dirs; i < nd && sc.numChars < SC_MAX_CHARS; i++, d += strlen( d ) + 1 ) {
		scChar_t    *c = &sc.chars[sc.numChars];
		char         name[32], text[256], *f, *nl;
		fileHandle_t fh;
		int          len, ns, k;

		Q_strncpyz( name, d, sizeof( name ) );
		len = strlen( name );
		if ( len && ( name[len - 1] == '/' || name[len - 1] == '\\' ) )
			name[--len] = 0;
		if ( !name[0] || name[0] == '.' || name[0] == '_' )
			continue;
		// the same folder can come back twice (from a pk3 as "kakashi/" and loose on disk
		// as "kakashi"): one tile each
		for ( k = 0; k < sc.numChars; k++ )
			if ( !Q_stricmp( sc.chars[k].dir, name ) )
				break;
		if ( k < sc.numChars )
			continue;
		len = trap->FS_Open( va( "models/players/%s/shinobi.char", name ), &fh, FS_READ );
		if ( !fh )
			continue; // not one of ours
		memset( c, 0, sizeof( *c ) );
		Q_strncpyz( c->dir, name, sizeof( c->dir ) );
		Q_strncpyz( c->title, name, sizeof( c->title ) );
		if ( len > 0 ) {
			if ( len > (int)sizeof( text ) - 1 )
				len = sizeof( text ) - 1;
			trap->FS_Read( text, len, fh );
			text[len] = 0;
			if ( ( nl = strchr( text, '\n' ) ) != NULL ) {
				*nl = 0;
				char low[256];
				Q_strncpyz( low, nl + 1, sizeof( low ) );
				Q_strlwr( low );
				c->female = strstr( low, "female" ) != NULL;
			}
			if ( ( nl = strchr( text, '\r' ) ) != NULL )
				*nl = 0;
			if ( text[0] )
				Q_strncpyz( c->title, text, sizeof( c->title ) );
		}
		trap->FS_Close( fh );
		ns = trap->FS_GetFileList( va( "models/players/%s", name ), ".skin", skins, sizeof( skins ) );
		for ( k = 0, f = skins; k < ns && c->numLooks < SC_MAX_LOOKS; k++, f += strlen( f ) + 1 ) {
			char look[32], *dot;
			if ( Q_stricmpn( f, "model_", 6 ) )
				continue;
			Q_strncpyz( look, f + 6, sizeof( look ) );
			if ( ( dot = strrchr( look, '.' ) ) != NULL )
				*dot = 0;
			if ( !Q_stricmp( look, "default" ) && c->numLooks ) { // "default" first
				Q_strncpyz( c->looks[c->numLooks++], c->looks[0], sizeof( c->looks[0] ) );
				Q_strncpyz( c->looks[0], look, sizeof( c->looks[0] ) );
			}
			else
				Q_strncpyz( c->looks[c->numLooks++], look, sizeof( c->looks[0] ) );
		}
		if ( !c->numLooks )
			continue;
		c->icon = trap->R_RegisterShaderNoMip( va( "models/players/%s/icon_%s", name, c->looks[0] ) );
		sc.numChars++;
	}
}

static void SC_ListAccessories( void ) {
	static char list[8192];
	char       *f;
	int         n, i;

	sc.accCount = 0;
	n = trap->FS_GetFileList( "models/accessories/head", ".md3", list, sizeof( list ) );
	for ( i = 0, f = list; i < n && sc.accCount < SC_MAX_ACC; i++, f += strlen( f ) + 1 ) {
		char *dot;
		Q_strncpyz( sc.accList[sc.accCount], f, 32 );
		if ( ( dot = strrchr( sc.accList[sc.accCount], '.' ) ) != NULL )
			*dot = 0;
		if ( !sc.accList[sc.accCount][0] )
			continue;
		sc.accModel[sc.accCount] = trap->R_RegisterModel( va( "models/accessories/head/%s.md3", sc.accList[sc.accCount] ) );
		sc.accCount++;
	}
}

static int SC_AccIndex( const char *name ) {
	int i;
	for ( i = 0; i < sc.accCount; i++ ) {
		if ( !Q_stricmp( sc.accList[i], name ) )
			return i;
	}
	return -1;
}

// read what the player wears now
static void SC_ReadCvars( void ) {
	char model[MAX_QPATH], buf[256], *slash;
	int  i, k;

	trap->Cvar_VariableStringBuffer( "model", model, sizeof( model ) );
	sc.cur = -1;
	sc.look = 0;
	slash = strchr( model, '/' );
	if ( slash )
		*slash++ = 0;
	for ( i = 0; i < sc.numChars; i++ ) {
		if ( Q_stricmp( model, sc.chars[i].dir ) )
			continue;
		sc.cur = i;
		for ( k = 0; slash && k < sc.chars[i].numLooks; k++ )
			if ( !Q_stricmp( slash, sc.chars[i].looks[k] ) )
				sc.look = k;
	}
	if ( sc.cur >= 0 )
		sc.page = sc.cur / SC_PAGE;

	memset( sc.acc, 0, sizeof( sc.acc ) );
	for ( i = 0; i < SHINOBI_ACC_MAX; i++ )
		Shinobi_AccDefault( &sc.acc[i] );
	trap->Cvar_VariableStringBuffer( "shinobi_acc", buf, sizeof( buf ) );
	Shinobi_AccParse( buf, sc.acc, SHINOBI_ACC_MAX );
}

static const char *SC_AccTitle( const char *name ) {
	static char t[40];
	int         i;
	if ( !name[0] )
		return "none";
	Q_strncpyz( t, name, sizeof( t ) );
	for ( i = 0; t[i]; i++ ) {
		if ( t[i] == '_' )
			t[i] = ' ';
		else if ( ( i == 0 || t[i - 1] == ' ' ) && t[i] >= 'a' && t[i] <= 'z' )
			t[i] -= 32;
	}
	return t;
}

// send the look out (userinfo: everyone sees it at once)
static void SC_ApplyBody( void ) {
	const scChar_t *c;
	if ( sc.cur < 0 || sc.cur >= sc.numChars )
		return;
	c = &sc.chars[sc.cur];
	if ( sc.look <= 0 || sc.look >= c->numLooks )
		trap->Cvar_Set( "model", c->dir );
	else
		trap->Cvar_Set( "model", va( "%s/%s", c->dir, c->looks[sc.look] ) );
	trap->Cvar_Set( "sex", c->female ? "female" : "male" );
	trap->Cvar_Set( "char_color_red", "255" ); // (our models aren't tinted)
	trap->Cvar_Set( "char_color_green", "255" );
	trap->Cvar_Set( "char_color_blue", "255" );
}

static void SC_ApplyAcc( void ) {
	shinobiAcc_t worn[SHINOBI_ACC_MAX];
	char         str[SHINOBI_ACC_STRING + 1];
	int          i, n = 0;
	for ( i = 0; i < SHINOBI_ACC_MAX; i++ ) {
		if ( sc.acc[i].name[0] )
			worn[n++] = sc.acc[i];
	}
	Shinobi_AccString( worn, n, str, sizeof( str ) );
	trap->Cvar_Set( "shinobi_acc", str );
}

void UI_ShinobiCharOpen( void ) {
	trap->Cvar_Register( NULL, "shinobi_acc", "", CVAR_USERINFO | CVAR_ARCHIVE );
	SC_ListChars();
	SC_ListAccessories();
	SC_ReadCvars();
	sc.slot = 0;
	sc.click = sc.mouseDown = sc.rotating = qfalse;
	sc.drag = NULL;
	sc.g2Char = -1;
	sc.yaw = 0;
	sc.ready = qtrue;
}

/*
---------------------------------------------------------------------------
the preview
---------------------------------------------------------------------------
*/
static void SC_LoadInv( void ) {
	static byte  buf[131072];
	fileHandle_t f;
	int          len;

	sc.invTried = qtrue;
	len = trap->FS_Open( "models/players/_humanoid/_humanoid.gla", &f, FS_READ );
	if ( len <= 0 || !f )
		return;
	if ( len > (int)sizeof( buf ) )
		len = sizeof( buf );
	trap->FS_Read( buf, len, f );
	trap->FS_Close( f );
	sc.invOk = Shinobi_GlaBoneInv( buf, len, "cranium", sc.inv );
}

static void SC_PrepareModel( void ) {
	const scChar_t *c;
	char            skinName[MAX_QPATH];

	if ( sc.g2Char != sc.cur ) {
		char gla[MAX_QPATH], *slash;

		if ( sc.g2 ) {
			UI_ClearG2Pointer( sc.g2 );
			trap->G2API_CleanGhoul2Models( &sc.g2 );
			sc.g2 = NULL;
		}
		sc.g2Char = sc.cur;
		sc.skinName[0] = 0;
		if ( sc.cur < 0 )
			return;
		if ( trap->G2API_InitGhoul2Model( &sc.g2, va( "models/players/%s/model.glm", sc.chars[sc.cur].dir ), 0, 0, 0, 0, 0 ) < 0 || !sc.g2 ) {
			sc.g2 = NULL;
			return;
		}
		UI_InsertG2Pointer( sc.g2 );
		sc.bolt = trap->G2API_AddBolt( sc.g2, 0, "cranium" );
		// a calm stance
		gla[0] = 0;
		trap->G2API_GetGLAName( sc.g2, 0, gla );
		if ( gla[0] && ( slash = Q_strrchr( gla, '/' ) ) != NULL ) {
			int idx;
			strcpy( slash, "/animation.cfg" );
			idx = UI_ParseAnimationFile( gla, NULL, qfalse );
			if ( idx != -1 ) {
				const animation_t *a = &bgAllAnims[idx].anims[BOTH_STAND1];
				trap->G2API_SetBoneAnim( sc.g2, 0, "model_root", a->firstFrame, a->firstFrame + a->numFrames,
					BONE_ANIM_OVERRIDE_FREEZE | BONE_ANIM_OVERRIDE_LOOP, 50.0f / ( a->frameLerp ? a->frameLerp : 50 ),
					uiInfo.uiDC.realTime, -1, 150 );
			}
		}
	}
	if ( !sc.g2 || sc.cur < 0 )
		return;
	c = &sc.chars[sc.cur];
	Com_sprintf( skinName, sizeof( skinName ), "models/players/%s/model_%s.skin", c->dir,
		c->looks[sc.look >= 0 && sc.look < c->numLooks ? sc.look : 0] );
	if ( Q_stricmp( skinName, sc.skinName ) ) {
		Q_strncpyz( sc.skinName, skinName, sizeof( sc.skinName ) );
		sc.skin = trap->R_RegisterSkin( skinName );
		trap->G2API_SetSkin( sc.g2, 0, sc.skin, sc.skin );
	}
}

static void SC_DrawPreview( float x, float y, float w, float h ) {
	refdef_t    rd;
	refEntity_t ent;
	vec3_t      angles, origin;
	mdxaBone_t  m;
	const float fovY = 38.0f;
	int         i;

	SC_PrepareModel();
	if ( !sc.g2 )
		return;
	if ( !sc.invTried )
		SC_LoadInv();

	memset( &rd, 0, sizeof( rd ) );
	rd.rdflags = RDF_NOWORLDMODEL;
	AxisClear( rd.viewaxis );
	rd.x = x * uiInfo.uiDC.xscale;
	rd.y = y * uiInfo.uiDC.yscale;
	rd.width = w * uiInfo.uiDC.xscale;
	rd.height = h * uiInfo.uiDC.yscale;
	rd.fov_y = fovY;
	rd.fov_x = 2.0f * RAD2DEG( atanf( tanf( DEG2RAD( fovY * 0.5f ) ) * rd.width / ( rd.height > 0 ? rd.height : 1 ) ) );
	rd.time = uiInfo.uiDC.realTime;
	trap->R_ClearScene();

	VectorSet( angles, 0, 180 + sc.yaw, 0 );
	VectorSet( origin, 128, 0, -37 ); // feet at -37, the head near +34: the whole body in view
	memset( &ent, 0, sizeof( ent ) );
	ent.ghoul2 = sc.g2;
	ent.customSkin = sc.skin;
	ent.radius = 1000;
	AnglesToAxis( angles, ent.axis );
	VectorCopy( origin, ent.origin );
	VectorCopy( origin, ent.oldorigin );
	VectorCopy( origin, ent.lightingOrigin );
	ent.renderfx = RF_LIGHTING_ORIGIN | RF_NOSHADOW;
	ent.shaderRGBA[0] = ent.shaderRGBA[1] = ent.shaderRGBA[2] = ent.shaderRGBA[3] = 255;
	trap->R_AddRefEntityToScene( &ent );

	if ( sc.invOk && sc.bolt >= 0
		&& trap->G2API_GetBoltMatrix( sc.g2, 0, sc.bolt, &m, angles, origin, uiInfo.uiDC.realTime, NULL, vec3_origin ) ) {
		for ( i = 0; i < SHINOBI_ACC_MAX; i++ ) {
			const int k = sc.acc[i].name[0] ? SC_AccIndex( sc.acc[i].name ) : -1;
			refEntity_t acc;
			if ( k < 0 || !sc.accModel[k] )
				continue;
			memset( &acc, 0, sizeof( acc ) );
			acc.hModel = sc.accModel[k];
			Shinobi_AccPlace( (const float (*)[4])m.matrix, (const float (*)[4])sc.inv, &sc.acc[i], 1.0f, acc.origin, acc.axis );
			VectorCopy( acc.origin, acc.oldorigin );
			acc.nonNormalizedAxes = qtrue;
			VectorCopy( origin, acc.lightingOrigin );
			acc.renderfx = RF_LIGHTING_ORIGIN | RF_NOSHADOW;
			acc.shaderRGBA[0] = acc.shaderRGBA[1] = acc.shaderRGBA[2] = acc.shaderRGBA[3] = 255;
			trap->R_AddRefEntityToScene( &acc );
		}
	}
	trap->R_RenderScene( &rd );
}

/*
---------------------------------------------------------------------------
widgets
---------------------------------------------------------------------------
*/
static qboolean SC_Over( float x, float y, float w, float h ) {
	return ( SC_CX >= x && SC_CX < x + w && SC_CY >= y && SC_CY < y + h ) ? qtrue : qfalse;
}

static qboolean SC_Clicked( float x, float y, float w, float h ) {
	if ( sc.click && sc.clickX >= x && sc.clickX < x + w && sc.clickY >= y && sc.clickY < y + h ) {
		sc.click = qfalse;
		trap->S_StartLocalSound( uiInfo.uiDC.Assets.itemFocusSound, CHAN_LOCAL_SOUND );
		return qtrue;
	}
	return qfalse;
}

static void SC_Text( float x, float y, float scale, const vec4_t color, const char *text ) {
	Text_Paint( x, y, scale, (float *)color, text, 0, 0, ITEM_TEXTSTYLE_SHADOWED, FONT_MEDIUM );
}

static qboolean SC_Button( float x, float y, float w, float h, const char *label, qboolean selected ) {
	const vec4_t bg = { 0.12f, 0.25f, 0.4f, 0.9f }, bgHot = { 0.2f, 0.4f, 0.65f, 0.95f }, bgSel = { 0.15f, 0.45f, 0.2f, 0.95f };
	const vec4_t frame = { 1, 1, 1, 0.4f }, white = { 1, 1, 1, 1 };
	const float  ts = 0.45f, tw = Text_Width( label, ts, FONT_MEDIUM );

	UI_FillRect( x, y, w, h, selected ? bgSel : ( SC_Over( x, y, w, h ) ? bgHot : bg ) );
	_UI_DrawRect( x, y, w, h, 1, frame );
	SC_Text( x + ( w - tw ) * 0.5f, y + ( h - 13 ) * 0.5f, ts, white, label );
	return SC_Clicked( x, y, w, h );
}

// "<  value  >": returns -1 / +1 when an arrow was clicked
static int SC_Spinner( float x, float y, float w, const char *value, qboolean hot ) {
	const vec4_t box = { 0.15f, 0.15f, 0.15f, 0.9f }, boxHot = { 0.18f, 0.3f, 0.45f, 0.95f };
	const vec4_t white = { 1, 1, 1, 1 };
	const float  tw = Text_Width( value, 0.45f, FONT_MEDIUM );
	int          r = 0;

	if ( SC_Button( x, y, 20, 18, "<", qfalse ) )
		r = -1;
	UI_FillRect( x + 23, y, w - 46, 18, hot ? boxHot : box );
	SC_Text( x + 23 + ( w - 46 - tw ) * 0.5f, y + 1, 0.45f, white, value );
	if ( SC_Button( x + w - 20, y, 20, 18, ">", qfalse ) )
		r = 1;
	return r;
}

// a draggable slider; returns qtrue while it is being dragged or when it was set
static void SC_Slider( float x, float y, const char *label, int *val, int lo, int hi, const char *fmt, float div, qboolean acc ) {
	const vec4_t white = { 1, 1, 1, 1 }, dim = { 0.75f, 0.75f, 0.75f, 1 }, bar = { 0.15f, 0.15f, 0.15f, 0.9f };
	const vec4_t fill = { 0.25f, 0.55f, 0.85f, 0.95f }, knob = { 1, 1, 1, 1 };
	const float  bx = x + 92, bw = 170, by = y + 4, bh = 7;
	const float  f = (float)( *val - lo ) / ( hi - lo );

	SC_Text( x, y, 0.4f, white, label );
	UI_FillRect( bx, by, bw, bh, bar );
	if ( lo < 0 && hi > 0 ) { // centred: fill from the middle
		const float mid = bx + bw * ( (float)-lo / ( hi - lo ) ), at = bx + bw * f;
		UI_FillRect( at < mid ? at : mid, by, fabs( at - mid ), bh, fill );
	}
	else {
		UI_FillRect( bx, by, bw * f, bh, fill );
	}
	UI_FillRect( bx + bw * f - 2, by - 3, 4, bh + 6, knob );
	SC_Text( bx + bw + 8, y, 0.4f, dim, va( fmt, *val / div ) );
	if ( sc.click && sc.clickX >= bx - 4 && sc.clickX < bx + bw + 4 && sc.clickY >= y && sc.clickY < y + 15 ) {
		sc.click = qfalse;
		sc.drag = val;
		sc.dragMin = lo;
		sc.dragMax = hi;
		sc.dragBarX = bx;
		sc.dragBarW = bw;
		sc.dragAcc = acc;
	}
}

static void SC_UpdateDrag( void ) {
	if ( sc.drag ) {
		float f = ( SC_CX - sc.dragBarX ) / sc.dragBarW;
		f = f < 0 ? 0 : ( f > 1 ? 1 : f );
		*sc.drag = sc.dragMin + (int)( f * ( sc.dragMax - sc.dragMin ) + 0.5f );
	}
	if ( sc.rotating ) {
		sc.yaw += ( SC_CX - sc.rotX ) * 1.2f;
		sc.rotX = SC_CX;
	}
}

/*
---------------------------------------------------------------------------
input / screen
---------------------------------------------------------------------------
*/
// qtrue = back to the build screen
qboolean UI_ShinobiCharKey( int key, qboolean down ) {
	if ( key == A_MOUSE1 ) {
		if ( down ) {
			sc.click = qtrue;
			sc.mouseDown = qtrue;
			sc.clickX = SC_CX;
			sc.clickY = SC_CY;
		}
		else {
			sc.mouseDown = qfalse;
			if ( sc.drag ) { // let go of a slider: send it
				if ( sc.dragAcc )
					SC_ApplyAcc();

			}
			sc.drag = NULL;
			sc.rotating = qfalse;
		}
		return qfalse;
	}
	if ( down && ( key == A_ESCAPE || key == A_BACKSPACE ) )
		return qtrue;
	return qfalse;
}

// qtrue = back to the build screen
qboolean UI_ShinobiCharDraw( void ) {
	const vec4_t back = { 0.02f, 0.03f, 0.05f, 0.88f }, frame = { 0.5f, 0.7f, 1, 0.5f }, box = { 0, 0, 0, 0.55f };
	const vec4_t white = { 1, 1, 1, 1 }, dim = { 0.7f, 0.7f, 0.7f, 1 }, title = { 0.55f, 0.85f, 1, 1 };
	float        x0 = 300, y;
	int          i, d, back_ = 0;

	if ( !sc.ready )
		UI_ShinobiCharOpen();
	SC_UpdateDrag();

	UI_FillRect( 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, back );
	SC_Text( 20, 8, 0.8f, title, "CHARACTER" );
	SC_Text( 230, 16, 0.4f, dim, "drag the model to turn it - everything shows at once, to everyone" );

	// --- preview ------------------------------------------------------------
	UI_FillRect( 20, 40, 260, 400, box );
	_UI_DrawRect( 20, 40, 260, 400, 1, frame );
	SC_DrawPreview( 21, 41, 258, 398 );
	if ( sc.cur < 0 )
		SC_Text( 40, 230, 0.45f, dim, "pick a character on the right" );
	if ( sc.click && SC_Over( 20, 40, 260, 400 ) && sc.clickX >= 20 && sc.clickX < 280 && sc.clickY >= 40 && sc.clickY < 440 ) {
		sc.click = qfalse;
		sc.rotating = qtrue;
		sc.rotX = SC_CX;
	}

	// --- characters (tiles: icon + name), a page at a time -------------------------
	y = 40;
	SC_Text( x0, y, 0.5f, white, "CHARACTER" );
	if ( sc.numChars > SC_PAGE ) {
		const int pages = ( sc.numChars + SC_PAGE - 1 ) / SC_PAGE;
		d = SC_Spinner( x0 + 190, y, 130, va( "page %i/%i", sc.page + 1, pages ), qfalse );
		if ( d )
			sc.page = ( sc.page + d + pages ) % pages;
	}
	y += 20;
	if ( !sc.numChars )
		SC_Text( x0, y + 4, 0.4f, dim, "no characters: models/players/<name>/shinobi.char" );
	for ( i = 0; i < SC_PAGE; i++ ) {
		const int   k = sc.page * SC_PAGE + i;
		const float tx = x0 + ( i % 5 ) * 64, ty = y + ( i / 5 ) * 70;
		const vec4_t tile = { 0.1f, 0.18f, 0.28f, 0.9f }, tileHot = { 0.18f, 0.32f, 0.5f, 0.95f }, tileSel = { 0.15f, 0.45f, 0.2f, 0.95f };
		const scChar_t *c;
		float       tw;
		char        name[16];
		if ( k >= sc.numChars )
			break;
		c = &sc.chars[k];
		UI_FillRect( tx, ty, 60, 66, k == sc.cur ? tileSel : ( SC_Over( tx, ty, 60, 66 ) ? tileHot : tile ) );
		_UI_DrawRect( tx, ty, 60, 66, 1, frame );
		if ( c->icon )
			UI_DrawHandlePic( tx + 6, ty + 3, 48, 48, c->icon );
		Q_strncpyz( name, c->title, sizeof( name ) );
		tw = Text_Width( name, 0.32f, FONT_SMALL );
		Text_Paint( tx + ( 60 - tw ) * 0.5f, ty + 52, 0.32f, (float *)white, name, 0, 0, ITEM_TEXTSTYLE_SHADOWED, FONT_SMALL );
		if ( SC_Clicked( tx, ty, 60, 66 ) && k != sc.cur ) {
			sc.cur = k;
			sc.look = 0;
			SC_ApplyBody();
		}
	}
	y += 142;

	// --- its look ------------------------------------------------------------------
	if ( sc.cur >= 0 ) {
		const scChar_t *c = &sc.chars[sc.cur];
		SC_Text( x0, y + 1, 0.45f, white, "LOOK" );
		d = SC_Spinner( x0 + 70, y, 245, va( "%s  (%i/%i)", c->looks[sc.look], sc.look + 1, c->numLooks ), qfalse );
		if ( d && c->numLooks > 1 ) {
			sc.look = ( sc.look + d + c->numLooks ) % c->numLooks;
			SC_ApplyBody();
		}
	}
	y += 24;

	// --- accessories --------------------------------------------------------------
	SC_Text( x0, y, 0.5f, white, "ACCESSORIES" );
	y += 18;
	for ( i = 0; i < SHINOBI_ACC_MAX; i++, y += 21 ) {
		const int k = sc.acc[i].name[0] ? SC_AccIndex( sc.acc[i].name ) : -1;
		SC_Text( x0, y + 1, 0.45f, i == sc.slot ? title : dim, va( "%i%s", i + 1, i == sc.slot ? " <" : "" ) );
		d = SC_Spinner( x0 + 70, y, 245, SC_AccTitle( sc.acc[i].name ), i == sc.slot );
		if ( SC_Clicked( x0, y, 315, 18 ) )
			sc.slot = i;
		if ( d ) {
			int idx = k + d; // -1 = none
			if ( idx < -1 )
				idx = sc.accCount - 1;
			else if ( idx >= sc.accCount )
				idx = -1;
			Shinobi_AccDefault( &sc.acc[i] );
			if ( idx >= 0 )
				Q_strncpyz( sc.acc[i].name, sc.accList[idx], sizeof( sc.acc[i].name ) );
			sc.slot = i;
			SC_ApplyAcc();
		}
	}

	// --- the selected accessory's fit -----------------------------------------------
	y += 4;
	if ( sc.acc[sc.slot].name[0] ) {
		shinobiAcc_t *a = &sc.acc[sc.slot];
		SC_Slider( x0, y, "size %", &a->scale, SHINOBI_ACC_SCALE_MIN, SHINOBI_ACC_SCALE_MAX, "%.0f", 1, qtrue ); y += 15;
		SC_Slider( x0, y, "forward", &a->ofs[0], -SHINOBI_ACC_OFS_MAX, SHINOBI_ACC_OFS_MAX, "%.1f", 10, qtrue ); y += 15;
		SC_Slider( x0, y, "left", &a->ofs[1], -SHINOBI_ACC_OFS_MAX, SHINOBI_ACC_OFS_MAX, "%.1f", 10, qtrue ); y += 15;
		SC_Slider( x0, y, "up", &a->ofs[2], -SHINOBI_ACC_OFS_MAX, SHINOBI_ACC_OFS_MAX, "%.1f", 10, qtrue ); y += 15;
		SC_Slider( x0, y, "tilt fwd", &a->ang[0], -90, 90, "%.0f", 1, qtrue ); y += 15;
		SC_Slider( x0, y, "turn", &a->ang[1], -180, 180, "%.0f", 1, qtrue ); y += 15;
		SC_Slider( x0, y, "tilt side", &a->ang[2], -90, 90, "%.0f", 1, qtrue );
	}
	else {
		SC_Text( x0, y + 2, 0.4f, dim, va( "slot %i is empty - pick something with < >", sc.slot + 1 ) );
	}

	// --- buttons -------------------------------------------------------------------
	y = 448;
	if ( SC_Button( 20, y, 160, 24, "Back to the build", qfalse ) )
		back_ = 1;
	if ( SC_Button( 460, y, 150, 24, "Take all off", qfalse ) ) {
		for ( i = 0; i < SHINOBI_ACC_MAX; i++ )
			Shinobi_AccDefault( &sc.acc[i] );
		SC_ApplyAcc();
	}
	if ( sc.acc[sc.slot].name[0] && SC_Button( 300, y, 150, 24, "Reset its fit", qfalse ) ) {
		char name[32];
		Q_strncpyz( name, sc.acc[sc.slot].name, sizeof( name ) );
		Shinobi_AccDefault( &sc.acc[sc.slot] );
		Q_strncpyz( sc.acc[sc.slot].name, name, sizeof( sc.acc[sc.slot].name ) );
		SC_ApplyAcc();
	}

	sc.click = qfalse; // a click on nothing is dropped
	return back_ ? qtrue : qfalse;
}
