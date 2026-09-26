#pragma once

#include <array>
#include <string>
#include <vector>

#include "core/frame.h"
#include "core/geometry.h"
#include "vision/ort_model.h"

namespace mausely {

// Palm keypoints: 0 wrist, 1 index MCP, 2 middle MCP, 3 ring MCP,
// 4 pinky MCP, 5 thumb CMC, 6 thumb MCP (image pixels).
struct PalmDetection {
    float score = 0.f;
    float x1 = 0.f, y1 = 0.f, x2 = 0.f, y2 = 0.f;
    std::array<Vec2, 7> kp{};
};

// 2016 anchor centres (normalised) for the 192x192 MediaPipe palm model.
std::vector<Vec2> generatePalmAnchors();

float iou(const PalmDetection& a, const PalmDetection& b);

// MediaPipe-style weighted NMS: overlapping detections are averaged by score.
std::vector<PalmDetection> weightedNms(std::vector<PalmDetection> dets, float iouThreshold);

// Palm box -> square hand ROI rotated so the fingers point "up" in the crop.
RotatedRect palmToRoi(const PalmDetection& palm);

class PalmDetector {
public:
    static constexpr int kInputSize = 192;

    bool load(const std::wstring& modelPath, int threads, std::string* error);
    // Detections sorted by descending score, in image pixels.
    std::vector<PalmDetection> detect(const Frame& frame, float scoreThreshold = 0.5f, float iouThreshold = 0.3f);
    const std::string& lastError() const { return lastError_; }

private:
    OrtModel model_;
    std::vector<Vec2> anchors_;
    std::vector<float> input_;
    std::vector<Tensor> outputs_;
    int regIndex_ = 0;
    int scoreIndex_ = 1;
    std::string lastError_;
};

}  // namespace mausely
