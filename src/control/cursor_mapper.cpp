#include "control/cursor_mapper.h"

#include <algorithm>

namespace mausely {

void CursorMapper::configure(const ActiveRegion& region, bool mirror, const ScreenRect& screen) {
    region_ = region;
    // Keep the region valid even if the settings file is odd.
    region_.x1 = std::max(region_.x1, region_.x0 + 0.05f);
    region_.y1 = std::max(region_.y1, region_.y0 + 0.05f);
    mirror_ = mirror;
    screen_ = screen;
}

Vec2 CursorMapper::toScreen(Vec2 frameNorm) const {
    float x = mirror_ ? 1.f - frameNorm.x : frameNorm.x;
    float u = std::clamp((x - region_.x0) / (region_.x1 - region_.x0), 0.f, 1.f);
    float v = std::clamp((frameNorm.y - region_.y0) / (region_.y1 - region_.y0), 0.f, 1.f);
    return {screen_.left + u * (screen_.width - 1), screen_.top + v * (screen_.height - 1)};
}

}  // namespace mausely
