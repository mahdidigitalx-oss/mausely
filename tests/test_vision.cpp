#include <doctest/doctest.h>

#include <cmath>
#include <string>

#include "capture/image_source.h"
#include "core/log.h"
#include "vision/hand_tracker.h"
#include "vision/palm_detector.h"

using namespace mausely;

namespace {

std::wstring testImage(const char* name) { return toWide(std::string(MAUSELY_TEST_IMAGE_DIR) + "/" + name); }
std::wstring model(const wchar_t* name) { return exeDirectory() + L"models/" + name; }

HandTracker& tracker() {
    static HandTracker t;
    static bool loaded = [] {
        std::string err;
        bool ok = t.load(model(L"palm_detection_mediapipe_2023feb.onnx"),
                         model(L"handpose_estimation_mediapipe_2023feb.onnx"), 2, &err);
        if (!ok) MESSAGE(err);
        return ok;
    }();
    REQUIRE(loaded);
    return t;
}

}  // namespace

TEST_CASE("palm anchors match the MediaPipe layout") {
    auto a = generatePalmAnchors();
    REQUIRE(a.size() == 2016);
    CHECK(a[0].x == doctest::Approx(0.5f / 24));
    CHECK(a[1].x == doctest::Approx(0.5f / 24));
    CHECK(a[2].x == doctest::Approx(1.5f / 24));
    CHECK(a[1152].x == doctest::Approx(0.5f / 12));
    CHECK(a[2015].y == doctest::Approx(11.5f / 12));
}

TEST_CASE("weighted NMS merges overlapping boxes and keeps separate ones") {
    PalmDetection a{0.9f, 0, 0, 10, 10, {}};
    PalmDetection b{0.6f, 1, 1, 11, 11, {}};
    PalmDetection c{0.8f, 50, 50, 60, 60, {}};
    auto r = weightedNms({a, b, c}, 0.3f);
    REQUIRE(r.size() == 2);
    CHECK(r[0].score == doctest::Approx(0.9f));
    CHECK(r[0].x1 == doctest::Approx(0.4f));  // (0*0.9 + 1*0.6) / 1.5
    CHECK(r[1].x1 == doctest::Approx(50.f));
}

TEST_CASE("palmToRoi points the crop up the hand") {
    PalmDetection p{0.9f, 90, 90, 110, 110, {}};
    p.kp[0] = {100, 110};  // wrist below
    p.kp[2] = {100, 90};   // middle MCP above
    RotatedRect r = palmToRoi(p);
    CHECK(r.angle == doctest::Approx(0.f));
    CHECK(r.cy < 100.f);  // shifted towards the fingers
    CHECK(r.size == doctest::Approx(52.f));

    p.kp[0] = {90, 100};  // hand pointing right
    p.kp[2] = {110, 100};
    CHECK(palmToRoi(p).angle == doctest::Approx(kPi / 2));
}

TEST_CASE("tracker finds a hand in every test photo") {
    const char* images[] = {"fist.jpg", "pointing_up.jpg", "pointing_up_rotated.jpg", "victory.jpg",
                            "thumb_up.jpg", "right_hands.jpg", "left_hands.jpg"};
    for (const char* name : images) {
        CAPTURE(name);
        Frame f;
        std::string err;
        REQUIRE(loadImageFile(testImage(name), f, &err));
        HandTracker& t = tracker();
        t.reset();
        TrackerTimes times;
        HandResult r = t.process(f, times);
        REQUIRE(r.valid);
        CHECK(times.ranPalm);
        CHECK(r.lm.presence >= 0.5f);
        for (const Vec3& p : r.lm.image) {
            CHECK(p.x > -0.05f * f.width);
            CHECK(p.x < 1.05f * f.width);
            CHECK(p.y > -0.05f * f.height);
            CHECK(p.y < 1.05f * f.height);
        }
        // World landmarks are metric: a palm is 5-15 cm from wrist to middle MCP.
        float palm = length(r.lm.world[kMiddleMcp] - r.lm.world[kWrist]);
        CHECK(palm > 0.04f);
        CHECK(palm < 0.15f);

        // Second frame: tracked from landmarks, no palm detection, same answer.
        HandResult r2 = t.process(f, times);
        REQUIRE(r2.valid);
        CHECK(r2.tracked);
        CHECK_FALSE(times.ranPalm);
        float drift = length(Vec2{r2.lm.image[kIndexTip].x - r.lm.image[kIndexTip].x,
                                  r2.lm.image[kIndexTip].y - r.lm.image[kIndexTip].y});
        CHECK(drift < 0.05f * f.height);
    }
}

TEST_CASE("pointing_up: index tip is the highest landmark") {
    Frame f;
    std::string err;
    REQUIRE(loadImageFile(testImage("pointing_up.jpg"), f, &err));
    HandTracker& t = tracker();
    t.reset();
    TrackerTimes times;
    HandResult r = t.process(f, times);
    REQUIRE(r.valid);
    for (int i = 0; i < kNumLandmarks; ++i) CHECK(r.lm.image[kIndexTip].y <= r.lm.image[i].y + 1e-3f);
}

TEST_CASE("a model can be reloaded without duplicating its tensors") {
    OrtModel m;
    std::string err;
    REQUIRE_MESSAGE(m.load(model(L"handpose_estimation_mediapipe_2023feb.onnx"), 1, &err), err);
    REQUIRE_MESSAGE(m.load(model(L"handpose_estimation_mediapipe_2023feb.onnx"), 1, &err), err);
    CHECK(m.inputs().size() == 1);
    CHECK(m.outputs().size() == 4);
    std::vector<float> zeros(224 * 224 * 3, 0.f);
    std::vector<Tensor> out;
    CHECK(m.run({zeros.data()}, {{1, 224, 224, 3}}, out, &err));
    CHECK(out.size() == 4);
    CHECK_FALSE(m.load(L"C:/does/not/exist.onnx", 1, &err));
    CHECK_FALSE(m.loaded());
}
