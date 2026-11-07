#!/usr/bin/env python3
"""JK_Shinobi: the mod's own little sounds, made from noise (no recordings).

    python3 tools/gen_sounds.py        -> assets/sound/shinobi/*.wav

inhale: Katon's long breath in (everyone near hears the fireball coming): breathy noise,
swelling and rising in pitch over a second, cut short at the top.
chidori_charge: the lightning gathering in the hand while Chidori charges - a crackle of
sparks over a buzz, a loop.
hit_light / hit_heavy: the confirm of a blow landing, in the hitter's ears - a short thump
under a snap (the heavy one lower and longer).
charge1..3: a sharp gust of wind - a blade's swing through the air - heard (by you only) as each
level bar of a jutsu being charged fills: noise through a band-pass that sweeps up and back
down, a quick swell and a short tail; each level a little higher and brighter.
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


def swing(seed, dur=0.32, f0=420.0, f1=1700.0, f2=650.0, gain=0.85):
    """Up and back down: the air cut by something fast."""
    rng = np.random.default_rng(seed)
    n = int(dur * RATE)
    t = np.arange(n) / RATE
    noise = rng.standard_normal(n)
    out = np.zeros(n)
    block = 128
    for i in range(0, n, block):
        u = i / n
        fc = f0 * (f1 / f0) ** (u / 0.4) if u < 0.4 else f1 * (f2 / f1) ** ((u - 0.4) / 0.6)
        lo, hi = fc / 1.5, min(fc * 1.5, RATE / 2 - 100)
        sos = butter(2, [lo, hi], btype="band", fs=RATE, output="sos")
        seg = noise[max(0, i - 256):i + block]
        y = sosfilt(sos, seg)[-min(block, n - i):]
        out[i:i + len(y)] = y
    peak = 0.38 * dur
    env = np.where(t < peak, (t / peak) ** 1.6, np.exp(-(t - peak) / (0.22 * dur)))
    out *= env
    out /= np.max(np.abs(out)) + 1e-9
    return out * gain


def inhale(seed=7, dur=1.15, gain=0.8):
    rng = np.random.default_rng(seed)
    n = int(dur * RATE)
    t = np.arange(n) / RATE
    noise = rng.standard_normal(n)
    out = np.zeros(n)
    block = 256
    for i in range(0, n, block):
        u = i / n
        fc = 500.0 + 1300.0 * u ** 1.4                      # the breath rises
        sos = butter(2, [fc / 1.8, min(fc * 1.8, RATE / 2 - 100)], btype="band", fs=RATE, output="sos")
        seg = noise[max(0, i - 512):i + block]
        y = sosfilt(sos, seg)[-min(block, n - i):]
        out[i:i + len(y)] = y
    env = np.minimum(t / 0.25, 1.0) ** 1.5 * (0.45 + 0.55 * t / dur)   # swells
    env *= np.minimum((dur - t) / 0.04, 1.0)                           # cut at the top
    out *= env
    out /= np.max(np.abs(out)) + 1e-9
    return out * gain


def crackle(seed=11, dur=1.0, gain=0.7):
    rng = np.random.default_rng(seed)
    n = int(dur * RATE)
    t = np.arange(n) / RATE
    buzz = np.sign(np.sin(2 * np.pi * 120 * t)) * 0.12 + rng.standard_normal(n) * 0.06
    sparks = np.zeros(n)
    for _ in range(140):                                    # sharp snaps of different sizes
        at = rng.integers(0, n - 400)
        ln = rng.integers(30, 380)
        sparks[at:at + ln] += rng.standard_normal(ln) * np.exp(-np.arange(ln) / (ln / 4)) * rng.uniform(0.3, 1.0)
    sos = butter(2, 1800, btype="high", fs=RATE, output="sos")
    out = buzz + sosfilt(sos, sparks)
    fade = int(0.01 * RATE)                                   # (loops without a click)
    out[:fade] *= np.linspace(0, 1, fade)
    out[-fade:] *= np.linspace(1, 0, fade)
    out /= np.max(np.abs(out)) + 1e-9
    return out * gain


def thump(seed, dur, f0, gain):
    rng = np.random.default_rng(seed)
    n = int(dur * RATE)
    t = np.arange(n) / RATE
    f = f0 * (1.0 + 1.5 * np.exp(-t / 0.012))                    # pitch drops fast: a punch
    body = np.sin(2 * np.pi * np.cumsum(f) / RATE) * np.exp(-t / (dur * 0.35))
    snap = rng.standard_normal(n) * np.exp(-t / 0.006)
    snap = sosfilt(butter(2, 1500, btype="high", fs=RATE, output="sos"), snap)
    out = body + 0.5 * snap
    out /= np.max(np.abs(out)) + 1e-9
    return out * gain


def main():
    for lvl, k in ((1, 1.0), (2, 1.3), (3, 1.7)):
        write("charge%d.wav" % lvl, swing(40 + lvl, f0=420 * k, f1=1700 * k, f2=650 * k))
    write("inhale.wav", inhale())
    write("hit_light.wav", thump(21, 0.12, 140.0, 0.7))
    write("hit_heavy.wav", thump(22, 0.22, 85.0, 0.9))
    write("chidori_charge.wav", crackle())
    print("wrote", os.path.normpath(OUT))


if __name__ == "__main__":
    main()
