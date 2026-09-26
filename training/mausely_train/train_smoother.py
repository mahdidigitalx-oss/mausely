"""Trains the motion smoother and exports models/smoother.onnx.

The model sees the last 16 raw pointer positions and returns the denoised
current position and a one-frame-ahead prediction. It is compared against a
One Euro filter whose parameters are grid-searched on the same data, and the
result is written to models/smoother_report.json.

    python -m mausely_train.train_smoother
"""

from __future__ import annotations

import argparse
import itertools
import json
from pathlib import Path

import numpy as np
import onnx
import onnxruntime as ort
import torch
from torch import nn

from .one_euro import filter_sequence
from .onnx_mlp import build_mlp, torch_mlp_weights
from .synth_motion import INPUT_DIM, SCALE, WINDOW, make_sequences, model_inputs

ROOT = Path(__file__).resolve().parents[2]
PX = 720.0  # report distances in pixels of a 720p frame


def make_model() -> nn.Sequential:
    return nn.Sequential(nn.Linear(INPUT_DIM, 128), nn.ReLU(), nn.Linear(128, 64), nn.ReLU(), nn.Linear(64, 4))


def build_pairs(obs, ref, rates):
    """Training tensors for consecutive frames (t-1, t) of every sequence."""
    xs, xps, est, pred, dobs, dref = [], [], [], [], [], []
    for o, r, fps in zip(obs, ref, rates):
        t = np.arange(1, len(o) - 1)
        xs.append(model_inputs(o, t, fps))
        xps.append(model_inputs(o, t - 1, fps))
        est.append((r[t] - o[t]) * SCALE)
        pred.append((r[t + 1] - o[t]) * SCALE)
        dobs.append((o[t] - o[t - 1]) * SCALE)
        dref.append((r[t] - r[t - 1]) * SCALE)
    cat = lambda a: torch.from_numpy(np.concatenate(a).astype(np.float32))  # noqa: E731
    return cat(xs), cat(xps), cat(est), cat(pred), cat(dobs), cat(dref)


def train(model, data, epochs, seed, mean, std):
    torch.manual_seed(seed)
    x, xp, est, pred, dobs, dref = data
    mean_t, std_t = torch.from_numpy(mean), torch.from_numpy(std)
    norm = lambda a: (a - mean_t) / std_t  # noqa: E731
    bs = 1024
    opt = torch.optim.AdamW(model.parameters(), lr=2e-3, weight_decay=1e-5)
    sched = torch.optim.lr_scheduler.OneCycleLR(opt, max_lr=2e-3, total_steps=epochs * ((len(x) + bs - 1) // bs))
    for ep in range(epochs):
        perm = torch.randperm(len(x))
        total = 0.0
        for i in range(0, len(perm), bs):
            idx = perm[i : i + bs]
            out = model(norm(x[idx]))
            outp = model(norm(xp[idx]))
            l_est = ((out[:, :2] - est[idx]) ** 2).sum(1).mean()
            l_pred = ((out[:, 2:] - pred[idx]) ** 2).sum(1).mean()
            # Velocity consistency: consecutive outputs must move like the true path.
            vel = dobs[idx] + out[:, :2] - outp[:, :2]
            l_vel = ((vel - dref[idx]) ** 2).sum(1).mean()
            loss = l_est + 0.5 * l_pred + 2.0 * l_vel
            opt.zero_grad()
            loss.backward()
            opt.step()
            sched.step()
            total += loss.item() * len(idx)
        print(f"epoch {ep + 1:3d}  loss {total / len(x):.5f}")
    return model


def run_model(sess, obs, fps, strength=0.0):
    t = np.arange(len(obs))
    out = sess.run(None, {"x": model_inputs(obs, t, fps)})[0]
    est = out[:, :2]
    offset = est + strength * (out[:, 2:] - est)
    return obs + offset / SCALE


def metrics(outs, refs, stills, rates):
    err_all, err_move, jit = [], [], []
    lags = []
    for y, r, s, fps in zip(outs, refs, stills, rates):
        e = np.linalg.norm(y - r, axis=1)
        err_all.append(e)
        err_move.append(e[~s])
        steady = s[1:] & s[:-1]
        jit.append(np.linalg.norm(np.diff(y, axis=0), axis=1)[steady])
        # Lag: shift (in ms) of the reference that best explains the output while moving.
        tt = np.arange(len(r), dtype=np.float64)
        best, best_k = np.inf, 0.0
        for k in np.arange(0.0, 6.01, 0.25):
            shifted = np.stack([np.interp(tt - k, tt, r[:, 0]), np.interp(tt - k, tt, r[:, 1])], 1)
            v = np.mean(np.linalg.norm(y - shifted, axis=1)[~s][8:])
            if v < best:
                best, best_k = v, k
        lags.append(best_k * 1000.0 / fps)
    rms = lambda a: float(np.sqrt(np.mean(np.concatenate(a) ** 2)) * PX)  # noqa: E731
    return {"rmse_px": rms(err_all), "rmse_moving_px": rms(err_move), "jitter_still_px": rms(jit),
            "lag_ms": float(np.median(lags))}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--sequences", type=int, default=1600)
    ap.add_argument("--length", type=int, default=240)
    ap.add_argument("--epochs", type=int, default=24)
    ap.add_argument("--seed", type=int, default=11)
    ap.add_argument("--out", type=Path, default=ROOT / "models" / "smoother.onnx")
    args = ap.parse_args(argv)
    torch.set_num_threads(4)

    print("generating trajectories ...")
    obs, ref, _, rates = make_sequences(args.sequences, args.length, args.seed)
    data = build_pairs(obs, ref, rates)
    mean = data[0].numpy().mean(0)
    std = data[0].numpy().std(0) + 1e-3
    mean[-1], std[-1] = 0.0, 1.0  # keep the frame-rate input as is

    model = train(make_model(), data, args.epochs, args.seed, mean, std)
    model.eval()

    onnx_model = build_mlp(
        torch_mlp_weights([m for m in model if isinstance(m, nn.Linear)]),
        input_name="x", input_dim=INPUT_DIM, output_name="out", mean=mean, std=std, softmax=False,
        metadata={"window": WINDOW, "scale": SCALE, "input": "16x(dx,dy)*scale oldest->newest, fps/30",
                  "outputs": "est_dx,est_dy,pred_dx,pred_dy (x scale)", "producer": "mausely_train.train_smoother"},
    )
    args.out.parent.mkdir(parents=True, exist_ok=True)
    onnx.save(onnx_model, args.out)
    sess = ort.InferenceSession(str(args.out))

    report = {"params": int(sum(p.numel() for p in model.parameters()))}
    for fps in (30.0, 15.0):
        vobs, vref, vstill, vrates = make_sequences(160, 300, args.seed + int(fps), fps=fps)
        tune, test = slice(0, 60), slice(60, None)
        ai_tune = metrics([run_model(sess, o, fps) for o in vobs[tune]], vref[tune], vstill[tune], vrates[tune])
        # Fair baseline: grid-search One Euro on held-out tuning sequences. Keep
        # (a) the lowest-error setting and (b) the lowest-error setting that is
        # at least as steady as the AI (same jitter), to compare lag and error.
        grid = []
        for mc, beta in itertools.product((0.3, 0.6, 1.0, 1.5, 2.5, 4.0, 6.0, 9.0),
                                          (2.0, 5.0, 10.0, 20.0, 40.0, 80.0, 160.0, 320.0)):
            outs = [filter_sequence(o, fps, mc, beta) for o in vobs[tune]]
            grid.append((metrics(outs, vref[tune], vstill[tune], vrates[tune]), mc, beta))
        best = min(grid, key=lambda g: g[0]["rmse_px"])
        steady = [g for g in grid if g[0]["jitter_still_px"] <= ai_tune["jitter_still_px"]]
        matched = min(steady, key=lambda g: g[0]["rmse_px"]) if steady else None

        def euro(mc, beta):
            return metrics([filter_sequence(o, fps, mc, beta) for o in vobs[test]], vref[test], vstill[test],
                           vrates[test])

        res = {
            "raw": metrics(list(vobs[test]), vref[test], vstill[test], vrates[test]),
            "one_euro": euro(best[1], best[2]),
            "one_euro_params": {"min_cutoff": best[1], "beta": best[2]},
            "ai": metrics([run_model(sess, o, fps) for o in vobs[test]], vref[test], vstill[test], vrates[test]),
            "ai_predict_0.5": metrics([run_model(sess, o, fps, 0.5) for o in vobs[test]],
                                      vref[test], vstill[test], vrates[test]),
        }
        if matched:
            res["one_euro_same_jitter"] = euro(matched[1], matched[2])
            res["one_euro_same_jitter_params"] = {"min_cutoff": matched[1], "beta": matched[2]}
        mc, beta = best[1], best[2]
        report[f"fps_{int(fps)}"] = res
        print(f"--- {int(fps)} fps (pixels of a 720p frame) ---")
        for k in ("raw", "one_euro", "one_euro_same_jitter", "ai", "ai_predict_0.5"):
            if k in res:
                print(f"  {k:21s} " + "  ".join(f"{m} {v:7.3f}" for m, v in res[k].items()))
        print(f"  one euro params: best {res['one_euro_params']}  same-jitter {res.get('one_euro_same_jitter_params')}")
    (args.out.with_name("smoother_report.json")).write_text(json.dumps(report, indent=2))

    # Reference vectors for the C++ tests (One Euro + AI on a short sequence).
    vobs, _, _, _ = make_sequences(1, 60, 99, fps=30.0)
    o = vobs[0].astype(np.float64).round(6)
    p = report["fps_30"]["one_euro_params"]
    vectors = {
        "fps": 30.0,
        "observed": o.tolist(),
        "one_euro": {"min_cutoff": p["min_cutoff"], "beta": p["beta"],
                     "output": filter_sequence(o, 30.0, p["min_cutoff"], p["beta"]).tolist()},
        "ai_est": run_model(sess, o.astype(np.float32), 30.0).astype(float).tolist(),
    }
    (ROOT / "tests" / "data" / "smoother_vectors.json").write_text(json.dumps(vectors))
    print(f"saved {args.out}")


if __name__ == "__main__":
    main()
