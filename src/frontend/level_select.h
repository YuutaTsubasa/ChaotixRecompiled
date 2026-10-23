// LEVEL SELECT (issue #51): any level, straight from the front end, the way
// Sonic 2's level select starts one -- no lobby, no Combi Catcher, no draw for
// the time of day.
//
// It is TIME ATTACK's way in (the game's own stage select, applied at its mode
// dispatcher by runtime/stage_select.h) without the clock: nothing is timed,
// recorded or raced, and save states, rewind and fast-forward stay available,
// because this is practice. Like TIME ATTACK it is NO SAVE -- the request sets
// FFE01C to 0xFFFF, so the end of a level does not write a DATA slot -- and
// when the level is over the player comes back to the LEVEL SELECT page
// rather than going on into the game's lobby, which a level started this way
// has no story progress for.
//
// Beyond TIME ATTACK's seven places it offers the hub (WORLD ENTRANCE with the
// five attractions open, the game's own stage select's level 1), the six
// special stages and the bonus stage. Measured on the stage select (all from
// the title screen, Knuckles and Espio):
//   - SPECIAL STAGE levels 0-5 are the six stages (LIMITS 4, 6, 8, 10, 12, 14);
//     6 and 7 are wireframe copies of the first two, not offered.
//   - BONUS STAGE is the same stage whatever the level.
//   - WORLD ENTRANCE 2-7 are the ending's scenes and a black screen; 0 and 1
//     both start the lobby, and 1 is the one the game's stage select opens
//     the five attractions for (stage_select::apply), so only it is offered.
//   - NOT USED (place 8) is not offered.
#pragma once
#include "runtime/stage_select.h"

#include <cstdint>
#include <string>

namespace chaotix {

class Machine;

namespace level_select {

// The places offered, in the game's order.
inline constexpr int kPlaces[] = {0, 1, 2, 3, 4, 5, 6, 7, 9, 10};
inline constexpr int kPlaceCount = int(sizeof kPlaces / sizeof kPlaces[0]);
inline constexpr int kWorldEntrance = 7;
inline constexpr int kBonusStage = 9;
inline constexpr int kSpecialStage = 10;

// The game modes a level select session plays in (game mode, FFDFDE).
inline constexpr uint16_t kModeLevel = 0x18;
inline constexpr uint16_t kModeSpecial = 0x20;
inline constexpr uint16_t kModeBonus = 0x50;

bool offered(int place);
// The levels offered for a place (a subset of what stage_select::has_level
// allows).
bool has_level(int place, int level);
int first_level(int place);
// Steps a place through kPlaces, and a level through the place's levels,
// wrapping; the level follows the place.
int step_place(int place, int step);
int step_level(int place, int level, int step);
// The level as the page shows it: the game's own number for a zone, the
// stage's number (1-6) for a special stage.
std::string level_label(int place, int level);
bool valid(const stage_select::Request& r);

// Following a level select session, once per emulated frame, to know when the
// level is over. The game just moves on when it is, so this watches for the
// scene it moves on to:
//   - a zone's level (places 0-6) is over when the game reaches its lobby
//     (WORLD ENTRANCE, where it sends a player however a level ends);
//   - a special or bonus stage when its game mode is left (a special stage
//     started this way goes on to the title screen);
//   - and any of them, the hub included, when the game leaves the modes it
//     plays in at all: the title screen, a game over.
struct Session {
    bool running = false;
    stage_select::Request request;
    // The scene asked for has been reached, as patches::scene_key has it.
    bool reached = false;
    uint64_t scene = 0;
    uint64_t reached_frame = 0;

    void begin(const stage_select::Request& r);
    void cancel() { running = false; }
    // Returns true on the frame the session ends; running is false after.
    bool update(const Machine& m);
};

} // namespace level_select
} // namespace chaotix
