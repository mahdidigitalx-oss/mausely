#include "vision/hand_landmarker.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

#include "vision/image_ops.h"

namespace mausely {

RotatedRect landmarksToRoi(const std::array<Vec3, kNumLandmarks>& lm) {
    static constexpr int kPartial[] = {0, 1, 2, 3, 5, 6, 9, 10, 13, 14, 17, 18};

    // Rotation: wrist -> blend of index, middle and ring MCPs.
    Vec2 p0{lm[kWrist].x, lm[kWrist].y};
    Vec2 p1{(lm[kIndexMcp].x + lm[kRingMcp].x) * 0.5f, (lm[kIndexMcp].y + lm[kRingMcp].y) * 0.5f};
    p1 = {(p1.x + lm[kMiddleMcp].x) * 0.5f, (p1.y + lm[kMiddleMcp].y) * 0.5f};
    float angle = normalizeRadians(kPi / 2.f - std::atan2(-(p1.y - p0.y), p1.x - p0.x));

    // Axis-aligned centre, then the bounding box in the rotated frame.
    float minX = FLT_MAX, minY = FLT_MAX, maxX = -FLT_MAX, maxY = -FLT_MAX;
    for (int i : kPartial) {
        minX = std::min(minX, lm[i].x);
        maxX = std::max(maxX, lm[i].x);
        minY = std::min(minY, lm[i].y);
        maxY = std::max(maxY, lm[i].y);
    }
    float acx = (minX + maxX) * 0.5f, acy = (minY + maxY) * 0.5f;
    float c = std::cos(-angle), s = std::sin(-angle);
    float pminX = FLT_MAX, pminY = FLT_MAX, pmaxX = -FLT_MAX, pmaxY = -FLT_MAX;
    for (int i : kPartial) {
        float ox = lm[i].x - acx, oy = lm[i].y - acy;
        float px = ox * c - oy * s, py = ox * s + oy * c;
        pminX = std::min(pminX, px);
        pmaxX = std::max(pmaxX, px);
        pminY = std::min(pminY, py);
        pmaxY = std::max(pmaxY, py);
    }
    float pcx = (pminX + pmaxX) * 0.5f, pcy = (pminY + pmaxY) * 0.5f;
    float cr = std::cos(angle), sr = std::sin(angle);
    float cx = pcx * cr - pcy * sr + acx;
    float cy = pcx * sr + pcy * cr + acy;
    float w = pmaxX - pminX, h = pmaxY - pminY;

    const float shiftY = -0.1f;
    cx += -h * shiftY * sr;
    cy += h * shiftY * cr;
    return {cx, cy, std::max(w, h) * 2.0f, angle};
}

bool HandLandmarker::load(const std::wstring& modelPath, int threads, std::string* error) {
    if (!model_.load(modelPath, threads, error)) return false;
    const auto& outs = model_.outputs();
    if (outs.size() != 4) {
        if (error) *error = "landmark model: expected 4 outputs";
        return false;
    }
    // OpenCV Zoo export: Identity=landmarks, Identity_1=presence,
    // Identity_2=handedness, Identity_3=world landmarks.
    int a = model_.outputIndex("Identity"), b = model_.outputIndex("Identity_1");
    int c = model_.outputIndex("Identity_2"), d = model_.outputIndex("Identity_3");
    if (a >= 0 && b >= 0 && c >= 0 && d >= 0) {
        idxLandmarks_ = a;
        idxPresence_ = b;
        idxHandedness_ = c;
        idxWorld_ = d;
    }
    input_.resize(static_cast<size_t>(kInputSize) * kInputSize * 3);
    return true;
}

bool HandLandmarker::run(const Frame& frame, const RotatedRect& roi, HandLandmarks& out) {
    if (!model_.loaded() || frame.empty() || roi.size < 8.f) return false;
    cropToTensor(frame, roi, kInputSize, input_.data());
    if (!model_.run({input_.data()}, {{1, kInputSize, kInputSize, 3}}, outputs_, &lastError_)) return false;

    const float* lm = outputs_[idxLandmarks_].data.data();
    const float* world = outputs_[idxWorld_].data.data();
    out.presence = outputs_[idxPresence_].data[0];
    out.handedness = outputs_[idxHandedness_].data[0];
    const float zScale = roi.size / kInputSize;
    for (int i = 0; i < kNumLandmarks; ++i) {
        Vec2 p = cropToImage(roi, kInputSize, {lm[i * 3], lm[i * 3 + 1]});
        out.image[i] = {p.x, p.y, lm[i * 3 + 2] * zScale};
        // World landmarks are in the crop's rotated frame: undo the rotation in x/y.
        float wx = world[i * 3], wy = world[i * 3 + 1];
        float c = std::cos(roi.angle), s = std::sin(roi.angle);
        out.world[i] = {c * wx - s * wy, s * wx + c * wy, world[i * 3 + 2]};
    }
    return true;
}

}  // namespace mausely
