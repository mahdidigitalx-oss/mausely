"""Hand feature vector, version 1 (must stay identical to src/ai/features.cpp).

Input: 21 MediaPipe world landmarks (any unit, any rotation, either hand).
Output: 81 float32 values that are invariant to translation, rotation, scale
and left/right mirroring:

  [0:15]  flexion angle at each finger joint (thumb, index, middle, ring, pinky; 3 each)
  [15:25] fingertip pair distances / palm        (pairs of tips 4,8,12,16,20 in order)
  [25:30] fingertip -> wrist distance / palm
  [30:35] fingertip -> palm centre distance / palm
  [35:39] angle between neighbouring finger directions
  [39:81] (x, y) of every landmark in the anatomical hand frame / palm
"""

from __future__ import annotations

import numpy as np

NUM_FEATURES = 81
TIPS = (4, 8, 12, 16, 20)
CHAINS = ((0, 1, 2, 3, 4), (0, 5, 6, 7, 8), (0, 9, 10, 11, 12), (0, 13, 14, 15, 16), (0, 17, 18, 19, 20))
# Finger direction vectors (from, to) for the spread angles.
DIRECTIONS = ((1, 4), (5, 8), (9, 12), (13, 16), (17, 20))
PALM_POINTS = (0, 5, 9, 13, 17)


def _angle(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    na = np.linalg.norm(a, axis=-1)
    nb = np.linalg.norm(b, axis=-1)
    c = np.sum(a * b, axis=-1) / np.maximum(na * nb, 1e-12)
    return np.arccos(np.clip(c, -1.0, 1.0))


def compute_features(world: np.ndarray) -> np.ndarray:
    """world: (21, 3) or (N, 21, 3) -> (81,) or (N, 81) float32."""
    p = np.asarray(world, dtype=np.float64)
    single = p.ndim == 2
    if single:
        p = p[None]
    n = p.shape[0]
    wrist = p[:, 0]
    palm = np.maximum(np.linalg.norm(p[:, 9] - wrist, axis=1), 1e-9)
    out = np.empty((n, NUM_FEATURES), dtype=np.float64)

    k = 0
    for chain in CHAINS:
        for j in range(1, 4):
            a = p[:, chain[j]] - p[:, chain[j - 1]]
            b = p[:, chain[j + 1]] - p[:, chain[j]]
            out[:, k] = _angle(a, b)
            k += 1

    for i in range(5):
        for j in range(i + 1, 5):
            out[:, k] = np.linalg.norm(p[:, TIPS[i]] - p[:, TIPS[j]], axis=1) / palm
            k += 1

    for t in TIPS:
        out[:, k] = np.linalg.norm(p[:, t] - wrist, axis=1) / palm
        k += 1

    centre = p[:, list(PALM_POINTS)].mean(axis=1)
    for t in TIPS:
        out[:, k] = np.linalg.norm(p[:, t] - centre, axis=1) / palm
        k += 1

    for i in range(4):
        a = p[:, DIRECTIONS[i][1]] - p[:, DIRECTIONS[i][0]]
        b = p[:, DIRECTIONS[i + 1][1]] - p[:, DIRECTIONS[i + 1][0]]
        out[:, k] = _angle(a, b)
        k += 1

    # Anatomical frame: y = wrist -> middle MCP, x = pinky MCP -> index MCP
    # made orthogonal to y. Projections on x and y do not depend on handedness.
    y = (p[:, 9] - wrist) / palm[:, None]
    across = p[:, 5] - p[:, 17]
    x = across - np.sum(across * y, axis=1, keepdims=True) * y
    x /= np.maximum(np.linalg.norm(x, axis=1, keepdims=True), 1e-12)
    rel = p - wrist[:, None]
    out[:, k : k + 42 : 2] = np.einsum("nij,nj->ni", rel, x) / palm[:, None]
    out[:, k + 1 : k + 42 : 2] = np.einsum("nij,nj->ni", rel, y) / palm[:, None]
    k += 42
    assert k == NUM_FEATURES

    out = out.astype(np.float32)
    return out[0] if single else out
