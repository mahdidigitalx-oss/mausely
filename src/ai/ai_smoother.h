#pragma once

#include <array>
#include <string>

#include "core/geometry.h"
#include "vision/ort_model.h"

namespace mausely {

// Learned pointer smoother (training/train_smoother.py): last 16 raw positions
// -> denoised position + one-frame-ahead prediction.
class AiSmoother {
public:
    static constexpr int kWindow = 16;
    static constexpr float kScale = 20.f;

    bool load(const std::wstring& modelPath, std::string* error);
    bool loaded() const { return model_.loaded(); }
    void reset() { count_ = 0; }

    // p: raw position in frame-height units. fps: current camera rate.
    // predictStrength 0 = denoised current position, 1 = one frame ahead.
    // Returns p unchanged if inference fails.
    Vec2 filter(Vec2 p, double fps, float predictStrength);

private:
    OrtModel model_;
    std::array<Vec2, kWindow> history_{};
    int count_ = 0;  // samples seen since reset (saturates at kWindow)
    std::array<float, kWindow * 2 + 1> input_{};
    std::vector<Tensor> out_;
    std::string lastError_;
};

}  // namespace mausely
