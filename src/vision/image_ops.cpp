#include "vision/image_ops.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace mausely {

void sampleRgb(const Frame& frame, float x, float y, float* rgb) {
    // Convert to pixel-center coordinates.
    float fx = x - 0.5f;
    float fy = y - 0.5f;
    int x0 = static_cast<int>(std::floor(fx));
    int y0 = static_cast<int>(std::floor(fy));
    float ax = fx - static_cast<float>(x0);
    float ay = fy - static_cast<float>(y0);

    float acc[3] = {0.f, 0.f, 0.f};
    const float w[4] = {(1 - ax) * (1 - ay), ax * (1 - ay), (1 - ax) * ay, ax * ay};
    const int xs[4] = {x0, x0 + 1, x0, x0 + 1};
    const int ys[4] = {y0, y0, y0 + 1, y0 + 1};
    for (int k = 0; k < 4; ++k) {
        if (xs[k] < 0 || ys[k] < 0 || xs[k] >= frame.width || ys[k] >= frame.height) continue;
        const uint8_t* p = frame.row(ys[k]) + static_cast<size_t>(xs[k]) * 4;
        acc[0] += w[k] * p[2];  // R
        acc[1] += w[k] * p[1];  // G
        acc[2] += w[k] * p[0];  // B
    }
    constexpr float inv = 1.f / 255.f;
    rgb[0] = acc[0] * inv;
    rgb[1] = acc[1] * inv;
    rgb[2] = acc[2] * inv;
}

void rgbaToFrame(const uint8_t* rgba, int width, int height, int stride, int rotation, Frame& out) {
    rotation = ((rotation % 360) + 360) % 360;
    const bool swap = rotation == 90 || rotation == 270;
    out.resize(swap ? height : width, swap ? width : height);
    // Source pixel (x, y) lands at index o0 + x * dx + y * dy of the W x H output.
    const ptrdiff_t w = out.width, h = out.height;
    ptrdiff_t o0 = 0, dx = 1, dy = w;
    switch (rotation) {
        case 90: o0 = w - 1; dx = w; dy = -1; break;          // (x, y) -> (W - 1 - y, x)
        case 180: o0 = w * h - 1; dx = -1; dy = -w; break;    // (x, y) -> (W - 1 - x, H - 1 - y)
        case 270: o0 = (h - 1) * w; dx = -w; dy = 1; break;   // (x, y) -> (y, H - 1 - x)
        default: break;
    }
    uint8_t* dst = out.bgra.data();
    for (int y = 0; y < height; ++y) {
        const uint8_t* s = rgba + static_cast<size_t>(y) * stride;
        ptrdiff_t o = o0 + y * dy;
        for (int x = 0; x < width; ++x, s += 4, o += dx) {
            uint8_t* d = dst + o * 4;
            d[0] = s[2];
            d[1] = s[1];
            d[2] = s[0];
            d[3] = 255;
        }
    }
}

Letterbox letterboxToTensor(const Frame& frame, int dst, float* out) {
    Letterbox lb;
    lb.scale = std::min(static_cast<float>(dst) / frame.width, static_cast<float>(dst) / frame.height);
    lb.padX = (dst - frame.width * lb.scale) * 0.5f;
    lb.padY = (dst - frame.height * lb.scale) * 0.5f;

    // When shrinking a lot, average a few bilinear taps per output pixel to
    // avoid aliasing (cheap box prefilter).
    float step = 1.f / lb.scale;
    int taps = std::clamp(static_cast<int>(std::ceil(step / 2.f)), 1, 3);

    for (int v = 0; v < dst; ++v) {
        for (int u = 0; u < dst; ++u) {
            float* o = out + (static_cast<size_t>(v) * dst + u) * 3;
            float cx = (u + 0.5f - lb.padX) / lb.scale;
            float cy = (v + 0.5f - lb.padY) / lb.scale;
            if (cx < 0.f || cy < 0.f || cx > frame.width || cy > frame.height) {
                o[0] = o[1] = o[2] = 0.f;
                continue;
            }
            float acc[3] = {0.f, 0.f, 0.f};
            for (int j = 0; j < taps; ++j) {
                for (int i = 0; i < taps; ++i) {
                    float sx = cx + ((i + 0.5f) / taps - 0.5f) * step;
                    float sy = cy + ((j + 0.5f) / taps - 0.5f) * step;
                    float rgb[3];
                    sampleRgb(frame, sx, sy, rgb);
                    acc[0] += rgb[0];
                    acc[1] += rgb[1];
                    acc[2] += rgb[2];
                }
            }
            float n = 1.f / static_cast<float>(taps * taps);
            o[0] = acc[0] * n;
            o[1] = acc[1] * n;
            o[2] = acc[2] * n;
        }
    }
    return lb;
}

Vec2 cropToImage(const RotatedRect& roi, int dst, Vec2 p) {
    float lx = (p.x / dst - 0.5f) * roi.size;
    float ly = (p.y / dst - 0.5f) * roi.size;
    float c = std::cos(roi.angle), s = std::sin(roi.angle);
    return {roi.cx + c * lx - s * ly, roi.cy + s * lx + c * ly};
}

void cropToTensor(const Frame& frame, const RotatedRect& roi, int dst, float* out) {
    float c = std::cos(roi.angle), s = std::sin(roi.angle);
    float step = roi.size / dst;
    // Crop pixel (u, v) centre -> image position; walk incrementally.
    float lx0 = (0.5f / dst - 0.5f) * roi.size;
    for (int v = 0; v < dst; ++v) {
        float ly = ((v + 0.5f) / dst - 0.5f) * roi.size;
        float x = roi.cx + c * lx0 - s * ly;
        float y = roi.cy + s * lx0 + c * ly;
        float* o = out + static_cast<size_t>(v) * dst * 3;
        for (int u = 0; u < dst; ++u) {
            sampleRgb(frame, x, y, o);
            o += 3;
            x += c * step;
            y += s * step;
        }
    }
}

}  // namespace mausely
