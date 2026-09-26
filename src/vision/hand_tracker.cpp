#include "vision/hand_tracker.h"

#include "core/clock.h"

namespace mausely {

bool HandTracker::load(const std::wstring& palmModel, const std::wstring& landmarkModel, int threads,
                       std::string* error) {
    tracking_ = false;
    return palm_.load(palmModel, threads, error) && landmarker_.load(landmarkModel, threads, error);
}

bool HandTracker::detectAndLandmark(const Frame& frame, HandResult& r, TrackerTimes& times) {
    Stopwatch sw;
    auto palms = palm_.detect(frame, palmScoreThreshold);
    times.palmMs += sw.lapMs();
    times.ranPalm = true;
    if (palms.empty()) return false;

    r.roi = palmToRoi(palms.front());
    bool ok = landmarker_.run(frame, r.roi, r.lm);
    times.landmarkMs += sw.lapMs();
    return ok && r.lm.presence >= presenceThreshold;
}

HandResult HandTracker::process(const Frame& frame, TrackerTimes& times) {
    times = {};
    HandResult r;
    if (tracking_) {
        Stopwatch sw;
        r.roi = roi_;
        bool ok = landmarker_.run(frame, roi_, r.lm);
        times.landmarkMs = sw.lapMs();
        r.valid = ok && r.lm.presence >= presenceThreshold;
        r.tracked = r.valid;
    }
    // Lost the hand (or never had it): search the whole frame right away so a
    // brief tracking miss costs one frame, not two.
    if (!r.valid) r.valid = detectAndLandmark(frame, r, times);

    tracking_ = r.valid;
    if (r.valid) roi_ = landmarksToRoi(r.lm.image);
    return r;
}

}  // namespace mausely
