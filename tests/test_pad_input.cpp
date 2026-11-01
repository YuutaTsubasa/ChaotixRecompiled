// Gamepad bindings read from a pad (platform/pad_input.h), on SDL's virtual
// gamepad, so no controller has to be plugged in.
#include "platform/pad_input.h"

#include "input/input.h"
#include "test_framework.h"

#include <map>
#include <string>

using namespace chaotix;

namespace {

// A virtual pad with every standard button and axis, in SDL's order.
struct VirtualPad {
    SDL_JoystickID id = 0;
    SDL_Gamepad* pad = nullptr;
    SDL_Joystick* joy = nullptr;
    VirtualPad() {
        CHECK(SDL_InitSubSystem(SDL_INIT_GAMEPAD));
        SDL_VirtualJoystickDesc desc;
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
        desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        // Button i and axis i are SDL's button i and axis i.
        desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
        desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
        desc.name = "Chaotix test pad";
        id = SDL_AttachVirtualJoystick(&desc);
        CHECK(id != 0);
        pad = SDL_OpenGamepad(id);
        CHECK(pad != nullptr);
        joy = SDL_GetGamepadJoystick(pad);
        // A trigger's axis runs the whole range; at the bottom it is let go.
        trigger(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -32768);
        trigger(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768);
    }
    ~VirtualPad() {
        SDL_CloseGamepad(pad);
        SDL_DetachVirtualJoystick(id);
        SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    }
    void trigger(SDL_GamepadAxis axis, Sint16 raw) {
        SDL_SetJoystickVirtualAxis(joy, axis, raw);
        SDL_UpdateJoysticks();
    }
    void button(SDL_GamepadButton b, bool down) {
        SDL_SetJoystickVirtualButton(joy, b, down);
        SDL_UpdateJoysticks();
    }
};

} // namespace

TEST(pad_input, a_trigger_bound_to_a_game_button_presses_it) {
    // Issue #113: the 8BitDo M30 and Retro-Bit pads report C and Z as the
    // right and left triggers. Bound there, they press C and Z.
    // SDL has no button by these names, which is why reading only buttons
    // never saw them (0.15.15 and before).
    CHECK(SDL_GetGamepadButtonFromString("righttrigger") == SDL_GAMEPAD_BUTTON_INVALID);
    CHECK(SDL_GetGamepadButtonFromString("lefttrigger") == SDL_GAMEPAD_BUTTON_INVALID);
    VirtualPad v;
    const std::map<std::string, std::string> binds = {
        {"a", "x"}, {"b", "a"}, {"c", "righttrigger"}, {"z", "lefttrigger"}};
    CHECK_EQ(int(pad_input::mega_drive_buttons(v.pad, binds)), 0);
    v.trigger(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767);
    CHECK_EQ(int(pad_input::mega_drive_buttons(v.pad, binds)), int(PAD_C));
    v.trigger(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 32767);
    CHECK_EQ(int(pad_input::mega_drive_buttons(v.pad, binds)), int(PAD_C | PAD_Z));
    v.trigger(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768);
    v.trigger(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -32768);
    CHECK_EQ(int(pad_input::mega_drive_buttons(v.pad, binds)), 0);
    // Buttons still read as before (SDL calls the bottom face button "a").
    v.button(SDL_GAMEPAD_BUTTON_SOUTH, true);
    CHECK_EQ(int(pad_input::mega_drive_buttons(v.pad, binds)), int(PAD_B));
    v.button(SDL_GAMEPAD_BUTTON_SOUTH, false);
}

TEST(pad_input, a_trigger_counts_past_half_way) {
    VirtualPad v;
    // Light pressure is not a press: a trigger has to go past half way.
    v.trigger(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -16384);
    CHECK(!pad_input::held(v.pad, "righttrigger"));
    v.trigger(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 16384);
    CHECK(pad_input::held(v.pad, "righttrigger"));
    CHECK(!pad_input::held(v.pad, "lefttrigger"));
    // Names that are neither a button nor a trigger are never held.
    CHECK(!pad_input::held(v.pad, "none"));
    CHECK(!pad_input::held(v.pad, "leftx"));
}
