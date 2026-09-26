#pragma once

#include <string>

#include "core/frame.h"

namespace mausely {

// A blocking source of frames (webcam or test image).
class FrameSource {
public:
    virtual ~FrameSource() = default;
    // Blocks until the next frame is available. Returns false on error / end.
    virtual bool read(Frame& out, std::string* error) = 0;
    virtual std::string description() const = 0;
    // Nominal frames per second (0 if unknown).
    virtual double nominalFps() const = 0;
};

}  // namespace mausely
