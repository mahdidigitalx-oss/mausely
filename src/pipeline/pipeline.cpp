#include "pipeline/pipeline.h"

#include <windows.h>
#include <objbase.h>

#include <chrono>

#include "ai/features.h"
#include "core/clock.h"
#include "core/log.h"

namespace mausely {
namespace {

constexpr size_t kMaxQueuedMetrics = 600;
constexpr int kIdleFramesBeforeThrottle = 60;  // ~2 s at 30 fps
constexpr int kLandmarksForControlPoint[] = {kWrist, kIndexMcp, kMiddleMcp, kRingMcp, kPinkyMcp};

}  // namespace

Pipeline::Pipeline() = default;

Pipeline::~Pipeline() { stop(); }

bool Pipeline::start(const Settings& settings, const std::wstring& modelDir, SourceFactory factory,
                     std::string* error) {
    stop();
    settings_ = settings;
    captureSettings_ = settings;
    factory_ = std::move(factory);

    std::string status, err;
    bool trackerOk = tracker_.load(modelDir + L"palm_detection_mediapipe_2023feb.onnx",
                                   modelDir + L"handpose_estimation_mediapipe_2023feb.onnx",
                                   settings.inferenceThreads, &err);
    if (!trackerOk) {
        status = "Hand tracking unavailable: " + err;
        logError(status);
    }
    if (!classifier_.load(modelDir + L"gesture_mlp.onnx", &err)) {
        logError("gesture model: " + err);
        if (status.empty()) status = "Gesture model unavailable (only cursor movement works): " + err;
    }
    if (!smoother_.loadAi(modelDir + L"smoother.onnx", &err)) logError("AI smoother: " + err);

    {
        std::lock_guard<std::mutex> lock(snapMutex_);
        snap_ = Snapshot{};
        snap_.modelStatus = status;
        snap_.ortVersion = OrtRuntime::instance().version();
        snap_.classifierReady = classifier_.loaded();
        snap_.aiSmootherReady = smoother_.aiAvailable();
    }

    engine_ = GestureEngine{};
    framesWithoutHand_ = 0;
    pendingSettings_ = settings;
    settingsDirty_ = true;
    applyPending();

    dropped_ = 0;
    hasPending_ = false;
    running_ = true;
    captureThread_ = std::thread(&Pipeline::captureLoop, this);
    processThread_ = std::thread(&Pipeline::processLoop, this);
    if (!trackerOk && error) *error = status;
    return trackerOk;
}

void Pipeline::stop() {
    if (!running_.exchange(false)) return;
    frameCv_.notify_all();
    if (captureThread_.joinable()) captureThread_.join();
    if (processThread_.joinable()) processThread_.join();
}

void Pipeline::updateSettings(const Settings& s) {
    std::lock_guard<std::mutex> lock(requestMutex_);
    pendingSettings_ = s;
    settingsDirty_ = true;
}

void Pipeline::startRecording(Pose label, double delaySeconds, double seconds, const std::wstring& folder) {
    std::lock_guard<std::mutex> lock(requestMutex_);
    recordRequest_ = {true, label, delaySeconds, seconds, folder};
}

void Pipeline::snapshot(Snapshot& out) {
    std::lock_guard<std::mutex> lock(snapMutex_);
    // Move the big buffers aside so `out = snap_` only copies small fields.
    Frame uiFrame = std::move(out.frame);
    Frame snapFrame = std::move(snap_.frame);
    std::vector<FrameMetrics> metrics = std::move(snap_.metrics);
    const bool fresh = snap_.newFrame;
    out = snap_;
    out.metrics = std::move(metrics);
    out.newFrame = fresh;
    out.droppedFrames = dropped_;
    if (fresh) {  // hand the new frame over and recycle the UI's old buffer
        out.frame = std::move(snapFrame);
        snap_.frame = std::move(uiFrame);
    } else {
        out.frame = std::move(uiFrame);
        snap_.frame = std::move(snapFrame);
    }
    snap_.metrics.clear();
    snap_.newFrame = false;
}

void Pipeline::captureLoop() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    std::unique_ptr<FrameSource> source;
    Frame f;
    auto setStatus = [this](const std::string& s, const std::string& name, double fps) {
        std::lock_guard<std::mutex> lock(snapMutex_);
        snap_.cameraStatus = s;
        if (!name.empty()) {
            snap_.cameraName = name;
            snap_.cameraFps = fps;
        }
    };
    auto sleepWhileRunning = [this](int ms) {
        for (int i = 0; i < ms / 50 && running_; ++i) Sleep(50);
    };

    while (running_) {
        if (!source) {
            std::string err;
            source = factory_(captureSettings_, &err);
            if (!source) {
                setStatus(err + " (retrying)", "", 0);
                logError("camera: " + err);
                sleepWhileRunning(2000);
                continue;
            }
            setStatus("", source->description(), source->nominalFps());
        }
        std::string err;
        if (!source->read(f, &err)) {
            setStatus(err + " (reconnecting)", "", 0);
            logError("camera read: " + err);
            source.reset();
            sleepWhileRunning(1000);
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(frameMutex_);
            if (hasPending_) ++dropped_;
            std::swap(pending_, f);
            hasPending_ = true;
        }
        frameCv_.notify_one();
    }
    source.reset();
    CoUninitialize();
}

void Pipeline::processLoop() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    Frame f;
    while (running_) {
        {
            std::unique_lock<std::mutex> lock(frameMutex_);
            frameCv_.wait_for(lock, std::chrono::milliseconds(100), [this] { return hasPending_ || !running_; });
            if (!running_) break;
            if (hasPending_) {
                std::swap(f, pending_);
                hasPending_ = false;
            } else {
                f.width = 0;
            }
        }
        applyPending();
        if (f.width > 0) processFrame(f);
    }
    // Never leave a button pressed.
    MouseActions release;
    engine_.setEnabled(false, release);
    if (!dryRun_) {
        injector_.apply(release);
        injector_.releaseAll();
    }
    recorder_.stop();
}

void Pipeline::applyPending() {
    MouseActions actions;
    {
        std::lock_guard<std::mutex> lock(requestMutex_);
        if (settingsDirty_) {
            settings_ = pendingSettings_;
            settingsDirty_ = false;
            mapper_.configure(settings_.region, settings_.mirror, virtualDesktop());
            tracker_.presenceThreshold = settings_.handConfidence;
            smoother_.setOneEuroParams(settings_.oneEuroMinCutoff, settings_.oneEuroBeta);
            smoother_.setPredictStrength(settings_.predictStrength);
            smoother_.setMode(settings_.smoothing);
            GestureConfig gc = settings_.gestures;
            gc.doubleClickUs = static_cast<int64_t>(GetDoubleClickTime()) * 1000;
            engine_.setConfig(gc);
        }
        if (recordRequest_.pending) {
            recordRequest_.pending = false;
            std::string err;
            int64_t now = Clock::nowUs();
            if (!recorder_.start(recordRequest_.folder, recordRequest_.label, now,
                                 static_cast<int64_t>(recordRequest_.delay * 1e6),
                                 static_cast<int64_t>(recordRequest_.seconds * 1e6), &err))
                logError(err);
        }
    }
    if (stopRecordingRequest_.exchange(false)) recorder_.stop();
    int req = enableRequest_.exchange(-1);
    if (req >= 0) engine_.setEnabled(req == 1, actions);
    if (toggleRequest_.exchange(false)) engine_.setEnabled(!engine_.enabled(), actions);
    if (!dryRun_) injector_.apply(actions);
}

void Pipeline::processFrame(const Frame& f) {
    FrameMetrics m;
    m.index = f.index;
    m.arrivalUs = f.arrivalUs;
    const int64_t t0 = Clock::nowUs();
    m.queueMs = static_cast<double>(t0 - f.arrivalUs) / 1000.0;

    // Hand tracking (searching less often after a while without a hand saves CPU).
    HandResult hand;
    TrackerTimes tt;
    bool throttle = settings_.idleThrottle && framesWithoutHand_ > kIdleFramesBeforeThrottle && f.index % 3 != 0;
    if (throttle) {
        m.skipped = true;
    } else {
        hand = tracker_.process(f, tt);
    }
    framesWithoutHand_ = hand.valid ? 0 : framesWithoutHand_ + 1;
    m.palmMs = tt.palmMs;
    m.landmarkMs = tt.landmarkMs;
    m.ranPalm = tt.ranPalm;
    m.hand = hand.valid;

    PoseProbs probs{};
    probs[static_cast<int>(Pose::Move)] = 1.f;
    Vec2 cursor;
    if (hand.valid) {
        Stopwatch sw;
        if (classifier_.loaded()) classifier_.classify(computeFeatures(hand.lm.world), probs);
        m.classifyMs = sw.lapMs();

        Vec2 c;
        for (int i : kLandmarksForControlPoint) c = c + Vec2{hand.lm.image[i].x, hand.lm.image[i].y};
        const float h = static_cast<float>(f.height);
        m.rawPoint = {c.x / 5.f / h, c.y / 5.f / h};
        m.smoothPoint = smoother_.filter(m.rawPoint, f.arrivalUs);
        m.smoothMs = sw.lapMs();
        const float aspect = h / static_cast<float>(f.width);
        cursor = mapper_.toScreen({m.smoothPoint.x * aspect, m.smoothPoint.y});
        m.rawCursor = mapper_.toScreen({m.rawPoint.x * aspect, m.rawPoint.y});
        m.smoothCursor = cursor;
    } else {
        smoother_.reset();
    }

    Stopwatch sc;
    EngineOutput eo = engine_.update({f.arrivalUs, hand.valid, probs, cursor});
    if (!dryRun_) injector_.apply(eo.actions);
    m.controlMs = sc.lapMs();
    const int64_t t1 = Clock::nowUs();
    m.totalMs = static_cast<double>(t1 - t0) / 1000.0;
    m.latencyMs = static_cast<double>(t1 - f.arrivalUs) / 1000.0;
    m.cursor = engine_.cursor();
    m.pose = engine_.pose();

    recorder_.onFrame(t1, hand.valid ? &hand.lm : nullptr);

    std::lock_guard<std::mutex> lock(snapMutex_);
    snap_.frame = f;
    snap_.newFrame = true;
    snap_.hand = hand;
    snap_.probs = engine_.smoothedProbs();
    snap_.pose = engine_.pose();
    snap_.enabled = engine_.enabled();
    snap_.frozen = engine_.frozen();
    snap_.fistProgress = eo.fistProgress;
    snap_.counters = engine_.counters();
    if (snap_.metrics.size() < kMaxQueuedMetrics) snap_.metrics.push_back(m);
    snap_.recording = recorder_.active();
    snap_.recordingWaiting = recorder_.waiting(t1);
    snap_.recordingSecondsLeft = recorder_.secondsLeft(t1);
    snap_.recordedSamples = recorder_.samples();
    snap_.recordingPath = toUtf8(recorder_.path());
}

}  // namespace mausely
