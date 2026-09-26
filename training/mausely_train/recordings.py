"""Loads gesture recordings exported by the Mausely dashboard (Recorder panel).

CSV columns: label, timestamp_us, handedness, w0x, w0y, w0z, ..., w20z,
i0x, i0y, i0z, ..., i20z (world landmarks in metres, image landmarks in pixels).
"""

from __future__ import annotations

import csv
from pathlib import Path

import numpy as np

from . import GESTURE_CLASSES


def load_recordings(folder: str | Path) -> tuple[np.ndarray, np.ndarray]:
    """Returns (world landmarks (N,21,3) float32, labels (N,) int64) from every CSV in folder."""
    worlds, labels = [], []
    for path in sorted(Path(folder).glob("*.csv")):
        with open(path, newline="", encoding="utf-8") as f:
            for row in csv.DictReader(f):
                label = row["label"].strip().upper()
                if label not in GESTURE_CLASSES:
                    continue
                w = [float(row[f"w{i}{a}"]) for i in range(21) for a in "xyz"]
                worlds.append(np.array(w, dtype=np.float32).reshape(21, 3))
                labels.append(GESTURE_CLASSES.index(label))
    if not worlds:
        return np.zeros((0, 21, 3), np.float32), np.zeros((0,), np.int64)
    return np.stack(worlds), np.array(labels, dtype=np.int64)
