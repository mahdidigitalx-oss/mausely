#pragma once

#include "core/geometry.h"

namespace mausely {

// Part of the camera frame (normalised, after mirroring) that spans the screen.
struct ActiveRegion {
    float x0 = 0.20f, y0 = 0.15f, x1 = 0.80f, y1 = 0.75f;
};

struct ScreenRect {
    int left = 0, top = 0, width = 1920, height = 1080;
};

// Maps normalised camera coordinates to virtual-desktop pixels.
class CursorMapper {
public:
    void configure(const ActiveRegion& region, bool mirror, const ScreenRect& screen);
    // frameNorm: (x / width, y / height) of the unmirrored camera frame. Clamped to the screen.
    Vec2 toScreen(Vec2 frameNorm) const;
    const ScreenRect& screen() const { return screen_; }

private:
    ActiveRegion region_;
    bool mirror_ = true;
    ScreenRect screen_;
};

}  // namespace mausely
