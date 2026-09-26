#pragma once

#include <functional>
#include <string>
#include <vector>

#include <imgui.h>

#include "capture/mf_camera.h"
#include "core/rolling_stats.h"
#include "pipeline/pipeline.h"
#include "ui/d3d_window.h"

namespace mausely {

// The dashboard: camera view with hand overlay, live statistics and settings.
class Dashboard {
public:
    Dashboard(Pipeline& pipeline, Settings& settings, std::wstring settingsPath, std::wstring recordingsDir,
              std::function<void()> restartPipeline);

    // Pulls the latest pipeline state (call every UI frame, even when minimised).
    void update(D3dWindow& win);
    void draw(D3dWindow& win);
    // Opens tab 0..4 (Performance, Smoothing, Gestures, Settings, Recorder) on the next draw.
    void selectTab(int index) { selectTab_ = index; }

private:
    struct Stage {
        const char* name;
        RollingStats stats{300};
    };

    void ingest(const FrameMetrics& m);
    void drawHeader();
    void drawCamera(const ImVec2& size);
    void drawPerformance();
    void drawSmoothing();
    void drawGestures();
    void drawSettings();
    void drawRecorder();
    void settingsChanged();
    void updateCpu();

    Pipeline& pipeline_;
    Settings& settings_;
    std::wstring settingsPath_;
    std::wstring recordingsDir_;
    std::function<void()> restart_;

    Snapshot snap_;
    DynamicTexture camTex_;
    std::vector<CameraInfo> cameras_;
    bool camerasListed_ = false;

    // Rolling statistics (last 300 frames ~ 10 s).
    enum StageId { kQueue, kPalm, kLandmark, kClassify, kSmooth, kControl, kTotal, kLatency, kStageCount };
    std::vector<Stage> stages_;
    RollingStats frameInterval_{120};
    RollingStats jitterRaw_{300}, jitterSmooth_{300};
    RollingStats handRate_{300};
    int64_t lastArrivalUs_ = 0;
    uint64_t processed_ = 0;

    // Plot history.
    static constexpr int kHistory = 300;
    std::vector<float> histTotal_, histLandmark_, histPalm_, histLatency_;
    std::vector<float> histRawX_, histSmoothX_, histRawY_, histSmoothY_;
    std::vector<FrameMetrics> recent_;  // last few frames with a hand, for jitter

    double cpuPercent_ = 0;
    int64_t cpuLastWallUs_ = 0;
    uint64_t cpuLastProcess_ = 0;

    int64_t lastSaveRequestUs_ = 0;
    bool saveDirty_ = false;

    int selectTab_ = -1;
    int recordLabel_ = 1;
    float recordDelay_ = 3.f, recordSeconds_ = 6.f;
};

}  // namespace mausely
