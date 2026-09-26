#pragma once

#include <string>

#include "vision/hand_landmarker.h"
#include "vision/palm_detector.h"

namespace mausely {

struct TrackerTimes {
    double palmMs = 0.0;      // 0 when the palm detector did not run
    double landmarkMs = 0.0;
    bool ranPalm = false;
};

struct HandResult {
    bool valid = false;
    bool tracked = false;     // ROI came from the previous frame's landmarks
    HandLandmarks lm;
    RotatedRect roi;          // ROI used for this frame
};

// Runs the palm detector only when no hand is tracked; otherwise follows the
// hand with the landmark model alone (MediaPipe's detect/track scheme).
class HandTracker {
public:
    bool load(const std::wstring& palmModel, const std::wstring& landmarkModel, int threads, std::string* error);
    HandResult process(const Frame& frame, TrackerTimes& times);
    void reset() { tracking_ = false; }

    float presenceThreshold = 0.5f;
    float palmScoreThreshold = 0.5f;

private:
    bool detectAndLandmark(const Frame& frame, HandResult& r, TrackerTimes& times);

    PalmDetector palm_;
    HandLandmarker landmarker_;
    bool tracking_ = false;
    RotatedRect roi_;
};

}  // namespace mausely
