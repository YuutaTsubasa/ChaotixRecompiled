// Hitachi SH-2 (SH7604) CPU model shared by the interpreter and recompiled code.
#pragma once
#include <cstdint>

namespace sh2 {

// SR bits
constexpr uint32_t SR_T = 1, SR_S = 2, SR_IMASK = 0xF0, SR_Q = 0x100, SR_M = 0x200, SR_MASK = 0x3F3;

struct Bus {
    void* user = nullptr;
    // Fast pages cover the cached (0x0xxxxxxx) and cache-through (0x2xxxxxxx)
    // areas below 0x08000000 in 16 KiB granules. Bytes are big-endian.
    static constexpr int kPageShift = 14;
    static constexpr uint32_t kPageMask = (1u << kPageShift) - 1;
    static constexpr int kPages = 0x08000000 >> kPageShift;
    const uint8_t* rpage[kPages] = {};
    uint8_t* wpage[kPages] = {};
    // The cache data array as RAM (area 6, 0xC0000000-0xDFFFFFFF, mirrored
    // every 4 KiB): reads from `cache`, writes to the 64-byte granules
    // `cache_w` points at (null: the granule holds recompiled code, and the
    // write goes through write8/16/32, which notes code changes).
    static constexpr int kCacheGranuleShift = 6;
    const uint8_t* cache = nullptr;
    uint8_t* cache_w[4096 >> kCacheGranuleShift] = {};
    // Per recompiled function, how often its code in SDRAM was changed
    // (Machine::sh2_fn_code_writes; read by generated code).
    const uint32_t* code_writes = nullptr;
    uint32_t (*read8)(void* user, uint32_t addr) = nullptr;
    uint32_t (*read16)(void* user, uint32_t addr) = nullptr;
    uint32_t (*read32)(void* user, uint32_t addr) = nullptr;
    void (*write8)(void* user, uint32_t addr, uint32_t v) = nullptr;
    void (*write16)(void* user, uint32_t addr, uint32_t v) = nullptr;
    void (*write32)(void* user, uint32_t addr, uint32_t v) = nullptr;
};

struct State {
    uint32_t r[16];
    uint32_t pc, pr, gbr, vbr, mach, macl, sr;
    int32_t cycles;        // remaining cycles in slice
    uint8_t sleeping;
    uint8_t irq_level;     // pending interrupt level (0 = none)
    uint8_t irq_vector;    // vector number for the pending interrupt
    uint8_t id;            // 0 = master, 1 = slave
    Bus bus;
    // Called when an interrupt is accepted (so the source can be acknowledged).
    void (*irq_ack)(void* user, State* c) = nullptr;
    void* irq_user = nullptr;
    // Record-only host hooks (the 32X sprite recorder): `hook` runs before any
    // instruction whose address is in the sorted list hook_pcs. The
    // interpreter and the generated code call it at the same points; it may
    // read the CPU and memory but must change nothing the machine sees, and
    // it costs no cycles. Never called for a delay slot.
    void (*hook)(State* c, uint32_t pc) = nullptr;
    void* hook_user = nullptr;
    const uint32_t* hook_pcs = nullptr;
    uint32_t hook_count = 0;
};

// Whether pc is one of c's hook addresses (the caller checked c->hook). The
// range test first keeps the interpreter's cost per instruction a compare.
inline bool is_hook_pc(const State* c, uint32_t pc) {
    if (pc < c->hook_pcs[0] || pc > c->hook_pcs[c->hook_count - 1]) return false;
    for (uint32_t i = 0; i < c->hook_count; ++i)
        if (c->hook_pcs[i] == pc) return true;
    return false;
}

inline bool fast_area(uint32_t a) { return (a & 0xD8000000u) == 0; }
inline uint32_t page_of(uint32_t a) { return (a >> Bus::kPageShift) & (Bus::kPages - 1); }
inline uint32_t page_off(uint32_t a) { return a & Bus::kPageMask; }

#if defined(__GNUC__)
#define SH2_ACCESS inline __attribute__((always_inline))
#else
#define SH2_ACCESS __forceinline
#endif

// The host address of an access the fast pages or the cache data array
// serve directly, else null (the access goes through the bus callbacks).
// Area 6 is only ever RAM here, as Machine::sh2_read and sh2_write treat
// it; writes reach it directly only in granules without recompiled code.
// Always inlined: generated code is made of these, in functions large enough
// that the compiler would otherwise call some of them.
SH2_ACCESS const uint8_t* rd_ptr(const State* c, uint32_t a) {
    if (fast_area(a)) { const uint8_t* p = c->bus.rpage[page_of(a)]; return p ? p + page_off(a) : nullptr; }
    if ((a >> 29) == 6 && c->bus.cache) return c->bus.cache + (a & 0xFFF);
    return nullptr;
}
SH2_ACCESS uint8_t* wr_ptr(const State* c, uint32_t a) {
    if (fast_area(a)) { uint8_t* p = c->bus.wpage[page_of(a)]; return p ? p + page_off(a) : nullptr; }
    if ((a >> 29) == 6) {
        uint8_t* g = c->bus.cache_w[(a & 0xFFF) >> Bus::kCacheGranuleShift];
        return g ? g + (a & ((1u << Bus::kCacheGranuleShift) - 1)) : nullptr;
    }
    return nullptr;
}

SH2_ACCESS uint32_t rd8(State* c, uint32_t a) {
    if (const uint8_t* p = rd_ptr(c, a)) return p[0];
    return c->bus.read8(c->bus.user, a) & 0xFF;
}
SH2_ACCESS uint32_t rd16(State* c, uint32_t a) {
    a &= ~1u;
    if (const uint8_t* p = rd_ptr(c, a)) return (uint32_t(p[0]) << 8) | p[1];
    return c->bus.read16(c->bus.user, a) & 0xFFFF;
}
SH2_ACCESS uint32_t rd32(State* c, uint32_t a) {
    a &= ~3u;
    if (const uint8_t* p = rd_ptr(c, a)) return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
    return c->bus.read32(c->bus.user, a);
}
SH2_ACCESS void wr8(State* c, uint32_t a, uint32_t v) {
    if (uint8_t* p = wr_ptr(c, a)) { p[0] = uint8_t(v); return; }
    c->bus.write8(c->bus.user, a, v & 0xFF);
}
SH2_ACCESS void wr16(State* c, uint32_t a, uint32_t v) {
    a &= ~1u;
    if (uint8_t* p = wr_ptr(c, a)) { p[0] = uint8_t(v >> 8); p[1] = uint8_t(v); return; }
    c->bus.write16(c->bus.user, a, v & 0xFFFF);
}
SH2_ACCESS void wr32(State* c, uint32_t a, uint32_t v) {
    a &= ~3u;
    if (uint8_t* p = wr_ptr(c, a)) { p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v); return; }
    c->bus.write32(c->bus.user, a, v);
}

inline uint32_t T(const State* c) { return c->sr & SR_T; }
inline void setT(State* c, bool t) { c->sr = (c->sr & ~SR_T) | (t ? 1u : 0u); }

// Accept a pending interrupt if its level exceeds the mask. Returns true if taken.
inline bool check_irq(State* c) {
    if (c->irq_level && c->irq_level > ((c->sr >> 4) & 15)) {
        uint32_t lvl = c->irq_level, vec = c->irq_vector;
        c->sleeping = 0;
        if (c->irq_ack) c->irq_ack(c->irq_user, c);
        c->r[15] -= 4; wr32(c, c->r[15], c->sr);
        c->r[15] -= 4; wr32(c, c->r[15], c->pc);
        c->sr = (c->sr & ~SR_IMASK) | (lvl << 4);
        c->pc = rd32(c, c->vbr + vec * 4);
        c->cycles -= 13;
        return true;
    }
    return false;
}

void reset(State* c, uint32_t pc, uint32_t sp, uint32_t vbr);

} // namespace sh2
