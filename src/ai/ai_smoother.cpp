#include "ai/ai_smoother.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace mausely {

bool AiSmoother::load(const std::wstring& modelPath, std::string* error) {
    if (!model_.load(modelPath, 1, error)) return false;
    const std::string window = model_.metadata("window"), scale = model_.metadata("scale");
    if (window != std::to_string(kWindow) || std::abs(std::atof(scale.c_str()) - kScale) > 1e-3) {
        if (error) *error = "smoother model metadata mismatch (window '" + window + "', scale '" + scale + "')";
        return false;
    }
    reset();
    return true;
}

Vec2 AiSmoother::filter(Vec2 p, double fps, float predictStrength) {
    // Shift the window; before it is full the first sample is repeated,
    // exactly like model_inputs() in training.
    if (count_ == 0) history_.fill(p);
    std::rotate(history_.begin(), history_.begin() + 1, history_.end());
    history_[kWindow - 1] = p;
    count_ = std::min(count_ + 1, kWindow);

    for (int i = 0; i < kWindow; ++i) {
        input_[i * 2] = (history_[i].x - p.x) * kScale;
        input_[i * 2 + 1] = (history_[i].y - p.y) * kScale;
    }
    input_[kWindow * 2] = static_cast<float>(std::clamp(fps, 10.0, 60.0) / 30.0);

    if (!model_.run({input_.data()}, {{1, kWindow * 2 + 1}}, out_, &lastError_)) return p;
    const float* o = out_[0].data.data();
    float s = std::clamp(predictStrength, 0.f, 1.f);
    float dx = o[0] + s * (o[2] - o[0]);
    float dy = o[1] + s * (o[3] - o[1]);
    return {p.x + dx / kScale, p.y + dy / kScale};
}

}  // namespace mausely
