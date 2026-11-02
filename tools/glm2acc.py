#!/usr/bin/env python3
"""Turn a head accessory exported from Blender as a Ghoul2 .glm into the MD3
the game draws on players' heads.

The accessory must be modelled in the bind pose of the stock _humanoid
skeleton, sitting where it belongs on the head (weights don't matter: it is
rigid). The game keeps it on the "cranium" bone: every frame it takes the
bone's transform times the inverse of its bind pose (read from
_humanoid.gla), so the MD3 keeps the same model-space coordinates as the glm.

  python3 tools/glm2acc.py <model.glm> <texture.png|tga|jpg> <name>

writes assets/models/accessories/head/<name>.md3 and <name>.jpg (.tga with alpha) (the shader
is the texture: models/accessories/head/<name>). Surfaces over ~1000
vertices are split (the renderer's per-surface limit).
"""
import os
import struct
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_models import MAX_QPATH, MD3_IDENT, MD3_VERSION, XYZ_SCALE, encode_normal, pad  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "assets", "models", "accessories", "head")
MAX_VERTS = 990


def read_glm(path):
    d = open(path, "rb").read()
    ident, version = struct.unpack_from("<4si", d, 0)
    if ident != b"2LGM":
        raise SystemExit("not a Ghoul2 model: %s" % path)
    _, _, num_lods, ofs_lods, num_surf, _, _ = struct.unpack_from("<7i", d, 136)
    surfaces = []
    lod = ofs_lods
    offs = struct.unpack_from("<%di" % num_surf, d, lod + 4)
    for o in offs:
        s = lod + 4 + o
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
        surfaces.append((verts, normals, uvs, tris))
    return surfaces


def split(surface):
    """Chunks of at most MAX_VERTS vertices."""
    verts, normals, uvs, tris = surface
    chunks, remap, cur = [], {}, None
    for tri in tris:
        new = [v for v in tri if v not in remap] if cur else list(tri)
        if cur is None or len(cur[0]) + len(set(new)) > MAX_VERTS:
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


def main():
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    glm, tex, name = sys.argv[1:]
    shader = "models/accessories/head/" + name
    chunks = [c for s in read_glm(glm) for c in split(s)]
    allv = [v for c in chunks for v in c[0]]
    mins = [min(v[i] for v in allv) for i in range(3)]
    maxs = [max(v[i] for v in allv) for i in range(3)]
    radius = max((v[0] ** 2 + v[1] ** 2 + v[2] ** 2) ** 0.5 for v in allv)
    surfaces = b"".join(md3_surface("acc%d" % i, shader, *c) for i, c in enumerate(chunks))
    frame = struct.pack("<3f3f3ff", *mins, *maxs, 0, 0, 0, radius) + pad("frame0", 16)
    header_size = 4 + 4 + MAX_QPATH + 4 * 9
    ofs_frames = header_size
    ofs_surf = ofs_frames + len(frame)
    header = (MD3_IDENT + struct.pack("<i", MD3_VERSION) + pad(shader + ".md3")
              + struct.pack("<9i", 0, 1, 0, len(chunks), 0, ofs_frames, ofs_surf, ofs_surf, ofs_surf + len(surfaces)))
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, name + ".md3"), "wb") as f:
        f.write(header + frame + surfaces)
    img = Image.open(tex)
    for ext in (".tga", ".jpg"):
        if os.path.exists(os.path.join(OUT, name + ext)):
            os.remove(os.path.join(OUT, name + ext))
    if img.mode in ("RGBA", "LA", "P"):  # (cut-outs need the alpha)
        img.convert("RGBA").save(os.path.join(OUT, name + ".tga"))
    else:
        img.convert("RGB").save(os.path.join(OUT, name + ".jpg"), quality=92)
    print("%s.md3: %d surfaces, %d verts, %d tris, z %.1f..%.1f" % (
        name, len(chunks), len(allv), sum(len(c[3]) for c in chunks), mins[2], maxs[2]))


if __name__ == "__main__":
    main()
