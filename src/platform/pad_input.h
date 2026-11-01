#pragma once

// Gamepad bindings by SDL's names: a button ("a", "rightshoulder", ...) or a
// trigger ("lefttrigger", "righttrigger"). SDL reports the triggers as axes;
// here they count as held past half way. Mega Drive style pads such as the
// 8BitDo M30 report their C and Z buttons as the triggers (issue #113), so
// the game's buttons take them as well as the host's functions.

#include <SDL3/SDL.h>

#include <cstdint>
#include <map>
#include <string>

namespace chaotix::pad_input {

// Past this a trigger is pressed; below kTriggerReleased it is let go again
// (the gap keeps a trigger resting near half way from flickering).
constexpr int kTriggerPressed = 16384;
constexpr int kTriggerReleased = 8192;

// The trigger a name stands for, or SDL_GAMEPAD_AXIS_INVALID.
SDL_GamepadAxis trigger_axis(const std::string& name);

// Whether the button or trigger with this name is down on the pad.
bool held(SDL_Gamepad* g, const std::string& name);

// The Mega Drive pad the game sees, from the player's bindings (Mega Drive
// button name -> SDL name). The left stick always steers as well.
uint16_t mega_drive_buttons(SDL_Gamepad* g, const std::map<std::string, std::string>& binds);

} // namespace chaotix::pad_input
