#include "vision/palm_detector.h"

#include <algorithm>
#include <cmath>

#include "vision/image_ops.h"

namespace mausely {

std::vector<Vec2> generatePalmAnchors() {
    // SSD anchors for input 192: stride 8 -> 24x24 grid with 2 anchors,
    // strides 16,16,16 -> one 12x12 grid with 6 anchors. Fixed size anchors.
    std::vector<Vec2> anchors;
    anchors.reserve(2016);
    auto grid = [&](int n, int perCell) {
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
                for (int k = 0; k < perCell; ++k)
                    anchors.push_back({(x + 0.5f) / n, (y + 0.5f) / n});
    };
    grid(24, 2);
    grid(12, 6);
    return anchors;
}

float iou(const PalmDetection& a, const PalmDetection& b) {
    float ix = std::max(0.f, std::min(a.x2, b.x2) - std::max(a.x1, b.x1));
    float iy = std::max(0.f, std::min(a.y2, b.y2) - std::max(a.y1, b.y1));
    float inter = ix * iy;
    float uni = (a.x2 - a.x1) * (a.y2 - a.y1) + (b.x2 - b.x1) * (b.y2 - b.y1) - inter;
    return uni > 0.f ? inter / uni : 0.f;
}

std::vector<PalmDetection> weightedNms(std::vector<PalmDetection> dets, float iouThreshold) {
    std::sort(dets.begin(), dets.end(), [](const auto& a, const auto& b) { return a.score > b.score; });
    std::vector<PalmDetection> result;
    std::vector<bool> used(dets.size(), false);
    for (size_t i = 0; i < dets.size(); ++i) {
        if (used[i]) continue;
        PalmDetection merged{};
        float wsum = 0.f;
        for (size_t j = i; j < dets.size(); ++j) {
            if (used[j] || iou(dets[i], dets[j]) <= iouThreshold) continue;
            used[j] = true;
            float w = dets[j].score;
            wsum += w;
            merged.x1 += dets[j].x1 * w;
            merged.y1 += dets[j].y1 * w;
            merged.x2 += dets[j].x2 * w;
            merged.y2 += dets[j].y2 * w;
            for (int k = 0; k < 7; ++k) merged.kp[k] = merged.kp[k] + dets[j].kp[k] * w;
        }
        float inv = 1.f / wsum;
        merged.x1 *= inv;
        merged.y1 *= inv;
        merged.x2 *= inv;
        merged.y2 *= inv;
        for (auto& p : merged.kp) p = p * inv;
        merged.score = dets[i].score;
        result.push_back(merged);
    }
    return result;
}

RotatedRect palmToRoi(const PalmDetection& palm) {
    // MediaPipe palm -> hand rect: rotation from wrist (kp0) to middle MCP (kp2),
    // shift_y = -0.5, scale = 2.6, square by the long side.
    Vec2 p0 = palm.kp[0], p2 = palm.kp[2];
    float angle = normalizeRadians(kPi / 2.f - std::atan2(-(p2.y - p0.y), p2.x - p0.x));
    float w = palm.x2 - palm.x1, h = palm.y2 - palm.y1;
    float cx = (palm.x1 + palm.x2) * 0.5f, cy = (palm.y1 + palm.y2) * 0.5f;
    const float shiftY = -0.5f;
    cx += -h * shiftY * std::sin(angle);
    cy += h * shiftY * std::cos(angle);
    return {cx, cy, std::max(w, h) * 2.6f, angle};
}

bool PalmDetector::load(const std::wstring& modelPath, int threads, std::string* error) {
    if (!model_.load(modelPath, threads, error)) return false;
    // Identify outputs by shape: [1,2016,18] regressors and [1,2016,1] scores.
    const auto& outs = model_.outputs();
    if (outs.size() != 2) {
        if (error) *error = "palm model: expected 2 outputs";
        return false;
    }
    regIndex_ = outs[0].shape.back() == 18 ? 0 : 1;
    scoreIndex_ = 1 - regIndex_;
    anchors_ = generatePalmAnchors();
    input_.resize(static_cast<size_t>(kInputSize) * kInputSize * 3);
    return true;
}

std::vector<PalmDetection> PalmDetector::detect(const Frame& frame, float scoreThreshold, float iouThreshold) {
    std::vector<PalmDetection> dets;
    if (!model_.loaded() || frame.empty()) return dets;

    Letterbox lb = letterboxToTensor(frame, kInputSize, input_.data());
    if (!model_.run({input_.data()}, {{1, kInputSize, kInputSize, 3}}, outputs_, &lastError_)) return dets;

    const float* reg = outputs_[regIndex_].data.data();
    const float* scores = outputs_[scoreIndex_].data.data();
    const float logitThreshold = std::log(scoreThreshold / (1.f - scoreThreshold));
    const float inv = 1.f / kInputSize;

    for (size_t i = 0; i < anchors_.size(); ++i) {
        float logit = scores[i];
        if (logit < logitThreshold) continue;
        const float* r = reg + i * 18;
        Vec2 a = anchors_[i];
        float cx = r[0] * inv + a.x, cy = r[1] * inv + a.y;
        float w = r[2] * inv, h = r[3] * inv;
        auto toImage = [&](float nx, float ny) { return lb.toImage({nx * kInputSize, ny * kInputSize}); };
        PalmDetection d;
        d.score = 1.f / (1.f + std::exp(-std::clamp(logit, -80.f, 80.f)));
        Vec2 tl = toImage(cx - w * 0.5f, cy - h * 0.5f);
        Vec2 br = toImage(cx + w * 0.5f, cy + h * 0.5f);
        d.x1 = tl.x;
        d.y1 = tl.y;
        d.x2 = br.x;
        d.y2 = br.y;
        for (int k = 0; k < 7; ++k) d.kp[k] = toImage(r[4 + 2 * k] * inv + a.x, r[5 + 2 * k] * inv + a.y);
        dets.push_back(d);
    }
    return weightedNms(std::move(dets), iouThreshold);
}

}  // namespace mausely
