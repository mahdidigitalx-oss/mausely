#pragma once

#include <string>

#include "ai/ai_smoother.h"
#include "ai/one_euro.h"

namespace mausely {

enum class SmoothingMode : int { None = 0, OneEuro, Ai, AiPredict, Count };
const char* smoothingModeName(SmoothingMode m);

// Chooses between raw, One Euro and the learned smoother at run time.
// Positions are in frame-height units, timestamps in microseconds.
class Smoother {
public:
    bool loadAi(const std::wstring& modelPath, std::string* error);
    bool aiAvailable() const { return ai_.loaded(); }

    void setMode(SmoothingMode m);
    SmoothingMode mode() const { return mode_; }
    void setOneEuroParams(double minCutoff, double beta) { oneEuro_.setParams(minCutoff, beta); }
    void setPredictStrength(float s) { predictStrength_ = s; }
    void reset();

    Vec2 filter(Vec2 p, int64_t timestampUs);
    double fps() const { return fps_; }

private:
    SmoothingMode mode_ = SmoothingMode::OneEuro;
    OneEuroFilter oneEuro_;
    AiSmoother ai_;
    float predictStrength_ = 0.5f;
    int64_t lastUs_ = 0;
    double fps_ = 30.0;
};

}  // namespace mausely
