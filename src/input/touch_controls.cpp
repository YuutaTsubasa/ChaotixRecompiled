#include "touch_controls.h"
#include "input.h"
#include <algorithm>
#include <cmath>

namespace chaotix {

void TouchControls::layout(int w, int h, bool stick, bool sonic_cd, bool fast_forward, bool rewind) {
    buttons_.clear();
    if (stick != stick_ && stick_held_) lift(stick_finger_);
    stick_ = stick;
    sonic_cd_ = sonic_cd;
    w_ = float(w);
    h_ = float(h);
    const float unit = float(std::min(w, h));
    const float pad_r = unit * 0.17f;
    const float btn_r = unit * 0.075f;
    const float margin = unit * 0.05f;
    // D-pad bottom-left
    buttons_.push_back({0, margin + pad_r, float(h) - margin - pad_r, pad_r, stick ? "STICK" : "D-PAD", true});
    const float bx = float(w) - margin - btn_r;
    const float by = float(h) - margin - btn_r;
    // Fast-forward top-left, the size of START: clear of the directions below
    // it, START in the middle, the award counter top-right, and the Sonic CD
    // layout's jump area (the lower right). After the directions, which the
    // joystick expects first.
    // Rewind goes in the corner and fast-forward beside it, as on a tape deck.
    const float fr = btn_r * 0.6f;
    if (rewind) buttons_.push_back({TOUCH_REWIND, margin + fr, margin + fr, fr, "REW", false});
    if (fast_forward) {
        const float fx = margin + fr + (rewind ? fr * 2.8f : 0.0f);
        buttons_.push_back({TOUCH_FAST_FORWARD, fx, margin + fr, fr, "FF", false});
    }
    // The menu button (issue #72) in that row after them, so neither moves;
    // last in the list, after the game's own buttons.
    const TouchButton menu{TOUCH_MENU, margin + fr + fr * 2.8f * float(int(rewind) + int(fast_forward)), margin + fr,
                           fr, "MENU", false};
    if (sonic_cd) {
        const float jump_r = unit * 0.13f;
        const float jx = float(w) - margin - jump_r, jy = float(h) - margin - jump_r;
        buttons_.push_back({PAD_C, jx, jy, jump_r, "C", false});
        buttons_.push_back({PAD_B, jx - jump_r * 1.25f, jy - jump_r * 1.55f, btn_r * 0.75f, "B", false});
        buttons_.push_back({PAD_A, jx + jump_r * 0.15f, jy - jump_r * 1.95f, btn_r * 0.75f, "A", false});
        buttons_.push_back({PAD_START, float(w) * 0.5f, margin + btn_r * 0.6f, btn_r * 0.6f, "START", false});
        buttons_.push_back(menu);
        return;
    }
    // A B C bottom-right in an arc, X Y Z above
    buttons_.push_back({PAD_C, bx, by - btn_r * 0.6f, btn_r, "C", false});
    buttons_.push_back({PAD_B, bx - btn_r * 2.3f, by, btn_r, "B", false});
    buttons_.push_back({PAD_A, bx - btn_r * 4.6f, by + btn_r * 0.2f, btn_r, "A", false});
    buttons_.push_back({PAD_Z, bx, by - btn_r * 3.0f, btn_r * 0.7f, "Z", false});
    buttons_.push_back({PAD_Y, bx - btn_r * 2.3f, by - btn_r * 2.4f, btn_r * 0.7f, "Y", false});
    buttons_.push_back({PAD_X, bx - btn_r * 4.6f, by - btn_r * 2.2f, btn_r * 0.7f, "X", false});
    // Start top-centre
    buttons_.push_back({PAD_START, float(w) * 0.5f, margin + btn_r * 0.6f, btn_r * 0.6f, "START", false});
    buttons_.push_back(menu);
}

uint16_t TouchControls::hit(float x, float y) const {
    uint16_t out = buttons_at(x, y);
    for (const auto& b : buttons_) {
        if (!b.is_dpad) continue;
        const float dx = x - b.cx, dy = y - b.cy;
        const float d = std::sqrt(dx * dx + dy * dy);
        if (d > b.r * 1.3f || d < b.r * 0.15f) continue;
        out |= stick_directions(dx, dy);
    }
    return out;
}

uint16_t TouchControls::buttons_at(float x, float y) const {
    uint16_t out = 0;
    for (const auto& b : buttons_) {
        if (b.is_dpad) continue;
        const float dx = x - b.cx, dy = y - b.cy;
        if (std::sqrt(dx * dx + dy * dy) <= b.r * 1.15f) out |= b.mask;
    }
    // Sonic CD's layout: anywhere else on the lower right jumps.
    if (sonic_cd_ && !out && x > w_ * 0.5f && y > h_ * 0.3f) out = PAD_C;
    return out;
}

// 8-way: directions within +-67.5 degrees of an axis are active.
uint16_t TouchControls::stick_directions(float dx, float dy) const {
    const float d = std::sqrt(dx * dx + dy * dy);
    if (d <= 0) return 0;
    const float ax = dx / d, ay = dy / d;
    uint16_t out = 0;
    if (ax > 0.38f) out |= PAD_RIGHT;
    if (ax < -0.38f) out |= PAD_LEFT;
    if (ay > 0.38f) out |= PAD_DOWN;
    if (ay < -0.38f) out |= PAD_UP;
    return out;
}

uint16_t TouchControls::finger(uint64_t id, float x, float y, bool down) {
    if (!stick_ || buttons_.empty()) return hit(x, y);
    const TouchButton& base = buttons_[0];
    const float dx = x - base.cx, dy = y - base.cy;
    const float d = std::sqrt(dx * dx + dy * dy);
    if (down && !stick_held_ && d <= base.r * 1.3f) { stick_held_ = true; stick_finger_ = id; }
    // Any other finger presses only the buttons, never the stick.
    if (!stick_held_ || id != stick_finger_) return buttons_at(x, y);
    const float k = d > base.r ? base.r / d : 1.0f;
    knob_dx_ = dx * k;
    knob_dy_ = dy * k;
    // A quarter of the way out before it counts, so resting a thumb on the
    // stick does not walk the character.
    return d < base.r * 0.25f ? 0 : stick_directions(dx, dy);
}

void TouchControls::lift(uint64_t id) {
    if (!stick_held_ || id != stick_finger_) return;
    stick_held_ = false;
    knob_dx_ = knob_dy_ = 0;
}

} // namespace chaotix
