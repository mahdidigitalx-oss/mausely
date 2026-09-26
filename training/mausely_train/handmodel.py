"""Batched kinematic model of a right hand in MediaPipe landmark layout.

Units are "palm lengths" (wrist -> middle-finger MCP == 1). The anatomical
frame is x = radial (thumb side), y = distal (towards the fingers),
z = palmar (x cross y). Fingers flex towards +z.

Proportions were measured from MediaPipe world landmarks of real hands
(tests/data/real_landmarks.json), so generated poses match what the landmark
model produces at run time.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

FINGERS = ("index", "middle", "ring", "pinky")
FINGER_BASE_LANDMARK = {"index": 5, "middle": 9, "ring": 13, "pinky": 17}

# Rigid palm points (palm units), measured from real MediaPipe output.
MCP = {
    "index": (0.262, 1.023, 0.054),
    "middle": (0.0, 1.0, 0.0),
    "ring": (-0.270, 0.891, -0.012),
    "pinky": (-0.444, 0.729, 0.054),
}
THUMB_CMC = (0.287, 0.293, 0.117)

# Rest direction of each finger in the palm plane (radians towards +x).
SPLAY = {"index": 0.08, "middle": 0.0, "ring": -0.08, "pinky": -0.18}

# Phalanx lengths (proximal, middle, distal) in palm units.
BONES = {
    "index": (0.29, 0.23, 0.26),
    "middle": (0.31, 0.28, 0.28),
    "ring": (0.30, 0.25, 0.26),
    "pinky": (0.23, 0.22, 0.19),
}
THUMB_BONES = (0.33, 0.38, 0.29)  # CMC->MCP, MCP->IP, IP->TIP

# Direction the thumb curls towards when flexing (ulnar + palmar).
_THUMB_FLEX_DIR = np.array([-0.7, 0.15, 0.7])

# Thumb joint limits used by IK, radians: azimuth, elevation, MCP, IP.
THUMB_LIMITS = np.radians(np.array([[-25.0, 85.0], [-15.0, 95.0], [-10.0, 80.0], [-20.0, 95.0]]))


@dataclass
class HandPose:
    """Joint angles for N hands (radians). Arrays have shape (N,) unless noted."""

    abd: np.ndarray    # (N, 4) finger abduction, order FINGERS
    flex: np.ndarray   # (N, 4, 3) MCP, PIP, DIP flexion per finger
    thumb: np.ndarray  # (N, 4) azimuth, elevation, MCP flexion, IP flexion
    rigid_jitter: np.ndarray  # (N, 5, 3) offsets of the 4 MCPs + thumb CMC
    bone_scale: np.ndarray    # (N, 5, 3) multiplicative length jitter (thumb last)

    @property
    def n(self) -> int:
        return self.abd.shape[0]


def neutral_variation(n: int, rng: np.random.Generator, jitter: float = 0.02, bone_jitter: float = 0.08):
    """Per-hand anatomical variation: rigid point offsets and bone scaling."""
    rigid = rng.normal(0.0, jitter, size=(n, 5, 3))
    bones = rng.uniform(1.0 - bone_jitter, 1.0 + bone_jitter, size=(n, 5, 3))
    return rigid, bones


def _finger_chain(base, abd, flex, lengths):
    """Planar finger chain. base (N,3), abd (N,), flex (N,3), lengths (N,3) -> (N,3,3) joints."""
    u = np.stack([np.sin(abd), np.cos(abd), np.zeros_like(abd)], axis=1)
    nrm = np.array([0.0, 0.0, 1.0])
    phi = np.cumsum(flex, axis=1)
    joints = []
    p = base
    for k in range(3):
        d = np.cos(phi[:, k])[:, None] * u + np.sin(phi[:, k])[:, None] * nrm
        p = p + lengths[:, k : k + 1] * d
        joints.append(p)
    return np.stack(joints, axis=1)


def thumb_chain(cmc, thumb, lengths):
    """Thumb joints MCP, IP, TIP for base `cmc` (N,3), angles (N,4), lengths (N,3)."""
    az, el, mcp, ip = thumb[:, 0], thumb[:, 1], thumb[:, 2], thumb[:, 3]
    t0 = np.stack([np.sin(az) * np.cos(el), np.cos(az) * np.cos(el), np.sin(el)], axis=1)
    f = np.broadcast_to(_THUMB_FLEX_DIR, t0.shape)
    p = f - np.sum(f * t0, axis=1, keepdims=True) * t0
    p /= np.linalg.norm(p, axis=1, keepdims=True) + 1e-9
    j1 = cmc + lengths[:, 0:1] * t0
    phi1 = mcp
    d1 = np.cos(phi1)[:, None] * t0 + np.sin(phi1)[:, None] * p
    j2 = j1 + lengths[:, 1:2] * d1
    phi2 = mcp + ip
    d2 = np.cos(phi2)[:, None] * t0 + np.sin(phi2)[:, None] * p
    j3 = j2 + lengths[:, 2:3] * d2
    return np.stack([j1, j2, j3], axis=1)


def forward(pose: HandPose) -> np.ndarray:
    """Returns (N, 21, 3) landmarks in palm units."""
    n = pose.n
    out = np.zeros((n, 21, 3))
    for fi, name in enumerate(FINGERS):
        base = np.asarray(MCP[name]) + pose.rigid_jitter[:, fi]
        lengths = np.asarray(BONES[name]) * pose.bone_scale[:, fi]
        chain = _finger_chain(base, SPLAY[name] + pose.abd[:, fi], pose.flex[:, fi], lengths)
        b = FINGER_BASE_LANDMARK[name]
        out[:, b] = base
        out[:, b + 1 : b + 4] = chain
    cmc = np.asarray(THUMB_CMC) + pose.rigid_jitter[:, 4]
    out[:, 1] = cmc
    out[:, 2:5] = thumb_chain(cmc, pose.thumb, np.asarray(THUMB_BONES) * pose.bone_scale[:, 4])
    return out


def solve_thumb_ik(pose: HandPose, target: np.ndarray, steps: int = 300, lr: float = 0.04,
                   rng: np.random.Generator | None = None) -> np.ndarray:
    """Adjusts pose.thumb in place so the thumb tip reaches `target` (N,3).

    Batched Adam on finite-difference gradients, clipped to THUMB_LIMITS.
    Returns the remaining tip error per hand (palm units).
    """
    cmc = np.asarray(THUMB_CMC) + pose.rigid_jitter[:, 4]
    lengths = np.asarray(THUMB_BONES) * pose.bone_scale[:, 4]
    lo, hi = THUMB_LIMITS[:, 0], THUMB_LIMITS[:, 1]
    theta = pose.thumb.copy()
    if rng is not None:
        theta = rng.uniform(lo, hi, size=theta.shape) * 0.5 + theta * 0.5

    def err(th):
        tip = thumb_chain(cmc, th, lengths)[:, 2]
        return np.sum((tip - target) ** 2, axis=1)

    m = np.zeros_like(theta)
    v = np.zeros_like(theta)
    eps = 1e-4
    for t in range(1, steps + 1):
        f0 = err(theta)
        grad = np.empty_like(theta)
        for k in range(4):
            th = theta.copy()
            th[:, k] += eps
            grad[:, k] = (err(th) - f0) / eps
        m = 0.9 * m + 0.1 * grad
        v = 0.999 * v + 0.001 * grad**2
        mh = m / (1 - 0.9**t)
        vh = v / (1 - 0.999**t)
        theta = np.clip(theta - lr * mh / (np.sqrt(vh) + 1e-8), lo, hi)
    pose.thumb[:] = theta
    return np.sqrt(err(theta))


def random_rotation(n: int, rng: np.random.Generator) -> np.ndarray:
    """Uniform random rotation matrices (N,3,3)."""
    q = rng.normal(size=(n, 4))
    q /= np.linalg.norm(q, axis=1, keepdims=True)
    w, x, y, z = q.T
    return np.stack([
        np.stack([1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)], axis=1),
        np.stack([2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)], axis=1),
        np.stack([2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)], axis=1),
    ], axis=1)
