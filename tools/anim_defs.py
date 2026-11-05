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
def _seal(lhand, rhand, lpalm, rpalm, lcurl, rcurl, lelbow=(11.0, 5.0, -9.0), relbow=(-11.0, 5.0, -9.0)):
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
    "BOTH_SHINOBI_STRAIGHT": dict(ref=STAND, frames=2, fps=20, loop=-1,
                                  keys=[(0.0, STRAIGHT), (1.0, STRAIGHT)]),
    "BOTH_SHINOBI_KICK_UP": dict(ref=STAND, frames=25, fps=30, loop=-1,
                                 keys=[(0.0, {}), (0.28, CROUCH), (0.44, COIL, "slow"),
                                       (0.62, KICK_UP, "snap"), (1.0, KICK_UP)],
                                 lag={"head": -1, "larm": 2, "lleg": (1.5, 0.44)},
                                 plant=("r",)),
}
