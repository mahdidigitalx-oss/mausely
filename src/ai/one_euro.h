#pragma once

#include <cmath>

#include "core/geometry.h"

namespace mausely {

// 2D One Euro filter with a cutoff shared by both axes (driven by 2D speed).
// Identical to training/mausely_train/one_euro.py.
class OneEuroFilter {
public:
    OneEuroFilter(double minCutoff = 1.0, double beta = 20.0, double dCutoff = 1.0)
        : minCutoff_(minCutoff), beta_(beta), dCutoff_(dCutoff) {}

    void setParams(double minCutoff, double beta) {
        minCutoff_ = minCutoff;
        beta_ = beta;
    }
    void reset() { init_ = false; }

    // p in any unit; dt in seconds.
    Vec2 filter(Vec2 p, double dt) {
        if (!init_ || dt <= 0.0) {
            init_ = true;
            x_ = {p.x, p.y};
            dx_ = {0.0, 0.0};
            return p;
        }
        double ad = alpha(dCutoff_, dt);
        dx_.x = ad * (p.x - x_.x) / dt + (1.0 - ad) * dx_.x;
        dx_.y = ad * (p.y - x_.y) / dt + (1.0 - ad) * dx_.y;
        double cutoff = minCutoff_ + beta_ * std::sqrt(dx_.x * dx_.x + dx_.y * dx_.y);
        double a = alpha(cutoff, dt);
        x_.x = a * p.x + (1.0 - a) * x_.x;
        x_.y = a * p.y + (1.0 - a) * x_.y;
        return {static_cast<float>(x_.x), static_cast<float>(x_.y)};
    }

private:
    struct D2 {
        double x, y;
    };
    static double alpha(double cutoff, double dt) {
        double tau = 1.0 / (2.0 * 3.14159265358979323846 * cutoff);
        return 1.0 / (1.0 + tau / dt);
    }

    double minCutoff_, beta_, dCutoff_;
    bool init_ = false;
    D2 x_{0, 0}, dx_{0, 0};
};

}  // namespace mausely
