// See livesplit.h.
#include "frontend/livesplit.h"
#include "frontend/speedrun.h"
#include "runtime/system.h"

namespace chaotix::livesplit {

// Initialised, so it is in the image's data section with its magic in place
// before the program runs, and written through a volatile pointer, since
// nothing in the program reads it back. The magic appears nowhere else in the
// program, so a scan finds this and only this.
extern Block g_block;
Block g_block = {"CHAOTIX_LIVESPLIT", kVersion, uint32_t(sizeof(Block)), 0, 0, 0, 0, 0, 0, 0, 0, 0};

const Block& block() { return g_block; }

void publish(const Machine* m, const speedrun::Tracker& t) {
    volatile Block* b = &g_block;
    b->wram = m ? uint64_t(reinterpret_cast<uintptr_t>(m->wram)) : 0;
    b->frame = m ? uint32_t(m->frame_count) : 0;
    b->state = t.state() == speedrun::Tracker::State::Running   ? 1u
               : t.state() == speedrun::Tracker::State::Stopped ? 2u
                                                                : 0u;
    b->run_id = t.run_id();
    b->splits = uint32_t(t.splits().size());
    b->igt_frames = uint32_t(t.igt_frames());
    b->level_frames = uint32_t(t.in_level() ? t.level_frames() : 0);
    b->time_attack = t.time_attack() ? 1u : 0u;
}

} // namespace chaotix::livesplit
