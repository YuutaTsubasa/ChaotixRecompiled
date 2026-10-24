#include "runtime/patches.h"
#include "runtime/vdp.h"
#include "test_framework.h"
#include <array>
#include <algorithm>

using namespace chaotix;

namespace {
constexpr int line = 64;
constexpr uint32_t sat = 0xE000;
void word(Vdp& v, uint32_t at, uint16_t value) {
    v.vram[at] = uint8_t(value >> 8);
    v.vram[at + 1] = uint8_t(value);
}
Vdp scene() {
    Vdp v;
    v.reset();
    v.reg[1] = 0x40;
    v.reg[2] = 0x30; // plane A at C000
    v.reg[4] = 5;    // plane B at A000
    v.reg[5] = 0x70; // sprites at E000
    v.reg[12] = 1;
    v.reg[16] = patches::kLevelPlaneSize;
    v.cram[1] = 0x000E;
    std::fill(v.vram + 32, v.vram + 64, uint8_t(0x11));
    return v;
}
void sprite(Vdp& v, int index, uint16_t raw_x, int next = 0) {
    const uint32_t at = sat + uint32_t(index) * 8;
    word(v, at, line + 128);
    word(v, at + 2, uint16_t(next));
    word(v, at + 4, 1);
    word(v, at + 6, raw_x);
}
std::array<uint32_t, Vdp::kMaxWidth> render(Vdp& v, int extra) {
    std::array<uint32_t, Vdp::kMaxWidth> rgb{};
    uint8_t backdrop[Vdp::kMaxWidth]{};
    v.render_line(line, rgb.data(), backdrop, extra);
    return rgb;
}
constexpr uint32_t red = 0xFFFF0000u, black = 0xFF000000u;
}

TEST(vdp_widescreen, ring_at_392_is_visible_only_in_the_extended_view) {
    for (int extra : {0, 53, 64, 80}) {
        auto v = scene();
        sprite(v, 0, 392 + 128);
        auto rgb = render(v, extra);
        if (extra == 80) {
            CHECK_EQ(rgb[extra + 392], red);
            CHECK_EQ(rgb[extra + 399], red);
        } else {
            CHECK(std::all_of(rgb.begin(), rgb.begin() + 320 + 2 * extra,
                              [](uint32_t px) { return px == black; }));
        }
    }
}

TEST(vdp_widescreen, first_extended_coordinate_is_not_a_sprite_mask) {
    auto v = scene();
    sprite(v, 0, 80 + 128, 1);
    sprite(v, 1, 384 + 128, 2);
    sprite(v, 2, 160 + 128);
    auto rgb = render(v, 80);
    CHECK_EQ(rgb[80 + 80], red);
    CHECK_EQ(rgb[80 + 384], red);
    CHECK_EQ(rgb[80 + 160], red);
    CHECK(!v.sprite_collision);
    CHECK(!v.sprite_overflow);

    for (int extra : {0, 64}) {
        auto native = render(v, extra);
        CHECK_EQ(native[extra + 80], red);
        CHECK_EQ(native[extra + 160], black); // raw512 retains native X0 masking
    }
}

TEST(vdp_widescreen, ordinary_and_negative_positions_keep_native_decoding) {
    for (uint16_t raw_x : {uint16_t(64), uint16_t(208), uint16_t(511), uint16_t(0x80D0), uint16_t(0xFFD0)}) {
        auto v = scene();
        sprite(v, 0, raw_x);
        auto rgb = render(v, 80);
        const int x = (raw_x & 0x1FF) - 128;
        CHECK_EQ(rgb[80 + x], red);
    }
}

TEST(vdp_widescreen, high_bits_and_menu_planes_do_not_enable_extra_coordinates) {
    for (uint16_t raw_x : {uint16_t(0x8208), uint16_t(0xFE08), uint16_t(528)}) {
        auto v = scene();
        sprite(v, 0, raw_x);
        auto rgb = render(v, 80);
        CHECK(std::all_of(rgb.begin(), rgb.begin() + 480,
                          [](uint32_t px) { return px == black; }));
    }
    auto v = scene();
    v.reg[16] = 0x11; // menu/transition plane size
    sprite(v, 0, 520);
    auto rgb = render(v, 80);
    CHECK_EQ(rgb[80 + 392], black);
}

TEST(vdp_widescreen, actual_zero_coordinate_still_masks_later_sprites) {
    auto v = scene();
    sprite(v, 0, 208, 1);
    sprite(v, 1, 0, 2);
    sprite(v, 2, 520);
    auto rgb = render(v, 80);
    CHECK_EQ(rgb[80 + 80], red);
    CHECK_EQ(rgb[80 + 392], black);
}

TEST(vdp_widescreen, a_hidden_frame_sprite_is_not_drawn_but_still_collides) {
    // Issue #21: the special stage frames its picture with black 2x4-cell
    // sprites at X 0 and 304; widened, those are hidden. One must not cover
    // a sprite behind it, and must still set the collision flag the game reads.
    for (bool hide : {false, true}) {
        auto v = scene();
        v.cram[17] = 0x00E0;  // palette 1, colour 1: green
        const uint32_t frame = sat, behind = sat + 8;
        word(v, frame, line + 128);
        word(v, frame + 2, (1 << 10) | (3 << 8) | 1);  // 2x4 cells, next = 1
        word(v, frame + 4, 1);
        word(v, frame + 6, 0 + 128);
        word(v, behind, line + 128);
        word(v, behind + 2, 0);
        word(v, behind + 4, 0x2000 | 1);
        word(v, behind + 6, 4 + 128);
        if (hide) v.hidden_frame_tile = 1;
        auto rgb = render(v, 53);
        CHECK(v.sprite_collision);
        CHECK_EQ(rgb[53 + 2], hide ? black : red);
        CHECK_EQ(rgb[53 + 6], hide ? 0xFF00FF00u : red);
    }
}

TEST(vdp_widescreen, bonus_stage_rings_hide_plane_b_in_the_margins) {
    // Issue #52: entered from a level, the bonus stage keeps the level's VRAM
    // beyond its own 40 columns of plane B, high-priority cells included. In
    // the margins the rings (low priority, opaque) must not be covered by
    // them; in the 4:3 picture plane B is drawn as the game set it.
    auto v = scene();
    v.cram[33] = 0x00E0;  // palette 2, colour 1: green
    const uint16_t high_b = 0x8000 | 0x4000 | 1;
    for (int col : {10, 42, 63}) word(v, 0xA000 + 2 * uint32_t((line / 8) * 64 + col), high_b);
    uint8_t lut[64];
    std::fill(std::begin(lut), std::end(lut), uint8_t(1));  // red rings
    v.radial_lut = lut;
    v.radial_len = 64;
    v.radial_cx2 = 320;
    v.radial_cy2 = 224;
    auto rgb = render(v, 53);
    CHECK_EQ(rgb[53 + 10 * 8], 0xFF00FF00u);  // native: plane B's own cell
    CHECK_EQ(rgb[53 + 42 * 8], red);          // right margin
    CHECK_EQ(rgb[53 - 4], red);               // left margin (column 63)
    CHECK_EQ(rgb[53 + 200], black);           // native: plane A as drawn
}
