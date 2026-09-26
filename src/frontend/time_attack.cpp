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
    started = false;
    finished = false;
    settling = 0;
    goal_was_set = false;
    stopped_total = 0;
    last_stopped = 0;
    have_start = false;
    start_at = 0;
    climbing = 0;
    last_since = 0;
}

bool Run::update(const Machine& m) {
    // Not yet: the request is applied at the game's mode dispatcher, and until
    // it is, the zone and level are still whatever came before.
    if (!running || m.stage_pending) return false;
    if (rd16(m, 0xDFF2) == request.place && rd16(m, 0xDFF4) == request.level) {
        const uint32_t counter = rd32(m, stage_select::kFrameCounter);
        const unsigned since = rd16(m, stage_select::kSinceStart);
        const unsigned stopped = rd16(m, stage_select::kStoppedFrames);

        // Where this level's clock started. Only a reading that has been
        // climbing steadily counts, and the earliest start any of them implies
        // is the one to keep.
        climbing = (since && since == last_since + 1) ? climbing + 1 : 0;
        last_since = since;
        if (climbing >= stage_select::kCalibrationFrames) {
            const uint32_t implied = counter - since;
            if (!have_start || implied < start_at) {
                have_start = true;
                start_at = implied;
            }
        }

        if (!started) {
            started = true;
            last_stopped = stopped;
            goal_was_set = m.wram[stage_select::kReachedGoal] != 0;
        } else {
            // As differences, because it wraps: it starts near the top, so a
            // few seconds of stopped time takes it past 65535.
            stopped_total += uint16_t(stopped - last_stopped);
            last_stopped = stopped;
        }

        if (finished && settling <= 0) return false;   // the time is settled

        const bool goal = m.wram[stage_select::kReachedGoal] != 0;
        if (!goal) goal_was_set = false;   // cleared: an arrival can count again
        if (!finished && goal && !goal_was_set) {
            finished = true;
            settling = stage_select::kGoalSettle;
        }
        if (finished) --settling;

        if (!have_start) return false;   // the level has not begun counting
        const int elapsed = int(counter - start_at) - int(stopped_total);
        time = elapsed > 0 ? elapsed : 0;
        return false;
    }
    if (!started) return false;   // still on its way in
    running = false;
    return true;
}

} // namespace chaotix::time_attack
