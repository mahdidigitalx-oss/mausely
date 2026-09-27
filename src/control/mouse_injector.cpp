#include "control/mouse_injector.h"

#include <windows.h>

#include <algorithm>
#include <vector>

namespace mausely {

ScreenRect virtualDesktop() {
    ScreenRect r;
    r.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    r.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    r.width = std::max(1, GetSystemMetrics(SM_CXVIRTUALSCREEN));
    r.height = std::max(1, GetSystemMetrics(SM_CYVIRTUALSCREEN));
    return r;
}

int64_t doubleClickTimeUs() { return static_cast<int64_t>(GetDoubleClickTime()) * 1000; }

void MouseInjector::apply(const MouseActions& actions) {
    if (actions.empty()) return;
    const ScreenRect vd = virtualDesktop();
    std::vector<INPUT> inputs;
    inputs.reserve(actions.size());
    for (const MouseAction& a : actions) {
        INPUT in{};
        in.type = INPUT_MOUSE;
        switch (a.type) {
            case MouseActionType::Move:
                in.mi.dx = static_cast<LONG>((static_cast<long long>(a.x - vd.left) * 65535 + (vd.width - 1) / 2) /
                                             std::max(1, vd.width - 1));
                in.mi.dy = static_cast<LONG>((static_cast<long long>(a.y - vd.top) * 65535 + (vd.height - 1) / 2) /
                                             std::max(1, vd.height - 1));
                in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
                break;
            case MouseActionType::LeftDown:
                in.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
                left_ = true;
                break;
            case MouseActionType::LeftUp:
                in.mi.dwFlags = MOUSEEVENTF_LEFTUP;
                left_ = false;
                break;
            case MouseActionType::RightDown:
                in.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
                right_ = true;
                break;
            case MouseActionType::RightUp:
                in.mi.dwFlags = MOUSEEVENTF_RIGHTUP;
                right_ = false;
                break;
            case MouseActionType::Wheel:
                in.mi.dwFlags = MOUSEEVENTF_WHEEL;
                in.mi.mouseData = static_cast<DWORD>(a.amount);
                break;
            case MouseActionType::HWheel:
                in.mi.dwFlags = MOUSEEVENTF_HWHEEL;
                in.mi.mouseData = static_cast<DWORD>(a.amount);
                break;
        }
        inputs.push_back(in);
    }
    SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
}

void MouseInjector::releaseAll() {
    MouseActions a;
    if (left_) a.push_back({MouseActionType::LeftUp});
    if (right_) a.push_back({MouseActionType::RightUp});
    apply(a);
}

}  // namespace mausely
