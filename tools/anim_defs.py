"""JK_Shinobi: our animations for tools/make_anims.py.

Each: ref = (stock animation, frame in it) the poses are built on; frames, fps, loop
(as animation.cfg: -1 = plays once - held on its last frame while the game keeps it on;
0 = loops);
keys = [(time 0..1, pose[, ease])]. An empty pose = the reference frame as it is. Each key
is solved to a full pose; between keys every joint turns (slerp) - ease = how the motion into
that key goes: smooth (default), linear, fast (snaps, settles: a strike), slow (winds up), snap
(past the key ~10 % and back: a blow with weight). lag = {part: frames or (frames, from time)} -
head / larm / rarm / lleg / rleg / spine run that many frames late (+) or early (-) on the rest
(a leg standing on the floor earlier: lag it only from when it leaves). plant = ("r",).

Pose keys (model space: +X the model's left, -Y forward, +Z up; units ~ inches):
  body_yaw      deg, the whole body about the pelvis (+ turns the chest to its left)
  pelvis        (x, y, z) moved
  spine_bend    deg forward over the three spine joints; spine_side; spine_twist (about up)
  head          (yaw, pitch down)
  body_pitch    deg, the whole body about the pelvis, forward (+: head down); body_roll
  turn          deg, the finished pose turned whole about the pelvis (+ = to the model's left)
  lleg/rleg     (direction, reach 0..1) the foot from the hip, lknee/rknee a pole from the hip;
                ltoes/rtoes where the toes point
  lhand_at/rhand_at  (x, y, z) the wrist, absolute (a hand planted on the floor ~-22)
  lgrip/rgrip   (blade direction, palm direction): a held weapon's blade along the first (the
                fist's axis = the hand tag's +Z, where the game puts the blade)
  (an animation's plant=("r",): that foot stays on the floor through the blends between keys)
  lfoot/rfoot   (x, y, z) from where that foot is in the reference frame; lknee/rknee the
                knee's pole direction; lfoot_yaw/rfoot_yaw deg (+ = toes to the model's left)
  larm/rarm     (direction, reach 0..1 of the arm's length - 1 = straight), from the shoulder
  lhand/rhand   (x, y, z) the wrist, from the shoulder (as the body / spine left it);
                lelbow/relbow a point the elbow bends towards, also from the shoulder
  (the reference stand: pelvis z ~9, shoulders ~27, head top ~37, feet ~-20, floor ~-24)
  lpalm/rpalm   (finger direction, palm direction); lcurl/rcurl 0 flat .. 1 fist
"""

STAND = ("BOTH_STAND1", 0)

# Neji's Eight Trigrams stance (64 Seals): a deep side-on lunge, the body leaning in, both
# arms straight in one line through the shoulders - the front one down towards the floor,
# the back one up and behind - palms open; eases in from the stand in 0.5 s and holds
HYUGA = {
    "body_yaw": -50.0,
    "pelvis": (3.8, -4.6, -17.0),
    "spine_bend": 0.0, "spine_side": -24.0,
    "head": (40.0, 14.0),
    "lfoot": (6.8, -11.3, 0.0), "lknee": (14.0, -16.0, 0.0), "lfoot_yaw": 20.0,
    "rfoot": (-12.2, 14.6, 0.0), "rknee": (-16.0, -8.0, 0.0), "rfoot_yaw": -70.0,
    "larm": ((0.15, -0.65, -0.74), 0.995),
    "lpalm": ((0.15, -0.65, -0.74), (0.2, -0.5, 0.8)), "lcurl": 0.0,
    "rarm": ((-0.28, 0.57, 0.77), 0.995),
    "rpalm": ((-0.28, 0.57, 0.77), (-0.6, -0.7, 0.2)), "rcurl": 0.0,
}

# Reverse Lotus, the kick up: down into a crouch, the right hand back to the floor, the body
# turns and leans back onto it on its side (the weight on that hand and the right foot), and the
# left leg goes straight up in front - the turned hips let it reach its highest; held
CROUCH = {
    "pelvis": (0.0, 3.0, -15.0), "spine_bend": 24.0, "head": (0.0, 8.0),
    "lfoot": (0.0, 0.0, 0.0), "lknee": (6.0, -20.0, 0.0),
    "rfoot": (0.0, 0.0, 0.0), "rknee": (-6.0, -20.0, 0.0),
    "rarm": ((-0.3, 0.7, -0.65), 0.95), "relbow": (-10.0, 10.0, 0.0),
    "larm": ((0.4, -0.6, -0.7), 0.8), "lelbow": (12.0, 0.0, -6.0),
}
COIL = {
    "turn": -35.0, "pelvis": (0.0, 4.0, -17.0), "spine_bend": 30.0, "head": (0.0, 4.0),
    "rfoot": (0.0, 0.0, 0.0), "rknee": (-6.0, -20.0, 0.0),
    "lleg": ((0.15, -0.55, -0.35), 0.45), "lknee": (4.0, -20.0, 10.0),
    "rarm": ((-0.35, 0.65, -0.68), 1.0), "relbow": (-10.0, 10.0, 0.0),
    "rpalm": ((0.0, 1.0, 0.0), (0.0, 0.0, -1.0)),
    "larm": ((0.55, -0.45, -0.4), 0.75), "lelbow": (12.0, 0.0, -6.0),
}
KICK_UP = {
    "turn": -90.0,  # (built rolled onto the right side, then turned: the leg kicks forward)
    "body_roll": -62.0, "body_pitch": -12.0, "pelvis": (0.0, 3.0, -18.5), "spine_bend": 8.0, "head": (0.0, 10.0),
    "rfoot": (-2.0, -5.0, 0.0), "rknee": (-10.0, -18.0, 8.0),
    "lleg": ((0.3, 0.0, 1.0), 1.0), "lknee": (0.0, -20.0, 0.0),
    "rhand_at": (-22.5, 7.0, -22.0), "relbow": (-14.0, 8.0, 0.0),
    "rpalm": ((-1.0, 0.0, 0.0), (0.0, 0.0, -1.0)),
    "larm": ((0.45, -0.55, 0.7), 0.9), "lelbow": (12.0, 4.0, -8.0),
}

# Straight as a string: feet together, arms down along the body (Reverse Lotus up in the air)
STRAIGHT = {
    "lfoot": (-3.5, 1.5, 0.0), "lknee": (0.0, -20.0, 0.0),
    "rfoot": (3.0, -1.5, 0.0), "rknee": (0.0, -20.0, 0.0),
    "larm": ((0.12, 0.02, -1.0), 1.0), "lelbow": (4.0, 10.0, -8.0),
    "rarm": ((-0.12, 0.02, -1.0), 1.0), "relbow": (-4.0, 10.0, -8.0),
    "lpalm": ((0.0, 0.0, -1.0), (-1.0, 0.0, 0.0)), "lcurl": 0.0,
    "rpalm": ((0.0, 0.0, -1.0), (1.0, 0.0, 0.0)), "rcurl": 0.0,
    "head": (0.0, 0.0),
}

# Hand seals (charging a jutsu): both hands together before the chest, cycling four seals of the
# twelve - tiger (palms together, index + middle up), snake (hands clasped, fingers interlaced),
# dog (left palm flat on the right fist), ram (index + middle up, the hands crossed a little).
# The skeleton has two finger pairs a hand (index + middle, ring + little): curl = (pair 1, pair 2).
# Hands from their shoulders; the chest's middle ~ (-1.3, -9, 21) in the stand.
def _seal(lhand, rhand, lpalm, rpalm, lcurl, rcurl, lelbow=(17.0, 3.0, -5.0), relbow=(-17.0, 3.0, -5.0)):
    return {"head": (0.0, 10.0), "spine_bend": 4.0,
            "lhand": lhand, "rhand": rhand, "lelbow": lelbow, "relbow": relbow,
            "lpalm": lpalm, "rpalm": rpalm, "lcurl": lcurl, "rcurl": rcurl}

SEAL_TIGER = _seal((-5.0, -6.5, -5.0), (5.4, -6.5, -5.7),
                   ((0.0, -0.15, 1.0), (-1.0, 0.0, 0.0)), ((0.0, -0.15, 1.0), (1.0, 0.0, 0.0)),
                   (0.0, 1.0), (0.0, 1.0))
SEAL_SNAKE = _seal((-5.0, -7.0, -6.5), (5.4, -7.0, -7.2),
                   ((0.0, -0.75, 0.65), (-1.0, 0.0, 0.0)), ((0.0, -0.75, 0.65), (1.0, 0.0, 0.0)),
                   (0.75, 0.85), (0.75, 0.85))
SEAL_DOG = _seal((-6.7, -7.5, -6.6), (6.0, -7.0, -10.0),
                 ((-0.35, -1.0, 0.0), (0.0, 0.0, -1.0)), ((0.0, -1.0, 0.0), (0.0, 0.0, -1.0)),
                 (0.0, 0.0), (1.0, 1.0))
SEAL_RAM = _seal((-4.6, -7.5, -4.0), (5.8, -6.0, -6.0),
                 ((-0.25, -0.4, 1.0), (-0.9, -0.3, 0.0)), ((0.25, -0.2, 1.0), (0.9, -0.3, 0.0)),
                 (0.0, 1.0), (0.2, 1.0))

# One-handed seals (a single blade in the right hand): the left hand alone before the chin, like
# Haku's - index + middle up (palm to the side), the palm out flat, then the fist with the two
# fingers up again turned; the right arm and its blade stay in the medium stance.
STAND_BLADE = ("BOTH_STAND2", 0)
def _seal1(lhand, lpalm, lcurl):
    return {"head": (0.0, 6.0), "lhand": lhand, "lelbow": (16.0, 2.0, -6.0), "lpalm": lpalm, "lcurl": lcurl}

SEAL1_TWO = _seal1((-4.5, -11.0, -0.5), ((0.0, -0.1, 1.0), (-1.0, 0.0, 0.0)), (0.0, 1.0))
SEAL1_FLAT = _seal1((-4.0, -12.0, -1.5), ((0.0, 0.0, 1.0), (0.0, -1.0, 0.0)), (0.0, 0.0))
SEAL1_TURN = _seal1((-5.0, -11.5, -2.5), ((-0.45, -0.2, 0.85), (-0.6, -0.75, 0.0)), (0.1, 1.0))

# The heavy single blade at rest: on the right shoulder, sloping up and back over it, the right
# hand out to the side on the grip (about shoulder height, a little forward); the left arm hangs
# loose. Breathes (a slow dip of the body) while it lasts.
SHOULDER_REST = {
    "pelvis": (0.0, 0.0, -0.5),
    "lfoot": (2.0, 0.0, 0.0), "lknee": (8.0, -20.0, 0.0), "lfoot_yaw": 8.0,
    "rfoot": (-2.0, 0.0, 0.0), "rknee": (-8.0, -20.0, 0.0), "rfoot_yaw": -8.0,
    "spine_bend": 3.0, "head": (0.0, 2.0),
    "rhand": (-5.0, -7.0, -5.0), "relbow": (-15.0, 2.0, -12.0),
    "rgrip": ((0.08, 0.68, 0.73), (0.6, -0.2, -0.7)), "rcurl": 1.0,
    "larm": ((0.25, 0.05, -1.0), 0.94), "lelbow": (8.0, 6.0, -10.0),
    "lpalm": ((0.1, 0.0, -1.0), (-1.0, 0.0, 0.0)), "lcurl": (0.25, 0.35),
}
SHOULDER_REST_IN = dict(SHOULDER_REST, pelvis=(0.0, 0.0, -1.2), spine_bend=5.0, head=(0.0, 4.0))

# Chidori charging (the full charge, rooted): a wide low stance, the chest turned a little to the
# right so the left arm can go down and forward, its hand open like a claw - the lightning
# gathers there - and the right hand gripping the left forearm to steady it; the head up
# watching the target. First a seal (tiger) at the chest, then down into it.
CHIDORI = {
    "body_yaw": -18.0, "pelvis": (0.0, 2.0, -7.0),
    "spine_bend": 14.0, "head": (16.0, -10.0),
    "lfoot": (4.0, -6.0, 0.0), "lknee": (10.0, -18.0, 0.0), "lfoot_yaw": 15.0,
    "rfoot": (-4.0, 6.0, 0.0), "rknee": (-10.0, -14.0, 0.0), "rfoot_yaw": -25.0,
    "larm": ((0.05, -0.62, -0.78), 0.97), "lelbow": (8.0, 4.0, -6.0),
    "lpalm": ((0.05, -0.62, -0.78), (0.0, -0.6, 0.8)), "lcurl": (0.35, 0.35),
    "rhand_at": (4.5, -11.0, 14.5), "relbow": (-14.0, 4.0, -10.0),
    "rpalm": ((0.95, -0.1, -0.2), (0.0, 0.3, -0.95)), "rcurl": (0.8, 0.9),
}
CHIDORI_IN = dict(CHIDORI, pelvis=(0.0, 2.5, -8.5), spine_bend=17.0)

# Katon (the great fireball) charging - walkable, slowly: the tiger seal at the chest, then the
# right hand up to the mouth, its fingers curled into a ring at the lips, the left arm down, and
# a deep breath in - the chest up and back, the head tipped back.
KATON_MOUTH = {
    "head": (0.0, -4.0), "spine_bend": -2.0,
    "rhand_at": (-4.5, -8.5, 28.5), "relbow": (-16.0, 0.0, -12.0),
    "rpalm": ((0.6, 0.75, 0.15), (0.0, 0.2, -1.0)), "rcurl": (1.0, 1.0),
    "larm": ((0.3, 0.05, -1.0), 0.95), "lelbow": (8.0, 6.0, -10.0),
    "lpalm": ((0.1, 0.0, -1.0), (-1.0, 0.0, 0.0)), "lcurl": (0.5, 0.6),
}
KATON_INHALE = dict(KATON_MOUTH, spine_bend=-11.0, head=(0.0, -16.0), pelvis=(0.0, 0.5, 0.8),
                    rhand_at=(-4.0, -7.5, 30.5))

# Fencing (the thrusting needle blade, shinobiThrust - Nuibari): side-on en garde, the right foot
# forward, knees bent, the sword arm forward with the point at the eyes, the left arm up behind;
# the attacks are thrusts and lunges from it, back to it each time (no swinging the blade round).
# (body_yaw + turns the chest to its left: the right side comes forward)
EN_GARDE = {
    "body_yaw": 62.0, "pelvis": (0.0, 0.0, -5.0), "spine_bend": 4.0, "head": (-52.0, 2.0),
    "rfoot": (2.0, -12.0, 0.0), "rknee": (-4.0, -20.0, 0.0), "rfoot_yaw": -5.0,
    "lfoot": (-2.0, 9.0, 0.0), "lknee": (14.0, -12.0, 0.0), "lfoot_yaw": 70.0,
    "rarm": ((-0.1, -0.95, -0.12), 0.78), "relbow": (-6.0, 2.0, -16.0),
    "rgrip": ((0.0, -0.97, 0.2), (0.9, 0.0, 0.35)), "rcurl": 1.0,
    "larm": ((0.55, 0.45, 0.45), 0.62), "lelbow": (14.0, 6.0, -2.0),
    "lpalm": ((0.1, -0.2, 1.0), (0.0, -1.0, 0.0)), "lcurl": (0.25, 0.4),
}
EN_GARDE_IN = dict(EN_GARDE, pelvis=(0.0, 0.5, -5.8), spine_bend=5.5)
# the arm first: the point goes out, the feet still
EXTEND = dict(EN_GARDE, rarm=((-0.02, -0.98, -0.12), 0.98), relbow=(-6.0, 0.0, -18.0),
              rgrip=((0.0, -0.99, 0.1), (0.85, 0.0, 0.5)))
# the lunge: the front foot far forward, the back leg straight, the body low, the arm and the
# blade one line at the chest, the left arm flung down and back
LUNGE = {
    "body_yaw": 68.0, "pelvis": (0.0, -12.0, -12.5), "spine_bend": 8.0, "head": (-58.0, -2.0),
    "rfoot": (2.0, -32.0, 0.0), "rknee": (-4.0, -20.0, 6.0), "rfoot_yaw": -5.0,
    "lfoot": (-3.0, 16.0, 0.0), "lknee": (14.0, -6.0, 0.0), "lfoot_yaw": 80.0,
    "rarm": ((-0.03, -0.99, 0.05), 1.0), "relbow": (-4.0, 0.0, -18.0),
    "rgrip": ((0.0, -0.995, 0.04), (0.8, 0.0, 0.6)), "rcurl": 1.0,
    "larm": ((0.35, 0.72, -0.6), 0.99), "lelbow": (8.0, 10.0, 6.0),
    "lpalm": ((0.35, 0.72, -0.6), (1.0, 0.0, 0.0)), "lcurl": (0.0, 0.1),
}
# a quick thrust high (a half step, at the head), low (down at the legs)
THRUST_HIGH = dict(EXTEND, pelvis=(0.0, -6.0, -6.0), rfoot=(2.0, -20.0, 0.0), spine_bend=6.0,
                   rarm=((-0.03, -0.98, 0.15), 1.0), rgrip=((0.0, -0.98, 0.18), (0.85, 0.0, 0.5)),
                   head=(-44.0, -6.0))
THRUST_LOW = dict(EXTEND, pelvis=(0.0, -8.0, -11.0), rfoot=(2.0, -22.0, 0.0), spine_bend=18.0,
                  rarm=((-0.05, -0.92, -0.38), 1.0), rgrip=((0.0, -0.94, -0.33), (0.85, 0.0, 0.5)),
                  head=(-44.0, 12.0))
# the cuts (strafing: a flick of the wrist across, the sabre's way): the point up and over to one
# side, then across and down to the other, back to guard
CUT_WIND_R = dict(EN_GARDE, rarm=((-0.35, -0.75, 0.2), 0.85), relbow=(-14.0, 2.0, -10.0),
                  rgrip=((-0.45, -0.35, 0.82), (0.5, -0.6, 0.0)))
CUT_DOWN_L = dict(EXTEND, rarm=((0.3, -0.92, -0.25), 0.98), relbow=(-8.0, 0.0, -16.0),
                  rgrip=((0.6, -0.7, -0.38), (0.0, -0.3, -0.95)), pelvis=(0.0, -5.0, -7.0))
CUT_WIND_L = dict(EN_GARDE, rarm=((0.25, -0.85, 0.3), 0.8), relbow=(-10.0, 2.0, -14.0),
                  rgrip=((0.4, -0.4, 0.82), (0.6, 0.0, -0.8)))
CUT_DOWN_R = dict(EXTEND, rarm=((-0.4, -0.88, -0.25), 0.98), relbow=(-12.0, 0.0, -14.0),
                  rgrip=((-0.65, -0.65, -0.38), (0.0, -0.3, 0.95)), pelvis=(0.0, -5.0, -7.0))
# the fleche (its own finisher, with a dash): thrown forward off the front foot, the back leg
# coming through, everything behind the point
FLECHE = dict(LUNGE, body_pitch=14.0, pelvis=(0.0, -16.0, -6.0), spine_bend=6.0,
              lfoot=(-1.0, -6.0, 8.0), lknee=(6.0, -20.0, 6.0), rfoot=(2.0, -26.0, 0.0),
              rarm=((-0.03, -0.97, 0.22), 1.0), rgrip=((0.0, -0.97, 0.22), (0.8, 0.0, 0.6)))
# a feint: the point dips and comes back (the moves between the thrusts)
FEINT = dict(EN_GARDE, rarm=((-0.08, -0.9, -0.45), 0.78), rgrip=((0.05, -0.9, -0.05), (0.9, 0.0, 0.35)))

ANIMS = {
    "BOTH_SHINOBI_STANCE_HYUGA": dict(ref=STAND, frames=11, fps=20, loop=-1,
                                      keys=[(0.0, {}), (1.0, HYUGA)]),
    # 0.8 s at 30 fps: down, a coil (the knee drawn up, the hand going back, the turn starting),
    # the kick snapping up past its line and settling; the head leads, the left arm and the
    # kicking leg trail the body a frame or two; the right foot stays planted
    # 1.6 s, looping while a jutsu is charged: each seal held a little, snapped to the next
    "BOTH_SHINOBI_SEALS": dict(ref=STAND, frames=33, fps=20, loop=0,
                               keys=[(0.0, SEAL_TIGER), (0.15, SEAL_TIGER), (0.25, SEAL_SNAKE, "fast"),
                                     (0.4, SEAL_SNAKE), (0.5, SEAL_DOG, "fast"), (0.65, SEAL_DOG),
                                     (0.75, SEAL_RAM, "fast"), (0.9, SEAL_RAM), (1.0, SEAL_TIGER, "fast")]),
    # 1.2 s, looping: the free left hand's seals (a single blade held in the right)
    "BOTH_SHINOBI_SEALS_1H": dict(ref=STAND_BLADE, frames=25, fps=20, loop=0,
                                  keys=[(0.0, SEAL1_TWO), (0.2, SEAL1_TWO), (0.33, SEAL1_FLAT, "fast"),
                                        (0.53, SEAL1_FLAT), (0.66, SEAL1_TURN, "fast"), (0.86, SEAL1_TURN),
                                        (1.0, SEAL1_TWO, "fast")]),
    # 2 s, looping: the heavy single blade's idle - on the shoulders behind the head
    "BOTH_SHINOBI_SHOULDER_REST": dict(ref=STAND, frames=41, fps=20, loop=0,
                                       keys=[(0.0, SHOULDER_REST), (0.5, SHOULDER_REST_IN), (1.0, SHOULDER_REST)]),
    "BOTH_SHINOBI_STRAIGHT": dict(ref=STAND, frames=2, fps=20, loop=-1,
                                  keys=[(0.0, STRAIGHT), (1.0, STRAIGHT)]),
    # 1.4 s, held: a seal, down into the stance, the lightning gathering in the left hand
    "BOTH_SHINOBI_CHIDORI_CHARGE": dict(ref=STAND, frames=29, fps=20, loop=-1,
                                        keys=[(0.0, SEAL_TIGER), (0.15, SEAL_TIGER), (0.38, CHIDORI, "fast"),
                                              (0.7, CHIDORI_IN), (1.0, CHIDORI_IN)]),
    # 1.4 s, held: the tiger seal, the hand to the mouth, the long breath in
    "BOTH_SHINOBI_KATON_INHALE": dict(ref=STAND, frames=29, fps=20, loop=-1,
                                      keys=[(0.0, SEAL_TIGER), (0.2, SEAL_TIGER), (0.38, KATON_MOUTH, "fast"),
                                            (1.0, KATON_INHALE, "slow")],
                                      lag={"head": 1}),
    # the needle blade's fencing (all from and back to the en garde; the attack moves' times)
    "BOTH_SHINOBI_FENCE_STANCE": dict(ref=STAND, frames=41, fps=20, loop=0,
                                      keys=[(0.0, EN_GARDE), (0.5, EN_GARDE_IN), (1.0, EN_GARDE)]),
    "BOTH_SHINOBI_FENCE_LUNGE": dict(ref=STAND, frames=21, fps=30, loop=-1,
                                     keys=[(0.0, EN_GARDE), (0.18, EXTEND, "fast"), (0.42, LUNGE, "snap"),
                                           (0.6, LUNGE), (1.0, EN_GARDE)],
                                     lag={"larm": 2, "lleg": 1}),
    "BOTH_SHINOBI_FENCE_HIGH": dict(ref=STAND, frames=15, fps=30, loop=-1,
                                    keys=[(0.0, EN_GARDE), (0.35, THRUST_HIGH, "snap"), (0.55, THRUST_HIGH),
                                          (1.0, EN_GARDE)]),
    "BOTH_SHINOBI_FENCE_LOW": dict(ref=STAND, frames=15, fps=30, loop=-1,
                                   keys=[(0.0, EN_GARDE), (0.35, THRUST_LOW, "snap"), (0.55, THRUST_LOW),
                                         (1.0, EN_GARDE)]),
    "BOTH_SHINOBI_FENCE_CUT_L": dict(ref=STAND, frames=15, fps=30, loop=-1,
                                     keys=[(0.0, EN_GARDE), (0.25, CUT_WIND_R, "slow"), (0.55, CUT_DOWN_L, "fast"),
                                           (1.0, EN_GARDE)]),
    "BOTH_SHINOBI_FENCE_CUT_R": dict(ref=STAND, frames=15, fps=30, loop=-1,
                                     keys=[(0.0, EN_GARDE), (0.25, CUT_WIND_L, "slow"), (0.55, CUT_DOWN_R, "fast"),
                                           (1.0, EN_GARDE)]),
    "BOTH_SHINOBI_FENCE_FLECHE": dict(ref=STAND, frames=24, fps=30, loop=-1,
                                      keys=[(0.0, EN_GARDE), (0.15, EXTEND, "fast"), (0.45, FLECHE, "snap"),
                                            (0.65, LUNGE), (1.0, EN_GARDE)],
                                      lag={"larm": 2, "lleg": 2}),
    "BOTH_SHINOBI_FENCE_FEINT": dict(ref=STAND, frames=6, fps=30, loop=-1,
                                     keys=[(0.0, EN_GARDE), (0.45, FEINT), (1.0, EN_GARDE)]),
    "BOTH_SHINOBI_KICK_UP": dict(ref=STAND, frames=25, fps=30, loop=-1,
                                 keys=[(0.0, {}), (0.28, CROUCH), (0.44, COIL, "slow"),
                                       (0.62, KICK_UP, "snap"), (1.0, KICK_UP)],
                                 lag={"head": -1, "larm": 2, "lleg": (1.5, 0.44)},
                                 plant=("r",)),
}
