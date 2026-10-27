#include "vdp.h"
#include "tile_replace.h"
#include "patches.h"
#include "log.h"
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

struct LinePix {
    uint8_t color;  // palette index 0-63 (0 in low nibble = transparent)
    uint8_t prio;
};

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
            out[x - xs + i].color = c ? uint8_t(pal | c) : 0;
            out[x - xs + i].prio = prio;
        }
        if (over) {
            // An image reaching past its run covers this tile where it is not 0.
            uint8_t ov[8];
            TextReplacer::pixels(*over, px & ~7, py, pw * 8, ph * 8, ov);
            uint8_t opal, oprio;
            TextReplacer::colours(*over, v.vram, opal, oprio);
            for (int i = 0; i < run; ++i)
                if (const uint8_t c = ov[(px & 7) + i]) {
                    out[x - xs + i].color = uint8_t(opal | c);
                    out[x - xs + i].prio = oprio;
                }
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
                    out[x - xs].color = c ? uint8_t(pal | c) : 0;
                    out[x - xs].prio = prio;
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
        out[x - xs].color = c ? uint8_t(((e >> 9) & 0x30) | c) : 0;
        out[x - xs].prio = (e >> 15) & 1;
    }
}

} // namespace

void Vdp::render_line(int line, uint32_t* out_rgb, uint8_t* out_bg, int extra) {
    const int width = this->width();
    const int xs = -extra, xe = width + extra, n = xe - xs;
    const uint8_t bgidx = reg[7] & 0x3F;
    const uint32_t bg = cram_to_rgb(cram[bgidx]);
    if (!display_enabled()) {
        for (int x = 0; x < n; ++x) { out_rgb[x] = bg; out_bg[x] = 1; }
        return;
    }

    if (tiles && tiles->active() && line == 0) {
        uint32_t argb[64];
        for (int i = 0; i < 64; ++i) argb[i] = cram_to_rgb(cram[i]) | 0xFF000000u;
        tiles->refresh(vram, argb);
        if (tiles->text.active(tiles->dumping)) tiles->text.refresh(*this, tiles->slot_hashes(), tiles->dumping);
        if (tiles->sprite_text.active(tiles->dumping))
            tiles->sprite_text.refresh(*this, tiles->slot_hashes(), tiles->dumping);
    }

    LinePix a[kMaxWidth], b[kMaxWidth], s[kMaxWidth];
    std::memset(a, 0, sizeof a);
    std::memset(b, 0, sizeof b);
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
            a[x - xs] = LinePix{radial_lut[r4 < radial_len ? r4 : radial_len - 1], 0};
            b[x - xs] = LinePix{0, 0};
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
        const int max_pixels = native_pixels * n / width;
        const int max_per_line_draw = max_per_line * n / width;
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
        while (count < max_sprites) {
            uint32_t e = sat + uint32_t(link) * 8;
            auto rw = [&](uint32_t o) { return uint16_t((vram[(e + o) & 0xFFFF] << 8) | vram[(e + o + 1) & 0xFFFF]); };
            int sy = (rw(0) & 0x3FF) - 128;
            uint16_t sz = rw(2);
            int hs = ((sz >> 10) & 3) + 1, vsz = ((sz >> 8) & 3) + 1;
            uint16_t pat = rw(4);
            const uint16_t raw_x = rw(6);
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
                        s[i].color = uint8_t(((pat >> 9) & 0x30) | c);
                        s[i].prio = (pat >> 15) & 1;
                        if (st) place[i] = uint8_t(count);
                        continue;
                    }
                    filled[i] = 1;
                    if (hidden) { hidden_only[i] = 1; continue; }
                    s[i].color = uint8_t(((pat >> 9) & 0x30) | c);
                    s[i].prio = (pat >> 15) & 1;
                    if (st) place[i] = uint8_t(count);
                }
            }
            link = sz & 0x7F;
            ++count;
            if (link == 0 || link >= max_sprites) break;
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
                    if (s[i].color && place[i] < hit.place) continue;
                    s[i].color = uint8_t(pal | src[k]);
                    s[i].prio = prio;
                    place[i] = hit.place;
                }
            }
        }
    }

    const bool shi = (reg[12] & 0x08) != 0;
    uint32_t palrgb[64];
    for (int i = 0; i < 64; ++i) palrgb[i] = cram_to_rgb(cram[i]);
    for (int x = 0; x < n; ++x) {
        uint8_t c = 0;
        bool isbg = false;
        // priority: sprite hi > A hi > B hi > sprite lo > A lo > B lo > backdrop
        if (s[x].color && s[x].prio) c = s[x].color;
        else if (a[x].color && a[x].prio) c = a[x].color;
        else if (b[x].color && b[x].prio) c = b[x].color;
        else if (s[x].color) c = s[x].color;
        else if (a[x].color) c = a[x].color;
        else if (b[x].color) c = b[x].color;
        else { c = bgidx; isbg = true; }
        uint32_t rgb = palrgb[c];
        if (shi) {
            bool shadow = !(a[x].prio || b[x].prio);
            if (s[x].color == 0x3E || s[x].color == 0x3F) {
                // operator sprites: recompute top non-sprite layer
                uint8_t u = 0;
                if (a[x].color && a[x].prio) u = a[x].color;
                else if (b[x].color && b[x].prio) u = b[x].color;
                else if (a[x].color) u = a[x].color;
                else if (b[x].color) u = b[x].color;
                else { u = bgidx; }
                rgb = palrgb[u];
                if (s[x].color == 0x3E) {
                    // highlight operator: shadowed -> normal, normal -> highlighted
                    if (!shadow) rgb = 0xFF000000u | (((rgb & 0xFEFEFE) >> 1) + 0x808080);
                    shadow = false;
                } else {
                    shadow = true;  // shadow operator
                }
            } else if (s[x].color && s[x].prio) {
                shadow = false;
            }
            if (shadow) rgb = 0xFF000000u | ((rgb & 0xFEFEFE) >> 1);
        }
        out_rgb[x] = rgb | 0xFF000000u;
        out_bg[x] = isbg ? 1 : 0;
    }
}

// The sprite walk of render_line with only what reaches the status flags:
// the same list, the same per-line and pixel budgets (they grow with the
// rendered width there too) and the original pixels of the native columns.
// tests/test_vdp_widescreen.cpp compares the two on random sprite tables.
void Vdp::sprite_status_line(int line, int extra) {
    if (!display_enabled()) return;
    // Both are only ever set here; reading the status clears them.
    if (sprite_overflow && sprite_collision) return;
    const int width = this->width();
    const int xe = width + extra, n = width + 2 * extra;
    const bool h40 = (reg[12] & 1) != 0;
    const uint32_t sat = h40 ? (uint32_t(reg[5] & 0x7E) << 9) : (uint32_t(reg[5] & 0x7F) << 9);
    const int max_sprites = h40 ? 80 : 64;
    const int max_per_line = h40 ? 20 : 16;
    const int native_pixels = h40 ? 320 : 256;
    const int max_pixels = native_pixels * n / width;
    const int max_per_line_draw = max_per_line * n / width;
    int link = 0, count = 0, on_line = 0, pixels = 0;
    bool nonzero_x_seen = false, masked = false;
    uint8_t solid[kMaxWidth] = {};  // native columns only
    while (count < max_sprites) {
        uint32_t e = sat + uint32_t(link) * 8;
        auto rw = [&](uint32_t o) { return uint16_t((vram[(e + o) & 0xFFFF] << 8) | vram[(e + o + 1) & 0xFFFF]); };
        int sy = (rw(0) & 0x3FF) - 128;
        uint16_t sz = rw(2);
        int hs = ((sz >> 10) & 3) + 1, vsz = ((sz >> 8) & 3) + 1;
        if (line >= sy && line < sy + vsz * 8) {
            uint16_t pat = rw(4);
            const uint16_t raw_x = rw(6);
            int sxr = raw_x & 0x1FF;
            if (h40 && reg[16] == patches::kLevelPlaneSize && extra > 64 &&
                raw_x >= 512 && int(raw_x) < xe + 128)
                sxr = raw_x;
            int sx = sxr - 128;
            if (++on_line > max_per_line) sprite_overflow = true;
            if (on_line > max_per_line_draw) break;
            if (sxr == 0) { if (nonzero_x_seen) masked = true; }
            else nonzero_x_seen = true;
            int row = line - sy;
            if (pat & 0x1000) row = vsz * 8 - 1 - row;
            for (int cx = 0; cx < hs * 8; ++cx) {
                if (pixels >= native_pixels) sprite_overflow = true;
                if (pixels >= max_pixels) break;
                ++pixels;
                if (masked || sprite_collision) continue;
                int x = sx + cx;
                if (x < 0 || x >= width) continue;
                int col = (pat & 0x0800) ? (hs * 8 - 1 - cx) : cx;
                uint32_t tile = (pat & 0x7FF) + uint32_t((col >> 3) * vsz + (row >> 3));
                if (!tile_pixel(vram, tile, col & 7, row & 7)) continue;
                if (solid[x]) sprite_collision = true;
                solid[x] = 1;
            }
        }
        link = sz & 0x7F;
        ++count;
        if (link == 0 || link >= max_sprites) break;
    }
}

} // namespace chaotix
