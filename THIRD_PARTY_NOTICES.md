# Third-party components

Mausely downloads (at configure time, SHA-256 pinned) and redistributes in its
release zip the following components.

| Component | Version | Licence | Used for |
|---|---|---|---|
| [ONNX Runtime](https://github.com/microsoft/onnxruntime) | 1.30.0 | MIT | running all neural networks (`onnxruntime.dll`) |
| MediaPipe palm detection model, via [OpenCV Zoo](https://github.com/opencv/opencv_zoo/tree/main/models/palm_detection_mediapipe) | 2023feb | Apache-2.0 | `models/palm_detection_mediapipe_2023feb.onnx` |
| MediaPipe hand landmark model, via [OpenCV Zoo](https://github.com/opencv/opencv_zoo/tree/main/models/handpose_estimation_mediapipe) | 2023feb | Apache-2.0 | `models/handpose_estimation_mediapipe_2023feb.onnx` |
| [Dear ImGui](https://github.com/ocornut/imgui) | 1.92.9 | MIT | dashboard UI |
| [ImPlot](https://github.com/epezent/implot) | 1.0 | MIT | dashboard plots |
| [stb_image](https://github.com/nothings/stb) | 2c980bb | MIT / public domain | loading test images |

Build/test only (not redistributed):

| Component | Licence | Used for |
|---|---|---|
| [doctest](https://github.com/doctest/doctest) 2.5.3 | MIT | C++ tests |
| MediaPipe test photos (`storage.googleapis.com/mediapipe-assets`) | Apache-2.0 (MediaPipe) | integration tests; the hands shown in `docs/screenshots` and `docs/banner.jpg` |
| [PyTorch](https://pytorch.org), NumPy, onnx, onnxruntime (Python) | BSD / MIT / Apache-2.0 | training the two Mausely models |

The two Mausely models (`models/gesture_mlp.onnx`, `models/smoother.onnx`) are
trained only on procedurally generated data and are covered by Mausely's MIT licence.

The MediaPipe models are © Google LLC, licensed under the Apache License 2.0
(http://www.apache.org/licenses/LICENSE-2.0); the ONNX conversions are by the
OpenCV Zoo project under the same licence.
