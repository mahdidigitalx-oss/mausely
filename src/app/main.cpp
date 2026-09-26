// Mausely - control the mouse with hand gestures through the webcam.
//
//   Mausely.exe                      dashboard + camera
//   Mausely.exe --image hand.jpg     use a still image instead of the camera
//   Mausely.exe --benchmark 300      headless: time 300 frames, print statistics, never touch the mouse
//   Mausely.exe --image x.jpg --screenshot shot.bmp   render the dashboard for 4 s, save it, exit

#include <windows.h>
#include <mfapi.h>
#include <shellapi.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>

#include <imgui.h>

#include "capture/image_source.h"
#include "capture/mf_camera.h"
#include "core/clock.h"
#include "core/log.h"
#include "core/rolling_stats.h"
#include "pipeline/pipeline.h"
#include "ui/d3d_window.h"
#include "ui/dashboard.h"
#include "vision/ort_model.h"

using namespace mausely;

namespace {

constexpr int kHotkeyToggle = 1;

struct Options {
    std::wstring image;
    int benchmarkFrames = 0;
    int camera = -1;
    std::wstring screenshot;  // save the dashboard to this .bmp after a few seconds, then exit
    int tab = -1;             // dashboard tab to open first
};

Options parseArgs() {
    Options o;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--image" && i + 1 < argc) o.image = argv[++i];
        else if (a == L"--benchmark" && i + 1 < argc) o.benchmarkFrames = _wtoi(argv[++i]);
        else if (a == L"--camera" && i + 1 < argc) o.camera = _wtoi(argv[++i]);
        else if (a == L"--screenshot" && i + 1 < argc) o.screenshot = argv[++i];
        else if (a == L"--tab" && i + 1 < argc) o.tab = _wtoi(argv[++i]);
    }
    LocalFree(argv);
    return o;
}

// GUI-subsystem programs have no console; reuse the parent's when printing.
void attachConsole() {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h && h != INVALID_HANDLE_VALUE && GetFileType(h) != FILE_TYPE_UNKNOWN) return;  // redirected
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        std::freopen("CONOUT$", "w", stdout);
        std::freopen("CONOUT$", "w", stderr);
    }
}

Pipeline::SourceFactory makeFactory(const Options& opt) {
    return [opt](const Settings& s, std::string* error) -> std::unique_ptr<FrameSource> {
        if (!opt.image.empty()) {
            auto src = std::make_unique<ImageSource>();
            if (!src->open(opt.image, s.captureFps, error)) return nullptr;
            return src;
        }
        auto cam = std::make_unique<MfCamera>();
        if (!cam->open(s.cameraIndex, s.captureWidth, s.captureHeight, s.captureFps, error)) return nullptr;
        return cam;
    };
}

int runBenchmark(Pipeline& pipeline, const Settings& settings, const Options& opt, const std::wstring& modelDir) {
    attachConsole();
    pipeline.setDryRun(true);
    std::string err;
    if (!pipeline.start(settings, modelDir, makeFactory(opt), &err)) {
        std::printf("error: %s\n", err.c_str());
        return 1;
    }
    std::map<std::string, RollingStats> st;
    const char* keys[] = {"frame wait", "palm detection", "hand landmarks", "gesture AI", "smoothing",
                          "gesture logic+input", "pipeline total", "camera->cursor"};
    for (const char* k : keys) st.emplace(k, RollingStats(100000));
    RollingStats interval(100000);
    int frames = 0, hands = 0, poseCount[kNumPoses + 1] = {};
    int64_t last = 0;
    Snapshot snap;
    const int64_t deadline = Clock::nowUs() + 120'000'000;
    while (frames < opt.benchmarkFrames && Clock::nowUs() < deadline) {
        Sleep(50);
        pipeline.snapshot(snap);
        for (const FrameMetrics& m : snap.metrics) {
            if (frames++ < 10) continue;  // warm-up
            if (last) interval.push(static_cast<double>(m.arrivalUs - last) / 1000.0);
            last = m.arrivalUs;
            st["frame wait"].push(m.queueMs);
            if (m.ranPalm) st["palm detection"].push(m.palmMs);
            if (m.landmarkMs > 0) st["hand landmarks"].push(m.landmarkMs);
            if (m.hand) {
                ++hands;
                st["gesture AI"].push(m.classifyMs);
                st["smoothing"].push(m.smoothMs);
            }
            st["gesture logic+input"].push(m.controlMs);
            st["pipeline total"].push(m.totalMs);
            st["camera->cursor"].push(m.latencyMs);
            ++poseCount[static_cast<int>(m.pose)];
        }
        if (!snap.cameraStatus.empty()) std::printf("camera: %s\n", snap.cameraStatus.c_str());
    }
    pipeline.stop();

    int measured = frames > 10 ? frames - 10 : 0;
    std::printf("\nMausely benchmark - %s\n", snap.cameraName.c_str());
    std::printf("ONNX Runtime %s, %d inference threads, %d frames measured\n", snap.ortVersion.c_str(),
                settings.inferenceThreads, measured);
    if (!snap.modelStatus.empty()) std::printf("models: %s\n", snap.modelStatus.c_str());
    std::printf("camera fps: %.1f   hand visible: %.0f%%   dropped: %llu\n",
                interval.count() ? 1000.0 / interval.mean() : 0.0, measured ? 100.0 * hands / measured : 0.0,
                static_cast<unsigned long long>(snap.droppedFrames));
    std::printf("%-22s %8s %8s %8s %8s %8s\n", "stage (ms)", "count", "avg", "p50", "p95", "max");
    for (const char* k : keys) {
        const RollingStats& s = st[k];
        std::printf("%-22s %8zu %8.2f %8.2f %8.2f %8.2f\n", k, s.count(), s.mean(), s.percentile(50),
                    s.percentile(95), s.max());
    }
    std::printf("poses:");
    for (int i = 0; i < kNumPoses; ++i) std::printf(" %s=%d", poseName(static_cast<Pose>(i)), poseCount[i]);
    std::printf(" none=%d\n", poseCount[kNumPoses]);
    std::fflush(stdout);
    return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    Options opt = parseArgs();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);

    const std::wstring dataDir = appDataDirectory();
    logInit(dataDir + L"mausely.log");
    logInfo("Mausely starting");

    std::string err;
    if (!OrtRuntime::instance().init(exeDirectory() + L"onnxruntime.dll", &err)) {
        logError(err);
        if (opt.benchmarkFrames) {
            attachConsole();
            std::printf("error: %s\n", err.c_str());
        } else {
            MessageBoxW(nullptr, toWide(err).c_str(), L"Mausely", MB_ICONERROR);
        }
        return 1;
    }

    const std::wstring settingsPath = dataDir + L"settings.ini";
    Settings settings;
    loadSettings(settingsPath, settings);
    if (opt.camera >= 0) settings.cameraIndex = opt.camera;
    const std::wstring modelDir = exeDirectory() + L"models\\";

    Pipeline pipeline;
    if (opt.benchmarkFrames > 0) {
        int rc = runBenchmark(pipeline, settings, opt, modelDir);
        MFShutdown();
        CoUninitialize();
        return rc;
    }

    D3dWindow win;
    if (!win.create(L"Mausely - hand gesture mouse", 1280, 760, &err)) {
        logError(err);
        MessageBoxW(nullptr, toWide(err).c_str(), L"Mausely", MB_ICONERROR);
        return 1;
    }
    if (!RegisterHotKey(win.hwnd(), kHotkeyToggle, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'M'))
        logError("Ctrl+Alt+M is used by another program");
    win.onHotkey = [&](int id) {
        if (id == kHotkeyToggle) pipeline.toggleEnabled();
    };

    auto startPipeline = [&] {
        std::string e;
        pipeline.start(settings, modelDir, makeFactory(opt), &e);
    };
    startPipeline();
    Dashboard dashboard(pipeline, settings, settingsPath, dataDir + L"recordings\\", [&] {
        pipeline.stop();
        startPipeline();
    });

    if (!opt.screenshot.empty()) {
        pipeline.setDryRun(true);  // never sends input, so showing the "on" state is safe
        pipeline.setEnabled(true);
    }
    if (opt.tab >= 0) dashboard.selectTab(opt.tab);
    const int64_t screenshotAt = Clock::nowUs() + 4000000;
    while (win.pumpMessages()) {
        dashboard.update(win);
        if (!win.beginFrame()) continue;
        dashboard.draw(win);
        const bool shoot = !opt.screenshot.empty() && Clock::nowUs() >= screenshotAt;
        win.endFrame(shoot ? opt.screenshot : std::wstring());
        if (shoot) break;
    }

    UnregisterHotKey(win.hwnd(), kHotkeyToggle);
    pipeline.stop();
    if (opt.screenshot.empty()) saveSettings(settingsPath, settings);  // a screenshot run must not touch the user's settings
    logInfo("Mausely stopped");
    MFShutdown();
    CoUninitialize();
    return 0;
}
