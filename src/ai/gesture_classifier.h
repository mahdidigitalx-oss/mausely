#pragma once

#include <array>
#include <string>

#include "ai/features.h"
#include "vision/ort_model.h"

namespace mausely {

enum class Pose : int { Move = 0, PinchIndex, PinchMiddle, Scroll, Fist, Count };
inline constexpr int kNumPoses = static_cast<int>(Pose::Count);
using PoseProbs = std::array<float, kNumPoses>;

const char* poseName(Pose p);

// Tiny MLP (features -> pose probabilities) trained by training/train_gestures.py.
class GestureClassifier {
public:
    bool load(const std::wstring& modelPath, std::string* error);
    bool loaded() const { return model_.loaded(); }
    // Returns false (and leaves probs untouched) on inference error.
    bool classify(const FeatureVector& features, PoseProbs& probs);

private:
    OrtModel model_;
    std::vector<Tensor> out_;
    std::string lastError_;
};

}  // namespace mausely
