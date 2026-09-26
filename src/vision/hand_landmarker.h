#pragma once

#include <array>
#include <string>
#include <vector>

#include "core/frame.h"
#include "core/geometry.h"
#include "vision/ort_model.h"

namespace mausely {

inline constexpr int kNumLandmarks = 21;

// MediaPipe hand landmark indices.
enum Landmark : int {
    kWrist = 0,
    kThumbCmc = 1, kThumbMcp = 2, kThumbIp = 3, kThumbTip = 4,
    kIndexMcp = 5, kIndexPip = 6, kIndexDip = 7, kIndexTip = 8,
    kMiddleMcp = 9, kMiddlePip = 10, kMiddleDip = 11, kMiddleTip = 12,
    kRingMcp = 13, kRingPip = 14, kRingDip = 15, kRingTip = 16,
    kPinkyMcp = 17, kPinkyPip = 18, kPinkyDip = 19, kPinkyTip = 20,
};

struct HandLandmarks {
    float presence = 0.f;    // probability that a hand is in the ROI
    float handedness = 0.f;  // > 0.5 means right hand (as seen by the model)
    std::array<Vec3, kNumLandmarks> image{};  // pixels; z in pixel-like units relative to the wrist
    std::array<Vec3, kNumLandmarks> world{};  // metres, hand-centred
};

// Next-frame ROI from landmarks (MediaPipe HandLandmarksToRect + scale 2.0, shift -0.1).
RotatedRect landmarksToRoi(const std::array<Vec3, kNumLandmarks>& image);

class HandLandmarker {
public:
    static constexpr int kInputSize = 224;

    bool load(const std::wstring& modelPath, int threads, std::string* error);
    bool run(const Frame& frame, const RotatedRect& roi, HandLandmarks& out);
    const std::string& lastError() const { return lastError_; }

private:
    OrtModel model_;
    std::vector<float> input_;
    std::vector<Tensor> outputs_;
    int idxLandmarks_ = 0, idxPresence_ = 1, idxHandedness_ = 2, idxWorld_ = 3;
    std::string lastError_;
};

}  // namespace mausely
