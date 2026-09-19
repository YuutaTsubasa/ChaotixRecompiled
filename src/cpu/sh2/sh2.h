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
};

inline bool fast_area(uint32_t a) { return (a & 0xD8000000u) == 0; }
inline uint32_t page_of(uint32_t a) { return (a >> Bus::kPageShift) & (Bus::kPages - 1); }
inline uint32_t page_off(uint32_t a) { return a & Bus::kPageMask; }

inline uint32_t rd8(State* c, uint32_t a) {
    if (fast_area(a)) { const uint8_t* p = c->bus.rpage[page_of(a)]; if (p) return p[page_off(a)]; }
    return c->bus.read8(c->bus.user, a) & 0xFF;
}
inline uint32_t rd16(State* c, uint32_t a) {
    a &= ~1u;
    if (fast_area(a)) {
        const uint8_t* p = c->bus.rpage[page_of(a)];
        if (p) { p += page_off(a); return (uint32_t(p[0]) << 8) | p[1]; }
    }
    return c->bus.read16(c->bus.user, a) & 0xFFFF;
}
inline uint32_t rd32(State* c, uint32_t a) {
    a &= ~3u;
    if (fast_area(a)) {
        const uint8_t* p = c->bus.rpage[page_of(a)];
        if (p) { p += page_off(a); return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }
    }
    return c->bus.read32(c->bus.user, a);
}
inline void wr8(State* c, uint32_t a, uint32_t v) {
    if (fast_area(a)) { uint8_t* p = c->bus.wpage[page_of(a)]; if (p) { p[page_off(a)] = uint8_t(v); return; } }
    c->bus.write8(c->bus.user, a, v & 0xFF);
}
inline void wr16(State* c, uint32_t a, uint32_t v) {
    a &= ~1u;
    if (fast_area(a)) {
        uint8_t* p = c->bus.wpage[page_of(a)];
        if (p) { p += page_off(a); p[0] = uint8_t(v >> 8); p[1] = uint8_t(v); return; }
    }
    c->bus.write16(c->bus.user, a, v & 0xFFFF);
}
inline void wr32(State* c, uint32_t a, uint32_t v) {
    a &= ~3u;
    if (fast_area(a)) {
        uint8_t* p = c->bus.wpage[page_of(a)];
        if (p) { p += page_off(a); p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v); return; }
    }
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
