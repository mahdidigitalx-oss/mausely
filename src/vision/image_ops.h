#pragma once

#include "core/frame.h"
#include "core/geometry.h"

namespace mausely {

// Maps tensor pixels back to image pixels: image = (tensor - pad) / scale.
struct Letterbox {
    float scale = 1.f;
    float padX = 0.f;
    float padY = 0.f;

    Vec2 toImage(Vec2 tensorPx) const { return {(tensorPx.x - padX) / scale, (tensorPx.y - padY) / scale}; }
};

// Resizes the whole frame into a dst x dst RGB float [0,1] NHWC tensor,
// keeping aspect ratio and padding with black.
Letterbox letterboxToTensor(const Frame& frame, int dst, float* out);

// Samples the rotated square `roi` into a dst x dst RGB float [0,1] tensor.
// Crop "up" (-y) maps to the image direction (sin a, -cos a).
void cropToTensor(const Frame& frame, const RotatedRect& roi, int dst, float* out);

// Maps a point in crop pixel coordinates [0, dst] back to image pixels.
Vec2 cropToImage(const RotatedRect& roi, int dst, Vec2 cropPx);

// Bilinear BGRA sample at continuous pixel position (x, y) where pixel i
// covers [i, i+1). Out-of-image samples blend towards black.
void sampleRgb(const Frame& frame, float x, float y, float* rgb);

}  // namespace mausely
