#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

#include "ai/gesture_classifier.h"
#include "vision/hand_landmarker.h"

namespace mausely {

// Writes labelled landmark samples to CSV for fine-tuning the gesture model
// (see training/mausely_train/recordings.py for the format).
class Recorder {
public:
    ~Recorder() { stop(); }

    // Starts a new file in `folder` after `delayUs` (time to get into pose), for `durationUs`.
    bool start(const std::wstring& folder, Pose label, int64_t nowUs, int64_t delayUs, int64_t durationUs,
               std::string* error);
    void stop();

    // Call every frame; writes the sample if a recording window is active.
    void onFrame(int64_t nowUs, const HandLandmarks* hand);

    bool active() const { return file_ != nullptr; }
    bool waiting(int64_t nowUs) const { return active() && nowUs < startUs_; }
    double secondsLeft(int64_t nowUs) const;
    int samples() const { return samples_; }
    Pose label() const { return label_; }
    const std::wstring& path() const { return path_; }

private:
    FILE* file_ = nullptr;
    Pose label_ = Pose::Move;
    int64_t startUs_ = 0, endUs_ = 0;
    int samples_ = 0;
    std::wstring path_;
};

}  // namespace mausely
