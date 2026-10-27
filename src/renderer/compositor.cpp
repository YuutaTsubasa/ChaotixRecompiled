// 32X VDP line rendering (packed pixel / direct colour / run length) and the
// priority encoding used to merge with the Mega Drive layer.
//
// Output encoding per pixel (alpha byte):
//   0xFE: 32X pixel is visible regardless of the MD pixel
//   0xFD: 32X pixel is visible only where the MD pixel is the backdrop
//   0x00: no 32X pixel (blank/disabled display or unsupported margin)
//
// Zero is an ordinary displayed pixel: pal[0] in indexed modes, black in
// direct colour. Zero-byte suppression applies only to overwrite-image writes.
#include "runtime/system.h"
#include "runtime/character_mods.h"
#include "runtime/patches.h"
#include "runtime/sprite_record.h"

#include <cstring>

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
    const uint8_t* shadow = fb_margin[mars.fb_display];
    // Replacement graphics (runtime/sprite_replace.h): the same picture
    // composed again with them, shown in place of the emulated buffer and
    // its margins. Only what is shown; the machine keeps its own buffers.
    if (sprites) sprites->shown(*this, fb, shadow);
    // Character mods' colours (runtime/character_mods.h): a copy of the
    // palette with their entries substituted, likewise only for the picture.
    const uint16_t* pal = characters ? characters->display(mars.pal, sdram + CharacterPalettes::kOffsetBytes) : mars.pal;
    // The palette's 256 entries encoded once, again only when it changed.
    if (enc32x_pri != int(pri) || std::memcmp(enc32x_pal, pal, sizeof enc32x_pal) != 0) {
        for (int i = 0; i < 256; ++i) enc32x[i] = encode(pal[i], pri);
        std::memcpy(enc32x_pal, pal, sizeof enc32x_pal);
        enc32x_pri = int(pri);
    }
    const uint32_t* enc = enc32x;
    // Widescreen rows above/below the active area are not in the line table;
    // the level frame buffer is a uniform grid, so extrapolate from the edge.
    const int lt = ln < 0 ? 0 : (ln >= kActiveLines ? kActiveLines - 1 : ln);
    uint32_t lineaddr = (uint32_t(fb[lt * 2]) << 8) | fb[lt * 2 + 1];
    lineaddr += uint32_t(ln - lt) * (patches::kFbLineStride / 2);
    uint32_t base = (lineaddr * 2) & 0x1FFFF;
    switch (mode) {
    case 1: {  // packed pixel, 8 bpp
        uint32_t p = base + (mars.screen_shift & 1);
        if (wide_src) {
            // Margins come from the host-side shadow (see Machine::fb_margin).
            const uint8_t* sh = shadow;
            for (int x = -extra; x < 0; ++x) all[x + extra] = enc[sh[(p + uint32_t(x)) & 0x1FFFF]];
            for (int x = kNativeWidth; x < kNativeWidth + extra; ++x) all[x + extra] = enc[sh[(p + uint32_t(x)) & 0x1FFFF]];
        }
        // Extended rows are entirely redirected to the shadow by the blitter.
        const uint8_t* center = wide_src && (ln < 0 || ln >= kActiveLines) ? shadow : fb;
        if (p + kNativeWidth <= 0x20000) {
            const uint8_t* src = center + p;  // the line does not wrap
            for (int x = 0; x < kNativeWidth; ++x) out[x] = enc[src[x]];
        } else {
            for (int x = 0; x < kNativeWidth; ++x) out[x] = enc[center[(p + uint32_t(x)) & 0x1FFFF]];
        }
        break;
    }
    case 2: {  // direct colour, 15 bpp
        for (int x = 0; x < kNativeWidth; ++x) {
            uint32_t o = (base + uint32_t(x) * 2) & 0x1FFFF;
            uint16_t v = uint16_t((fb[o] << 8) | fb[o + 1]);
            out[x] = encode(v, pri);
        }
        break;
    }
    case 3: {  // run length
        int x = 0;
        uint32_t o = base;
        while (x < kNativeWidth) {
            uint32_t len = fb[o & 0x1FFFF] + 1u;
            uint8_t idx = fb[(o + 1) & 0x1FFFF];
            o += 2;
            uint32_t px = enc[idx];
            for (uint32_t i = 0; i < len && x < kNativeWidth; ++i) out[x++] = px;
        }
        break;
    }
    }
}

} // namespace chaotix
