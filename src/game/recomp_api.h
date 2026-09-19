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
#include "cpu/sh2/sh2_ops.h"
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

struct Sh2Entry {
    uint32_t pc;
    Sh2Fn fn;
    uint32_t range_begin, range_count;  // validation ranges (RAM-resident code)
};

// Native call depth guard: guest calls become native calls; past this depth
// control returns to the dispatcher instead.
extern int g_depth;
constexpr int kMaxDepth = 64;

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

// Look up generated code for c->pc and call it as a subroutine. Returns the
// callee's result, or 1 if there is no (valid) generated code (the caller
// then unwinds to the dispatcher).
int m68k_call_dynamic(m68k::State* c);
int sh2_call_dynamic(sh2::State* c);

// Tables emitted by the recompiler.
extern const M68kEntry g_m68k_entries[];
extern const size_t g_m68k_entry_count;
extern const Sh2Entry g_sh2_entries[];
extern const size_t g_sh2_entry_count;
extern const CodeRange g_sh2_ranges[];
extern const size_t g_sh2_range_count;
extern const char g_rom_sha1[];

} // namespace recomp

#define M68K_BOUNDARY() do { if (!::recomp::m68k_ok(c)) return 0; } while (0)
#define SH2_BOUNDARY() do { if (!::recomp::sh2_ok(c)) return 0; } while (0)
