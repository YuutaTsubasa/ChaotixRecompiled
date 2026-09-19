// Virtual on-screen controls for touch devices (Android / iOS, or desktop
// touch screens). Pure layout + hit-testing; the platform layer draws them.
#pragma once
#include <cstdint>
#include <vector>

namespace chaotix {

struct TouchButton {
    uint16_t mask;       // PAD_* bits produced while held
    float cx, cy, r;     // centre and radius in output pixels
    const char* label;
    bool is_dpad;
};

class TouchControls {
public:
    // Lays out controls for an output of w x h pixels.
    void layout(int w, int h);
    // Buttons pressed by a touch at (x, y) in output pixels.
    uint16_t hit(float x, float y) const;
    const std::vector<TouchButton>& buttons() const { return buttons_; }

private:
    std::vector<TouchButton> buttons_;
};

} // namespace chaotix
