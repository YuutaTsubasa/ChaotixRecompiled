// The input display overlay (issue #47): a pad's buttons, lit while held,
// drawn small in a corner. Laid out here in units of a box kWidth x kHeight,
// so the window code and the headless tool draw the same thing, and a test can
// see that nothing falls outside the box.
#pragma once
#include <cstdint>
#include <vector>

namespace chaotix::input_display {

inline constexpr int kWidth = 56;
inline constexpr int kHeight = 20;

struct Button {
    int x, y, w, h;      // in box units
    uint16_t mask;       // PAD_* (input/input.h)
    bool lit;            // held
    char label;          // a letter for it, 0 for the directions
};

// The D-pad (a cross), START, and A B C under X Y Z. MODE is not shown.
std::vector<Button> layout(uint16_t pad);

// Draws one pad's display into an XRGB8888 picture, `scale` pixels to a unit,
// with its box's top left at (x0, y0): a dark translucent box, held buttons
// bright, the rest dim. Everything is clipped to the box and to the picture.
// `opacity` 0-255 scales the whole display.
void draw(uint32_t* pixels, int stride, int width, int height, int x0, int y0, int scale, uint16_t pad,
          uint8_t opacity);

} // namespace chaotix::input_display
