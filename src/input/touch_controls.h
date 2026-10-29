// Virtual on-screen controls for touch devices (Android / iOS, or desktop
// touch screens). Pure layout + hit-testing; the platform layer draws them.
#pragma once
#include <cstdint>
#include <vector>

namespace chaotix {

// Not a Mega Drive button: the on-screen fast-forward button (issue #45). The
// platform layer takes it out before the pad reaches the game.
constexpr uint16_t TOUCH_FAST_FORWARD = 1 << 15;
// Likewise the on-screen rewind button (issue #40), held to step back.
constexpr uint16_t TOUCH_REWIND = 1 << 14;
// And the on-screen menu button (issue #72): it opens the menu as Escape or
// the pad's MENU BUTTON do -- the pause menu in play, with BACK TO TITLE --
// so a phone with no controller can get back to LEVEL SELECT.
constexpr uint16_t TOUCH_MENU = 1 << 13;

struct TouchButton {
    uint16_t mask;       // PAD_* bits produced while held
    float cx, cy, r;     // centre and radius in output pixels
    const char* label;
    bool is_dpad;        // the directions: a d-pad, or the joystick's base
};

class TouchControls {
public:
    // Lays out controls for an output of w x h pixels. With stick set the
    // directions are a joystick rather than a d-pad (issue #30). With
    // sonic_cd, the layout of Sonic CD's 2013 mobile version (issue #34): the
    // directions and one large jump button, with HOLD (B) and CALL (A) small
    // above it -- the only buttons this game uses -- and the whole lower
    // right of the screen jumping where nothing else is. With fast_forward, a
    // small fast-forward button (TOUCH_FAST_FORWARD) in the top-left corner,
    // which both layouts leave empty (issue #45). With rewind, a rewind
    // button (TOUCH_REWIND) there too, left of fast-forward (issue #40). The
    // menu button (TOUCH_MENU) is always there, in that row after them.
    void layout(int w, int h, bool stick = false, bool sonic_cd = false, bool fast_forward = false,
                bool rewind = false);
    // Buttons pressed by a touch at (x, y) in output pixels.
    uint16_t hit(float x, float y) const;
    // A finger put down (down) or moved, by its id; returns what it presses.
    // A finger that lands on the joystick holds it until it is lifted, so it
    // keeps steering when it slides off the base, as a real stick would.
    uint16_t finger(uint64_t id, float x, float y, bool down);
    void lift(uint64_t id);
    const std::vector<TouchButton>& buttons() const { return buttons_; }
    bool stick() const { return stick_; }
    // The knob's offset from the base's centre, at most the base's radius.
    float knob_dx() const { return knob_dx_; }
    float knob_dy() const { return knob_dy_; }
    bool stick_held() const { return stick_held_; }

private:
    uint16_t stick_directions(float dx, float dy) const;
    uint16_t buttons_at(float x, float y) const;  // everything but the directions

    std::vector<TouchButton> buttons_;
    bool stick_ = false;
    bool sonic_cd_ = false;
    float w_ = 0, h_ = 0;
    bool stick_held_ = false;   // a finger has the stick
    uint64_t stick_finger_ = 0;
    float knob_dx_ = 0, knob_dy_ = 0;
};

} // namespace chaotix
