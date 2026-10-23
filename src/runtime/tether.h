// Ring tether options (issue #43): tether strength, an auto-following
// partner, and solo play. See docs/RING_TETHER.md for how the Combi Ring
// works in the 68K code and how each option was found and measured.
//
// The Combi Ring in brief (all 68K, the fixed window $880000-$8FFFFF):
//
//  - The player's object is at [FFE034], the partner's at [FFE036] (FFAD48
//    and FFAE08 in a level). Both run the same character code ($889C10, one
//    state machine on object byte +7); the player reads its buttons from the
//    record at FFE04C, the partner from FFE05C. In one-player games FFE05C's
//    first byte is negative and the partner's buttons are made up by
//    $8904D8: the player's own, 16 frames late (a ring at FFFD00), without
//    A and START, or "walk to the player" while FFFCFD counts down.
//  - The tether ($8912C4-$891560, once a frame while FFFCEA bit 2 is set):
//    the distance between the two (FFFCDE, plus FFFCDA/FFFCDC when the ring
//    is caught on a post at FFFCF0) against the table at $891B06: a rest
//    length of 100 px, a stretch limit of 75 px, then one force byte per
//    pixel of stretch (4 x stretch, 0xFF from 64 px). The force,
//    (byte + 1) x 8, pulls each character towards the other along the
//    tether: velocity when airborne, ground speed when grounded, or, while
//    a character holds a direction, a pull it moves with ($2E, read at
//    $88B71E). Past the limit ($891BDA) one of them is simply put back on
//    the circle of 175 px: the partner if the player is grounded and the
//    partner airborne, else the player.
//  - HOLD (B by default, FFE04E bit 4) keeps the CPU partner where it is
//    ($889E1A) and leaves the pull on the player alone: walk away and let go
//    for the slingshot ($891428 launches a grounded character along the
//    tether when the pull points up from the ground).
//  - CALL (A, $891B72) costs 10 rings and flies the partner back as a ball
//    (state $1A, $88FF5A / $88AB7C).
//  - FFFCEA bit 0 says both characters are in play. Without it the camera
//    follows the player alone ($89111E), the tether, CALL and HOLD do
//    nothing, and a hit without rings is fatal ($88FC0C). The game clears it
//    while the partner is away: state $14 ($88FECE), in which the partner
//    shrinks and falls, waits hidden and intangible, and comes back to the
//    player's side. The Combi Catcher in the lobby keeps a partner away in
//    that state for as long as it likes (word +$32 = -1, $8A92AE).
//
// The options, each off by default (the original game, byte for byte):
//
//  - STRENGTH (WEAK / NORMAL / STRONG): the force is scaled (x1/2, x3/2)
//    just before $8913A2 turns it into velocity. The 175 px limit stays.
//  - PARTNER FOLLOW (one player only): the tether no longer pulls the
//    player, unless HOLD is held (the slingshot still works); it pulls the
//    partner as before, and past the limit the partner, never the player, is
//    put back on the circle. So the partner keeps up and the
//    player is never held back. ($891BDA has three ways of doing that,
//    with the ring caught on no post, one or two: whichever moved the player,
//    the player is put back and the partner moved by as much instead, which
//    leaves them as far apart as the game made them.)
//  - PARTNER SOLO (one player only): the partner is sent away the way the
//    game itself does it and kept away (state $14, +$32 = -1, as the Combi
//    Catcher does): hidden, intangible, no tether, single camera. Objects
//    that wait for both characters to stand in them count the player twice
//    (kBothObjects). CALL held for half a second brings the partner back
//    for the rest of that life (the game's own return), for anything else
//    that needs two.
//
// Everything is applied by patch hooks (runtime/patches.h) on the 68K, which
// the interpreter and the recompiled code call at the same instructions, and
// depends only on machine state and these settings, so lockstep holds.
// Applied in the five attractions' levels only (game mode $18, zones 0-4):
// not in the lobby, TRAINING, INTRODUCTION, the bonus or special stages.
#pragma once
#include <cstdint>

namespace m68k {
struct State;
}

namespace chaotix {

class Machine;

namespace tether {

enum class Strength : uint8_t { Normal = 0, Weak = 1, Strong = 2 };
enum class Partner : uint8_t { Normal = 0, Follow = 1, Solo = 2 };
inline constexpr int kStrengthCount = 3;
inline constexpr int kPartnerCount = 3;
// Menu and config names, indexed by the enums.
inline constexpr const char* kStrengthNames[] = {"NORMAL", "WEAK", "STRONG"};
inline constexpr const char* kPartnerNames[] = {"NORMAL", "FOLLOW", "SOLO"};

// 68K addresses (the hooks are in patches::kM68kHooks).
enum : uint32_t {
    kCharsUpdate = 0x889C10,    // start of the characters' frame (st FFFCCF)
    kForce = 0x8913A2,          // lsl.w #3,d3: d3 = force byte + 1
    kPartnerSide = 0x891494,    // the player's share is done, the partner's next
    kClampBefore = 0x89138E,    // bsr $891BDA: puts one back on the circle past the limit
    kClampAfter = 0x891392,     // back from it
};

// Objects that wait for both characters to stand in them: each counts the
// characters in contact ($88F980, which skips one with object +5 bit 5, as an
// absent partner has) into a byte and compares it with 2 here. Found by
// looking for that pattern; see docs/RING_TETHER.md for where they are.
struct BothObject { uint32_t pc; uint8_t counter; };
inline constexpr BothObject kBothObjects[] = {
    {0x896566, 0x2C}, {0x898C48, 0x2C}, {0x898E6C, 0x2C},
    {0x89A2C8, 0x2C}, {0x89FF7A, 0x2C}, {0x8A3266, 0x2D},
};

// Work RAM (offsets in FF0000-FFFFFF).
inline constexpr uint32_t kPlayerObj = 0xE034;   // word: the player's object
inline constexpr uint32_t kPartnerObj = 0xE036;  // word: the partner's object
inline constexpr uint32_t kPlayerPad = 0xE04C;   // player's buttons: +2 held, +3 pressed
inline constexpr uint32_t kPartnerPad = 0xE05C;  // partner's: first byte negative = CPU
inline constexpr uint32_t kCombiFlags = 0xFCEA;  // bit 0 both in play, bit 2 tether on
inline constexpr uint32_t kForceLevel = 0xFCE2;  // the force byte + 1, -1 past the limit
inline constexpr uint8_t kHoldButton = 0x10;     // logical B (after the button map)
inline constexpr uint8_t kCallButton = 0x40;     // logical A
inline constexpr uint8_t kStateAway = 0x14;      // the partner sent away
inline constexpr uint8_t kStateCalled = 0x1A;    // the partner flying back after CALL

// The settings apply here: a level of the five attractions, in play.
bool in_scope(const Machine& m);
// One player: the partner's buttons are the computer's.
bool cpu_partner(const Machine& m);
// The force scaled for the strength setting (d3 before the lsl, 1..0x100).
uint16_t scaled_force(Strength s, uint16_t force);
// Whether any setting differs from the original game.
bool any(const Machine& m);

// The hooks (called from patches::m68k_hook).
void on_chars_update(Machine& m);
void on_force(Machine& m, m68k::State* c);
void on_partner_side(Machine& m, m68k::State* c);
void on_clamp_before(Machine& m, m68k::State* c);
void on_clamp_after(Machine& m, m68k::State* c);
void on_both_object(Machine& m, m68k::State* c, uint32_t pc);
// The mode dispatcher: a new scene, so a partner called back in SOLO goes
// away again in the next level.
void on_scene_change(Machine& m);

} // namespace tether
} // namespace chaotix
