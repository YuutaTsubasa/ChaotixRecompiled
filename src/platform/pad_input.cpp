// See pad_input.h.
#include "platform/pad_input.h"

#include "input/input.h"

namespace chaotix::pad_input {

SDL_GamepadAxis trigger_axis(const std::string& name) {
    const SDL_GamepadAxis axis = SDL_GetGamepadAxisFromString(name.c_str());
    return axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER ? axis
                                                                                          : SDL_GAMEPAD_AXIS_INVALID;
}

bool held(SDL_Gamepad* g, const std::string& name) {
    const SDL_GamepadButton b = SDL_GetGamepadButtonFromString(name.c_str());
    if (b != SDL_GAMEPAD_BUTTON_INVALID) return SDL_GetGamepadButton(g, b);
    const SDL_GamepadAxis axis = trigger_axis(name);
    return axis != SDL_GAMEPAD_AXIS_INVALID && SDL_GetGamepadAxis(g, axis) > kTriggerPressed;
}

uint16_t mega_drive_buttons(SDL_Gamepad* g, const std::map<std::string, std::string>& binds) {
    uint16_t out = 0;
    for (const auto& [name, sdl_name] : binds)
        if (held(g, sdl_name)) out |= pad_button_from_name(name.c_str());
    // The sticks always steer, whatever the d-pad is bound to.
    const int dead = 12000;
    const int lx = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX);
    const int ly = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY);
    if (lx < -dead) out |= PAD_LEFT;
    if (lx > dead) out |= PAD_RIGHT;
    if (ly < -dead) out |= PAD_UP;
    if (ly > dead) out |= PAD_DOWN;
    return out;
}

} // namespace chaotix::pad_input
