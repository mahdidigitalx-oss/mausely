#include <doctest/doctest.h>

#include <cmath>
#include <string>

#include "ai/features.h"
#include "ai/gesture_classifier.h"
#include "capture/image_source.h"
#include "core/log.h"
#include "test_util.h"
#include "vision/hand_tracker.h"

using namespace mausely;

namespace {

std::wstring model(const wchar_t* name) { return exeDirectory() + L"models/" + name; }

}  // namespace

TEST_CASE("C++ features match the Python reference implementation") {
    // Each vector: 63 world coordinates, 81 features, 5 probabilities.
    auto v = test::readNumbers(test::dataFile("feature_vectors.json"));
    REQUIRE(v.size() > 0);
    REQUIRE(v.size() % 149 == 0);
    GestureClassifier clf;
    std::string err;
    REQUIRE_MESSAGE(clf.load(model(L"gesture_mlp.onnx"), &err), err);

    for (size_t off = 0; off < v.size(); off += 149) {
        std::array<Vec3, kNumLandmarks> world;
        for (int i = 0; i < kNumLandmarks; ++i)
            world[i] = {static_cast<float>(v[off + i * 3]), static_cast<float>(v[off + i * 3 + 1]),
                        static_cast<float>(v[off + i * 3 + 2])};
        FeatureVector f = computeFeatures(world);
        for (int k = 0; k < kNumFeatures; ++k) {
            CAPTURE(k);
            CHECK(f[k] == doctest::Approx(v[off + 63 + k]).epsilon(1e-3).scale(1.0));
        }
        PoseProbs p;
        REQUIRE(clf.classify(f, p));
        for (int k = 0; k < kNumPoses; ++k) CHECK(p[k] == doctest::Approx(v[off + 144 + k]).epsilon(1e-3).scale(1.0));
    }
}

TEST_CASE("features are invariant to rotation, scale and mirroring") {
    auto v = test::readNumbers(test::dataFile("feature_vectors.json"));
    REQUIRE(v.size() >= 149);
    std::array<Vec3, kNumLandmarks> a, b;
    const float c = std::cos(0.7f), s = std::sin(0.7f);
    for (int i = 0; i < kNumLandmarks; ++i) {
        a[i] = {static_cast<float>(v[i * 3]), static_cast<float>(v[i * 3 + 1]), static_cast<float>(v[i * 3 + 2])};
        // rotate around z, scale x3, mirror x, translate
        Vec3 r{c * a[i].x - s * a[i].y, s * a[i].x + c * a[i].y, a[i].z};
        b[i] = {-3.f * r.x + 1.f, 3.f * r.y - 2.f, 3.f * r.z + 0.5f};
    }
    FeatureVector fa = computeFeatures(a), fb = computeFeatures(b);
    for (int k = 0; k < kNumFeatures; ++k) CHECK(fa[k] == doctest::Approx(fb[k]).epsilon(1e-3).scale(1.0));
}

TEST_CASE("gesture AI recognises the poses in real photos") {
    struct Case {
        const char* image;
        Pose expected;
    };
    const Case cases[] = {{"fist.jpg", Pose::Fist},          {"victory.jpg", Pose::Scroll},
                          {"pointing_up.jpg", Pose::Move},   {"thumb_up.jpg", Pose::Move},
                          {"right_hands.jpg", Pose::Move}};
    HandTracker tracker;
    GestureClassifier clf;
    std::string err;
    REQUIRE_MESSAGE(tracker.load(model(L"palm_detection_mediapipe_2023feb.onnx"),
                                 model(L"handpose_estimation_mediapipe_2023feb.onnx"), 2, &err), err);
    REQUIRE_MESSAGE(clf.load(model(L"gesture_mlp.onnx"), &err), err);
    for (const Case& tc : cases) {
        CAPTURE(tc.image);
        Frame f;
        REQUIRE(loadImageFile(toWide(std::string(MAUSELY_TEST_IMAGE_DIR) + "/" + tc.image), f, &err));
        tracker.reset();
        TrackerTimes t;
        HandResult r = tracker.process(f, t);
        REQUIRE(r.valid);
        PoseProbs p;
        REQUIRE(clf.classify(computeFeatures(r.lm.world), p));
        int best = 0;
        for (int k = 1; k < kNumPoses; ++k)
            if (p[k] > p[best]) best = k;
        CHECK(static_cast<Pose>(best) == tc.expected);
    }
}
