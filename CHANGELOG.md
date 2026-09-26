# Changelog

All notable changes to Mausely are documented here. The project follows [Semantic Versioning](https://semver.org/).

## [0.1.0] – 2026-09-26

First public release.

### Added
- Webcam hand tracking with the MediaPipe palm detector and 21-point landmark model through ONNX
  Runtime. Uses the palm detector only when the hand is lost, and searches less often when idle.
- Gesture AI: a 7.5k-parameter MLP on 81 hand features that don't depend on position, rotation,
  hand size or left vs right hand. Classes: move, pinch-index, pinch-middle, scroll, fist.
- AI motion smoother (last 16 positions → denoised position and one-frame prediction), with a
  One Euro filter and raw mode available for comparison.
- Mouse actions: move, left click, double click with snapping, drag and drop, right click,
  vertical and horizontal scroll, pause/resume with a held fist or `Ctrl+Alt+M`.
- Click rewind, drag threshold, a guard against clicks from a hand entering the frame already
  pinched, and automatic button release.
- Dashboard (Direct3D 11 + Dear ImGui + ImPlot):
  - camera view with skeleton overlay;
  - per-stage timings, latency, CPU and jitter statistics;
  - gesture probabilities and counters;
  - settings and a recorder for your own training data.
- Python training pipeline with procedural data (kinematic hand model with inverse kinematics,
  human-motion simulator) and direct ONNX export.
- `--benchmark`, `--image`, `--camera`, `--screenshot` and `--tab` command-line options.
- 34 C++ tests (unit, integration on real photos, C++/Python parity) and 14 Python tests.
