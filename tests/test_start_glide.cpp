// The glide into a level's start (issue #136): which pictures start a glide,
// where the characters' draws are put on the way, and the composed picture
// against the machine's own buffers; on hand-made draw lists and a machine
// without a ROM.
#include "cpu/sh2/sh2.h"
#include "runtime/patches.h"
#include "runtime/sprite_record.h"
#include "runtime/start_glide.h"
#include "runtime/system.h"
#include "test_framework.h"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace chaotix;

namespace {

SpriteDraw character(int bank, uint8_t colour, int x, int y, int scale) {
    SpriteDraw d;
    d.bank = int16_t(bank);
    d.frame = 0;
    d.colour = colour;
    d.x = x;
    d.y = y;
    d.size = 16;
    if (scale != 0x100) {
        d.kind = SpriteKind::Scaled;
        d.sx = d.sy = scale;
    }
    return d;
}

SpriteList picture(std::initializer_list<SpriteDraw> draws) {
    SpriteList l;
    l.draws = draws;
    return l;
}

} // namespace

TEST(start_glide, a_character_drawn_scaled_then_put_down_glides_there) {
    StartGlide g;
    std::vector<SpriteMove> moves;
    // Circling: the player far off (smaller), drawn scaled.
    CHECK(!g.picture(picture({character(2, 0x12, 200, 70, 0x176)}), moves));
    CHECK(g.watching());
    // Put down 42 pixels lower at full size: the first picture of the glide
    // is still close to where it was.
    CHECK(g.picture(picture({character(2, 0x12, 184, 112, 0x100)}), moves));
    CHECK_EQ(g.glides, 1u);
    CHECK(moves.at(0).on);
    CHECK(std::abs(moves[0].x - 200) <= 1);
    CHECK(moves[0].y < 75);
    CHECK(moves[0].sx > 0x170);
    // On the way it closes in, never overshooting, and ends where the game
    // has it.
    int last_y = moves[0].y, pictures = 1;
    while (g.picture(picture({character(2, 0x12, 184, 112, 0x100)}), moves)) {
        CHECK(moves[0].y >= last_y);
        CHECK(moves[0].y <= 112);
        CHECK(moves[0].sx >= 0x100 && moves[0].sx <= 0x176);
        last_y = moves[0].y;
        ++pictures;
    }
    CHECK_EQ(pictures, StartGlide::kPictures - 1);
    CHECK(!moves[0].on);
    CHECK(!g.watching());
}

TEST(start_glide, in_play_the_partner_scaling_back_to_size_is_no_jump) {
    StartGlide g;
    std::vector<SpriteMove> moves;
    // The partner coming back from the background steps its scale to 0x100.
    for (int s : {0xF0, 0xF4, 0xF8, 0xFC}) CHECK(!g.picture(picture({character(7, 0x24, 150, 100 + (s & 15), s)}), moves));
    CHECK(!g.picture(picture({character(7, 0x24, 150, 94, 0x100)}), moves));
    CHECK_EQ(g.glides, 0u);
    // Nor is a small move with a big change of scale, or a draw that is not
    // a character's.
    g.picture(picture({character(7, 0x24, 150, 100, 0x180)}), moves);
    CHECK(!g.picture(picture({character(7, 0x24, 152, 103, 0x100)}), moves));
    g.picture(picture({character(29, 0, 150, 100, 0x180)}), moves);
    CHECK(!g.picture(picture({character(29, 0, 150, 160, 0x100)}), moves));
    CHECK_EQ(g.glides, 0u);
}

TEST(start_glide, a_characters_draws_move_together_and_one_hidden_starts_at_its_own_size) {
    StartGlide g;
    std::vector<SpriteMove> moves;
    // Espio is two graphics at one hotspot; the player is another character
    // and glides apart. Espio was drawn larger than life (nearer than the
    // whirlwind, hidden in it): he starts at full size, not as a giant.
    g.picture(picture({character(7, 0x24, 122, 88, 0x8A), character(15, 0x24, 122, 88, 0x8A), character(2, 0x12, 198, 72, 0x176)}), moves);
    CHECK(g.picture(picture({character(7, 0x24, 136, 101, 0x100), character(15, 0x24, 136, 101, 0x100),
                             character(10, 0x12, 177, 113, 0x100), character(2, 0x12, 184, 112, 0x100)}),
                    moves));
    CHECK_EQ(g.glides, 2u);
    CHECK(moves[0].on && moves[1].on);
    CHECK_EQ(moves[0].x, moves[1].x);
    CHECK_EQ(moves[0].y, moves[1].y);
    CHECK_EQ(moves[0].sx, 0x100);
    // The player's first draw sets the place; the second keeps its offset,
    // scaled with it.
    CHECK(moves[2].on && moves[3].on);
    CHECK(moves[3].sx > 0x100);
    CHECK(moves[2].x < moves[3].x);
    CHECK(moves[3].x - moves[2].x < 7);
}

TEST(start_glide, a_character_gone_from_the_picture_ends_its_glide) {
    StartGlide g;
    std::vector<SpriteMove> moves;
    g.picture(picture({character(2, 0x12, 200, 70, 0x176)}), moves);
    CHECK(g.picture(picture({character(2, 0x12, 184, 112, 0x100)}), moves));
    CHECK(!g.picture(picture({}), moves));
    CHECK(!g.picture(picture({character(2, 0x12, 184, 112, 0x100)}), moves));
    g.reset();
    CHECK(!g.watching());
}

namespace {

void put16(std::vector<uint8_t>& v, int x) {
    v.push_back(uint8_t(uint16_t(x) >> 8));
    v.push_back(uint8_t(x));
}

// A 4 x 2 block of pixel value 1 with its hotspot at the top-left corner,
// in the game's graphic format (see test_sprite_record.cpp).
std::vector<uint8_t> block() {
    std::vector<uint8_t> g;
    for (int v : {0, 4, 0, 256}) put16(g, v);
    for (int line = 0; line < 2; ++line) {
        g.push_back(0);
        g.push_back(4);
        put16(g, line * 256);
        for (int i = 0; i < 4; ++i) g.push_back(1);
    }
    g.push_back(0);
    g.push_back(0);
    put16(g, 0);
    return g;
}

void be32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v);
}

uint32_t at(int x, int y) { return uint32_t(0x200 + 512 * y + x) & 0x1FFFF; }

// As test_sprite_replace.cpp's: the hooked routines, a native clip
// rectangle, bank 2 (Knuckles) with block() as frame 0, a line table.
std::unique_ptr<Machine> machine() {
    auto m = std::make_unique<Machine>();
    m->rom.data.resize(0x400000);
    m->reset();
    m->m68k.stopped = true;
    auto code = [&](uint32_t pc, uint16_t op) { m->sdram[pc & 0x3FFFF] = uint8_t(op >> 8); m->sdram[(pc & 0x3FFFF) + 1] = uint8_t(op); };
    code(patches::kSh2RecolourCopy, 0x2F46);
    code(patches::kSh2Blit, 0xD91A);
    code(patches::kSh2BlitScaled, 0xD02C);
    m->sdram[0x3836] = 0x01; m->sdram[0x3837] = 0x40;  // right 320
    be32(m->sdram + 0x383C, 223 << 8);
    m->sdram[0x3840] = 0x00; m->sdram[0x3841] = 0x40;  // scale limits
    m->sdram[0x3842] = 0x04; m->sdram[0x3843] = 0x00;
    be32(m->sdram + 0x3614 + 4 * 2, 0x06010000);
    be32(m->sdram + 0x10000, 0x06010200);
    const std::vector<uint8_t> g = block();
    std::copy(g.begin(), g.end(), m->sdram + 0x10200);
    for (int b = 0; b < 2; ++b)
        for (int y = 0; y < 256; ++y) {
            m->mars.fb[b][2 * y] = uint8_t((0x100 + 0x100 * y) >> 8);
            m->mars.fb[b][2 * y + 1] = 0;
        }
    m->mars.adapter_ctrl = 1;
    m->mars.bitmap_mode = 1;
    m->mars.fb_display = m->mars.fb_select_req = 0;
    return m;
}

void flip(Machine& m) {
    m.mars.fb_display ^= 1;
    m.mars.fb_select_req = m.mars.fb_display;
    if (m.sprites) m.sprites->flipped(m);
}

// The player's draw as the game makes it: the recolour copy into the
// scratch buffer (offset 0x12), then the blitter, and its stores.
void draw_player(Machine& m, int x, int y, int scale) {
    sh2::State c{};
    c.r[1] = 0x06010200; c.r[2] = 0x06005310; c.r[3] = 0x12;
    m.sprites->hook(m, c, patches::kSh2RecolourCopy);
    std::vector<uint8_t> copy = block();
    for (size_t i : {12, 13, 14, 15, 20, 21, 22, 23}) copy[i] = 0x13;  // the pixels, 1 + 0x12
    std::copy(copy.begin(), copy.end(), m.sdram + 0x5310);
    c.r[1] = uint32_t(x); c.r[2] = uint32_t(y); c.r[3] = 0; c.r[4] = 0x06005310; c.r[5] = c.r[6] = uint32_t(scale);
    m.sprites->hook(m, c, scale == 0x100 ? patches::kSh2Blit : patches::kSh2BlitScaled);
    const SpriteList& l = m.sprites->list(m.mars.fb_display ^ 1);
    const SpriteDraw& d = l.draws.back();
    CHECK_EQ(d.colour, 0x12);
    CHECK_EQ(d.bank, 2);
    auto store = [](void* user, uint32_t o, uint8_t v) { static_cast<Machine*>(user)->sh2_write(0, 0x24020000u + o, v, 1); };
    CHECK(rasterise_sprite(d, l.bytes(d), d.size, store, &m));
}

// The per-frame clear of the back buffer: an auto fill of every line.
void clear(Machine& m) {
    for (int y = 0; y < 224; ++y) {
        m.sh2_write(0, 0x20004104, 255, 2);
        m.sh2_write(0, 0x20004106, (0x200 + 512 * y) / 2, 2);
        m.sh2_write(0, 0x20004108, 0x0505, 2);
    }
}

const uint8_t* shown(Machine& m) {
    const uint8_t* fb = m.mars.fb[m.mars.fb_display];
    const uint8_t* margin = m.fb_margin[m.mars.fb_display];
    m.sprites->shown(m, fb, margin);
    return fb;
}

} // namespace

TEST(start_glide, the_picture_shows_the_glide_and_the_machine_keeps_its_own) {
    auto m = machine();
    m->sprites = std::make_shared<SpriteRecorder>();
    m->sprites->set_start_glide(true);
    CHECK(!m->sprites->tracks_writes());  // nothing followed until a glide may come
    // Two pictures circling (half size, at the top), then put down.
    for (int i = 0; i < 2; ++i) {
        clear(*m);
        draw_player(*m, 100, 20, 0x200);
        flip(*m);
    }
    CHECK(m->sprites->tracks_writes());
    clear(*m);
    draw_player(*m, 100, 120, 0x100);
    flip(*m);
    const uint8_t* own = m->mars.fb[m->mars.fb_display];
    CHECK_EQ(own[at(100, 120)], 0x13);  // the machine drew it where the game put it
    const uint8_t* fb = shown(*m);
    CHECK(fb != own);
    CHECK_EQ(fb[at(100, 120)], 0x05);  // in the picture, not there yet: the clear shows
    // It is drawn near where it was, at about its old size.
    int top = -1, width = 0;
    for (int y = 0; y < 224 && top < 0; ++y)
        if (fb[at(100, y)] == 0x13) top = y;
    for (int x = 90; x < 120; ++x) width += top >= 0 && fb[at(x, top)] == 0x13;
    CHECK(top >= 20 && top < 30);
    CHECK(width >= 2 && width < 4);
    CHECK_EQ(own[at(100, 20)], 0x05);
    CHECK_EQ(m->sprites->glide().glides, 1u);
    // Then the glide ends on the game's own picture.
    for (int i = 1; i < StartGlide::kPictures; ++i) {
        clear(*m);
        draw_player(*m, 100, 120, 0x100);
        flip(*m);
    }
    CHECK(shown(*m) == m->mars.fb[m->mars.fb_display]);
    CHECK(!m->sprites->tracks_writes());
}
