// See input_display.h.
#include "frontend/input_display.h"
#include "input/input.h"

#include <algorithm>

namespace chaotix::input_display {

std::vector<Button> layout(uint16_t pad) {
    struct Spot { int x, y, w, h; uint16_t mask; char label; };
    static constexpr Spot kSpots[] = {
        {7, 1, 6, 6, PAD_UP, 0},    {1, 7, 6, 6, PAD_LEFT, 0},  {13, 7, 6, 6, PAD_RIGHT, 0},
        {7, 13, 6, 6, PAD_DOWN, 0}, {21, 9, 8, 4, PAD_START, 'S'},
        {32, 2, 6, 6, PAD_X, 'X'},  {40, 2, 6, 6, PAD_Y, 'Y'},   {48, 2, 6, 6, PAD_Z, 'Z'},
        {32, 12, 6, 6, PAD_A, 'A'}, {40, 12, 6, 6, PAD_B, 'B'},  {48, 12, 6, 6, PAD_C, 'C'},
    };
    std::vector<Button> out;
    for (const Spot& s : kSpots) out.push_back({s.x, s.y, s.w, s.h, s.mask, (pad & s.mask) != 0, s.label});
    return out;
}

namespace {

// Blends colour c over the pixel at alpha a (0-255).
void blend(uint32_t& px, uint32_t c, unsigned a) {
    const unsigned ia = 255 - a;
    const unsigned r = (((px >> 16) & 0xFF) * ia + ((c >> 16) & 0xFF) * a) / 255;
    const unsigned g = (((px >> 8) & 0xFF) * ia + ((c >> 8) & 0xFF) * a) / 255;
    const unsigned b = ((px & 0xFF) * ia + (c & 0xFF) * a) / 255;
    px = (px & 0xFF000000u) | r << 16 | g << 8 | b;
}

struct Clip { int x0, y0, x1, y1; };

void fill(uint32_t* pixels, int stride, const Clip& clip, int x, int y, int w, int h, uint32_t c, unsigned a) {
    const int xa = std::max(x, clip.x0), ya = std::max(y, clip.y0);
    const int xb = std::min(x + w, clip.x1), yb = std::min(y + h, clip.y1);
    for (int yy = ya; yy < yb; ++yy)
        for (int xx = xa; xx < xb; ++xx) blend(pixels[size_t(yy) * size_t(stride) + size_t(xx)], c, a);
}

} // namespace

void draw(uint32_t* pixels, int stride, int width, int height, int x0, int y0, int scale, uint16_t pad,
          uint8_t opacity) {
    if (!pixels || scale < 1 || !opacity) return;
    // The box, and the picture: nothing is drawn outside either.
    const Clip clip{std::max(x0, 0), std::max(y0, 0), std::min(x0 + kWidth * scale, width),
                    std::min(y0 + kHeight * scale, height)};
    if (clip.x0 >= clip.x1 || clip.y0 >= clip.y1) return;
    auto alpha = [opacity](unsigned a) { return a * opacity / 255; };
    fill(pixels, stride, clip, x0, y0, kWidth * scale, kHeight * scale, 0x000000, alpha(150));
    for (const Button& b : layout(pad)) {
        const uint32_t colour = b.lit ? 0xFFF0A0 : 0x8890B0;
        fill(pixels, stride, clip, x0 + b.x * scale, y0 + b.y * scale, b.w * scale, b.h * scale, colour,
             alpha(b.lit ? 255 : 110));
    }
}

} // namespace chaotix::input_display
