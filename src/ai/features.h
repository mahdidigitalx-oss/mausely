#pragma once

#include <array>

#include "core/geometry.h"
#include "vision/hand_landmarker.h"

namespace mausely {

inline constexpr int kFeatureVersion = 1;
inline constexpr int kNumFeatures = 81;

using FeatureVector = std::array<float, kNumFeatures>;

// Invariant hand features, version 1. Must stay identical to
// training/mausely_train/features.py (checked by tests/test_features.cpp).
FeatureVector computeFeatures(const std::array<Vec3, kNumLandmarks>& world);

}  // namespace mausely
