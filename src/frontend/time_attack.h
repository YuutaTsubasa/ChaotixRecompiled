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
    // Elapsed level-clock frames, as the game itself reckons them.
    int time = 0;
    // The level has been reached at least once. Until then a mismatch only
    // means the game has not arrived yet.
    bool started = false;
    // The goal has been reached, so the time is what it is: the game holds it
    // on its results screen while everything else runs on. The clock settles a
    // few frames after the flag, so it is counted down rather than stopped.
    bool finished = false;
    int settling = 0;
    // Whether the goal flag was already set when this run reached the level,
    // so one left over from whatever came before cannot end it.
    bool goal_was_set = false;
    // Frames the game's clock was not counting (see stage_select.h), summed as
    // differences because the game's own counter of them wraps.
    unsigned stopped_total = 0;
    unsigned last_stopped = 0;
    // Where this level's clock started, and the state of working that out: how
    // many consecutive frames FFE052 has climbed by one, and what it read last.
    bool have_start = false;
    uint32_t start_at = 0;
    int climbing = 0;
    unsigned last_since = 0;

    void begin(const stage_select::Request& r);
    void cancel() { running = false; }
    // Call once per emulated frame. Returns true on the frame the run ends,
    // with time holding the result; running is false afterwards.
    bool update(const Machine& m);
    // A run that reached the level's own limit was not finished, so it is not
    // a time.
    bool timed_out() const { return time >= stage_select::kTimeLimit; }
    // Whether this run was timed at all. Introduction's clock never starts --
    // it is a tutorial, and nothing in it counts -- so a run there has no time
    // rather than a time of zero.
    bool timed() const { return have_start; }
};

} // namespace time_attack
} // namespace chaotix
