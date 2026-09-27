# Mausely — Hand-Gesture Mouse for Windows and Android (Design)

Date: 2026-09-23 · Status: implemented in v0.1.0

## 1. Goal

A native Windows desktop app that turns webcam hand movement into mouse input,
with a small dashboard showing the camera, the tracked hand, and precise
per-stage statistics. A small custom neural network improves gesture
recognition and cursor smoothness. Open source (MIT), primary target is a
2012-era laptop (i7-3740QM, no AVX2, Kepler GPU, 720p/30fps webcam), so
anything that runs well there runs well everywhere.

## 2. Key decisions

| Topic | Decision | Why |
|---|---|---|
| App language | C++20, single process | latency, no runtime deps for users |
| Inference | ONNX Runtime C API, loaded dynamically (`LoadLibrary` + `OrtGetApiBase`), CPU EP | official prebuilt DLL, compiler-agnostic (MSVC or MinGW), no import lib |
| Hand landmarks | MediaPipe palm detector + hand landmark models in ONNX form from OpenCV Zoo (Apache-2.0) | training a landmark model from scratch is not reasonable |
| Custom AI | two tiny models trained in Python (PyTorch) and exported to ONNX: **gesture classifier** (MLP on invariant hand features) and **motion smoother** (MLP on a trajectory window) | small (<20k params), microsecond inference, trainable on CPU |
| Training data | procedurally generated (kinematic hand model, synthetic human-like trajectories) + optional user recordings from the dashboard | no dataset licensing issues, reproducible; user data fine-tunes |
| Baseline smoother | One Euro filter, always available, A/B switchable | the AI must beat a proven baseline with measured numbers |
| Camera | Media Foundation `IMFSourceReader`, RGB32, low-latency mode | native, no OpenCV |
| UI | Win32 + Direct3D 11 + Dear ImGui + ImPlot | light, native, real-time plots |
| Mouse | `SendInput`, absolute virtual-desktop coordinates, per-monitor DPI aware | standard, works across monitors |
| Build | CMake + Ninja; deps fetched with pinned hashes | reproducible for contributors |

## 3. Architecture

```
 Capture thread            Processing thread                                   UI thread
 ┌──────────────┐  latest  ┌───────────────────────────────────────────────┐  ┌─────────────┐
 │ MF camera or │─frame──▶ │ HandTracker (palm det ⇄ landmark tracking)    │  │ D3D11 +     │
 │ image source │  slot    │ → Features → GestureClassifier (ONNX)         │─▶│ ImGui/ImPlot│
 └──────────────┘          │ → Smoother (None / OneEuro / AI / AI+predict) │  │ dashboard   │
                           │ → GestureEngine (state machine) → CursorMapper│  └─────────────┘
                           │ → MouseInjector (SendInput)  → Snapshot+Stats │
                           └───────────────────────────────────────────────┘
```

Modules (each one header + one source, one purpose):

- `core/` — `Frame` (BGRA8 image + timestamps), QPC clock, rolling statistics (mean, p50, p95, max).
- `capture/` — `ICameraSource`; `MfCamera` (Media Foundation); `ImageSource` (static image files for tests/benchmarks).
- `vision/` — `OrtEnv`/`OrtModel` (dynamic ONNX Runtime), `image_ops` (letterbox, rotated-crop bilinear sampling), `PalmDetector` (anchors, decode, weighted NMS), `HandLandmarker` (crop, infer, un-project), `HandTracker` (detect ↔ track state machine, ROI from landmarks).
- `ai/` — `features` (81-dim invariant feature vector, identical spec in Python), `GestureClassifier` (ONNX), `OneEuroFilter`, `AiSmoother` (ONNX), `Smoother` facade.
- `control/` — `GestureEngine` (pose hysteresis → mouse events, click freeze/rewind, double-click snapping, scroll, fist toggle), `CursorMapper` (active region → virtual desktop), `MouseInjector` (SendInput, safe release).
- `pipeline/` — owns threads, settings, publishes a `Snapshot` for the UI.
- `ui/` — Win32/D3D11 window, dashboard panels.
- `recorder/` — writes labelled landmark samples to CSV for fine-tuning.

## 4. Vision pipeline details

- Palm detector: input `1x192x192x3` RGB float [0,1], letterboxed; outputs 2016 anchors (24×24×2 + 12×12×6), 18 regressors (box + 7 keypoints), logit score. Sigmoid, threshold 0.5, weighted NMS IoU 0.3.
- Palm → hand ROI (MediaPipe convention): rotation `θ = π/2 − atan2(−(kp2.y−kp0.y), kp2.x−kp0.x)`, shift −0.5·h along hand "up", square of side `2.6·max(w,h)`.
- Landmark model: input `1x224x224x3` RGB [0,1] from rotated crop; outputs 21×3 image landmarks (crop pixels), presence score, handedness, 21×3 world landmarks (metres).
- Tracking: next ROI from landmarks (subset 0,1,2,3,5,6,9,10,13,14,17,18; rotation from wrist to mean of MCPs; scale 2.0; shift −0.1). If presence < 0.5 → back to palm detection.
- Cursor control point: mean of landmarks 0,5,9,13,17 (palm centroid) — does not jump when the fingers pinch.

## 5. Custom AI

### 5.1 Gesture classifier
- Classes: `MOVE` (open/relaxed/pointing), `PINCH_INDEX`, `PINCH_MIDDLE`, `SCROLL` (index+middle extended), `FIST`.
- Features (81, from world landmarks; translation/rotation/scale/chirality invariant): 15 joint flexion angles, 10 fingertip pair distances, 5 tip–wrist and 5 tip–palm-centre distances, 4 inter-finger spread angles, 42 (x,y) coordinates in the anatomical hand frame. Distances are divided by palm size.
- Model: MLP 81→64→32→5 with feature normalisation and softmax baked into the ONNX graph. ONNX metadata carries `feature_version`, class names.
- Data: kinematic right hand (metric bone lengths, joint limits), class-conditional joint sampling, thumb placed by batched IK for pinches, bone-length jitter, landmark noise, mirrored left hands.

### 5.2 Motion smoother
- Input: last 16 raw control-point positions (frame-height-normalised units) relative to the newest, scaled ×20. Output: 4 values — denoised current offset and one-frame-ahead prediction offset.
- Runtime output = estimate + `predict_strength`·(prediction − estimate).
- Data: synthetic minimum-jerk reaches with Fitts-like timing, dwells, drifts, 8–12 Hz tremor, heteroscedastic Gaussian + outlier noise, occasional dropped frames.
- Loss: position MSE + velocity-consistency term (penalises output jitter).
- Evaluation vs One Euro on held-out data: RMSE, jitter while still, lag while moving — written to `models/smoother_report.json`.

## 6. Gesture engine (per frame)

- Probabilities → EMA (α 0.6). Switch pose when the new pose's smoothed prob ≥ 0.6 while the current one is ≤ 0.35 (a clean pinch needs ~2 frames, so single-frame glitches never click).
- `MOVE`: cursor follows smoothed control point.
- `PINCH_INDEX` start → left button down at the cursor position from 100 ms earlier (click rewind); the cursor stays frozen until the hand moves more than the drag threshold (then it is a drag). End → left button up. Double click comes naturally; a second pinch within the system double-click time and 20 px reuses the exact previous click point.
- `PINCH_MIDDLE` → right button down/up, same freeze logic.
- `SCROLL` → cursor frozen, vertical/horizontal hand motion → wheel/hwheel units (dead-zone, gain setting).
- `FIST` held 0.8 s → toggle control on/off (re-arms after 0.3 s of non-fist). Global hotkey Ctrl+Alt+M also toggles.
- Hand lost, pause, or exit → all pressed buttons are released. App starts paused.

## 7. Dashboard

Left: camera with overlay (skeleton, ROI, active region, control point, pose). Right, collapsible panels:
- Status: control on/off, tracking state, handedness, presence score.
- Performance: camera FPS, pipeline FPS, dropped frames, per-stage ms table (avg/p50/p95/max) and live plot, capture→cursor latency.
- Smoothing: mode selector, prediction strength, jitter (raw vs smoothed, px RMS while still), plot of raw vs smoothed.
- Gestures: probability bars, current pose, counters (clicks, double clicks, right clicks, drags, scroll units, toggles).
- Settings: active region, scroll gain, freeze/rewind times, mirror, camera device. Persisted to `%APPDATA%\Mausely\settings.ini`.
- Recorder: pick label, record N seconds → CSV in `%APPDATA%\Mausely\recordings`.

CLI: `--image <file>` (static source), `--benchmark <n>` (headless timing on a source, prints stats).

## 8. Error handling

Missing camera / models / ONNX Runtime DLL → dashboard shows a clear message and keeps running; camera loss → retry every 2 s; tracking loss → release buttons. Settings file parse errors fall back to defaults.

## 9. Testing

- C++ (doctest): anchors, NMS, ROI math, affine round-trip, One Euro, features parity against Python-generated vectors, gesture engine transitions (scripted probability sequences → expected events), cursor mapper, ONNX models load and produce sane output, integration on real hand photos (detect + classify).
- Python (pytest): hand model limits, generator class separability, feature spec, export parity torch↔onnxruntime.
- Benchmark mode for per-stage timing on the target laptop.

## 10. Android

The Android app (`android/`) reuses `src/` unchanged except for platform seams; only capture, input and UI are new.

| Topic | Decision | Why |
|---|---|---|
| Shared code | `src/` built with the NDK through `add_subdirectory` of the root CMake project (core + `Pipeline`) | one implementation of tracking, AI and gesture logic, covered by the same tests (also run on Linux in CI) |
| Inference | `libonnxruntime.so` from the `onnxruntime-android` AAR (same version as the desktop headers), loaded with `dlopen` | same C API and loading path as Windows |
| Camera | CameraX `ImageAnalysis`, RGBA, keep-only-latest, front camera 640×480; frames rotated upright in C++ (`rgbaToFrame`) and pushed with `Pipeline::submitFrame` | latest frame wins, as on the desktop |
| Background | a foreground service of type `camera`, started from the app | the camera stays available while other apps are in front |
| Input | an accessibility service: `TYPE_ACCESSIBILITY_OVERLAY` pointer + `dispatchGesture` with continued strokes (API 26) | the only way for an app to touch other apps; continued strokes let a press last exactly as long as the pinch |
| Mapping | left button = finger down/up (tap, long press, drag); wheel = finger drag from the cursor after the touch slop, lifted after 150 ms without motion (no taps, no flings); right button = Back or another global action | touch-screen semantics for the same `MouseActions` |
| UI | Jetpack Compose dashboard polling `Pipeline::snapshot` once per display frame; settings exchanged as the settings.ini text | no duplicated settings schema in Kotlin |

`control/mouse_injector.h` is the platform input backend: `MouseInjector`, `virtualDesktop()` and `doubleClickTimeUs()` are implemented by `mouse_injector.cpp` on Windows and `android_injector.cpp` on Android (which forwards each batch to Kotlin through JNI).

## 11. Out of scope (v1)

Two-hand gestures, keyboard emulation, GPU execution provider (DirectML can be added later), installer (a portable zip is produced instead).
