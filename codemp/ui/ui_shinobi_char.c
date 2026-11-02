/*
===========================================================================
JK_Shinobi - the character screen (Appearance on the build screen).

Instead of the stock profile menu: a base (Human Male / Human Female, the
customisable jedi_hm / jedi_hf with their head / torso / legs skins), the
cloth colour, and up to SHINOBI_ACC_MAX of our accessories, each with its own
scale, offset and turn. A live preview of the whole character, turned by
dragging it. Every change goes out at once through the "model",
"char_color_*", "sex" and "shinobi_acc" userinfo, so everyone sees it.
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

#define SC_MAX_SKINS 48
#define SC_MAX_ACC   64

static const char * const scBases[2]      = { "jedi_hm", "jedi_hf" };
static const char * const scBaseTitles[2] = { "Human Male", "Human Female" };

typedef struct {
	char head[SC_MAX_SKINS][32], torso[SC_MAX_SKINS][32], legs[SC_MAX_SKINS][32];
	int  numHead, numTorso, numLegs;
} scParts_t;

static struct {
	qboolean     ready;
	scParts_t    parts[2];
	int          base, head, torso, legs; // base 0/1; the rest index parts[base]
	int          color[3];
	shinobiAcc_t acc[SHINOBI_ACC_MAX];      // name "" = empty slot
	int          slot;                       // slot being edited
	char         accList[SC_MAX_ACC][32];
	qhandle_t    accModel[SC_MAX_ACC];
	int          accCount;

	void        *g2;
	int          g2Base;                     // the base g2 was made for (-1 none)
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
static void SC_ListSkins( int base ) {
	static char list[16384];
	scParts_t  *p = &sc.parts[base];
	char       *f;
	int         n, i;

	memset( p, 0, sizeof( *p ) );
	n = trap->FS_GetFileList( va( "models/players/%s", scBases[base] ), ".skin", list, sizeof( list ) );
	for ( i = 0, f = list; i < n; i++, f += strlen( f ) + 1 ) {
		char  name[32], *dot;
		Q_strncpyz( name, f, sizeof( name ) );
		if ( ( dot = strrchr( name, '.' ) ) != NULL )
			*dot = 0;
		if ( !Q_stricmpn( name, "head_", 5 ) && p->numHead < SC_MAX_SKINS )
			Q_strncpyz( p->head[p->numHead++], name, 32 );
		else if ( !Q_stricmpn( name, "torso_", 6 ) && p->numTorso < SC_MAX_SKINS )
			Q_strncpyz( p->torso[p->numTorso++], name, 32 );
		else if ( !Q_stricmpn( name, "lower_", 6 ) && p->numLegs < SC_MAX_SKINS )
			Q_strncpyz( p->legs[p->numLegs++], name, 32 );
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

static int SC_Find( char list[][32], int n, const char *name ) {
	int i;
	for ( i = 0; i < n; i++ ) {
		if ( !Q_stricmp( list[i], name ) )
			return i;
	}
	return 0;
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
	char model[MAX_QPATH], buf[256], *slash, *p1, *p2;
	int  i;

	trap->Cvar_VariableStringBuffer( "model", model, sizeof( model ) );
	sc.base = 0;
	sc.head = sc.torso = sc.legs = 0;
	slash = strchr( model, '/' );
	if ( slash ) {
		*slash = 0;
		for ( i = 0; i < 2; i++ ) {
			if ( !Q_stricmp( model, scBases[i] ) )
				sc.base = i;
		}
		p1 = strchr( slash + 1, '|' );
		p2 = p1 ? strchr( p1 + 1, '|' ) : NULL;
		if ( p1 && p2 && !Q_stricmp( model, scBases[sc.base] ) ) {
			*p1 = *p2 = 0;
			sc.head = SC_Find( sc.parts[sc.base].head, sc.parts[sc.base].numHead, slash[1] == '|' ? slash + 2 : slash + 1 );
			sc.torso = SC_Find( sc.parts[sc.base].torso, sc.parts[sc.base].numTorso, p1 + 1 );
			sc.legs = SC_Find( sc.parts[sc.base].legs, sc.parts[sc.base].numLegs, p2 + 1 );
		}
	}
	sc.color[0] = (int)trap->Cvar_VariableValue( "char_color_red" );
	sc.color[1] = (int)trap->Cvar_VariableValue( "char_color_green" );
	sc.color[2] = (int)trap->Cvar_VariableValue( "char_color_blue" );
	if ( !sc.color[0] && !sc.color[1] && !sc.color[2] )
		sc.color[0] = sc.color[1] = sc.color[2] = 255;

	memset( sc.acc, 0, sizeof( sc.acc ) );
	for ( i = 0; i < SHINOBI_ACC_MAX; i++ )
		Shinobi_AccDefault( &sc.acc[i] );
	trap->Cvar_VariableStringBuffer( "shinobi_acc", buf, sizeof( buf ) );
	Shinobi_AccParse( buf, sc.acc, SHINOBI_ACC_MAX );
}

static const char *SC_PartTitle( int kind, int i ) {
	const scParts_t *p = &sc.parts[sc.base];
	const char      *n = kind == 0 ? ( p->numHead ? p->head[i] : "-" )
		: kind == 1 ? ( p->numTorso ? p->torso[i] : "-" ) : ( p->numLegs ? p->legs[i] : "-" );
	const char      *u = strchr( n, '_' );
	return u ? u + 1 : n; // "head_a1" -> "a1"
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
	const scParts_t *p = &sc.parts[sc.base];
	if ( !p->numHead || !p->numTorso || !p->numLegs )
		return;
	trap->Cvar_Set( "model", va( "%s/%s|%s|%s", scBases[sc.base], p->head[sc.head], p->torso[sc.torso], p->legs[sc.legs] ) );
	trap->Cvar_Set( "sex", sc.base == 1 ? "female" : "male" );
}

static void SC_ApplyColor( void ) {
	trap->Cvar_Set( "char_color_red", va( "%i", sc.color[0] ) );
	trap->Cvar_Set( "char_color_green", va( "%i", sc.color[1] ) );
	trap->Cvar_Set( "char_color_blue", va( "%i", sc.color[2] ) );
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
	SC_ListSkins( 0 );
	SC_ListSkins( 1 );
	SC_ListAccessories();
	SC_ReadCvars();
	sc.slot = 0;
	sc.click = sc.mouseDown = sc.rotating = qfalse;
	sc.drag = NULL;
	sc.g2Base = -1;
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
	const scParts_t *p = &sc.parts[sc.base];
	char             skinName[MAX_QPATH];

	if ( sc.g2Base != sc.base ) {
		char gla[MAX_QPATH], *slash;

		if ( sc.g2 ) {
			UI_ClearG2Pointer( sc.g2 );
			trap->G2API_CleanGhoul2Models( &sc.g2 );
			sc.g2 = NULL;
		}
		sc.g2Base = sc.base;
		sc.skinName[0] = 0;
		if ( trap->G2API_InitGhoul2Model( &sc.g2, va( "models/players/%s/model.glm", scBases[sc.base] ), 0, 0, 0, 0, 0 ) < 0 || !sc.g2 ) {
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
	if ( !sc.g2 || !p->numHead || !p->numTorso || !p->numLegs )
		return;
	Com_sprintf( skinName, sizeof( skinName ), "models/players/%s/|%s|%s|%s", scBases[sc.base],
		p->head[sc.head], p->torso[sc.torso], p->legs[sc.legs] );
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
	ent.shaderRGBA[0] = sc.color[0];
	ent.shaderRGBA[1] = sc.color[1];
	ent.shaderRGBA[2] = sc.color[2];
	ent.shaderRGBA[3] = 255;
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
				else
					SC_ApplyColor();
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
	const scParts_t *p;
	float        x0 = 300, y;
	int          i, d, back_ = 0;
	static const char * const kinds[3] = { "HEAD", "TORSO", "LEGS" };

	if ( !sc.ready )
		UI_ShinobiCharOpen();
	p = &sc.parts[sc.base];
	SC_UpdateDrag();

	UI_FillRect( 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, back );
	SC_Text( 20, 8, 0.8f, title, "CHARACTER" );
	SC_Text( 230, 16, 0.4f, dim, "drag the model to turn it - everything shows at once, to everyone" );

	// --- preview ------------------------------------------------------------
	UI_FillRect( 20, 40, 260, 400, box );
	_UI_DrawRect( 20, 40, 260, 400, 1, frame );
	SC_DrawPreview( 21, 41, 258, 398 );
	if ( sc.click && SC_Over( 20, 40, 260, 400 ) && sc.clickX >= 20 && sc.clickX < 280 && sc.clickY >= 40 && sc.clickY < 440 ) {
		sc.click = qfalse;
		sc.rotating = qtrue;
		sc.rotX = SC_CX;
	}

	// --- body ---------------------------------------------------------------
	y = 44;
	SC_Text( x0, y + 2, 0.5f, white, "BODY" );
	for ( i = 0; i < 2; i++ ) {
		if ( SC_Button( x0 + 70 + i * 125, y, 120, 20, scBaseTitles[i], sc.base == i ) && sc.base != i ) {
			sc.base = i;
			sc.head = sc.torso = sc.legs = 0;
			SC_ApplyBody();
		}
	}
	y += 28;
	for ( i = 0; i < 3; i++, y += 22 ) {
		const int n = i == 0 ? p->numHead : i == 1 ? p->numTorso : p->numLegs;
		int      *cur = i == 0 ? &sc.head : i == 1 ? &sc.torso : &sc.legs;
		SC_Text( x0, y + 1, 0.45f, white, kinds[i] );
		d = SC_Spinner( x0 + 70, y, 245, n ? va( "%s  (%i/%i)", SC_PartTitle( i, *cur ), *cur + 1, n ) : "-", qfalse );
		if ( d && n ) {
			*cur = ( *cur + d + n ) % n;
			SC_ApplyBody();
		}
	}

	// --- colour ---------------------------------------------------------------
	y += 6;
	{
		vec4_t sw;
		SC_Text( x0, y + 1, 0.5f, white, "COLOUR" );
		VectorSet( sw, sc.color[0] / 255.0f, sc.color[1] / 255.0f, sc.color[2] / 255.0f );
		sw[3] = 1;
		UI_FillRect( x0 + 70, y + 2, 30, 12, sw );
		_UI_DrawRect( x0 + 70, y + 2, 30, 12, 1, frame );
	}
	y += 18;
	SC_Slider( x0, y, "red", &sc.color[0], 0, 255, "%.0f", 1, qfalse );  y += 15;
	SC_Slider( x0, y, "green", &sc.color[1], 0, 255, "%.0f", 1, qfalse ); y += 15;
	SC_Slider( x0, y, "blue", &sc.color[2], 0, 255, "%.0f", 1, qfalse );  y += 22;

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
