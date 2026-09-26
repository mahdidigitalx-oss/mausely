#include "ai/smoother.h"

#include <algorithm>

namespace mausely {

const char* smoothingModeName(SmoothingMode m) {
    switch (m) {
        case SmoothingMode::None: return "None (raw)";
        case SmoothingMode::OneEuro: return "One Euro filter";
        case SmoothingMode::Ai: return "AI smoother";
        case SmoothingMode::AiPredict: return "AI smoother + prediction";
        default: return "?";
    }
}

bool Smoother::loadAi(const std::wstring& modelPath, std::string* error) { return ai_.load(modelPath, error); }

void Smoother::setMode(SmoothingMode m) {
    if ((m == SmoothingMode::Ai || m == SmoothingMode::AiPredict) && !ai_.loaded()) m = SmoothingMode::OneEuro;
    if (m != mode_) reset();
    mode_ = m;
}

void Smoother::reset() {
    oneEuro_.reset();
    ai_.reset();
    lastUs_ = 0;
}

Vec2 Smoother::filter(Vec2 p, int64_t timestampUs) {
    double dt = lastUs_ ? static_cast<double>(timestampUs - lastUs_) * 1e-6 : 1.0 / 30.0;
    lastUs_ = timestampUs;
    if (dt > 0.0 && dt < 0.5) fps_ = 0.9 * fps_ + 0.1 * std::clamp(1.0 / dt, 5.0, 120.0);

    switch (mode_) {
        case SmoothingMode::OneEuro: return oneEuro_.filter(p, dt);
        case SmoothingMode::Ai: return ai_.filter(p, fps_, 0.f);
        case SmoothingMode::AiPredict: return ai_.filter(p, fps_, predictStrength_);
        default: return p;
    }
}

}  // namespace mausely
