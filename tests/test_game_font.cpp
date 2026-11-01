// The game's menu font (issue #8): the Nemesis decoder on hand-built
// streams, the atlas built from hand-built tiles, and with the ROM, the real
// font and a menu backdrop rendered on a machine of its own.
#include "renderer/game_font.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "frontend/backdrop.h"
#include "game/recomp_dispatch.h"
#include "renderer/image_io.h"
#include "runtime/system.h"
#include "runtime/vdp.h"
#include "test_framework.h"

using namespace chaotix;

namespace {

// Writes bits most significant first.
struct BitWriter {
    std::vector<uint8_t> bytes;
    int used = 8;
    void put(uint32_t v, int n) {
        for (int i = n - 1; i >= 0; --i) {
            if (used == 8) { bytes.push_back(0); used = 0; }
            if ((v >> i) & 1) bytes.back() |= uint8_t(0x80 >> used);
            ++used;
        }
    }
};

// The simplest valid Nemesis stream for any tiles: an empty code table and
// every nybble as an inline run of one (111111, count - 1 in three bits, the
// nybble in four). XOR mode stores each row as its difference from the last.
std::vector<uint8_t> encode_inline(const std::vector<uint8_t>& tiles, bool xor_mode) {
    const size_t count = tiles.size() / 32;
    std::vector<uint8_t> out = {uint8_t((xor_mode ? 0x80 : 0) | (count >> 8)), uint8_t(count), 0xFF};
    BitWriter w;
    uint32_t previous = 0;
    for (size_t r = 0; r < count * 8; ++r) {
        uint32_t row = uint32_t(tiles[r * 4]) << 24 | uint32_t(tiles[r * 4 + 1]) << 16 |
                       uint32_t(tiles[r * 4 + 2]) << 8 | tiles[r * 4 + 3];
        const uint32_t stored = xor_mode ? row ^ previous : row;
        previous = row;
        for (int n = 7; n >= 0; --n) {
            w.put(0x3F, 6);
            w.put(0, 3);
            w.put((stored >> (n * 4)) & 0xF, 4);
        }
    }
    out.insert(out.end(), w.bytes.begin(), w.bytes.end());
    return out;
}

// 94 tiles in which glyph g's pixels are all g % 15 + 1, except a transparent
// left column, so each glyph is told apart by its colour.
std::vector<uint8_t> numbered_tiles() {
    std::vector<uint8_t> tiles(size_t(kGameFontTileCount) * 32);
    for (int t = 0; t < kGameFontTileCount; ++t) {
        const uint8_t v = uint8_t((t / 2) % 15 + 1);
        for (int y = 0; y < 8; ++y) {
            tiles[size_t(t) * 32 + size_t(y) * 4] = v;  // pixel 0 is 0, pixel 1 is v
            for (int b = 1; b < 4; ++b) tiles[size_t(t) * 32 + size_t(y) * 4 + size_t(b)] = uint8_t(v << 4 | v);
        }
    }
    return tiles;
}

std::array<uint16_t, 16> grey_palette() {
    std::array<uint16_t, 16> p{};
    for (int i = 0; i < 16; ++i) p[size_t(i)] = uint16_t(((i & 7) << 1) * 0x111);
    return p;
}

} // namespace

TEST(game_font, nemesis_decodes_a_code_table) {
    // Nybble 0 is "0" and nybble 1 is "1", each a run of a whole row (8).
    const std::vector<uint8_t> stream = {0x00, 0x01, 0x80, 0x71, 0x00, 0x81, 0x71, 0x01, 0xFF, 0x55};
    std::vector<uint8_t> tiles;
    size_t used = 0;
    std::string why;
    CHECK(nemesis_decode(stream.data(), stream.size(), tiles, &used, &why));
    CHECK_EQ(tiles.size(), size_t(32));
    CHECK_EQ(used, stream.size());
    for (int row = 0; row < 8; ++row)
        for (int b = 0; b < 4; ++b) CHECK_EQ(int(tiles[size_t(row) * 4 + size_t(b)]), row & 1 ? 0x11 : 0x00);
}

TEST(game_font, nemesis_inline_runs_and_xor_mode) {
    std::vector<uint8_t> tiles(3 * 32);
    for (size_t i = 0; i < tiles.size(); ++i) tiles[i] = uint8_t(i * 37 + (i >> 3));
    for (bool xor_mode : {false, true}) {
        const std::vector<uint8_t> stream = encode_inline(tiles, xor_mode);
        std::vector<uint8_t> out;
        std::string why;
        CHECK(nemesis_decode(stream.data(), stream.size(), out, nullptr, &why));
        CHECK(out == tiles);
    }
}

TEST(game_font, nemesis_refuses_broken_streams) {
    std::vector<uint8_t> tiles(32, 0x12);
    std::vector<uint8_t> stream = encode_inline(tiles, false);
    std::vector<uint8_t> out;
    std::string why;
    // Cut short: the bits run out.
    CHECK(!nemesis_decode(stream.data(), stream.size() - 4, out, nullptr, &why));
    CHECK(!why.empty());
    // A code the (empty) table does not have: eight zero bits.
    const std::vector<uint8_t> unknown = {0x00, 0x01, 0xFF, 0x00, 0x00};
    CHECK(!nemesis_decode(unknown.data(), unknown.size(), out, nullptr, &why));
    // No tiles at all, or absurdly many.
    const std::vector<uint8_t> none = {0x00, 0x00, 0xFF};
    CHECK(!nemesis_decode(none.data(), none.size(), out, nullptr, &why));
    const std::vector<uint8_t> many = {0x7F, 0xFF, 0xFF};
    CHECK(!nemesis_decode(many.data(), many.size(), out, nullptr, &why, 2048));
}

TEST(game_font, atlas_from_tiles_keeps_the_order_and_colours) {
    GameFont f;
    std::string why;
    const auto palette = grey_palette();
    CHECK(game_font_from_tiles(numbered_tiles(), palette, f, &why));
    CHECK_EQ(f.glyph_count, 53);  // 47 from the ROM, ':' '<' '>' '/' '[' ']' made from them
    CHECK_EQ(f.width, 53 * GameFont::kGlyphW);
    CHECK_EQ(f.height, GameFont::kGlyphH + GameFont::kSmallGlyphH);
    CHECK_EQ(f.glyph_for('0'), 0);
    CHECK_EQ(f.glyph_for('9'), 9);
    CHECK_EQ(f.glyph_for('A'), 10);
    CHECK_EQ(f.glyph_for('a'), 10);  // capitals only
    CHECK_EQ(f.glyph_for('Z'), 35);
    CHECK_EQ(f.glyph_for('?'), 36);
    CHECK_EQ(f.glyph_for('.'), 40);
    CHECK_EQ(f.glyph_for('['), 51);
    CHECK_EQ(f.glyph_for(']'), 52);
    CHECK_EQ(f.glyph_for('%'), -1);
    // Glyph g is drawn in palette colour g % 15 + 1, its left column clear,
    // both its tiles (the top one, then the one below).
    for (int g : {0, 10, 35, 46}) {
        const uint32_t want = Vdp::cram_to_rgb(palette[size_t(g % 15 + 1)]);
        CHECK_EQ(f.pixels[size_t(g * 8 + 1)], want);
        CHECK_EQ(f.pixels[size_t(15 * f.width + g * 8 + 7)], want);
        CHECK_EQ(f.pixels[size_t(g * 8)], 0u);
    }
    CHECK(f.supports("V-SYNC"));
    CHECK(f.supports("4:3"));
    CHECK(f.supports("Resume"));
    CHECK(!f.supports("65%"));
    CHECK(f.supports("Up/Down"));
    CHECK(!f.supports("(1)"));
    CHECK_EQ(f.width_of("BACK TO TITLE"), 13 * 8);
    // Too few tiles is not a font.
    GameFont none;
    CHECK(!game_font_from_tiles(std::vector<uint8_t>(64), palette, none, &why));
}

TEST(game_font, colon_is_the_full_stop_twice) {
    std::vector<uint8_t> tiles(size_t(kGameFontTileCount) * 32, 0);
    // The full stop (glyph 40): a 2x2 dot of colour 3 on the bottom tile's
    // rows 5 and 6, columns 2 and 3.
    for (int y = 5; y <= 6; ++y) tiles[size_t(81) * 32 + size_t(y) * 4 + 1] = 0x33;
    GameFont f;
    std::string why;
    CHECK(game_font_from_tiles(tiles, grey_palette(), f, &why));
    const int colon = f.glyph_for(':');
    CHECK(colon >= 47);
    auto px = [&f](int g, int x, int y) { return f.pixels[size_t(y) * size_t(f.width) + size_t(g * 8 + x)]; };
    int dots = 0;
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 8; ++x) dots += px(colon, x, y) != 0;
    CHECK_EQ(dots, 8);
    CHECK(px(colon, 2, 13) != 0);  // where the full stop's dot is
    CHECK(px(colon, 2, 7) != 0);   // and the one above it
    CHECK(px(colon, 2, 10) == 0);
}

TEST(game_font, the_small_glyphs_fold_each_pair_of_rows) {
    // A glyph of colour 3 on rows 1, 3 and 5 only (one-row strokes, the
    // rows between them empty) and the shadow colour 5 on row 15. Folding
    // each pair keeps every stroke, and the shadow stays the bottom row.
    std::vector<uint8_t> tiles(size_t(kGameFontTileCount) * 32, 0);
    for (int y : {1, 3, 5}) tiles[size_t(y) * 4] = 0x03;  // glyph 0, top tile, column 1
    tiles[size_t(1) * 32 + 7 * 4] = 0x05;                  // its bottom tile's last row
    GameFont f;
    std::string why;
    CHECK(game_font_from_tiles(tiles, grey_palette(), f, &why));
    auto small = [&f](int x, int y) { return f.pixels[size_t(GameFont::kGlyphH + y) * size_t(f.width) + size_t(x)]; };
    const auto palette = grey_palette();
    for (int y = 0; y < 3; ++y) CHECK_EQ(small(1, y), Vdp::cram_to_rgb(palette[3]));
    CHECK_EQ(small(1, 3), 0u);
    CHECK_EQ(small(1, 7), Vdp::cram_to_rgb(palette[5]));
}

#ifdef CHAOTIX_TEST_ROM
TEST(game_font, the_rom_has_the_font) {
    Rom rom;
    std::string err;
    CHECK(rom.load(CHAOTIX_TEST_ROM, &err));
    GameFont f;
    CHECK(build_game_font(rom.data, f, &err));
    CHECK_EQ(f.glyph_count, 53);
    CHECK_EQ(f.width, 424);
    CHECK_EQ(f.height, 24);
    // White digits, yellow letters, as the TRAINING screen shows them.
    auto ink = [&f](char c) {
        std::set<uint32_t> colours;
        const int g = f.glyph_for(static_cast<unsigned char>(c));
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 8; ++x)
                if (const uint32_t p = f.pixels[size_t(y) * size_t(f.width) + size_t(g * 8 + x)]) colours.insert(p);
        return colours;
    };
    CHECK(ink('0').count(0xFFFFFFFFu) == 1);
    const auto a = ink('A');
    CHECK(a.size() >= 2);
    bool yellow = false;
    for (uint32_t p : a) yellow |= ((p >> 16) & 0xFF) > 200 && ((p >> 8) & 0xFF) > 150 && (p & 0xFF) < 80;
    CHECK(yellow);
    // A ROM without it there is refused, not misread.
    std::vector<uint8_t> other = rom.data;
    other[kGameFontTiles] ^= 0x01;
    GameFont none;
    CHECK(!build_game_font(other, none, &err));
}

TEST(game_font, a_backdrop_renders_beside_a_playing_machine) {
    // The menu's picture is made on a thread, on a machine of its own, while
    // the game plays on the recompiled one. The game must not notice: its
    // frames are the same as on a run with no picture being made.
    Rom rom;
    std::string err;
    CHECK(rom.load(CHAOTIX_TEST_ROM, &err));
    auto play = [&rom](bool with_backdrop, BackdropImage* image) {
        auto m = std::make_unique<Machine>();
        m->load_rom(Rom(rom));
        m->reset();
        install_recompiled_code(*m, true);
        std::thread worker;
        std::atomic<bool> done{false};
        bool ok = false;
        if (with_backdrop) {
            worker = std::thread([&] {
                std::string why;
                ok = render_backdrop(rom, backdrop_specs().front(), *image, &why);
                done = true;
            });
        }
        uint64_t h = 0;
        for (int f = 0; f < 700; ++f) {
            m->input.pad[0] = (f >= 500 && f < 504) ? uint16_t(0x80) : uint16_t(0);
            m->run_frame();
            h = h * 1099511628211ull ^ image_hash(m->framebuffer, m->fb_width, m->fb_height, kScreenWidth);
        }
        if (worker.joinable()) worker.join();
        CHECK(!with_backdrop || (done && ok));
        return h;
    };
    BackdropImage image;
    const uint64_t alone = play(false, nullptr);
    const uint64_t beside = play(true, &image);
    CHECK_EQ(beside, alone);
    CHECK_EQ(image.width, 320 + 2 * kBackdropWideExtra);
    CHECK_EQ(image.height, 224);
    // A picture, not one flat colour.
    std::set<uint32_t> colours(image.pixels.begin(), image.pixels.end());
    CHECK(colours.size() > 32);
    CHECK((backdrop_tint(image) >> 24) == 0xFF);
}

TEST(game_font, a_paused_backdrop_makes_no_progress_and_resumes) {
    // While the game is played the picture's machine is held between two of
    // its frames, so it takes no CPU from the game; it then carries on where
    // it was, and the picture is the same as one made in one go.
    Rom rom;
    std::string err;
    CHECK(rom.load(CHAOTIX_TEST_ROM, &err));
    auto wait_ms = [](int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); };
    BackdropControl control;
    control.pause = true;  // "playing" from the start
    BackdropImage paused_image;
    bool ok = false;
    std::thread worker([&] {
        std::string why;
        ok = render_backdrop(rom, backdrop_specs().front(), paused_image, &why, &control);
    });
    wait_ms(300);
    CHECK_EQ(control.frames.load(), uint64_t(0));  // not one frame while held
    control.pause = false;                         // a menu opened
    for (int i = 0; i < 400 && control.frames.load() < 60; ++i) wait_ms(5);
    CHECK(control.frames.load() >= 60);
    control.pause = true;                          // back to playing
    wait_ms(50);                                   // the frame in progress ends
    const uint64_t held = control.frames.load();
    wait_ms(300);
    CHECK_EQ(control.frames.load(), held);
    control.pause = false;
    worker.join();
    CHECK(ok);
    BackdropImage straight;
    std::string why;
    CHECK(render_backdrop(rom, backdrop_specs().front(), straight, &why));
    CHECK(straight.pixels == paused_image.pixels);

    // Cancelling wakes a held worker at once.
    BackdropControl stop;
    stop.pause = true;
    BackdropImage none;
    bool cancelled_ok = true;
    std::thread held_worker([&] {
        std::string w;
        cancelled_ok = render_backdrop(rom, backdrop_specs().front(), none, &w, &stop);
    });
    wait_ms(50);
    stop.cancel = true;
    held_worker.join();
    CHECK(!cancelled_ok);
    CHECK_EQ(stop.frames.load(), uint64_t(0));
}

TEST(game_font, the_game_own_zones_render_as_backdrops) {
    // Issue #37: WORLD ENTRANCE (the lobby, kept 4:3) and ISOLATED ISLAND
    // (the opening level, wide) are pictures like the attractions: the place
    // itself, not a blank or a stray scene, with the characters, the tether
    // and the HUD left out.
    Rom rom;
    std::string err;
    CHECK(rom.load(CHAOTIX_TEST_ROM, &err));
    struct Want { const char* zone; int width; };
    for (const Want& want : {Want{"WorldEntrance", 320}, Want{"IsolatedIsland", 320 + 2 * kBackdropWideExtra}}) {
        const int index = backdrop_find_zone(want.zone);
        CHECK(index >= 0);
        if (index < 0) continue;
        BackdropImage image;
        std::string why;
        CHECK(render_backdrop(rom, backdrop_specs()[size_t(index)], image, &why));
        CHECK_EQ(image.width, want.width);
        CHECK_EQ(image.height, 224);
        // Many colours, none of them covering most of the picture (a blank
        // or a flat scene such as a fade has one colour nearly everywhere).
        std::map<uint32_t, size_t> count;
        for (uint32_t p : image.pixels) ++count[p & 0xFFFFFF];
        size_t most = 0;
        for (const auto& [colour, n] : count) most = std::max(most, n);
        CHECK(count.size() > 16);  // 32 and 60 here; a fade or a blank has a handful
        CHECK(most * 4 < image.pixels.size());
        // Each quarter of the picture has something in it: not a black band.
        for (int q = 0; q < 4; ++q) {
            std::set<uint32_t> colours;
            for (int y = (q / 2) * 112; y < (q / 2 + 1) * 112; ++y)
                for (int x = (q % 2) * image.width / 2; x < (q % 2 + 1) * image.width / 2; ++x)
                    colours.insert(image.pixels[size_t(y) * size_t(image.width) + size_t(x)] & 0xFFFFFF);
            CHECK(colours.size() > 8);
        }
        // The characters were in the frame and were left out of it.
        CHECK(image.hidden_draws > 0);
    }
}
#endif
