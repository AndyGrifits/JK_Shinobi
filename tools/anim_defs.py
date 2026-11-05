"""JK_Shinobi: our animations for tools/make_anims.py.

Each: ref = (stock animation, frame in it) the poses are built on; frames, fps, loop
(as animation.cfg: -1 = plays once - held on its last frame while the game keeps it on;
0 = loops);
keys = [(time 0..1, pose)]. An empty pose = the reference frame as it is.

Pose keys (model space: +X the model's left, -Y forward, +Z up; units ~ inches):
  body_yaw      deg, the whole body about the pelvis (+ turns the chest to its left)
  pelvis        (x, y, z) moved
  spine_bend    deg forward over the three spine joints; spine_side; spine_twist (about up)
  head          (yaw, pitch down)
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
    "spine_bend": 6.0, "spine_side": -24.0,
    "head": (40.0, -14.0),
    "lfoot": (6.8, -11.3, 0.0), "lknee": (14.0, -16.0, 0.0), "lfoot_yaw": 20.0,
    "rfoot": (-12.2, 14.6, 0.0), "rknee": (-16.0, -8.0, 0.0), "rfoot_yaw": -70.0,
    "larm": ((0.15, -0.65, -0.74), 0.995),
    "lpalm": ((0.15, -0.65, -0.74), (0.2, -0.5, 0.8)), "lcurl": 0.0,
    "rarm": ((-0.28, 0.57, 0.77), 0.995),
    "rpalm": ((-0.28, 0.57, 0.77), (-0.6, -0.7, 0.2)), "rcurl": 0.0,
}

ANIMS = {
    "BOTH_SHINOBI_STANCE_HYUGA": dict(ref=STAND, frames=11, fps=20, loop=-1,
                                      keys=[(0.0, {}), (1.0, HYUGA)]),
}
