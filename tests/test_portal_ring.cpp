// The game's giant ring for the menu's cursor (issue #134): the unpacker on
// hand-built graphics, the turn's frames, and with the ROM, the ring
// unpacked exactly as the game unpacks it into SDRAM.
#include "renderer/portal_ring.h"

#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "game/recomp_dispatch.h"
#include "runtime/system.h"
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

// A packed graphic: the box -4..4 by lines -2..1, and two spans on lines
// -2 and 0 (columns -4..0 and 0..2) with pixels 0-3 on a base of 4, so 0
// (transparent) stays 0.
std::vector<uint8_t> packed(uint16_t size) {
    // Columns: base -4, 4 bits. Lines: base -2, 2 bits. Pixels: base 0, 3 bits.
    std::vector<uint8_t> out = {uint8_t(size >> 8), uint8_t(size), 'B', uint8_t(-4), 4, uint8_t(-2), 2, 0, 3};
    BitWriter w;
    w.put(8, 4);  // right edge 4
    w.put(3, 2);  // bottom line 1
    w.put(0, 4); w.put(4, 4); w.put(0, 2);           // span -4..0 on line -2
    for (int p : {1, 0, 2, 3}) w.put(uint32_t(p), 3);
    w.put(4, 4); w.put(6, 4); w.put(2, 2);           // span 0..2 on line 0
    for (int p : {5, 0}) w.put(uint32_t(p), 3);
    w.put(7, 4); w.put(7, 4);                        // the end
    out.insert(out.end(), w.bytes.begin(), w.bytes.end());
    return out;
}

} // namespace

TEST(portal_ring, unpacks_a_graphic_as_the_game_does) {
    const std::vector<uint8_t> want = {
        0xFF, 0xFC, 0x00, 0x04, 0xFE, 0x00, 0x01, 0x00,  // box: -4, 4, -2 x 256, 1 x 256
        0xFC, 0x00, 0xFE, 0x00, 0x11, 0x00, 0x12, 0x13,  // -4..0 on -2: 1 0 2 3, plus the colour 0x10
        0x00, 0x02, 0x00, 0x00, 0x15, 0x00,              // 0..2 on 0: 5 0
        0x00, 0x00,                                      // the end
    };
    const std::vector<uint8_t> in = packed(uint16_t(want.size()));
    std::vector<uint8_t> out;
    std::string why;
    CHECK(unpack_sprite_graphic(in.data(), in.size(), 0x10, out, &why));
    CHECK(out == want);
}

TEST(portal_ring, refuses_what_is_not_a_packed_graphic) {
    std::vector<uint8_t> in = packed(24), out;
    std::string why;
    // The size in the header must be what comes out.
    std::vector<uint8_t> wrong = packed(26);
    CHECK(!unpack_sprite_graphic(wrong.data(), wrong.size(), 0, out, &why));
    // Not marked 'B' (the game copies those as they are; the ring is packed).
    in[2] = 'A';
    CHECK(!unpack_sprite_graphic(in.data(), in.size(), 0, out, &why));
    // Cut short.
    in = packed(24);
    CHECK(!unpack_sprite_graphic(in.data(), in.size() - 3, 0, out, &why));
    CHECK(!unpack_sprite_graphic(in.data(), 5, 0, out, &why));
}

TEST(portal_ring, a_turn_goes_edge_on_and_back_twice) {
    bool mirrored = true;
    CHECK_EQ(portal_ring_frame(0.0f, &mirrored), 0);
    CHECK(!mirrored);
    // A quarter turn is edge on, then the ring turns back mirrored.
    CHECK_EQ(portal_ring_frame(0.249f, &mirrored), PortalRing::kFrames - 1);
    CHECK(!mirrored);
    CHECK_EQ(portal_ring_frame(0.251f, &mirrored), PortalRing::kFrames - 1);
    CHECK(mirrored);
    CHECK_EQ(portal_ring_frame(0.499f, &mirrored), 0);
    CHECK(mirrored);
    // Half a turn on it looks the same, and so does a whole turn on.
    std::set<int> seen;
    for (int i = 0; i < 200; ++i) {
        const float t = float(i) / 200.0f;
        bool a = false, b = false, c = false;
        const int f = portal_ring_frame(t, &a);
        CHECK_EQ(portal_ring_frame(t + 0.5f, &b), f);
        CHECK_EQ(portal_ring_frame(t - 1.0f, &c), f);
        CHECK(a == b && a == c);
        seen.insert(f);
    }
    CHECK_EQ(seen.size(), size_t(PortalRing::kFrames));
}

#ifdef CHAOTIX_TEST_ROM
TEST(portal_ring, the_rom_ring_is_what_the_game_unpacks) {
    Rom rom;
    std::string err;
    CHECK(rom.load(CHAOTIX_TEST_ROM, &err));
    PortalRing ring;
    CHECK(build_portal_ring(rom.data, ring, &err));
    CHECK_EQ(ring.width, PortalRing::kCellW * PortalRing::kFrames);
    CHECK_EQ(ring.height, PortalRing::kCellH);
    // Front on, the ring is gold: its widest row has yellow in it, and it is
    // the same upside down.
    auto px = [&ring](int x, int y) { return ring.pixels[size_t(y) * size_t(ring.width) + size_t(x)]; };
    bool yellow = false;
    for (int x = 0; x < PortalRing::kCellW; ++x) {
        const uint32_t p = px(x, PortalRing::kCellH / 2);
        yellow |= ((p >> 16) & 0xFF) > 200 && ((p >> 8) & 0xFF) > 150 && (p & 0xFF) < 80;
        for (int y = 0; y < PortalRing::kCellH; ++y) CHECK_EQ(px(x, y), px(x, PortalRing::kCellH - 1 - y));
    }
    CHECK(yellow);
    // Edge on it is narrow: nothing at the cell's sides.
    const int edge = (PortalRing::kFrames - 1) * PortalRing::kCellW;
    for (int y = 0; y < PortalRing::kCellH; ++y) CHECK_EQ(px(edge + 2, y), 0u);

    // In BOTANIC BASE 1 the game has unpacked the bank into SDRAM: the
    // frames there are byte for byte what unpack_sprite_graphic makes.
    auto m = std::make_unique<Machine>();
    CHECK(m->load_rom(CHAOTIX_TEST_ROM, &err));
    m->reset();
    install_recompiled_code(*m, true);
    for (int f = 0; f < 900; ++f) {
        if (f == 300) { m->stage_request.place = 0; m->stage_request.level = 1; m->stage_pending = true; }
        m->input.pad[0] = (f >= 300 && f < 400 && (f % 24) < 6) ? 0x80 : 0;
        m->run_frame();
    }
    auto be32 = [](const uint8_t* p) {
        return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
    };
    auto sdram = [&m](uint32_t a) { return m->sdram + (a & 0x3FFFF); };
    const uint32_t table = be32(m->sdram + 0x3614 + 4 * kPortalRingBank);
    CHECK_EQ(table >> 24 & 0xDF, 0x06u);  // unpacked into SDRAM, not drawn from the ROM
    const uint32_t rom_table = be32(rom.data.data() + 0x0A0000 + 4 * kPortalRingBank) - 0x02000000;
    for (int f = kPortalRingFirstFrame; f < kPortalRingFirstFrame + PortalRing::kFrames; ++f) {
        const uint32_t at = be32(rom.data.data() + rom_table + 4 * uint32_t(f)) - 0x02000000;
        std::vector<uint8_t> g;
        CHECK(unpack_sprite_graphic(rom.data.data() + at, rom.data.size() - at, 0, g, &err));
        const uint8_t* game = sdram(be32(sdram(table + 4 * uint32_t(f))));
        CHECK(std::vector<uint8_t>(game, game + g.size()) == g);
    }

    // A ROM without it there is refused, not misread.
    std::vector<uint8_t> other = rom.data;
    other[0x0A0000 + 4 * kPortalRingBank] ^= 0x40;
    PortalRing none;
    CHECK(!build_portal_ring(other, none, &err));
    CHECK(none.empty());
}
#endif
