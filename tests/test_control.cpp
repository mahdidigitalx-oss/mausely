#include <doctest/doctest.h>

#include <algorithm>

#include "control/cursor_mapper.h"
#include "control/gesture_engine.h"

using namespace mausely;

namespace {

PoseProbs probsFor(Pose p) {
    PoseProbs pr{};
    pr.fill(0.01f);
    pr[static_cast<int>(p)] = 0.96f;
    return pr;
}

// Drives the engine at 30 fps with scripted poses and cursor positions.
struct Script {
    GestureEngine engine;
    int64_t t = 0;
    MouseActions all;

    Script() {
        MouseActions tmp;
        engine.setEnabled(true, tmp);
    }

    EngineOutput step(Pose p, Vec2 cursor, bool hand = true) {
        t += 33'333;
        EngineOutput o = engine.update({t, hand, probsFor(p), cursor});
        all.insert(all.end(), o.actions.begin(), o.actions.end());
        return o;
    }
    void run(Pose p, Vec2 cursor, int frames) {
        for (int i = 0; i < frames; ++i) step(p, cursor);
    }
    int count(MouseActionType type) const {
        return static_cast<int>(std::count_if(all.begin(), all.end(), [&](auto& a) { return a.type == type; }));
    }
    const MouseAction* last(MouseActionType type) const {
        for (auto it = all.rbegin(); it != all.rend(); ++it)
            if (it->type == type) return &*it;
        return nullptr;
    }
};

}  // namespace

TEST_CASE("move pose moves the cursor") {
    Script s;
    s.run(Pose::Move, {100, 200}, 3);
    REQUIRE(s.last(MouseActionType::Move));
    CHECK(s.last(MouseActionType::Move)->x == 100);
    CHECK(s.last(MouseActionType::Move)->y == 200);
    CHECK(s.count(MouseActionType::LeftDown) == 0);
}

TEST_CASE("a quick pinch is one left click at the rewound position") {
    Script s;
    s.run(Pose::Move, {100, 100}, 10);
    // During the pinch onset the palm drifts 12 px; the click must not follow it.
    s.run(Pose::Move, {112, 100}, 1);
    s.run(Pose::PinchIndex, {112, 100}, 4);
    s.run(Pose::Move, {112, 100}, 4);
    CHECK(s.count(MouseActionType::LeftDown) == 1);
    CHECK(s.count(MouseActionType::LeftUp) == 1);
    // Down happens at the position from ~100 ms before the onset was recognised.
    auto down = std::find_if(s.all.begin(), s.all.end(), [](auto& a) { return a.type == MouseActionType::LeftDown; });
    REQUIRE(down != s.all.begin());
    auto move = std::prev(down);
    CHECK(move->type == MouseActionType::Move);
    CHECK(move->x == 100);
    CHECK(s.engine.counters().leftClicks == 1);
    CHECK(s.engine.counters().drags == 0);
}

TEST_CASE("cursor is frozen during a pinch until the hand moves past the drag threshold") {
    Script s;
    s.run(Pose::Move, {100, 100}, 10);
    s.run(Pose::PinchIndex, {100, 100}, 3);
    size_t before = s.all.size();
    s.run(Pose::PinchIndex, {110, 100}, 3);  // 10 px < 18 px threshold
    for (size_t i = before; i < s.all.size(); ++i) CHECK(s.all[i].type != MouseActionType::Move);
    s.run(Pose::PinchIndex, {160, 100}, 3);  // drag
    CHECK(s.engine.counters().drags == 1);
    REQUIRE(s.last(MouseActionType::Move));
    CHECK(s.last(MouseActionType::Move)->x == 100);  // drag starts at the click point, then follows
    s.run(Pose::PinchIndex, {200, 100}, 1);
    CHECK(s.last(MouseActionType::Move)->x == 140);
    s.run(Pose::Move, {200, 100}, 3);
    CHECK(s.count(MouseActionType::LeftUp) == 1);
}

TEST_CASE("two quick pinches snap to the same point (double click)") {
    Script s;
    s.run(Pose::Move, {300, 300}, 10);
    s.run(Pose::PinchIndex, {300, 300}, 3);
    s.run(Pose::Move, {306, 303}, 3);
    s.run(Pose::PinchIndex, {306, 303}, 3);
    s.run(Pose::Move, {306, 303}, 3);
    CHECK(s.count(MouseActionType::LeftDown) == 2);
    CHECK(s.engine.counters().doubleClicks == 1);
    // Both presses land at exactly (300, 300).
    int pressesAt300 = 0;
    for (size_t i = 1; i < s.all.size(); ++i)
        if (s.all[i].type == MouseActionType::LeftDown && s.all[i - 1].x == 300 && s.all[i - 1].y == 300)
            ++pressesAt300;
    CHECK(pressesAt300 == 2);
}

TEST_CASE("middle pinch is a right click") {
    Script s;
    s.run(Pose::Move, {50, 50}, 5);
    s.run(Pose::PinchMiddle, {50, 50}, 3);
    s.run(Pose::Move, {50, 50}, 3);
    CHECK(s.count(MouseActionType::RightDown) == 1);
    CHECK(s.count(MouseActionType::RightUp) == 1);
    CHECK(s.count(MouseActionType::LeftDown) == 0);
}

TEST_CASE("scroll pose turns vertical hand motion into wheel units") {
    Script s;
    s.run(Pose::Move, {500, 500}, 5);
    s.run(Pose::Scroll, {500, 500}, 3);
    size_t before = s.all.size();
    for (int i = 1; i <= 10; ++i) s.step(Pose::Scroll, {500, 500.f - 10.f * i});  // hand moves up 100 px
    int wheel = 0;
    for (size_t i = before; i < s.all.size(); ++i) {
        CHECK(s.all[i].type != MouseActionType::Move);
        if (s.all[i].type == MouseActionType::Wheel) wheel += s.all[i].amount;
    }
    CHECK(wheel == 150);  // 100 px * gain 1.5, positive = scroll up
}

TEST_CASE("holding a fist toggles control once") {
    Script s;
    s.run(Pose::Move, {10, 10}, 3);
    bool toggled = false;
    for (int i = 0; i < 40; ++i) toggled |= s.step(Pose::Fist, {10, 10}).toggled;
    CHECK(toggled);
    CHECK_FALSE(s.engine.enabled());
    CHECK(s.engine.counters().toggles == 1);
    // While disabled, pinches do nothing.
    s.run(Pose::Move, {10, 10}, 15);
    s.run(Pose::PinchIndex, {10, 10}, 3);
    CHECK(s.count(MouseActionType::LeftDown) == 0);
    // A second fist turns it back on.
    s.run(Pose::Move, {10, 10}, 15);
    s.run(Pose::Fist, {10, 10}, 40);
    CHECK(s.engine.enabled());
}

TEST_CASE("losing the hand releases a held button") {
    Script s;
    s.run(Pose::Move, {0, 0}, 5);
    s.run(Pose::PinchIndex, {0, 0}, 3);
    CHECK(s.count(MouseActionType::LeftDown) == 1);
    s.step(Pose::Move, {0, 0}, false);
    CHECK(s.count(MouseActionType::LeftUp) == 1);
}

TEST_CASE("a hand entering the frame already pinched does not click") {
    Script s;
    s.run(Pose::PinchIndex, {0, 0}, 5);
    CHECK(s.count(MouseActionType::LeftDown) == 0);
    s.run(Pose::Move, {0, 0}, 5);
    s.run(Pose::PinchIndex, {0, 0}, 3);
    CHECK(s.count(MouseActionType::LeftDown) == 1);
}

TEST_CASE("a hand entering in the scroll pose can scroll right away") {
    Script s;
    s.run(Pose::Scroll, {0, 100}, 3);
    CHECK(s.engine.pose() == Pose::Scroll);
}

TEST_CASE("a single noisy frame does not trigger a click") {
    Script s;
    s.run(Pose::Move, {0, 0}, 5);
    s.step(Pose::PinchIndex, {0, 0});
    s.run(Pose::Move, {0, 0}, 5);
    CHECK(s.count(MouseActionType::LeftDown) == 0);
}

TEST_CASE("cursor mapper mirrors and clamps to the active region") {
    CursorMapper m;
    m.configure({0.2f, 0.2f, 0.8f, 0.8f}, true, {0, 0, 1001, 501});
    Vec2 c = m.toScreen({0.5f, 0.5f});
    CHECK(c.x == doctest::Approx(500.f));
    CHECK(c.y == doctest::Approx(250.f));
    Vec2 left = m.toScreen({0.8f, 0.2f});  // right side of the camera = left of the screen when mirrored
    CHECK(left.x == doctest::Approx(0.f));
    CHECK(left.y == doctest::Approx(0.f));
    Vec2 out = m.toScreen({-1.f, 2.f});
    CHECK(out.x == doctest::Approx(1000.f));
    CHECK(out.y == doctest::Approx(500.f));
}
