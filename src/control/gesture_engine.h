#pragma once

#include <cstdint>
#include <deque>

#include "ai/gesture_classifier.h"
#include "control/mouse_action.h"
#include "core/geometry.h"

namespace mausely {

struct GestureConfig {
    float emaAlpha = 0.6f;          // smoothing of class probabilities
    float enterProb = 0.6f;         // a pose must reach this ...
    float exitProb = 0.35f;         // ... while the current pose falls below this
    int enterFrames = 1;            // ... for this many consecutive frames (the EMA already needs ~2)
    int64_t clickRewindUs = 100'000;  // click lands where the cursor was this long ago
    float dragThresholdPx = 18.f;   // hand travel that turns a held pinch into a drag
    int64_t doubleClickUs = 500'000;  // doubleClickTimeUs()
    float doubleClickSnapPx = 20.f;
    float scrollGain = 1.5f;        // wheel units per screen pixel of hand travel
    float scrollDeadzonePx = 1.5f;  // per-frame hand motion ignored while scrolling
    int64_t fistHoldUs = 800'000;
    int64_t fistRearmUs = 300'000;
    float offsetDecay = 0.85f;      // per-frame decay of the post-freeze cursor offset
};

struct GestureCounters {
    int leftClicks = 0;
    int doubleClicks = 0;
    int rightClicks = 0;
    int drags = 0;
    int toggles = 0;
    int64_t scrollUnits = 0;
};

struct EngineInput {
    int64_t timeUs = 0;
    bool handPresent = false;
    PoseProbs probs{};
    Vec2 cursor;  // where the hand points on screen (smoothed, virtual-desktop px)
};

struct EngineOutput {
    MouseActions actions;
    bool toggled = false;       // control was switched on/off by the fist gesture
    float fistProgress = 0.f;   // 0..1 while a toggle fist is being held
};

// Turns per-frame pose probabilities + cursor position into mouse events.
// Pure logic (no Win32 calls) so it can be unit tested with scripted input.
class GestureEngine {
public:
    static constexpr Pose kNoHand = Pose::Count;

    void setConfig(const GestureConfig& c) { cfg_ = c; }
    const GestureConfig& config() const { return cfg_; }

    // Turning control off releases any held button (actions appended to `out`).
    void setEnabled(bool on, MouseActions& out);
    bool enabled() const { return enabled_; }

    EngineOutput update(const EngineInput& in);

    Pose pose() const { return pose_; }
    const PoseProbs& smoothedProbs() const { return ema_; }
    const GestureCounters& counters() const { return counters_; }
    bool frozen() const { return frozen_; }
    Vec2 cursor() const { return cursorOut_; }

private:
    void releaseButtons(MouseActions& out);
    void exitPose(Pose from, const EngineInput& in, MouseActions& out);
    void enterPose(Pose to, const EngineInput& in, MouseActions& out, EngineOutput& res);
    Vec2 rewoundCursor(int64_t nowUs) const;
    void emitMove(Vec2 p, MouseActions& out);

    GestureConfig cfg_;
    bool enabled_ = false;
    Pose pose_ = kNoHand;
    PoseProbs ema_{};
    bool emaInit_ = false;
    Pose candidate_ = kNoHand;
    int candidateFrames_ = 0;
    bool needMoveFirst_ = false;

    bool leftDown_ = false, rightDown_ = false;
    bool frozen_ = false;
    Vec2 freezePos_, freezeAnchor_;
    Vec2 offset_;
    Vec2 cursorOut_;
    std::deque<std::pair<int64_t, Vec2>> history_;

    int64_t lastClickUs_ = -1'000'000'000;
    Vec2 lastClickPos_;

    Vec2 scrollLast_;
    float scrollAccX_ = 0.f, scrollAccY_ = 0.f;

    int64_t fistStartUs_ = 0;
    int64_t fistEndUs_ = 0;
    bool fistArmed_ = true;

    GestureCounters counters_;
};

}  // namespace mausely
