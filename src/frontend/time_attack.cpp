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
    base = 0;
    clock_started = false;
    started = false;
}

bool Run::update(const Machine& m) {
    // Not yet: the request is applied at the game's mode dispatcher, and until
    // it is, the zone and level are still whatever came before.
    if (!running || m.stage_pending) return false;
    if (rd16(m, 0xDFF2) == request.place && rd16(m, 0xDFF4) == request.level) {
        started = true;
        const unsigned since_start = rd16(m, stage_select::kClockStart);
        if (!clock_started) {
            // Still in the level's entry sequence; the clock has not begun.
            if (!since_start) return false;
            clock_started = true;
            base = rd32(m, stage_select::kFrameCounter) - since_start;
        }
        time = int(rd32(m, stage_select::kFrameCounter) - base);
        return false;
    }
    if (!started) return false;   // still on its way in
    running = false;
    return true;
}

} // namespace chaotix::time_attack
