"""Procedural gesture dataset: class-conditional hand poses -> landmarks.

Each class samples finger joint angles from anatomical ranges; pinches place
the thumb with inverse kinematics. Every sample gets per-hand anatomical
variation, random mirroring, a random 3D rotation and landmark noise shaped
like MediaPipe's world-landmark error.
"""

from __future__ import annotations

import numpy as np

from . import GESTURE_CLASSES
from .handmodel import FINGERS, HandPose, forward, neutral_variation, random_rotation, solve_thumb_ik

MOVE, PINCH_INDEX, PINCH_MIDDLE, SCROLL, FIST = range(5)

# Finger state ranges in degrees: (MCP, PIP, DIP) each (lo, hi).
EXT = ((-10, 25), (0, 25), (0, 20))
HALF = ((15, 55), (20, 70), (10, 50))
CURL = ((55, 95), (70, 110), (35, 75))
PINCH_FLEX = ((15, 65), (20, 85), (5, 55))

# Thumb angle ranges (degrees): azimuth, elevation, MCP, IP.
THUMB_OPEN = ((25, 75), (0, 50), (-5, 40), (-10, 50))
THUMB_UP = ((0, 30), (-10, 20), (-5, 15), (-10, 15))

PINCH_MAX = 0.15      # thumb tip <-> fingertip distance for a pinch (palm units, ~1.4 cm)
CLEAR_MIN = 0.35      # minimum distance for "not touching" (~3.4 cm)


def _u(rng, lo_hi, n):
    lo, hi = lo_hi
    return np.radians(rng.uniform(lo, hi, size=n))


def _finger_state(rng, ranges, n):
    return np.stack([_u(rng, r, n) for r in ranges], axis=1)


class _Builder:
    """Accumulates poses for one class with vectorised sampling."""

    def __init__(self, n, rng):
        self.n = n
        self.rng = rng
        rigid, bones = neutral_variation(n, rng)
        self.pose = HandPose(
            abd=np.radians(rng.uniform(-8, 8, size=(n, 4))),
            flex=np.zeros((n, 4, 3)),
            thumb=np.stack([_u(rng, r, n) for r in THUMB_OPEN], axis=1),
            rigid_jitter=rigid,
            bone_scale=bones,
        )

    def set_finger(self, name, ranges, mask=None):
        i = FINGERS.index(name)
        vals = _finger_state(self.rng, ranges, self.n)
        if mask is None:
            self.pose.flex[:, i] = vals
        else:
            self.pose.flex[mask, i] = vals[mask]

    def set_random_state(self, name, choices):
        pick = self.rng.integers(0, len(choices), size=self.n)
        for c, ranges in enumerate(choices):
            self.set_finger(name, ranges, pick == c)

    def set_thumb(self, ranges, mask=None):
        vals = np.stack([_u(self.rng, r, self.n) for r in ranges], axis=1)
        if mask is None:
            self.pose.thumb[:] = vals
        else:
            self.pose.thumb[mask] = vals[mask]

    def thumb_to(self, target, mask=None):
        """IK the thumb tip onto target (N,3); returns per-sample error."""
        if mask is None:
            mask = np.ones(self.n, dtype=bool)
        err = np.zeros(self.n)
        if mask.any():
            sub = HandPose(self.pose.abd[mask], self.pose.flex[mask], self.pose.thumb[mask],
                           self.pose.rigid_jitter[mask], self.pose.bone_scale[mask])
            err[mask] = solve_thumb_ik(sub, target[mask], rng=self.rng)
            self.pose.thumb[mask] = sub.thumb
        return err

    def landmarks(self):
        return forward(self.pose)


def _random_offsets(rng, n, radius):
    d = rng.normal(size=(n, 3))
    d /= np.linalg.norm(d, axis=1, keepdims=True)
    return d * (radius * np.cbrt(rng.uniform(0, 1, size=n)))[:, None]


def _tipdist(lm, a, b):
    return np.linalg.norm(lm[:, a] - lm[:, b], axis=1)


def sample_class(cls: int, n: int, rng: np.random.Generator) -> np.ndarray:
    """Returns up to n valid (N', 21, 3) landmark sets for class `cls` (clean, palm units)."""
    b = _Builder(n, rng)

    if cls == MOVE:
        sub = rng.integers(0, 6, size=n)
        for name in FINGERS:  # 0: open
            b.set_finger(name, EXT, sub == 0)
            b.set_finger(name, HALF, sub == 1)  # 1: relaxed
        # 2: pointing
        b.set_finger("index", EXT, sub == 2)
        for name in ("middle", "ring", "pinky"):
            b.set_finger(name, CURL, sub == 2)
        # 3: thumb up (fist with the thumb out)
        for name in FINGERS:
            b.set_finger(name, CURL, sub == 3)
        b.set_thumb(THUMB_UP, sub == 3)
        # 4: three fingers
        for name in ("index", "middle", "ring"):
            b.set_finger(name, EXT, sub == 4)
        b.set_finger("pinky", CURL, sub == 4)
        # 5: any mix of extended / relaxed
        mixed = sub == 5
        for name in FINGERS:
            pick = rng.integers(0, 2, size=n)
            b.set_finger(name, EXT, mixed & (pick == 0))
            b.set_finger(name, HALF, mixed & (pick == 1))
        # Half of the pointing hands tuck the thumb over the middle finger.
        lm = b.landmarks()
        tuck = (sub == 2) & (rng.uniform(size=n) < 0.5)
        target = 0.5 * (lm[:, 10] + lm[:, 11]) + np.array([0.0, 0.0, 0.08]) + _random_offsets(rng, n, 0.06)
        err = b.thumb_to(target, tuck)
        lm = b.landmarks()
        ok = (err < 0.08) & (_tipdist(lm, 4, 8) > CLEAR_MIN) & (_tipdist(lm, 4, 12) > CLEAR_MIN)

    elif cls == PINCH_INDEX:
        b.set_finger("index", PINCH_FLEX)
        for name in ("middle", "ring", "pinky"):
            b.set_random_state(name, (EXT, HALF, CURL))
        lm = b.landmarks()
        target = lm[:, 8] + _random_offsets(rng, n, 0.10)
        err = b.thumb_to(target)
        lm = b.landmarks()
        ok = (err < 0.03) & (_tipdist(lm, 4, 8) < PINCH_MAX) & (_tipdist(lm, 4, 12) > 0.2)

    elif cls == PINCH_MIDDLE:
        b.set_finger("middle", PINCH_FLEX)
        b.set_random_state("index", (EXT, HALF))
        for name in ("ring", "pinky"):
            b.set_random_state(name, (EXT, HALF, CURL))
        lm = b.landmarks()
        target = lm[:, 12] + _random_offsets(rng, n, 0.10)
        err = b.thumb_to(target)
        lm = b.landmarks()
        ok = (err < 0.03) & (_tipdist(lm, 4, 12) < PINCH_MAX) & (_tipdist(lm, 4, 8) > 0.3)

    elif cls == SCROLL:
        b.set_finger("index", EXT)
        b.set_finger("middle", EXT)
        b.pose.abd[:, 0] = np.radians(rng.uniform(-10, 14, size=n))
        b.pose.abd[:, 1] = np.radians(rng.uniform(-12, 8, size=n))
        b.set_finger("ring", CURL)
        b.set_finger("pinky", CURL)
        tucked = rng.uniform(size=n) < 0.5
        lm = b.landmarks()
        target = 0.5 * (lm[:, 14] + lm[:, 15]) + np.array([0.0, 0.0, 0.08]) + _random_offsets(rng, n, 0.08)
        err = b.thumb_to(target, tucked)
        b.set_thumb(((20, 60), (10, 60), (10, 50), (10, 60)), ~tucked)
        lm = b.landmarks()
        ok = (err < 0.08) & (_tipdist(lm, 4, 8) > CLEAR_MIN) & (_tipdist(lm, 4, 12) > CLEAR_MIN)

    elif cls == FIST:
        for name in FINGERS:
            b.set_finger(name, CURL)
        over = rng.uniform(size=n) < 0.6
        lm = b.landmarks()
        target = np.where(
            over[:, None],
            0.5 * (lm[:, 10] + lm[:, 11]) + np.array([0.0, 0.0, 0.08]),
            lm[:, 6] + np.array([0.06, 0.0, 0.05]),
        ) + _random_offsets(rng, n, 0.06)
        err = b.thumb_to(target)
        lm = b.landmarks()
        ok = err < 0.08

    else:
        raise ValueError(f"unknown class {cls}")

    return lm[ok]


def augment(lm: np.ndarray, rng: np.random.Generator, noise=(0.01, 0.045)) -> np.ndarray:
    """Mirror half the hands, rotate randomly, scale to metres, add landmark noise.

    Also mimics two systematic errors seen in MediaPipe world landmarks: a
    depth scale that is off, and a hand compressed sideways when seen from
    the back (see tests/data/real_landmarks.json, *_hands.jpg).
    """
    n = lm.shape[0]
    out = lm.copy()
    out[:, :, 2] *= rng.uniform(0.7, 1.3, size=(n, 1))
    squeeze = rng.uniform(size=n) < 0.3
    out[squeeze, :, 0] *= rng.uniform(0.4, 1.0, size=(int(squeeze.sum()), 1))
    mirror = rng.uniform(size=n) < 0.5
    out[mirror, :, 0] *= -1.0
    out = np.einsum("nij,nkj->nki", random_rotation(n, rng), out)
    sigma = rng.uniform(*noise, size=(n, 1, 1))
    out = out + rng.normal(size=out.shape) * sigma
    return out * rng.uniform(0.08, 0.11, size=(n, 1, 1))  # palm length in metres


def make_dataset(per_class: int, seed: int):
    """Returns (landmarks (N,21,3) float32, labels (N,) int64), balanced over classes."""
    rng = np.random.default_rng(seed)
    xs, ys = [], []
    for cls in range(len(GESTURE_CLASSES)):
        have = 0
        chunks = []
        while have < per_class:
            lm = sample_class(cls, max(256, int((per_class - have) * 1.6)), rng)
            chunks.append(lm)
            have += len(lm)
        lm = np.concatenate(chunks)[:per_class]
        xs.append(augment(lm, rng))
        ys.append(np.full(per_class, cls, dtype=np.int64))
    x = np.concatenate(xs).astype(np.float32)
    y = np.concatenate(ys)
    perm = rng.permutation(len(y))
    return x[perm], y[perm]
