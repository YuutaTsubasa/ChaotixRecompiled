// See time_attack.h.
#include "frontend/time_attack.h"
#include "runtime/system.h"

namespace chaotix::time_attack {

namespace {

unsigned rd16(const Machine& m, uint32_t off) {
    return unsigned(m.wram[off] << 8 | m.wram[off + 1]);
}
uint32_t rd32(const Machine& m, uint32_t off) {
    return uint32_t(m.wram[off] << 24 | m.wram[off + 1] << 16 | m.wram[off + 2] << 8 |
                    m.wram[off + 3]);
}

} // namespace

void Run::begin(const stage_select::Request& r) {
    running = true;
    request = r;
    time = 0;
    clock_started = false;
    finished = false;
    settling = 0;
    goal_was_set = false;
    started = false;
    last_counter = 0;
    last_stopped = 0;
}

bool Run::update(const Machine& m) {
    // Not yet: the request is applied at the game's mode dispatcher, and until
    // it is, the zone and level are still whatever came before.
    if (!running || m.stage_pending) return false;
    if (rd16(m, 0xDFF2) == request.place && rd16(m, 0xDFF4) == request.level) {
        started = true;
        const uint32_t counter = rd32(m, stage_select::kFrameCounter);
        const unsigned stopped = rd16(m, stage_select::kStoppedFrames);
        if (!clock_started) {
            // Still in the level's entry sequence; the clock has not begun.
            if (!rd16(m, stage_select::kClockStart)) return false;
            clock_started = true;
            goal_was_set = m.wram[stage_select::kReachedGoal] != 0;
        } else if (finished && settling <= 0) {
            // Reached the goal: the time is settled, and everything after it
            // is the game's tally, which is not the player's time.
        } else {
            const bool goal = m.wram[stage_select::kReachedGoal] != 0;
            if (!goal) goal_was_set = false;   // cleared: an arrival can count again
            if (!finished && goal && !goal_was_set) {
                finished = true;
                settling = stage_select::kGoalSettle;
            }
            if (finished) --settling;
            // Frame by frame, and as differences, so that the counter of
            // stopped frames wrapping past 65535 does not matter -- it starts
            // near the top, so it wraps within a few seconds of stopped time.
            const uint32_t ran = counter - last_counter;
            const unsigned held = uint16_t(stopped - last_stopped);
            if (ran > held) time += int(ran - held);
        }
        last_counter = counter;
        last_stopped = stopped;
        return false;
    }
    if (!started) return false;   // still on its way in
    running = false;
    return true;
}

} // namespace chaotix::time_attack
