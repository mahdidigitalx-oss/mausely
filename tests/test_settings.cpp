#include <doctest/doctest.h>

#include <cstdio>
#include <filesystem>

#include "core/log.h"
#include "pipeline/settings.h"

using namespace mausely;

namespace {

std::wstring tempFile(const wchar_t* name) { return (std::filesystem::temp_directory_path() / name).wstring(); }

void removeFile(const std::wstring& path) {
    std::error_code ec;
    std::filesystem::remove(std::filesystem::path(path), ec);
}

}  // namespace

TEST_CASE("settings round-trip through the file") {
    Settings s;
    s.cameraIndex = 2;
    s.mirror = false;
    s.region = {0.1f, 0.2f, 0.7f, 0.9f};
    s.smoothing = SmoothingMode::AiPredict;
    s.predictStrength = 0.25f;
    s.oneEuroBeta = 42.5;
    s.gestures.scrollGain = 3.0f;
    s.gestures.clickRewindUs = 150000;
    std::wstring path = tempFile(L"mausely_settings_test.ini");
    REQUIRE(saveSettings(path, s));

    Settings r;
    REQUIRE(loadSettings(path, r));
    CHECK(r.cameraIndex == 2);
    CHECK_FALSE(r.mirror);
    CHECK(r.region.x0 == doctest::Approx(0.1f));
    CHECK(r.region.y1 == doctest::Approx(0.9f));
    CHECK(r.smoothing == SmoothingMode::AiPredict);
    CHECK(r.predictStrength == doctest::Approx(0.25f));
    CHECK(r.oneEuroBeta == doctest::Approx(42.5));
    CHECK(r.gestures.scrollGain == doctest::Approx(3.0f));
    CHECK(r.gestures.clickRewindUs == 150000);
    removeFile(path);
}

TEST_CASE("unknown keys and junk lines are ignored") {
    std::wstring path = tempFile(L"mausely_settings_junk.ini");
    FILE* f = openFile(path, "w");
    REQUIRE(f);
    std::fputs("# comment\nnot a setting\nfoo=bar\ncamera_index = 3\nsmoothing=99\n", f);
    std::fclose(f);
    Settings r;
    REQUIRE(loadSettings(path, r));
    CHECK(r.cameraIndex == 3);
    CHECK(r.smoothing == Settings{}.smoothing);  // out of range -> default kept
    removeFile(path);
}

TEST_CASE("missing file keeps defaults") {
    Settings r;
    CHECK_FALSE(loadSettings(tempFile(L"mausely_does_not_exist.ini"), r));
    CHECK(r.captureWidth == 1280);
}

TEST_CASE("settings text round-trips and ignores junk") {
    Settings s;
    s.region.x0 = 0.3f;
    s.idleThrottle = false;
    s.gestures.dragThresholdPx = 25.f;
    Settings r;
    parseSettings(formatSettings(s), r);
    CHECK(r.region.x0 == doctest::Approx(0.3f));
    CHECK_FALSE(r.idleThrottle);
    CHECK(r.gestures.dragThresholdPx == doctest::Approx(25.f));

    parseSettings("scroll_gain = 2.5\r\nno equals sign\nunknown=1\nmirror=0", r);  // no trailing newline
    CHECK(r.gestures.scrollGain == doctest::Approx(2.5f));
    CHECK_FALSE(r.mirror);
    CHECK(r.region.x0 == doctest::Approx(0.3f));  // untouched keys keep their value
}
