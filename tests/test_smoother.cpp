#include <doctest/doctest.h>

#include <cmath>
#include <random>

#include "ai/ai_smoother.h"
#include "ai/one_euro.h"
#include "ai/smoother.h"
#include "core/log.h"
#include "test_util.h"

using namespace mausely;

namespace {

struct Vectors {
    double fps = 0, minCutoff = 0, beta = 0;
    std::vector<Vec2> observed, oneEuro, aiEst;
};

// Layout written by train_smoother.py: fps, 60 observed pairs, min_cutoff,
// beta, 60 One Euro pairs, 60 AI pairs.
Vectors loadVectors() {
    auto v = test::readNumbers(test::dataFile("smoother_vectors.json"));
    Vectors r;
    if (v.size() != 1 + 120 + 2 + 120 + 120) return r;
    size_t i = 0;
    r.fps = v[i++];
    auto pairs = [&](std::vector<Vec2>& out) {
        for (int k = 0; k < 60; ++k, i += 2) out.push_back({static_cast<float>(v[i]), static_cast<float>(v[i + 1])});
    };
    pairs(r.observed);
    r.minCutoff = v[i++];
    r.beta = v[i++];
    pairs(r.oneEuro);
    pairs(r.aiEst);
    return r;
}

}  // namespace

TEST_CASE("One Euro filter matches the Python reference") {
    Vectors v = loadVectors();
    REQUIRE(v.observed.size() == 60);
    OneEuroFilter f(v.minCutoff, v.beta);
    for (size_t i = 0; i < v.observed.size(); ++i) {
        Vec2 o = f.filter(v.observed[i], 1.0 / v.fps);
        CHECK(o.x == doctest::Approx(v.oneEuro[i].x).epsilon(1e-5));
        CHECK(o.y == doctest::Approx(v.oneEuro[i].y).epsilon(1e-5));
    }
}

TEST_CASE("AI smoother matches the Python reference") {
    Vectors v = loadVectors();
    REQUIRE(v.observed.size() == 60);
    AiSmoother ai;
    std::string err;
    REQUIRE_MESSAGE(ai.load(exeDirectory() + L"models/smoother.onnx", &err), err);
    for (size_t i = 0; i < v.observed.size(); ++i) {
        Vec2 o = ai.filter(v.observed[i], v.fps, 0.f);
        CHECK(o.x == doctest::Approx(v.aiEst[i].x).epsilon(1e-4));
        CHECK(o.y == doctest::Approx(v.aiEst[i].y).epsilon(1e-4));
    }
}

TEST_CASE("smoothers cut jitter on a still, noisy hand") {
    std::mt19937 rng(3);
    std::normal_distribution<float> noise(0.f, 0.002f);  // ~1.4 px at 720p
    Smoother s;
    std::string err;
    bool ai = s.loadAi(exeDirectory() + L"models/smoother.onnx", &err);
    for (SmoothingMode mode : {SmoothingMode::OneEuro, SmoothingMode::Ai}) {
        if (mode == SmoothingMode::Ai && !ai) continue;
        CAPTURE(smoothingModeName(mode));
        s.setMode(mode);
        s.setOneEuroParams(1.0, 20.0);
        s.reset();
        double rawJ = 0, outJ = 0;
        Vec2 prevRaw, prevOut;
        for (int i = 0; i < 200; ++i) {
            Vec2 raw{0.8f + noise(rng), 0.5f + noise(rng)};
            Vec2 out = s.filter(raw, static_cast<int64_t>(i) * 33333);
            if (i > 30) {
                rawJ += std::pow(length(raw - prevRaw), 2);
                outJ += std::pow(length(out - prevOut), 2);
            }
            prevRaw = raw;
            prevOut = out;
        }
        CHECK(outJ < rawJ * 0.25);  // at least 2x less RMS jitter
    }
}

TEST_CASE("smoother falls back to One Euro when the AI model is missing") {
    Smoother s;
    s.setMode(SmoothingMode::Ai);
    CHECK(s.mode() == SmoothingMode::OneEuro);
}
