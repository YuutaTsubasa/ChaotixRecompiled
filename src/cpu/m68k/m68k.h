// Motorola 68000 CPU model shared by the reference interpreter and by
// statically recompiled code.
//
// Design notes:
//  * Condition codes are kept as separate 0/1 bytes. The architectural SR is
//    assembled on demand (m68k_get_sr / m68k_set_sr).
//  * Memory is accessed through M68kBus. 64 KiB pages that are plain memory
//    (ROM, work RAM) are exposed via page pointers holding BIG-ENDIAN bytes, so
//    the fast path is endian-independent. Everything else goes to callbacks.
//  * All ALU semantics live in this header as inline templates so the
//    interpreter and the generated C++ share one implementation.
#pragma once
#include <cstdint>

namespace m68k {

struct Bus {
    void* user = nullptr;
    const uint8_t* rpage[256] = {};  // readable 64 KiB pages (24-bit space)
    uint8_t* wpage[256] = {};        // writable 64 KiB pages
    uint32_t (*read8)(void* user, uint32_t addr) = nullptr;
    uint32_t (*read16)(void* user, uint32_t addr) = nullptr;
    void (*write8)(void* user, uint32_t addr, uint32_t v) = nullptr;
    void (*write16)(void* user, uint32_t addr, uint32_t v) = nullptr;
};

struct State {
    uint32_t d[8];
    uint32_t a[8];        // a[7] is the active stack pointer
    uint32_t pc;
    uint32_t other_sp;    // USP while in supervisor mode, SSP while in user mode
    uint8_t s, t, imask;  // supervisor, trace, interrupt mask
    uint8_t x, n, z, v, c;
    uint8_t stopped;
    uint8_t irq_level;    // highest pending autovector level (0 = none)
    int32_t cycles;       // remaining cycles in the current time slice
    uint64_t clock;       // total cycles executed (for timestamping)
    Bus bus;
    // Called when an interrupt is taken so hardware can clear/ack it.
    void (*irq_ack)(void* user, int level) = nullptr;
    void* irq_user = nullptr;
    // Hook invoked on RESET instruction (external devices reset).
    void (*reset_hook)(void* user) = nullptr;
};

// ---------------------------------------------------------------------------
// Memory access
// ---------------------------------------------------------------------------
inline uint32_t rd8(State* c, uint32_t a) {
    a &= 0xFFFFFF;
    const uint8_t* p = c->bus.rpage[a >> 16];
    if (p) return p[a & 0xFFFF];
    return c->bus.read8(c->bus.user, a) & 0xFF;
}
inline uint32_t rd16(State* c, uint32_t a) {
    a &= 0xFFFFFE;
    const uint8_t* p = c->bus.rpage[a >> 16];
    if (p) { p += a & 0xFFFF; return (uint32_t(p[0]) << 8) | p[1]; }
    return c->bus.read16(c->bus.user, a) & 0xFFFF;
}
inline uint32_t rd32(State* c, uint32_t a) {
    return (rd16(c, a) << 16) | rd16(c, a + 2);
}
inline void wr8(State* c, uint32_t a, uint32_t v) {
    a &= 0xFFFFFF;
    uint8_t* p = c->bus.wpage[a >> 16];
    if (p) { p[a & 0xFFFF] = uint8_t(v); return; }
    c->bus.write8(c->bus.user, a, v & 0xFF);
}
inline void wr16(State* c, uint32_t a, uint32_t v) {
    a &= 0xFFFFFE;
    uint8_t* p = c->bus.wpage[a >> 16];
    if (p) { p += a & 0xFFFF; p[0] = uint8_t(v >> 8); p[1] = uint8_t(v); return; }
    c->bus.write16(c->bus.user, a, v & 0xFFFF);
}
inline void wr32(State* c, uint32_t a, uint32_t v) {
    wr16(c, a, v >> 16);
    wr16(c, a + 2, v);
}
template <int S> inline uint32_t rd(State* c, uint32_t a) {
    if constexpr (S == 1) return rd8(c, a);
    else if constexpr (S == 2) return rd16(c, a);
    else return rd32(c, a);
}
template <int S> inline void wr(State* c, uint32_t a, uint32_t v) {
    if constexpr (S == 1) wr8(c, a, v);
    else if constexpr (S == 2) wr16(c, a, v);
    else wr32(c, a, v);
}

// ---------------------------------------------------------------------------
// Size helpers
// ---------------------------------------------------------------------------
template <int S> constexpr uint32_t mask() { return S == 1 ? 0xFFu : S == 2 ? 0xFFFFu : 0xFFFFFFFFu; }
template <int S> constexpr uint32_t msb() { return S == 1 ? 0x80u : S == 2 ? 0x8000u : 0x80000000u; }
template <int S> constexpr int bits() { return S * 8; }
template <int S> inline int32_t sext(uint32_t v) {
    if constexpr (S == 1) return int32_t(int8_t(v));
    else if constexpr (S == 2) return int32_t(int16_t(v));
    else return int32_t(v);
}
// Write the low S bytes of v into register r, preserving upper bits.
template <int S> inline void set_reg(uint32_t& r, uint32_t v) {
    r = (r & ~mask<S>()) | (v & mask<S>());
}

// ---------------------------------------------------------------------------
// Status register
// ---------------------------------------------------------------------------
inline uint32_t get_ccr(const State* c) {
    return (c->x << 4) | (c->n << 3) | (c->z << 2) | (c->v << 1) | c->c;
}
inline uint32_t get_sr(const State* c) {
    return (c->t << 15) | (c->s << 13) | (c->imask << 8) | get_ccr(c);
}
inline void set_ccr(State* c, uint32_t v) {
    c->x = (v >> 4) & 1; c->n = (v >> 3) & 1; c->z = (v >> 2) & 1;
    c->v = (v >> 1) & 1; c->c = v & 1;
}
inline void set_sr(State* c, uint32_t v) {
    uint8_t news = (v >> 13) & 1;
    if (news != c->s) {
        uint32_t tmp = c->a[7];
        c->a[7] = c->other_sp;
        c->other_sp = tmp;
        c->s = news;
    }
    c->t = (v >> 15) & 1;
    c->imask = (v >> 8) & 7;
    set_ccr(c, v);
}

// ---------------------------------------------------------------------------
// Condition codes (Bcc/DBcc/Scc)
// ---------------------------------------------------------------------------
inline bool cond(const State* c, int cc) {
    switch (cc & 15) {
    case 0: return true;                       // T
    case 1: return false;                      // F
    case 2: return !c->c && !c->z;             // HI
    case 3: return c->c || c->z;               // LS
    case 4: return !c->c;                      // CC
    case 5: return c->c;                       // CS
    case 6: return !c->z;                      // NE
    case 7: return c->z;                       // EQ
    case 8: return !c->v;                      // VC
    case 9: return c->v;                       // VS
    case 10: return !c->n;                     // PL
    case 11: return c->n;                      // MI
    case 12: return c->n == c->v;              // GE
    case 13: return c->n != c->v;              // LT
    case 14: return !c->z && (c->n == c->v);   // GT
    default: return c->z || (c->n != c->v);    // LE
    }
}

// ---------------------------------------------------------------------------
// ALU semantics
// ---------------------------------------------------------------------------
template <int S> inline void set_nz(State* c, uint32_t r) {
    c->n = (r & msb<S>()) != 0;
    c->z = (r & mask<S>()) == 0;
}
template <int S> inline uint32_t alu_logic(State* c, uint32_t r) {
    r &= mask<S>();
    set_nz<S>(c, r);
    c->v = 0; c->c = 0;
    return r;
}
template <int S> inline uint32_t alu_add(State* c, uint32_t s, uint32_t d) {
    s &= mask<S>(); d &= mask<S>();
    uint64_t full = uint64_t(s) + d;
    uint32_t r = uint32_t(full) & mask<S>();
    c->c = c->x = (full >> bits<S>()) & 1;
    c->v = (((s ^ r) & (d ^ r)) & msb<S>()) != 0;
    set_nz<S>(c, r);
    return r;
}
template <int S> inline uint32_t alu_addx(State* c, uint32_t s, uint32_t d) {
    s &= mask<S>(); d &= mask<S>();
    uint64_t full = uint64_t(s) + d + c->x;
    uint32_t r = uint32_t(full) & mask<S>();
    c->c = c->x = (full >> bits<S>()) & 1;
    c->v = (((s ^ r) & (d ^ r)) & msb<S>()) != 0;
    c->n = (r & msb<S>()) != 0;
    if (r) c->z = 0;
    return r;
}
// d - s
template <int S> inline uint32_t alu_sub(State* c, uint32_t s, uint32_t d) {
    s &= mask<S>(); d &= mask<S>();
    uint32_t r = (d - s) & mask<S>();
    c->c = c->x = s > d;
    c->v = (((s ^ d) & (r ^ d)) & msb<S>()) != 0;
    set_nz<S>(c, r);
    return r;
}
template <int S> inline uint32_t alu_subx(State* c, uint32_t s, uint32_t d) {
    s &= mask<S>(); d &= mask<S>();
    uint64_t full = uint64_t(d) - s - c->x;
    uint32_t r = uint32_t(full) & mask<S>();
    c->c = c->x = (full >> 63) & 1;
    c->v = (((s ^ d) & (r ^ d)) & msb<S>()) != 0;
    c->n = (r & msb<S>()) != 0;
    if (r) c->z = 0;
    return r;
}
template <int S> inline void alu_cmp(State* c, uint32_t s, uint32_t d) {
    s &= mask<S>(); d &= mask<S>();
    uint32_t r = (d - s) & mask<S>();
    c->c = s > d;
    c->v = (((s ^ d) & (r ^ d)) & msb<S>()) != 0;
    set_nz<S>(c, r);
}
template <int S> inline uint32_t alu_neg(State* c, uint32_t d) {
    return alu_sub<S>(c, d, 0);
}
template <int S> inline uint32_t alu_negx(State* c, uint32_t d) {
    return alu_subx<S>(c, d, 0);
}

inline uint32_t alu_abcd(State* c, uint32_t src, uint32_t dst) {
    uint32_t res = (src & 0x0F) + (dst & 0x0F) + c->x;
    uint32_t v = ~res;
    if (res > 9) res += 6;
    res += (src & 0xF0) + (dst & 0xF0);
    c->x = c->c = res > 0x99;
    if (c->c) res -= 0xA0;
    c->v = ((v & res) & 0x80) != 0;
    c->n = (res & 0x80) != 0;
    res &= 0xFF;
    if (res) c->z = 0;
    return res;
}
inline uint32_t alu_sbcd(State* c, uint32_t src, uint32_t dst) {
    uint32_t res = (dst & 0x0F) - (src & 0x0F) - c->x;
    uint32_t v = ~res;
    if (res > 9) res -= 6;
    res += (dst & 0xF0) - (src & 0xF0);
    c->x = c->c = res > 0x99;
    if (c->c) res += 0xA0;
    res &= 0xFF;
    c->v = ((v & res) & 0x80) != 0;
    c->n = (res & 0x80) != 0;
    if (res) c->z = 0;
    return res;
}
inline uint32_t alu_nbcd(State* c, uint32_t dst, bool& write) {
    uint32_t res = (0x9A - dst - c->x) & 0xFF;
    if (res != 0x9A) {
        uint32_t v = ~res;
        if ((res & 0x0F) == 0x0A) res = (res & 0xF0) + 0x10;
        res &= 0xFF;
        c->v = ((v & res) & 0x80) != 0;
        if (res) c->z = 0;
        c->c = c->x = 1;
        write = true;
    } else {
        c->v = 0; c->c = 0; c->x = 0;
        write = false;
    }
    c->n = (res & 0x80) != 0;
    return res;
}

// Shifts/rotates. `cnt` is already reduced modulo 64 by the caller.
template <int S> inline uint32_t alu_asl(State* c, uint32_t d, uint32_t cnt) {
    d &= mask<S>();
    const uint32_t B = bits<S>();
    if (cnt == 0) { c->c = 0; c->v = 0; set_nz<S>(c, d); return d; }
    uint32_t r;
    if (cnt < B) {
        r = (d << cnt) & mask<S>();
        c->c = c->x = (d >> (B - cnt)) & 1;
        // V: set if the top cnt+1 bits of d are not all identical.
        uint32_t topmask = (cnt + 1 >= B) ? mask<S>()
                                          : (mask<S>() & ~(mask<S>() >> (cnt + 1)));
        uint32_t top = d & topmask;
        c->v = !(top == 0 || top == topmask);
    } else {
        r = 0;
        c->c = c->x = (cnt == B) ? (d & 1) : 0;
        c->v = d != 0;
    }
    set_nz<S>(c, r);
    return r;
}
template <int S> inline uint32_t alu_asr(State* c, uint32_t d, uint32_t cnt) {
    d &= mask<S>();
    const uint32_t B = bits<S>();
    c->v = 0;
    if (cnt == 0) { c->c = 0; set_nz<S>(c, d); return d; }
    int32_t sd = sext<S>(d);
    uint32_t r;
    if (cnt < B) {
        r = uint32_t(sd >> cnt) & mask<S>();
        c->c = c->x = (uint32_t(sd) >> (cnt - 1)) & 1;
    } else {
        r = (sd < 0) ? mask<S>() : 0;
        c->c = c->x = sd < 0;
    }
    set_nz<S>(c, r);
    return r;
}
template <int S> inline uint32_t alu_lsl(State* c, uint32_t d, uint32_t cnt) {
    d &= mask<S>();
    const uint32_t B = bits<S>();
    c->v = 0;
    if (cnt == 0) { c->c = 0; set_nz<S>(c, d); return d; }
    uint32_t r;
    if (cnt < B) {
        r = (d << cnt) & mask<S>();
        c->c = c->x = (d >> (B - cnt)) & 1;
    } else {
        r = 0;
        c->c = c->x = (cnt == B) ? (d & 1) : 0;
    }
    set_nz<S>(c, r);
    return r;
}
template <int S> inline uint32_t alu_lsr(State* c, uint32_t d, uint32_t cnt) {
    d &= mask<S>();
    const uint32_t B = bits<S>();
    c->v = 0;
    if (cnt == 0) { c->c = 0; set_nz<S>(c, d); return d; }
    uint32_t r;
    if (cnt < B) {
        r = d >> cnt;
        c->c = c->x = (d >> (cnt - 1)) & 1;
    } else {
        r = 0;
        c->c = c->x = (cnt == B) ? ((d >> (B - 1)) & 1) : 0;
    }
    set_nz<S>(c, r);
    return r;
}
template <int S> inline uint32_t alu_rol(State* c, uint32_t d, uint32_t cnt) {
    d &= mask<S>();
    const uint32_t B = bits<S>();
    c->v = 0;
    if (cnt == 0) { c->c = 0; set_nz<S>(c, d); return d; }
    uint32_t n = cnt % B;
    uint32_t r = n ? (((d << n) | (d >> (B - n))) & mask<S>()) : d;
    c->c = r & 1;
    set_nz<S>(c, r);
    return r;
}
template <int S> inline uint32_t alu_ror(State* c, uint32_t d, uint32_t cnt) {
    d &= mask<S>();
    const uint32_t B = bits<S>();
    c->v = 0;
    if (cnt == 0) { c->c = 0; set_nz<S>(c, d); return d; }
    uint32_t n = cnt % B;
    uint32_t r = n ? (((d >> n) | (d << (B - n))) & mask<S>()) : d;
    c->c = (r >> (B - 1)) & 1;
    set_nz<S>(c, r);
    return r;
}
template <int S> inline uint32_t alu_roxl(State* c, uint32_t d, uint32_t cnt) {
    d &= mask<S>();
    const uint32_t B = bits<S>();
    c->v = 0;
    uint32_t n = cnt % (B + 1);
    uint64_t r = d;
    uint32_t x = c->x;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t out = (uint32_t(r) >> (B - 1)) & 1;
        r = ((r << 1) | x) & mask<S>();
        x = out;
    }
    c->x = uint8_t(x);
    c->c = uint8_t(x);
    set_nz<S>(c, uint32_t(r));
    return uint32_t(r);
}
template <int S> inline uint32_t alu_roxr(State* c, uint32_t d, uint32_t cnt) {
    d &= mask<S>();
    const uint32_t B = bits<S>();
    c->v = 0;
    uint32_t n = cnt % (B + 1);
    uint32_t r = d;
    uint32_t x = c->x;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t out = r & 1;
        r = (r >> 1) | (x << (B - 1));
        x = out;
    }
    c->x = uint8_t(x);
    c->c = uint8_t(x);
    set_nz<S>(c, r);
    return r;
}

// Bit ops: returns new value; sets Z from tested bit.
inline void alu_btst(State* c, uint32_t v, uint32_t bit) { c->z = ((v >> bit) & 1) == 0; }

inline uint32_t alu_mulu(State* c, uint32_t s, uint32_t d) {
    uint32_t r = (s & 0xFFFF) * (d & 0xFFFF);
    set_nz<4>(c, r); c->v = 0; c->c = 0;
    return r;
}
inline uint32_t alu_muls(State* c, uint32_t s, uint32_t d) {
    uint32_t r = uint32_t(int32_t(int16_t(s)) * int32_t(int16_t(d)));
    set_nz<4>(c, r); c->v = 0; c->c = 0;
    return r;
}
inline int mulu_cycles(uint32_t s) {
    int n = 0; s &= 0xFFFF;
    while (s) { n += s & 1; s >>= 1; }
    return 38 + 2 * n;
}
inline int muls_cycles(uint32_t s) {
    uint32_t v = (s & 0xFFFF) << 1;
    int n = 0;
    for (int i = 0; i < 16; ++i) { if (((v >> i) & 3) == 1 || ((v >> i) & 3) == 2) n++; }
    return 38 + 2 * n;
}
// Returns false on overflow (register unchanged).
inline bool alu_divu(State* c, uint32_t s, uint32_t& d) {
    s &= 0xFFFF;
    uint32_t q = d / s, r = d % s;
    c->c = 0;
    if (q > 0xFFFF) { c->v = 1; c->n = 1; return false; }
    d = (r << 16) | q;
    c->v = 0;
    c->n = (q & 0x8000) != 0;
    c->z = q == 0;
    return true;
}
inline bool alu_divs(State* c, uint32_t s, uint32_t& d) {
    int32_t sd = int16_t(s);
    int32_t dd = int32_t(d);
    c->c = 0;
    if (dd == INT32_MIN && sd == -1) { c->v = 1; c->n = 1; return false; }
    int32_t q = dd / sd, r = dd % sd;
    if (q < -32768 || q > 32767) { c->v = 1; c->n = 1; return false; }
    d = (uint32_t(r & 0xFFFF) << 16) | uint32_t(q & 0xFFFF);
    c->v = 0;
    c->n = (q & 0x8000) != 0;
    c->z = (q & 0xFFFF) == 0;
    return true;
}

// ---------------------------------------------------------------------------
// Exceptions
// ---------------------------------------------------------------------------
enum Vector : int {
    VEC_ILLEGAL = 4, VEC_ZERO_DIVIDE = 5, VEC_CHK = 6, VEC_TRAPV = 7,
    VEC_PRIVILEGE = 8, VEC_TRACE = 9, VEC_LINE_A = 10, VEC_LINE_F = 11,
    VEC_AUTOVECTOR_BASE = 24, VEC_TRAP_BASE = 32,
};

inline void push32(State* c, uint32_t v) { c->a[7] -= 4; wr32(c, c->a[7], v); }
inline void push16(State* c, uint32_t v) { c->a[7] -= 2; wr16(c, c->a[7], v); }
inline uint32_t pop32(State* c) { uint32_t v = rd32(c, c->a[7]); c->a[7] += 4; return v; }
inline uint32_t pop16(State* c) { uint32_t v = rd16(c, c->a[7]); c->a[7] += 2; return v; }

// Enter exception processing. `ret_pc` is the PC pushed on the frame.
inline void exception(State* c, int vec, uint32_t ret_pc) {
    uint32_t old_sr = get_sr(c);
    if (!c->s) {
        uint32_t tmp = c->a[7]; c->a[7] = c->other_sp; c->other_sp = tmp; c->s = 1;
    }
    c->t = 0;
    push32(c, ret_pc);
    push16(c, old_sr);
    c->pc = rd32(c, uint32_t(vec) * 4);
    c->cycles -= 34;
}

// Check for and service a pending interrupt. Returns true if taken.
inline bool check_irq(State* c) {
    int lvl = c->irq_level;
    if (lvl && (lvl > c->imask || lvl == 7)) {
        c->stopped = 0;
        if (c->irq_ack) c->irq_ack(c->irq_user, lvl);
        uint32_t old_sr = get_sr(c);
        if (!c->s) {
            uint32_t tmp = c->a[7]; c->a[7] = c->other_sp; c->other_sp = tmp; c->s = 1;
        }
        c->t = 0;
        push32(c, c->pc);
        push16(c, old_sr);
        c->imask = uint8_t(lvl);
        c->pc = rd32(c, uint32_t(VEC_AUTOVECTOR_BASE + lvl) * 4);
        c->cycles -= 44;
        return true;
    }
    return false;
}

void reset(State* c);

} // namespace m68k
