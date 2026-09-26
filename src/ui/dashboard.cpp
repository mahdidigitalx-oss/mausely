#include "ui/dashboard.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <implot.h>

#include "core/clock.h"
#include "core/log.h"

namespace mausely {
namespace {

const ImVec4 kGood(0.30f, 0.80f, 0.45f, 1.f);
const ImVec4 kWarn(0.95f, 0.70f, 0.25f, 1.f);
const ImVec4 kBad(0.95f, 0.35f, 0.35f, 1.f);
const ImVec4 kMuted(0.60f, 0.63f, 0.68f, 1.f);
const ImU32 kAccent = IM_COL32(61, 158, 245, 255);

constexpr int kBones[][2] = {{0, 1},   {1, 2},   {2, 3},   {3, 4},   {0, 5},   {5, 6},   {6, 7},
                             {7, 8},   {5, 9},   {9, 10},  {10, 11}, {11, 12}, {9, 13},  {13, 14},
                             {14, 15}, {15, 16}, {13, 17}, {17, 18}, {18, 19}, {19, 20}, {0, 17}};

const char* kPoseLabels[] = {"Move", "Left click / drag", "Right click", "Scroll", "Fist (toggle)"};

// Shows paths inside the roaming profile as %APPDATA%\... (shorter, and no user name).
std::string displayPath(const std::string& path) {
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return path;
    const std::string root = toUtf8(std::wstring(buf, n));
    if (path.size() >= root.size() && _strnicmp(path.c_str(), root.c_str(), root.size()) == 0)
        return "%APPDATA%" + path.substr(root.size());
    return path;
}

void pushHistory(std::vector<float>& v, float x, size_t cap) {
    if (v.size() >= cap) v.erase(v.begin());
    v.push_back(x);
}

// Small "metric card": big number + caption.
void kpi(const char* caption, const char* value, const ImVec4& color, float width) {
    ImGui::BeginGroup();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.13f, 0.14f, 0.17f, 1.f));
    ImGui::BeginChild(caption, ImVec2(width, ImGui::GetTextLineHeight() * 3.3f), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar);
    ImGui::SetCursorPos(ImVec2(ImGui::GetStyle().ItemSpacing.x, ImGui::GetStyle().ItemSpacing.y));
    ImGui::TextColored(kMuted, "%s", caption);
    ImGui::SetCursorPosX(ImGui::GetStyle().ItemSpacing.x);
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.45f);
    ImGui::TextColored(color, "%s", value);
    ImGui::PopFont();
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::EndGroup();
}

}  // namespace

Dashboard::Dashboard(Pipeline& pipeline, Settings& settings, std::wstring settingsPath, std::wstring recordingsDir,
                     std::function<void()> restartPipeline)
    : pipeline_(pipeline),
      settings_(settings),
      settingsPath_(std::move(settingsPath)),
      recordingsDir_(std::move(recordingsDir)),
      restart_(std::move(restartPipeline)) {
    const char* names[kStageCount] = {"Frame wait", "Palm detection", "Hand landmarks", "Gesture AI",
                                      "Smoothing",  "Gesture logic + input", "Pipeline total", "Camera -> cursor"};
    for (const char* n : names) stages_.push_back({n, RollingStats(300)});
}

void Dashboard::ingest(const FrameMetrics& m) {
    ++processed_;
    if (lastArrivalUs_ && m.arrivalUs > lastArrivalUs_)
        frameInterval_.push(static_cast<double>(m.arrivalUs - lastArrivalUs_) / 1000.0);
    lastArrivalUs_ = m.arrivalUs;

    stages_[kQueue].stats.push(m.queueMs);
    if (m.ranPalm) stages_[kPalm].stats.push(m.palmMs);
    if (m.landmarkMs > 0) stages_[kLandmark].stats.push(m.landmarkMs - (m.ranPalm ? 0.0 : 0.0));
    if (m.hand) {
        stages_[kClassify].stats.push(m.classifyMs);
        stages_[kSmooth].stats.push(m.smoothMs);
    }
    stages_[kControl].stats.push(m.controlMs);
    stages_[kTotal].stats.push(m.totalMs);
    stages_[kLatency].stats.push(m.latencyMs);
    handRate_.push(m.hand ? 1.0 : 0.0);

    pushHistory(histTotal_, static_cast<float>(m.totalMs), kHistory);
    pushHistory(histLandmark_, static_cast<float>(m.landmarkMs), kHistory);
    pushHistory(histPalm_, static_cast<float>(m.palmMs), kHistory);
    pushHistory(histLatency_, static_cast<float>(m.latencyMs), kHistory);

    if (!m.hand) {
        recent_.clear();
        return;
    }
    pushHistory(histRawX_, m.rawCursor.x, 150);
    pushHistory(histSmoothX_, m.smoothCursor.x, 150);
    pushHistory(histRawY_, m.rawCursor.y, 150);
    pushHistory(histSmoothY_, m.smoothCursor.y, 150);

    // Jitter while the hand is (nearly) still: RMS frame-to-frame motion over
    // the last 15 frames when the raw point stayed within 15 px.
    recent_.push_back(m);
    if (recent_.size() > 15) recent_.erase(recent_.begin());
    if (recent_.size() == 15 && length(recent_.back().rawCursor - recent_.front().rawCursor) < 15.f) {
        double r = 0, s = 0;
        for (size_t i = 1; i < recent_.size(); ++i) {
            float dr = length(recent_[i].rawCursor - recent_[i - 1].rawCursor);
            float ds = length(recent_[i].smoothCursor - recent_[i - 1].smoothCursor);
            r += dr * dr;
            s += ds * ds;
        }
        jitterRaw_.push(std::sqrt(r / 14.0));
        jitterSmooth_.push(std::sqrt(s / 14.0));
    }
}

void Dashboard::updateCpu() {
    int64_t now = Clock::nowUs();
    if (now - cpuLastWallUs_ < 1'000'000) return;
    FILETIME c, e, k, u;
    if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) return;
    auto toUs = [](FILETIME f) { return ((static_cast<uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime) / 10; };
    uint64_t proc = toUs(k) + toUs(u);
    if (cpuLastWallUs_) {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        double wall = static_cast<double>(now - cpuLastWallUs_) * si.dwNumberOfProcessors;
        cpuPercent_ = 100.0 * static_cast<double>(proc - cpuLastProcess_) / wall;
    }
    cpuLastWallUs_ = now;
    cpuLastProcess_ = proc;
}

void Dashboard::update(D3dWindow& win) {
    pipeline_.snapshot(snap_);
    for (const FrameMetrics& m : snap_.metrics) ingest(m);
    if (snap_.newFrame && !snap_.frame.empty())
        camTex_.update(win.device(), win.context(), snap_.frame.bgra.data(), snap_.frame.width, snap_.frame.height);
    updateCpu();
    if (saveDirty_ && Clock::nowUs() - lastSaveRequestUs_ > 500'000) {
        saveSettings(settingsPath_, settings_);
        saveDirty_ = false;
    }
}

void Dashboard::settingsChanged() {
    pipeline_.updateSettings(settings_);
    saveDirty_ = true;
    lastSaveRequestUs_ = Clock::nowUs();
}

void Dashboard::draw(D3dWindow&) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("##main", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    drawHeader();

    const float avail = ImGui::GetContentRegionAvail().x;
    const float leftW = std::floor(avail * 0.56f);
    ImGui::BeginChild("left", ImVec2(leftW, 0), ImGuiChildFlags_None);
    drawCamera(ImGui::GetContentRegionAvail());
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("right", ImVec2(0, 0), ImGuiChildFlags_None);
    if (ImGui::BeginTabBar("tabs")) {
        using Draw = void (Dashboard::*)();
        const std::pair<const char*, Draw> tabs[] = {
            {"Performance", &Dashboard::drawPerformance}, {"Smoothing", &Dashboard::drawSmoothing},
            {"Gestures", &Dashboard::drawGestures},       {"Settings", &Dashboard::drawSettings},
            {"Recorder", &Dashboard::drawRecorder}};
        for (int i = 0; i < 5; ++i) {
            ImGuiTabItemFlags flags = i == selectTab_ ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem(tabs[i].first, nullptr, flags)) {
                (this->*tabs[i].second)();
                ImGui::EndTabItem();
            }
        }
        selectTab_ = -1;
        ImGui::EndTabBar();
    }
    ImGui::EndChild();
    ImGui::End();
}

void Dashboard::drawHeader() {
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.35f);
    ImGui::TextUnformatted("Mausely");
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(kMuted, " hand-gesture mouse");
    ImGui::SameLine(0, 24);

    const bool on = snap_.enabled;
    ImGui::PushStyleColor(ImGuiCol_Button, on ? ImVec4(0.16f, 0.52f, 0.30f, 1.f) : ImVec4(0.30f, 0.31f, 0.35f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, on ? ImVec4(0.20f, 0.62f, 0.36f, 1.f) : ImVec4(0.36f, 0.37f, 0.42f, 1.f));
    if (ImGui::Button(on ? "  CONTROL ON  " : "  PAUSED  ")) pipeline_.toggleEnabled();
    ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Toggle mouse control.\nShortcut: Ctrl+Alt+M, or hold a fist for 1 second.");
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(kMuted, "Ctrl+Alt+M");

    ImGui::SameLine(0, 24);
    ImGui::AlignTextToFramePadding();
    if (!snap_.cameraStatus.empty())
        ImGui::TextColored(kBad, "Camera: %s", snap_.cameraStatus.c_str());
    else if (!snap_.cameraName.empty())
        ImGui::TextColored(kMuted, "%s", snap_.cameraName.c_str());
    else
        ImGui::TextColored(kMuted, "Opening camera...");
    if (!snap_.modelStatus.empty()) ImGui::TextColored(kWarn, "%s", snap_.modelStatus.c_str());
    ImGui::Separator();
}

void Dashboard::drawCamera(const ImVec2& area) {
    const float infoH = ImGui::GetTextLineHeightWithSpacing() * 2.4f;
    ImVec2 size(area.x, area.y - infoH);
    const Frame& f = snap_.frame;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();

    if (f.empty() || !camTex_.srv()) {
        dl->AddRectFilled(p0, ImVec2(p0.x + size.x, p0.y + size.y), IM_COL32(20, 22, 26, 255), 6.f);
        const char* msg = snap_.cameraStatus.empty() ? "Waiting for the camera..." : snap_.cameraStatus.c_str();
        ImVec2 ts = ImGui::CalcTextSize(msg);
        dl->AddText(ImVec2(p0.x + (size.x - ts.x) / 2, p0.y + (size.y - ts.y) / 2), IM_COL32(150, 155, 165, 255), msg);
        ImGui::Dummy(size);
    } else {
        // Fit the frame into the area, keep aspect.
        float scale = std::min(size.x / f.width, size.y / f.height);
        ImVec2 dsz(f.width * scale, f.height * scale);
        ImVec2 o(p0.x + (size.x - dsz.x) / 2, p0.y);
        ImGui::SetCursorScreenPos(o);
        const bool mirror = settings_.mirror;
        ImGui::Image(ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(camTex_.srv()))), dsz,
                     mirror ? ImVec2(1, 0) : ImVec2(0, 0), mirror ? ImVec2(0, 1) : ImVec2(1, 1));
        auto toScreen = [&](float x, float y) {
            float u = x / f.width;
            if (mirror) u = 1.f - u;
            return ImVec2(o.x + u * dsz.x, o.y + y / f.height * dsz.y);
        };
        dl->PushClipRect(o, ImVec2(o.x + dsz.x, o.y + dsz.y), true);

        // Active region (defined in mirrored/display coordinates).
        const ActiveRegion& r = settings_.region;
        dl->AddRect(ImVec2(o.x + r.x0 * dsz.x, o.y + r.y0 * dsz.y), ImVec2(o.x + r.x1 * dsz.x, o.y + r.y1 * dsz.y),
                    IM_COL32(255, 200, 60, 150), 4.f, 0, 1.5f);

        if (snap_.hand.valid) {
            const HandResult& h = snap_.hand;
            // Tracking ROI.
            float c = std::cos(h.roi.angle), s = std::sin(h.roi.angle), hs = h.roi.size / 2;
            ImVec2 q[4];
            const float corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
            for (int i = 0; i < 4; ++i) {
                float lx = corners[i][0] * hs, ly = corners[i][1] * hs;
                q[i] = toScreen(h.roi.cx + c * lx - s * ly, h.roi.cy + s * lx + c * ly);
            }
            dl->AddQuad(q[0], q[1], q[2], q[3], IM_COL32(255, 255, 255, 60), 1.f);
            // Skeleton.
            for (auto& b : kBones)
                dl->AddLine(toScreen(h.lm.image[b[0]].x, h.lm.image[b[0]].y),
                            toScreen(h.lm.image[b[1]].x, h.lm.image[b[1]].y), IM_COL32(90, 230, 140, 220), 2.f);
            for (int i = 0; i < kNumLandmarks; ++i) {
                bool tip = i == 4 || i == 8 || i == 12 || i == 16 || i == 20;
                dl->AddCircleFilled(toScreen(h.lm.image[i].x, h.lm.image[i].y), tip ? 4.5f : 3.f,
                                    tip ? IM_COL32(255, 90, 90, 255) : IM_COL32(240, 240, 255, 255));
            }
            // Control point: raw (grey) and smoothed (accent).
            if (!recent_.empty()) {
                const FrameMetrics& m = recent_.back();
                ImVec2 raw = toScreen(m.rawPoint.x * f.height, m.rawPoint.y * f.height);
                ImVec2 sm = toScreen(m.smoothPoint.x * f.height, m.smoothPoint.y * f.height);
                dl->AddCircle(raw, 7.f, IM_COL32(200, 200, 200, 160), 0, 1.5f);
                dl->AddCircleFilled(sm, 6.f, kAccent);
            }
            // Pose label + fist progress ring.
            ImVec2 wrist = toScreen(h.lm.image[0].x, h.lm.image[0].y);
            const char* label = snap_.pose < Pose::Count ? kPoseLabels[static_cast<int>(snap_.pose)] : "";
            ImVec2 ts = ImGui::CalcTextSize(label);
            ImVec2 lp(wrist.x - ts.x / 2, wrist.y + 10);
            dl->AddRectFilled(ImVec2(lp.x - 6, lp.y - 3), ImVec2(lp.x + ts.x + 6, lp.y + ts.y + 3),
                              IM_COL32(0, 0, 0, 170), 4.f);
            dl->AddText(lp, IM_COL32(255, 255, 255, 255), label);
            if (snap_.fistProgress > 0.f) {
                dl->PathArcTo(wrist, 22.f, -kPi / 2, -kPi / 2 + 2 * kPi * snap_.fistProgress, 32);
                dl->PathStroke(IM_COL32(255, 200, 60, 255), 0, 4.f);
            }
        }
        dl->PopClipRect();

        // Status badge in the corner.
        const char* badge = snap_.enabled ? "CONTROL ON" : "PAUSED";
        ImVec2 bs = ImGui::CalcTextSize(badge);
        ImVec2 bp(o.x + 10, o.y + 10);
        dl->AddRectFilled(bp, ImVec2(bp.x + bs.x + 16, bp.y + bs.y + 8),
                          snap_.enabled ? IM_COL32(40, 150, 80, 220) : IM_COL32(70, 72, 80, 220), 4.f);
        dl->AddText(ImVec2(bp.x + 8, bp.y + 4), IM_COL32(255, 255, 255, 255), badge);
        ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + size.y));
        ImGui::Dummy(ImVec2(0, 0));
    }

    // Info line under the camera.
    if (snap_.hand.valid) {
        ImGui::Text("Hand: %s   presence %.2f   %s", snap_.hand.lm.handedness > 0.5f ? "right" : "left",
                    snap_.hand.lm.presence, snap_.hand.tracked ? "tracking" : "detected");
    } else {
        ImGui::TextColored(kMuted, "No hand in view - show your palm to the camera");
    }
    ImGui::TextColored(kMuted, "Yellow box = area mapped to the whole screen.  Grey ring = raw point, blue = smoothed.");
}

void Dashboard::drawPerformance() {
    const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 3) / 4;
    char buf[64];
    double interval = frameInterval_.mean();
    std::snprintf(buf, sizeof buf, "%.1f", interval > 0 ? 1000.0 / interval : 0.0);
    kpi("Camera FPS", buf, ImVec4(1, 1, 1, 1), w);
    ImGui::SameLine();
    const RollingStats& lat = stages_[kLatency].stats;
    std::snprintf(buf, sizeof buf, "%.1f ms", lat.percentile(50));
    kpi("Latency (p50)", buf, lat.percentile(50) < 33 ? kGood : kWarn, w);
    ImGui::SameLine();
    std::snprintf(buf, sizeof buf, "%.0f %%", cpuPercent_);
    kpi("CPU (this app)", buf, cpuPercent_ < 35 ? kGood : kWarn, w);
    ImGui::SameLine();
    std::snprintf(buf, sizeof buf, "%llu", static_cast<unsigned long long>(snap_.droppedFrames));
    kpi("Dropped frames", buf, snap_.droppedFrames ? kWarn : kGood, w);

    ImGui::Spacing();
    ImGui::TextColored(kMuted, "Per-stage time over the last %zu frames (milliseconds)", stages_[kTotal].stats.count());
    if (ImGui::BeginTable("stages", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Stage", ImGuiTableColumnFlags_WidthStretch, 2.2f);
        for (const char* c : {"last", "avg", "p50", "p95", "max"})
            ImGui::TableSetupColumn(c, ImGuiTableColumnFlags_WidthStretch, 1.f);
        ImGui::TableHeadersRow();
        for (int i = 0; i < kStageCount; ++i) {
            const RollingStats& s = stages_[i].stats;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (i >= kTotal)
                ImGui::TextColored(ImVec4(0.55f, 0.78f, 1.f, 1.f), "%s", stages_[i].name);
            else
                ImGui::TextUnformatted(stages_[i].name);
            if (!s.count()) {
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(kMuted, "-");
                continue;
            }
            double v[5] = {s.last(), s.mean(), s.percentile(50), s.percentile(95), s.max()};
            for (int c = 0; c < 5; ++c) {
                ImGui::TableSetColumnIndex(c + 1);
                ImGui::Text("%.2f", v[c]);
            }
        }
        ImGui::EndTable();
    }
    ImGui::TextColored(kMuted, "Hand visible in %.0f%% of frames.  Palm detection runs only when the hand is lost.",
                       handRate_.mean() * 100.0);
    ImGui::TextColored(kMuted, "ONNX Runtime %s, %d inference threads.", snap_.ortVersion.c_str(),
                       settings_.inferenceThreads);

    if (ImPlot::BeginPlot("##timing", ImVec2(-1, -1))) {
        ImPlot::SetupAxes("frame", "ms", ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisLimits(ImAxis_X1, 0, kHistory, ImPlotCond_Always);
        ImPlot::SetupLegend(ImPlotLocation_NorthWest);
        ImPlotSpec spec;
        spec.LineWeight = 1.5f;
        ImPlot::PlotLine("Camera -> cursor", histLatency_.data(), static_cast<int>(histLatency_.size()), 1.0, 0.0, spec);
        ImPlot::PlotLine("Pipeline total", histTotal_.data(), static_cast<int>(histTotal_.size()), 1.0, 0.0, spec);
        ImPlot::PlotLine("Hand landmarks", histLandmark_.data(), static_cast<int>(histLandmark_.size()), 1.0, 0.0, spec);
        ImPlot::PlotLine("Palm detection", histPalm_.data(), static_cast<int>(histPalm_.size()), 1.0, 0.0, spec);
        ImPlot::EndPlot();
    }
}

void Dashboard::drawSmoothing() {
    ImGui::SeparatorText("Pointer smoothing");
    int mode = static_cast<int>(settings_.smoothing);
    const char* modes[] = {"None (raw)", "One Euro filter (classic)", "AI smoother", "AI smoother + prediction"};
    if (ImGui::Combo("Mode", &mode, modes, IM_ARRAYSIZE(modes))) {
        settings_.smoothing = static_cast<SmoothingMode>(mode);
        settingsChanged();
    }
    if (!snap_.aiSmootherReady) ImGui::TextColored(kWarn, "AI smoother model not found - using One Euro.");
    if (settings_.smoothing == SmoothingMode::AiPredict &&
        ImGui::SliderFloat("Prediction", &settings_.predictStrength, 0.f, 1.f, "%.2f frame"))
        settingsChanged();
    if (settings_.smoothing == SmoothingMode::OneEuro) {
        float mc = static_cast<float>(settings_.oneEuroMinCutoff), beta = static_cast<float>(settings_.oneEuroBeta);
        bool changed = ImGui::SliderFloat("Min cutoff (Hz)", &mc, 0.1f, 5.f, "%.2f");
        changed |= ImGui::SliderFloat("Beta", &beta, 0.f, 150.f, "%.1f");
        if (changed) {
            settings_.oneEuroMinCutoff = mc;
            settings_.oneEuroBeta = beta;
            settingsChanged();
        }
    }

    ImGui::SeparatorText("Jitter while the hand is still (screen pixels, RMS per frame)");
    char buf[64];
    const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2) / 3;
    double raw = jitterRaw_.mean(), sm = jitterSmooth_.mean();
    std::snprintf(buf, sizeof buf, "%.2f px", raw);
    kpi("Raw", buf, ImVec4(1, 1, 1, 1), w);
    ImGui::SameLine();
    std::snprintf(buf, sizeof buf, "%.2f px", sm);
    kpi("Smoothed", buf, kGood, w);
    ImGui::SameLine();
    std::snprintf(buf, sizeof buf, "%.0f %%", raw > 1e-6 ? 100.0 * (1.0 - sm / raw) : 0.0);
    kpi("Reduction", buf, kGood, w);
    ImGui::TextColored(kMuted, "Samples: %zu windows. Hold your hand still to measure; switch modes to compare.",
                       jitterRaw_.count());

    if (ImPlot::BeginPlot("Cursor X (raw vs smoothed)", ImVec2(-1, -1))) {
        ImPlot::SetupAxes("frame", "px", ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisLimits(ImAxis_X1, 0, 150, ImPlotCond_Always);
        ImPlotSpec rawSpec;
        rawSpec.LineColor = ImVec4(0.7f, 0.7f, 0.7f, 0.8f);
        ImPlot::PlotLine("raw", histRawX_.data(), static_cast<int>(histRawX_.size()), 1.0, 0.0, rawSpec);
        ImPlotSpec smSpec;
        smSpec.LineColor = ImVec4(0.24f, 0.62f, 0.96f, 1.f);
        smSpec.LineWeight = 2.f;
        ImPlot::PlotLine("smoothed", histSmoothX_.data(), static_cast<int>(histSmoothX_.size()), 1.0, 0.0, smSpec);
        ImPlot::EndPlot();
    }
}

void Dashboard::drawGestures() {
    ImGui::SeparatorText("Gesture AI output (smoothed probabilities)");
    if (!snap_.classifierReady) ImGui::TextColored(kWarn, "Gesture model not loaded.");
    for (int i = 0; i < kNumPoses; ++i) {
        bool active = snap_.pose == static_cast<Pose>(i);
        char overlay[64];
        std::snprintf(overlay, sizeof overlay, "%s  %.0f%%", kPoseLabels[i], snap_.probs[i] * 100.f);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, active ? ImVec4(0.30f, 0.80f, 0.45f, 1.f) : ImVec4(0.24f, 0.45f, 0.70f, 1.f));
        ImGui::ProgressBar(snap_.hand.valid ? snap_.probs[i] : 0.f, ImVec2(-1, 0), overlay);
        ImGui::PopStyleColor();
    }
    ImGui::Text("Current: %s%s", snap_.pose < Pose::Count ? kPoseLabels[static_cast<int>(snap_.pose)] : "no hand",
                snap_.frozen ? "   (cursor held)" : "");

    ImGui::SeparatorText("Counters");
    const GestureCounters& c = snap_.counters;
    if (ImGui::BeginTable("counters", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        auto row = [](const char* k, long long v) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(k);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%lld", v);
        };
        row("Left clicks", c.leftClicks);
        row("Double clicks", c.doubleClicks);
        row("Right clicks", c.rightClicks);
        row("Drags", c.drags);
        row("Scroll (wheel units, 120 = 1 notch)", static_cast<long long>(c.scrollUnits));
        row("On/off toggles", c.toggles);
        ImGui::EndTable();
    }

    ImGui::SeparatorText("How to use");
    if (ImGui::BeginTable("help", 2, ImGuiTableFlags_RowBg)) {
        const char* rows[][2] = {
            {"Move the cursor", "Open or relaxed hand - the palm centre drives the cursor"},
            {"Left click", "Touch thumb and index fingertips, then release"},
            {"Double click", "Two quick index pinches"},
            {"Drag", "Pinch index, move, release"},
            {"Right click", "Touch thumb and middle fingertips"},
            {"Scroll", "Index + middle fingers up (V), move the hand up/down/sideways"},
            {"Pause / resume", "Hold a fist for 1 second (or Ctrl+Alt+M)"},
        };
        for (auto& r : rows) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(ImVec4(0.55f, 0.78f, 1.f, 1.f), "%s", r[0]);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextWrapped("%s", r[1]);
        }
        ImGui::EndTable();
    }
}

void Dashboard::drawSettings() {
    bool changed = false;
    ImGui::SeparatorText("Screen mapping");
    changed |= ImGui::Checkbox("Mirror camera (selfie view)", &settings_.mirror);
    ImGui::TextColored(kMuted, "Active area of the camera image that spans the whole screen:");
    changed |= ImGui::SliderFloat("Left", &settings_.region.x0, 0.f, 0.45f, "%.2f");
    changed |= ImGui::SliderFloat("Right", &settings_.region.x1, 0.55f, 1.f, "%.2f");
    changed |= ImGui::SliderFloat("Top", &settings_.region.y0, 0.f, 0.45f, "%.2f");
    changed |= ImGui::SliderFloat("Bottom", &settings_.region.y1, 0.55f, 1.f, "%.2f");

    ImGui::SeparatorText("Gestures");
    GestureConfig& g = settings_.gestures;
    float rewindMs = g.clickRewindUs / 1000.f;
    if (ImGui::SliderFloat("Click rewind (ms)", &rewindMs, 0.f, 250.f, "%.0f")) {
        g.clickRewindUs = static_cast<int64_t>(rewindMs * 1000.f);
        changed = true;
    }
    changed |= ImGui::SliderFloat("Drag threshold (px)", &g.dragThresholdPx, 4.f, 60.f, "%.0f");
    changed |= ImGui::SliderFloat("Scroll speed", &g.scrollGain, 0.2f, 6.f, "%.1f");
    changed |= ImGui::SliderFloat("Gesture confidence", &g.enterProb, 0.4f, 0.95f, "%.2f");
    float fistS = g.fistHoldUs / 1e6f;
    if (ImGui::SliderFloat("Fist hold to toggle (s)", &fistS, 0.3f, 3.f, "%.1f")) {
        g.fistHoldUs = static_cast<int64_t>(fistS * 1e6f);
        changed = true;
    }

    ImGui::SeparatorText("Tracking");
    changed |= ImGui::SliderFloat("Hand confidence", &settings_.handConfidence, 0.3f, 0.95f, "%.2f");

    ImGui::SeparatorText("Performance");
    changed |= ImGui::Checkbox("Search for hands less often when idle (saves CPU)", &settings_.idleThrottle);
    if (changed) settingsChanged();

    ImGui::SeparatorText("Camera (applies on restart)");
    if (!camerasListed_) {
        cameras_ = enumerateCameras();
        camerasListed_ = true;
    }
    std::vector<const char*> names;
    for (auto& c : cameras_) names.push_back(c.name.c_str());
    if (names.empty()) names.push_back("(no camera found)");
    ImGui::Combo("Device", &settings_.cameraIndex, names.data(), static_cast<int>(names.size()));
    const char* res[] = {"640 x 480", "1280 x 720"};
    int ri = settings_.captureWidth >= 1280 ? 1 : 0;
    if (ImGui::Combo("Resolution", &ri, res, 2)) {
        settings_.captureWidth = ri ? 1280 : 640;
        settings_.captureHeight = ri ? 720 : 480;
    }
    ImGui::SliderInt("Inference threads", &settings_.inferenceThreads, 1, 4);
    if (ImGui::Button("Apply and restart")) {
        saveSettings(settingsPath_, settings_);
        if (restart_) restart_();
        camerasListed_ = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset to defaults")) {
        settings_ = Settings{};
        settingsChanged();
    }
    ImGui::TextColored(kMuted, "Settings file: %s", displayPath(toUtf8(settingsPath_)).c_str());
}

void Dashboard::drawRecorder() {
    ImGui::TextWrapped(
        "Record your own hand to fine-tune the gesture AI. Pick a gesture, press Record, hold the pose "
        "(move and rotate your hand a little) until the countdown ends. A minute per gesture helps a lot.");
    ImGui::Spacing();
    const char* labels[] = {"MOVE (open / relaxed hand)", "PINCH_INDEX", "PINCH_MIDDLE", "SCROLL (V sign)", "FIST"};
    ImGui::Combo("Gesture", &recordLabel_, labels, IM_ARRAYSIZE(labels));
    ImGui::SliderFloat("Delay (s)", &recordDelay_, 0.f, 10.f, "%.0f");
    ImGui::SliderFloat("Duration (s)", &recordSeconds_, 2.f, 60.f, "%.0f");
    if (!snap_.recording) {
        if (ImGui::Button("Record", ImVec2(160, 0)))
            pipeline_.startRecording(static_cast<Pose>(recordLabel_), recordDelay_, recordSeconds_, recordingsDir_);
    } else {
        if (ImGui::Button("Stop", ImVec2(160, 0))) pipeline_.stopRecording();
        ImGui::SameLine();
        if (snap_.recordingWaiting)
            ImGui::TextColored(kWarn, "Get ready... %.1f s", snap_.recordingSecondsLeft);
        else
            ImGui::TextColored(kBad, "REC %.1f s left  (%d samples)", snap_.recordingSecondsLeft, snap_.recordedSamples);
    }
    if (!snap_.recordingPath.empty())
        ImGui::TextColored(kMuted, "Last file: %s", displayPath(snap_.recordingPath).c_str());

    ImGui::SeparatorText("Then retrain");
    ImGui::TextWrapped("In the repository's training folder run:");
    std::string dir = toUtf8(recordingsDir_);
    while (!dir.empty() && (dir.back() == '\\' || dir.back() == '/')) dir.pop_back();  // "\" would escape the quote
    std::string cmd = "python -m mausely_train.train_gestures --recordings \"" + dir + "\"";
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.78f, 1.f, 1.f));
    ImGui::TextWrapped("python -m mausely_train.train_gestures --recordings \"%s\"", displayPath(dir).c_str());
    ImGui::PopStyleColor();
    if (ImGui::Button("Copy command")) ImGui::SetClipboardText(cmd.c_str());
    ImGui::SameLine();
    if (ImGui::Button("Open recordings folder"))
        ShellExecuteW(nullptr, L"open", recordingsDir_.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    ImGui::TextWrapped("Copy the new models/gesture_mlp.onnx next to Mausely.exe (models folder) and restart.");
}

}  // namespace mausely
