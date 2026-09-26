#pragma once

#include <string>
#include <vector>

#include "capture/camera_source.h"

struct IMFSourceReader;
struct IMFMediaSource;

namespace mausely {

struct CameraInfo {
    std::string name;
};

// Lists video capture devices (call from a thread with COM initialised).
std::vector<CameraInfo> enumerateCameras();

// Webcam capture through Media Foundation's source reader in low-latency
// mode, converted to BGRA by the reader's video processor.
class MfCamera : public FrameSource {
public:
    MfCamera() = default;
    ~MfCamera() override;
    MfCamera(const MfCamera&) = delete;
    MfCamera& operator=(const MfCamera&) = delete;

    // Picks the native mode closest to width x height with the highest fps <= maxFps.
    bool open(int index, int width, int height, int maxFps, std::string* error);
    void close();

    bool read(Frame& out, std::string* error) override;
    std::string description() const override { return description_; }
    double nominalFps() const override { return fps_; }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    IMFMediaSource* source_ = nullptr;
    IMFSourceReader* reader_ = nullptr;
    int width_ = 0, height_ = 0;
    long stride_ = 0;
    double fps_ = 0.0;
    uint64_t counter_ = 0;
    std::string description_;
};

}  // namespace mausely
