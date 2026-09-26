#pragma once

#include <vector>

namespace mausely {

enum class MouseActionType { Move, LeftDown, LeftUp, RightDown, RightUp, Wheel, HWheel };

// One input event in virtual-desktop pixels (Move) or wheel units (Wheel/HWheel,
// 120 = one notch; positive = up / right).
struct MouseAction {
    MouseActionType type = MouseActionType::Move;
    int x = 0;
    int y = 0;
    int amount = 0;
};

using MouseActions = std::vector<MouseAction>;

}  // namespace mausely
