// The game's own stage select, as data.
//
// Knuckles' Chaotix carries a complete stage-select screen (game mode 0x30,
// handler $8F6F6C) that the shipped game never reaches. It offers PLACE,
// LEVEL, AT-TIME, PLAYER, COMBI and PLAYERS, which is exactly what a time
// attack needs, and its "start" routine ($8F72C8, via $8F7352) is the game's
// own way into a level. Everything below is taken from that code and its
// tables rather than guessed at:
//
//   $8F739E   the PLACE names, 0x15 bytes apart, eleven of them
//   $8F738A   one byte per place: bit N set means LEVEL N exists
//   $8F74B1   the AT-TIME names (FFDFF6 masked with 6, so 0/2/4/6)
//   $8F74E8   the character names, in FFE038 / FFE03A order
//   $8F739A   two bytes per PLAYERS setting: the new FFE04C and FFE05C
//
// The variables:
//   FFFBC0  PLACE, copied to FFDFF2 (the zone) when the level starts
//   FFDFF4  LEVEL
//   FFDFF6  AT-TIME
//   FFE038  player's character, multiplied by 4 on the way in
//   FFE03A  partner's character, likewise
//   FFE04C  PLAYERS while the menu is up; on start it selects FFE05C, which
//           is the offset of the controller a player reads ($8A8778), so
//           0x10 is what gives the partner to the second pad
#pragma once
#include <cstdint>
#include <string>

namespace chaotix {

class Machine;

namespace stage_select {

struct Place {
    const char* name;
    uint8_t levels;   // bit N: LEVEL N is a real level
};

// Eleven places; the last three are not ordinary levels and the front end
// offers only kPlayablePlaces of them.
inline constexpr Place kPlaces[] = {
    {"BOTANIC BASE", 0x3E},   {"SPEED SLIDER", 0x3E},  {"AMAZING ARENA", 0x3E},
    {"TECHNO TOWER", 0x3E},   {"MARINA MADNESS", 0x3E}, {"TRAINING", 0x1F},
    {"INTRODUCTION", 0x3F},   {"WORLD ENTRANCE", 0xFF}, {"NOT USED", 0x01},
    {"BONUS STAGE", 0xFF},    {"SPECIAL STAGE", 0xFF},
};
inline constexpr int kPlaceCount = int(sizeof kPlaces / sizeof kPlaces[0]);
// The seven that are a level you can run through and time.
inline constexpr int kPlayablePlaces = 7;

inline constexpr const char* kTimes[] = {"MORNING", "DAY", "SUNSET", "NIGHT"};
inline constexpr int kTimeCount = 4;

// FFE038 / FFE03A order. Slot 1 is a gap in the game's own table (it shows as
// ten asterisks and loads the wrong art), so the front end skips it.
inline constexpr const char* kCharacters[] = {
    "MIGHTY", "**********", "KNUCKLES", "CHARMY BEE", "VECTOR", "BOMB", "HEAVY", "ESPIO",
};
inline constexpr int kCharacterCount = int(sizeof kCharacters / sizeof kCharacters[0]);
inline constexpr int kBrokenCharacter = 1;

// What the game normally starts you with.
inline constexpr int kKnuckles = 2;
inline constexpr int kEspio = 7;

struct Request {
    uint16_t place = 0;
    uint16_t level = 1;
    uint16_t attime = 0;        // 0, 2, 4 or 6
    uint16_t player = kKnuckles;
    uint16_t combi = kEspio;
    bool two_players = false;
};

// The level clock.
//
// FFE052 looks like it, and it does start at zero when play begins -- but the
// player resets it by pressing anything, while the HUD carries on (measured:
// one jump sent FFE052 from 888 back to 1 with the HUD going 0'14"58 ->
// 0'16"41). It was only ever checked against the HUD in runs with no input
// after the level loaded, which is why it passed.
//
// What the HUD draws is the level engine's own frame counter, the long at
// FFE002, less whatever it held when the clock started. FFE052 is still what
// marks that moment: it reads zero through the level's entry sequence, and the
// player cannot press anything to reset it before play begins. So the base is
// taken on the first frame FFE052 is non-zero and FFE052 is not used again.
inline constexpr uint32_t kFrameCounter = 0xE002;   // long
inline constexpr uint32_t kClockStart = 0xE052;     // word, zero until play begins
// FFE002 keeps going while the game's clock is stopped -- pause it and the HUD
// holds still while FFE002 does not. The frames it was stopped for are counted
// in FFAEEC: it does not move while a level is being played and advances by
// exactly the length of a pause across one (measured: +600 for a pause of 600
// frames). So the clock is the difference of the two, and it also stops for
// whatever else stops the game, which is what an end-of-level tally does.
inline constexpr uint32_t kStoppedFrames = 0xAEEC;  // word, wraps
// The HUD draws the time five frames behind the counter (measured against the
// screen at several points), so the same offset is applied when a time is
// written out and the two agree to within a hundredth of a second.
inline constexpr int kHudLag = 5;
// The level's own limit. A run that reaches it was not finished.
inline constexpr int kTimeLimit = 36000;

// m'ss"cc, the way the game writes it.
std::string format_time(int frames);

// Where the game sends you when a run ends, however it ended: its lobby. So a
// run is over once the zone is no longer the one that was asked for.
inline constexpr int kLobbyPlace = 7;

// Whether the place has this level at all, by the game's own table.
bool has_level(int place, int level);
// The first level the place does have, for when a place changes under a level
// number it does not offer.
int first_level(int place);
bool valid(const Request& r);

// Writes the request into 68K work RAM and leaves the game in the level mode,
// doing exactly what $8F72C8 does when Start is pressed on the stage select.
//
// This must run while the game is between scenes: a game mode handler runs its
// own loop and only returns to the dispatcher at $883262 when it is done, so
// the caller is the patch hook there rather than the frame loop.
void apply(Machine& m, const Request& r);

} // namespace stage_select
} // namespace chaotix
