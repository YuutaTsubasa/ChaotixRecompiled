// Lobby shortcuts (issue #44): the partner and the time of day, chosen.
//
// In story mode the game picks both for the player. Found in the 68K code
// (see docs/LOBBY_SHORTCUTS.md for the evidence):
//
//  - The partner is FFE03A (character id x 4, like FFE038 for the player).
//    In WORLD ENTRANCE the Combi Catcher ($8A8644: press Down on its box)
//    puts the partner in a pen with every character but the player's --
//    Mighty, Knuckles, Charmy, Vector, Bomb, Heavy, Espio -- and whichever
//    one the claw comes up with becomes the partner ($8A9960 -> $88F85E).
//    The partner is not in the save; the player is.
//  - The time of day is FFDFF6. Everything that draws a time reads bits 1-2
//    (MORNING, DAY, SUNSET, NIGHT: `and #6` or `lsr #1; and #3`); bit 0 is a
//    half step. Every level finished adds one ($890F72), so the time moves
//    on every second level, and the save keeps FFDFF6 & 7 ($8BE590).
//
// With the shortcut on, the choice is written into those two variables each
// time the game starts the lobby or one of the five attractions, at the mode
// dispatcher ($883262) -- the same point and the same way TIME ATTACK starts
// a stage (stage_select::apply), before the scene loads its characters and
// palettes. Both values are ones the game itself produces (any character but
// the player's can come out of the catcher, and every time comes round), so
// what is saved is a save the game could have written: the time's half step
// is kept and the level end goes on counting as before, and the DATA slot
// (FFE01C) is not touched, so progress saves as usual.
#pragma once
#include <cstdint>

namespace chaotix {

class Machine;

namespace lobby {

constexpr uint32_t kPartner = 0xE03A;  // word: character id x 4
constexpr uint32_t kPlayer = 0xE038;   // word: character id x 4
constexpr uint32_t kTime = 0xDFF6;     // word: bits 1-2 the time of day, bit 0 a half step

constexpr int kTimeCount = 4;          // MORNING, DAY, SUNSET, NIGHT (stage_select::kTimes)

// Whether the scene the dispatcher is about to start is one the choice is put
// into: the lobby (WORLD ENTRANCE level 0) or a level of the five attractions
// (places 0-4), in the level mode. INTRODUCTION and TRAINING keep their own
// characters, and the special and bonus stages are not levels.
bool takes_choice(const Machine& m);

// Writes Machine::lobby_partner and lobby_time into the game when
// takes_choice. A partner that is the player's own character, or slot 1 (no
// character), is left to the catcher; -1 leaves either alone. Called by the
// patch hook at the mode dispatcher, on every machine alike, so lockstep
// still compares like with like.
void apply(Machine& m);

} // namespace lobby
} // namespace chaotix
