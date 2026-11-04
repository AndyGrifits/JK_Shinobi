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

  userFloat1/2 (raw bits)          hand jutsu level - 1
  userFloat3   (raw bit)           Kawarimi: hidden (slow, invisible to others)
  userVec1[0..2] (raw bits)        burning, wet, stuck in mud
  userVec2[0..2] (raw bits)        trapped in a Water Prison, holding one,
                                   stunned right after one

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
	SJ_SHUNSHIN,  // Shift + jump (charged by holding jump), not in the selection list
	SJ_KAWARIMI,  // instant, no levels
	SJ_SUITON,    // water bullet
	SJ_FUTON,     // wind gust
	SJ_SUIRO,     // water prison
	SJ_WAVE,      // water wave
	SJ_BLADE,     // wind blade
	SJ_FLAME,     // fire breath (held)
	SJ_ASH,       // ash cloud
	SJ_SHADOW,    // shadow bind (held)
	SJ_WATERCLONE,// water clone: casts the next jutsu of your build on its own
	SJ_HIRAISHIN, // flying thunder god: teleport to your marked kunai
	SJ_RAITRAP,   // lightning trap: two or three marked kunai string a lightning net
	SJ_MIST,      // hidden mist: a bank of fog around you
	SJ_QUAKE,     // earthquake: long charge, the radius grows with it
	SJ_WINDWALL,  // wind wall: throws projectiles back at their thrower
	SJ_SKYSTEP,   // sky step: passive, jumps and wall runs cost half, no falling damage
	SJ_WINDEDGE,  // wind edge: 15 s, strikes throw air slashes ahead and cut through guards
	SJ_RAIEDGE,   // lightning edge: 15 s, +20% taijutsu, x3 against stone
	SJ_BURROW,    // burrow (held): travel under the ground, burst out under the enemy
	SJ_PILLAR,    // rock pillar: erupts where you look, launching whoever stands there
	SJ_ARMOR,     // stone skin: 8 s, less damage, no knockdowns, slower
	SJ_EARTHCLONE,// earth clone: a slow, tough clone that hits hard
	SJ_RAICLONE,  // lightning clone: fights; when struck down it discharges, paralysing
	SJ_RAIDASH,   // lightning dash (held): a bolt of lightning racing ahead
	SJ_SHADOWHIDE,// shadow hide (held): melt into your shadow
	SJ_FLASH,     // flash: blinds whoever looks at you, light chases out what hides
	SJ_HEAL,      // healing: chakra into health
	SJ_SHADOWTRAP,// shadow trap: shadows reach out from your marked kunai and hold whoever they catch
	SJ_KAITEN,    // kaiten (held): spin inside a dome of chakra - nothing gets through, the close ones are thrown off
	SJ_COUNT
} shinobiJutsu_t;

#define SHINOBI_MAX_LEVEL          3
#define SHINOBI_BAR1_MS            350  // bar 1 full (level 1 is also what a tap gives)
#define SHINOBI_BAR2_MS            850  // bar 2 full -> level 2
#define SHINOBI_BAR3_MS            1450 // bar 3 full -> level 3

// console command names (also the "+name"/"-name" hold commands on the client)
static const char * const shinobiJutsuNames[SJ_COUNT] = {
	"", "rasengan", "chidori", "katon", "doton", "kagebunshin", "shunshin", "kawarimi",
	"suiton", "futon", "suiro", "wave", "windblade", "flame", "ash", "kagemane", "mizubunshin", "hiraishin", "raitrap", "kirigakure", "jishin", "kazekabe", "tenpo", "kazeyaiba", "raiyaiba", "moguragakure", "ganchu", "tsuchiyoroi", "tsuchibunshin", "raibunshin", "raido", "kagegakure", "senko", "shosen", "kagewana", "kaiten"
};

// names shown in the HUD
static const char * const shinobiJutsuTitles[SJ_COUNT] = {
	"", "Rasengan", "Chidori", "Katon", "Doton", "Kage Bunshin", "Shunshin", "Kawarimi",
	"Water Bullets", "Storm Wind", "Water Prison", "Water Wave", "Wind Blade", "Fire Breath", "Ash Cloud", "Shadow Bind", "Water Clone", "Hiraishin", "Lightning Trap", "Hidden Mist", "Earthquake", "Wind Wall", "Sky Step", "Wind Edge", "Lightning Edge", "Burrow", "Rock Pillar", "Stone Skin", "Earth Clone", "Lightning Clone", "Lightning Dash", "Shadow Hide", "Flash", "Healing", "Shadow Trap", "Kaiten"
};

// the jutsu the selection keys cycle through (Shunshin and Kawarimi have their own keys)
static const int shinobiJutsuCycle[] = {
	SJ_KATON, SJ_FLAME, SJ_ASH, SJ_SUITON, SJ_WAVE, SJ_SUIRO, SJ_WATERCLONE, SJ_MIST, SJ_FUTON, SJ_BLADE, SJ_WINDWALL, SJ_WINDEDGE, SJ_SKYSTEP, SJ_DOTON, SJ_QUAKE, SJ_PILLAR, SJ_BURROW, SJ_ARMOR, SJ_EARTHCLONE, SJ_CHIDORI, SJ_RAIEDGE, SJ_RAIDASH, SJ_RAICLONE,
	SJ_RAITRAP, SJ_RASENGAN, SJ_BUNSHIN, SJ_SHADOW, SJ_SHADOWTRAP, SJ_HIRAISHIN, SJ_SHADOWHIDE, SJ_FLASH, SJ_HEAL, SJ_KAITEN
};
#define SHINOBI_CYCLE_COUNT ( (int)( sizeof( shinobiJutsuCycle ) / sizeof( shinobiJutsuCycle[0] ) ) )

// chakra cost per level (index 0 = level 1)
static const int shinobiJutsuCost[SJ_COUNT][SHINOBI_MAX_LEVEL] = {
	{  0,  0,  0 },
	{ 25, 40, 60 }, // Rasengan
	{ 25, 40, 60 }, // Chidori
	{ 15, 30, 50 }, // Katon
	{ 15, 30, 50 }, // Doton
	{ 25, 45, 70 }, // Kage Bunshin
	{ 10, 15, 20 }, // Shunshin (charges limit it now)
	{ 35, 35, 35 }, // Kawarimi
	{ 10, 18, 26 }, // Water Bullets (1 / 2 / 3 bullets)
	{ 15, 25, 40 }, // Storm Wind
	{ 20, 20, 20 }, // Water Prison (to start; then it drains while held)
	{ 20, 35, 50 }, // Water Wave
	{ 15, 25, 40 }, // Wind Blade
	{ 10, 10, 10 }, // Fire Breath (to start; then it drains while held)
	{ 20, 30, 40 }, // Ash Cloud
	{ 15, 15, 15 }, // Shadow Bind (to start; then it drains while held)
	{ 25, 25, 25 }, // Water Clone (its own jutsu are paid by you too)
	{ 25, 25, 25 }, // Hiraishin
	{ 20, 20, 20 }, // Lightning Trap
	{ 20, 30, 40 }, // Hidden Mist
	{ 20, 35, 55 }, // Earthquake
	{ 15, 25, 35 }, // Wind Wall
	{  0,  0,  0 }, // Sky Step (passive)
	{ 20, 20, 20 }, // Wind Edge
	{ 20, 20, 20 }, // Lightning Edge
	{ 15, 15, 15 }, // Burrow (to start; then it drains while held)
	{ 20, 30, 40 }, // Rock Pillar
	{ 25, 25, 25 }, // Stone Skin
	{ 30, 30, 30 }, // Earth Clone
	{ 30, 30, 30 }, // Lightning Clone
	{ 15, 15, 15 }, // Lightning Dash (held: + 40/s)
	{ 15, 15, 15 }, // Shadow Hide (to start; then it drains while held)
	{ 25, 25, 25 }, // Flash
	{  5,  5,  5 }, // Healing (to start; then held, chakra into health one for one)
	{ 20, 20, 20 }, // Shadow Trap (to start; then each caught enemy drains it)
	{ 10, 10, 10 }, // Kaiten (to start; then it burns chakra fast while held)
};

// jutsu that cast on key press, without charging
#define SHINOBI_JUTSU_INSTANT(j)   ( (j) == SJ_KAWARIMI || (j) == SJ_WATERCLONE || (j) == SJ_HIRAISHIN || (j) == SJ_RAITRAP || (j) == SJ_SHADOWTRAP \
	|| (j) == SJ_WINDEDGE || (j) == SJ_RAIEDGE || (j) == SJ_ARMOR || (j) == SJ_EARTHCLONE || (j) == SJ_RAICLONE || (j) == SJ_FLASH )
// passive techniques: in the build, never cast (skipped by the selection keys)
#define SHINOBI_JUTSU_PASSIVE(j)   ( (j) == SJ_SKYSTEP )
// jutsu that last while the key is held, without levels
#define SHINOBI_JUTSU_HOLD(j)      ( (j) == SJ_SUIRO || (j) == SJ_FLAME || (j) == SJ_SHADOW || (j) == SJ_BURROW || (j) == SJ_SHADOWHIDE || (j) == SJ_RAIDASH || (j) == SJ_HEAL || (j) == SJ_KAITEN )
// the lowest level a jutsu can be cast at (the great fireball needs a long charge)
#define SHINOBI_JUTSU_MIN_LEVEL(j) ( (j) == SJ_KATON ? 2 : 1 )

// when each bar is full; Shunshin charges faster than the big jutsu
static QINLINE int Shinobi_BarEnd( int jutsu, int bar ) {
	static const int normal[SHINOBI_MAX_LEVEL]   = { SHINOBI_BAR1_MS, SHINOBI_BAR2_MS, SHINOBI_BAR3_MS };
	static const int shunshin[SHINOBI_MAX_LEVEL] = { 200, 500, 900 };
	static const int quake[SHINOBI_MAX_LEVEL]    = { 700, 1700, 3000 }; // the earthquake takes its time
	if ( bar < 0 ) bar = 0;
	if ( bar >= SHINOBI_MAX_LEVEL ) bar = SHINOBI_MAX_LEVEL - 1;
	if ( jutsu == SJ_QUAKE )
		return quake[bar];
	return jutsu == SJ_SHUNSHIN ? shunshin[bar] : normal[bar];
}

static QINLINE int Shinobi_ChargeLevelFor( int jutsu, int heldMs ) {
	if ( heldMs >= Shinobi_BarEnd( jutsu, 2 ) ) return 3;
	if ( heldMs >= Shinobi_BarEnd( jutsu, 1 ) ) return 2;
	return 1;
}

static QINLINE int Shinobi_ChargeLevel( int heldMs ) {
	return Shinobi_ChargeLevelFor( SJ_NONE, heldMs );
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
#define SHINOBI_HIDDEN(s)          ( Shinobi_RawBit( &(s)->userFloat3 ) )
#define SHINOBI_BURNING(s)         ( Shinobi_RawBit( &(s)->userVec1[0] ) )
#define SHINOBI_WET(s)             ( Shinobi_RawBit( &(s)->userVec1[1] ) )
#define SHINOBI_MUDDY(s)           ( Shinobi_RawBit( &(s)->userVec1[2] ) )
#define SHINOBI_IMPRISONED(s)      ( Shinobi_RawBit( &(s)->userVec2[0] ) )
#define SHINOBI_PRISON_CASTER(s)   ( Shinobi_RawBit( &(s)->userVec2[1] ) )
#define SHINOBI_STUNNED(s)         ( Shinobi_RawBit( &(s)->userVec2[2] ) ) // just out of a Water Prison
#define SHINOBI_SET_HIDDEN(s, on)  Shinobi_SetRawBit( &(s)->userFloat3, (on) )

// Shift is the chakra modifier (it no longer walks): Shift + jump = Shunshin.
// The engine reports a held Shift as BUTTON_WALKING (with cl_run 1).
#define SHINOBI_MODIFIER(cmd)      ( ( (cmd)->buttons & BUTTON_WALKING ) != 0 )

// Wall run: also from the air, from a little further away, and kept going
// while holding forward (paid with chakra)
#define SHINOBI_WALLRUN_REACH      40   // how far to the side a wall may be
#define SHINOBI_WALLRUN_EXTEND_COST 6   // chakra per extension (~every 0.5 s)
#define SHINOBI_WALLRUN_COST(ps)   ( ( (ps)->stats[STAT_SHINOBI] & SHINOBI_STAT_SKYSTEP ) ? SHINOBI_WALLRUN_EXTEND_COST / 2 : SHINOBI_WALLRUN_EXTEND_COST )

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

// Shunshin (body flicker): Shift + jump teleports along the view (up and down
// too), Shift + back + jump the opposite way; holding jump charges the
// distance (about 4 / 10 / 18 metres)
static const int shinobiShunshinDist[3]  = { 160, 360, 640 };
#define SHINOBI_SHUNSHIN_MIN_DIST  24   // don't bother if a wall is right there
#define SHINOBI_SHUNSHIN_CHARGES   3    // blinks ready at once; each one comes back on its own
#define SHINOBI_SHUNSHIN_RECHARGE  6000

// Kawarimi (substitution): an AI double takes your place, you turn invisible
// and creep away; any action but moving ends it and the double becomes a log
#define SHINOBI_KAWARIMI_CHARGES   3    // substitutions ready at once (also from the ground); each comes back on its own
#define SHINOBI_KAWARIMI_RECHARGE  15000
#define SHINOBI_KAWARIMI_HOP       80   // you are put this far back, already hidden
#define SHINOBI_KAWARIMI_MAX_MS    12000 // hidden at most this long
#define SHINOBI_KAWARIMI_DECOY_MS  15000
#define SHINOBI_KAWARIMI_MOVE      40    // movement while hidden (127 = run, 46 = walk)
#define SHINOBI_LOG_LIFETIME       8000

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
// Cast at level 2 or 3 only (at least the second bar): a slow, dodgeable nuke.
// Level 1 exists for fireballs weakened by water.
static const int   shinobiFireballSpeed[3]  = { 900, 700, 580 };   // bigger balls fly slower
static const int   shinobiFireballSize[3]   = {  10,  16,  24 };   // collision half-size
static const int   shinobiFireballDamage[3] = {  25,  60,  95 };   // direct hit, on top of the blast
static const int   shinobiFireballSplash[3] = {  40,  80, 120 };
static const int   shinobiFireballRadius[3] = { 130, 190, 260 };
static const float shinobiFireballVisual[3] = { 14.0f, 22.0f, 34.0f }; // drawn ball radius

// Doton: Doryuheki (earth wall): row of stone pillars rising from the ground
#define SHINOBI_WALL_COOLDOWN      3000

// Earthquake: cracks race out to the radius, then everyone standing on the
// ground inside is knocked down (level 1: staggered). Jumping dodges it.
// Ignores the guard (it comes from below).
static const int shinobiQuakeRadius[3] = { 220, 340, 480 };
static const int shinobiQuakeWindup[3] = { 500, 600, 750 };  // cracks spreading before the strike
static const int shinobiQuakeDamage[3] = { 15, 30, 45 };
#define SHINOBI_QUAKE_COOLDOWN     5000
#define SHINOBI_QUAKE_LINGER       900   // dust after the strike

// Wind Wall: a standing wall of wind in front of the caster. Projectiles
// flying into its face are thrown back at whoever threw them (they become
// the caster's); a fireball grows a level on the way back.
static const int shinobiWindWallLife[3]  = { 4000, 6000, 8000 };
static const int shinobiWindWallHalfW[3] = { 80, 100, 120 };
#define SHINOBI_WINDWALL_DISTANCE  64
#define SHINOBI_WINDWALL_HEIGHT    140
#define SHINOBI_WINDWALL_DEPTH     28    // how thick the catching zone is
#define SHINOBI_WINDWALL_COOLDOWN  6000

// Burrow (held): under the ground, out of sight (Byakugan aside), immune to
// all but an Earthquake; a dust trail gives you away. Let go (or attack) to
// burst out: whoever stands right above is launched.
#define SHINOBI_BURROW_MAX_MS      6000
#define SHINOBI_BURROW_TICK_MS     200   // 1 chakra per tick
#define SHINOBI_BURROW_COOLDOWN    8000
#define SHINOBI_BURROW_SPEED       0.9f
#define SHINOBI_BURROW_RADIUS      96
#define SHINOBI_BURROW_DAMAGE      20
#define SHINOBI_BURROW_LAUNCH      420
// Rock Pillar: erupts where you look after a short rumble
static const int shinobiPillarRange[3]  = { 450, 550, 650 };
static const int shinobiPillarDamage[3] = { 20, 30, 40 };
static const int shinobiPillarLaunch[3] = { 420, 500, 580 };
#define SHINOBI_PILLAR_RADIUS      76    // launch area (the block's footprint)
#define SHINOBI_PILLAR_DELAY       350
#define SHINOBI_PILLAR_LIFE        90000 // a lasting platform...
#define SHINOBI_PILLAR_MAX         3     // ...but at most this many each: a 4th takes the oldest down
#define SHINOBI_PILLAR_COOLDOWN    4000
static const int shinobiPillarHealth[3] = { 50, 100, 170 };  // the charge: how tough...
// the stone blocks (axis-aligned: the model is the collision box)
#define SHINOBI_ROCK_HALF          54    // half width (both ways)
static const int shinobiRockHeight[3]   = { 72, 112, 160 };  // ...and how tall a pillar on the ground
#define SHINOBI_ROCK_LEDGE_HALF    16    // half thickness of a ledge out of a wall
#define SHINOBI_ROCK_RISE_MS       300
// Stone Skin
#define SHINOBI_ARMOR_MS           8000
#define SHINOBI_ARMOR_COOLDOWN     20000
#define SHINOBI_ARMOR_TAKEN        0.6f  // damage taken x
#define SHINOBI_ARMOR_SPEED        0.85f
// Earth Clone
#define SHINOBI_EARTHCLONE_LIFE    25000
#define SHINOBI_EARTHCLONE_HEALTH  150
#define SHINOBI_EARTHCLONE_SPEED   0.7f
#define SHINOBI_CLONE_MELEE        0.3f  // any clone's blows: this much of its owner's

// Lightning Clone: struck down, it discharges
#define SHINOBI_RAICLONE_LIFE      12000
#define SHINOBI_RAICLONE_TRIGGER   80    // this close to its enemy it goes off
#define SHINOBI_RAICLONE_RUN       1.6f  // runs this much faster than a normal clone
#define SHINOBI_RAICLONE_RADIUS    160
#define SHINOBI_RAICLONE_DAMAGE    15
#define SHINOBI_RAICLONE_STUN      1300
// Lightning Dash (held): you become a bolt of lightning racing ahead (steer
// with the mouse) for as long as you hold it and have the chakra
#define SHINOBI_RAIDASH_SPEED      3.0f  // x run speed
#define SHINOBI_RAIDASH_MAX_MS     4000
#define SHINOBI_RAIDASH_TICK_MS    100
#define SHINOBI_RAIDASH_DRAIN      4     // chakra per tick (40/s)
#define SHINOBI_RAIDASH_WIDTH      48    // shocks whoever it passes this close to
#define SHINOBI_RAIDASH_DAMAGE     10
#define SHINOBI_RAIDASH_REHIT_MS   800   // the same victim at most this often
#define SHINOBI_RAIDASH_COOLDOWN   3000
// Shadow Hide (held)
#define SHINOBI_SHADOWHIDE_MAX_MS  6000
#define SHINOBI_SHADOWHIDE_TICK_MS 250   // 1 chakra per tick
#define SHINOBI_SHADOWHIDE_COOLDOWN 8000
#define SHINOBI_SHADOWHIDE_SPEED   0.7f
// Flash
#define SHINOBI_FLASH_RADIUS       550
#define SHINOBI_FLASH_BLIND_MS     2500
#define SHINOBI_FLASH_FACING       0.2f  // victims looking at least this much toward you
#define SHINOBI_FLASH_COOLDOWN     10000
// Healing (held): chakra into health, one for one, as long as the key is held
#define SHINOBI_HEAL_TICK_MS       100
#define SHINOBI_HEAL_PER_TICK      2      // 20 chakra -> 20 health a second
#define SHINOBI_WALL_MAX_PILLARS   7
static const int shinobiWallPillars[3]   = {   3,   5,   7 };      // width
static const int shinobiWallHealth[3]    = {  35,  57,  85 };      // shared by the whole wall (Lv1 Rasengan = 70% of Lv2)
#define SHINOBI_WALL_DISTANCE      96   // in front of the caster
#define SHINOBI_WALL_SPACING       34
#define SHINOBI_WALL_HALF_WIDTH    18   // pillar collision half-size
#define SHINOBI_WALL_HEIGHT        104
#define SHINOBI_WALL_RISE_MS       400
#define SHINOBI_WALL_LIFETIME      12000
#define SHINOBI_WALL_SINK_MS       600
#define SHINOBI_WALL_MODELS        3    // models/shinobi/earth_pillar1..3.md3
// Pushing your own wall (Doton selected, F near the wall): it slides forward
// as a wave of earth and crumbles at the end; pinning someone crushes him
#define SHINOBI_WALL_PUSH_COST     15
#define SHINOBI_WALL_PUSH_REACH    150  // wall centre at most this far in front
#define SHINOBI_WALL_PUSH_SPEED    600
static const int shinobiWallPushDist[3]  = { 240, 320, 400 };
static const int shinobiWallPushDamage[3] = { 15, 25, 35 };
static const int shinobiWallCrushDamage[3] = { 30, 45, 60 };

/*
---------------------------------------------------------------------------
Elements. Jutsu carry an element; elements react with each other through
zones left on the ground and through the statuses they put on players:

  water + fire (zone or ball)  -> fire goes out, steam cloud (blocks sight)
  wind  + fire                 -> fire grows (zones), fireballs level up
  wind  + steam                -> steam blown away
  lightning + water            -> the puddle is electrified (stuns, hurts)
  earth + water                -> mud (slows)
  wet: fire hurts half, lightning 1.5x, can't catch fire
  earth wall: fire does half damage, water and wind a third
---------------------------------------------------------------------------
*/
typedef enum {
	SE_NONE,
	SE_FIRE,
	SE_WATER,
	SE_EARTH,
	SE_WIND,
	SE_LIGHTNING
} shinobiElement_t;

typedef enum {
	SZ_FIRE,      // burning ground: sets people on fire
	SZ_WATER,     // puddle: wets, puts out fire
	SZ_MUD,       // slows
	SZ_STEAM,     // thick cloud, puts out fire
	SZ_ELECTRIC,  // electrified puddle: stuns and hurts, back to water later
	SZ_ASH,       // ash cloud: blinds, chokes; any fire makes it explode
	SZ_MIST,      // hidden mist: thick fog, puts out fire, only shapes show
	SZ_DUST,      // loose earth and sand left by Doton; water on it (or it on water) makes mud
	SZ_ICE,       // a puddle frozen by the wind: slippery; fire melts it back to water
	SZ_COUNT
} shinobiZone_t;

#define SHINOBI_ZONE_HEIGHT        64    // people this far above a zone stand in it
#define SHINOBI_ZONE_TICK_MS       250
#define SHINOBI_ZONE_MAX_RADIUS    220

static const int shinobiFireZoneRadius[3] = {   0,   90,  130 }; // Katon Lv2+ leaves burning ground
static const int shinobiFireZoneLife[3]   = {   0, 5000, 7000 };
#define SHINOBI_BURN_MS            3000  // still burning after leaving the fire
#define SHINOBI_BURN_TICK_MS       500
#define SHINOBI_BURN_DAMAGE        3
#define SHINOBI_WET_MS             6000
#define SHINOBI_STEAM_MS           4000
#define SHINOBI_MUD_MS             24000
#define SHINOBI_DUST_MS            40000
#define SHINOBI_ICE_MS             24000
// Wind + water = ice (not canon, but fun): the wind (Wind Wall, Storm Wind)
// freezes puddles; your own water bullets through your own Wind Wall freeze
// into icicles (harder, faster, no soaking); your own fireball through it
// grows twice as big (the same damage, just a bigger ball)
#define SHINOBI_ICE_DAMAGE         1.6f
#define SHINOBI_ICE_SPEED          1.3f
#define SHINOBI_FIREBALL_WIND_SCALE 200  // %
#define SHINOBI_BURROW_DUST_STEP   64    // a patch of dust every this many units underground
#define SHINOBI_BURROW_DUST_RADIUS 56
#define SHINOBI_MUD_SLOW           0.5f
#define SHINOBI_ELECTRIC_MS        3000
#define SHINOBI_ELECTRIC_DAMAGE    3     // per zone tick
#define SHINOBI_ELECTRIC_STUN_MS   350

// Water Bullets: the charge level is the number of bullets (1-3), fired in a
// quick burst. Fast, small, each leaves a little puddle and puts out fire.
// (Tables per bullet "level"; a bullet weakened by a fireball drops a level.)
#define SHINOBI_WATER_COOLDOWN     900
#define SHINOBI_WATER_LIFE         1500
#define SHINOBI_WATER_BURST_MS     140   // between bullets of one burst
static const int   shinobiWaterSpeed[3]   = { 1600, 1500, 1400 };
static const int   shinobiWaterSize[3]    = {    6,    9,   12 };
static const int   shinobiWaterDamage[3]  = {   12,   20,   30 };
static const int   shinobiWaterPush[3]    = {  150,  250,  350 };
static const int   shinobiPuddleRadius[3] = {   45,   60,   80 };
static const int   shinobiPuddleLife[3]   = { 12000, 14000, 16000 };
static const float shinobiWaterVisual[3]  = { 7.0f, 10.0f, 14.0f };
#define SHINOBI_PUDDLE_SLOW        0.8f  // puddles slow a little

// Water Wave: a low wall of water rolling along the floor. Low enough to clear
// with a force jump, not with a normal one. Pushes, soaks, puts out fire,
// leaves puddles behind; breaks on walls (mud at an earth wall).
#define SHINOBI_WAVE_SPEED         520
#define SHINOBI_WAVE_HEIGHT        56
#define SHINOBI_WAVE_PUDDLE_STEP   140   // a puddle every this many units
static const int shinobiWaveHalfWidth[3] = {  70, 100, 130 };
static const int shinobiWaveRange[3]     = { 400, 550, 700 };
static const int shinobiWaveDamage[3]    = {  10,  18,  28 };
static const int shinobiWavePush[3]      = { 350, 500, 700 };

// Wind Blade: a wide arc of wind sweeping forward at chest height; hard to
// dodge sideways, jump over it. Hits each target once; walls stop it.
#define SHINOBI_BLADE_SPEED        1300
#define SHINOBI_BLADE_ARC          0.57f // cos of the half angle (~55 degrees)
#define SHINOBI_BLADE_HALF_HEIGHT  40
#define SHINOBI_BLADE_COOLDOWN     900
static const int shinobiBladeRange[3]  = { 450, 600, 800 };
static const int shinobiBladeDamage[3] = {  20,  30,  45 };
static const int shinobiBladePush[3]   = { 150, 250, 350 };

// Fire Breath: held; a cone of flame that follows the view at a limited turn
// rate, sets people and the ground on fire. The breather moves slowly.
#define SHINOBI_FLAME_RANGE        260
#define SHINOBI_FLAME_CONE         0.95f  // cos of the half angle (~18 degrees)
#define SHINOBI_FLAME_TURN         55.0f  // degrees per second (bots and clones turn the same stream)
#define SHINOBI_FLAME_TICK_MS      150
#define SHINOBI_FLAME_DAMAGE       3      // per tick (20/s)
#define SHINOBI_FLAME_DRAIN_MS     250
#define SHINOBI_FLAME_DRAIN        3      // chakra per drain tick (12/s)
#define SHINOBI_FLAME_ZONE_MS      400    // a patch of burning ground this often
#define SHINOBI_FLAME_SLOW         0.4f

// Ash Cloud: a cloud of hot ash in front; blinds and chokes, and any fire
// (fireball, fire breath, burning ground, Chidori) blows it up.
static const int shinobiAshRadius[3]  = { 110, 150, 190 };
static const int shinobiMistRadius[3] = { 260, 340, 420 };
static const int shinobiMistLife[3]   = { 10000, 13000, 16000 };
#define SHINOBI_MIST_SHAPES        450    // inside the mist people show as shapes this far
static const int shinobiAshBlast[3]   = {  35,  50,  70 };
#define SHINOBI_ASH_LIFE           9000
#define SHINOBI_ASH_DISTANCE       200
#define SHINOBI_ASH_DAMAGE         1      // choking, per zone tick

// Shadow Bind: held; a shadow crawls forward along the ground. An enemy standing
// on it is caught and copies every move you make (attacks included) until you
// let go. Jump over the shadow to dodge.
#define SHINOBI_SHADOW_SPEED       900
#define SHINOBI_SHADOW_RANGE       600
#define SHINOBI_SHADOW_HOLD_RANGE  900
#define SHINOBI_SHADOW_WIDTH       28
#define SHINOBI_SHADOW_DRAIN_MS    300
#define SHINOBI_SHADOW_DRAIN       3      // chakra per drain tick (10/s)
#define SHINOBI_BUTTON_MIRROR      (1<<14) // usercmd bit: moves set by the server (Shadow Bind)
#define SHINOBI_BUTTON_BLOCK       (1<<13) // usercmd bit: block held (right mouse button = "+button13")
#define SHINOBI_BUTTON_ROLL        (1<<12) // usercmd bit: the roll key (Ctrl = "+button12")

/*
Block (hold Ctrl): a guard stance. Hits from the front go into the guard
(the shield stat) instead of health: fists, kicks and blades fully, jutsu
half. No attacks or jutsu while blocking, slow steps, no jumping. Empty
guard = guard break (a stagger). A full Rasengan / Chidori (level 3) breaks
any guard at once. Perfect parry: a hit landing within the first moments of
pressing block does nothing, and a close attacker is staggered.
The shield no longer soaks damage on its own.
*/
#define SHINOBI_GUARD_MAX          200    // twice the first version: one combo no longer breaks it
#define SHINOBI_GUARD_REGEN_DELAY  1500   // after a blocked hit or letting go
#define SHINOBI_GUARD_REGEN_MS     80     // +1 guard
#define SHINOBI_BLOCK_SLOW         0.4f
#define SHINOBI_BLOCK_ARC          0.26f  // cos of the half-angle covered (~75 deg)
#define SHINOBI_PARRY_MS           200    // perfect parry window after pressing block
#define SHINOBI_PARRY_SPAM_MS      700    // presses closer than this get no parry window
#define SHINOBI_PARRY_STAGGER_MS   1500   // the parried attacker reels (long enough to step in and hit)
// a blade swing on a blade guard costs by the swing's style, once per swing (the
// stock SP-style damage of the touch itself is only a few points)
#define SHINOBI_GUARD_SWING_FAST   24
#define SHINOBI_GUARD_SWING_MEDIUM 34
#define SHINOBI_GUARD_SWING_STRONG 50
#define SHINOBI_GUARD_SWING_OTHER  30     // dual / staff
#define SHINOBI_GUARD_SWING_MS     350    // the same attacker's next touch within this is the same swing
#define SHINOBI_GUARD_BREAK_MS     1000   // the guard broke: reeling
#define SHINOBI_PARRY_REACH        160    // parried attackers this close get staggered
#define SHINOBI_BLOCK_ANIM         BOTH_P1_S1_T_
#define SHINOBI_STAGGER_ANIM       BOTH_H1_S1_T_

void     BG_ShinobiBlockAttack( const playerState_t *ps, usercmd_t *cmd );
qboolean BG_ShinobiCanBlock( const playerState_t *ps, int buttons ); // bg_pmove.c

// Player size (models/players/<model>/scale.cfg, "scale 85" = 85 %): the model, the hitbox
// and the eyes shrink together, the feet stay on the floor (bbox bottom DEFAULT_MINS_2).
#define SHINOBI_SIZE_MIN 50
#define SHINOBI_SIZE_MAX 150
float BG_ShinobiSize( const playerState_t *ps );                 // 1 = stock size
int   BG_ShinobiSized( const playerState_t *ps, int height );    // a height over the feet, scaled

/*
---------------------------------------------------------------------------
Ninja tools on the maps: the stock pickups are turned into these at load
(Shinobi_PatchItems, in both the game and the cgame, so item numbers match).
  Healing Pill    health at once          (medkits, bacta)
  Chakra Pill     chakra at once          (force items, ammo packs, gadgets)
  Explosive Kunai 2 to throw              (thermals, pistols, blaster)
  Explosive Tag   2 to stick as traps     (trip mines, droids, turrets)
  Fuma Shuriken   2 big ones to throw     (det packs, heavy guns)
The throwables are the stock explosive weapons underneath (thermal = kunai,
trip mine = tag, det pack = shuriken): picked and used like any weapon
(weapon keys / wheel, attack), counts in ps.ammo[], the stock HUD. Z with
one of them in hand uses it too; with fists or the blade Z throws a kunai.
---------------------------------------------------------------------------
*/
typedef enum {
	STOOL_NONE,
	STOOL_HEAL,
	STOOL_CHAKRA,
	STOOL_EXKUNAI,
	STOOL_TAG,
	STOOL_SHURIKEN,
	STOOL_COUNT
} shinobiTool_t;

#define SHINOBI_AMMO_EXKUNAI       AMMO_THERMAL
#define SHINOBI_AMMO_TAG           AMMO_TRIPMINE
#define SHINOBI_AMMO_SHURIKEN      AMMO_DETPACK
#define SHINOBI_WP_EXKUNAI         WP_THERMAL
#define SHINOBI_WP_TAG             WP_TRIP_MINE
#define SHINOBI_WP_SHURIKEN        WP_DET_PACK
#define SHINOBI_IS_TOOL_WEAPON(w)  ( (w) == WP_THERMAL || (w) == WP_TRIP_MINE || (w) == WP_DET_PACK )
#define SHINOBI_TOOL_MAX           6     // carried of each throwable
#define SHINOBI_TOOL_PICKUP        2     // given by one pickup

void Shinobi_PatchItems( void );                       // bg_shinobi.c
int  Shinobi_ToolForItem( const char *classname, int *amount );
int  Shinobi_CanGrabTool( const char *classname, const playerState_t *ps );
const char *Shinobi_ToolName( int tool );   // "2 Explosive Kunai" (pickup line)
const char *Shinobi_ToolTitle( int tool );  // "Explosive Kunai"
const char *Shinobi_ToolModel( int tool );  // its model (on the floor, in flight, in the hand)
#define SHINOBI_TOOL_FOR_WEAPON(w) ( (w) == SHINOBI_WP_EXKUNAI ? STOOL_EXKUNAI : (w) == SHINOBI_WP_TAG ? STOOL_TAG \
	: (w) == SHINOBI_WP_SHURIKEN ? STOOL_SHURIKEN : STOOL_NONE )

// Explosive Kunai: sticks where it lands (or in whom it hits), then goes off
#define SHINOBI_EXKUNAI_FUSE       1000
#define SHINOBI_EXKUNAI_RADIUS     140
#define SHINOBI_EXKUNAI_DAMAGE     45
#define SHINOBI_EXKUNAI_COOLDOWN   700
// Explosive Tag: stuck on a wall or the floor; armed after a moment, it
// goes off shortly after an enemy comes near (or fire / a blast reaches it)
#define SHINOBI_TAG_ARM_MS         1000
#define SHINOBI_TAG_TRIGGER        90
#define SHINOBI_TAG_FUSE           400
#define SHINOBI_TAG_RADIUS         170
#define SHINOBI_TAG_DAMAGE         55
#define SHINOBI_TAG_LIFE           90000
#define SHINOBI_TAG_MAX            2     // stuck at once (the oldest goes)
#define SHINOBI_TAG_REACH          96    // how far from the eye it can be placed
#define SHINOBI_TAG_COOLDOWN       700
// Fuma Shuriken: a big spinning star, straight and hard
// Fuma Shuriken: only one carried; thrown like a saber, it flies out, turns and comes back
// to the hand (catch it to throw again); a wall on the way keeps it - go and pull it out
#define SHINOBI_SHURIKEN_MAX       1
#define SHINOBI_SHURIKEN_SPEED     750
#define SHINOBI_SHURIKEN_RANGE     850   // flies out this far, then turns back
#define SHINOBI_SHURIKEN_BACK_SPEED 700
#define SHINOBI_SHURIKEN_CATCH     48    // close enough to the thrower: caught
#define SHINOBI_SHURIKEN_DAMAGE    45    // to each one it cuts, once out and once back
#define SHINOBI_SHURIKEN_LIFE      5000  // in the air at most (then it just drops)
#define SHINOBI_SHURIKEN_STUCK_LIFE 30000 // stuck in a wall
#define SHINOBI_SHURIKEN_GRAB      40    // walk this close to a stuck one to pull it out
#define SHINOBI_SHURIKEN_COOLDOWN  900

#define SHINOBI_ENT_EXKUNAI        234   // explosive kunai (model + sizzling tag glow); s.time2 = blast time (0 = flying)
#define SHINOBI_ENT_TAG            235   // explosive tag; s.time = armed at, s.time2 = blast time (0 = waiting)
#define SHINOBI_ENT_SHURIKEN       236   // fuma shuriken (spinning model)
#define SHINOBI_ENT_FINISHER       239   // a blade finisher's look; origin = the one hit, s.origin2 = the attacker,
                                         // s.frame = SFX_*, s.time = when
#define SHINOBI_ENT_WINDSLASH      238   // a Wind Edge air slash; origin = start, s.origin2 = where it stopped, s.time = thrown,
                                         // s.frame = 1 hit someone | 2 a blade, s.angles2[2] = tilt of the cut
#define SHINOBI_ENT_RAINET         237   // lightning net; origin, s.origin2, s.angles2 = nodes; s.frame = count | lit sides << 2

// Water Clone: one clone made of water that fights like a shadow clone and casts,
// on its own, the jutsu that comes after Water Clone in your build (Water Prison:
// it holds the prison for you). Its jutsu cost your chakra. One hit: a puddle.
#define SHINOBI_WATERCLONE_LIFE    30000
#define SHINOBI_WATERCLONE_CAST_MS 3000  // between its casts
#define SHINOBI_WATERCLONE_HOLD_MS 6000  // longest it holds a prison / bind
#define SHINOBI_WATERCLONE_BREATH_MS 2500

// Kunai (Z, everyone): thrown point first, sticks in walls. Shift+Z throws a
// marked kunai (Hiraishin seal): costs chakra, stays 30 s, marks the enemy it
// hits for 12 s. Hiraishin teleports to it, Lightning Trap discharges it.
#define SHINOBI_KUNAI_SPEED        1800
#define SHINOBI_KUNAI_DAMAGE       10
#define SHINOBI_KUNAI_COOLDOWN     2500
#define SHINOBI_KUNAI_LIFE         8000   // stuck in a wall
#define SHINOBI_MARK_COST          10
#define SHINOBI_MARK_LIFE          30000
#define SHINOBI_MARK_ON_ENEMY      12000
#define SHINOBI_MARK_MAX           3      // per player; a fourth replaces the oldest
#define SHINOBI_HIRAISHIN_COOLDOWN 8000
#define SHINOBI_RAITRAP_RADIUS     110
#define SHINOBI_RAITRAP_DAMAGE     25
#define SHINOBI_RAITRAP_STUN_MS    800
// Lightning Trap with two or three marks: a net of lightning between them
#define SHINOBI_RAINET_MS          8000
#define SHINOBI_RAINET_SPAN        500    // the longest side that lights
#define SHINOBI_RAINET_REACH       14     // how close to a line a body must come
#define SHINOBI_RAINET_DAMAGE      20
#define SHINOBI_RAINET_STUN_MS     1000
#define SHINOBI_RAINET_REHIT_MS    1500   // the same man again only after this
#define SHINOBI_RAINET_THINK_MS    50
#define SHINOBI_SHADOWTRAP_MARKS   2      // Shadow Trap: from the newest two marks only
// Shadow Trap: from each marked kunai a shadow reaches out along the floor to the
// nearest enemy in range and holds him where he stands (no moving, no attacks, no
// jutsu but Kawarimi) while its maker pays for it. Any damage to him breaks it.
#define SHINOBI_SHADOWTRAP_RANGE    420
#define SHINOBI_SHADOWTRAP_SPEED    1500   // how fast the shadow reaches him
#define SHINOBI_SHADOWTRAP_REACH_MS 1500   // not caught by then: it pulls back
#define SHINOBI_SHADOWTRAP_MS       15000  // the longest hold
#define SHINOBI_SHADOWTRAP_DRAIN_MS 300
#define SHINOBI_SHADOWTRAP_DRAIN    2      // chakra per tick per caught enemy (~7/s)
#define SHINOBI_KUNAI_MODEL        "models/shinobi/kunai.md3"

// Wind Edge / Lightning Edge (jutsu): chakra on the fists and blade for a while
// (the old Shift + attack infusion is gone - Shift + attack does nothing special)
#define SHINOBI_EDGE_MS            15000 // Wind / Lightning Edge duration
#define SHINOBI_EDGE_FX_MS         220
/*
Blades: one blow per swing to each one it cuts, for a set amount by stance (times the
weapon's damageScale; taijutsu and the rest come on top in Shinobi_BuildDamage), instead
of the stock sum of every frame the blade spent inside the body (1 to 120 for one swing).
Special moves and katas: SHINOBI_BLADE_SPECIAL times as much, again every
SHINOBI_BLADE_SPECIAL_MS while they last. A touch outside a swing does nothing.
*/
// (the saber_dps autotest, attack held: fast swings 1.25 a second, medium 0.75, strong 0.375,
// dual 0.875 - so fast, medium and dual do ~10 a second, strong ~9 in rare heavier blows;
// cut to 30% after a play test - half the health a blow was too much)
//                                           -   fast medium strong desann tavion dual staff
static const int shinobiSwordDamage[8] = {   0,  8,   14,    24,    24,    9,     12,  12 };
/*
Blade combo: swings that land on the same one, each soon after the last (shinobiSwordWindow),
hold him flinching like the fist combo; the shinobiSwordChain-th is the finisher (x1.5),
picked by what you hold as it lands: back - a sweep that knocks him down; left / right - a
spin that also cuts everyone around you; jump - a rising cut that throws him up; nothing or
forward - the weapon's own (shinobiFinisher in its .sab):
  drain  (Samehada)      eats up to SHINOBI_SWORD_DRAIN of his chakra and gives it to you
  bleed  (Kubikiribocho) opens a wound: SHINOBI_SWORD_BLEED_DMG every _TICK for _MS
  reach  (trench knives) the finishing swing throws a chakra blade out to _REACH: it lands
                         even when he got out of the knives' reach
  iai    (katana)        a clean draw-cut: staggers him (_IAI_MS)
  flurry (Kurosawa)      two more quick cuts right after (_FLURRY_SCALE of the blow each)
*/
//                                           -   fast medium strong desann tavion dual staff
static const int shinobiSwordChain[8] =  {   0,  4,   3,     2,     2,     4,     4,   4 };
static const int shinobiSwordWindow[8] = {   0, 1300, 1900, 3200,  3200, 1300, 1700, 1700 };
#define SHINOBI_SWORD_FINISH       1.5f
enum { SBF_NONE, SBF_DRAIN, SBF_BLEED, SBF_REACH, SBF_IAI, SBF_FLURRY, SBF_COUNT };
static const char * const shinobiSwordFinisherNames[SBF_COUNT] = { "none", "drain", "bleed", "reach", "iai", "flurry" };
#define SHINOBI_SWORD_DRAIN        25
#define SHINOBI_SWORD_BLEED_DMG    2
#define SHINOBI_SWORD_BLEED_TICK   500
#define SHINOBI_SWORD_BLEED_MS     4000
#define SHINOBI_SWORD_REACH        220
#define SHINOBI_SWORD_IAI_MS       800
#define SHINOBI_SWORD_FLURRY       2
#define SHINOBI_SWORD_FLURRY_MS    120
#define SHINOBI_SWORD_FLURRY_SCALE 0.6f
#define SHINOBI_SWORD_SPIN_RADIUS  100
#define SHINOBI_SWORD_LAUNCH_UP    480
// what the cgame draws for a finisher (SHINOBI_ENT_FINISHER s.frame)
enum { SFX_NONE, SFX_DRAIN, SFX_BLEED, SFX_IAI, SFX_FLURRY, SFX_SWEEP, SFX_SPIN, SFX_LAUNCH };
#define SHINOBI_FINISHER_FX_MS     700

#define SHINOBI_BLADE_SPECIAL      1.6f
#define SHINOBI_BLADE_SPECIAL_MS   350

#define SHINOBI_WINDSLASH_REACH    180   // air slashes fly this far (from the eyes)
#define SHINOBI_WINDSLASH_FLY_MS   110   // drawn flying out over this long
#define SHINOBI_WINDSLASH_LIFE_MS  420   // the entity drawing it (fly + fade)
#define SHINOBI_WINDSLASH_DAMAGE_SABER 14
#define SHINOBI_WINDSLASH_DAMAGE_FIST  7
#define SHINOBI_WINDSLASH_MS       350   // at most one slash this often
#define SHINOBI_RAIEDGE_BONUS      120   // % damage
#define SHINOBI_RAIEDGE_STONE      3     // x against stone

// After any hard crowd control ends, a short immunity to the next one
#define SHINOBI_CC_IMMUNE_MS       2500
// Chakra from fighting: the pool comes back at the normal pace up to 30%, four times slower
// above that - the rest is earned (x(1 + 10% per Chakra regen point), x2 with the Curse Seal awake)
#define SHINOBI_REGEN_FULL_UPTO    30     // % of the pool
#define SHINOBI_REGEN_SLOW_ABOVE   4.0f   // regen interval x this above it
#define SHINOBI_GAIN_HIT_PCT       25     // fist / blade damage you deal
#define SHINOBI_GAIN_JUTSU_PCT     12     // jutsu damage you deal
#define SHINOBI_GAIN_HURT_PCT      10     // damage you take (a way back into the fight)
#define SHINOBI_GAIN_BLOCKED       3      // your hit lands on a guard
#define SHINOBI_GAIN_COMBO         10     // a combo finisher lands
#define SHINOBI_GAIN_GUARDBREAK    15     // you break a guard
#define SHINOBI_GAIN_PARRY         10     // your perfect parry
// knocked down: on the floor this long at least (stock 1.1 s), then a direction key rolls
// you up that way and jump flips you up (stock); doing nothing you stand up after this
// much more (stock 4 s)
#define SHINOBI_KNOCKDOWN_MS       750
#define SHINOBI_GETUP_AUTO_MS      700

// Extra predicted status bits that only the player himself needs
#define STAT_SHINOBI               12    // ps.stats[] slot (unused by the base game)
#define STAT_SHINOBI2              13    // a second one (STAT_SHINOBI is full)
#define SHINOBI_STAT2_ICE          1     // standing on ice: slides
#define SHINOBI_STAT2_DIVE         2     // diving kick on the way down
#define SHINOBI_STAT2_DIVELAND     4     // ... just landed (the server makes it hit, then clears it)
#define SHINOBI_STAT2_BLOCKHELD    8     // (to tell a new press of block / attack from a held one)
#define SHINOBI_STAT2_ATTACKHELD   16

/*
Kaiten (Yin / Yang, held): SHINOBI_KAITEN_WINDUP_MS rooted (a hit then breaks it),
then you spin for as long as the key is held and the chakra lasts. Nothing that
deals damage gets through (falls, lava and the like aside), no knockdown, stagger or
bind takes; missiles that would come within SHINOBI_KAITEN_DEFLECT glance off the
dome; whoever is within SHINOBI_KAITEN_RADIUS is thrown away and knocked down.
*/
/*
Water: a shinobi stands on map water while he has chakra (SHINOBI_WATERWALK_DRAIN a
second; a stun, a knockdown, a prison or an empty pool and he sinks; crouch to dive,
jump at the surface to come back up). Standing on it: Suiton costs 30% less. Lightning
reaching the water electrifies a patch of it (SHINOBI_WATER_SHOCK_RADIUS, 3 s); the wind
freezes a floe on it; fire only steams; puddles, mud and dust aren't left on it; the
Earthquake doesn't reach anyone standing on water.
*/
#define SHINOBI_WATERWALK_DRAIN    5      // chakra per second on the water (more than the natural regen)
#define SHINOBI_WATER_SUITON_COST  0.7f   // Suiton cast standing on water
#define SHINOBI_WATER_SHOCK_RADIUS 250
#define SHINOBI_WATER_SHOCK_MS     3000
#define SHINOBI_WATER_ICE_RADIUS   200
#define SHINOBI_WATER_ICE_MS       15000
#define SHINOBI_WATER_POPUP        320    // jump at the surface while swimming: up and out

#define SHINOBI_KAITEN_WINDUP_MS   500
#define SHINOBI_KAITEN_TICK_MS     100
#define SHINOBI_KAITEN_DRAIN       3     // chakra per tick (30 a second)
#define SHINOBI_KAITEN_RADIUS      96    // thrown off (centre to centre, flat)
#define SHINOBI_KAITEN_DEFLECT     84    // missiles glance off at this distance
#define SHINOBI_KAITEN_THROW       560   // outward speed
#define SHINOBI_KAITEN_THROW_UP    220
#define SHINOBI_KAITEN_SPIN        1.3f  // degrees per ms the model turns (cgame)

#define SHINOBI_STAT2_DIVEREQ      32    // attack pressed in the air: dive as soon as it can
#define SHINOBI_STAT2_SHADOWTRAP   64    // held in place by a Shadow Trap
// floating combat text over a target ("stx <ent> <kind> <value>", shinobi_hitnumbers 1)
enum { STX_DAMAGE, STX_FLINCH, STX_STAGGER, STX_KNOCKDOWN, STX_LAUNCH, STX_STUN, STX_TRAPPED, STX_BOUND,
	STX_BLOCK, STX_PARRY, STX_GUARDBREAK, STX_IMPRISONED, STX_INTERRUPT, STX_CHAKRA, STX_FINISHER, STX_COUNT };
static const char * const shinobiHitTextNames[STX_COUNT] = {
	"", "Flinch", "Stagger", "Knockdown", "Launch", "Stunned", "Shadow Trap", "Shadow Bind",
	"Block", "Parry!", "Guard break!", "Water Prison", "Interrupted", "chakra", "Finisher!"
};
#define SHINOBI_STAT2_FLINCH       128   // caught in a combo: rooted, no attacks / jutsu (the guard still works)
#define SHINOBI_STAT2_ROOTED       256   // charging an Earthquake: standing still
#define SHINOBI_STAT2_KAITEN       32768 // winding up / spinning a Kaiten: rooted, no attacks, no jumps
#define SHINOBI_STAT2_KAWA_SHIFT   9     // bits 9-10: Kawarimi charges ready (0-3)
#define SHINOBI_STAT2_SHUN_SHIFT   11    // bits 11-12: Shunshin charges ready (0-3)
#define SHINOBI_KAWA_CHARGES(ps)   ( ( (ps)->stats[STAT_SHINOBI2] >> SHINOBI_STAT2_KAWA_SHIFT ) & 3 )
#define SHINOBI_SHUN_CHARGES(ps)   ( ( (ps)->stats[STAT_SHINOBI2] >> SHINOBI_STAT2_SHUN_SHIFT ) & 3 )

/*
Taijutsu combo (fists): hold (or keep pressing) attack - jab, cross, jab, each
step within SHINOBI_COMBO_WINDOW of the last - then a finisher picked by the
direction held at that moment:
  nothing   front kick: pushed away and knocked down
  forward   launcher: thrown up into the air
  back      sweep: a forward roll into his legs, knocked down on the spot
  left / right  spin kick all around: just damage (a second chance to land one)
Chain hits are plain damage; kicks keep the stock kick knockdown. Attack next to
someone lying in front of you: a downward blow (the saber stab-down animation).
Attack in the air (anywhere, combo or not): a kick forward and down (45 degrees) that hits
around where you land. Block + attack: the stock grab.
*/
/*
Lock-on, for close combat: with fists or a blade in hand the nearest enemy in front of you,
in plain sight within SHINOBI_LOCK_RANGE, is locked on by itself (shinobi_autolock); T moves
it to the next one close by. Pmove turns the view after him - only sideways (the mouse keeps
the pitch: the floor for a tag, the air for a throw), at most SHINOBI_LOCK_TURN a second, so
a quick step round him, a Shunshin behind him or a jump over him slips it for a moment.
It lets go past SHINOBI_LOCK_KEEP, out of sight, with a tool in hand, the moment he hides
(Kawarimi, under the ground, in his shadow) - then nothing is locked for a moment - and
while your own sight is spoiled: blinded by a Flash, inside a Hidden Mist or an Ash Cloud
(the Byakugan sees through mist and ash).
*/
#define STAT_SHINOBI_LOCK          11    // the target's entity number + 1 (0 = free)
#define STAT_SHINOBI_BLADECOMBO    10    // the blade combo for the HUD: blows landed (bits 0-2), the finisher's number (bits 3-5)
#define SHINOBI_LOCK_RANGE         250   // locks on within this (halved after a play test)
#define SHINOBI_LOCK_KEEP          350   // lets go past this
#define SHINOBI_LOCK_LOST_MS       1000  // ... or out of sight this long
#define SHINOBI_LOCK_HIDE_MS       1200  // after he hid: no new lock this long
#define SHINOBI_LOCK_FOV           0.34f // cos of the half angle it picks from (70 degrees; within 150: anywhere)
#define SHINOBI_LOCK_TURN          300.0f // degrees a second the view turns after him
#define STAT_SHINOBI_COMBO         14    // hit number (bits 0-2) and kind (bits 3-5) of the current move
#define STAT_SHINOBI_COMBOT        15    // ms left to chain the next hit
#define SHINOBI_COMBO_WINDOW       450
#define SHINOBI_COMBO_HIT(ps)      ( (ps)->stats[STAT_SHINOBI_COMBO] & 7 )
#define SHINOBI_COMBO_KIND(ps)     ( ( (ps)->stats[STAT_SHINOBI_COMBO] >> 3 ) & 7 )
// Air combo: jump right after the launcher (SHINOBI_AIR_WINDOW) and you fly after him; in the
// air your punches keep him up (two of them), the next attack is the diving kick that slams
// him down. While up there he is helpless (a flinch until he lands) - Kawarimi still works
#define SHINOBI_COMBO_AIR          ( 1 << 6 ) // STAT_SHINOBI_COMBO: in an air combo
#define SHINOBI_AIR_WINDOW         450   // ms after the launcher to jump after him
#define SHINOBI_AIR_JUMP           620   // the leap after him (up)
#define SHINOBI_AIR_JUMP_FWD       200   // ... and forward
#define SHINOBI_AIR_GRAVITY        0.45f // hanging in the air while the combo goes on
#define SHINOBI_AIR_HITS           2     // punches up there before the slam
#define SHINOBI_AIR_LIFT           220   // each air punch keeps him this high up (speed)
#define SHINOBI_SLAM_DAMAGE        18
#define SHINOBI_SLAM_REACH         110
enum { SCK_NONE, SCK_HIT, SCK_SPIN, SCK_LAUNCH, SCK_KICK, SCK_SWEEP, SCK_DIVE, SCK_GROUND };
// combo damage grows hit by hit, the finisher hardest: the four make a bit less than four
// blade swings (-30% after a play test) - get out of the way or raise the guard once a chain starts
static const int shinobiComboPunch[3] = { 11, 15, 21 };  // jab, cross, jab (fast style)
// Taijutsu styles (L, like the blade stances): fast - jab, cross, jab, finisher (the above);
// medium - jab, roundhouse kick, side kick, finisher: slower, longer reach, more per hit;
// strong - shoulder ram, double kick, finisher: slow, few hits, eats the guard
enum { SFS_FAST, SFS_MEDIUM, SFS_STRONG, SFS_COUNT };
static const char * const shinobiFistStyleNames[SFS_COUNT] = { "Fast", "Medium", "Strong" };
static const int   shinobiFistChain[SFS_COUNT] = { 3, 3, 2 };                // chain hits before the finisher
static const int   shinobiFistHit[SFS_COUNT][3] = { { 11, 15, 21 }, { 14, 20, 26 }, { 24, 32, 0 } };
static const float shinobiFistFinisher[SFS_COUNT] = { 1.0f, 1.15f, 1.3f }; // finisher damage x
static const float shinobiFistGuard[SFS_COUNT] = { 1.0f, 1.2f, 1.8f };     // guard eaten x
#define SHINOBI_STAT2_FIST_SHIFT   13    // bits 13-14: the taijutsu style
#define SHINOBI_FIST_STYLE(ps)     ( ( ( (ps)->stats[STAT_SHINOBI2] >> SHINOBI_STAT2_FIST_SHIFT ) & 3 ) % SFS_COUNT )
#define SHINOBI_COMBO_KICK_DAMAGE  35    // front kick finisher
#define SHINOBI_COMBO_LAUNCH_DAMAGE 32   // rising kick
#define SHINOBI_COMBO_SPIN_DAMAGE  25    // spin kick, to everyone around
#define SHINOBI_SWEEP_DAMAGE       28    // the rolling sweep, to each one it bowls over
#define SHINOBI_SWEEP_RADIUS       44
#define SHINOBI_COMBO_CHAIN        3     // chain hits before the finisher
#define SHINOBI_PUNCH_RANGE        34    // a punch reaches this far past the fist's start (stock 8)
#define SHINOBI_PUNCH_BOX          10    // half size of the punch trace (stock 6)
// a chain punch roots the victim and stops him hitting back until the attacker's next
// hit can land (his weapon time + this); only the guard or Kawarimi gets him out
#define SHINOBI_COMBO_FLINCH_MS    250
#define SHINOBI_COMBO_FLINCH_MIN   400
#define SHINOBI_COMBO_FLINCH_MAX   1100  // (the strong style's blows come slowly)
#define SHINOBI_DIVE_SPEED         650   // forward and down each (45 degrees)
#define SHINOBI_DIVE_RADIUS        90
#define SHINOBI_DIVE_DAMAGE        10
#define SHINOBI_GROUND_RANGE       96    // the downward blow: how far ahead the lying enemy may be
#define SHINOBI_GROUND_DAMAGE      21
#define SHINOBI_STAT_PUDDLE        1     // standing in a puddle
#define SHINOBI_STAT_CURSE         2     // Curse Seal awakened
#define SHINOBI_STAT_CHANNEL       4     // breathing fire: slowed
#define SHINOBI_STAT_BOUND         8     // caught by a Shadow Bind (moves come from the server)
#define SHINOBI_STAT_GATES         16    // Inner Gates open: faster, harder fists
#define SHINOBI_STAT_TIRED         32    // after the Gates: slow, no chakra regen
#define SHINOBI_STAT_STAGGER       64    // guard broken / parried: reeling, helpless
#define SHINOBI_STAT_CURSEWEAK     128   // Curse Seal stirs but the body is too weak (health <= 10)
#define SHINOBI_STAT_WINDEDGE      256   // Wind Edge on the hands / blade
#define SHINOBI_STAT_RAIEDGE       512   // Lightning Edge on the hands / blade
#define SHINOBI_STAT_SKYSTEP       1024  // Sky Step in the build: cheap jumps
#define SHINOBI_STAT_BURROW        2048  // under the ground (Burrow)
#define SHINOBI_STAT_ARMOR         4096  // Stone Skin
#define SHINOBI_STAT_SHADOWHIDE    8192  // melted into the shadow
#define SHINOBI_STAT_BLIND         16384 // blinded by a Flash
#define SHINOBI_STAT_RAIDASH       32768 // a bolt of lightning (Lightning Dash held)
// out of sight: Kawarimi-hidden or burrowed (no targeting, no body collisions)
#define SHINOBI_UNSEEN(ps)         ( SHINOBI_HIDDEN( ps ) || ( (ps)->stats[STAT_SHINOBI] & ( SHINOBI_STAT_BURROW | SHINOBI_STAT_SHADOWHIDE ) ) )

// eye techniques, shown to everyone (the two spare eFlags2 bits)
// eFlags bits the stock game leaves unused (seen by everyone)
#define SHINOBI_EF_STONE           (1<<12) // Stone Skin / Earth Clone: drawn as stone
#define SHINOBI_EF_RAIDASH         (1<<15) // Lightning Dash: drawn as a bolt, the body hidden
#define SHINOBI_EF_WATERWALK       (1<<31) // chakra in the feet: the water surface holds him (the server says when)
#define SHINOBI_PMF_ONWATER        32768   // ps.pm_flags: standing on the water surface (Pmove says)
#define SHINOBI_EF_KAITEN          (1<<18) // spinning a Kaiten: the model turns, a dome of chakra around it
#define SHINOBI_EF_GUARD_SHIFT     16      // guard up: 1 = strong (blue), 2 = half (yellow), 3 = low (red); 0 = down
#define SHINOBI_EF_GUARD_MASK      ( 3 << SHINOBI_EF_GUARD_SHIFT )
#define SHINOBI_EF_GUARD(ef)       ( ( (ef) & SHINOBI_EF_GUARD_MASK ) >> SHINOBI_EF_GUARD_SHIFT )
#define SHINOBI_EF2_SHARINGAN      (1<<8)
#define SHINOBI_EF2_BYAKUGAN       (1<<9)

/*
Perks (perk command, key C):
  Sharingan  toggle; 5 chakra, then 3/s (no natural regen while open). See health bars, the jutsu an
             enemy is charging and Kawarimi-hidden enemies. Red eyes.
  Byakugan   toggle; 5 chakra, then 8/s (no natural regen while open); fists burn chakra (Gentle Fist). See chakra bars and everyone
             within range through walls; clones show as hollow shells.
             Loud to switch on. Pale eyes.
  Inner Gates  10 s faster and +50% taijutsu, then 15 s exhausted
             (slow, weak, no chakra regen); once a minute.
*/
#define SHINOBI_DOJUTSU_COST           5
#define SHINOBI_SHARINGAN_DRAIN_MS     333     // 1 chakra per tick (3/s), and no natural regen meanwhile
#define SHINOBI_BYAKUGAN_DRAIN_MS      125     // 8/s (it also gives the Gentle Fist)
#define SHINOBI_SHARINGAN_RANGE        1400
#define SHINOBI_SHARINGAN_HIDDEN_RANGE 1500
#define SHINOBI_BYAKUGAN_RANGE         1400
#define SHINOBI_PERK_INFO_MS           200     // sinfo updates to the eye user
#define SHINOBI_GATES_MS               10000
#define SHINOBI_GATES_TIRED_MS         15000
#define SHINOBI_GATES_COOLDOWN         60000
#define SHINOBI_GATES_SPEED            1.35f
#define SHINOBI_GATES_TIRED_SPEED      0.75f
#define SHINOBI_GATES_TAIJUTSU         1.5f
#define SHINOBI_GATES_TIRED_TAIJUTSU   0.7f

// "sinfo" entry flags
#define SHINOBI_SEEN_HEALTH   1
#define SHINOBI_SEEN_CHAKRA   2
#define SHINOBI_SEEN_CLONE    4
#define SHINOBI_SEEN_HIDDEN   8
#define SHINOBI_SEEN_JUTSU    16  // Copy Sharingan: only the jutsu being charged

// Futon: Daitoppa (wind gust): cone in front, pushes, fans flames
#define SHINOBI_WIND_COOLDOWN      1500
// Storm Wind cast with back held: the gust pulls everyone in the cone toward you
// instead, in a short hop that ends about at your feet
#define SHINOBI_WIND_PULL_STOP     64     // lands this far in front of you
#define SHINOBI_WIND_PULL_AIR      0.5f   // seconds in the air (with the hop below)
#define SHINOBI_WIND_PULL_UP       200
#define SHINOBI_WIND_PULL_MAX      1000
#define SHINOBI_WIND_CONE          0.82f // cos of the half angle (~35 degrees)
static const int shinobiWindRange[3]  = { 320,  480,  640 };
static const int shinobiWindPush[3]   = { 450,  700, 1000 };
static const int shinobiWindDamage[3] = {   4,    8,   12 };

// Suiro: water prison. Lasts while you hold the key and pay chakra: the victim
// floats in a ball of water, helpless (only Kawarimi gets out); you can't
// move either.
#define SHINOBI_PRISON_RANGE       150
#define SHINOBI_PRISON_COOLDOWN    6000
#define SHINOBI_PRISON_TICK_MS     250
#define SHINOBI_PRISON_DRAIN       2     // chakra per tick (8/s)
#define SHINOBI_PRISON_DAMAGE      2     // drowning per tick (8/s)
#define SHINOBI_PRISON_RADIUS      42.0f // drawn water ball
#define SHINOBI_PRISON_STUN_MS     500   // gasping for air after the prison bursts: free hit

// Hand seals while a jutsu key is held (cycled torso gestures)
#define SHINOBI_SEAL_MS            220

// Non-player entities drawn by the cgame (entityState.generic1 on ET_GENERAL)
#define SHINOBI_ENT_FIREBALL       201  // 201..203 = fireball level 1..3
#define SHINOBI_LOG_MODEL          "models/shinobi/log.md3"
#define SHINOBI_ENT_WAVE           220  // 220..222 = water wave level 1..3; s.angles2 = half width, yaw
#define SHINOBI_ENT_BLADE          223  // 223..225 = wind blade level 1..3; s.angles2[0] = yaw, s.time = start
#define SHINOBI_ENT_FLAME          226  // fire breath stream; origin = mouth, s.angles2 = direction angles
#define SHINOBI_ENT_SHADOW         227  // shadow bind; origin = caster's feet, s.angles2 = tip on the floor
#define SHINOBI_ENT_KUNAI          228  // plain kunai (drawn from its model)
#define SHINOBI_ENT_KUNAI_MARKED   229  // marked kunai: model + seal glow
#define SHINOBI_ENT_QUAKE          230  // earthquake; s.angles2[0] = radius, s.time = cast, s.time2 = strike
#define SHINOBI_ENT_WINDWALL       231  // wind wall; origin = bottom centre, s.angles2 = yaw, half width; s.time2 = end
#define SHINOBI_ENT_PILLARCAST     232  // rock pillar rumbling; s.time2 = eruption
#define SHINOBI_ENT_SHADOWPATCH    233  // the dark patch of someone hidden in their shadow
#define SHINOBI_ENT_WATERBALL      204  // 204..206 = water bullet level 1..3
#define SHINOBI_ENT_ICESHARD       207  // 207..209 = a water bullet frozen into an icicle (level 1..3)
#define SHINOBI_ENT_ZONE           210  // 210 + shinobiZone_t; s.angles2[0] = radius,
                                        // s.time = start, s.time2 = end

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
#define SHINOBI_FX_FIRE_GROUND     "shinobi/fire_ground"
#define SHINOBI_FX_FIRE_BODY       "shinobi/fire_body"
#define SHINOBI_FX_STEAM           "shinobi/steam"
#define SHINOBI_FX_STEAM_BURST     "shinobi/steam_burst"
#define SHINOBI_FX_WATER_SPLASH    "shinobi/water_splash"       // + "_1".."_3" per level
#define SHINOBI_FX_WATER_TRAIL     "shinobi/water_trail"
#define SHINOBI_FX_WIND_GUST       "shinobi/wind_gust"          // + "_1".."_3": now the Wind Blade cast
#define SHINOBI_FX_STORM           "shinobi/storm_wind"         // + "_1".."_3" per level
#define SHINOBI_FX_WAVE_CREST      "shinobi/wave_crest"
#define SHINOBI_FX_DRIP            "shinobi/drip"
#define SHINOBI_FX_BUBBLES         "shinobi/bubbles"
#define SHINOBI_FX_ZONE_SPARK      "shinobi/zone_spark"
#define SHINOBI_FX_CURSE           "shinobi/curse_aura"
#define SHINOBI_FX_FLAME           "shinobi/fire_breath"
#define SHINOBI_FX_ASH             "shinobi/ash"
#define SHINOBI_FX_MIST            "shinobi/mist"
#define SHINOBI_FX_QUAKE_DUST      "shinobi/quake_dust"
#define SHINOBI_FX_WIND_EDGE       "shinobi/wind_edge"
#define SHINOBI_FX_FLASH           "shinobi/flash"
#define SHINOBI_FX_HIRAISHIN       "shinobi/hiraishin"
#define SHINOBI_FX_GATES           "shinobi/gates_aura"

/*
---------------------------------------------------------------------------
Ninja build (bg_shinobi.c): chosen on the build screen, stored in the
userinfo cvar "shinobi_build" as "<jutsu,...>/<stat points>/<perk>", e.g.
"suiton,wave,suiro,futon,windblade/2,2,2,2,2/none". Applied on respawn.

  techniques  up to 5, from at most 2 schools (basic ones are free for all)
  stats       10 points minus the special's price; at most 5 in one stat
              (no special: chakra pool, health and taijutsu go up to 7)
  special     one perk, paid for with stat points (shinobiPerkCost)
---------------------------------------------------------------------------
*/
typedef enum {
	SS_BASE,       // everyone: Shunshin, Kawarimi
	SS_KATON,
	SS_SUITON,
	SS_FUTON,
	SS_DOTON,
	SS_RAITON,
	SS_YINYANG,
	SS_COUNT
} shinobiSchool_t;

static const char * const shinobiSchoolTitles[SS_COUNT] = {
	"Basic", "Katon - fire", "Suiton - water", "Futon - wind", "Doton - earth", "Raiton - lightning", "Yin / Yang"
};

static const int shinobiJutsuSchool[SJ_COUNT] = {
	SS_BASE,    // none
	SS_YINYANG, // Rasengan
	SS_RAITON,  // Chidori
	SS_KATON,   // Katon
	SS_DOTON,   // Doton
	SS_YINYANG, // Kage Bunshin
	SS_BASE,    // Shunshin
	SS_BASE,    // Kawarimi
	SS_SUITON,  // Water Bullets
	SS_FUTON,   // Storm Wind
	SS_SUITON,  // Water Prison
	SS_SUITON,  // Water Wave
	SS_FUTON,   // Wind Blade
	SS_KATON,   // Fire Breath
	SS_KATON,   // Ash Cloud
	SS_YINYANG, // Shadow Bind
	SS_SUITON,  // Water Clone
	SS_YINYANG, // Hiraishin
	SS_RAITON,  // Lightning Trap
	SS_SUITON,  // Hidden Mist
	SS_DOTON,   // Earthquake
	SS_FUTON,   // Wind Wall
	SS_FUTON,   // Sky Step
	SS_FUTON,   // Wind Edge
	SS_RAITON,  // Lightning Edge
	SS_DOTON,   // Burrow
	SS_DOTON,   // Rock Pillar
	SS_DOTON,   // Stone Skin
	SS_DOTON,   // Earth Clone
	SS_RAITON,  // Lightning Clone
	SS_RAITON,  // Lightning Dash
	SS_YINYANG, // Shadow Hide
	SS_YINYANG, // Flash
	SS_YINYANG, // Healing
	SS_YINYANG, // Shadow Trap
	SS_YINYANG, // Kaiten
};

// the fixed stock force levels of every player (after "rank-side-"): heal lev
// speed push pull tele grip light rage protect absorb teamheal teamforce drain
// see saberoff saberdef saberthrow - force jump 3, saber 3/3, nothing else
#define SHINOBI_FORCELEVELS     "030000000000000330"

#define SHINOBI_BUILD_JUTSU     5
#define SHINOBI_BUILD_SCHOOLS   2

typedef enum {
	SST_CHAKRA,    // +20 max chakra per point
	SST_HEALTH,    // +15 max health per point
	SST_TAIJUTSU,  // +10% fist, kick and saber damage per point
	SST_CAST,      // charge bars fill 8% faster per point
	SST_REGEN,     // chakra regenerates 12% faster per point
	SST_COUNT
} shinobiStat_t;

#define SHINOBI_STAT_POINTS     10
#define SHINOBI_STAT_MAX        5
#define SHINOBI_STAT_MAX_TRAINED 7   // no special: chakra pool, health, taijutsu go this high

static const char * const shinobiStatTitles[SST_COUNT] = {
	"Chakra pool", "Health", "Taijutsu", "Casting speed", "Chakra regen"
};
static const char * const shinobiStatInfo[SST_COUNT] = {
	"+20 max chakra per point (up to 7 without a special)", "+15 max health per point (up to 7 without a special)",
	"+10% fist, kick and blade damage per point (up to 7 without a special)",
	"charge bars fill 8% faster per point", "chakra comes back 12% faster and fights give 10% more of it, per point"
};

typedef enum {
	SP_NONE,
	SP_CURSE_SEAL,
	SP_SHARINGAN,
	SP_BYAKUGAN,
	SP_GATES,
	SP_COPYEYE,   // Kakashi's Sharingan: no health / rims / aim, reads the jutsu and copies the last one seen
	SP_COUNT
} shinobiPerk_t;

static const char * const shinobiPerkNames[SP_COUNT] = {
	"none", "curseseal", "sharingan", "byakugan", "gates", "copyeye"
};
static const char * const shinobiPerkTitles[SP_COUNT] = {
	"None", "Curse Seal", "Sharingan", "Byakugan", "Inner Gates", "Copy Sharingan"
};
static const char * const shinobiPerkInfo[SP_COUNT] = {
	"no special ability: pure training - all 10 stat points, and chakra pool, health and taijutsu go up to 7",
	"costs 4 stat points. Awakens at 0 chakra and won't sleep until your health is down to 10: chakra returns 5x faster (2x from fighting), your mind clouds; C: on/off",
	"costs 4 stat points. C: toggle (3 chakra/s, no regen): see health, Kawarimi-hidden enemies and the jutsu being charged; red eyes",
	"costs 5 stat points. C: toggle (8 chakra/s, no regen): see chakra and everyone nearby through walls and mist, clones look hollow; Gentle Fist: your fists burn chakra and guards; loud",
	"costs 3 stat points. C: 10 s faster and +50% taijutsu, then 15 s exhausted (slow, no chakra regen); once a minute",
	"costs 4 stat points. C: toggle (6 chakra/s, no regen): no health, rims or aim - you read the jutsu being charged, and the last jutsu you see cast goes into an extra slot of your list (Lv2 at most, 25% dearer) until you copy another or die; close the eye to keep it. Yin / Yang can't be copied"
};
// one line per jutsu for the build screen
static const char * const shinobiJutsuInfo[SJ_COUNT] = {
	"",
	"ball of chakra in the hand; attack = dash strike that knocks down",
	"lightning in the hand; long piercing dash that stuns",
	"great fireball: needs at least 2 bars; huge blast, leaves the ground burning",
	"earth wall; F next to it again pushes it forward",
	"1-3 shadow clones in a cloud of smoke; they drain chakra",
	"Shift + Space: blink where you look",
	"X: a double takes your place, you vanish",
	"1-3 fast water bullets in a burst; small puddles",
	"storm wind: throws people back, fans flames, blows steam away; cast holding back (S): it pulls them in to you",
	"hold F: trap an enemy in a ball of water (drains chakra)",
	"low wave rolling along the floor; clear it with a force jump",
	"wide arc of wind at chest height; jump over it",
	"hold F: breathe a cone of fire, sets people and ground alight (12 chakra/s)",
	"a cloud of hot ash ahead: blinds and chokes; any fire blows it up",
	"hold F: a shadow crawls ahead; whoever it catches copies your moves",
	"a water clone (30 s) that fights and uses the next water jutsu in your list (paid by you)",
	"teleport to your marked kunai (Shift+Z), or behind the enemy it marked",
	"needs two or three marked kunai (Shift+Z): lightning strings between them (a line / a triangle, up to 500 u a side) for 8 s - whoever crosses it (you too) is shocked and stunned",
	"a bank of fog around you: only shapes show inside; wind blows it away, Byakugan sees through",
	"long charge (bigger radius): the ground cracks, then knocks down everyone standing on it and leaves dust (water makes it mud). Jump!",
	"a wall of wind ahead (4-8 s): fireballs, water bullets and kunai fly back at whoever threw them; fireballs grow",
	"passive: jumps and wall runs cost half the chakra, no falling damage",
	"15 s: fists and blade throw air slashes a few metres ahead and cut through guards (not a perfect parry)",
	"15 s: fists and blade hit 20% harder, 3x against earth walls and other stone",
	"hold F: go under the ground, leaving a trail of dust; let go or attack to burst out, launching whoever is above",
	"a wide stone block erupts where you look (on a wall: a ledge out of it), launching whoever is there; stays 90 s, 3 at most; broken, it leaves dust",
	"8 s: 40% less damage, no knockdowns or knockback, a bit slower; Lightning cuts through it",
	"a slow, tough clone of stone (25 s); crumbles into mud",
	"a clone of lightning that runs at the nearest enemy and goes off like a lightning trap: shock and paralysis around it",
	"hold F: become a bolt of lightning racing ahead (steer with the mouse), zapping whoever you pass; 40 chakra/s",
	"hold F: melt into your shadow - only a dark patch moves; blades can't touch it, fire, lightning and light drive you out",
	"a burst of light: whoever looks at you is blinded; flushes out the hidden around you",
	"hold F: chakra turns into health, one for one (20 a second); you walk slowly",
	"shadows reach out from your two newest marked kunai (Shift+Z) to the nearest enemy and hold him in place (up to 15 s, drains chakra); any hit frees him",
	"hold F: half a second to wind up, then you spin inside a dome of chakra (30 chakra a second): blades, fists and jutsu don't get through, missiles glance off, whoever is close is thrown away and knocked down; rooted, no attacks"
};

static const qboolean shinobiPerkReady[SP_COUNT] = { qtrue, qtrue, qtrue, qtrue, qtrue, qtrue };
// a gift is paid for with training: stat points the special takes from the 10
//                                           none  curse  sharingan  byakugan  gates  copyeye
static const int shinobiPerkCost[SP_COUNT] = { 0,    4,     4,         5,        3,     4 };

/*
Copy Sharingan: a jutsu cast by someone else within SHINOBI_COPY_RANGE, in front of the
open eye (within SHINOBI_COPY_FOV) and in plain sight (no walls, no Hidden Mist), goes
into the copy slot - unless it is Yin / Yang, basic (Shunshin, Kawarimi), passive or in
your own build. One slot: the next one seen replaces it. Cast from it: Lv2 at most and
SHINOBI_COPY_COST times the price.
*/
#define SHINOBI_COPYEYE_DRAIN_MS   167     // 6 chakra a second
#define SHINOBI_COPY_RANGE         1200
#define SHINOBI_COPY_FOV           0.5f    // cos of the half angle (60 degrees)
#define SHINOBI_COPY_COST          1.25f
#define SHINOBI_COPY_MAX_LEVEL     2
static QINLINE qboolean Shinobi_Copyable( int jutsu ) {
	return jutsu > SJ_NONE && jutsu < SJ_COUNT && !SHINOBI_JUTSU_PASSIVE( jutsu )
		&& shinobiJutsuSchool[jutsu] != SS_YINYANG && shinobiJutsuSchool[jutsu] != SS_BASE;
}
// the price of a copied jutsu (0 = above the copy's top level)
static QINLINE int Shinobi_CopyCost( int jutsu, int level ) {
	if ( level > SHINOBI_COPY_MAX_LEVEL )
		return 0;
	return (int)( Shinobi_JutsuCost( jutsu, level ) * SHINOBI_COPY_COST + 0.99f );
}

typedef struct {
	int numJutsu;
	int jutsu[SHINOBI_BUILD_JUTSU];
	int stats[SST_COUNT];
	int perk;
} shinobiBuild_t;

void     Shinobi_BuildDefault( shinobiBuild_t *b );
void     Shinobi_BuildParse( const char *str, shinobiBuild_t *b );   // invalid parts are dropped
void     Shinobi_BuildString( const shinobiBuild_t *b, char *out, int size );
qboolean Shinobi_BuildValid( const shinobiBuild_t *b, char *why, int whySize );
void     Shinobi_BuildFix( shinobiBuild_t *b );                       // make it valid by dropping extras
qboolean Shinobi_BuildHas( const shinobiBuild_t *b, int jutsu );
int      Shinobi_BuildSchools( const shinobiBuild_t *b, int *schools ); // distinct schools used
int      Shinobi_BuildPoints( const shinobiBuild_t *b );

// stat points this build may spend (10 minus its special's price)
static QINLINE int Shinobi_StatBudget( const shinobiBuild_t *b ) {
	const int perk = ( b->perk > SP_NONE && b->perk < SP_COUNT ) ? b->perk : SP_NONE;
	return SHINOBI_STAT_POINTS - shinobiPerkCost[perk];
}
// the most points one stat may take: pure training lets the body go further
static QINLINE int Shinobi_StatCap( const shinobiBuild_t *b, int stat ) {
	if ( b->perk == SP_NONE && ( stat == SST_CHAKRA || stat == SST_HEALTH || stat == SST_TAIJUTSU ) )
		return SHINOBI_STAT_MAX_TRAINED;
	return SHINOBI_STAT_MAX;
}

/*
Accessories ("shinobi_acc" userinfo, "acc" in the player configstring):
"name[,scale,fwd,left,up,pitch,yaw,roll];..." - scale in %, offsets in tenths
of a unit along the head's forward / left / up, angles in degrees. A model is
models/accessories/head/<name>.md3, modelled in the _humanoid bind pose; it
rides the "cranium" bone (see Shinobi_AccPlace).
*/
#define SHINOBI_ACC_MAX        4
#define SHINOBI_ACC_STRING     200   // longest "acc" string the server passes on
#define SHINOBI_ACC_SCALE_MIN  40
#define SHINOBI_ACC_SCALE_MAX  250
#define SHINOBI_ACC_OFS_MAX    150   // tenths: 15 units
typedef struct {
	char name[32];
	int  scale;   // %
	int  ofs[3];  // tenths of a unit: forward, left, up
	int  ang[3];  // pitch, yaw, roll
} shinobiAcc_t;

void     Shinobi_AccDefault( shinobiAcc_t *a );
int      Shinobi_AccParse( const char *s, shinobiAcc_t *acc, int max );   // how many
void     Shinobi_AccString( const shinobiAcc_t *acc, int n, char *out, int size );
qboolean Shinobi_GlaBoneInv( const unsigned char *gla, int len, const char *bone, float inv[3][4] );
void     Shinobi_AccPlace( const float bolt[3][4], const float inv[3][4], const shinobiAcc_t *a, float modelScale,
	vec3_t origin, vec3_t axis[3] );

// the charge bars, sped up by the Casting speed stat
static QINLINE int Shinobi_BarEndCast( int jutsu, int bar, int castLvl ) {
	return Shinobi_BarEnd( jutsu, bar ) * ( 100 - 8 * castLvl ) / 100;
}
static QINLINE int Shinobi_ChargeLevelCast( int jutsu, int heldMs, int castLvl ) {
	if ( heldMs >= Shinobi_BarEndCast( jutsu, 2, castLvl ) ) return 3;
	if ( heldMs >= Shinobi_BarEndCast( jutsu, 1, castLvl ) ) return 2;
	return 1;
}


// the build's main nature: its first elemental school
static QINLINE int Shinobi_BuildElement( const shinobiBuild_t *b ) {
	int i;
	for ( i = 0; i < b->numJutsu; i++ ) {
		const int s = shinobiJutsuSchool[b->jutsu[i]];
		if ( s >= SS_KATON && s <= SS_RAITON )
			return s;
	}
	return SS_YINYANG;
}

/*
Weapons ("shinobi_weapon" userinfo, chosen on the weapon screen): fists only, one blade, a
blade in each hand, or a staff. The blades are sabers without a light blade (tools/
weapon2glm.py: ext_data/sabers/shinobi_*.sab, models/weapons2/shinobi_*), named in the
stock "saber1" / "saber2" userinfo; the server takes only those and fixes what doesn't
fit the style. Applied on the next spawn, like the build.
*/
typedef enum {
	SWS_NONE,	// fists only (the blade key does nothing)
	SWS_ONE,	// one blade, right hand
	SWS_TWO,	// a blade in each hand (only the weapons made for it: "shinobiUse two")
	SWS_COUNT
} shinobiWeaponStyle_t;

#define SHINOBI_WEAPON_PREFIX        "shinobi_"
#define SHINOBI_WEAPON_DEFAULT       "shinobi_katana"
#define SHINOBI_WEAPON_DEFAULT_TWO   "shinobi_knife"
/*
Each weapon is held one way only ("shinobiUse one" / "two" in its .sab: one blade, or a pair)
and fights in one stance ("saberStyle" in its .sab) - no stance switching with blades.
*/

extern const char * const shinobiWeaponStyleNames[SWS_COUNT];   // the userinfo words
extern const char * const shinobiWeaponStyleTitles[SWS_COUNT];
extern const char * const shinobiWeaponStyleInfo[SWS_COUNT];
int Shinobi_WeaponStyleParse( const char *s ); // unknown / empty = SWS_ONE
