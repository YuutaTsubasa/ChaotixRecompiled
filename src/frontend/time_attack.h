// Following a time attack run.
//
// The game has no notion of one: when a run ends -- the goal, the level's own
// ten-minute limit, anything -- it simply moves the player on, to its lobby or
// to the next level. So a run is watched from outside: while the zone and
// level are the ones that were asked for it is still going, and the level
// clock at the last moment they were is the time.
//
// Kept out of the frontend's window code so it can be driven by a test with a
// Machine and nothing else.
#pragma once
#include "runtime/stage_select.h"

#include <cstdint>

namespace chaotix {

class Machine;

namespace time_attack {

struct Run {
    bool running = false;
    stage_select::Request request;
    // Elapsed level-clock frames at the last moment the run was still going.
    int time = 0;
    // Whether the clock has begun, and the two readings it is stepped from:
    // the level engine's frame counter and the count of frames the clock was
    // stopped for (see stage_select.h).
    bool clock_started = false;
    uint32_t last_counter = 0;
    unsigned last_stopped = 0;
    // The level has been reached at least once. Until then a mismatch only
    // means the game has not arrived yet.
    bool started = false;

    void begin(const stage_select::Request& r);
    void cancel() { running = false; }
    // Call once per emulated frame. Returns true on the frame the run ends,
    // with time holding the result; running is false afterwards.
    bool update(const Machine& m);
    // A run that reached the level's own limit was not finished, so it is not
    // a time.
    bool timed_out() const { return time >= stage_select::kTimeLimit; }
};

} // namespace time_attack
} // namespace chaotix
