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
        for vi, view in enumerate(views):
            rot = 0.0 if view == "front" else (np.pi / 2 if view == "side" else np.pi)
            R = np.array([[np.cos(rot), -np.sin(rot), 0], [np.sin(rot), np.cos(rot), 0], [0, 0, 1]])
            offset = np.array([(ci * len(views) + vi) * spacing, 0, 0])
            for s, p in skin(surfs, mats):
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
