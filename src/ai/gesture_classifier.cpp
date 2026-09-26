#include "ai/gesture_classifier.h"

namespace mausely {

const char* poseName(Pose p) {
    switch (p) {
        case Pose::Move: return "MOVE";
        case Pose::PinchIndex: return "PINCH_INDEX";
        case Pose::PinchMiddle: return "PINCH_MIDDLE";
        case Pose::Scroll: return "SCROLL";
        case Pose::Fist: return "FIST";
        default: return "NONE";
    }
}

bool GestureClassifier::load(const std::wstring& modelPath, std::string* error) {
    if (!model_.load(modelPath, 1, error)) return false;
    std::string version = model_.metadata("feature_version");
    if (version != std::to_string(kFeatureVersion)) {
        if (error) *error = "gesture model feature_version '" + version + "' != " + std::to_string(kFeatureVersion);
        return false;
    }
    if (model_.metadata("classes") != "MOVE,PINCH_INDEX,PINCH_MIDDLE,SCROLL,FIST") {
        if (error) *error = "gesture model has unexpected classes: " + model_.metadata("classes");
        return false;
    }
    return true;
}

bool GestureClassifier::classify(const FeatureVector& features, PoseProbs& probs) {
    if (!model_.run({features.data()}, {{1, kNumFeatures}}, out_, &lastError_)) return false;
    for (int i = 0; i < kNumPoses; ++i) probs[i] = out_[0].data[i];
    return true;
}

}  // namespace mausely
