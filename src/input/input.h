// Unified input model. Game/runtime code only ever sees InputState; the
// platform layer (keyboard, gamepads, touch) translates into it.
#pragma once
#include <cstdint>

namespace chaotix {

enum PadButton : uint16_t {
    PAD_UP = 1 << 0, PAD_DOWN = 1 << 1, PAD_LEFT = 1 << 2, PAD_RIGHT = 1 << 3,
    PAD_A = 1 << 4, PAD_B = 1 << 5, PAD_C = 1 << 6, PAD_START = 1 << 7,
    PAD_X = 1 << 8, PAD_Y = 1 << 9, PAD_Z = 1 << 10, PAD_MODE = 1 << 11,
};

struct InputState {
    uint16_t pad[2] = {0, 0};   // pressed buttons (active high)
    bool six_button[2] = {true, false};
    bool reset_pressed = false;
};

// Parses a button name ("up", "a", "start", ...). Returns 0 if unknown.
uint16_t pad_button_from_name(const char* name);
const char* pad_button_name(uint16_t button);

} // namespace chaotix
