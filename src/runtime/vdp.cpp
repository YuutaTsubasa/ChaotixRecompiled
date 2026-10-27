#include "vdp.h"
#include "tile_replace.h"
#include "patches.h"
#include "log.h"
#include "same_bytes.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace chaotix {

void Vdp::reset() {
    std::memset(vram, 0, sizeof vram);
    std::memset(cram, 0, sizeof cram);
    std::memset(vsram, 0, sizeof vsram);
    std::memset(reg, 0, sizeof reg);
    cmd_pending = false;
    cmd_first = 0;
    addr = 0;
    code = 0;
    fill_pending = false;
    vint_pending = hint_pending = false;
    hint_counter = 0;
    in_vblank = false;
    odd_frame = false;
    sprite_overflow = sprite_collision = false;
    dma_stall_cycles = 0;
}

uint32_t Vdp::cram_to_rgb(uint16_t c) {
    static const uint8_t lv[8] = {0, 36, 73, 109, 146, 182, 219, 255};
    uint32_t r = lv[(c >> 1) & 7], g = lv[(c >> 5) & 7], b = lv[(c >> 9) & 7];
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

uint32_t Vdp::dma_length() const {
    uint32_t len = reg[19] | (uint32_t(reg[20]) << 8);
    return len ? len : 0x10000;
}
uint32_t Vdp::dma_source() const {
    return (uint32_t(reg[21]) << 1) | (uint32_t(reg[22]) << 9) | (uint32_t(reg[23] & 0x7F) << 17);
}
void Vdp::set_dma_source(uint32_t words) {
    // Source address increments within a 128 KiB window (low 16 bits of word address).
    uint32_t src = (reg[21] | (uint32_t(reg[22]) << 8)) + words;
    reg[21] = uint8_t(src);
    reg[22] = uint8_t(src >> 8);
}
void Vdp::set_dma_length_zero() { reg[19] = 0; reg[20] = 0; }

void Vdp::write_vram_word(uint32_t a, uint16_t v) {
    a &= 0xFFFF;
    if (on_vram_write) on_vram_write(on_vram_write_user, a, v);
    vram[a] = uint8_t(v >> 8);
    vram[a ^ 1] = uint8_t(v);
}

void Vdp::write_ctrl(uint16_t v) {
    if (!cmd_pending) {
        if ((v & 0xC000) == 0x8000) {
            int r = (v >> 8) & 0x1F;
            if (r < 24) reg[r] = uint8_t(v);
            // Register writes also clear the code register's low bits (hardware quirk).
            code = 0;
            return;
        }
        cmd_first = v;
        cmd_pending = true;
        code = uint8_t((code & 0x3C) | ((v >> 14) & 3));
        addr = (addr & 0xC000) | (v & 0x3FFF);
        return;
    }
    cmd_pending = false;
    code = uint8_t((code & 0x03) | ((v >> 2) & 0x3C));
    addr = (addr & 0x3FFF) | (uint32_t(v & 3) << 14);
    if ((code & 0x20) && (reg[1] & 0x10)) {  // DMA requested and enabled
        int mode = reg[23] >> 6;
        uint32_t len = dma_length();
        if (mode <= 1) do_dma_68k(len);
        else if (mode == 3) do_dma_copy(len);
        else fill_pending = true;  // fill starts on next data port write
    }
}

void Vdp::do_dma_68k(uint32_t len) {
    uint32_t src = dma_source();
    const uint32_t inc = reg[15];
    for (uint32_t i = 0; i < len; ++i) {
        uint32_t a = (src & 0xFE0000) | ((src + i * 2) & 0x1FFFF);
        uint16_t w = host.dma_read16(host.user, a);
        switch (code & 0x0F) {
        case 1: write_vram_word(addr, w); break;
        case 3: cram[(addr >> 1) & 63] = w & 0x0EEE; break;
        case 5: if (((addr >> 1) & 63) < 40) vsram[(addr >> 1) & 63] = w & 0x07FF; break;
        default: break;
        }
        addr = (addr + inc) & 0xFFFF;
    }
    set_dma_source(len);
    set_dma_length_zero();
    code &= ~0x20;
    // Approximate bus stall: ~205 words/line in blanking, ~18 during display.
    bool blank = in_vblank || !display_enabled();
    dma_stall_cycles += int(len * (blank ? 488 / 205 + 1 : 488 / 18));
}

void Vdp::do_dma_copy(uint32_t len) {
    uint32_t src = reg[21] | (uint32_t(reg[22]) << 8);
    const uint32_t inc = reg[15];
    for (uint32_t i = 0; i < len; ++i) {
        vram[addr & 0xFFFF] = vram[(src + i) & 0xFFFF];
        addr = (addr + inc) & 0xFFFF;
    }
    set_dma_source(len);
    set_dma_length_zero();
    code &= ~0x20;
}

void Vdp::do_dma_fill(uint16_t data, uint32_t len) {
    const uint32_t inc = reg[15];
    switch (code & 0x0F) {
    case 1:
        for (uint32_t i = 0; i < len; ++i) {
            vram[(addr ^ 1) & 0xFFFF] = uint8_t(data >> 8);
            addr = (addr + inc) & 0xFFFF;
        }
        break;
    case 3:
        for (uint32_t i = 0; i < len; ++i) { cram[(addr >> 1) & 63] = data & 0x0EEE; addr = (addr + inc) & 0xFFFF; }
        break;
    case 5:
        for (uint32_t i = 0; i < len; ++i) {
            if (((addr >> 1) & 63) < 40) vsram[(addr >> 1) & 63] = data & 0x07FF;
            addr = (addr + inc) & 0xFFFF;
        }
        break;
    default: break;
    }
    set_dma_source(len);
    set_dma_length_zero();
    code &= ~0x20;
}

void Vdp::write_data(uint16_t v) {
    cmd_pending = false;
    switch (code & 0x0F) {
    case 1: write_vram_word(addr, v); break;
    case 3: cram[(addr >> 1) & 63] = v & 0x0EEE; break;
    case 5: if (((addr >> 1) & 63) < 40) vsram[(addr >> 1) & 63] = v & 0x07FF; break;
    default: CHAOTIX_LOG_LIMITED(LogLevel::Debug, "vdp", 8, "data write with code %X", code); break;
    }
    addr = (addr + reg[15]) & 0xFFFF;
    if (fill_pending) {
        fill_pending = false;
        do_dma_fill(v, dma_length());
    }
}

uint16_t Vdp::read_data() {
    cmd_pending = false;
    uint16_t v = 0;
    switch (code & 0x0F) {
    case 0: v = uint16_t((vram[addr & 0xFFFE] << 8) | vram[(addr & 0xFFFE) | 1]); break;
    case 4: v = vsram[((addr >> 1) & 63) % 40]; break;
    case 8: v = cram[(addr >> 1) & 63]; break;
    default: break;
    }
    addr = (addr + reg[15]) & 0xFFFF;
    return v;
}

uint16_t Vdp::read_status(bool hblank) {
    uint16_t s = 0x3400 | 0x0200;  // FIFO empty
    if (vint_pending) s |= 0x80;
    if (sprite_overflow) s |= 0x40;
    if (sprite_collision) s |= 0x20;
    if (odd_frame) s |= 0x10;
    if (in_vblank || !display_enabled()) s |= 0x08;
    if (hblank) s |= 0x04;
    cmd_pending = false;
    sprite_overflow = sprite_collision = false;
    return s;
}

uint16_t Vdp::read_hv(int line, int hpos_mclk) const {
    // H40: 420 pixel clocks per line (3420 mclk / 8.14). Map to the internal
    // counter sequence 0x00..0xB6, 0xE4..0xFF.
    int pix = hpos_mclk * 420 / 3420;
    int h;
    if (pix <= 0xB6) h = pix; else h = 0xE4 + (pix - 0xB7);
    if (h > 0xFF) h = 0xFF;
    int v = line;
    if (v > 0xEA) v = v - 0xEB + 0xE5;
    return uint16_t(((v & 0xFF) << 8) | (h & 0xFF));
}

void Vdp::on_line_start(int line, int active_lines) {
    if (line <= active_lines) {
        if (line == 0) hint_counter = reg[10];
        else if (hint_counter-- == 0) {
            hint_counter = reg[10];
            hint_pending = true;
        }
    } else {
        hint_counter = reg[10];
    }
    if (line == active_lines) {
        in_vblank = true;
        vint_pending = true;
    }
    if (line == 0) {
        in_vblank = false;
        if (reg[12] & 0x02) odd_frame = !odd_frame;
    }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------
namespace {

// One layer's pixel on a line, in a byte: the palette index in bits 0-5 (0
// where the pattern pixel is transparent) and the priority bit in bit 7, kept
// for transparent pixels too (shadow/highlight looks at it). Bit 6 is never
// set; the composition marks the backdrop with it.
using LinePix = uint8_t;
constexpr uint8_t kPixColor = 0x3F;
constexpr uint8_t kPixPrio = 0x80;
inline LinePix line_pix(unsigned color, unsigned prio) {
    return uint8_t((color & kPixColor) | (prio ? kPixPrio : 0));
}

// A byte of a pattern row as its two pixels, for each horizontal flip and
// name table attribute (bits 13-15: palette line, priority):
// kRowPairs.t[flip][attr][byte], the two LinePix in memory order. A flipped
// row also takes its bytes from the last one.
struct RowPairs {
    uint16_t t[2][8][256];
    RowPairs() {
        for (int f = 0; f < 2; ++f)
            for (unsigned a = 0; a < 8; ++a)
                for (unsigned b = 0; b < 256; ++b) {
                    const unsigned pal = (a & 3) << 4, c0 = b >> 4, c1 = b & 15;
                    const LinePix p0 = line_pix(c0 ? pal | c0 : 0, a >> 2), p1 = line_pix(c1 ? pal | c1 : 0, a >> 2);
                    const LinePix pair[2] = {f ? p1 : p0, f ? p0 : p1};
                    std::memcpy(&t[f][a][b], pair, 2);
                }
    }
};
const RowPairs kRowPairs;

// Two pairs of pixels in memory order, to be stored as one word.
inline uint32_t join_pairs(uint16_t first, uint16_t second) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return uint32_t(first) << 16 | second;
#else
    return uint32_t(second) << 16 | first;
#endif
}

// The 8 pixels of row `ty` (already flipped vertically) of pattern `tile`,
// in the palette line and priority of name table or sprite entry `e` and
// flipped horizontally as it says, made in registers and stored as two words.
inline void pattern_row(const uint8_t* vram, uint32_t tile, int ty, uint16_t e, LinePix* out) {
    // At most 0xFFE0 + 7 * 4 + 3: never wraps.
    const uint8_t* row = vram + ((tile & 0x7FF) << 5) + uint32_t(ty) * 4;
    const uint16_t* pp = kRowPairs.t[(e >> 11) & 1][(e >> 13) & 7];
    const int f = (e & 0x0800) ? 3 : 0;
    const uint32_t lo = join_pairs(pp[row[f]], pp[row[1 ^ f]]), hi = join_pairs(pp[row[2 ^ f]], pp[row[3 ^ f]]);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    const uint64_t all = uint64_t(lo) << 32 | hi;
#else
    const uint64_t all = uint64_t(hi) << 32 | lo;
#endif
    std::memcpy(out, &all, 8);
}

// Plane columns [x0, x1) without replacement graphics (render_plane): out[x -
// xs], from stretches of one V-scroll value (the whole span, or a 16-pixel
// column), each from the name table row of one line. Everything comes by
// value: stores to the line (bytes) could otherwise alias it.
void plane_span(const uint8_t* vram, const uint16_t* vsram, LinePix* out, int xs, int x0, int x1, int line,
                int lane, bool col_vs, int hscroll, uint32_t nt, int pw, int xmask, int ymask) {
    for (int x = x0; x < x1;) {
        int vs = vsram[lane];
        int lim = x1;
        if (col_vs) {
            const int col = x < 0 ? 0 : (x >> 4) > 19 ? 19 : (x >> 4);
            vs = vsram[col * 2 + lane];
            if (((x | 15) + 1) < lim) lim = (x | 15) + 1;
        }
        const int py = (line + vs) & ymask, ty0 = py & 7;
        // nt is 1 KiB aligned, so every entry address is even.
        const uint32_t ntrow = nt + uint32_t((py >> 3) * pw) * 2;
        int px = (x - hscroll) & xmask;
        while (x < lim) {
            const uint32_t ea = (ntrow + uint32_t(px >> 3) * 2) & 0xFFFF;
            const uint16_t e = uint16_t((vram[ea] << 8) | vram[ea + 1]);
            const int ty = (e & 0x1000) ? 7 - ty0 : ty0;
            LinePix* o = out + (x - xs);
            int run = 8 - (px & 7);
            if (run > lim - x) run = lim - x;
            if (run == 8) {
                pattern_row(vram, e, ty, e, o);
            } else {
                LinePix pix[8];
                pattern_row(vram, e, ty, e, pix);
                for (int i = 0; i < run; ++i) o[i] = pix[(px & 7) + i];
            }
            x += run;
            px = (px + run) & xmask;
        }
    }
}

inline uint8_t tile_pixel(const uint8_t* vram, uint32_t tile, int px, int py) {
    uint32_t a = ((tile & 0x7FF) << 5) + uint32_t(py) * 4 + uint32_t(px >> 1);
    uint8_t b = vram[a & 0xFFFF];
    return (px & 1) ? (b & 15) : (b >> 4);
}

// Renders screen columns [xs, xe) (native screen coordinates; xs may be
// negative for widescreen) into out[x - xs].
void render_plane(const Vdp& v, int line, bool planeA, LinePix* out, int xs, int xe, int win_x0, int win_x1) {
    static const int sizes[4] = {32, 64, 32, 128};
    const int pw = sizes[v.reg[16] & 3];
    int ph = sizes[(v.reg[16] >> 4) & 3];
    if ((v.reg[16] & 3) == 3 && ph > 32) ph = 32;  // 128-wide limits height
    const uint32_t nt = planeA ? (uint32_t(v.reg[2] & 0x38) << 10) : (uint32_t(v.reg[4] & 0x07) << 13);
    const uint32_t hs_base = uint32_t(v.reg[13] & 0x3F) << 10;
    // Widescreen extra rows above/below the active area reuse the nearest
    // line's scroll value (the tables only cover the active lines).
    const int hs_line = line < 0 ? 0 : (line >= v.height() ? v.height() - 1 : line);
    uint32_t hs_addr;
    switch (v.reg[11] & 3) {
    case 2: hs_addr = hs_base + uint32_t(hs_line & ~7) * 4; break;
    case 3: hs_addr = hs_base + uint32_t(hs_line) * 4; break;
    default: hs_addr = hs_base; break;
    }
    if (!planeA) hs_addr += 2;
    int hscroll = ((v.vram[hs_addr & 0xFFFF] << 8) | v.vram[(hs_addr + 1) & 0xFFFF]) & 0x3FF;
    const bool col_vs = (v.reg[11] & 4) != 0;
    // Text replaced by tile sequence (text_replace.h): this plane's matched
    // cells, or nullptr (no patterns, or none matched).
    const TextReplacer::Plane* text = v.tiles ? v.tiles->text.plane(planeA ? 0 : 1, pw, ph) : nullptr;
    // Process runs of pixels that share one tile row: a run ends at a tile
    // edge, the window edge, or a 16-pixel column (per-column V-scroll).
    if (!text && (!v.tiles || (!v.tiles->size() && !v.tiles->dumping))) {
        // Nothing replaced and nothing recorded (the usual case): the same
        // runs, each tile row read from VRAM as one 8-pixel row.
        auto span = [&](int x0, int x1) {
            plane_span(v.vram, v.vsram, out, xs, x0, x1, line, planeA ? 0 : 1, col_vs, hscroll, nt, pw, pw * 8 - 1,
                       ph * 8 - 1);
        };
        // Plane A leaves the window's columns to render_window.
        if (planeA && win_x1 > win_x0) {
            span(xs, std::min(win_x0, xe));
            span(std::max(win_x1, xs), xe);
        } else {
            span(xs, xe);
        }
        return;
    }
    int x = xs;
    while (x < xe) {
        if (planeA && x >= win_x0 && x < win_x1) { x = win_x1; continue; }
        // Columns outside the native 20 V-scroll columns reuse the nearest one.
        int col = x < 0 ? 0 : (x >> 4) > 19 ? 19 : (x >> 4);
        int vs = col_vs ? v.vsram[col * 2 + (planeA ? 0 : 1)] : v.vsram[planeA ? 0 : 1];
        int py = (line + vs) & (ph * 8 - 1);
        int px = (x - hscroll) & (pw * 8 - 1);
        int lim = xe;
        if (planeA && x < win_x0 && win_x0 < lim) lim = win_x0;
        if (col_vs && ((x | 15) + 1) < lim) lim = (x | 15) + 1;
        int run = 8 - (px & 7);
        if (run > lim - x) run = lim - x;
        uint32_t ea = nt + uint32_t(((py >> 3) * pw + (px >> 3)) * 2);
        uint16_t e = uint16_t((v.vram[ea & 0xFFFF] << 8) | v.vram[(ea + 1) & 0xFFFF]);
        int ty = py & 7;
        if (e & 0x1000) ty = 7 - ty;
        uint8_t pal = uint8_t((e >> 9) & 0x30), prio = uint8_t((e >> 15) & 1);
        bool hflip = (e & 0x0800) != 0;
        uint8_t pix[8];
        const TextReplacer::Hit* hit = nullptr;
        const TextReplacer::Hit* over = nullptr;
        if (text) {
            const int cell = (py >> 3) * pw + (px >> 3);
            hit = TextReplacer::run_hit(*text, cell, e);
            if (!hit) over = TextReplacer::over_hit(*text, cell);
        }
        if (hit) {
            // The image is drawn as it is (no flip), in the first tile's colours.
            TextReplacer::pixels(*hit, px & ~7, py, pw * 8, ph * 8, pix);
            TextReplacer::colours(*hit, v.vram, pal, prio);
            hflip = false;
        } else if (const uint8_t* rp = v.tiles ? v.tiles->slot(e) : nullptr) {
            for (int k = 0; k < 8; ++k) pix[k] = rp[ty * 8 + k];
        } else {
            uint32_t row = ((uint32_t(e) & 0x7FF) << 5) + uint32_t(ty) * 4;
            for (int k = 0; k < 4; ++k) {
                uint8_t b = v.vram[(row + uint32_t(k)) & 0xFFFF];
                pix[k * 2] = b >> 4;
                pix[k * 2 + 1] = b & 15;
            }
        }
        if (v.tiles) v.tiles->used(e, (e >> 13) & 3);
        for (int i = 0; i < run; ++i) {
            int tx = (px & 7) + i;
            uint8_t c = pix[hflip ? 7 - tx : tx];
            out[x - xs + i] = line_pix(c ? pal | c : 0, prio);
        }
        if (over) {
            // An image reaching past its run covers this tile where it is not 0.
            uint8_t ov[8];
            TextReplacer::pixels(*over, px & ~7, py, pw * 8, ph * 8, ov);
            uint8_t opal, oprio;
            TextReplacer::colours(*over, v.vram, opal, oprio);
            for (int i = 0; i < run; ++i)
                if (const uint8_t c = ov[(px & 7) + i]) out[x - xs + i] = line_pix(opal | c, oprio);
        }
        x += run;
    }
}

void render_window(const Vdp& v, int line, LinePix* out, int xs, int x0, int x1) {
    const bool h40 = (v.reg[12] & 1) != 0;
    const uint32_t nt = h40 ? (uint32_t(v.reg[3] & 0x3C) << 10) : (uint32_t(v.reg[3] & 0x3E) << 10);
    const int pw = h40 ? 64 : 32;
    int ty0 = line >> 3, ty = line & 7;
    const TextReplacer::Plane* text = v.tiles ? v.tiles->text.plane(2, pw, 32) : nullptr;
    for (int x = x0; x < x1; ++x) {
        uint32_t ea = nt + uint32_t((ty0 * pw + (x >> 3)) * 2);
        uint16_t e = uint16_t((v.vram[ea & 0xFFFF] << 8) | v.vram[(ea + 1) & 0xFFFF]);
        if (text && ty0 >= 0 && ty0 < 32 && x >= 0 && (x >> 3) < pw) {
            // Text replaced by tile sequence (text_replace.h).
            const int cell = ty0 * pw + (x >> 3);
            const TextReplacer::Hit* hit = TextReplacer::run_hit(*text, cell, e);
            const TextReplacer::Hit* h = hit ? hit : TextReplacer::over_hit(*text, cell);
            if (h) {
                uint8_t px8[8];
                TextReplacer::pixels(*h, x & ~7, line, pw * 8, 32 * 8, px8);
                const uint8_t c = px8[x & 7];
                if (hit || c) {
                    if (v.tiles) v.tiles->used(e, (e >> 13) & 3);
                    uint8_t pal, prio;
                    TextReplacer::colours(*h, v.vram, pal, prio);
                    out[x - xs] = line_pix(c ? pal | c : 0, prio);
                    continue;
                }
            }
        }
        int tx = x & 7, yy = ty;
        if (e & 0x0800) tx = 7 - tx;
        if (e & 0x1000) yy = 7 - yy;
        const uint8_t* rp = v.tiles ? v.tiles->slot(e) : nullptr;
        uint8_t c = rp ? rp[yy * 8 + tx] : tile_pixel(v.vram, e, tx, yy);
        if (v.tiles) v.tiles->used(e, (e >> 13) & 3);
        out[x - xs] = line_pix(c ? ((e >> 9) & 0x30) | c : 0, (e >> 15) & 1);
    }
}

} // namespace

void Vdp::render_line(int line, uint32_t* out_rgb, uint8_t* out_bg, int extra) {
    uint8_t top[kMaxWidth];
    if (!render_line_indexed(line, top, out_rgb, out_bg, extra)) return;
    const int n = width() + 2 * extra;
    for (int x = 0; x < n; ++x) {
        out_rgb[x] = palrgb_[top[x] & 0x3F] | 0xFF000000u;
        out_bg[x] = uint8_t(top[x] >> 6);
    }
}

bool Vdp::render_line_indexed(int line, uint8_t* top, uint32_t* out_rgb, uint8_t* out_bg, int extra) {
    const int width = this->width();
    const int xs = -extra, xe = width + extra, n = xe - xs;
    const uint8_t bgidx = reg[7] & 0x3F;
    // CRAM as colours, converted again only when it changed.
    if (!palrgb_valid_ || !same_bytes(palrgb_cram_, cram, sizeof cram)) {
        for (int i = 0; i < 64; ++i) palrgb_[i] = cram_to_rgb(cram[i]);
        std::memcpy(palrgb_cram_, cram, sizeof cram);
        palrgb_valid_ = true;
    }
    if (!display_enabled()) {
        std::memset(top, bgidx | 0x40, size_t(n));
        return true;
    }

    if (tiles && tiles->active() && line == 0) {
        uint32_t argb[64];
        for (int i = 0; i < 64; ++i) argb[i] = cram_to_rgb(cram[i]) | 0xFF000000u;
        tiles->refresh(vram, argb);
        if (tiles->text.active(tiles->dumping)) tiles->text.refresh(*this, tiles->slot_hashes(), tiles->dumping);
        if (tiles->sprite_text.active(tiles->dumping))
            tiles->sprite_text.refresh(*this, tiles->slot_hashes(), tiles->dumping);
    }

    // Planes A (with the window) and B write every column; sprites only some.
    LinePix a[kMaxWidth], b[kMaxWidth], s[kMaxWidth];
    std::memset(s, 0, sizeof s);

    // Window region for this line.
    int win_x0 = 0, win_x1 = 0;
    if (line >= 0 && line < height()) {
        int wvp = (reg[18] & 0x1F) * 8;
        bool down = (reg[18] & 0x80) != 0;
        bool vwin = down ? (line >= wvp) : (line < wvp);
        if (vwin) { win_x0 = 0; win_x1 = width; }
        else {
            int whp = (reg[17] & 0x1F) * 16;
            if (whp > width) whp = width;
            if (reg[17] & 0x80) { win_x0 = whp; win_x1 = width; }
            else { win_x0 = 0; win_x1 = whp; }
        }
    }

    // Text replaced by tile sequence follows the name table as this line
    // shows it (text_replace.h).
    if (tiles && tiles->text.size()) tiles->text.update_line(*this, line, win_x1 > win_x0, tiles->slot_hashes());

    render_plane(*this, line, false, b, xs, xe, 0, 0);
    render_plane(*this, line, true, a, xs, xe, win_x0, win_x1);
    if (win_x1 > win_x0) render_window(*this, line, a, xs, win_x0, win_x1);
    if (radial_len > 0 && extra > 0 && line >= 0 && line < height()) {
        // The margins continue the scene's rings (see radial_lut). The rings
        // are opaque, and plane B (the scene's own cells stop at the 4:3
        // edges) would only show over them where a cell left behind in its
        // name table has the priority bit: the bonus stage, entered from a
        // level, keeps the level's VRAM there (issue #52), so it is left out.
        const double dy = double(2 * line + 1 - radial_cy2);
        for (int x = xs; x < xe; ++x) {
            if (x == 0) x = width;
            const double dx = double(2 * x + 1 - radial_cx2);
            const int r4 = int(2.0 * std::sqrt(dx * dx + dy * dy));
            a[x - xs] = line_pix(radial_lut[r4 < radial_len ? r4 : radial_len - 1], 0);
            b[x - xs] = 0;
        }
    }

    // Sprites
    {
        const bool h40 = (reg[12] & 1) != 0;
        const uint32_t sat = h40 ? (uint32_t(reg[5] & 0x7E) << 9) : (uint32_t(reg[5] & 0x7F) << 9);
        const int max_sprites = h40 ? 80 : 64;
        const int max_per_line = h40 ? 20 : 16;
        // The status flags (overflow/collision) always follow the native
        // rules so the game observes exactly the same values in widescreen;
        // only the drawing budget grows with the rendered width.
        const int native_pixels = h40 ? 320 : 256;
        // width is 320 or 256 with h40: constant divisors, so no division
        // call on 32-bit ARM.
        auto per_width = [h40](int v) { return h40 ? v / 320 : v / 256; };
        const int max_pixels = per_width(native_pixels * n);
        const int max_per_line_draw = per_width(max_per_line * n);
        int link = 0, count = 0, on_line = 0, pixels = 0;
        bool nonzero_x_seen = false, masked = false;
        uint8_t filled[kMaxWidth] = {};
        uint8_t hidden_only[kMaxWidth] = {};  // filled by a hidden sprite alone
        // With replaced tiles (issue #35) the collision flag still follows the
        // original pixels, so the game sees exactly what it would have; this
        // tracks them apart from what is drawn.
        uint8_t solid[kMaxWidth] = {};
        // Text replaced by sprite (sprite_text.h): the matched groups whose
        // sprites are as matched on this line are drawn from their images
        // instead, each in its first member's place in the list (`place`
        // keeps, per pixel, the list place of the sprite drawn there).
        const SpriteTextReplacer* st =
            tiles && !tiles->sprite_text.hits().empty() ? &tiles->sprite_text : nullptr;
        bool st_live[SpriteTextReplacer::kMaxSprites] = {};
        uint8_t place[kMaxWidth];
        if (st) {
            for (size_t h = 0; h < st->hits().size(); ++h) st_live[h] = st->unchanged_hit(vram, sat, int(h));
            std::memset(place, 0xFF, sizeof place);
        }
        const bool replacing = tiles && (tiles->size() || st);
        // Nothing replaced and nothing recorded (the usual case): each 8-pixel
        // column of a sprite drawn from one pattern row.
        const bool plain = !st && !(tiles && (tiles->size() || tiles->dumping));
        // The list follows the links from entry 0. The table ends at most at
        // 0xFC00 + 80 * 8 (H40) or 0xFE00 + 64 * 8 (H32): never wraps.
        const size_t sat_size = size_t(max_sprites) * 8;
        if (sat != sat_addr_ || max_sprites != sat_max_ || !same_bytes(sat_bytes_, vram + sat, sat_size)) {
            std::memcpy(sat_bytes_, vram + sat, sat_size);
            sat_addr_ = sat;
            sat_max_ = max_sprites;
            sat_len_ = 0;
            while (sat_len_ < max_sprites) {
                const uint8_t* p = sat_bytes_ + link * 8;
                sat_list_[sat_len_++] = SatEntry{uint16_t(p[0] << 8 | p[1]), uint16_t(p[2] << 8 | p[3]),
                                                 uint16_t(p[4] << 8 | p[5]), uint16_t(p[6] << 8 | p[7]), uint8_t(link)};
                link = p[3] & 0x7F;
                if (link == 0 || link >= max_sprites) break;
            }
        }
        for (; count < sat_len_; ++count) {
            const SatEntry& se = sat_list_[count];
            link = se.index;
            int sy = (se.y & 0x3FF) - 128;
            uint16_t sz = se.size;
            int hs = ((sz >> 10) & 3) + 1, vsz = ((sz >> 8) & 3) + 1;
            uint16_t pat = se.pattern;
            const uint16_t raw_x = se.x;
            int sxr = raw_x & 0x1FF;
            // The widened ring hook retains screen X + 128 in the SAT word.
            // Only the added right edge of a level needs coordinates beyond
            // the native nine bits. Preserve masking of unrelated high bits.
            if (h40 && reg[16] == patches::kLevelPlaneSize && extra > 64 &&
                raw_x >= 512 && int(raw_x) < xe + 128)
                sxr = raw_x;
            // An extended X of 384 is raw 512, not the zero-X mask sprite.
            int sx = sxr - 128;
            if (line >= sy && line < sy + vsz * 8) {
                if (++on_line > max_per_line) sprite_overflow = true;
                if (on_line > max_per_line_draw) break;
                if (sxr == 0) { if (nonzero_x_seen) masked = true; }
                else nonzero_x_seen = true;
                const bool hidden = (pat & 0x7FF) == hidden_frame_tile && hs == 2 && vsz == 4 && (sx == 0 || sx == 304);
                // A member of a replaced group: counted, not drawn.
                const int st_hit = st ? st->hit_of(link) : -1;
                const bool st_member = st_hit >= 0 && st_live[st_hit];
                int row = line - sy;
                if (pat & 0x1000) row = vsz * 8 - 1 - row;
                if (plain) {
                    // The pixel budget as the loop below spends it: `cnt`
                    // pixels drawn, the overflow flag set if one checked
                    // (up to the one that stops it) is past the native budget.
                    const int w = hs * 8, left = max_pixels - pixels;
                    const int cnt = left <= 0 ? 0 : left < w ? left : w;
                    const int last = left <= 0 ? 0 : left < w - 1 ? left : w - 1;
                    if (pixels + last >= native_pixels) sprite_overflow = true;
                    pixels += cnt;
                    if (masked) continue;
                    for (int c0 = 0; c0 < cnt; c0 += 8) {
                        // Sprite columns [a0, a1) of this 8-pixel column that are on the line.
                        const int a0 = std::max(c0, xs - sx), a1 = std::min(std::min(c0 + 8, cnt), xe - sx);
                        if (a0 >= a1) continue;
                        const int cell = (pat & 0x0800) ? hs - 1 - (c0 >> 3) : (c0 >> 3);
                        LinePix pix[8];
                        pattern_row(vram, (pat & 0x7FFu) + uint32_t(cell * vsz + (row >> 3)), row & 7, pat, pix);
                        for (int cx = a0; cx < a1; ++cx) {
                            const LinePix p = pix[cx - c0];
                            if (!(p & kPixColor)) continue;
                            const int x = sx + cx, i = x - xs;
                            if (filled[i]) {
                                if (x >= 0 && x < width) sprite_collision = true;
                                // A hidden sprite still collides, but does not cover what is behind it.
                                if (!hidden_only[i] || hidden) continue;
                                hidden_only[i] = 0;
                                s[i] = p;
                                continue;
                            }
                            filled[i] = 1;
                            if (hidden) { hidden_only[i] = 1; continue; }
                            s[i] = p;
                        }
                    }
                    continue;
                }
                for (int cx = 0; cx < hs * 8; ++cx) {
                    if (pixels >= native_pixels) sprite_overflow = true;
                    if (pixels >= max_pixels) break;
                    ++pixels;
                    if (masked) continue;
                    int x = sx + cx;
                    if (x < xs || x >= xe) continue;
                    int col = (pat & 0x0800) ? (hs * 8 - 1 - cx) : cx;
                    uint32_t tile = (pat & 0x7FF) + uint32_t((col >> 3) * vsz + (row >> 3));
                    uint8_t c = tile_pixel(vram, tile, col & 7, row & 7);
                    const int i = x - xs;
                    if (tiles) tiles->used(tile, (pat >> 13) & 3);
                    if (replacing) {
                        if (c) {
                            if (solid[i] && x >= 0 && x < width) sprite_collision = true;
                            solid[i] = 1;
                        }
                        if (st_member) continue;
                        if (const uint8_t* rp = tiles->slot(tile)) c = rp[(row & 7) * 8 + (col & 7)];
                    }
                    if (!c) continue;
                    if (filled[i]) {
                        if (!replacing && x >= 0 && x < width) sprite_collision = true;
                        // A hidden sprite still collides, but does not cover what is behind it.
                        if (!hidden_only[i] || hidden) continue;
                        hidden_only[i] = 0;
                        s[i] = line_pix(((pat >> 9) & 0x30) | c, (pat >> 15) & 1);
                        if (st) place[i] = uint8_t(count);
                        continue;
                    }
                    filled[i] = 1;
                    if (hidden) { hidden_only[i] = 1; continue; }
                    s[i] = line_pix(((pat >> 9) & 0x30) | c, (pat >> 15) & 1);
                    if (st) place[i] = uint8_t(count);
                }
            }
        }
        if (st) {
            // The images, over the sprites after their first present member
            // in the list and under those before it, placed from that
            // member with its palette line and priority as they are now,
            // clipped to the line.
            for (size_t h = 0; h < st->hits().size(); ++h) {
                if (!st_live[h]) continue;
                const SpriteTextReplacer::Hit& hit = st->hits()[h];
                const uint32_t e = sat + uint32_t(hit.first) * 8;
                auto rw = [&](uint32_t o) { return uint16_t((vram[(e + o) & 0xFFFF] << 8) | vram[(e + o + 1) & 0xFFFF]); };
                const SpriteTextReplacer::Pattern& p = *hit.p;
                const int iy = line - ((rw(0) & 0x3FF) - 128 - hit.dy + p.image_y);
                if (iy < 0 || iy >= p.height) continue;
                const int x0 = (rw(6) & 0x1FF) - 128 - hit.dx + p.image_x;
                const uint16_t pat = rw(4);
                const uint8_t pal = uint8_t((pat >> 9) & 0x30), prio = uint8_t((pat >> 15) & 1);
                const uint8_t* src = p.indices.data() + size_t(iy) * size_t(p.width);
                for (int k = 0; k < p.width; ++k) {
                    const int x = x0 + k;
                    if (x < xs || x >= xe || !src[k]) continue;
                    const int i = x - xs;
                    if ((s[i] & kPixColor) && place[i] < hit.place) continue;
                    s[i] = line_pix(pal | src[k], prio);
                    place[i] = hit.place;
                }
            }
        }
    }

    const bool shi = (reg[12] & 0x08) != 0;
    const uint32_t* palrgb = palrgb_;
    if (!shi && !alt_cram) {
        // The priorities below as selections from the lowest up, the last
        // that applies winning, which the compiler can do for many pixels at
        // once; bit 6 marks the backdrop.
        const uint8_t back = uint8_t(bgidx | 0x40);
        for (int x = 0; x < n; ++x) {
            const uint8_t sp = s[x], ap = a[x], bp = b[x];
            const bool so = (sp & kPixColor) != 0, ao = (ap & kPixColor) != 0, bo = (bp & kPixColor) != 0;
            uint8_t t = back;
            t = bo ? bp : t;
            t = ao ? ap : t;
            t = so ? sp : t;
            t = bo && (bp & kPixPrio) ? bp : t;
            t = ao && (ap & kPixPrio) ? ap : t;
            t = so && (sp & kPixPrio) ? sp : t;
            top[x] = uint8_t(t & ~kPixPrio);
        }
        return true;
    }
    for (int x = 0; x < n; ++x) {
        const uint8_t sc = s[x] & kPixColor, ac = a[x] & kPixColor, bc = b[x] & kPixColor;
        const bool sh = (s[x] & kPixPrio) != 0, ah = (a[x] & kPixPrio) != 0, bh = (b[x] & kPixPrio) != 0;
        uint8_t c = 0;
        bool isbg = false, sprite = false;
        // priority: sprite hi > A hi > B hi > sprite lo > A lo > B lo > backdrop
        if (sc && sh) { c = sc; sprite = true; }
        else if (ac && ah) c = ac;
        else if (bc && bh) c = bc;
        else if (sc) { c = sc; sprite = true; }
        else if (ac) c = ac;
        else if (bc) c = bc;
        else { c = bgidx; isbg = true; }
        uint32_t rgb = palrgb[c];
        // The lobby's next palette in its margins (see alt_cram); sprites keep
        // the game's colours.
        if (alt_cram && !sprite && c < 32 && x >= alt_x0 && x < alt_x1) rgb = cram_to_rgb(alt_cram[c]);
        if (shi) {
            bool shadow = !(ah || bh);
            if (sc == 0x3E || sc == 0x3F) {
                // operator sprites: recompute top non-sprite layer
                uint8_t u = 0;
                if (ac && ah) u = ac;
                else if (bc && bh) u = bc;
                else if (ac) u = ac;
                else if (bc) u = bc;
                else { u = bgidx; }
                rgb = palrgb[u];
                if (sc == 0x3E) {
                    // highlight operator: shadowed -> normal, normal -> highlighted
                    if (!shadow) rgb = 0xFF000000u | (((rgb & 0xFEFEFE) >> 1) + 0x808080);
                    shadow = false;
                } else {
                    shadow = true;  // shadow operator
                }
            } else if (sc && sh) {
                shadow = false;
            }
            if (shadow) rgb = 0xFF000000u | ((rgb & 0xFEFEFE) >> 1);
        }
        out_rgb[x] = rgb | 0xFF000000u;
        out_bg[x] = isbg ? 1 : 0;
    }
    return false;
}

} // namespace chaotix
