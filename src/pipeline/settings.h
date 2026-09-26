#pragma once

#include <string>

#include "ai/smoother.h"
#include "control/cursor_mapper.h"
#include "control/gesture_engine.h"

namespace mausely {

struct Settings {
    // Camera
    int cameraIndex = 0;
    int captureWidth = 1280;
    int captureHeight = 720;
    int captureFps = 30;
    bool mirror = true;

    // Pointer
    ActiveRegion region;
    SmoothingMode smoothing = SmoothingMode::Ai;
    float predictStrength = 0.5f;
    double oneEuroMinCutoff = 1.0;  // see models/smoother_report.json for the trade-offs
    double oneEuroBeta = 80.0;

    // Gestures
    GestureConfig gestures;

    // Tracking
    float handConfidence = 0.6f;  // minimum landmark-model presence to accept a hand

    // Performance
    int inferenceThreads = 2;
    bool idleThrottle = true;  // search for hands less often after 2 s without one
};

// Simple key=value file. Missing keys keep their defaults; returns false if the file can't be read.
bool loadSettings(const std::wstring& path, Settings& s);
bool saveSettings(const std::wstring& path, const Settings& s);

}  // namespace mausely
