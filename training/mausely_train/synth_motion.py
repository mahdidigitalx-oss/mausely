"""Synthetic hand-pointer trajectories for training the motion smoother.

Positions are in "frame heights" (y in [0, 1], x in [0, 16/9]) sampled at the
camera rate. The *intended* path is built from human-like movement primitives
(minimum-jerk reaches with Fitts-style timing, dwells with slow drift, smooth
pursuit, micro-corrections). The *observed* path adds physiological tremor
(8-12 Hz), temporally correlated landmark jitter, rare outliers and repeated
frames -- everything the smoother should remove.
"""

from __future__ import annotations

import numpy as np

FPS = 30.0
ASPECT = 16.0 / 9.0
WINDOW = 16          # samples fed to the model
SCALE = 20.0         # model input/output scaling
REGION_LO = np.array([0.08, 0.08])
REGION_HI = np.array([ASPECT - 0.08, 0.92])


def _min_jerk(p0, p1, n):
    t = np.linspace(0.0, 1.0, n + 1)[1:]
    s = 10 * t**3 - 15 * t**4 + 6 * t**5
    return p0 + (p1 - p0) * s[:, None]


def intended_path(length: int, rng: np.random.Generator, fps: float = FPS):
    """Returns (path (T,2), still_mask (T,)) where still marks dwell frames."""
    pos = rng.uniform(REGION_LO, REGION_HI)
    segs, still = [], []
    while sum(len(s) for s in segs) < length:
        kind = rng.choice(4, p=[0.3, 0.4, 0.15, 0.15])
        if kind == 0:  # dwell with slow drift
            n = int(rng.uniform(0.2, 1.5) * fps)
            drift = rng.normal(0, 0.004, size=2) / fps
            seg = pos + drift * np.arange(1, n + 1)[:, None]
            segs.append(seg)
            still.append(np.ones(n, bool))
        elif kind in (1, 3):  # reach or micro-correction
            d = np.exp(rng.uniform(np.log(0.02), np.log(0.6))) if kind == 1 else rng.uniform(0.002, 0.02)
            ang = rng.uniform(0, 2 * np.pi)
            target = np.clip(pos + d * np.array([np.cos(ang), np.sin(ang)]), REGION_LO, REGION_HI)
            d = float(np.linalg.norm(target - pos))
            mt = (0.12 + 0.1 * np.log2(d / 0.01 + 1.0)) * rng.uniform(0.8, 1.25)
            n = max(3, int(round(mt * fps)))
            seg = _min_jerk(pos, target, n)
            segs.append(seg)
            still.append(np.zeros(n, bool))
        else:  # smooth pursuit
            n = int(rng.uniform(1.0, 3.0) * fps)
            f = rng.uniform(0.2, 1.0)
            amp = rng.uniform(0.01, 0.12, size=2)
            ph = rng.uniform(0, 2 * np.pi, size=2)
            t = np.arange(1, n + 1) / fps
            off = amp * (np.sin(2 * np.pi * f * t[:, None] + ph) - np.sin(ph))
            seg = np.clip(pos + off, REGION_LO, REGION_HI)
            segs.append(seg)
            still.append(np.zeros(n, bool))
        pos = segs[-1][-1]
    return np.concatenate(segs)[:length], np.concatenate(still)[:length]


def observe(path: np.ndarray, rng: np.random.Generator, fps: float = FPS) -> np.ndarray:
    """Adds tremor, correlated jitter, outliers and repeated frames."""
    n = len(path)
    t = np.arange(n) / fps
    obs = path.copy()
    for _ in range(2):
        f = rng.uniform(8.0, 12.0)
        amp = rng.uniform(0.0, 0.0012, size=2)
        obs += amp * np.sin(2 * np.pi * f * t[:, None] + rng.uniform(0, 2 * np.pi, size=2))
    sigma = np.exp(rng.uniform(np.log(0.0004), np.log(0.004)))
    rho = rng.uniform(0.0, 0.7)
    e = np.zeros(2)
    noise = np.empty((n, 2))
    for i in range(n):
        e = rho * e + np.sqrt(1 - rho * rho) * sigma * rng.normal(size=2)
        noise[i] = e
    obs += noise
    spikes = rng.uniform(size=n) < 0.004
    ang = rng.uniform(0, 2 * np.pi, size=n)
    mag = rng.uniform(4, 12, size=n) * sigma
    obs[spikes] += (mag[:, None] * np.stack([np.cos(ang), np.sin(ang)], 1))[spikes]
    for i in np.nonzero(rng.uniform(size=n) < 0.02)[0]:
        if i > 0:
            obs[i] = obs[i - 1]
    return obs


# Frame rates seen from webcams (dim light often halves 30 -> 15).
FPS_CHOICES = (15.0, 20.0, 24.0, 25.0, 30.0, 30.0, 30.0, 60.0)


def make_sequences(count: int, length: int, seed: int, fps: float | None = None):
    """Returns (observed (S,T,2), intended (S,T,2), still (S,T), fps (S,)).

    fps=None draws a camera rate per sequence from FPS_CHOICES.
    """
    rng = np.random.default_rng(seed)
    obs, ref, still, rates = [], [], [], []
    for _ in range(count):
        r = fps if fps is not None else float(rng.choice(FPS_CHOICES))
        p, s = intended_path(length, rng, r)
        obs.append(observe(p, rng, r))
        ref.append(p)
        still.append(s)
        rates.append(r)
    return np.array(obs, np.float32), np.array(ref, np.float32), np.array(still), np.array(rates, np.float32)


INPUT_DIM = WINDOW * 2 + 1


def model_inputs(obs: np.ndarray, t: np.ndarray, fps: float) -> np.ndarray:
    """Model inputs for one sequence obs (T,2) at indices t (M,) -> (M, 33).

    Layout: 16 (dx, dy) pairs oldest -> newest, relative to the newest sample
    and multiplied by SCALE, then fps / 30. Before the sequence start the first
    sample is repeated (the C++ side does the same).
    """
    idx = t[:, None] - np.arange(WINDOW - 1, -1, -1)[None, :]
    idx = np.clip(idx, 0, None)
    w = (obs[idx] - obs[t][:, None, :]) * SCALE
    rate = np.full((len(t), 1), fps / 30.0)
    return np.concatenate([w.reshape(len(t), -1), rate], axis=1).astype(np.float32)
