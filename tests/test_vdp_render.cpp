// The renderer's fast paths and caches (issue #62) against its general paths:
// synthetic VDP and 32X states, no ROM data. The general paths are the ones
// replacement graphics take (TileReplacer) and the lobby's margin palette
// (alt_cram); given nothing to replace and the same palette, they must draw
// exactly what the fast paths draw, line after line, while the state changes
// between lines as a game changes it.
#include "runtime/patches.h"
#include "runtime/system.h"
#include "runtime/tile_replace.h"
#include "runtime/vdp.h"
#include "test_framework.h"
#include <algorithm>
#include <memory>

using namespace chaotix;

namespace {

struct Rng {
    uint32_t s;
    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    int below(int n) { return int(next() % uint32_t(n)); }
};

void randomize(Vdp& v, Rng& r) {
    v.reset();
    for (uint8_t& b : v.vram) b = uint8_t(r.next());
    for (uint16_t& c : v.cram) c = uint16_t(r.next() & 0x0EEE);
    for (uint16_t& s : v.vsram) s = uint16_t(r.next() & 0x07FF);
    v.reg[1] = r.below(16) ? 0x40 : 0x00;  // display mostly on
    v.reg[2] = uint8_t(r.next());
    v.reg[3] = uint8_t(r.next());
    v.reg[4] = uint8_t(r.next());
    v.reg[5] = uint8_t(r.next());
    v.reg[7] = uint8_t(r.next() & 0x3F);
    v.reg[11] = uint8_t(r.next() & 7);  // full, cell and line H-scroll; column V-scroll
    v.reg[12] = uint8_t((r.below(4) ? 0x01 : 0x00) | (r.below(4) ? 0x00 : 0x08));  // H40/H32, shadow/highlight
    v.reg[13] = uint8_t(r.next());
    v.reg[16] = r.below(3) ? patches::kLevelPlaneSize : uint8_t(r.next() & 0x33);
    v.reg[17] = r.below(2) ? 0 : uint8_t(r.next());
    v.reg[18] = r.below(2) ? 0 : uint8_t(r.next());
    // Sprites on screen: most random entries are far off it.
    const bool h40 = v.reg[12] & 1;
    const uint32_t sat = h40 ? (uint32_t(v.reg[5] & 0x7E) << 9) : (uint32_t(v.reg[5] & 0x7F) << 9);
    for (int i = 0; i < (h40 ? 80 : 64); ++i) {
        uint8_t* e = v.vram + sat + i * 8;
        if (r.below(2)) {
            const int y = 128 + r.below(240) - 16;
            e[0] = uint8_t(y >> 8);
            e[1] = uint8_t(y);
        }
        if (r.below(2)) e[3] = uint8_t(r.below(h40 ? 80 : 64));  // the next link
        if (r.below(2)) {
            const int x = 128 + r.below(450) - 64;
            e[6] = uint8_t(x >> 8);
            e[7] = uint8_t(x);
        }
    }
    // The special stage's hidden frame sprites, with a tile in use.
    v.hidden_frame_tile = r.below(3) ? 0xFFFF : uint16_t(((v.vram[sat + 4] << 8) | v.vram[sat + 5]) & 0x7FF);
}

// The same change to both machines, as a game makes between two lines.
void change(Vdp& f, Vdp& g, uint16_t* alt, Rng& r) {
    const bool h40 = f.reg[12] & 1;
    const uint32_t sat = h40 ? (uint32_t(f.reg[5] & 0x7E) << 9) : (uint32_t(f.reg[5] & 0x7F) << 9);
    switch (r.below(6)) {
    case 0: {  // a sprite attribute byte
        const uint32_t a = (sat + uint32_t(r.below(h40 ? 640 : 512))) & 0xFFFF;
        f.vram[a] = g.vram[a] = uint8_t(r.next());
        break;
    }
    case 1: {  // a colour
        const int i = r.below(64);
        f.cram[i] = g.cram[i] = alt[i] = uint16_t(r.next() & 0x0EEE);
        break;
    }
    case 2: {  // V-scroll
        const int i = r.below(40);
        f.vsram[i] = g.vsram[i] = uint16_t(r.next() & 0x07FF);
        break;
    }
    case 3: {  // anything in VRAM: patterns, name tables, H-scroll
        const uint32_t a = r.next() & 0xFFFF;
        f.vram[a] = g.vram[a] = uint8_t(r.next());
        break;
    }
    case 4: {  // the sprite table moves (H-interrupt effects)
        f.reg[5] = g.reg[5] = uint8_t(r.next());
        break;
    }
    default: {  // scroll mode, plane addresses, window
        static const int regs[] = {2, 4, 7, 11, 17, 18};
        const int i = regs[r.below(6)];
        f.reg[i] = g.reg[i] = uint8_t(r.next());
        break;
    }
    }
}

} // namespace

TEST(vdp_render, fast_paths_draw_what_the_general_paths_draw) {
    Rng r{0x2545F491u};
    int compared = 0, differing = 0;
    for (int round = 0; round < 48; ++round) {
        auto f = std::make_unique<Vdp>();
        randomize(*f, r);
        auto g = std::make_unique<Vdp>(*f);
        // Recording tiles takes the general plane and sprite paths; the same
        // palette for the margins the general composition.
        g->tiles = std::make_shared<TileReplacer>();
        g->tiles->dumping = true;
        uint16_t alt[64];
        std::copy(f->cram, f->cram + 64, alt);
        g->alt_cram = alt;
        g->alt_x0 = 0;
        g->alt_x1 = Vdp::kMaxWidth;
        static const int extras[] = {0, 32, 53, 64, 100, Vdp::kMaxExtra};
        const int extra = extras[r.below(6)];
        for (int line = 0; line < 240; ++line) {
            for (int k = r.below(3); k > 0; --k) change(*f, *g, alt, r);
            uint32_t fr[Vdp::kMaxWidth], gr[Vdp::kMaxWidth];
            uint8_t fb[Vdp::kMaxWidth], gb[Vdp::kMaxWidth];
            f->render_line(line, fr, fb, extra);
            g->render_line(line, gr, gb, extra);
            const int n = f->width() + 2 * extra;
            const bool same = std::equal(fr, fr + n, gr) && std::equal(fb, fb + n, gb) &&
                              f->sprite_overflow == g->sprite_overflow && f->sprite_collision == g->sprite_collision;
            ++compared;
            if (!same && differing++ < 4)
                std::printf("    round %d line %d (extra %d) differs\n", round, line, extra);
            f->sprite_overflow = g->sprite_overflow = false;
            f->sprite_collision = g->sprite_collision = false;
        }
    }
    CHECK_EQ(differing, 0);
    CHECK_EQ(compared, 48 * 240);
}

TEST(vdp_render, colour_changes_between_lines_are_drawn) {
    Vdp v;
    v.reset();
    v.reg[1] = 0x40;
    v.reg[12] = 1;
    v.reg[7] = 0x05;  // backdrop: colour 5
    uint32_t rgb[Vdp::kMaxWidth];
    uint8_t bg[Vdp::kMaxWidth];
    v.cram[5] = 0x000E;
    v.render_line(0, rgb, bg, 0);
    CHECK_EQ(rgb[0], 0xFFFF0000u);
    CHECK_EQ(int(bg[0]), 1);
    v.cram[5] = 0x0E00;
    v.render_line(1, rgb, bg, 0);
    CHECK_EQ(rgb[319], 0xFF0000FFu);
    // As CRAM indices: the backdrop bit and the colours of the last line.
    uint8_t top[Vdp::kMaxWidth];
    CHECK(v.render_line_indexed(2, top, rgb, bg, 0));
    CHECK_EQ(int(top[0]), 0x45);
    CHECK_EQ(v.palette_rgb()[5], 0xFF0000FFu);
}

TEST(vdp_render, sprite_table_changes_between_lines_are_drawn) {
    Vdp v;
    v.reset();
    v.reg[1] = 0x40;
    v.reg[5] = 0x70;  // sprite table at E000
    v.reg[12] = 1;
    v.cram[1] = 0x000E;
    std::fill(v.vram + 32, v.vram + 64, uint8_t(0x11));  // tile 1: colour 1
    auto entry = [&](int i, int y, int link, int x) {
        uint8_t* e = v.vram + 0xE000 + i * 8;
        e[0] = uint8_t((y + 128) >> 8);
        e[1] = uint8_t(y + 128);
        e[2] = 0;
        e[3] = uint8_t(link);
        e[4] = 0;
        e[5] = 1;
        e[6] = uint8_t((x + 128) >> 8);
        e[7] = uint8_t(x + 128);
    };
    uint32_t rgb[Vdp::kMaxWidth];
    uint8_t bg[Vdp::kMaxWidth];
    entry(0, 10, 0, 100);
    v.render_line(10, rgb, bg, 0);
    CHECK_EQ(rgb[100], 0xFFFF0000u);
    // Moved down: not on line 11 any more, then back on it.
    entry(0, 20, 0, 100);
    v.render_line(11, rgb, bg, 0);
    CHECK_EQ(rgb[100], 0xFF000000u);
    entry(0, 11, 0, 100);
    v.render_line(11, rgb, bg, 0);
    CHECK_EQ(rgb[100], 0xFFFF0000u);
    // A second sprite linked in after the first, then the link cut again.
    entry(0, 11, 5, 100);
    entry(5, 12, 0, 200);
    v.render_line(12, rgb, bg, 0);
    CHECK_EQ(rgb[100], 0xFFFF0000u);
    CHECK_EQ(rgb[200], 0xFFFF0000u);
    v.vram[0xE000 + 3] = 0;
    v.render_line(13, rgb, bg, 0);
    CHECK_EQ(rgb[200], 0xFF000000u);
    // The table moved (register 5): nothing at the new place.
    v.reg[5] = 0x78;
    v.render_line(12, rgb, bg, 0);
    CHECK_EQ(rgb[100], 0xFF000000u);
}

TEST(compositor, lines_merged_as_drawn_match_the_merge_after_them) {
    auto m = std::make_unique<Machine>();
    m->rom.data.resize(0x400000);
    m->reset();
    Rng r{0x9E3779B9u};
    for (int b = 0; b < 2; ++b) {
        for (int i = 0; i < 0x20000; ++i) m->mars.fb[b][i] = uint8_t(r.next());
        for (int i = 0; i < 0x20000; ++i) m->fb_margin[b][i] = uint8_t(r.next());
    }
    int differing = 0;
    for (int round = 0; round < 400; ++round) {
        m->mars.adapter_ctrl = r.below(8) ? 1 : 0;
        m->mars.bitmap_mode = uint16_t(r.below(4) | (r.below(2) << 7));
        m->mars.screen_shift = uint16_t(r.below(2));
        m->mars.fb_display = uint8_t(r.below(2));
        for (int k = r.below(4); k > 0; --k) m->mars.pal[r.below(256)] = uint16_t(r.next());
        static const int extras[] = {0, 32, 53, patches::kMaxWideExtra};
        const int extra = extras[r.below(4)];
        const bool wide_src = r.below(2) != 0;
        const int ln = r.below(kActiveLines + patches::kMaxWideExtraBottom);
        const int n = kNativeWidth + 2 * extra;
        uint8_t top[Vdp::kMaxWidth];
        uint32_t md_rgb[64];
        for (int x = 0; x < n; ++x) top[x] = uint8_t(r.next() & 0x7F);
        for (uint32_t& c : md_rgb) c = 0xFF000000u | (r.next() & 0xFFFFFF);
        uint32_t encoded[Vdp::kMaxWidth], merged[Vdp::kMaxWidth];
        m->render_line32x(ln, encoded, extra, wide_src);
        m->render_line32x(ln, merged, extra, wide_src, top, md_rgb);
        for (int x = 0; x < n; ++x) {
            const uint32_t p = encoded[x], a = p >> 24;
            const bool show32x = a == 0xFE || (a == 0xFD && (top[x] & 0x40));
            if (merged[x] != (show32x ? (p | 0xFF000000u) : md_rgb[top[x] & 0x3F])) {
                if (differing++ < 4) std::printf("    round %d x %d differs\n", round, x);
                break;
            }
        }
    }
    CHECK_EQ(differing, 0);
}

TEST(compositor, palette_changes_between_lines_are_drawn) {
    auto m = std::make_unique<Machine>();
    m->rom.data.resize(0x400000);
    m->reset();
    m->mars.adapter_ctrl = 1;
    m->mars.fb_display = 0;
    std::fill_n(m->mars.fb[0], 0x20000, uint8_t(7));
    m->mars.fb[0][0] = 1;  // line 0 at word 0x100
    uint32_t row[Vdp::kMaxWidth];
    m->mars.bitmap_mode = 1;
    m->mars.pal[7] = 0x001F;  // red, shown over the backdrop only
    m->render_line32x(0, row, 0, false);
    CHECK_EQ(row[10], 0xFDFF0000u);
    m->mars.pal[7] = 0x801F;  // through bit: in front
    m->render_line32x(0, row, 0, false);
    CHECK_EQ(row[10], 0xFEFF0000u);
    m->mars.bitmap_mode = 0x81;  // priority bit: the other way round
    m->render_line32x(0, row, 0, false);
    CHECK_EQ(row[10], 0xFDFF0000u);
    m->mars.pal[7] = 0x7C00;  // blue
    m->render_line32x(0, row, 0, false);
    CHECK_EQ(row[10], 0xFE0000FFu);
}
