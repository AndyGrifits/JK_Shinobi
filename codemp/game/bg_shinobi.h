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
	SJ_COUNT
} shinobiJutsu_t;

#define SHINOBI_MAX_LEVEL          3
#define SHINOBI_BAR1_MS            350  // bar 1 full (level 1 is also what a tap gives)
#define SHINOBI_BAR2_MS            850  // bar 2 full -> level 2
#define SHINOBI_BAR3_MS            1450 // bar 3 full -> level 3

// console command names (also the "+name"/"-name" hold commands on the client)
static const char * const shinobiJutsuNames[SJ_COUNT] = {
	"", "rasengan", "chidori", "katon", "doton", "kagebunshin", "shunshin", "kawarimi",
	"suiton", "futon", "suiro", "wave", "windblade", "flame", "ash", "kagemane", "mizubunshin"
};

// names shown in the HUD
static const char * const shinobiJutsuTitles[SJ_COUNT] = {
	"", "Rasengan", "Chidori", "Katon", "Doton", "Kage Bunshin", "Shunshin", "Kawarimi",
	"Water Bullets", "Storm Wind", "Water Prison", "Water Wave", "Wind Blade", "Fire Breath", "Ash Cloud", "Shadow Bind", "Water Clone"
};

// the jutsu the selection keys cycle through (Shunshin and Kawarimi have their own keys)
static const int shinobiJutsuCycle[] = {
	SJ_KATON, SJ_FLAME, SJ_ASH, SJ_SUITON, SJ_WAVE, SJ_SUIRO, SJ_WATERCLONE, SJ_FUTON, SJ_BLADE, SJ_DOTON, SJ_CHIDORI,
	SJ_RASENGAN, SJ_BUNSHIN, SJ_SHADOW
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
	{ 20, 30, 40 }, // Shunshin
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
};

// jutsu that cast on key press, without charging
#define SHINOBI_JUTSU_INSTANT(j)   ( (j) == SJ_KAWARIMI || (j) == SJ_WATERCLONE )
// jutsu that last while the key is held, without levels
#define SHINOBI_JUTSU_HOLD(j)      ( (j) == SJ_SUIRO || (j) == SJ_FLAME || (j) == SJ_SHADOW )
// the lowest level a jutsu can be cast at (the great fireball needs a long charge)
#define SHINOBI_JUTSU_MIN_LEVEL(j) ( (j) == SJ_KATON ? 2 : 1 )

// when each bar is full; Shunshin charges faster than the big jutsu
static QINLINE int Shinobi_BarEnd( int jutsu, int bar ) {
	static const int normal[SHINOBI_MAX_LEVEL]   = { SHINOBI_BAR1_MS, SHINOBI_BAR2_MS, SHINOBI_BAR3_MS };
	static const int shunshin[SHINOBI_MAX_LEVEL] = { 200, 500, 900 };
	if ( bar < 0 ) bar = 0;
	if ( bar >= SHINOBI_MAX_LEVEL ) bar = SHINOBI_MAX_LEVEL - 1;
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
#define SHINOBI_SHUNSHIN_COOLDOWN  5000

// Kawarimi (substitution): an AI double takes your place, you turn invisible
// and creep away; any action but moving ends it and the double becomes a log
#define SHINOBI_KAWARIMI_COOLDOWN  10000
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
#define SHINOBI_MUD_MS             12000
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
static const int   shinobiPuddleLife[3]   = { 6000, 7000, 8000 };
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
#define SHINOBI_FLAME_TURN         110.0f // degrees per second
#define SHINOBI_FLAME_TICK_MS      150
#define SHINOBI_FLAME_DAMAGE       3      // per tick (20/s)
#define SHINOBI_FLAME_DRAIN_MS     250
#define SHINOBI_FLAME_DRAIN        3      // chakra per drain tick (12/s)
#define SHINOBI_FLAME_ZONE_MS      400    // a patch of burning ground this often
#define SHINOBI_FLAME_SLOW         0.4f

// Ash Cloud: a cloud of hot ash in front; blinds and chokes, and any fire
// (fireball, fire breath, burning ground, Chidori) blows it up.
static const int shinobiAshRadius[3]  = { 110, 150, 190 };
static const int shinobiAshBlast[3]   = {  35,  50,  70 };
#define SHINOBI_ASH_LIFE           9000
#define SHINOBI_ASH_DISTANCE       200
#define SHINOBI_ASH_DAMAGE         1      // choking, per zone tick

// Shadow Bind: held; a shadow crawls forward along the ground. An enemy standing
// on it is caught and copies every move you make (attacks included) until you
// let go. Jump over the shadow to dodge.
#define SHINOBI_SHADOW_SPEED       450
#define SHINOBI_SHADOW_RANGE       600
#define SHINOBI_SHADOW_HOLD_RANGE  900
#define SHINOBI_SHADOW_WIDTH       28
#define SHINOBI_SHADOW_DRAIN_MS    300
#define SHINOBI_SHADOW_DRAIN       3      // chakra per drain tick (10/s)
#define SHINOBI_BUTTON_MIRROR      (1<<14) // usercmd bit: moves set by the server (Shadow Bind)

// Water Clone: one clone made of water that fights like a shadow clone and casts,
// on its own, the jutsu that comes after Water Clone in your build (Water Prison:
// it holds the prison for you). Its jutsu cost your chakra. One hit: a puddle.
#define SHINOBI_WATERCLONE_LIFE    20000
#define SHINOBI_WATERCLONE_CAST_MS 3000  // between its casts
#define SHINOBI_WATERCLONE_HOLD_MS 6000  // longest it holds a prison / bind
#define SHINOBI_WATERCLONE_BREATH_MS 2500

// After any hard crowd control ends, a short immunity to the next one
#define SHINOBI_CC_IMMUNE_MS       2500

// Extra predicted status bits that only the player himself needs
#define STAT_SHINOBI               12    // ps.stats[] slot (unused by the base game)
#define SHINOBI_STAT_PUDDLE        1     // standing in a puddle
#define SHINOBI_STAT_CURSE         2     // Curse Seal awakened
#define SHINOBI_STAT_CHANNEL       4     // breathing fire: slowed
#define SHINOBI_STAT_BOUND         8     // caught by a Shadow Bind (moves come from the server)

// Futon: Daitoppa (wind gust): cone in front, pushes, fans flames
#define SHINOBI_WIND_COOLDOWN      1500
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
#define SHINOBI_ENT_WATERBALL      204  // 204..206 = water bullet level 1..3
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

/*
---------------------------------------------------------------------------
Ninja build (bg_shinobi.c): chosen on the build screen, stored in the
userinfo cvar "shinobi_build" as "<jutsu,...>/<stat points>/<perk>", e.g.
"suiton,wave,suiro,futon,windblade/2,2,2,2,2/none". Applied on respawn.

  techniques  up to 5, from at most 2 schools (basic ones are free for all)
  stats       10 points, at most 5 in one stat
  special     one perk
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
};

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

static const char * const shinobiStatTitles[SST_COUNT] = {
	"Chakra pool", "Health", "Taijutsu", "Casting speed", "Chakra regen"
};
static const char * const shinobiStatInfo[SST_COUNT] = {
	"+20 max chakra per point", "+15 max health per point", "+10% fist, kick and blade damage per point",
	"charge bars fill 8% faster per point", "chakra comes back 12% faster per point"
};

typedef enum {
	SP_NONE,
	SP_GENTLE_FIST,
	SP_CURSE_SEAL,
	SP_SHARINGAN,
	SP_BYAKUGAN,
	SP_GATES,
	SP_COUNT
} shinobiPerk_t;

static const char * const shinobiPerkNames[SP_COUNT] = {
	"none", "gentlefist", "curseseal", "sharingan", "byakugan", "gates"
};
static const char * const shinobiPerkTitles[SP_COUNT] = {
	"None", "Gentle Fist", "Curse Seal", "Sharingan", "Byakugan", "Inner Gates"
};
static const char * const shinobiPerkInfo[SP_COUNT] = {
	"no special ability",
	"your fists and kicks also burn the enemy's chakra",
	"awakens at 0 chakra and won't sleep until your health is down to 10: chakra returns 2.5x faster; /curseseal off",
	"coming soon: see health, hidden enemies and the jutsu being charged",
	"coming soon: see chakra and enemies through walls",
	"coming soon: a burst of speed and taijutsu, then exhaustion"
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
	"storm wind: throws people back, fans flames, blows steam away",
	"hold F: trap an enemy in a ball of water (drains chakra)",
	"low wave rolling along the floor; clear it with a force jump",
	"wide arc of wind at chest height; jump over it",
	"hold F: breathe a cone of fire, sets people and ground alight (12 chakra/s)",
	"a cloud of hot ash ahead: blinds and chokes; any fire blows it up",
	"hold F: a shadow crawls ahead; whoever it catches copies your moves",
	"a clone of water that fights and casts the jutsu after it in your list (paid by you)"
};

static const qboolean shinobiPerkReady[SP_COUNT] = { qtrue, qtrue, qtrue, qfalse, qfalse, qfalse };

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

// the charge bars, sped up by the Casting speed stat
static QINLINE int Shinobi_BarEndCast( int jutsu, int bar, int castLvl ) {
	return Shinobi_BarEnd( jutsu, bar ) * ( 100 - 8 * castLvl ) / 100;
}
static QINLINE int Shinobi_ChargeLevelCast( int jutsu, int heldMs, int castLvl ) {
	if ( heldMs >= Shinobi_BarEndCast( jutsu, 2, castLvl ) ) return 3;
	if ( heldMs >= Shinobi_BarEndCast( jutsu, 1, castLvl ) ) return 2;
	return 1;
}

