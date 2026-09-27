#include "pipeline/settings.h"

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>

#include "core/log.h"

namespace mausely {
namespace {

// One table drives both load and save so they cannot drift apart.
struct Field {
    std::function<std::string(const Settings&)> get;
    std::function<void(Settings&, const std::string&)> set;
};

template <typename T>
Field num(T Settings::*member) {
    return {[member](const Settings& s) { return std::to_string(s.*member); },
            [member](Settings& s, const std::string& v) { s.*member = static_cast<T>(std::atof(v.c_str())); }};
}

template <typename T>
Field nested(T GestureConfig::*member) {
    return {[member](const Settings& s) { return std::to_string(s.gestures.*member); },
            [member](Settings& s, const std::string& v) { s.gestures.*member = static_cast<T>(std::atof(v.c_str())); }};
}

Field region(float ActiveRegion::*member) {
    return {[member](const Settings& s) { return std::to_string(s.region.*member); },
            [member](Settings& s, const std::string& v) { s.region.*member = static_cast<float>(std::atof(v.c_str())); }};
}

Field flag(bool Settings::*member) {
    return {[member](const Settings& s) { return std::string(s.*member ? "1" : "0"); },
            [member](Settings& s, const std::string& v) { s.*member = v == "1" || v == "true"; }};
}

const std::map<std::string, Field>& fields() {
    static const std::map<std::string, Field> f = {
        {"camera_index", num(&Settings::cameraIndex)},
        {"capture_width", num(&Settings::captureWidth)},
        {"capture_height", num(&Settings::captureHeight)},
        {"capture_fps", num(&Settings::captureFps)},
        {"mirror", flag(&Settings::mirror)},
        {"region_x0", region(&ActiveRegion::x0)},
        {"region_y0", region(&ActiveRegion::y0)},
        {"region_x1", region(&ActiveRegion::x1)},
        {"region_y1", region(&ActiveRegion::y1)},
        {"smoothing", {[](const Settings& s) { return std::to_string(static_cast<int>(s.smoothing)); },
                       [](Settings& s, const std::string& v) {
                           int m = std::atoi(v.c_str());
                           if (m >= 0 && m < static_cast<int>(SmoothingMode::Count)) s.smoothing = static_cast<SmoothingMode>(m);
                       }}},
        {"predict_strength", num(&Settings::predictStrength)},
        {"one_euro_min_cutoff", num(&Settings::oneEuroMinCutoff)},
        {"one_euro_beta", num(&Settings::oneEuroBeta)},
        {"inference_threads", num(&Settings::inferenceThreads)},
        {"hand_confidence", num(&Settings::handConfidence)},
        {"idle_throttle", flag(&Settings::idleThrottle)},
        {"click_rewind_us", nested(&GestureConfig::clickRewindUs)},
        {"drag_threshold_px", nested(&GestureConfig::dragThresholdPx)},
        {"double_click_snap_px", nested(&GestureConfig::doubleClickSnapPx)},
        {"scroll_gain", nested(&GestureConfig::scrollGain)},
        {"fist_hold_us", nested(&GestureConfig::fistHoldUs)},
        {"enter_prob", nested(&GestureConfig::enterProb)},
        {"exit_prob", nested(&GestureConfig::exitProb)},
    };
    return f;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

}  // namespace

void parseSettings(const std::string& text, Settings& s) {
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string l = trim(text.substr(pos, end - pos));
        pos = end + 1;
        if (l.empty() || l[0] == '#' || l[0] == ';') continue;
        size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        auto it = fields().find(trim(l.substr(0, eq)));
        if (it != fields().end()) it->second.set(s, trim(l.substr(eq + 1)));
    }
}

std::string formatSettings(const Settings& s) {
    std::string text = "# Mausely settings\n";
    for (const auto& [key, field] : fields()) text += key + "=" + field.get(s) + "\n";
    return text;
}

bool loadSettings(const std::wstring& path, Settings& s) {
    FILE* f = openFile(path, "r");
    if (!f) return false;
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
    std::fclose(f);
    parseSettings(text, s);
    return true;
}

bool saveSettings(const std::wstring& path, const Settings& s) {
    FILE* f = openFile(path, "w");
    if (!f) return false;
    const std::string text = formatSettings(s);
    bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    return std::fclose(f) == 0 && ok;
}

}  // namespace mausely
