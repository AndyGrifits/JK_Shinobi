#!/usr/bin/env python3
"""JK_Shinobi: render a player .glm the way the game skins it, in the bind pose or in
frames of _humanoid animations, to check a converted model without starting the game.

    python3 tools/glmpreview.py models/players/asuma/model.glm --gla _humanoid.gla \
        --cfg animation.cfg --anims BOTH_STAND1,BOTH_RUN1:0.5 --out preview.png
(needs Blender's Python module for the rendering: pip install bpy)

--anims: names from animation.cfg (frame = first frame, NAME:0.5 = half way through),
or plain frame numbers; "base" = the bind pose. Each one is a column, seen from the
front and from the side. Textures come from the .skin next to the model.
"""
import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import g2file  # noqa: E402


def read_cfg(path):
    anims = {}
    for line in open(path, encoding="latin-1"):
        p = line.split()
        if len(p) >= 4 and p[0].isupper() and p[1].lstrip("-").isdigit():
            anims[p[0]] = (int(p[1]), int(p[2]))
    return anims


def skin(surfs, mats):
    out = []
    for s in surfs:
        if s.flags & (g2file.SURF_BOLT | g2file.SURF_OFF) or not s.tris:
            continue
        v = np.array(s.verts)
        p = np.zeros_like(v)
        for i, ws in enumerate(s.weights):
            for b, w in ws:
                m = mats[b]
                p[i] += w * (m[:, :3] @ v[i] + m[:, 3])
        out.append((s, p))
    return out


def tag_points(surfs, mats):
    pts = {}
    for s in surfs:
        if s.flags & g2file.SURF_BOLT and len(s.verts) == 3:
            p = []
            for v, ws in zip(s.verts, s.weights):
                q = np.zeros(3)
                for b, w in ws:
                    q += w * (mats[b][:, :3] @ np.array(v) + mats[b][:, 3])
                p.append(q)
            pts[s.name] = p
    return pts


ARM_BONES = {
    "upper": ("humerus", "humerusX"),
    "fore": ("radius", "radiusX"),
}


def shorten_arms(gla, mats, k):
    """What a runtime bone override would do: in the animated pose, the upper arm and the
    forearm get k times shorter along their own direction (thickness kept), the elbow,
    the hand and the fingers are moved in to the new joint ends, every bone keeps its
    rotation. Ghoul2 can do the same per frame with post-multiplied bone matrices."""
    mats = [np.array(m, dtype=float) for m in mats]
    idx = gla.index

    def joint(i):
        b = gla.bones[i]
        j = np.array([b["base"][0][3], b["base"][1][3], b["base"][2][3]])
        return mats[i][:, :3] @ j + mats[i][:, 3]

    def left(m, A, t):          # posed space: x -> A x + t, applied after m
        return np.hstack([A @ m[:, :3], (A @ m[:, 3] + t)[:, None]])

    for s in "lr":
        hum, rad, hand = idx[s + "humerus"], idx[s + "radius"], idx[s + "hand"]
        ph, pr, pk = joint(hum), joint(rad), joint(hand)
        u, v = pr - ph, pk - pr
        d1 = (k - 1) * u                       # the elbow moves in
        d2 = d1 + (k - 1) * v                  # the wrist moves in twice
        for seg, (p0, w, shift) in (("upper", (ph, u, np.zeros(3))), ("fore", (pr, v, d1))):
            n = w / (np.linalg.norm(w) + 1e-9)
            A = np.eye(3) + (k - 1) * np.outer(n, n)
            t = p0 - A @ p0 + shift
            for name in ARM_BONES[seg]:
                i = idx[s + name]
                mats[i] = left(mats[i], A, t)
        for i, b in enumerate(gla.bones):      # the hand, the fingers, the hang tag
            nm = b["name"]
            if nm == s + "hand" or nm.startswith(s + "_d") or nm == s + "hang_tag_bone":
                mats[i] = left(mats[i], np.eye(3), d2)
    return mats


def twist(gla, mats, deg):
    """Turn everything above upper_lumbar by deg/2 about its joint and everything above
    thoracic by another deg/2 about that one (world z, in the animated pose)."""
    mats = [np.array(m, dtype=float) for m in mats]
    kids = {i: [] for i in range(gla.num_bones)}
    for i, b in enumerate(gla.bones):
        if b["parent"] >= 0:
            kids[b["parent"]].append(i)

    def below(i):
        out, todo = [], [i]
        while todo:
            j = todo.pop()
            out.append(j)
            todo += kids[j]
        return out
    for name in ("upper_lumbar", "thoracic"):
        i = gla.index[name]
        b = gla.bones[i]
        joint = np.array([b["base"][0][3], b["base"][1][3], b["base"][2][3]])
        piv = mats[i][:, :3] @ joint + mats[i][:, 3]
        t = np.radians(deg / 2)
        R = np.array([[np.cos(t), -np.sin(t), 0], [np.sin(t), np.cos(t), 0], [0, 0, 1]])
        for j in below(i):
            m = mats[j]
            mats[j] = np.hstack([R @ m[:, :3], (R @ (m[:, 3] - piv) + piv)[:, None]])
    return mats


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("glm")
    ap.add_argument("--gla", required=True)
    ap.add_argument("--cfg")
    ap.add_argument("--anims", default="base")
    ap.add_argument("--out", default="preview.png")
    ap.add_argument("--size", type=int, default=360)
    ap.add_argument("--tags", action="store_true", help="draw the tag triangles in red")
    ap.add_argument("--views", default="front,side")
    ap.add_argument("--scale", type=float, default=0.0,
                    help="model scale in %% (the model's scale.cfg by default), about the feet "
                         "the way the game draws it")
    ap.add_argument("--arms", type=float, default=1.0,
                    help="arm length factor in the animated pose (0.9 = 10%% shorter), the way a "
                         "runtime bone override would shorten them")
    ap.add_argument("--twist", type=float, default=0.0,
                    help="also turn the torso by this many degrees, half at upper_lumbar and "
                         "half at thoracic, the way the game turns it to follow the aim")
    a = ap.parse_args(argv)

    import bpy
    gla = g2file.Gla(a.gla)
    head, surfs = g2file.read_glm(a.glm)
    cfg = read_cfg(a.cfg) if a.cfg else {}
    skin_file = os.path.join(os.path.dirname(a.glm), "model_default.skin")
    shaders = {}
    if os.path.exists(skin_file):
        for line in open(skin_file):
            if "," in line:
                k, v = line.strip().split(",", 1)
                shaders[k] = v
    root = os.path.dirname(a.glm)
    for _ in head["name"].split("/")[:-1]:
        root = os.path.dirname(root)

    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    images = {}

    def material(shader):
        if shader in images:
            return images[shader]
        mat = bpy.data.materials.new(shader or "none")
        img = None
        for ext in (".tga", ".png", ".jpg"):
            f = os.path.join(root, shader + ext)
            if shader and os.path.exists(f):
                img = bpy.data.images.load(f)
                break
        mat.use_nodes = True
        if img:
            n = mat.node_tree.nodes.new("ShaderNodeTexImage")
            n.image = img
            n.interpolation = "Closest"
            bsdf = mat.node_tree.nodes["Principled BSDF"]
            mat.node_tree.links.new(n.outputs["Color"], bsdf.inputs["Base Color"])
        images[shader] = mat
        return mat

    scale = a.scale / 100.0 if a.scale else 1.0
    scale_cfg = os.path.join(os.path.dirname(a.glm), "scale.cfg")
    if not a.scale and os.path.exists(scale_cfg):
        for line in open(scale_cfg):
            q = line.split()
            if len(q) == 2 and q[0] == "scale":
                scale = int(q[1]) / 100.0
    columns = [x for x in a.anims.split(",") if x]
    views = a.views.split(",")
    spacing = 60.0
    for ci, spec in enumerate(columns):
        if spec == "base":
            mats = [np.eye(3, 4) for _ in range(gla.num_bones)]
        else:
            name, _, frac = spec.partition(":")
            if name.isdigit():
                frame = int(name)
            else:
                first, count = cfg[name]
                frame = first + int((count - 1) * float(frac or 0))
            mats = [np.array(m) for m in gla.frame_skin(frame)]
        if a.twist:
            mats = twist(gla, mats, a.twist)
        if a.arms != 1.0 and spec != "base":
            mats = shorten_arms(gla, mats, a.arms)
        for vi, view in enumerate(views):
            rot = 0.0 if view == "front" else (np.pi / 2 if view == "side" else np.pi)
            R = np.array([[np.cos(rot), -np.sin(rot), 0], [np.sin(rot), np.cos(rot), 0], [0, 0, 1]])
            offset = np.array([(ci * len(views) + vi) * spacing, 0, 0])
            for s, p in skin(surfs, mats):
                if scale != 1.0 and spec != "base":
                    p = (p - np.array([0, 0, -24.0])) * scale + np.array([0, 0, -24.0])
                if shaders.get(s.name) == "*off":
                    continue
                me = bpy.data.meshes.new(s.name)
                pts = (p @ R.T) + offset
                # game triangles are clockwise from outside: flip back for Blender
                me.from_pydata(pts.tolist(), [], [(t[0], t[2], t[1]) for t in s.tris])
                uv = me.uv_layers.new()
                for poly in me.polygons:
                    for li in poly.loop_indices:
                        u, v = s.uvs[me.loops[li].vertex_index]
                        uv.data[li].uv = (u, 1 - v)
                me.materials.append(material(shaders.get(s.name, s.shader)))
                ob = bpy.data.objects.new(s.name, me)
                sc.collection.objects.link(ob)
            if a.tags:
                for tname, tri in tag_points(surfs, mats).items():
                    pts = [(np.array(q) @ R.T + offset).tolist() for q in tri]
                    me = bpy.data.meshes.new(tname)
                    me.from_pydata(pts, [], [(0, 1, 2)])
                    m = bpy.data.materials.new("tag")
                    m.diffuse_color = (1, 0, 0, 1)
                    me.materials.append(m)
                    ob = bpy.data.objects.new(tname, me)
                    ob.show_in_front = True
                    sc.collection.objects.link(ob)
    n = len(columns) * len(views)
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    cam.data.type = "ORTHO"
    cam.data.ortho_scale = max(spacing * n, 90) * 1.0
    cam.location = ((n - 1) * spacing / 2, -300, 26)
    cam.rotation_euler = (np.pi / 2, 0, 0)
    sc.collection.objects.link(cam)
    sc.camera = cam
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.display.shading.light = "STUDIO"
    sc.display.shading.color_type = "TEXTURE"
    sc.display.shading.show_backface_culling = True
    sc.render.resolution_x = int(a.size * max(n, 1.5) * 60 / 90)
    sc.render.resolution_y = int(a.size * 1.2)
    cam.data.ortho_scale = max(spacing * n, 90 * 1.2)
    sc.render.film_transparent = False
    sc.world = bpy.data.worlds.new("w")
    sc.render.filepath = os.path.abspath(a.out)
    bpy.ops.render.render(write_still=True)
    print("wrote", a.out)


if __name__ == "__main__":
    args = sys.argv[1:]
    if "--" in args:
        args = args[args.index("--") + 1:]
    main(args)
