#pragma once

#include <string>

#include "capture/camera_source.h"

namespace mausely {

// Loads an image file (JPEG/PNG/BMP) into a BGRA frame.
bool loadImageFile(const std::wstring& path, Frame& out, std::string* error);

// Replays one still image at a fixed rate; used for tests and --benchmark.
class ImageSource : public FrameSource {
public:
    bool open(const std::wstring& path, double fps, std::string* error);
    bool read(Frame& out, std::string* error) override;
    std::string description() const override { return description_; }
    double nominalFps() const override { return fps_; }

private:
    Frame image_;
    double fps_ = 30.0;
    int64_t nextUs_ = 0;
    uint64_t counter_ = 0;
    std::string description_;
};

}  // namespace mausely
