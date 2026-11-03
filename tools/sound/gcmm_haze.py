"""gcmm_haze.py - GCMM exFAT's opening sound (all original, made from scratch), in the
same airy style as the other apps': soft air and an open-fifth pad under the logo, a gloss
shimmer, a breath as MEMORY MANAGER folds into the slot, wind rising through the flip,
a soft click and a bloom into a wide F chord as the card goes in, sparkles, and a
breath as the screen comes up. Timed to the intro: gloss 0.4 s, words fold 1.4 s,
wind-up 2.6 s, flip 3.1 s, line up 4.05 s, in at 4.65 s, slot gone 5.4 s, grid 5.4 s,
main screen 6.2 s."""
import sys, wave, numpy as np
from scipy.signal import fftconvolve, butter, sosfilt
SR = 32000
LEN = 9.6
N = int(LEN * SR)
t = np.arange(N) / SR
rng = np.random.default_rng(7)
T_IN = 4.65

def env(t0, att, hold, rel, shape=2.0):
    e = np.zeros(N)
    a = (t >= t0) & (t < t0 + att); e[a] = ((t[a] - t0) / att) ** shape
    h = (t >= t0 + att) & (t < t0 + att + hold); e[h] = 1
    r = t >= t0 + att + hold; e[r] = np.exp(-(t[r] - t0 - att - hold) / rel)
    return e

def lp(x, f, order=2): return sosfilt(butter(order, min(f, SR * 0.45), 'low', fs=SR, output='sos'), x)
def hp(x, f, order=2): return sosfilt(butter(order, f, 'high', fs=SR, output='sos'), x)
def bp(x, lo, hi, order=2): return sosfilt(butter(order, [lo, min(hi, SR * 0.45)], 'band', fs=SR, output='sos'), x)

def sweep_bp(x, f0, f1, t0, t1, q=3.0, steps=60):
    out = np.zeros(N)
    i0, i1 = int(t0 * SR), int(t1 * SR)
    edges = np.linspace(i0, i1, steps + 1).astype(int)
    for k in range(steps):
        f = f0 * (f1 / f0) ** (k / steps)
        a, b = max(0, edges[k] - 800), min(N, edges[k + 1] + 800)
        out[a:b] += bp(x[a:b], f / (1 + 1 / q), f * (1 + 1 / q))
    return out

def sine(f, phase=0): return np.sin(2 * np.pi * np.cumsum(np.broadcast_to(f, (N,)) / SR) + phase)

def reverb(x, secs=2.8, wet=0.35, dark=4000, seed=0):
    r = np.random.default_rng(seed); n = int(secs * SR)
    ir = r.standard_normal(n) * np.exp(-np.arange(n) / SR * 6.9 / secs)
    ir = lp(ir, dark); ir /= np.sqrt((ir ** 2).sum())
    return x * (1 - wet) + fftconvolve(x, ir)[:N] * wet * 3

def pad(freqs, e, bright=1200, det=0.004):
    l = np.zeros(N); r = np.zeros(N)
    for f in freqs:
        for d, side in ((-det, 0), (det, 1), (0, 2)):
            s = sine(f * (1 + d), rng.random() * 6) + 0.25 * sine(2 * f * (1 + d)) + 0.08 * sine(3 * f * (1 + d))
            if side == 0: l += s
            elif side == 1: r += s
            else: l += s * 0.5; r += s * 0.5
    return lp(l, bright) * e, lp(r, bright) * e

F1 = 43.65
noise = rng.standard_normal(N); noise2 = rng.standard_normal(N)

def opening():
    # soft air under everything, a touch stronger through the flip
    lift = env(2.6, 1.2, 0.4, 0.7)
    air = lp(noise, 800) * (0.35 + 0.5 * lift) * (0.8 + 0.2 * np.sin(2 * np.pi * 0.27 * t))
    air2 = lp(noise2, 1200) * (0.35 + 0.5 * lift) * (0.8 + 0.2 * np.sin(2 * np.pi * 0.33 * t + 1))
    # an airy open-fifth pad, there from the start (the logo's already on screen)
    ce = env(0.0, 0.9, 3.2, 0.6)
    cl, cr = pad([F1 * 4, F1 * 6, F1 * 8, F1 * 12], ce * 0.55, bright=1300, det=0.006)
    # the gloss passing over the logo: a little rising shimmer
    shim = np.zeros(N)
    for k, f in enumerate([1046.5, 1318.5, 1568.0, 2093.0]):
        shim += sine(f) * env(0.45 + k * 0.16, 0.02, 0, 0.45) * 0.045
    # MEMORY MANAGER folds into the slot: a breath falling, and a soft tone settling
    fold = sweep_bp(noise2, 3200, 700, 1.35, 2.3, q=2.2) * env(1.35, 0.5, 0.2, 0.3, 2) * 0.35
    glide = sine(np.where(t < 1.4, 784, 784 * np.exp(-np.clip(t - 1.4, 0, 0.9) * 0.77))) * env(1.4, 0.25, 0.5, 0.35) * 0.05
    # the wind-up and the flip: wind rising, a whoosh as it turns over, close to you
    whoosh = sweep_bp(noise, 350, 3600, 2.7, 4.05, q=1.8) * env(2.7, 1.05, 0.15, 0.25, 2.6) * 0.75
    turn = sweep_bp(noise2, 900, 2400, 3.45, 3.8, q=2.5) * env(3.45, 0.18, 0, 0.18) * 0.3   # the card's edge passing
    # lining up over the slot: the air falls back
    settle = sweep_bp(noise, 2400, 800, 4.05, 4.6, q=2) * env(4.05, 0.12, 0.15, 0.2) * 0.3
    # in: a soft click and a low touch, then the bloom into a wide F chord
    click = hp(noise * env(T_IN, 0.001, 0.004, 0.012, 1), 2500) * 0.9
    thump = sine(70 * np.exp(-np.clip(t - T_IN, 0, None) * 10) + 45) * env(T_IN, 0.004, 0, 0.16) * 0.55
    bloom = sweep_bp(noise, 300, 5000, T_IN - 0.05, T_IN + 0.6, q=1.5) * env(T_IN - 0.05, 0.2, 0.05, 0.5, 2) * 0.45
    pe = env(T_IN, 0.3, 2.0, 1.6)
    pl, pr = pad([F1 * 2, F1 * 4, F1 * 5, F1 * 6, F1 * 9, F1 * 10], pe, bright=2600)
    dust = np.zeros(N)
    for k in range(14):
        t0 = T_IN + 0.1 + rng.random() * 3.2
        f = rng.choice([1046.5, 1174.7, 1318.5, 1568.0])
        dust += sine(f) * env(t0, 0.02, 0, 0.35) * 0.05
    # the slot shrinks away: two small notes falling
    gone = sine(1568.0) * env(5.0, 0.01, 0, 0.3) * 0.05 + sine(1046.5) * env(5.2, 0.01, 0, 0.4) * 0.05
    # the screen comes up: a breath and a soft high chime
    breath = sweep_bp(noise2, 600, 2000, 5.6, 7.2, q=2) * env(5.6, 0.7, 0.2, 0.6) * 0.25
    chime = (sine(1396.9) + 0.6 * sine(2093.0)) * env(6.25, 0.01, 0, 0.9) * 0.06
    mixl = air + cl + shim + fold + glide + whoosh + turn + settle + click + thump + bloom + pl + dust + gone + breath + chime
    mixr = air2 + cr + shim * 0.8 + fold + glide + whoosh + turn * 0.7 + settle + click + thump + bloom + pr + dust * 0.8 + gone + breath + chime
    return np.stack([reverb(mixl, 3.2, 0.38, seed=3), reverb(mixr, 3.2, 0.38, seed=4)])

def out(name, st):
    st = st / (np.abs(st).max() + 1e-9) * 0.85
    st = np.tanh(st * 1.15) / np.tanh(1.15)
    st[:, -4000:] *= np.linspace(1, 0, 4000)
    w = wave.open(name, 'wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR)
    w.writeframes((st.T * 32767 * 0.95).astype(np.int16).tobytes()); w.close()

if __name__ == '__main__':
    out(sys.argv[1], opening())
