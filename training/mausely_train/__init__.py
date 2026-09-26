"""Training pipeline for Mausely's small on-device models.

Everything here is procedural: hand poses come from a kinematic hand model and
cursor trajectories from a human-motion simulator, so no third-party dataset
(and no dataset licence) is involved. Optional user recordings exported by the
dashboard can be mixed in to fine-tune the gesture classifier.
"""

GESTURE_CLASSES = ["MOVE", "PINCH_INDEX", "PINCH_MIDDLE", "SCROLL", "FIST"]
FEATURE_VERSION = 1
