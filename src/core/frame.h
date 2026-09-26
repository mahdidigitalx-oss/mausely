#pragma once

#include <cstdint>
#include <vector>

namespace mausely {

// A top-down BGRA8 image (stride == width * 4) with capture timing.
struct Frame {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> bgra;
    int64_t arrivalUs = 0;  // Clock::nowUs() when the frame left the camera API
    uint64_t index = 0;

    bool empty() const { return width <= 0 || height <= 0 || bgra.empty(); }
    const uint8_t* row(int y) const { return bgra.data() + static_cast<size_t>(y) * width * 4; }
    void resize(int w, int h) {
        width = w;
        height = h;
        bgra.resize(static_cast<size_t>(w) * h * 4);
    }
};

}  // namespace mausely
