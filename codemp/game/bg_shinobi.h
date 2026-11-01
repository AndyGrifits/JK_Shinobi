/*
===========================================================================
JK_Shinobi - shared (game / cgame / bg) definitions.

State that both client prediction and remote clients need is carried in the
mod-reserved playerState/entityState fields. The engine networks each of
those as a single bit by default, so the three bits are combined:

  userInt2 userInt3   hand jutsu state
     0        0       none
     0        1       forming the ball / lightning
     1        0       armed (held in the left hand)
     1        1       dash strike in progress

  userInt1 alone (no hand jutsu)   chakra charging
  userInt1 with a hand jutsu       the hand jutsu is Chidori (else Rasengan)

Timers live server-side in gclient_t; only these bits are sent.
===========================================================================
*/
#pragma once

/*
---------------------------------------------------------------------------
Jutsu levels (hold-to-charge)

Holding a jutsu key fills three bars in turn; releasing casts the jutsu at
the level of the last full bar (a tap casts level 1). Each level costs more
chakra and makes the jutsu bigger. The client draws the bars from its own
key timing; the server times the hold itself and decides the level.
---------------------------------------------------------------------------
*/
typedef enum {
	SJ_NONE,
	SJ_RASENGAN,
	SJ_CHIDORI,
	SJ_KATON,
	SJ_DOTON,
	SJ_BUNSHIN,
	SJ_COUNT
} shinobiJutsu_t;

#define SHINOBI_MAX_LEVEL          3
#define SHINOBI_BAR1_MS            350  // bar 1 full (level 1 is also what a tap gives)
#define SHINOBI_BAR2_MS            850  // bar 2 full -> level 2
#define SHINOBI_BAR3_MS            1450 // bar 3 full -> level 3

// console command names (also the "+name"/"-name" hold commands on the client)
static const char * const shinobiJutsuNames[SJ_COUNT] = {
	"", "rasengan", "chidori", "katon", "doton", "kagebunshin"
};

// chakra cost per level (index 0 = level 1)
static const int shinobiJutsuCost[SJ_COUNT][SHINOBI_MAX_LEVEL] = {
	{  0,  0,  0 },
	{ 25, 40, 60 }, // Rasengan
	{ 25, 40, 60 }, // Chidori
	{ 15, 30, 50 }, // Katon
	{ 15, 30, 50 }, // Doton
	{ 25, 45, 70 }, // Kage Bunshin
};

static QINLINE int Shinobi_ChargeLevel( int heldMs ) {
	if ( heldMs >= SHINOBI_BAR3_MS ) return 3;
	if ( heldMs >= SHINOBI_BAR2_MS ) return 2;
	return 1;
}

static QINLINE int Shinobi_JutsuCost( int jutsu, int level ) {
	if ( jutsu <= SJ_NONE || jutsu >= SJ_COUNT || level < 1 || level > SHINOBI_MAX_LEVEL )
		return 0;
	return shinobiJutsuCost[jutsu][level - 1];
}

static QINLINE int Shinobi_JutsuForName( const char *name ) {
	int i;
	for ( i = SJ_NONE + 1; i < SJ_COUNT; i++ ) {
		if ( !Q_stricmp( name, shinobiJutsuNames[i] ) )
			return i;
	}
	return SJ_NONE;
}

/*
The hand jutsu level (1-3) travels in userFloat1/userFloat2, which the
engine sends as single raw bits: we store the integer 0/1 bit pattern in
them (never use them as floats).
*/
static QINLINE int Shinobi_RawBit( const float *f ) {
	int i;
	memcpy( &i, f, sizeof( i ) );
	return i & 1;
}

static QINLINE void Shinobi_SetRawBit( float *f, int on ) {
	int i = on ? 1 : 0;
	memcpy( f, &i, sizeof( i ) );
}

#define SHINOBI_HAND_LEVEL(s)      ( 1 + Shinobi_RawBit( &(s)->userFloat1 ) + 2 * Shinobi_RawBit( &(s)->userFloat2 ) )
#define SHINOBI_SET_HAND_LEVEL(s, lvl) \
	( Shinobi_SetRawBit( &(s)->userFloat1, ((lvl) - 1) & 1 ), Shinobi_SetRawBit( &(s)->userFloat2, (((lvl) - 1) >> 1) & 1 ) )

#define SHINOBI_HAND_ANY(s)        ((s)->userInt2 || (s)->userInt3)
#define SHINOBI_HAND_FORMING(s)    (!(s)->userInt2 && (s)->userInt3)
#define SHINOBI_HAND_ARMED(s)      ((s)->userInt2 && !(s)->userInt3)
#define SHINOBI_HAND_DASH(s)       ((s)->userInt2 && (s)->userInt3)
#define SHINOBI_HAND_IS_CHIDORI(s) ((s)->userInt1 && SHINOBI_HAND_ANY(s))
#define SHINOBI_CHARGING(s)        ((s)->userInt1 && !SHINOBI_HAND_ANY(s))

// Chakra (the force pool, fd.forcePower)
#define SHINOBI_CHARGE_TICK_MS     50   // chakra gained every tick while charging
#define SHINOBI_CHARGE_PER_TICK    2    // = 40 chakra per second

// Hand jutsu (Rasengan / Chidori): form, hold, then dash-strike with attack
#define SHINOBI_HAND_REACH         44   // hit box distance in front of the user
#define SHINOBI_HAND_HITBOX        28   // half-size of the hit box

// per-level tables are indexed [level - 1]
#define SHINOBI_HAND_FORM_MS       300  // after release the ball / lightning settles in the hand

#define SHINOBI_RASENGAN_HOLD_MS   6000
#define SHINOBI_RASENGAN_DASH_SPEED 700
static const int shinobiRasenganDamage[3] = {  40,  60,   90 };
static const int shinobiRasenganThrow[3]  = { 600, 900, 1300 }; // knockback speed
static const int shinobiRasenganDashMs[3] = { 300, 350,  400 };
static const float shinobiRasenganRadius[3] = { 5.0f, 8.0f, 12.0f }; // ball size in the hand

#define SHINOBI_CHIDORI_HOLD_MS    5000
#define SHINOBI_CHIDORI_DASH_SPEED 1150 // faster and longer than Rasengan...
static const int shinobiChidoriDamage[3]  = {  50,  75,  105 }; // ...hits harder...
static const int shinobiChidoriThrow[3]   = { 200, 250,  320 }; // ...barely pushes: it pierces
static const int shinobiChidoriStunMs[3]  = { 600, 900, 1300 }; // electrocuted
static const int shinobiChidoriDashMs[3]  = { 330, 430,  540 }; // 380 / 495 / 620 units
static const float shinobiChidoriRadius[3] = { 6.0f, 9.0f, 13.0f };

// Shunshin (body flicker): short teleport along the view direction
#define SHINOBI_SHUNSHIN_COST      15
#define SHINOBI_SHUNSHIN_DIST      320
#define SHINOBI_SHUNSHIN_MIN_DIST  48   // don't bother if a wall is right in front
#define SHINOBI_SHUNSHIN_COOLDOWN  600
#define SHINOBI_SHUNSHIN_MAX_PITCH 35   // degrees up/down the blink may follow the view

// Kage Bunshin (shadow clones): NPC copies with Jedi AI, one hit kills them
// number of clones = level (1-3); each clone drains the owner's chakra
#define SHINOBI_CLONE_MAX          3
#define SHINOBI_CLONE_DRAIN_MS     333   // 1 chakra per clone every 333 ms = 3/s per clone
#define SHINOBI_CLONE_LIFETIME     25000
#define SHINOBI_CLONE_HEALTH       1
#define SHINOBI_CLONE_SIGHT        1500  // enemy search radius
#define SHINOBI_CLONE_THINK_MS     250   // how often clone targeting is supervised
#define SHINOBI_CLONE_NPC_TYPE     "shinobi_clone" // ext_data/NPCs/shinobi.npc

// Katon: Goukakyuu (great fireball): explosive projectile
#define SHINOBI_FIREBALL_COOLDOWN  900
#define SHINOBI_FIREBALL_LIFE      2500
static const int   shinobiFireballSpeed[3]  = { 900, 820, 740 };   // bigger balls fly slower
static const int   shinobiFireballSize[3]   = {  10,  16,  24 };   // collision half-size
static const int   shinobiFireballDamage[3] = {  25,  45,  70 };   // direct hit, on top of the blast
static const int   shinobiFireballSplash[3] = {  40,  65, 100 };
static const int   shinobiFireballRadius[3] = { 130, 180, 250 };
static const float shinobiFireballVisual[3] = { 14.0f, 22.0f, 34.0f }; // drawn ball radius

// Doton: Doryuheki (earth wall): row of stone pillars rising from the ground
#define SHINOBI_WALL_COOLDOWN      3000
#define SHINOBI_WALL_MAX_PILLARS   7
static const int shinobiWallPillars[3]   = {   3,   5,   7 };      // width
static const int shinobiWallHealth[3]    = { 250, 450, 700 };      // shared by the whole wall
#define SHINOBI_WALL_DISTANCE      96   // in front of the caster
#define SHINOBI_WALL_SPACING       34
#define SHINOBI_WALL_HALF_WIDTH    18   // pillar collision half-size
#define SHINOBI_WALL_HEIGHT        104
#define SHINOBI_WALL_RISE_MS       400
#define SHINOBI_WALL_LIFETIME      12000
#define SHINOBI_WALL_SINK_MS       600
#define SHINOBI_WALL_MODELS        3    // models/shinobi/earth_pillar1..3.md3

// Non-player entities drawn by the cgame (entityState.generic1 on ET_GENERAL)
#define SHINOBI_ENT_FIREBALL       201  // 201..203 = fireball level 1..3

// Effect files (inside zz_shinobi.pk3, under effects/)
#define SHINOBI_FX_RASENGAN_HAND   "shinobi/rasengan_hand"
#define SHINOBI_FX_RASENGAN_FORM   "shinobi/rasengan_form"
#define SHINOBI_FX_RASENGAN_HIT    "shinobi/rasengan_hit"       // + "_1".."_3" per level
#define SHINOBI_FX_RASENGAN_FIZZLE "shinobi/rasengan_fizzle"
#define SHINOBI_FX_CHIDORI_HAND    "shinobi/chidori_hand"
#define SHINOBI_FX_CHIDORI_FORM    "shinobi/chidori_form"
#define SHINOBI_FX_CHIDORI_HIT     "shinobi/chidori_hit"        // + "_1".."_3" per level
#define SHINOBI_FX_CHIDORI_FIZZLE  "shinobi/chidori_fizzle"
#define SHINOBI_FX_CHAKRA_AURA     "shinobi/chakra_aura"
#define SHINOBI_FX_SHUNSHIN        "shinobi/shunshin"
#define SHINOBI_FX_CLONE_POOF      "shinobi/clone_poof"
#define SHINOBI_FX_FIREBALL_BOOM   "shinobi/fireball_explode"   // + "_1".."_3" per level
#define SHINOBI_FX_FIREBALL_TRAIL  "shinobi/fireball_trail"
#define SHINOBI_FX_CLONE_SMOKE     "shinobi/clone_smoke"
#define SHINOBI_FX_WALL_RISE       "shinobi/earthwall_rise"
#define SHINOBI_FX_WALL_BREAK      "shinobi/earthwall_break"
