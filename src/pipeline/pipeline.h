#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ai/gesture_classifier.h"
#include "ai/smoother.h"
#include "capture/camera_source.h"
#include "control/cursor_mapper.h"
#include "control/gesture_engine.h"
#include "control/mouse_injector.h"
#include "pipeline/settings.h"
#include "recorder/recorder.h"
#include "vision/hand_tracker.h"

namespace mausely {

// Timing and results of one processed camera frame.
struct FrameMetrics {
    uint64_t index = 0;
    int64_t arrivalUs = 0;
    double queueMs = 0;      // frame waited before processing started
    double palmMs = 0;       // palm detection (0 when tracking)
    double landmarkMs = 0;
    double classifyMs = 0;   // features + gesture MLP
    double smoothMs = 0;
    double controlMs = 0;    // gesture engine + SendInput
    double totalMs = 0;      // processing start -> input sent
    double latencyMs = 0;    // frame arrival -> input sent
    bool ranPalm = false;
    bool hand = false;
    bool skipped = false;    // idle throttling skipped the search on this frame
    Vec2 rawPoint;           // control point, frame-height units
    Vec2 smoothPoint;
    Vec2 rawCursor;          // screen px of the unsmoothed point (for jitter stats)
    Vec2 smoothCursor;       // screen px of the smoothed point
    Vec2 cursor;             // screen px actually sent (after gesture logic)
    Pose pose = GestureEngine::kNoHand;
};

// Everything the dashboard needs, copied out under a lock.
struct Snapshot {
    Frame frame;
    bool newFrame = false;
    HandResult hand;
    PoseProbs probs{};
    Pose pose = GestureEngine::kNoHand;
    bool enabled = false;
    bool frozen = false;
    float fistProgress = 0.f;
    GestureCounters counters;
    std::vector<FrameMetrics> metrics;  // frames processed since the previous snapshot

    std::string cameraName;
    double cameraFps = 0;
    uint64_t droppedFrames = 0;
    std::string modelStatus;   // model loading problem, empty when all is well
    std::string cameraStatus;  // camera problem, empty when frames are flowing
    std::string ortVersion;
    bool classifierReady = false;
    bool aiSmootherReady = false;

    bool recording = false;
    bool recordingWaiting = false;
    double recordingSecondsLeft = 0;
    int recordedSamples = 0;
    std::string recordingPath;
};

// Owns the capture and processing threads.
class Pipeline {
public:
    using SourceFactory = std::function<std::unique_ptr<FrameSource>(const Settings&, std::string* error)>;

    Pipeline();
    ~Pipeline();

    // Loads models from `modelDir` and starts the threads. The factory is
    // called on the capture thread (with COM initialised) and again to reconnect.
    bool start(const Settings& settings, const std::wstring& modelDir, SourceFactory factory, std::string* error);
    void stop();

    // Copies the latest state; moves out the metrics gathered since the last call.
    void snapshot(Snapshot& out);

    // Dry run: everything runs but no input is ever sent to Windows (benchmarks).
    void setDryRun(bool on) { dryRun_ = on; }
    void setEnabled(bool on) { enableRequest_ = on ? 1 : 0; }
    void toggleEnabled() { toggleRequest_ = true; }
    // Takes effect on the next frame (camera changes need a restart).
    void updateSettings(const Settings& s);
    void startRecording(Pose label, double delaySeconds, double seconds, const std::wstring& folder);
    void stopRecording() { stopRecordingRequest_ = true; }

private:
    void captureLoop();
    void processLoop();
    void processFrame(const Frame& f);
    void applyPending();

    // Models and state used only by the processing thread.
    HandTracker tracker_;
    GestureClassifier classifier_;
    Smoother smoother_;
    GestureEngine engine_;
    CursorMapper mapper_;
    MouseInjector injector_;
    Recorder recorder_;
    Settings settings_;
    int framesWithoutHand_ = 0;
    bool dryRun_ = false;

    SourceFactory factory_;
    Settings captureSettings_;  // copy for the capture thread (camera choice is fixed per start())
    std::thread captureThread_, processThread_;
    std::atomic<bool> running_{false};

    // Capture -> processing hand-off (latest frame wins).
    std::mutex frameMutex_;
    std::condition_variable frameCv_;
    Frame pending_;
    bool hasPending_ = false;
    std::atomic<uint64_t> dropped_{0};

    // Processing -> UI.
    std::mutex snapMutex_;
    Snapshot snap_;

    // UI -> processing requests.
    std::mutex requestMutex_;
    bool settingsDirty_ = false;
    Settings pendingSettings_;
    struct RecordRequest {
        bool pending = false;
        Pose label = Pose::Move;
        double delay = 3, seconds = 5;
        std::wstring folder;
    } recordRequest_;
    std::atomic<int> enableRequest_{-1};
    std::atomic<bool> toggleRequest_{false};
    std::atomic<bool> stopRecordingRequest_{false};
};

}  // namespace mausely
