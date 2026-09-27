#pragma once

#include <cstdint>

#include "control/cursor_mapper.h"
#include "control/mouse_action.h"

namespace mausely {

// The platform's input backend. Windows: mouse_injector.cpp (SendInput).
// Android: android/app/src/main/cpp/android_injector.cpp (accessibility gestures).

// Screen area the cursor can reach, in physical pixels. Windows: the bounding
// rectangle of all monitors (needs DPI awareness).
ScreenRect virtualDesktop();
// Maximum time between the two presses of a double click / double tap.
int64_t doubleClickTimeUs();

// Sends MouseActions to the system (one batch per frame).
// Note: Windows blocks injected input into elevated (admin) windows.
class MouseInjector {
public:
    void apply(const MouseActions& actions);
    // Releases any button this injector still holds down.
    void releaseAll();

private:
    bool left_ = false;
    bool right_ = false;
};

}  // namespace mausely
