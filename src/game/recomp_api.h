// Interface between statically recompiled code (generated/*.cpp) and the
// runtime. Generated functions share instruction semantics with the reference
// interpreters (m68k_ops.h / sh2_ops.h); only control flow is compiled.
//
// Execution contract (identical to the interpreters, which is what makes
// lockstep validation possible):
//  * Interrupts, slice ends, STOP/SLEEP are only observed at basic-block
//    boundaries, i.e. after an instruction for which ends_block() is true.
//  * A generated function returns 0 when a boundary check fails ("stop": the
//    scheduler must regain control) and 1 when control leaves the function
//    normally (c->pc holds the next address).
#pragma once
#include "cpu/m68k/m68k_ops.h"
#include "cpu/sh2/sh2_interp.h"
#include "cpu/sh2/sh2_ops.h"
#include <climits>
#include <cstddef>
#include <cstdint>

namespace recomp {

using M68kFn = int (*)(m68k::State*);
using Sh2Fn = int (*)(sh2::State*);

struct M68kEntry {
    uint32_t key;   // pc (24-bit) | (bank + 1) << 24 for the banked 0x900000 window
    M68kFn fn;
};

struct CodeRange {
    uint32_t addr;     // runtime address
    uint32_t len;
    uint32_t rom_off;  // expected contents come from this ROM offset
};

// ROM bytes the generated code relies on without checking them (code run
// from ROM, SH-2 literals folded from it). cpu: 0 = 68K, 1 = SH-2.
struct RomCode {
    uint32_t cpu, rom_off, len;
};

struct Sh2Entry {
    uint32_t pc;
    Sh2Fn fn;
    uint32_t range_begin, range_count;  // validation ranges (RAM-resident code)
    uint32_t fn_index;                  // the function's number, 0 .. g_sh2_function_count - 1
};

// Native call depth guard: guest calls become native calls; past this depth
// control returns to the dispatcher instead.
extern int g_depth;
constexpr int kMaxDepth = 64;

// Wait loops are fast-forwarded in closed form (the default). Off, generated
// code runs every iteration, as the interpreter does: the same result, more
// slowly (chaotix_headless --no-idle-skip, for comparison; issue #62).
extern bool g_idle_skip;
// Cycles charged that way, per CPU (0 = 68K, 1/2 = master/slave SH-2).
extern uint64_t g_idle_skipped[3];

inline bool m68k_ok(const m68k::State* c) {
    return c->cycles > 0 && !c->stopped && !(c->irq_level && (c->irq_level > c->imask || c->irq_level == 7));
}
inline bool sh2_ok(const sh2::State* c) {
    return c->cycles > 0 && !c->sleeping && !(c->irq_level > ((c->sr >> 4) & 15));
}

// True if an SH-2 load from `a` is side-effect free and cannot change while
// this CPU's time slice runs (other CPUs, interrupts and timers only act
// between slices): ROM, frame buffer, SDRAM, cache RAM and the COMM ports.
// Used to fast-forward idempotent polling loops exactly.
inline bool sh2_pure_load(uint32_t a) {
    const uint32_t area = a >> 29;
    if (area == 6) return true;
    if (area > 1) return false;
    const uint32_t p = a & 0x1FFFFFFF;
    if (p >= 0x02000000 && p < 0x08000000) return true;
    return p >= 0x4020 && p < 0x4030;
}

// The 68000's counterpart (issue #62): work RAM, which only the 68000 (and,
// after it in a slice, the Z80) writes, and the COMM ports, which the SH-2s
// write after it in a slice. Neither read has side effects.
inline bool m68k_pure_load(uint32_t a) {
    a &= 0xFFFFFF;
    return a >= 0xE00000 || (a >= 0xA15120 && a < 0xA15130);
}

// Polling loops whose reads change with time (issue #62): how many further
// iterations of `cost` cycles each are certain to read from `a` (`size`
// bytes) what this iteration read, made when c->cycles was `at`. 0 when it
// cannot tell (e.g. a read with side effects).
// (Inlined, `cost` a constant: the division is a multiply, also on
// armeabi-v7a, which has no divide instruction.)
int32_t sh2_read_stable_to(const sh2::State* c, uint32_t a, int size, int32_t at);
inline int32_t sh2_poll_repeats(const sh2::State* c, uint32_t a, int size, int32_t at, int32_t cost) {
    if (sh2_pure_load(a)) return INT32_MAX;
    const int32_t last = sh2_read_stable_to(c, a, size, at);
    if (last == INT32_MIN) return INT32_MAX;
    const int64_t span = int64_t(at) - last;
    return span > INT32_MAX ? INT32_MAX : int32_t(span) / cost;
}

// Look up generated code for c->pc and call it as a subroutine. Returns the
// callee's result, or 1 if there is no (valid) generated code (the caller
// then unwinds to the dispatcher).
int m68k_call_dynamic(m68k::State* c);
int sh2_call_dynamic(sh2::State* c);
// Before generated code calls a function in RAM directly: whether its code is
// still what it was compiled from. c->pc is the callee's entry; *cache keeps
// which function that is (0 = not looked up yet).
bool sh2_callee_valid(const sh2::State* c, uint32_t* cache);

// SH-2 code memory as it is now: the immediates the runtime patches
// (patches::is_patched_sh2_immediate), and PC-relative loads from SDRAM,
// which the game can use as variables.
uint8_t sh2_code_byte(const sh2::State* c, uint32_t addr);
uint16_t sh2_code16(const sh2::State* c, uint32_t addr);
uint32_t sh2_code32(const sh2::State* c, uint32_t addr);

// Tables emitted by the recompiler.
extern const M68kEntry g_m68k_entries[];
extern const size_t g_m68k_entry_count;
extern const Sh2Entry g_sh2_entries[];
extern const size_t g_sh2_entry_count;
extern const size_t g_sh2_function_count;
extern const CodeRange g_sh2_ranges[];
extern const size_t g_sh2_range_count;
extern const char g_rom_sha1[];
extern const RomCode g_rom_code[];
extern const size_t g_rom_code_count;

} // namespace recomp

#define M68K_BOUNDARY() do { if (!::recomp::m68k_ok(c)) return 0; } while (0)
// Self-modifying code in SDRAM (see the emitter): what generated code does
// when its own code was written since the function began (c->bus.code_writes,
// Machine::sdram_code_written; fn_ is the function's number, e0_ the count
// on entry). Mid-block, the rest of the block is interpreted, so that the
// scheduler regains control at the boundary the interpreter would have
// reached. Writes over other functions' code leave this one's code, and so
// what it does, unchanged; they are checked where those functions are
// entered (issue #62).
#define SH2_CODE_CHANGED(next) \
    do { if (c->bus.code_writes[fn_] != e0_) { c->pc = (next); ::sh2::interp_block(c); return 1; } } while (0)
#define SH2_CODE_CHANGED_AT_BOUNDARY() do { if (c->bus.code_writes[fn_] != e0_) return 1; } while (0)
#define SH2_BOUNDARY() do { if (!::recomp::sh2_ok(c)) return 0; } while (0)
