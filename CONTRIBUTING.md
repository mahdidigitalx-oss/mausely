# Contributing to Mausely

Thanks for helping make hands-free computer control better! Bug reports, gesture ideas,
performance numbers from different PCs and pull requests are all welcome.

## Reporting a problem

Please include:

- Windows version, CPU, and webcam model
- The output of `mausely.exe --benchmark 300` (run from a terminal next to `mausely.exe`)
- What you did, what you expected, and what happened
- `%APPDATA%\Mausely\mausely.log` if the app shows an error

## Building

```bash
cmake --preset default          # Release build in ./build (Ninja)
cmake --build build
ctest --test-dir build --output-on-failure
```

Any C++20 compiler for Windows works. CI builds with GCC ([w64devkit](https://github.com/skeeto/w64devkit))
and MSVC 2022. The first configure downloads pinned, hash-checked dependencies into `.deps/`.

The platform-independent core and the tests also build on Linux with the same commands
(`cmake --preset default`, needs Ninja and GCC or Clang). Please keep new core code portable:
Windows-only code belongs in `src/app`, `src/ui`, `src/capture/mf_camera.*` and
`src/control/mouse_injector.cpp`.

### Android

```bash
cd android
./gradlew testDebugUnitTest lintDebug assembleDebug
```

Needs JDK 17+ and the Android SDK (Gradle installs the pinned NDK and CMake). The app builds the
shared core from `src/` through `android/app/src/main/cpp/CMakeLists.txt`; its own native code is
the JNI bridge (`jni_bridge.cpp`) and the input backend (`android_injector.cpp`). User-visible
text lives in `res/values/strings.xml`, with the Arabic translation in `res/values-ar/`.

Useful switches while developing:

- `mausely.exe --image some_hand.jpg` runs the whole pipeline on a still photo.
- `mausely.exe --benchmark 300` prints timings and never moves the mouse.
- `mausely.exe --image some_hand.jpg --screenshot out.bmp --tab 1` renders the dashboard to a file.

## Code layout and style

- One header + one source per unit, each with a single job (`src/vision`, `src/ai`, `src/control`, ...).
- Logic that decides mouse actions lives in `GestureEngine` and is **pure** (no Win32 calls), so it
  can be tested with scripted input. Keep it that way.
- Match the surrounding style: 4-space indent, `camelCase` functions, `trailing_` members,
  short comments that explain *why*.
- Every behaviour change comes with a test in `tests/` (doctest). Run the full suite before a PR.

## Changing the AI models

The feature vector exists twice: `training/mausely_train/features.py` and `src/ai/features.cpp`.
They must stay identical. The parity test (`tests/test_features.cpp`) compares them using vectors
written by the training script.

```bash
cd training
python -m venv .venv && .venv\Scripts\activate
pip install -e .[dev]
python -m pytest
python -m mausely_train.train_gestures
python -m mausely_train.train_smoother
```

Training rewrites `models/*.onnx`, `models/*_report.json` and `tests/data/*_vectors.json`.
Rebuild and run `ctest` afterwards, and mention the new report numbers in your PR.

If you change the feature layout, bump `FEATURE_VERSION` (Python) and `kFeatureVersion` (C++).
The app refuses to load a model with a different version.

## Pull requests

- Keep PRs focused; describe the user-visible change and how you tested it.
- CI must be green (Windows GCC and MSVC, Linux, Android and Python jobs).
- By contributing you agree that your work is released under the [MIT License](LICENSE).
