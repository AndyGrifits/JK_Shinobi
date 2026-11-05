"""JK_Shinobi: our animations for tools/make_anims.py.

Each: ref = (stock animation, frame in it) the poses are built on; frames, fps, loop
(as animation.cfg: -1 = plays once - held on its last frame while the game keeps it on;
0 = loops);
keys = [(time 0..1, pose[, ease])]. An empty pose = the reference frame as it is. Each key
is solved to a full pose; between keys every joint turns (slerp) - ease = how the motion into
that key goes: smooth (default), linear, fast (snaps, settles: a strike), slow.

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
KICK_UP = {
    "turn": -90.0,  # (built rolled onto the right side, then turned: the leg kicks forward)
    "body_roll": -62.0, "body_pitch": -12.0, "pelvis": (0.0, 3.0, -18.5), "spine_bend": 8.0, "head": (0.0, 10.0),
    "rfoot": (-2.0, -5.0, 0.0), "rknee": (-10.0, -18.0, 8.0),
    "lleg": ((0.3, 0.0, 1.0), 1.0), "lknee": (0.0, -20.0, 0.0),
    "rhand_at": (-22.5, 7.0, -22.0), "relbow": (-14.0, 8.0, 0.0),
    "rpalm": ((-1.0, 0.0, 0.0), (0.0, 0.0, -1.0)),
    "larm": ((0.45, -0.55, 0.7), 0.9), "lelbow": (12.0, 4.0, -8.0),
}

ANIMS = {
    "BOTH_SHINOBI_STANCE_HYUGA": dict(ref=STAND, frames=11, fps=20, loop=-1,
                                      keys=[(0.0, {}), (1.0, HYUGA)]),
    "BOTH_SHINOBI_KICK_UP": dict(ref=STAND, frames=17, fps=20, loop=-1,
                                 keys=[(0.0, {}), (0.35, CROUCH), (0.65, KICK_UP, "fast"), (1.0, KICK_UP)],
                                 plant=("r",)),
}
