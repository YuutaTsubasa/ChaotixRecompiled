// Ring tether options (issue #43, runtime/tether.h): what each hook does to
// the registers and to work RAM. No ROM needed: the hooks only read and write
// 68K registers and work RAM. The ROM tests in CMakeLists.txt play them.
#include "runtime/tether.h"

#include "cpu/m68k/m68k.h"
#include "runtime/system.h"
#include "test_framework.h"

#include <algorithm>
#include <memory>

using namespace chaotix;

namespace {

constexpr uint32_t kPlayer = 0xAD48, kPartner = 0xAE08;

uint16_t rd16(const Machine& m, uint32_t off) { return uint16_t(m.wram[off] << 8 | m.wram[off + 1]); }
uint32_t rd32(const Machine& m, uint32_t off) { return uint32_t(rd16(m, off)) << 16 | rd16(m, off + 2); }
void wr16(Machine& m, uint32_t off, uint16_t v) {
    m.wram[off] = uint8_t(v >> 8);
    m.wram[off + 1] = uint8_t(v);
}
void wr32(Machine& m, uint32_t off, uint32_t v) {
    wr16(m, off, uint16_t(v >> 16));
    wr16(m, off + 2, uint16_t(v));
}

// BOTANIC BASE 1 in play, one player: Knuckles and Espio on the ground, both
// in play (FFFCEA bits 0 and 2), the partner's buttons the computer's.
std::unique_ptr<Machine> level(uint16_t zone = 0, uint16_t mode = 0x18) {
    auto m = std::make_unique<Machine>();
    wr16(*m, 0xDFDE, mode);
    wr16(*m, 0xDFF2, zone);
    wr16(*m, 0xDFF4, 1);
    wr16(*m, tether::kPlayerObj, kPlayer);
    wr16(*m, tether::kPartnerObj, kPartner);
    m->wram[tether::kPartnerPad] = 0xFE;
    m->wram[tether::kCombiFlags] = 0x05;
    for (uint32_t o : {kPlayer, kPartner}) {
        m->wram[o + 5] = 0x80;  // shown, tangible
        m->wram[o + 7] = 0x02;  // on the ground
        m->wram[o + 0x25] = 0x01;
    }
    wr32(*m, kPlayer + 8, 0x01163800);
    wr32(*m, kPlayer + 0xC, 0x066CE400);
    wr32(*m, kPartner + 8, 0x00D81C00);
    wr32(*m, kPartner + 0xC, 0x066F3800);
    return m;
}

m68k::State regs() {
    m68k::State c{};
    c.a[0] = 0xFFFF0000u | kPlayer;
    c.a[1] = 0xFFFF0000u | kPartner;
    return c;
}

bool wram_equal(const Machine& a, const Machine& b) { return std::equal(a.wram, a.wram + sizeof a.wram, b.wram); }

} // namespace

TEST(tether, strength_scales_the_force_and_normal_leaves_it) {
    using tether::Strength;
    for (uint16_t f : {1, 5, 0x41, 0xFD, 0x100}) CHECK_EQ(tether::scaled_force(Strength::Normal, f), f);
    CHECK_EQ(tether::scaled_force(Strength::Weak, 1), 1u);  // never nothing
    CHECK_EQ(tether::scaled_force(Strength::Weak, 0x41), 0x21u);
    CHECK_EQ(tether::scaled_force(Strength::Weak, 0x100), 0x80u);
    CHECK_EQ(tether::scaled_force(Strength::Strong, 0x41), 0x61u);
    CHECK_EQ(tether::scaled_force(Strength::Strong, 0x100), 0x180u);  // << 3 still fits a word
}

TEST(tether, only_the_attractions_levels_are_in_scope) {
    for (uint16_t z = 0; z < 5; ++z) CHECK(tether::in_scope(*level(z)));
    CHECK(!tether::in_scope(*level(5)));  // TRAINING
    CHECK(!tether::in_scope(*level(6)));  // INTRODUCTION
    CHECK(!tether::in_scope(*level(7)));  // WORLD ENTRANCE
    CHECK(!tether::in_scope(*level(0, 0x38)));  // an attract demo
    CHECK(!tether::in_scope(*level(0, 0x50)));
}

TEST(tether, the_force_hooks) {
    auto m = level();
    auto c = regs();
    // Off: nothing.
    c.d[3] = 0xABCD0041;
    tether::on_force(*m, &c);
    CHECK_EQ(c.d[3], 0xABCD0041u);
    m->tether_strength = uint8_t(tether::Strength::Strong);
    tether::on_force(*m, &c);
    CHECK_EQ(c.d[3], 0xABCD0061u);  // only the low word
    // FOLLOW: nothing for the player, the partner's share given back.
    m->tether_partner = uint8_t(tether::Partner::Follow);
    wr16(*m, tether::kForceLevel, 0x41);
    c.d[3] = 0x41;
    tether::on_force(*m, &c);
    CHECK_EQ(c.d[3], 0u);
    tether::on_partner_side(*m, &c);
    CHECK_EQ(c.d[3], 0x61u << 3);
    // Past the limit FFFCE2 is -1 and the force the last one.
    wr16(*m, tether::kForceLevel, 0xFFFF);
    tether::on_partner_side(*m, &c);
    CHECK_EQ(c.d[3], 0x180u << 3);
    // HOLD: the slingshot is the game's own.
    m->wram[tether::kPlayerPad + 2] = tether::kHoldButton;
    c.d[3] = 0x41;
    tether::on_force(*m, &c);
    CHECK_EQ(c.d[3], 0x61u);
    // Two players: the partner is a person, FOLLOW does nothing.
    m->wram[tether::kPlayerPad + 2] = 0;
    m->wram[tether::kPartnerPad] = 0x10;
    c.d[3] = 0x41;
    tether::on_force(*m, &c);
    CHECK_EQ(c.d[3], 0x61u);
    // Outside the attractions: nothing.
    auto lobby = level(7);
    lobby->tether_strength = uint8_t(tether::Strength::Weak);
    lobby->tether_partner = uint8_t(tether::Partner::Follow);
    c.d[3] = 0x41;
    tether::on_force(*lobby, &c);
    CHECK_EQ(c.d[3], 0x41u);
}

TEST(tether, follow_moves_the_partner_where_the_game_moved_the_player) {
    auto m = level();
    m->tether_partner = uint8_t(tether::Partner::Follow);
    auto c = regs();
    const uint32_t px = rd32(*m, kPlayer + 8), py = rd32(*m, kPlayer + 0xC);
    const uint32_t qx = rd32(*m, kPartner + 8), qy = rd32(*m, kPartner + 0xC);
    tether::on_clamp_before(*m, &c);
    // The game pulls the player 0x40.8 px left and 2 px up.
    wr32(*m, kPlayer + 8, px - 0x408000);
    wr32(*m, kPlayer + 0xC, py - 0x20000);
    tether::on_clamp_after(*m, &c);
    CHECK_EQ(rd32(*m, kPlayer + 8), px);
    CHECK_EQ(rd32(*m, kPlayer + 0xC), py);
    CHECK_EQ(rd32(*m, kPartner + 8), qx + 0x408000);
    CHECK_EQ(rd32(*m, kPartner + 0xC), qy + 0x20000);
    // The game moved the partner itself: left as it is.
    tether::on_clamp_before(*m, &c);
    wr32(*m, kPartner + 8, 0x12345678);
    tether::on_clamp_after(*m, &c);
    CHECK_EQ(rd32(*m, kPartner + 8), 0x12345678u);
    CHECK_EQ(rd32(*m, kPlayer + 8), px);
    // NORMAL: the player stays where the game put it.
    m->tether_partner = 0;
    tether::on_clamp_before(*m, &c);
    wr32(*m, kPlayer + 8, px - 0x10000);
    tether::on_clamp_after(*m, &c);
    CHECK_EQ(rd32(*m, kPlayer + 8), px - 0x10000);
}

TEST(tether, solo_sends_the_partner_away_as_the_game_does) {
    auto m = level();
    m->tether_partner = uint8_t(tether::Partner::Solo);
    // Not while it is still dropping in (intangible).
    m->wram[kPartner + 5] = 0xA0;
    tether::on_chars_update(*m);
    CHECK_EQ(m->wram[kPartner + 7], 0x02);
    m->wram[kPartner + 5] = 0x80;
    tether::on_chars_update(*m);
    CHECK_EQ(m->wram[kPartner + 7], tether::kStateAway);
    CHECK_EQ(rd16(*m, kPartner + 0x32), 0xFFFFu);  // for good, as the Combi Catcher has it
    CHECK_EQ(rd16(*m, kPartner + 0x34), 0u);
    CHECK_EQ(m->wram[kPartner + 5] & 0x20, 0x20);  // intangible
    CHECK_EQ(m->wram[kPartner + 0x25] & 1, 0);
    CHECK_EQ(m->wram[tether::kCombiFlags], 0x04);  // not both in play: single camera, no tether
    // Away: a frame later nothing else changes.
    const auto before = std::make_unique<Machine>(*m);
    tether::on_chars_update(*m);
    CHECK(wram_equal(*m, *before));
    // A tap of CALL is not enough; held on its own for half a second it is.
    m->wram[tether::kPlayerPad + 2] = tether::kCallButton;
    wr16(*m, tether::kPlayerPad + 6, 29);
    tether::on_chars_update(*m);
    CHECK_EQ(rd16(*m, kPartner + 0x32), 0xFFFFu);
    m->wram[tether::kPlayerPad + 2] = tether::kCallButton | 0x20;  // with C
    wr16(*m, tether::kPlayerPad + 6, 40);
    tether::on_chars_update(*m);
    CHECK_EQ(rd16(*m, kPartner + 0x32), 0xFFFFu);
    m->wram[tether::kPlayerPad + 2] = tether::kCallButton | 0x08;  // with a direction
    tether::on_chars_update(*m);
    CHECK(rd16(*m, kPartner + 0x32) != 0xFFFFu);  // on its way back
    CHECK(m->tether_called);
    // Back in play, it stays for the rest of this life...
    m->wram[kPartner + 7] = 0x02;
    m->wram[kPartner + 5] = 0x80;
    m->wram[tether::kCombiFlags] = 0x05;
    tether::on_chars_update(*m);
    CHECK_EQ(m->wram[kPartner + 7], 0x02);
    // ...until the level starts again (the object is created anew).
    m->wram[kPartner + 7] = 0x00;
    tether::on_chars_update(*m);
    CHECK(!m->tether_called);
    m->wram[kPartner + 7] = 0x02;
    tether::on_chars_update(*m);
    CHECK_EQ(m->wram[kPartner + 7], tether::kStateAway);
    // Or the next scene.
    m->tether_called = true;
    tether::on_scene_change(*m);
    CHECK(!m->tether_called);
}

TEST(tether, solo_keeps_away_and_lets_go) {
    auto m = level();
    m->tether_partner = uint8_t(tether::Partner::Solo);
    // Held by an object, hurt, flying back after CALL: wait.
    for (uint8_t st : {uint8_t(0x0E), uint8_t(0x12), tether::kStateCalled}) {
        m->wram[kPartner + 7] = st;
        tether::on_chars_update(*m);
        CHECK_EQ(m->wram[kPartner + 7], st);
    }
    // Knocked away by the game (a hit without rings): kept away.
    m->wram[kPartner + 7] = tether::kStateAway;
    wr16(*m, kPartner + 0x32, 2);
    tether::on_chars_update(*m);
    CHECK_EQ(rd16(*m, kPartner + 0x32), 0xFFFFu);
    // Out of harm's way.
    wr16(*m, kPartner + 0x30, 3);
    tether::on_chars_update(*m);
    CHECK(rd16(*m, kPartner + 0x30) >= 0x20u);
    // SOLO off: it comes back.
    m->tether_partner = 0;
    tether::on_chars_update(*m);
    CHECK(rd16(*m, kPartner + 0x32) != 0xFFFFu);
    // Two players: never sent away.
    auto two = level();
    two->tether_partner = uint8_t(tether::Partner::Solo);
    two->wram[tether::kPartnerPad] = 0x10;
    tether::on_chars_update(*two);
    CHECK_EQ(two->wram[kPartner + 7], 0x02);
    // Nor in the lobby, whose Combi Catcher keeps partners away itself.
    auto lobby = level(7);
    lobby->tether_partner = 0;
    lobby->wram[kPartner + 7] = tether::kStateAway;
    wr16(*lobby, kPartner + 0x32, 0xFFFF);
    tether::on_chars_update(*lobby);
    CHECK_EQ(rd16(*lobby, kPartner + 0x32), 0xFFFFu);
}

TEST(tether, objects_that_wait_for_both_take_the_solo_player) {
    auto m = level();
    auto c = regs();
    const uint32_t obj = 0xB548;
    c.a[6] = 0xFFFF0000u | obj;
    const tether::BothObject lift = tether::kBothObjects[1];  // TECHNO TOWER's lift
    m->wram[obj + lift.counter] = 1;
    // Not SOLO: as the game counts.
    tether::on_both_object(*m, &c, lift.pc);
    CHECK_EQ(m->wram[obj + lift.counter], 1);
    m->tether_partner = uint8_t(tether::Partner::Solo);
    // SOLO with the partner still in play: as the game counts.
    tether::on_both_object(*m, &c, lift.pc);
    CHECK_EQ(m->wram[obj + lift.counter], 1);
    m->wram[kPartner + 7] = tether::kStateAway;
    tether::on_both_object(*m, &c, lift.pc);
    CHECK_EQ(m->wram[obj + lift.counter], 2);
    // Nobody in it: nothing.
    m->wram[obj + lift.counter] = 0;
    tether::on_both_object(*m, &c, lift.pc);
    CHECK_EQ(m->wram[obj + lift.counter], 0);
    // The one that counts in +$2D.
    const tether::BothObject other = tether::kBothObjects[5];
    CHECK_EQ(other.counter, 0x2D);
    m->wram[obj + 0x2D] = 1;
    tether::on_both_object(*m, &c, other.pc);
    CHECK_EQ(m->wram[obj + 0x2D], 2);
}

TEST(tether, off_changes_nothing) {
    auto m = level();
    const auto before = std::make_unique<Machine>(*m);
    auto c = regs();
    c.d[0] = 0x1234;
    c.d[1] = 0x5678;
    c.d[3] = 0x41;
    c.a[6] = 0xFFFFB548;
    m->wram[0xB548 + 0x2C] = 1;
    before->wram[0xB548 + 0x2C] = 1;
    const m68k::State c0 = c;
    tether::on_chars_update(*m);
    tether::on_force(*m, &c);
    tether::on_partner_side(*m, &c);
    tether::on_clamp_before(*m, &c);
    tether::on_clamp_after(*m, &c);
    for (const tether::BothObject& b : tether::kBothObjects) tether::on_both_object(*m, &c, b.pc);
    CHECK(wram_equal(*m, *before));
    CHECK(std::equal(c.d, c.d + 8, c0.d));
    CHECK(std::equal(c.a, c.a + 8, c0.a));
    CHECK(!tether::any(*m));
}
