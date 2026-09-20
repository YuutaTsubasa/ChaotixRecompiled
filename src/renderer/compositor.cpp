// 32X VDP line rendering (packed pixel / direct colour / run length) and the
// priority encoding used to merge with the Mega Drive layer.
//
// Output encoding per pixel (alpha byte):
//   0xFE: 32X pixel is visible regardless of the MD pixel
//   0xFD: 32X pixel is visible only where the MD pixel is the backdrop
//   0x00: no 32X pixel (blank mode)
#include "runtime/system.h"

namespace chaotix {

namespace {
inline uint32_t rgb555(uint16_t c) {
    uint32_t r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
    r = (r << 3) | (r >> 2); g = (g << 3) | (g >> 2); b = (b << 3) | (b >> 2);
    return (r << 16) | (g << 8) | b;
}
inline uint32_t encode(uint16_t c, bool pri) {
    bool through = (c & 0x8000) != 0;
    return ((through != pri) ? 0xFE000000u : 0xFD000000u) | rgb555(c);
}
} // namespace

void Machine::render_line32x(int ln, uint32_t* all, int extra, bool wide_src) {
    // Widescreen margins show no 32X pixels (the MD layer shows through)
    // unless the scene draws into the frame buffer around each line.
    for (int x = 0; x < kNativeWidth + 2 * extra; ++x) all[x] = 0;
    uint32_t* out = all + extra;
    const int mode = mars.bitmap_mode & 3;
    if (mode == 0 || !(mars.adapter_ctrl & 1)) return;
    const bool pri = (mars.bitmap_mode & 0x80) != 0;
    const uint8_t* fb = mars.fb[mars.fb_display];
    uint32_t lineaddr = (uint32_t(fb[ln * 2]) << 8) | fb[ln * 2 + 1];
    uint32_t base = (lineaddr * 2) & 0x1FFFF;
    switch (mode) {
    case 1: {  // packed pixel, 8 bpp
        uint32_t p = base + (mars.screen_shift & 1);
        if (wide_src) {
            // Margins come from the host-side shadow (see Machine::fb_margin).
            const uint8_t* sh = fb_margin[mars.fb_display];
            for (int x = -extra; x < 0; ++x) all[x + extra] = encode(mars.pal[sh[(p + uint32_t(x)) & 0x1FFFF]], pri);
            for (int x = kNativeWidth; x < kNativeWidth + extra; ++x) all[x + extra] = encode(mars.pal[sh[(p + uint32_t(x)) & 0x1FFFF]], pri);
        }
        for (int x = 0; x < kNativeWidth; ++x) out[x] = encode(mars.pal[fb[(p + uint32_t(x)) & 0x1FFFF]], pri);
        break;
    }
    case 2: {  // direct colour, 15 bpp
        for (int x = 0; x < kNativeWidth; ++x) {
            uint32_t o = (base + uint32_t(x) * 2) & 0x1FFFF;
            out[x] = encode(uint16_t((fb[o] << 8) | fb[o + 1]), pri);
        }
        break;
    }
    case 3: {  // run length
        int x = 0;
        uint32_t o = base;
        while (x < kNativeWidth) {
            uint32_t len = fb[o & 0x1FFFF] + 1u;
            uint16_t c = mars.pal[fb[(o + 1) & 0x1FFFF]];
            o += 2;
            uint32_t px = encode(c, pri);
            for (uint32_t i = 0; i < len && x < kNativeWidth; ++i) out[x++] = px;
        }
        break;
    }
    }
}

} // namespace chaotix
