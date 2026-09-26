#include "control/gesture_engine.h"

#include <algorithm>
#include <cmath>

namespace mausely {
namespace {

bool isPinch(Pose p) { return p == Pose::PinchIndex || p == Pose::PinchMiddle; }

int argmax(const PoseProbs& p) { return static_cast<int>(std::max_element(p.begin(), p.end()) - p.begin()); }

}  // namespace

void GestureEngine::setEnabled(bool on, MouseActions& out) {
    if (!on) releaseButtons(out);
    enabled_ = on;
}

void GestureEngine::releaseButtons(MouseActions& out) {
    if (leftDown_) out.push_back({MouseActionType::LeftUp});
    if (rightDown_) out.push_back({MouseActionType::RightUp});
    leftDown_ = rightDown_ = false;
}

void GestureEngine::emitMove(Vec2 p, MouseActions& out) {
    cursorOut_ = p;
    out.push_back({MouseActionType::Move, static_cast<int>(std::lround(p.x)), static_cast<int>(std::lround(p.y))});
}

Vec2 GestureEngine::rewoundCursor(int64_t nowUs) const {
    Vec2 best = cursorOut_;
    for (auto it = history_.rbegin(); it != history_.rend(); ++it) {
        best = it->second;
        if (it->first <= nowUs - cfg_.clickRewindUs) break;
    }
    return best;
}

void GestureEngine::exitPose(Pose from, const EngineInput& in, MouseActions& out) {
    if (from == Pose::PinchIndex && leftDown_) {
        out.push_back({MouseActionType::LeftUp});
        leftDown_ = false;
    }
    if (from == Pose::PinchMiddle && rightDown_) {
        out.push_back({MouseActionType::RightUp});
        rightDown_ = false;
    }
    if (from == Pose::Fist) fistEndUs_ = in.timeUs;
    if (frozen_ || isPinch(from) || from == Pose::Scroll || from == Pose::Fist) {
        // Continue from where the cursor is instead of jumping to the hand.
        offset_ = cursorOut_ - in.cursor;
    }
    frozen_ = false;
}

void GestureEngine::enterPose(Pose to, const EngineInput& in, MouseActions& out, EngineOutput&) {
    if (isPinch(to)) {
        Vec2 at = rewoundCursor(in.timeUs);
        if (to == Pose::PinchIndex) {
            bool quick = in.timeUs - lastClickUs_ <= cfg_.doubleClickUs;
            if (quick && length(at - lastClickPos_) <= cfg_.doubleClickSnapPx) {
                at = lastClickPos_;  // land exactly on the first click
                ++counters_.doubleClicks;
            }
            lastClickUs_ = in.timeUs;
            lastClickPos_ = at;
        }
        if (enabled_) {
            emitMove(at, out);
            out.push_back({to == Pose::PinchIndex ? MouseActionType::LeftDown : MouseActionType::RightDown});
            (to == Pose::PinchIndex ? leftDown_ : rightDown_) = true;
            ++(to == Pose::PinchIndex ? counters_.leftClicks : counters_.rightClicks);
        }
        cursorOut_ = at;
        frozen_ = true;
        freezePos_ = at;
        freezeAnchor_ = in.cursor;
    } else if (to == Pose::Scroll) {
        frozen_ = true;
        freezePos_ = cursorOut_;
        scrollLast_ = in.cursor;
        scrollAccX_ = scrollAccY_ = 0.f;
    } else if (to == Pose::Fist) {
        fistStartUs_ = in.timeUs;
    }
}

EngineOutput GestureEngine::update(const EngineInput& in) {
    EngineOutput res;
    MouseActions& out = res.actions;

    if (!in.handPresent) {
        releaseButtons(out);
        if (pose_ == Pose::Fist) fistEndUs_ = in.timeUs;
        pose_ = kNoHand;
        emaInit_ = false;
        candidateFrames_ = 0;
        frozen_ = false;
        offset_ = {};
        history_.clear();
        return res;
    }

    // Smooth the classifier output.
    if (!emaInit_) {
        ema_ = in.probs;
        emaInit_ = true;
    } else {
        for (int i = 0; i < kNumPoses; ++i) ema_[i] = cfg_.emaAlpha * in.probs[i] + (1.f - cfg_.emaAlpha) * ema_[i];
    }
    const Pose best = static_cast<Pose>(argmax(ema_));

    if (pose_ == kNoHand) {
        // A hand that enters the frame already pinching must open first:
        // no clicks from a hand sliding into view.
        pose_ = Pose::Move;
        needMoveFirst_ = isPinch(best);
    }
    if (needMoveFirst_ && best == Pose::Move && ema_[0] >= cfg_.enterProb) needMoveFirst_ = false;

    // Pose switching with hysteresis.
    bool allowed = !(needMoveFirst_ && isPinch(best));
    if (best != pose_ && allowed && ema_[static_cast<int>(best)] >= cfg_.enterProb &&
        ema_[static_cast<int>(pose_)] <= cfg_.exitProb) {
        candidateFrames_ = (candidate_ == best) ? candidateFrames_ + 1 : 1;
        candidate_ = best;
    } else {
        candidateFrames_ = 0;
        candidate_ = kNoHand;
    }
    if (candidateFrames_ >= cfg_.enterFrames) {
        exitPose(pose_, in, out);
        pose_ = best;
        candidateFrames_ = 0;
        enterPose(best, in, out, res);
    }

    // Per-pose behaviour.
    switch (pose_) {
        case Pose::Move:
            offset_ = offset_ * cfg_.offsetDecay;
            if (length(offset_) < 0.5f) offset_ = {};
            if (enabled_) emitMove(in.cursor + offset_, out);
            else cursorOut_ = in.cursor + offset_;
            break;
        case Pose::PinchIndex:
        case Pose::PinchMiddle:
            if (frozen_ && length(in.cursor - freezeAnchor_) > cfg_.dragThresholdPx) {
                frozen_ = false;
                offset_ = freezePos_ - in.cursor;  // start the drag exactly at the click point
                if (pose_ == Pose::PinchIndex && leftDown_) ++counters_.drags;
            }
            if (!frozen_) {
                if (enabled_) emitMove(in.cursor + offset_, out);
                else cursorOut_ = in.cursor + offset_;
            }
            break;
        case Pose::Scroll: {
            Vec2 d = in.cursor - scrollLast_;
            scrollLast_ = in.cursor;
            if (length(d) >= cfg_.scrollDeadzonePx) {
                scrollAccY_ += -d.y * cfg_.scrollGain;  // hand up -> scroll up
                scrollAccX_ += d.x * cfg_.scrollGain;   // hand right -> scroll right
            }
            int wy = static_cast<int>(scrollAccY_), wx = static_cast<int>(scrollAccX_);
            scrollAccY_ -= static_cast<float>(wy);
            scrollAccX_ -= static_cast<float>(wx);
            if (enabled_ && wy) out.push_back({MouseActionType::Wheel, 0, 0, wy});
            if (enabled_ && wx) out.push_back({MouseActionType::HWheel, 0, 0, wx});
            if (enabled_) counters_.scrollUnits += std::abs(wy) + std::abs(wx);
            break;
        }
        case Pose::Fist:
            if (fistArmed_) {
                res.fistProgress = std::clamp(
                    static_cast<float>(in.timeUs - fistStartUs_) / static_cast<float>(cfg_.fistHoldUs), 0.f, 1.f);
                if (in.timeUs - fistStartUs_ >= cfg_.fistHoldUs) {
                    fistArmed_ = false;
                    setEnabled(!enabled_, out);
                    res.toggled = true;
                    ++counters_.toggles;
                }
            }
            break;
        default:
            break;
    }
    if (pose_ != Pose::Fist && !fistArmed_ && in.timeUs - fistEndUs_ >= cfg_.fistRearmUs) fistArmed_ = true;

    history_.emplace_back(in.timeUs, cursorOut_);
    while (!history_.empty() && history_.front().first < in.timeUs - 1'000'000) history_.pop_front();
    return res;
}

}  // namespace mausely
