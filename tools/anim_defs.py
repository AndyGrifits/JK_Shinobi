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
  lhand/rhand   (x, y, z) the wrist, from the shoulder (as the body / spine left it);
                lelbow/relbow a point the elbow bends towards, also from the shoulder
  (the reference stand: pelvis z ~9, shoulders ~27, head top ~37, feet ~-20, floor ~-24)
  lpalm/rpalm   (finger direction, palm direction); lcurl/rcurl 0 flat .. 1 fist
"""

STAND = ("BOTH_STAND1", 0)

# Neji's Gentle Fist stance (64 Seals): side-on, the left foot and the left palm forward,
# the right hand raised by the head, knees bent; eases in from the stand in 0.5 s and holds
HYUGA = {
    "body_yaw": -40.0,
    "pelvis": (0.0, 0.0, -7.0),
    "spine_bend": 8.0,
    "head": (38.0, 4.0),
    "lfoot": (4.0, -13.0, 0.0), "lknee": (8.0, -20.0, 0.0), "lfoot_yaw": 10.0,
    "rfoot": (-5.0, 10.0, 0.0), "rknee": (-20.0, -6.0, 0.0), "rfoot_yaw": -65.0,
    "lhand": (1.0, -19.0, -4.0), "lelbow": (10.0, -4.0, -12.0),
    "lpalm": ((0.0, -0.35, 1.0), (0.0, -1.0, 0.15)), "lcurl": 0.0,
    "rhand": (-4.0, -6.0, 9.0), "relbow": (-16.0, 4.0, -4.0),
    "rpalm": ((0.2, -0.2, 1.0), (0.1, -1.0, 0.0)), "rcurl": 0.0,
}

ANIMS = {
    "BOTH_SHINOBI_STANCE_HYUGA": dict(ref=STAND, frames=11, fps=20, loop=-1,
                                      keys=[(0.0, {}), (1.0, HYUGA)]),
}
