#include "recorder/recorder.h"

#include <windows.h>

#include <ctime>
#include <cwchar>

#include "core/log.h"

namespace mausely {

bool Recorder::start(const std::wstring& folder, Pose label, int64_t nowUs, int64_t delayUs, int64_t durationUs,
                     std::string* error) {
    stop();
    CreateDirectoryW(folder.c_str(), nullptr);
    std::time_t t = std::time(nullptr);
    wchar_t stamp[32];
    std::wcsftime(stamp, 32, L"%Y%m%d_%H%M%S", std::localtime(&t));
    path_ = folder + toWide(poseName(label)) + L"_" + stamp + L".csv";
    file_ = _wfopen(path_.c_str(), L"w");
    if (!file_) {
        if (error) *error = "Cannot create " + toUtf8(path_);
        return false;
    }
    std::fprintf(file_, "label,timestamp_us,handedness");
    for (char kind : {'w', 'i'})
        for (int i = 0; i < kNumLandmarks; ++i) std::fprintf(file_, ",%c%dx,%c%dy,%c%dz", kind, i, kind, i, kind, i);
    std::fprintf(file_, "\n");
    label_ = label;
    startUs_ = nowUs + delayUs;
    endUs_ = startUs_ + durationUs;
    samples_ = 0;
    return true;
}

void Recorder::stop() {
    if (!file_) return;
    std::fclose(file_);
    file_ = nullptr;
    logInfo("recorded " + std::to_string(samples_) + " samples to " + toUtf8(path_));
}

double Recorder::secondsLeft(int64_t nowUs) const {
    if (!active()) return 0.0;
    return static_cast<double>((nowUs < startUs_ ? startUs_ : endUs_) - nowUs) * 1e-6;
}

void Recorder::onFrame(int64_t nowUs, const HandLandmarks* hand) {
    if (!file_) return;
    if (nowUs >= endUs_) {
        stop();
        return;
    }
    if (nowUs < startUs_ || !hand) return;
    std::fprintf(file_, "%s,%lld,%.4f", poseName(label_), static_cast<long long>(nowUs), hand->handedness);
    for (const Vec3& p : hand->world) std::fprintf(file_, ",%.6f,%.6f,%.6f", p.x, p.y, p.z);
    for (const Vec3& p : hand->image) std::fprintf(file_, ",%.2f,%.2f,%.2f", p.x, p.y, p.z);
    std::fprintf(file_, "\n");
    ++samples_;
}

}  // namespace mausely
