#pragma once

#include "control/cursor_mapper.h"
#include "control/mouse_action.h"

namespace mausely {

// Sends MouseActions to Windows with SendInput (one batch per frame).
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
