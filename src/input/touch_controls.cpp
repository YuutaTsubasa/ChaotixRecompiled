#include "touch_controls.h"
#include "input.h"
#include <algorithm>
#include <cmath>

namespace chaotix {

void TouchControls::layout(int w, int h) {
    buttons_.clear();
    const float unit = float(std::min(w, h));
    const float pad_r = unit * 0.17f;
    const float btn_r = unit * 0.075f;
    const float margin = unit * 0.05f;
    // D-pad bottom-left
    buttons_.push_back({0, margin + pad_r, float(h) - margin - pad_r, pad_r, "D-PAD", true});
    // A B C bottom-right in an arc, X Y Z above
    const float bx = float(w) - margin - btn_r;
    const float by = float(h) - margin - btn_r;
    buttons_.push_back({PAD_C, bx, by - btn_r * 0.6f, btn_r, "C", false});
    buttons_.push_back({PAD_B, bx - btn_r * 2.3f, by, btn_r, "B", false});
    buttons_.push_back({PAD_A, bx - btn_r * 4.6f, by + btn_r * 0.2f, btn_r, "A", false});
    buttons_.push_back({PAD_Z, bx, by - btn_r * 3.0f, btn_r * 0.7f, "Z", false});
    buttons_.push_back({PAD_Y, bx - btn_r * 2.3f, by - btn_r * 2.4f, btn_r * 0.7f, "Y", false});
    buttons_.push_back({PAD_X, bx - btn_r * 4.6f, by - btn_r * 2.2f, btn_r * 0.7f, "X", false});
    // Start top-centre
    buttons_.push_back({PAD_START, float(w) * 0.5f, margin + btn_r * 0.6f, btn_r * 0.6f, "START", false});
}

uint16_t TouchControls::hit(float x, float y) const {
    uint16_t out = 0;
    for (const auto& b : buttons_) {
        float dx = x - b.cx, dy = y - b.cy;
        float d = std::sqrt(dx * dx + dy * dy);
        if (b.is_dpad) {
            if (d > b.r * 1.3f || d < b.r * 0.15f) continue;
            // 8-way: directions within +-67.5 degrees of an axis are active.
            float ax = dx / d, ay = dy / d;
            if (ax > 0.38f) out |= PAD_RIGHT;
            if (ax < -0.38f) out |= PAD_LEFT;
            if (ay > 0.38f) out |= PAD_DOWN;
            if (ay < -0.38f) out |= PAD_UP;
        } else if (d <= b.r * 1.15f) {
            out |= b.mask;
        }
    }
    return out;
}

} // namespace chaotix
