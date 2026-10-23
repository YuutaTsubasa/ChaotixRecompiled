// Ring tether options (issue #43). See tether.h and docs/RING_TETHER.md.
#include "runtime/tether.h"
#include "cpu/m68k/m68k.h"
#include "runtime/patches.h"
#include "runtime/system.h"

namespace chaotix::tether {

namespace {

uint16_t rd16(const Machine& m, uint32_t off) {
    off &= 0xFFFF;
    return uint16_t(m.wram[off] << 8 | m.wram[(off + 1) & 0xFFFF]);
}
void wr16(Machine& m, uint32_t off, uint16_t v) {
    off &= 0xFFFF;
    m.wram[off] = uint8_t(v >> 8);
    m.wram[(off + 1) & 0xFFFF] = uint8_t(v);
}
uint32_t rd32(const Machine& m, uint32_t off) { return uint32_t(rd16(m, off)) << 16 | rd16(m, off + 2); }
void wr32(Machine& m, uint32_t off, uint32_t v) {
    wr16(m, off, uint16_t(v >> 16));
    wr16(m, off + 2, uint16_t(v));
}
void set_low_word(uint32_t& r, uint32_t v) { r = (r & 0xFFFF0000u) | (v & 0xFFFFu); }

constexpr uint16_t kModeLevel = 0x18;
constexpr unsigned kAttractions = 5;  // BOTANIC BASE .. MARINA MADNESS

// Object fields (docs/CHARACTERS.md section 3).
constexpr uint32_t kObjPriority = 0x04;  // byte: draw priority
constexpr uint32_t kObjFlags = 0x05;     // bit 7 shown, bit 5 intangible
constexpr uint32_t kObjState = 0x07;
constexpr uint32_t kObjX = 0x08, kObjY = 0x0C;  // 16.16
constexpr uint32_t kObjYVel = 0x1C;
constexpr uint32_t kObjStatus = 0x25;    // bit 0 on the ground
constexpr uint32_t kObjInvuln = 0x30;    // frames a hit is ignored ($88FBBA)
constexpr uint32_t kObjAwaySpeed = 0x32; // state $14: how fast the return comes, -1 never
constexpr uint32_t kObjAwayPhase = 0x34; // state $14: phase in bits 2-3 of the high byte

// A partner the game, or SOLO, keeps away for good (the Combi Catcher's way).
constexpr uint16_t kAwayForGood = 0xFFFF;
// What the return is given when it may come (+$34 advances by this each
// frame: 4 frames to the reappearing phase).
constexpr uint16_t kReturnSpeed = 0x0100;
// How long CALL must be held in SOLO to bring the partner back.
constexpr uint16_t kCallHoldFrames = 30;

bool hold_held(const Machine& m) { return m.wram[kPlayerPad + 2] & kHoldButton; }

bool follow(const Machine& m) {
    return Partner(m.tether_partner) == Partner::Follow && in_scope(m) && cpu_partner(m) && !hold_held(m);
}

// States the partner may be sent away from: playing on its own. Not while
// it is created (0), held by an object ($0E) or hurt ($12), already away
// ($14) or flying back after CALL ($1A).
bool can_send_away(uint8_t state) {
    return state != 0x00 && state != 0x0E && state != 0x12 && state != kStateAway && state != kStateCalled;
}

// $88FECE with d0 = -1, which the Combi Catcher uses ($8A92AE): the partner
// shrinks and falls out of play, then waits hidden until +$32 lets it back.
void send_away(Machine& m, uint32_t partner, uint32_t player) {
    m.wram[kCombiFlags] &= uint8_t(~0x01);
    m.wram[partner + kObjFlags] |= 0x20;
    wr32(m, partner + kObjYVel, 0xFFFB0000u);
    m.wram[partner + kObjStatus] &= uint8_t(~0x01);
    m.wram[partner + kObjState] = kStateAway;
    wr16(m, partner + kObjAwayPhase, 0);
    wr16(m, partner + kObjInvuln, 0x00FF);
    wr16(m, partner + kObjAwaySpeed, kAwayForGood);
    m.wram[partner + kObjPriority] = 0x34;
    m.wram[player + kObjPriority] = 0x44;
}

} // namespace

bool in_scope(const Machine& m) {
    return rd16(m, patches::kGameMode) == kModeLevel && rd16(m, patches::kSceneZone) < kAttractions;
}

bool cpu_partner(const Machine& m) { return m.wram[kPartnerPad] & 0x80; }

uint16_t scaled_force(Strength s, uint16_t force) {
    switch (s) {
    case Strength::Weak: return uint16_t((force + 1) / 2);
    case Strength::Strong: return uint16_t(force + force / 2);
    default: return force;
    }
}

bool any(const Machine& m) { return m.tether_strength != 0 || m.tether_partner != 0; }

void on_force(Machine& m, m68k::State* c) {
    if (!any(m) || !in_scope(m)) return;
    uint16_t d3 = scaled_force(Strength(m.tether_strength), uint16_t(c->d[3]));
    // FOLLOW: the player's share of the pull is nothing (on_partner_side
    // gives the partner its own back).
    if (follow(m)) d3 = 0;
    set_low_word(c->d[3], d3);
}

void on_partner_side(Machine& m, m68k::State* c) {
    if (!follow(m)) return;
    // The force as $891392 made it: the byte + 1 the game kept in FFFCE2, or
    // the last one (0xFF + 1) past the limit, where FFFCE2 is -1.
    const int16_t level = int16_t(rd16(m, kForceLevel));
    const uint16_t force = level < 0 ? 0x100 : uint16_t(level);
    set_low_word(c->d[3], uint32_t(scaled_force(Strength(m.tether_strength), force)) << 3);
}

void on_clamp_before(Machine& m, m68k::State* c) {
    m.tether_clamp_saved = false;
    if (!follow(m)) return;
    const uint32_t player = c->a[0] & 0xFFFF;
    if (player != rd16(m, kPlayerObj) || (c->a[1] & 0xFFFF) != rd16(m, kPartnerObj)) return;
    m.tether_clamp_x = rd32(m, player + kObjX);
    m.tether_clamp_y = rd32(m, player + kObjY);
    m.tether_clamp_saved = true;
}

void on_clamp_after(Machine& m, m68k::State* c) {
    if (!m.tether_clamp_saved) return;
    m.tether_clamp_saved = false;
    const uint32_t player = c->a[0] & 0xFFFF, partner = c->a[1] & 0xFFFF;
    const uint32_t dx = rd32(m, player + kObjX) - m.tether_clamp_x;
    const uint32_t dy = rd32(m, player + kObjY) - m.tether_clamp_y;
    if (!dx && !dy) return;  // the game moved the partner itself
    // The player goes back where it was and the partner moves the other way,
    // so they end up exactly as far apart as $891BDA left them.
    wr32(m, player + kObjX, m.tether_clamp_x);
    wr32(m, player + kObjY, m.tether_clamp_y);
    wr32(m, partner + kObjX, rd32(m, partner + kObjX) - dx);
    wr32(m, partner + kObjY, rd32(m, partner + kObjY) - dy);
}

void on_both_object(Machine& m, m68k::State* c, uint32_t pc) {
    if (Partner(m.tether_partner) != Partner::Solo || !in_scope(m) || !cpu_partner(m)) return;
    const uint32_t partner = rd16(m, kPartnerObj);
    if (!partner || m.wram[partner + kObjState] != kStateAway) return;
    for (const BothObject& b : kBothObjects) {
        if (b.pc != pc) continue;
        uint8_t& count = m.wram[(c->a[6] + b.counter) & 0xFFFF];
        if (count == 1) count = 2;  // the player is in: as if both were
        return;
    }
}

void on_chars_update(Machine& m) {
    if (!in_scope(m) || !cpu_partner(m)) return;
    const uint32_t player = rd16(m, kPlayerObj), partner = rd16(m, kPartnerObj);
    if (!player || !partner || player == partner) return;
    const uint8_t state = m.wram[partner + kObjState];
    const bool away = state == kStateAway && rd16(m, partner + kObjAwaySpeed) == kAwayForGood;
    if (Partner(m.tether_partner) != Partner::Solo) {
        // Not (or no longer) SOLO: a partner it kept away comes back. In the
        // attractions only SOLO keeps one away for good.
        if (away) wr16(m, partner + kObjAwaySpeed, kReturnSpeed);
        return;
    }
    if (state == 0x00) {  // created: a level (re)starts
        m.tether_called = false;
        return;
    }
    if (m.tether_called) return;
    if (away) {
        // Out of harm's way: a hit on a partner out of play would end the
        // life ($88FC0C), and nothing should reach it, but make sure.
        if (rd16(m, partner + kObjInvuln) < 0x20) wr16(m, partner + kObjInvuln, 0x00FF);
        // CALL held on its own for half a second (+6 of the button record
        // counts the frames the buttons have stayed as they are, $890598)
        // brings it back, free, for the rest of this life. Held, not
        // pressed, so that a habit of tapping CALL does not end SOLO.
        const uint8_t held = m.wram[kPlayerPad + 2];
        const uint16_t steady = rd16(m, kPlayerPad + 6);
        const uint8_t ps = m.wram[player + kObjState];
        if ((held & ~0x0F) == kCallButton && steady >= kCallHoldFrames && ps != kStateAway && ps != kStateCalled) {
            wr16(m, partner + kObjAwaySpeed, kReturnSpeed);
            m.tether_called = true;
        }
        return;
    }
    if (state == kStateAway) {
        // The game sent it away (a hit without rings): keep it there.
        wr16(m, partner + kObjAwaySpeed, kAwayForGood);
        return;
    }
    // Only once it is in play: tangible (the level's opening drop and the
    // game's own flights set +5 bit 5) and in a state of its own.
    if (can_send_away(state) && !(m.wram[partner + kObjFlags] & 0x20)) send_away(m, partner, player);
}

void on_scene_change(Machine& m) { m.tether_called = false; }

} // namespace chaotix::tether
