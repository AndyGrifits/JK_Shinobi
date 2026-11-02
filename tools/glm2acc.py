#!/usr/bin/env python3
"""JK_Shinobi: turn a model exported from Blender (Ghoul2 .glm or plain .obj)
into an MD3 for the game: head accessories (default) or props / items
(--game-dir models/shinobi --fit <size> --center).

The accessory must be modelled in the bind pose of the stock _humanoid
skeleton, sitting where it belongs on the head (weights don't matter: the
game keeps it rigid on the "cranium" bone). The MD3 keeps the glm's
model-space coordinates.

Self-contained (standard library only): runs with any Python 3, including
the one inside Blender. The texture is converted with Pillow when it is
installed, else with Blender's image API when run inside Blender, else copied
as it is.

Command line:
  python glm2acc.py <model.glm> <texture.png|tga|jpg> <name> [--out DIR]
From Python / a Blender add-on:
  import glm2acc; glm2acc.convert(glm_path, tex_path, name, out_dir)

Writes <out>/<name>.md3 and <out>/<name>.jpg (.tga when the texture has
alpha). Default out: assets/models/accessories/head next to this script's
repo. In the game the files must end up in models/accessories/head/ (in a
pk3, or loose in GameData/shinobi/models/accessories/head/).
"""
import argparse
import math
import os
import re
import shutil
import struct

MD3_IDENT = b"IDP3"
MD3_VERSION = 15
MAX_QPATH = 64
XYZ_SCALE = 64.0      # MD3 stores coordinates * 64 as shorts
MAX_COORD = 511.0     # ... so they must stay within +-511 units
MAX_VERTS = 990       # the renderer's per-surface limit is 1000
NAME_RE = re.compile(r"^[a-z0-9_]{1,31}$")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_OUT = os.path.join(ROOT, "assets", "models", "accessories", "head")


def encode_normal(n):
    """Q3 lat/long normal encoding."""
    x, y, z = n
    length = math.sqrt(x * x + y * y + z * z) or 1.0
    x, y, z = x / length, y / length, z / length
    if x == 0 and y == 0:
        return 0 if z > 0 else 128
    lat = int(round(math.atan2(y, x) * 255 / (2 * math.pi))) & 0xFF
    lng = int(round(math.acos(max(-1.0, min(1.0, z))) * 255 / (2 * math.pi))) & 0xFF
    return (lat << 8) | lng


def pad(name, size=MAX_QPATH):
    data = name.encode("ascii")
    if len(data) >= size:
        raise ValueError("name too long: %s" % name)
    return data + b"\0" * (size - len(data))


def read_glm(path):
    """LOD 0 surfaces of a Ghoul2 model: [(verts, normals, uvs, tris)]."""
    d = open(path, "rb").read()
    ident, version = struct.unpack_from("<4si", d, 0)
    if ident != b"2LGM":
        raise ValueError("not a Ghoul2 model (.glm): %s" % path)
    _, _, num_lods, ofs_lods, num_surf, _, _ = struct.unpack_from("<7i", d, 136)
    surfaces = []
    offs = struct.unpack_from("<%di" % num_surf, d, ofs_lods + 4)
    for o in offs:
        s = ofs_lods + 4 + o
        _, idx, _, nv, ofs_v, nt, ofs_t, _, _, _ = struct.unpack_from("<10i", d, s)
        verts, normals, uvs = [], [], []
        for v in range(nv):
            nx, ny, nz, x, y, z = struct.unpack_from("<6f", d, s + ofs_v + v * 32)
            verts.append((x, y, z))
            normals.append((nx, ny, nz))
        uv_base = s + ofs_v + nv * 32
        for v in range(nv):
            uvs.append(struct.unpack_from("<2f", d, uv_base + v * 8))
        tris = [struct.unpack_from("<3i", d, s + ofs_t + t * 12) for t in range(nt)]
        if tris:
            surfaces.append((verts, normals, uvs, tris))
    if not surfaces:
        raise ValueError("no triangles in %s" % path)
    return surfaces


def read_obj(path):
    """A Wavefront .obj (Blender's own exporter, no add-on): one surface, polygons fanned
    into triangles. OBJ's texture v grows upward, the game's downward: flipped."""
    pos, tex, nor = [], [], []
    verts, normals, uvs, tris, index = [], [], [], [], {}
    for line in open(path, encoding="utf-8", errors="replace"):
        p = line.split()
        if not p:
            continue
        if p[0] == "v":
            pos.append(tuple(float(c) for c in p[1:4]))
        elif p[0] == "vt":
            tex.append((float(p[1]), 1.0 - float(p[2]) if len(p) > 2 else 0.0))
        elif p[0] == "vn":
            nor.append(tuple(float(c) for c in p[1:4]))
        elif p[0] == "f":
            corner = []
            for c in p[1:]:
                ids = (c.split("/") + ["", ""])[:3]
                key = tuple(int(i) if i else 0 for i in ids)
                if key not in index:
                    vi, ti, ni = key
                    index[key] = len(verts)
                    verts.append(pos[vi - 1 if vi > 0 else vi])
                    uvs.append(tex[ti - 1 if ti > 0 else ti] if ti else (0.0, 0.0))
                    normals.append(nor[ni - 1 if ni > 0 else ni] if ni else (0.0, 0.0, 1.0))
                corner.append(index[key])
            for k in range(1, len(corner) - 1):
                tris.append((corner[0], corner[k], corner[k + 1]))
    if not tris:
        raise ValueError("no faces in %s" % path)
    return [(verts, normals, uvs, tris)]


def obj_texture(path):
    """The texture the .obj's material names (map_Kd), looked up next to the .obj too."""
    mtl = None
    for line in open(path, encoding="utf-8", errors="replace"):
        if line.startswith("mtllib"):
            mtl = os.path.join(os.path.dirname(path), line.split(None, 1)[1].strip())
            break
    if not mtl or not os.path.exists(mtl):
        return None
    for line in open(mtl, encoding="utf-8", errors="replace"):
        if line.strip().startswith("map_Kd"):
            tex = line.strip().split(None, 1)[1].strip()
            for cand in (tex, os.path.join(os.path.dirname(path), os.path.basename(tex.replace("\\", "/")))):
                if os.path.exists(cand):
                    return cand
            return os.path.join(os.path.dirname(path), os.path.basename(tex.replace("\\", "/")))
    return None


def split(surface):
    """Chunks of at most MAX_VERTS vertices."""
    verts, normals, uvs, tris = surface
    chunks, remap, cur = [], {}, None
    for tri in tris:
        new = {v for v in tri if v not in remap}
        if cur is None or len(cur[0]) + len(new) > MAX_VERTS:
            cur = ([], [], [], [])
            remap = {}
            chunks.append(cur)
        out = []
        for v in tri:
            if v not in remap:
                remap[v] = len(cur[0])
                cur[0].append(verts[v])
                cur[1].append(normals[v])
                cur[2].append(uvs[v])
            out.append(remap[v])
        cur[3].append(tuple(out))
    return chunks


def md3_surface(name, shader, verts, normals, uvs, tris):
    header_size = 4 + MAX_QPATH + 4 * 10
    shaders = pad(shader) + struct.pack("<i", 0)
    tri_data = b"".join(struct.pack("<3i", *t) for t in tris)
    st_data = b"".join(struct.pack("<2f", u, v) for u, v in uvs)
    xyz_data = b"".join(struct.pack("<3hH", *(int(round(c * XYZ_SCALE)) for c in p), encode_normal(n))
                        for p, n in zip(verts, normals))
    ofs_tris = header_size + len(shaders)
    ofs_st = ofs_tris + len(tri_data)
    ofs_xyz = ofs_st + len(st_data)
    ofs_end = ofs_xyz + len(xyz_data)
    return (MD3_IDENT + pad(name) + struct.pack("<10i", 0, 1, 1, len(verts), len(tris), ofs_tris, header_size,
                                                  ofs_st, ofs_xyz, ofs_end)
            + shaders + tri_data + st_data + xyz_data)


def write_texture(tex, out_base):
    """Returns the file written. Alpha -> .tga, else .jpg."""
    for ext in (".tga", ".jpg", ".png"):
        if os.path.exists(out_base + ext):
            os.remove(out_base + ext)
    try:
        from PIL import Image
        img = Image.open(tex)
        if img.mode in ("RGBA", "LA", "P"):
            img.convert("RGBA").save(out_base + ".tga")
            return out_base + ".tga"
        img.convert("RGB").save(out_base + ".jpg", quality=92)
        return out_base + ".jpg"
    except ImportError:
        pass
    try:
        import bpy  # inside Blender
        img = bpy.data.images.load(os.path.abspath(tex), check_existing=False)
        alpha = img.channels == 4 and img.alpha_mode != 'NONE'
        path = out_base + (".tga" if alpha else ".jpg")
        img.file_format = 'TARGA' if alpha else 'JPEG'
        img.filepath_raw = path
        img.save()
        bpy.data.images.remove(img)
        return path
    except ImportError:
        pass
    ext = os.path.splitext(tex)[1].lower()
    path = out_base + ext
    shutil.copyfile(tex, path)
    if ext not in (".jpg", ".jpeg", ".tga"):
        print("warning: %s copied as it is (no Pillow, not in Blender): the game wants .jpg or .tga" % path)
    return path


def convert(glm, tex, name, out_dir=DEFAULT_OUT, game_dir="models/accessories/head", fit=0.0, center=False):
    """Converts; returns (md3 path, texture path). Raises ValueError on bad input.
    game_dir: where the files sit inside the game (the shader name follows it).
    fit: scale so the biggest extent becomes this many units (0 = keep).
    center: move the model's middle to the origin (props, thrown things)."""
    if not NAME_RE.match(name):
        raise ValueError("name must be 1-31 of a-z 0-9 _ (it goes over the network): %r" % name)
    shader = game_dir.rstrip("/") + "/" + name
    surfaces = read_obj(glm) if glm.lower().endswith(".obj") else read_glm(glm)
    if fit or center:
        allp = [v for s in surfaces for v in s[0]]
        lo = [min(v[i] for v in allp) for i in range(3)]
        hi = [max(v[i] for v in allp) for i in range(3)]
        mid = [(lo[i] + hi[i]) / 2 if center else 0.0 for i in range(3)]
        k = fit / max(hi[i] - lo[i] for i in range(3)) if fit else 1.0
        surfaces = [([tuple((v[i] - mid[i]) * k for i in range(3)) for v in s[0]], s[1], s[2], s[3]) for s in surfaces]
    chunks = [c for s in surfaces for c in split(s)]
    allv = [v for c in chunks for v in c[0]]
    mins = [min(v[i] for v in allv) for i in range(3)]
    maxs = [max(v[i] for v in allv) for i in range(3)]
    if max(max(abs(m) for m in mins), max(abs(m) for m in maxs)) > MAX_COORD:
        raise ValueError("coordinates beyond +-%d units: was it exported in the skeleton's scale?" % MAX_COORD)
    radius = max(math.sqrt(v[0] ** 2 + v[1] ** 2 + v[2] ** 2) for v in allv)
    surfaces = b"".join(md3_surface("acc%d" % i, shader, *c) for i, c in enumerate(chunks))
    frame = struct.pack("<3f3f3ff", *mins, *maxs, 0, 0, 0, radius) + pad("frame0", 16)
    header_size = 4 + 4 + MAX_QPATH + 4 * 9
    ofs_frames = header_size
    ofs_surf = ofs_frames + len(frame)
    header = (MD3_IDENT + struct.pack("<i", MD3_VERSION) + pad(shader + ".md3")
              + struct.pack("<9i", 0, 1, 0, len(chunks), 0, ofs_frames, ofs_surf, ofs_surf, ofs_surf + len(surfaces)))
    os.makedirs(out_dir, exist_ok=True)
    md3 = os.path.join(out_dir, name + ".md3")
    with open(md3, "wb") as f:
        f.write(header + frame + surfaces)
    texture = write_texture(tex, os.path.join(out_dir, name))
    print("%s: %d surfaces, %d verts, %d tris, z %.1f..%.1f; texture %s" % (
        md3, len(chunks), len(allv), sum(len(c[3]) for c in chunks), mins[2], maxs[2], texture))
    return md3, texture


def main():
    ap = argparse.ArgumentParser(description="Ghoul2 head accessory (.glm) -> JK_Shinobi MD3")
    ap.add_argument("model", help=".glm (Ghoul2 add-on) or .obj (Blender's own exporter)")
    ap.add_argument("texture", help="the texture; '-' = the one the .obj's material names")
    ap.add_argument("name", help="a-z 0-9 _ only, up to 31")
    ap.add_argument("--out", default=DEFAULT_OUT, help="output folder (default: the repo's accessory assets)")
    ap.add_argument("--game-dir", default="models/accessories/head", help="folder inside the game (shader name)")
    ap.add_argument("--fit", type=float, default=0.0, help="scale so the biggest extent is this many units")
    ap.add_argument("--center", action="store_true", help="put the model's middle at the origin")
    a = ap.parse_args()
    tex = a.texture
    if tex == "-":
        tex = obj_texture(a.model) if a.model.lower().endswith(".obj") else None
        if not tex or not os.path.exists(tex):
            raise SystemExit("no texture found (the material names %r) - put it next to the model" % tex)
    convert(a.model, tex, a.name, a.out, a.game_dir, a.fit, a.center)


if __name__ == "__main__":
    main()
