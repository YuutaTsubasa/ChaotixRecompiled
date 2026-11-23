// Cheats (issue #20): Action Replay style codes, i.e. 68K work RAM bytes the
// host sets before every frame. Game Genie codes are not supported: they
// patch ROM bytes, mostly instructions, and the game runs as statically
// recompiled code, so a patched instruction would not be the one executed.
//
// The built-in list is the subset of the community's codes (Sonic Retro's
// Knuckles' Chaotix page) that was checked against this game; the player can
// add their own in Config/chaotix.ini. While any cheat is on, achievements
// and time attack records are not earned.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace chaotix {
class Machine;
}

namespace chaotix::cheats {

// Byte of 68K work RAM ($FF0000-$FFFFFF), by the low word of its address.
struct Write {
    uint16_t addr;
    uint8_t value;
    // false: the byte is set to `value`. true: it is only raised to it, so a
    // count that is already higher (Chaos Rings go on to 11) is left alone.
    bool at_least = false;
};

struct Cheat {
    const char* key;   // in the config file
    const char* name;  // in the menu
    const char* note;
    std::vector<Write> writes;
};

const std::vector<Cheat>& builtin();

// "FFE009:63", "FFFFEC:FF00" (a word: two bytes), several joined with '+',
// spaces or commas. Addresses must be in $FF0000-$FFFFFF. False, with
// nothing added, if any part is not a code.
bool parse_codes(const std::string& text, std::vector<Write>& out);

// Before each frame.
void apply(Machine& m, const std::vector<Write>& writes);

} // namespace chaotix::cheats
