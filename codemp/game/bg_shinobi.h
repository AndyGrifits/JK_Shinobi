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

#define SHINOBI_RASENGAN_COST      40
#define SHINOBI_RASENGAN_FORM_MS   700
#define SHINOBI_RASENGAN_HOLD_MS   6000
#define SHINOBI_RASENGAN_DASH_MS   350
#define SHINOBI_RASENGAN_DASH_SPEED 700
#define SHINOBI_RASENGAN_DAMAGE    60
#define SHINOBI_RASENGAN_THROW     900  // knockback speed applied to victim

#define SHINOBI_CHIDORI_COST       35
#define SHINOBI_CHIDORI_FORM_MS    500
#define SHINOBI_CHIDORI_HOLD_MS    5000
#define SHINOBI_CHIDORI_DASH_MS    320
#define SHINOBI_CHIDORI_DASH_SPEED 1050 // faster and longer than Rasengan...
#define SHINOBI_CHIDORI_DAMAGE     75   // ...hits harder...
#define SHINOBI_CHIDORI_THROW      250  // ...but barely pushes: it pierces
#define SHINOBI_CHIDORI_STUN_MS    900  // victim can't act (electrocuted)

// Shunshin (body flicker): short teleport along the view direction
#define SHINOBI_SHUNSHIN_COST      15
#define SHINOBI_SHUNSHIN_DIST      320
#define SHINOBI_SHUNSHIN_MIN_DIST  48   // don't bother if a wall is right in front
#define SHINOBI_SHUNSHIN_COOLDOWN  600
#define SHINOBI_SHUNSHIN_MAX_PITCH 35   // degrees up/down the blink may follow the view

// Kage Bunshin (shadow clones): NPC copies with Jedi AI, one hit kills them
#define SHINOBI_CLONE_COST         50
#define SHINOBI_CLONE_COUNT        2
#define SHINOBI_CLONE_LIFETIME     25000
#define SHINOBI_CLONE_HEALTH       1
#define SHINOBI_CLONE_SIGHT        1500  // enemy search radius
#define SHINOBI_CLONE_THINK_MS     250   // how often clone targeting is supervised
#define SHINOBI_CLONE_NPC_TYPE     "shinobi_clone" // ext_data/NPCs/shinobi.npc

// Katon: Goukakyuu (great fireball): explosive projectile
#define SHINOBI_FIREBALL_COST      30
#define SHINOBI_FIREBALL_COOLDOWN  900
#define SHINOBI_FIREBALL_SPEED     950
#define SHINOBI_FIREBALL_SIZE      12   // collision half-size
#define SHINOBI_FIREBALL_LIFE      2500
#define SHINOBI_FIREBALL_DAMAGE    35   // direct hit, on top of the blast
#define SHINOBI_FIREBALL_SPLASH    55
#define SHINOBI_FIREBALL_RADIUS    170

// Doton: Doryuheki (earth wall): row of stone pillars rising from the ground
#define SHINOBI_WALL_COST          30
#define SHINOBI_WALL_COOLDOWN      3000
#define SHINOBI_WALL_PILLARS       5
#define SHINOBI_WALL_DISTANCE      96   // in front of the caster
#define SHINOBI_WALL_SPACING       34
#define SHINOBI_WALL_HALF_WIDTH    18   // pillar collision half-size
#define SHINOBI_WALL_HEIGHT        104
#define SHINOBI_WALL_HEALTH        150  // per pillar
#define SHINOBI_WALL_RISE_MS       400
#define SHINOBI_WALL_LIFETIME      12000
#define SHINOBI_WALL_SINK_MS       600
#define SHINOBI_WALL_MODELS        3    // models/shinobi/earth_pillar1..3.md3

// Non-player entities drawn by the cgame (entityState.generic1 on ET_GENERAL)
#define SHINOBI_ENT_FIREBALL       201

// Effect files (inside zz_shinobi.pk3, under effects/)
#define SHINOBI_FX_RASENGAN_HAND   "shinobi/rasengan_hand"
#define SHINOBI_FX_RASENGAN_FORM   "shinobi/rasengan_form"
#define SHINOBI_FX_RASENGAN_HIT    "shinobi/rasengan_hit"
#define SHINOBI_FX_RASENGAN_FIZZLE "shinobi/rasengan_fizzle"
#define SHINOBI_FX_CHIDORI_HAND    "shinobi/chidori_hand"
#define SHINOBI_FX_CHIDORI_FORM    "shinobi/chidori_form"
#define SHINOBI_FX_CHIDORI_HIT     "shinobi/chidori_hit"
#define SHINOBI_FX_CHIDORI_FIZZLE  "shinobi/chidori_fizzle"
#define SHINOBI_FX_CHAKRA_AURA     "shinobi/chakra_aura"
#define SHINOBI_FX_SHUNSHIN        "shinobi/shunshin"
#define SHINOBI_FX_CLONE_POOF      "shinobi/clone_poof"
#define SHINOBI_FX_FIREBALL        "shinobi/fireball"
#define SHINOBI_FX_FIREBALL_BOOM   "shinobi/fireball_explode"
#define SHINOBI_FX_WALL_RISE       "shinobi/earthwall_rise"
#define SHINOBI_FX_WALL_BREAK      "shinobi/earthwall_break"
