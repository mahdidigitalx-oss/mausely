# Mausely training

Python pipeline for the two small models shipped in `../models`.

| Module | Purpose |
|---|---|
| `handmodel.py` | batched kinematic hand in MediaPipe landmark layout, thumb inverse kinematics |
| `synth_gestures.py` | class-conditional hand poses + augmentation → gesture dataset |
| `features.py` | the 81-dim invariant feature vector (must match `src/ai/features.cpp`) |
| `train_gestures.py` | trains the gesture MLP, exports `gesture_mlp.onnx`, `gesture_report.json`, C++ parity vectors |
| `synth_motion.py` | human-like pointer trajectories with tremor / jitter / outliers |
| `one_euro.py` | One Euro baseline (must match `src/ai/one_euro.h`) |
| `train_smoother.py` | trains the smoother MLP, exports `smoother.onnx`, `smoother_report.json`, parity vectors |
| `onnx_mlp.py` | writes the ONNX graphs directly (normalisation + softmax baked in, metadata) |
| `recordings.py` | loads CSV recordings from the dashboard's Recorder |

```bash
python -m venv .venv && .venv\Scripts\activate
pip install -e .[dev]
pytest                                        # ~5 s
python -m mausely_train.train_gestures        # a few minutes on a laptop CPU
python -m mausely_train.train_smoother        # ~10 min
```

After retraining, rebuild the C++ project (models are copied next to the exe) and
run `ctest` – the parity tests check that C++ and Python still agree.
