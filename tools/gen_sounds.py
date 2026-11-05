#!/usr/bin/env python3
"""JK_Shinobi: the mod's own little sounds, made from noise (no recordings).

    python3 tools/gen_sounds.py        -> assets/sound/shinobi/*.wav

seal1..3: the sharp "fwip" of a hand seal snapping into place (anime-style): a short burst of
noise through a band-pass that sweeps down, a hard attack and a quick tail, a faint cloth
snap on top. Three takes so a run of seals doesn't sound like one sample.
"""
import os
import struct
import wave

import numpy as np
from scipy.signal import butter, sosfilt

RATE = 22050
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "sound", "shinobi")


def whoosh(seed, dur=0.16, f_hi=3200.0, f_lo=900.0, gain=0.9):
    rng = np.random.default_rng(seed)
    n = int(dur * RATE)
    t = np.arange(n) / RATE
    noise = rng.standard_normal(n)
    out = np.zeros(n)
    # the band-pass sweeps down: done in short blocks
    block = 128
    for i in range(0, n, block):
        u = i / n
        fc = f_hi * (f_lo / f_hi) ** (u ** 0.6)
        lo, hi = fc / 1.6, min(fc * 1.6, RATE / 2 - 100)
        sos = butter(2, [lo, hi], btype="band", fs=RATE, output="sos")
        seg = noise[max(0, i - 256):i + block]
        y = sosfilt(sos, seg)[-min(block, n - i):]
        out[i:i + len(y)] = y
    env = np.minimum(t / 0.006, 1.0) * np.exp(-t / 0.045)        # snaps in, dies fast
    out *= env
    # the cloth snap: a few ms of brighter noise at the start
    snap = rng.standard_normal(n) * np.exp(-t / 0.004) * 0.6
    sos = butter(2, 2500, btype="high", fs=RATE, output="sos")
    out += sosfilt(sos, snap)
    out /= np.max(np.abs(out)) + 1e-9
    return out * gain


def write(name, x):
    os.makedirs(OUT, exist_ok=True)
    pcm = np.clip(x * 32767, -32768, 32767).astype("<i2")
    with wave.open(os.path.join(OUT, name), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(pcm.tobytes())


def main():
    for i, (seed, hi, lo) in enumerate(((11, 3400, 950), (23, 3000, 800), (37, 3700, 1050)), 1):
        write("seal%d.wav" % i, whoosh(seed, f_hi=hi, f_lo=lo))
    print("wrote", os.path.normpath(OUT))


if __name__ == "__main__":
    main()
