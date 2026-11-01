/*
===========================================================================
JK_Shinobi - shared (game / cgame / bg) definitions.

State that both client prediction and remote clients need is carried in the
mod-reserved playerState/entityState fields. The engine networks each of
those as a single bit by default, so every flag below is a plain boolean:

  userInt1  chakra charging        (hold USE, stand still)
  userInt2  rasengan in hand       (armed)
  userInt3  rasengan forming       (charging the ball)

  userInt2 && userInt3 together = rasengan dash (strike in progress).

Timers live server-side in gclient_t; only these booleans are sent.
===========================================================================
*/
#pragma once

#define SHINOBI_CHARGING(s)        ((s)->userInt1)
#define SHINOBI_RASENGAN_DASH(s)   ((s)->userInt2 && (s)->userInt3)
#define SHINOBI_RASENGAN_ARMED(s)  ((s)->userInt2 && !(s)->userInt3)
#define SHINOBI_RASENGAN_FORMING(s) (!(s)->userInt2 && (s)->userInt3)
#define SHINOBI_RASENGAN_ANY(s)    ((s)->userInt2 || (s)->userInt3)

// Chakra (the force pool, fd.forcePower)
#define SHINOBI_CHARGE_TICK_MS     50   // chakra gained every tick while charging
#define SHINOBI_CHARGE_PER_TICK    2    // = 40 chakra per second

// Rasengan
#define SHINOBI_RASENGAN_COST      40
#define SHINOBI_RASENGAN_FORM_MS   700  // time to form the ball
#define SHINOBI_RASENGAN_HOLD_MS   6000 // how long it stays in hand
#define SHINOBI_RASENGAN_DASH_MS   350  // strike lunge duration
#define SHINOBI_RASENGAN_DASH_SPEED 700
#define SHINOBI_RASENGAN_DAMAGE    60
#define SHINOBI_RASENGAN_THROW     900  // knockback speed applied to victim
#define SHINOBI_RASENGAN_REACH     44   // hit box distance in front of the user
#define SHINOBI_RASENGAN_HITBOX    28   // half-size of the hit box

// Effect files (inside zz_shinobi.pk3)
#define SHINOBI_FX_RASENGAN_HAND   "shinobi/rasengan_hand"
#define SHINOBI_FX_RASENGAN_FORM   "shinobi/rasengan_form"
#define SHINOBI_FX_RASENGAN_HIT    "shinobi/rasengan_hit"
#define SHINOBI_FX_RASENGAN_FIZZLE "shinobi/rasengan_fizzle"
#define SHINOBI_FX_CHAKRA_AURA     "shinobi/chakra_aura"
