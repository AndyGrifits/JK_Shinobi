#!/usr/bin/env python3
"""JK_Shinobi: a copy of the stock _humanoid skeleton with other bone lengths, every
animation carried over (each bone keeps its rotation in every frame; a joint moved by D
in the bind pose stays D away in every pose).

    python3 tools/make_skeleton.py _humanoid.gla _humanoid_ninja.gla [--upper-arm 0.85]
        [--forearm 1.0]

The old Naruto rigs have shorter upper arms than _humanoid (Asuma 85 %, Kakashi 78 %,
Sasuke 89 % of it) and about the same forearms, legs and collar bones; models made on
this skeleton (blend2glm --gla) don't need their upper arms stretched any more. The
file must sit in models/players/_humanoid/ (the game only takes skeletons from there,
and that folder's animation.cfg goes with it): assets/models/players/_humanoid/.
"""
import argparse
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import g2file  # noqa: E402


def decompress(pool):
    """(N, 7) uint16 -> (N, 3, 4) float, as MC_UnCompressQuat."""
    q = pool[:, :4].astype(np.float64) / 16383.0 - 2.0
    w, x, y, z = q[:, 0], q[:, 1], q[:, 2], q[:, 3]
    X, Y, Z = 2 * x, 2 * y, 2 * z
    m = np.empty((len(pool), 3, 4))
    m[:, 0, 0] = 1 - (Y * y + Z * z)
    m[:, 0, 1] = Y * x - Z * w
    m[:, 0, 2] = Z * x + Y * w
    m[:, 1, 0] = Y * x + Z * w
    m[:, 1, 1] = 1 - (X * x + Z * z)
    m[:, 1, 2] = Z * y - X * w
    m[:, 2, 0] = Z * x - Y * w
    m[:, 2, 1] = Z * y + X * w
    m[:, 2, 2] = 1 - (X * x + Y * y)
    m[:, :, 3] = pool[:, 4:7].astype(np.float64) / 64.0 - 512.0
    return m


def quat_from(R):
    """(N, 3, 3) -> (N, 4) w x y z, for the matrix layout above (vectorised Shepperd)."""
    r = R
    tr = r[:, 0, 0] + r[:, 1, 1] + r[:, 2, 2]
    q = np.empty((len(r), 4))
    c0 = tr > 0
    c1 = ~c0 & (r[:, 0, 0] > r[:, 1, 1]) & (r[:, 0, 0] > r[:, 2, 2])
    c2 = ~c0 & ~c1 & (r[:, 1, 1] > r[:, 2, 2])
    c3 = ~c0 & ~c1 & ~c2
    with np.errstate(invalid="ignore", divide="ignore"):
        s = np.sqrt(np.maximum(tr + 1.0, 0)) * 2
        q[c0] = np.stack([0.25 * s, (r[:, 2, 1] - r[:, 1, 2]) / s, (r[:, 0, 2] - r[:, 2, 0]) / s,
                          (r[:, 1, 0] - r[:, 0, 1]) / s], 1)[c0]
        s = np.sqrt(np.maximum(1.0 + r[:, 0, 0] - r[:, 1, 1] - r[:, 2, 2], 0)) * 2
        q[c1] = np.stack([(r[:, 2, 1] - r[:, 1, 2]) / s, 0.25 * s, (r[:, 0, 1] + r[:, 1, 0]) / s,
                          (r[:, 0, 2] + r[:, 2, 0]) / s], 1)[c1]
        s = np.sqrt(np.maximum(1.0 + r[:, 1, 1] - r[:, 0, 0] - r[:, 2, 2], 0)) * 2
        q[c2] = np.stack([(r[:, 0, 2] - r[:, 2, 0]) / s, (r[:, 0, 1] + r[:, 1, 0]) / s, 0.25 * s,
                          (r[:, 1, 2] + r[:, 2, 1]) / s], 1)[c2]
        s = np.sqrt(np.maximum(1.0 + r[:, 2, 2] - r[:, 0, 0] - r[:, 1, 1], 0)) * 2
        q[c3] = np.stack([(r[:, 1, 0] - r[:, 0, 1]) / s, (r[:, 0, 2] + r[:, 2, 0]) / s,
                          (r[:, 1, 2] + r[:, 2, 1]) / s, 0.25 * s], 1)[c3]
    q /= np.linalg.norm(q, axis=1, keepdims=True)
    return q


def compress(m):
    """(N, 3, 4) -> (N, 7) uint16."""
    q = quat_from(m[:, :, :3])
    out = np.empty((len(m), 7), dtype=np.uint16)
    out[:, :4] = np.clip(np.round((q + 2.0) * 16383.0), 0, 65535)
    out[:, 4:] = np.clip(np.round((m[:, :, 3] + 512.0) * 64.0), 0, 65535)
    return out


def joint_moves(gla, upper=1.0, fore=1.0):
    """{bone index: bind-pose shift} for shorter / longer arms: the elbow moves along the
    upper arm, the twist bone on it proportionally, everything below the elbow with it;
    a forearm factor moves the wrist and the hand's bones along the forearm."""
    J = {b["name"]: np.array([b["base"][r][3] for r in range(3)]) for b in gla.bones}
    moves = {}
    for s in "lr":
        u = J[s + "radius"] - J[s + "humerus"]
        v = J[s + "hand"] - J[s + "radius"]
        d_elbow = (upper - 1) * u
        moves[s + "humerusX"] = (upper - 1) * (J[s + "humerusX"] - J[s + "humerus"])
        moves[s + "radius"] = d_elbow
        moves[s + "radiusX"] = d_elbow + (fore - 1) * (J[s + "radiusX"] - J[s + "radius"])
        d_wrist = d_elbow + (fore - 1) * v
        for b in gla.bones:
            n = b["name"]
            if n == s + "hand" or n.startswith(s + "_d") or n == s + "hang_tag_bone":
                moves[n] = d_wrist
    return {gla.index[n]: d for n, d in moves.items() if np.linalg.norm(d) > 1e-6}


def write_gla(src, out_path, name, moves):
    gla = g2file.Gla(src)
    d = bytearray(gla.data)
    nb, nf = gla.num_bones, gla.num_frames
    hdr = struct.calcsize(g2file.GLA_HEADER)
    offs = struct.unpack_from("<%di" % nb, d, hdr)
    # bind pose: move the joints, recompute the inverses
    for i, dv in moves.items():
        p = hdr + offs[i]
        base = np.array(struct.unpack_from("<12f", d, p + 72), dtype=np.float64).reshape(3, 4)
        base[:, 3] += dv
        full = np.vstack([base, [0, 0, 0, 1]])
        inv = np.linalg.inv(full)[:3]
        struct.pack_into("<12f", d, p + 72, *base.ravel())
        struct.pack_into("<12f", d, p + 120, *inv.ravel())
    # frames: index (3 bytes per bone per frame) into the pool of 14-byte bones
    idx_raw = np.frombuffer(bytes(d[gla.ofs_frames:gla.ofs_frames + nf * nb * 3]), dtype=np.uint8)
    idx = idx_raw.reshape(nf, nb, 3).astype(np.int64)
    idx = idx[:, :, 0] | (idx[:, :, 1] << 8) | (idx[:, :, 2] << 16)
    pool_len = (len(d) - gla.ofs_pool) // 14
    pool = np.frombuffer(bytes(d[gla.ofs_pool:gla.ofs_pool + pool_len * 14]), dtype="<u2").reshape(-1, 7)
    used = int(idx.max()) + 1
    pool = pool[:used].copy()
    new_entries = []
    next_id = len(pool)
    for i, dv in sorted(moves.items()):
        m = decompress(pool[idx[:, i]])                        # (nf, 3, 4)
        R = m[:, :, :3]
        m[:, :, 3] += dv - np.einsum("nij,j->ni", R, dv)       # t' = t + (I - R) D
        comp = compress(m)
        uniq, inverse = np.unique(comp, axis=0, return_inverse=True)
        new_entries.append(uniq)
        idx[:, i] = next_id + inverse.ravel()
        next_id += len(uniq)
    pool = np.vstack([pool] + new_entries)
    if next_id >= 1 << 24:
        raise SystemExit("too many bones in the pool for 24-bit indexes")
    idx_out = np.empty((nf, nb, 3), dtype=np.uint8)
    idx_out[:, :, 0] = idx & 0xFF
    idx_out[:, :, 1] = (idx >> 8) & 0xFF
    idx_out[:, :, 2] = (idx >> 16) & 0xFF
    head = bytes(d[:gla.ofs_frames])
    frames = idx_out.tobytes()
    pad = (-(gla.ofs_frames + len(frames))) % 4
    ofs_pool = gla.ofs_frames + len(frames) + pad
    pool_b = pool.astype("<u2").tobytes()
    out = bytearray(head + frames + b"\0" * pad + pool_b)
    end = len(out)
    nm = name.encode("latin-1")
    struct.pack_into("<64s", out, 8, nm + b"\0" * (64 - len(nm)))
    struct.pack_into("<6i", out, 76, nf, gla.ofs_frames, nb, ofs_pool, gla.ofs_skel, end)  # after fScale
    with open(out_path, "wb") as f:
        f.write(out)
    return len(pool), end


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("src", help="the stock _humanoid.gla")
    ap.add_argument("out")
    ap.add_argument("--name", default="models/players/_humanoid/_humanoid_ninja",
                    help="its name inside the game (no extension)")
    ap.add_argument("--upper-arm", type=float, default=0.85)
    ap.add_argument("--forearm", type=float, default=1.0)
    a = ap.parse_args()
    gla = g2file.Gla(a.src)
    moves = joint_moves(gla, a.upper_arm, a.forearm)
    n, size = write_gla(a.src, a.out, a.name, moves)
    print("wrote %s (%s): %d bones moved, pool %d, %.1f MB"
          % (a.out, a.name, len(moves), n, size / 1e6))


if __name__ == "__main__":
    main()
