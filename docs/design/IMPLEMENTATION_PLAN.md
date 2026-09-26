# Mausely Implementation Plan

> **For agentic workers:** executed inline (owner asked for uninterrupted execution). Steps use checkbox (`- [ ]`) syntax. Code lives in the repository; each task lists files, interfaces and the verification that gates it.

**Goal:** Native Windows hand-gesture mouse with a live statistics dashboard and two tiny custom ONNX models (gesture classifier, motion smoother).

**Architecture:** Three threads (capture → processing → UI). Processing runs MediaPipe-derived palm/landmark ONNX models, computes invariant features, classifies the pose, smooths the control point, runs a gesture state machine and injects input with `SendInput`. Dashboard is ImGui/ImPlot on D3D11. Models are trained in Python (PyTorch) on procedurally generated data and exported to ONNX.

**Tech Stack:** C++20, CMake+Ninja, w64devkit GCC (MSVC also supported), ONNX Runtime 1.30 C API (dynamic load), Media Foundation, Direct3D 11, Dear ImGui 1.92, ImPlot 1.0, stb_image, doctest; Python 3.12, PyTorch, onnx, onnxruntime, numpy, pytest.

## Global Constraints

- Min target: i7-3740QM (AVX, no AVX2), Windows 10 19045, 30 fps webcam. Pipeline must stay under 33 ms/frame there.
- No admin rights required to build or run.
- All third-party pieces MIT/Apache/BSD/public-domain; project licence MIT.
- Feature spec version `1`; C++ and Python feature code must produce identical vectors (tolerance 1e-4).
- Gesture classes in this exact order: `MOVE, PINCH_INDEX, PINCH_MIDDLE, SCROLL, FIST`.
- Smoother window 16 samples, scale ×20, outputs `[est_dx, est_dy, pred_dx, pred_dy]`.
- App starts paused; hand loss / pause / exit releases all buttons.

---

### Task 1: Build skeleton and dependencies
**Files:** `CMakeLists.txt`, `CMakePresets.json`, `cmake/Dependencies.cmake`, `cmake/Models.cmake`, `.gitignore`, `LICENSE`, `tests/test_main.cpp`
- [ ] FetchContent (pinned): imgui v1.92.x, implot v1.0, stb, doctest v2.5.3.
- [ ] Download ONNX Runtime wheel 1.30.0 (hash pinned) → extract `onnxruntime.dll`; download C API headers for v1.30.0.
- [ ] Download OpenCV-Zoo palm/handpose ONNX models (hash pinned) into `models/`.
- [ ] Verify: `cmake --preset default && cmake --build build && ctest --test-dir build` runs an empty doctest suite.

### Task 2: Core utilities
**Files:** `src/core/clock.h`, `src/core/rolling_stats.h`, `src/core/frame.h`; test `tests/test_core.cpp`
**Produces:** `mausely::Clock::nowUs()`, `RollingStats{push(double), mean(), percentile(p), max(), count()}`, `Frame{int w,h,stride; std::vector<uint8_t> bgra; int64_t captureUs, arrivalUs; uint64_t index;}`
- [ ] Tests: percentile of 1..100, window eviction. Implement. Pass.

### Task 3: Image ops
**Files:** `src/vision/image_ops.{h,cpp}`; test `tests/test_image_ops.cpp`
**Produces:** `struct RotatedRect{float cx,cy,size,angle;}`, `Letterbox letterboxToTensor(const Frame&, int dst, float* out)`, `void cropToTensor(const Frame&, const RotatedRect&, int dst, float* out)`, `Vec2 cropToImage(const RotatedRect&, int dst, Vec2 p)`.
- [ ] Tests: letterbox of 1280x720 → pad and scale; crop round-trip of a known pixel; rotation 90° maps "up" to image +x. Implement. Pass.

### Task 4: ONNX Runtime wrapper
**Files:** `src/vision/ort_model.{h,cpp}`; test `tests/test_ort.cpp`
**Produces:** `OrtRuntime::instance().load(path)`, `OrtModel{bool load(path, threads); run(inputs) ; inputShape(i); metadata(key)}`.
- [ ] Test: load handpose model, input shape `[1,224,224,3]`, run zeros, 4 outputs. Pass.

### Task 5: Palm detector
**Files:** `src/vision/palm_detector.{h,cpp}`; test `tests/test_palm.cpp`
**Produces:** `struct PalmDetection{float score; Rect box; Vec2 kp[7];}`, `generateAnchors()` (2016), `weightedNms(...)`, `PalmDetector::detect(const Frame&) -> std::vector<PalmDetection>`, `palmToRoi(const PalmDetection&) -> RotatedRect`.
- [ ] Tests: anchor count/values, NMS merges overlaps, detects a palm on test photos. Pass.

### Task 6: Landmarks + tracker
**Files:** `src/vision/hand_landmarker.{h,cpp}`, `src/vision/hand_tracker.{h,cpp}`; test `tests/test_tracker.cpp`
**Produces:** `struct HandResult{bool valid; float presence, handedness; Vec3 image[21], world[21]; RotatedRect roi;}`, `HandTracker::process(const Frame&) -> HandResult` with `StageTimes`.
- [ ] Tests: landmarks inside image for photos, second call uses tracking (no palm det). Pass.

### Task 7: Gesture training pipeline (Python)
**Files:** `training/pyproject.toml`, `training/mausely_train/{handmodel,synth_gestures,features,train_gestures,onnx_export}.py`, `training/tests/test_*.py`
- [ ] Kinematic hand, per-class sampler, batched thumb IK, 81-dim features, MLP training, ONNX export with baked normalisation+softmax and metadata, parity vectors `tests/data/feature_vectors.json`.
- [ ] Verify: pytest green; held-out synthetic accuracy ≥ 97%; ONNX vs torch max diff < 1e-5.

### Task 8: C++ features + classifier
**Files:** `src/ai/features.{h,cpp}`, `src/ai/gesture_classifier.{h,cpp}`; tests `tests/test_features.cpp`
- [ ] Parity with `feature_vectors.json` (1e-4). Classifier on photos gives expected class for fist / victory(scroll) / open palm. Pass.

### Task 9: Smoother training (Python)
**Files:** `training/mausely_train/{synth_motion,one_euro,train_smoother}.py`, tests.
- [ ] Verify: AI beats One Euro on held-out RMSE and still-jitter; report JSON written.

### Task 10: C++ smoothing
**Files:** `src/ai/one_euro.h`, `src/ai/ai_smoother.{h,cpp}`, `src/ai/smoother.{h,cpp}`; test `tests/test_smoother.cpp`
- [ ] One Euro matches Python reference values; AI smoother reduces jitter on a noisy still sequence. Pass.

### Task 11: Control layer
**Files:** `src/control/{cursor_mapper,gesture_engine,mouse_injector}.{h,cpp}`; test `tests/test_control.cpp`
- [ ] Engine tests: pinch→down/up, rewind position, freeze then drag, double-click snapping, right click, scroll units, fist toggle, release on loss. Mapper tests: mirror, region clamp. Pass.

### Task 12: Capture
**Files:** `src/capture/{camera_source.h,mf_camera.cpp,mf_camera.h,image_source.{h,cpp}}`
- [ ] Verify: benchmark mode reads frames from the real webcam (frame size/fps only, no images stored).

### Task 13: Pipeline, settings, recorder
**Files:** `src/pipeline/{pipeline,settings}.{h,cpp}`, `src/recorder/recorder.{h,cpp}`; test `tests/test_settings.cpp`
- [ ] Settings round-trip test; pipeline runs headless with `--image` and `--benchmark`.

### Task 14: Dashboard + app
**Files:** `src/ui/{d3d_window,dashboard}.{h,cpp}`, `src/app/main.cpp`
- [ ] Verify: app launches, window renders, screenshot check, hotkey toggles.

### Task 15: Docs and packaging
**Files:** `README.md`, `docs/ARCHITECTURE.md`, `scripts/package.ps1`, `THIRD_PARTY_NOTICES.md`
- [ ] Portable zip with exe, DLLs, models.
