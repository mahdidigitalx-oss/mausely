#include <doctest/doctest.h>

#include <vector>

#include "vision/image_ops.h"

using namespace mausely;

namespace {

Frame solidFrame(int w, int h, uint8_t b, uint8_t g, uint8_t r) {
    Frame f;
    f.resize(w, h);
    for (size_t i = 0; i < f.bgra.size(); i += 4) {
        f.bgra[i] = b;
        f.bgra[i + 1] = g;
        f.bgra[i + 2] = r;
        f.bgra[i + 3] = 255;
    }
    return f;
}

}  // namespace

TEST_CASE("letterbox pads a wide frame vertically and converts BGR to RGB") {
    Frame f = solidFrame(1280, 720, 0, 0, 255);  // pure red
    std::vector<float> t(192 * 192 * 3);
    Letterbox lb = letterboxToTensor(f, 192, t.data());
    CHECK(lb.scale == doctest::Approx(0.15f));
    CHECK(lb.padX == doctest::Approx(0.f));
    CHECK(lb.padY == doctest::Approx(42.f));
    // Top padding row is black, the centre is red.
    CHECK(t[0] == 0.f);
    size_t centre = (96 * 192 + 96) * 3;
    CHECK(t[centre] == doctest::Approx(1.f));
    CHECK(t[centre + 2] == doctest::Approx(0.f));
    // Round trip: tensor centre -> image centre.
    Vec2 p = lb.toImage({96.f, 96.f});
    CHECK(p.x == doctest::Approx(640.f));
    CHECK(p.y == doctest::Approx(360.f));
}

TEST_CASE("cropToImage maps crop corners through rotation") {
    RotatedRect r{100.f, 50.f, 40.f, 0.f};
    Vec2 tl = cropToImage(r, 224, {0.f, 0.f});
    CHECK(tl.x == doctest::Approx(80.f));
    CHECK(tl.y == doctest::Approx(30.f));

    // Rotated by +90 degrees the crop's "up" points to image +x.
    r.angle = kPi / 2.f;
    Vec2 top = cropToImage(r, 224, {112.f, 0.f});
    CHECK(top.x == doctest::Approx(120.f));
    CHECK(top.y == doctest::Approx(50.f).epsilon(1e-4));
}

TEST_CASE("cropToTensor samples the right pixels") {
    Frame f = solidFrame(64, 64, 0, 0, 0);
    // One white pixel block at (40..43, 20..23).
    for (int y = 20; y < 24; ++y)
        for (int x = 40; x < 44; ++x)
            for (int c = 0; c < 3; ++c) f.bgra[(static_cast<size_t>(y) * 64 + x) * 4 + c] = 255;
    std::vector<float> t(8 * 8 * 3);
    RotatedRect r{42.f, 22.f, 8.f, 0.f};  // 1:1 crop centred on the block
    cropToTensor(f, r, 8, t.data());
    CHECK(t[(4 * 8 + 4) * 3] == doctest::Approx(1.f));
    CHECK(t[(0 * 8 + 0) * 3] == doctest::Approx(0.f));
}
