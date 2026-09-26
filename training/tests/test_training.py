"""Fast checks of the procedural data, features and ONNX export."""

import json
from pathlib import Path

import numpy as np
import onnxruntime as ort
import pytest

from mausely_train import GESTURE_CLASSES
from mausely_train.features import NUM_FEATURES, compute_features
from mausely_train.handmodel import HandPose, forward, neutral_variation, random_rotation
from mausely_train.one_euro import OneEuro2D
from mausely_train.onnx_mlp import build_mlp
from mausely_train.synth_gestures import CLEAR_MIN, PINCH_MAX, augment, sample_class
from mausely_train.synth_motion import SCALE, WINDOW, intended_path, make_sequences, model_inputs

ROOT = Path(__file__).resolve().parents[2]


def _flat_hand(n=1):
    rng = np.random.default_rng(0)
    rigid, bones = neutral_variation(n, rng, jitter=0.0, bone_jitter=0.0)
    return HandPose(np.zeros((n, 4)), np.zeros((n, 4, 3)), np.zeros((n, 4)), rigid, bones)


def test_flat_hand_is_planar_and_palm_is_unit_length():
    pose = _flat_hand()
    pose.thumb[:, 1] = 0.0  # no elevation
    lm = forward(pose)[0]
    assert np.linalg.norm(lm[9] - lm[0]) == pytest.approx(1.0)
    # Unflexed fingers stay in the plane of their MCP joint.
    for mcp in (5, 9, 13, 17):
        assert lm[mcp + 1 : mcp + 4, 2] == pytest.approx([lm[mcp, 2]] * 3)
    # Extended fingers point "up" (+y) from their MCP.
    for tip, mcp in ((8, 5), (12, 9), (16, 13), (20, 17)):
        assert lm[tip, 1] > lm[mcp, 1] + 0.5


def test_features_are_invariant():
    rng = np.random.default_rng(1)
    lm = sample_class(0, 50, rng)[:10]
    f0 = compute_features(lm)
    moved = np.einsum("nij,nkj->nki", random_rotation(len(lm), rng), lm) * 2.5 + 0.3
    moved[:, :, 0] *= -1  # mirror
    assert compute_features(moved) == pytest.approx(f0, abs=1e-4)
    assert f0.shape == (len(lm), NUM_FEATURES)


@pytest.mark.parametrize("cls", range(len(GESTURE_CLASSES)))
def test_sampler_respects_class_geometry(cls):
    lm = sample_class(cls, 400, np.random.default_rng(cls))
    assert len(lm) > 100
    thumb_index = np.linalg.norm(lm[:, 4] - lm[:, 8], axis=1)
    thumb_middle = np.linalg.norm(lm[:, 4] - lm[:, 12], axis=1)
    if cls == 1:
        assert thumb_index.max() < PINCH_MAX
    elif cls == 2:
        assert thumb_middle.max() < PINCH_MAX
    elif cls in (0, 3):
        assert thumb_index.min() > CLEAR_MIN and thumb_middle.min() > CLEAR_MIN


def test_augment_keeps_shape_and_scales_to_metres():
    lm = sample_class(0, 100, np.random.default_rng(3))
    out = augment(lm, np.random.default_rng(4))
    assert out.shape == lm.shape
    palm = np.linalg.norm(out[:, 9] - out[:, 0], axis=1)
    assert 0.05 < np.median(palm) < 0.15


def test_onnx_mlp_matches_numpy(tmp_path):
    rng = np.random.default_rng(5)
    w1, b1 = rng.normal(size=(6, 4)), rng.normal(size=4)
    w2, b2 = rng.normal(size=(4, 3)), rng.normal(size=3)
    mean, std = rng.normal(size=6), rng.uniform(0.5, 2, size=6)
    model = build_mlp([(w1, b1), (w2, b2)], "x", 6, "y", mean, std, softmax=True, metadata={"k": "v"})
    path = tmp_path / "m.onnx"
    path.write_bytes(model.SerializeToString())
    x = rng.normal(size=(7, 6)).astype(np.float32)
    got = ort.InferenceSession(str(path)).run(None, {"x": x})[0]
    h = np.maximum(((x - mean) / std) @ w1 + b1, 0) @ w2 + b2
    ref = np.exp(h - h.max(1, keepdims=True))
    ref /= ref.sum(1, keepdims=True)
    assert got == pytest.approx(ref, abs=1e-5)


def test_motion_windows_pad_with_first_sample():
    obs = np.arange(40, dtype=np.float32).reshape(20, 2)
    x = model_inputs(obs, np.array([0, 5]), 30.0)
    assert x.shape == (2, WINDOW * 2 + 1)
    assert np.all(x[0, :-1] == 0)            # window before the start repeats sample 0
    assert x[1, -3:-1].tolist() == [0, 0]     # newest sample is the origin
    assert x[1, 0] == pytest.approx((0 - 10) * SCALE)
    assert x[1, -1] == pytest.approx(1.0)


def test_trajectories_stay_in_region_and_mark_dwells():
    path, still = intended_path(600, np.random.default_rng(6))
    assert path.shape == (600, 2) and still.dtype == bool
    assert still.any() and (~still).any()
    obs, ref, _, rates = make_sequences(3, 100, 7)
    assert obs.shape == ref.shape == (3, 100, 2) and rates.shape == (3,)


def test_one_euro_holds_still_and_follows_steps():
    f = OneEuro2D(1.0, 20.0)
    out = [f(np.array([0.5, 0.5]), 1 / 30) for _ in range(10)]
    assert np.allclose(out[-1], [0.5, 0.5])
    for _ in range(30):
        y = f(np.array([0.8, 0.5]), 1 / 30)
    assert y[0] == pytest.approx(0.8, abs=0.01)


def test_committed_models_have_expected_interface():
    g = ort.InferenceSession(str(ROOT / "models" / "gesture_mlp.onnx"))
    assert g.get_inputs()[0].name == "features" and g.get_inputs()[0].shape[1] == NUM_FEATURES
    meta = g.get_modelmeta().custom_metadata_map
    assert meta["classes"] == ",".join(GESTURE_CLASSES) and meta["feature_version"] == "1"
    s = ort.InferenceSession(str(ROOT / "models" / "smoother.onnx"))
    assert s.get_inputs()[0].shape[1] == WINDOW * 2 + 1
    assert s.get_modelmeta().custom_metadata_map["window"] == str(WINDOW)


def test_real_photo_report_is_all_correct():
    report = json.loads((ROOT / "models" / "gesture_report.json").read_text())
    for name, r in report["real_photos"].items():
        assert r["predicted"] == r["expected"], name
    assert report["synthetic_val_accuracy"] > 0.97
