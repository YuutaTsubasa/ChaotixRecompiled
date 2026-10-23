// LiveSplit auto-splitter support (issue #47).
//
// An ASL script (tools/livesplit/ChaotixRecompiled.asl) has to find the game
// in the program's memory. Where the 68K work RAM lives moves from run to run
// (it is part of a Machine on the heap), so the program keeps one block at a
// fixed place in its own image -- a global in its data section -- that starts
// with a magic string a script can scan for, and says where the RAM is and
// what the speedrun timer (frontend/speedrun.h) makes of the run. The block is
// written by the host after every emulated frame and read by nobody inside
// the program: the emulation never sees it.
//
// Layout, version 1 (little-endian, as the host is; 72 bytes):
//
//   +0   char[24]  "CHAOTIX_LIVESPLIT", NUL padded
//   +24  u32       version (1)
//   +28  u32       size of the block (72)
//   +32  u64       address of the 68K work RAM: 64 KB, $FF0000-$FFFFFF, in
//                  the 68K's own byte order (big-endian words), 0 until the
//                  game has started
//   +40  u32       emulated frame counter (low 32 bits)
//   +44  u32       run state: 0 no run, 1 running, 2 stopped (TIME ATTACK over)
//   +48  u32       run id: goes up by one each time a run starts
//   +52  u32       splits taken in the run
//   +56  u32       game time of the run (IGT), frames at 60 Hz
//   +60  u32       the current level's own clock, frames at 60 Hz
//   +64  u32       1 while the run is a TIME ATTACK run
//   +68  u32       reserved (0)
#pragma once
#include <cstddef>
#include <cstdint>

namespace chaotix {

class Machine;
namespace speedrun { class Tracker; }

namespace livesplit {

inline constexpr uint32_t kVersion = 1;

struct Block {
    char magic[24];
    uint32_t version;
    uint32_t size;
    uint64_t wram;
    uint32_t frame;
    uint32_t state;
    uint32_t run_id;
    uint32_t splits;
    uint32_t igt_frames;
    uint32_t level_frames;
    uint32_t time_attack;
    uint32_t reserved;
};
static_assert(sizeof(Block) == 72, "the documented layout");
static_assert(offsetof(Block, wram) == 32 && offsetof(Block, frame) == 40 && offsetof(Block, igt_frames) == 56,
              "the documented layout");

// The block itself. Read it through block(); it is only ever written by
// publish().
const Block& block();

// After every emulated frame: where the machine's RAM is and how the run
// stands. `m` may be null (no game yet).
void publish(const Machine* m, const speedrun::Tracker& t);

} // namespace livesplit
} // namespace chaotix
