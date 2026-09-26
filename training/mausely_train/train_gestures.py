"""Trains the gesture classifier and exports models/gesture_mlp.onnx.

    python -m mausely_train.train_gestures                 # synthetic data only
    python -m mausely_train.train_gestures --recordings %APPDATA%/Mausely/recordings
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
import onnx
import onnxruntime as ort
import torch
from torch import nn

from . import FEATURE_VERSION, GESTURE_CLASSES
from .features import NUM_FEATURES, compute_features
from .onnx_mlp import build_mlp, torch_mlp_weights
from .recordings import load_recordings
from .synth_gestures import make_dataset

ROOT = Path(__file__).resolve().parents[2]
# Photos from MediaPipe's test assets and the class a user would expect.
REAL_EXPECTED = {
    "fist.jpg": "FIST",
    "pointing_up.jpg": "MOVE",
    "pointing_up_rotated.jpg": "MOVE",
    "victory.jpg": "SCROLL",
    "thumb_up.jpg": "MOVE",
    "right_hands.jpg": "MOVE",
    "left_hands.jpg": "MOVE",
}


def make_model() -> nn.Sequential:
    return nn.Sequential(
        nn.Linear(NUM_FEATURES, 64), nn.ReLU(),
        nn.Linear(64, 32), nn.ReLU(),
        nn.Linear(32, len(GESTURE_CLASSES)),
    )


def train(model, xtr, ytr, xva, yva, epochs, seed, log=print):
    torch.manual_seed(seed)
    opt = torch.optim.AdamW(model.parameters(), lr=3e-3, weight_decay=1e-4)
    steps = epochs * ((len(xtr) + 511) // 512)
    sched = torch.optim.lr_scheduler.OneCycleLR(opt, max_lr=3e-3, total_steps=steps)
    loss_fn = nn.CrossEntropyLoss(label_smoothing=0.05)
    xtr_t, ytr_t = torch.from_numpy(xtr), torch.from_numpy(ytr)
    xva_t = torch.from_numpy(xva)
    for ep in range(epochs):
        model.train()
        perm = torch.randperm(len(xtr_t))
        total = 0.0
        for i in range(0, len(perm), 512):
            idx = perm[i : i + 512]
            opt.zero_grad()
            loss = loss_fn(model(xtr_t[idx]), ytr_t[idx])
            loss.backward()
            opt.step()
            sched.step()
            total += loss.item() * len(idx)
        model.eval()
        with torch.no_grad():
            acc = (model(xva_t).argmax(1).numpy() == yva).mean()
        if ep % 5 == 4 or ep == epochs - 1:
            log(f"epoch {ep + 1:3d}  loss {total / len(xtr):.4f}  val acc {acc:.4f}")
    return model


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--per-class", type=int, default=30000)
    ap.add_argument("--epochs", type=int, default=40)
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--recordings", type=str, default=None, help="folder with dashboard CSV recordings")
    ap.add_argument("--out", type=Path, default=ROOT / "models" / "gesture_mlp.onnx")
    args = ap.parse_args(argv)
    torch.set_num_threads(4)

    print("generating synthetic hands ...")
    lm_tr, y_tr = make_dataset(args.per_class, args.seed)
    lm_va, y_va = make_dataset(max(500, args.per_class // 10), args.seed + 1000)
    x_tr, x_va = compute_features(lm_tr), compute_features(lm_va)

    real_x = real_y = None
    if args.recordings:
        rec_lm, rec_y = load_recordings(args.recordings)
        print(f"recordings: {len(rec_y)} samples " +
              str({c: int((rec_y == i).sum()) for i, c in enumerate(GESTURE_CLASSES)}))
        if len(rec_y):
            rng = np.random.default_rng(args.seed)
            perm = rng.permutation(len(rec_y))
            cut = int(0.8 * len(perm))
            rec_f = compute_features(rec_lm)
            real_x, real_y = rec_f[perm[cut:]], rec_y[perm[cut:]]
            # Real data is scarce but precious: repeat it to ~25% of the mix.
            reps = max(1, int(0.25 * len(x_tr) / max(1, cut)))
            x_tr = np.concatenate([x_tr] + [rec_f[perm[:cut]]] * reps)
            y_tr = np.concatenate([y_tr] + [rec_y[perm[:cut]]] * reps)

    mean = x_tr.mean(0).astype(np.float32)
    std = (x_tr.std(0) + 1e-6).astype(np.float32)
    norm = lambda a: ((a - mean) / std).astype(np.float32)  # noqa: E731

    model = train(make_model(), norm(x_tr), y_tr, norm(x_va), y_va, args.epochs, args.seed)
    model.eval()

    with torch.no_grad():
        pred = model(torch.from_numpy(norm(x_va))).argmax(1).numpy()
    conf = np.zeros((5, 5), dtype=int)
    for t, p in zip(y_va, pred):
        conf[t, p] += 1
    val_acc = float((pred == y_va).mean())

    onnx_model = build_mlp(
        torch_mlp_weights([m for m in model if isinstance(m, nn.Linear)]),
        input_name="features", input_dim=NUM_FEATURES, output_name="probs",
        mean=mean, std=std, softmax=True,
        metadata={"feature_version": FEATURE_VERSION, "classes": ",".join(GESTURE_CLASSES),
                  "producer": "mausely_train.train_gestures"},
    )
    args.out.parent.mkdir(parents=True, exist_ok=True)
    onnx.save(onnx_model, args.out)

    # Parity: onnxruntime vs torch.
    sess = ort.InferenceSession(str(args.out))
    probs_ort = sess.run(None, {"features": x_va[:2000]})[0]
    with torch.no_grad():
        probs_t = torch.softmax(model(torch.from_numpy(norm(x_va[:2000]))), 1).numpy()
    parity = float(np.abs(probs_ort - probs_t).max())

    # Real photos (landmarks produced by the C++ pipeline).
    real = json.loads((ROOT / "tests" / "data" / "real_landmarks.json").read_text())
    real_report = {}
    for e in real:
        p = sess.run(None, {"features": compute_features(np.array(e["world"]))[None]})[0][0]
        real_report[e["file"]] = {
            "expected": REAL_EXPECTED.get(e["file"]),
            "predicted": GESTURE_CLASSES[int(p.argmax())],
            "probs": [round(float(v), 4) for v in p],
        }

    report = {
        "classes": GESTURE_CLASSES,
        "synthetic_val_accuracy": val_acc,
        "confusion": conf.tolist(),
        "onnx_torch_max_abs_diff": parity,
        "real_photos": real_report,
        "train_samples": int(len(y_tr)),
        "params": int(sum(p.numel() for p in model.parameters())),
    }
    if real_x is not None:
        with torch.no_grad():
            rp = model(torch.from_numpy(norm(real_x))).argmax(1).numpy()
        report["recordings_holdout_accuracy"] = float((rp == real_y).mean())
    (args.out.with_name("gesture_report.json")).write_text(json.dumps(report, indent=2))

    # Feature/probability vectors for the C++ parity test.
    rng = np.random.default_rng(123)
    pick = rng.choice(len(lm_va), 24, replace=False)
    vectors = [{
        "world": lm_va[i].astype(float).round(6).tolist(),
        "features": compute_features(lm_va[i].round(6)).astype(float).tolist(),
        "probs": sess.run(None, {"features": compute_features(lm_va[i].round(6))[None]})[0][0].astype(float).tolist(),
    } for i in pick]
    (ROOT / "tests" / "data" / "feature_vectors.json").write_text(json.dumps(vectors))

    print(json.dumps({k: report[k] for k in ("synthetic_val_accuracy", "onnx_torch_max_abs_diff", "params")}))
    print("confusion (rows = truth):")
    for c, row in zip(GESTURE_CLASSES, conf):
        print(f"  {c:13s} {row}")
    for f, r in real_report.items():
        flag = "ok " if r["predicted"] == r["expected"] else "BAD"
        print(f"  {flag} {f:24s} expected {r['expected']:7s} got {r['predicted']:12s} {r['probs']}")
    print(f"saved {args.out}")


if __name__ == "__main__":
    main()
