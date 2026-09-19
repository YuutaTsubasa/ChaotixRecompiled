// 68000 instruction semantics, shared by the interpreter and by generated code.
//
// Every op_* function takes the decoded instruction by const reference. In
// generated code the Insn is a `static constexpr` object, so after inlining
// the compiler folds all addressing-mode and size switches away and the
// instruction becomes straight-line native code.
//
// Contract:
//  * The caller subtracts in.cycles before calling; op functions only account
//    for data-dependent extra cycles.
//  * c->pc must hold the address of the NEXT instruction when an op that can
//    raise an exception or branch is called (IF_BRANCH / IF_END_BLOCK / IF_PRIV).
#pragma once
#include "m68k.h"
#include "m68k_decode.h"

#if defined(__GNUC__)
#define M68K_INLINE inline __attribute__((always_inline))
#else
#define M68K_INLINE __forceinline
#endif

namespace m68k {

M68K_INLINE uint32_t index_value(const State* c, const Ea& e) {
    uint32_t x = e.xreg < 8 ? c->d[e.xreg] : c->a[e.xreg - 8];
    if (!e.xlong) x = uint32_t(int32_t(int16_t(x)));
    return x;
}

template <int S> M68K_INLINE uint32_t ea_addr(State* c, const Ea& e) {
    switch (e.mode) {
    case EA_IND: return c->a[e.reg];
    case EA_POSTINC: {
        uint32_t a = c->a[e.reg];
        c->a[e.reg] += (S == 1 && e.reg == 7) ? 2 : S;
        return a;
    }
    case EA_PREDEC:
        c->a[e.reg] -= (S == 1 && e.reg == 7) ? 2 : S;
        return c->a[e.reg];
    case EA_DISP: return c->a[e.reg] + uint32_t(e.disp);
    case EA_INDEX: return c->a[e.reg] + uint32_t(e.disp) + index_value(c, e);
    case EA_ABSW: case EA_ABSL: case EA_PCDISP: return e.value;
    case EA_PCINDEX: return e.value + index_value(c, e);
    default: return 0;
    }
}

template <int S> M68K_INLINE uint32_t ea_read(State* c, const Ea& e) {
    switch (e.mode) {
    case EA_DREG: return c->d[e.reg] & mask<S>();
    case EA_AREG: return c->a[e.reg] & mask<S>();
    case EA_IMM: return e.value & mask<S>();
    default: return rd<S>(c, ea_addr<S>(c, e));
    }
}

// Read for read-modify-write; `addr` receives the memory address (if any).
template <int S> M68K_INLINE uint32_t ea_rmw_read(State* c, const Ea& e, uint32_t& addr) {
    switch (e.mode) {
    case EA_DREG: return c->d[e.reg] & mask<S>();
    case EA_AREG: return c->a[e.reg];
    default: addr = ea_addr<S>(c, e); return rd<S>(c, addr);
    }
}
template <int S> M68K_INLINE void ea_rmw_write(State* c, const Ea& e, uint32_t addr, uint32_t v) {
    switch (e.mode) {
    case EA_DREG: set_reg<S>(c->d[e.reg], v); break;
    case EA_AREG: c->a[e.reg] = v; break;
    default: wr<S>(c, addr, v); break;
    }
}
template <int S> M68K_INLINE void ea_write(State* c, const Ea& e, uint32_t v) {
    switch (e.mode) {
    case EA_DREG: set_reg<S>(c->d[e.reg], v); break;
    case EA_AREG: c->a[e.reg] = v; break;
    default: wr<S>(c, ea_addr<S>(c, e), v); break;
    }
}

// Dispatch helper: call F<S>() with the runtime size folded to a template.
#define M68K_SIZED(fn, in) \
    do { switch ((in).size) { case 1: fn<1>(c, in); break; case 2: fn<2>(c, in); break; default: fn<4>(c, in); break; } } while (0)

// ---------------------------------------------------------------------------
// Data movement
// ---------------------------------------------------------------------------
template <int S> M68K_INLINE void op_move_s(State* c, const Insn& in) {
    uint32_t v = ea_read<S>(c, in.src);
    alu_logic<S>(c, v);
    ea_write<S>(c, in.dst, v);
}
M68K_INLINE void op_move(State* c, const Insn& in) { M68K_SIZED(op_move_s, in); }

M68K_INLINE void op_movea(State* c, const Insn& in) {
    uint32_t v = in.size == 2 ? uint32_t(int32_t(int16_t(ea_read<2>(c, in.src)))) : ea_read<4>(c, in.src);
    c->a[in.dst.reg] = v;
}
M68K_INLINE void op_moveq(State* c, const Insn& in) {
    c->d[in.dst.reg] = in.src.value;
    alu_logic<4>(c, in.src.value);
}
M68K_INLINE void op_lea(State* c, const Insn& in) { c->a[in.dst.reg] = ea_addr<4>(c, in.src); }
M68K_INLINE void op_pea(State* c, const Insn& in) { uint32_t a = ea_addr<4>(c, in.src); push32(c, a); }
M68K_INLINE void op_exg(State* c, const Insn& in) {
    uint32_t& x = in.src.mode == EA_DREG ? c->d[in.src.reg] : c->a[in.src.reg];
    uint32_t& y = in.dst.mode == EA_DREG ? c->d[in.dst.reg] : c->a[in.dst.reg];
    uint32_t t = x; x = y; y = t;
}
M68K_INLINE void op_swap(State* c, const Insn& in) {
    uint32_t v = c->d[in.dst.reg];
    v = (v >> 16) | (v << 16);
    c->d[in.dst.reg] = v;
    alu_logic<4>(c, v);
}
M68K_INLINE void op_ext(State* c, const Insn& in) {
    uint32_t& r = c->d[in.dst.reg];
    if (in.size == 2) { set_reg<2>(r, uint32_t(int32_t(int8_t(r)))); alu_logic<2>(c, r); }
    else { r = uint32_t(int32_t(int16_t(r))); alu_logic<4>(c, r); }
}
template <int S> M68K_INLINE void op_movem_rm_s(State* c, const Insn& in) {
    const uint32_t m = in.imm;
    if (in.dst.mode == EA_PREDEC) {
        uint32_t addr = c->a[in.dst.reg];
        for (int i = 0; i < 16; ++i) {
            if (m & (1u << i)) {
                int r = 15 - i;
                addr -= S;
                uint32_t v = r < 8 ? c->d[r] : c->a[r - 8];
                wr<S>(c, addr, v);
            }
        }
        c->a[in.dst.reg] = addr;
    } else {
        uint32_t addr = ea_addr<S>(c, in.dst);
        for (int r = 0; r < 16; ++r) {
            if (m & (1u << r)) {
                uint32_t v = r < 8 ? c->d[r] : c->a[r - 8];
                wr<S>(c, addr, v);
                addr += S;
            }
        }
    }
}
M68K_INLINE void op_movem_rm(State* c, const Insn& in) {
    if (in.size == 2) op_movem_rm_s<2>(c, in); else op_movem_rm_s<4>(c, in);
}
template <int S> M68K_INLINE void op_movem_mr_s(State* c, const Insn& in) {
    const uint32_t m = in.imm;
    uint32_t addr = in.src.mode == EA_POSTINC ? c->a[in.src.reg] : ea_addr<S>(c, in.src);
    for (int r = 0; r < 16; ++r) {
        if (m & (1u << r)) {
            uint32_t v = rd<S>(c, addr);
            if (S == 2) v = uint32_t(int32_t(int16_t(v)));
            if (r < 8) c->d[r] = v; else c->a[r - 8] = v;
            addr += S;
        }
    }
    if (in.src.mode == EA_POSTINC) c->a[in.src.reg] = addr;
}
M68K_INLINE void op_movem_mr(State* c, const Insn& in) {
    if (in.size == 2) op_movem_mr_s<2>(c, in); else op_movem_mr_s<4>(c, in);
}
M68K_INLINE void op_movep_mr(State* c, const Insn& in) {
    uint32_t a = c->a[in.dst.reg] + uint32_t(in.dst.disp);
    if (in.size == 2) {
        uint32_t v = (rd8(c, a) << 8) | rd8(c, a + 2);
        set_reg<2>(c->d[in.reg], v);
    } else {
        c->d[in.reg] = (rd8(c, a) << 24) | (rd8(c, a + 2) << 16) | (rd8(c, a + 4) << 8) | rd8(c, a + 6);
    }
}
M68K_INLINE void op_movep_rm(State* c, const Insn& in) {
    uint32_t a = c->a[in.dst.reg] + uint32_t(in.dst.disp);
    uint32_t v = c->d[in.reg];
    if (in.size == 2) { wr8(c, a, v >> 8); wr8(c, a + 2, v); }
    else { wr8(c, a, v >> 24); wr8(c, a + 2, v >> 16); wr8(c, a + 4, v >> 8); wr8(c, a + 6, v); }
}

// ---------------------------------------------------------------------------
// Arithmetic / logic
// ---------------------------------------------------------------------------
enum class Alu { ADD, SUB, AND, OR, EOR, CMP, ADDX, SUBX };

template <Alu K, int S> M68K_INLINE void op_alu_s(State* c, const Insn& in) {
    uint32_t s = ea_read<S>(c, in.src);
    uint32_t addr = 0;
    uint32_t d = ea_rmw_read<S>(c, in.dst, addr);
    uint32_t r;
    if constexpr (K == Alu::ADD) r = alu_add<S>(c, s, d);
    else if constexpr (K == Alu::SUB) r = alu_sub<S>(c, s, d);
    else if constexpr (K == Alu::AND) r = alu_logic<S>(c, s & d);
    else if constexpr (K == Alu::OR) r = alu_logic<S>(c, s | d);
    else if constexpr (K == Alu::EOR) r = alu_logic<S>(c, s ^ d);
    else if constexpr (K == Alu::ADDX) r = alu_addx<S>(c, s, d);
    else if constexpr (K == Alu::SUBX) r = alu_subx<S>(c, s, d);
    else { alu_cmp<S>(c, s, d); return; }
    ea_rmw_write<S>(c, in.dst, addr, r);
}
template <Alu K> M68K_INLINE void op_alu(State* c, const Insn& in) {
    switch (in.size) {
    case 1: op_alu_s<K, 1>(c, in); break;
    case 2: op_alu_s<K, 2>(c, in); break;
    default: op_alu_s<K, 4>(c, in); break;
    }
}

// ADDQ/SUBQ: address register destination is a 32-bit op without flags.
template <bool SUB> M68K_INLINE void op_addq(State* c, const Insn& in) {
    if (in.dst.mode == EA_AREG) {
        if (SUB) c->a[in.dst.reg] -= in.src.value; else c->a[in.dst.reg] += in.src.value;
        return;
    }
    if (SUB) op_alu<Alu::SUB>(c, in); else op_alu<Alu::ADD>(c, in);
}

template <int K> M68K_INLINE void op_adda(State* c, const Insn& in) {  // K: 0 add, 1 sub, 2 cmp
    uint32_t s = in.size == 2 ? uint32_t(int32_t(int16_t(ea_read<2>(c, in.src)))) : ea_read<4>(c, in.src);
    uint32_t& d = c->a[in.dst.reg];
    if (K == 0) d += s;
    else if (K == 1) d -= s;
    else alu_cmp<4>(c, s, d);
}

template <int S> M68K_INLINE void op_cmpm_s(State* c, const Insn& in) {
    uint32_t s = ea_read<S>(c, in.src);
    uint32_t d = ea_read<S>(c, in.dst);
    alu_cmp<S>(c, s, d);
}
M68K_INLINE void op_cmpm(State* c, const Insn& in) { M68K_SIZED(op_cmpm_s, in); }

enum class Un { NEG, NEGX, NOT, CLR };
template <Un K, int S> M68K_INLINE void op_unary_s(State* c, const Insn& in) {
    uint32_t addr = 0;
    if constexpr (K == Un::CLR) {
        if (in.dst.mode != EA_DREG) addr = ea_addr<S>(c, in.dst);
        c->n = 0; c->z = 1; c->v = 0; c->c = 0;
        ea_rmw_write<S>(c, in.dst, addr, 0);
        return;
    } else {
        uint32_t d = ea_rmw_read<S>(c, in.dst, addr);
        uint32_t r;
        if constexpr (K == Un::NEG) r = alu_neg<S>(c, d);
        else if constexpr (K == Un::NEGX) r = alu_negx<S>(c, d);
        else r = alu_logic<S>(c, ~d);
        ea_rmw_write<S>(c, in.dst, addr, r);
    }
}
template <Un K> M68K_INLINE void op_unary(State* c, const Insn& in) {
    switch (in.size) {
    case 1: op_unary_s<K, 1>(c, in); break;
    case 2: op_unary_s<K, 2>(c, in); break;
    default: op_unary_s<K, 4>(c, in); break;
    }
}

template <int S> M68K_INLINE void op_tst_s(State* c, const Insn& in) { alu_logic<S>(c, ea_read<S>(c, in.dst)); }
M68K_INLINE void op_tst(State* c, const Insn& in) { M68K_SIZED(op_tst_s, in); }

M68K_INLINE void op_tas(State* c, const Insn& in) {
    uint32_t addr = 0;
    uint32_t v = ea_rmw_read<1>(c, in.dst, addr);
    alu_logic<1>(c, v);
    ea_rmw_write<1>(c, in.dst, addr, v | 0x80);
}

enum class Bcd { ABCD, SBCD };
template <Bcd K> M68K_INLINE void op_bcd(State* c, const Insn& in) {
    uint32_t s = ea_read<1>(c, in.src);
    uint32_t addr = 0;
    uint32_t d = ea_rmw_read<1>(c, in.dst, addr);
    uint32_t r = K == Bcd::ABCD ? alu_abcd(c, s, d) : alu_sbcd(c, s, d);
    ea_rmw_write<1>(c, in.dst, addr, r);
}
M68K_INLINE void op_nbcd(State* c, const Insn& in) {
    uint32_t addr = 0;
    uint32_t d = ea_rmw_read<1>(c, in.dst, addr);
    bool w = false;
    uint32_t r = alu_nbcd(c, d, w);
    if (w) ea_rmw_write<1>(c, in.dst, addr, r);
}

M68K_INLINE void op_mulu(State* c, const Insn& in) {
    uint32_t s = ea_read<2>(c, in.src);
    c->d[in.dst.reg] = alu_mulu(c, s, c->d[in.dst.reg]);
    c->cycles -= mulu_cycles(s);
}
M68K_INLINE void op_muls(State* c, const Insn& in) {
    uint32_t s = ea_read<2>(c, in.src);
    c->d[in.dst.reg] = alu_muls(c, s, c->d[in.dst.reg]);
    c->cycles -= muls_cycles(s);
}
template <bool SGN> M68K_INLINE void op_div(State* c, const Insn& in) {
    uint32_t s = ea_read<2>(c, in.src);
    if ((s & 0xFFFF) == 0) {
        exception(c, VEC_ZERO_DIVIDE, c->pc);
        return;
    }
    uint32_t d = c->d[in.dst.reg];
    bool ok = SGN ? alu_divs(c, s, d) : alu_divu(c, s, d);
    if (ok) c->d[in.dst.reg] = d;
}

M68K_INLINE void op_chk(State* c, const Insn& in) {
    int32_t bound = int16_t(ea_read<2>(c, in.src));
    int32_t v = int16_t(c->d[in.dst.reg]);
    if (v < 0) { c->n = 1; exception(c, VEC_CHK, c->pc); }
    else if (v > bound) { c->n = 0; exception(c, VEC_CHK, c->pc); }
}

// ---------------------------------------------------------------------------
// Shifts
// ---------------------------------------------------------------------------
enum class Sh { ASL, ASR, LSL, LSR, ROL, ROR, ROXL, ROXR };
template <Sh K, int S> M68K_INLINE void op_shift_s(State* c, const Insn& in) {
    uint32_t cnt = in.src.mode == EA_IMM ? in.src.value : (c->d[in.src.reg] & 63);
    uint32_t addr = 0;
    uint32_t d = ea_rmw_read<S>(c, in.dst, addr);
    uint32_t r;
    if constexpr (K == Sh::ASL) r = alu_asl<S>(c, d, cnt);
    else if constexpr (K == Sh::ASR) r = alu_asr<S>(c, d, cnt);
    else if constexpr (K == Sh::LSL) r = alu_lsl<S>(c, d, cnt);
    else if constexpr (K == Sh::LSR) r = alu_lsr<S>(c, d, cnt);
    else if constexpr (K == Sh::ROL) r = alu_rol<S>(c, d, cnt);
    else if constexpr (K == Sh::ROR) r = alu_ror<S>(c, d, cnt);
    else if constexpr (K == Sh::ROXL) r = alu_roxl<S>(c, d, cnt);
    else r = alu_roxr<S>(c, d, cnt);
    ea_rmw_write<S>(c, in.dst, addr, r);
    if (in.dst.mode == EA_DREG) c->cycles -= int32_t(2 * cnt);
}
template <Sh K> M68K_INLINE void op_shift(State* c, const Insn& in) {
    switch (in.size) {
    case 1: op_shift_s<K, 1>(c, in); break;
    case 2: op_shift_s<K, 2>(c, in); break;
    default: op_shift_s<K, 4>(c, in); break;
    }
}

// ---------------------------------------------------------------------------
// Bit operations
// ---------------------------------------------------------------------------
enum class Bit { TST, CHG, CLR, SET };
template <Bit K> M68K_INLINE void op_bit(State* c, const Insn& in) {
    uint32_t bitn = in.src.mode == EA_IMM ? in.src.value : c->d[in.src.reg];
    if (in.dst.mode == EA_DREG) {
        bitn &= 31;
        uint32_t& r = c->d[in.dst.reg];
        c->z = ((r >> bitn) & 1) == 0;
        if (K == Bit::CHG) r ^= 1u << bitn;
        else if (K == Bit::CLR) r &= ~(1u << bitn);
        else if (K == Bit::SET) r |= 1u << bitn;
        if (K == Bit::CLR || (K != Bit::TST && bitn >= 16)) c->cycles -= 2;
        return;
    }
    bitn &= 7;
    uint32_t addr = 0;
    uint32_t v = ea_rmw_read<1>(c, in.dst, addr);
    c->z = ((v >> bitn) & 1) == 0;
    if (K == Bit::TST) return;
    if (K == Bit::CHG) v ^= 1u << bitn;
    else if (K == Bit::CLR) v &= ~(1u << bitn);
    else v |= 1u << bitn;
    ea_rmw_write<1>(c, in.dst, addr, v);
}

// ---------------------------------------------------------------------------
// Status register ops (privileged ones check S)
// ---------------------------------------------------------------------------
M68K_INLINE bool priv_check(State* c, const Insn& in) {
    if (!c->s) { exception(c, VEC_PRIVILEGE, in.pc); return false; }
    return true;
}
M68K_INLINE void op_ccr_imm(State* c, const Insn& in) {
    uint32_t ccr = get_ccr(c);
    if (in.op == Op::ORI_CCR) ccr |= in.imm;
    else if (in.op == Op::ANDI_CCR) ccr &= in.imm;
    else ccr ^= in.imm;
    set_ccr(c, ccr);
}
M68K_INLINE void op_sr_imm(State* c, const Insn& in) {
    if (!priv_check(c, in)) return;
    uint32_t sr = get_sr(c);
    if (in.op == Op::ORI_SR) sr |= in.imm;
    else if (in.op == Op::ANDI_SR) sr &= in.imm;
    else sr ^= in.imm;
    set_sr(c, sr);
}
M68K_INLINE void op_move_from_sr(State* c, const Insn& in) {
    uint32_t addr = 0;
    if (in.dst.mode != EA_DREG) addr = ea_addr<2>(c, in.dst);
    ea_rmw_write<2>(c, in.dst, addr, get_sr(c));
}
M68K_INLINE void op_move_to_ccr(State* c, const Insn& in) { set_ccr(c, ea_read<2>(c, in.src)); }
M68K_INLINE void op_move_to_sr(State* c, const Insn& in) {
    if (!priv_check(c, in)) return;
    set_sr(c, ea_read<2>(c, in.src));
}
M68K_INLINE void op_move_usp(State* c, const Insn& in) {
    if (!priv_check(c, in)) return;
    if (in.op == Op::MOVE_TO_USP) c->other_sp = c->a[in.reg];
    else c->a[in.reg] = c->other_sp;
}

// ---------------------------------------------------------------------------
// Control flow (used by the interpreter; generated code emits these inline)
// ---------------------------------------------------------------------------
M68K_INLINE void op_link(State* c, const Insn& in) {
    push32(c, c->a[in.reg]);
    c->a[in.reg] = c->a[7];
    c->a[7] += in.imm;
}
M68K_INLINE void op_unlk(State* c, const Insn& in) {
    c->a[7] = c->a[in.reg];
    c->a[in.reg] = pop32(c);
}
M68K_INLINE void op_rts(State* c) { c->pc = pop32(c); }
M68K_INLINE void op_rtr(State* c) { set_ccr(c, pop16(c)); c->pc = pop32(c); }
M68K_INLINE void op_rte(State* c, const Insn& in) {
    if (!priv_check(c, in)) return;
    uint32_t sr = pop16(c);
    c->pc = pop32(c);
    set_sr(c, sr);
}
M68K_INLINE void op_jsr(State* c, const Insn& in) {
    uint32_t t = ea_addr<4>(c, in.src);
    push32(c, in.pc + in.len);
    c->pc = t;
}
M68K_INLINE void op_jmp(State* c, const Insn& in) { c->pc = ea_addr<4>(c, in.src); }
M68K_INLINE void op_bsr(State* c, const Insn& in) { push32(c, in.pc + in.len); c->pc = in.target; }
// Returns true if taken.
M68K_INLINE bool op_bcc(State* c, const Insn& in) {
    if (cond(c, in.cc)) { c->pc = in.target; return true; }
    c->cycles += in.size == 1 ? 2 : -2;  // not taken: 8 (short) / 12 (word)
    return false;
}
M68K_INLINE bool op_dbcc(State* c, const Insn& in) {
    if (cond(c, in.cc)) { c->cycles -= 2; return false; }
    uint32_t& r = c->d[in.dst.reg];
    uint32_t cnt = (r - 1) & 0xFFFF;
    set_reg<2>(r, cnt);
    if (cnt != 0xFFFF) { c->pc = in.target; return true; }
    c->cycles -= 4;
    return false;
}
M68K_INLINE void op_scc(State* c, const Insn& in) {
    bool t = cond(c, in.cc);
    uint32_t addr = 0;
    if (in.dst.mode != EA_DREG) addr = ea_addr<1>(c, in.dst);
    ea_rmw_write<1>(c, in.dst, addr, t ? 0xFF : 0x00);
    if (t && in.dst.mode == EA_DREG) c->cycles -= 2;
}
M68K_INLINE void op_trap(State* c, const Insn& in) { exception(c, VEC_TRAP_BASE + int(in.imm), in.pc + in.len); }
M68K_INLINE void op_trapv(State* c, const Insn& in) { if (c->v) exception(c, VEC_TRAPV, in.pc + in.len); }
M68K_INLINE void op_stop(State* c, const Insn& in) {
    if (!priv_check(c, in)) return;
    set_sr(c, in.imm);
    c->stopped = 1;
}
M68K_INLINE void op_reset(State* c, const Insn& in) {
    if (!priv_check(c, in)) return;
    if (c->reset_hook) c->reset_hook(c->irq_user);
}

// Execute any decoded instruction. c->pc must already point past it.
void execute(State* c, const Insn& in);

} // namespace m68k
