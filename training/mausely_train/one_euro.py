"""2D One Euro filter (Casiez et al., CHI 2012) -- the classic cursor smoother.

Uses one shared cutoff driven by the 2D speed so both axes behave the same.
Must match src/ai/one_euro.h exactly (see tests/data/one_euro_vectors.json).
"""

from __future__ import annotations

import math

import numpy as np


def _alpha(cutoff: float, dt: float) -> float:
    tau = 1.0 / (2.0 * math.pi * cutoff)
    return 1.0 / (1.0 + tau / dt)


class OneEuro2D:
    def __init__(self, min_cutoff: float = 1.0, beta: float = 20.0, d_cutoff: float = 1.0):
        self.min_cutoff, self.beta, self.d_cutoff = min_cutoff, beta, d_cutoff
        self.reset()

    def reset(self):
        self.x = None
        self.dx = np.zeros(2)

    def __call__(self, p, dt: float) -> np.ndarray:
        p = np.asarray(p, dtype=np.float64)
        if self.x is None or dt <= 0:
            self.x = p.copy()
            self.dx = np.zeros(2)
            return self.x.copy()
        ad = _alpha(self.d_cutoff, dt)
        self.dx = ad * (p - self.x) / dt + (1.0 - ad) * self.dx
        cutoff = self.min_cutoff + self.beta * float(np.linalg.norm(self.dx))
        a = _alpha(cutoff, dt)
        self.x = a * p + (1.0 - a) * self.x
        return self.x.copy()


def filter_sequence(obs: np.ndarray, fps: float, min_cutoff: float, beta: float) -> np.ndarray:
    f = OneEuro2D(min_cutoff, beta)
    return np.stack([f(p, 1.0 / fps) for p in obs])
