// SH-2 instruction semantics shared by the interpreter and generated code.
// Non-branch instructions are executed with exec_simple(); control transfer
// instructions are split into "compute target" helpers so the recompiler can
// emit the delay slot between target computation and the jump, exactly as the
// hardware does (target registers are sampled before the slot executes).
#pragma once
#include "sh2.h"
#include "sh2_decode.h"

#if defined(__GNUC__)
#define SH2_INLINE inline __attribute__((always_inline))
#else
#define SH2_INLINE __forceinline
#endif

namespace sh2 {

SH2_INLINE uint32_t sx8(uint32_t v) { return uint32_t(int32_t(int8_t(v))); }
SH2_INLINE uint32_t sx16(uint32_t v) { return uint32_t(int32_t(int16_t(v))); }

SH2_INLINE void div1(State* c, uint32_t n, uint32_t m) {
    uint32_t& Rn = c->r[n];
    const uint32_t Rm = c->r[m];
    const uint32_t old_q = (c->sr & SR_Q) ? 1 : 0;
    const uint32_t M = (c->sr & SR_M) ? 1 : 0;
    uint32_t Q = (Rn & 0x80000000u) ? 1 : 0;
    Rn = (Rn << 1) | (c->sr & SR_T);
    uint32_t tmp0 = Rn, tmp1;
    if (old_q == M) {
        Rn -= Rm;
        tmp1 = Rn > tmp0;
    } else {
        Rn += Rm;
        tmp1 = Rn < tmp0;
    }
    // Q update per the SH-2 programming manual table.
    if (old_q == 0) {
        if (M == 0) Q = Q ? (tmp1 == 0) : tmp1;
        else Q = Q ? tmp1 : (tmp1 == 0);
    } else {
        if (M == 0) Q = Q ? (tmp1 == 0) : tmp1;
        else Q = Q ? tmp1 : (tmp1 == 0);
    }
    c->sr = (c->sr & ~(SR_Q | SR_T)) | (Q ? SR_Q : 0) | ((Q == M) ? SR_T : 0);
}

SH2_INLINE void mac_l(State* c, uint32_t n, uint32_t m) {
    int32_t tn = int32_t(rd32(c, c->r[n])); c->r[n] += 4;
    int32_t tm = int32_t(rd32(c, c->r[m])); c->r[m] += 4;
    int64_t mac = int64_t((uint64_t(c->mach) << 32) | c->macl);
    mac += int64_t(tn) * int64_t(tm);
    if (c->sr & SR_S) {
        if (mac > int64_t(0x00007FFFFFFFFFFFLL)) mac = 0x00007FFFFFFFFFFFLL;
        else if (mac < -int64_t(0x0000800000000000LL)) mac = -int64_t(0x0000800000000000LL);
    }
    c->mach = uint32_t(uint64_t(mac) >> 32);
    c->macl = uint32_t(mac);
}
SH2_INLINE void mac_w(State* c, uint32_t n, uint32_t m) {
    int32_t tn = int16_t(rd16(c, c->r[n])); c->r[n] += 2;
    int32_t tm = int16_t(rd16(c, c->r[m])); c->r[m] += 2;
    int64_t prod = int64_t(tn) * tm;
    if (c->sr & SR_S) {
        int64_t s = int64_t(int32_t(c->macl)) + prod;
        if (s > 0x7FFFFFFFLL) { s = 0x7FFFFFFFLL; c->mach |= 1; }
        else if (s < -0x80000000LL) { s = -0x80000000LL; c->mach |= 1; }
        c->macl = uint32_t(s);
    } else {
        int64_t mac = int64_t((uint64_t(c->mach) << 32) | c->macl) + prod;
        c->mach = uint32_t(uint64_t(mac) >> 32);
        c->macl = uint32_t(mac);
    }
}

// Executes any instruction that is not a control transfer. Returns false if
// `in` is a branch (caller handles those).
SH2_INLINE void exec_simple(State* c, const Insn& in) {
    uint32_t* R = c->r;
    const uint32_t n = in.n, m = in.m;
    switch (in.op) {
    case Op::STC_SR: R[n] = c->sr; break;
    case Op::STC_GBR: R[n] = c->gbr; break;
    case Op::STC_VBR: R[n] = c->vbr; break;
    case Op::STS_MACH: R[n] = c->mach; break;
    case Op::STS_MACL: R[n] = c->macl; break;
    case Op::STS_PR: R[n] = c->pr; break;
    case Op::MOVB_S0: wr8(c, R[n] + R[0], R[m]); break;
    case Op::MOVW_S0: wr16(c, R[n] + R[0], R[m]); break;
    case Op::MOVL_S0: wr32(c, R[n] + R[0], R[m]); break;
    case Op::MUL_L: c->macl = R[n] * R[m]; break;
    case Op::CLRT: c->sr &= ~SR_T; break;
    case Op::SETT: c->sr |= SR_T; break;
    case Op::CLRMAC: c->mach = c->macl = 0; break;
    case Op::NOP: break;
    case Op::DIV0U: c->sr &= ~(SR_M | SR_Q | SR_T); break;
    case Op::MOVT: R[n] = c->sr & SR_T; break;
    case Op::MOVB_L0: R[n] = sx8(rd8(c, R[m] + R[0])); break;
    case Op::MOVW_L0: R[n] = sx16(rd16(c, R[m] + R[0])); break;
    case Op::MOVL_L0: R[n] = rd32(c, R[m] + R[0]); break;
    case Op::MAC_L: mac_l(c, n, m); break;
    case Op::MOVL_S4: wr32(c, R[n] + uint32_t(in.imm), R[m]); break;
    case Op::MOVB_S: wr8(c, R[n], R[m]); break;
    case Op::MOVW_S: wr16(c, R[n], R[m]); break;
    case Op::MOVL_S: wr32(c, R[n], R[m]); break;
    case Op::MOVB_M: { uint32_t v = R[m]; wr8(c, R[n] - 1, v); R[n] -= 1; break; }
    case Op::MOVW_M: { uint32_t v = R[m]; wr16(c, R[n] - 2, v); R[n] -= 2; break; }
    case Op::MOVL_M: { uint32_t v = R[m]; wr32(c, R[n] - 4, v); R[n] -= 4; break; }
    case Op::DIV0S: {
        uint32_t q = R[n] >> 31, mm = R[m] >> 31;
        c->sr = (c->sr & ~(SR_Q | SR_M | SR_T)) | (q ? SR_Q : 0) | (mm ? SR_M : 0) | ((q ^ mm) ? SR_T : 0);
        break;
    }
    case Op::TST: setT(c, (R[n] & R[m]) == 0); break;
    case Op::AND: R[n] &= R[m]; break;
    case Op::XOR: R[n] ^= R[m]; break;
    case Op::OR: R[n] |= R[m]; break;
    case Op::CMP_STR: {
        uint32_t t = R[n] ^ R[m];
        setT(c, (t & 0xFF000000u) == 0 || (t & 0x00FF0000u) == 0 || (t & 0x0000FF00u) == 0 || (t & 0xFFu) == 0);
        break;
    }
    case Op::XTRCT: R[n] = (R[m] << 16) | (R[n] >> 16); break;
    case Op::MULU_W: c->macl = (R[n] & 0xFFFF) * (R[m] & 0xFFFF); break;
    case Op::MULS_W: c->macl = uint32_t(int32_t(int16_t(R[n])) * int32_t(int16_t(R[m]))); break;
    case Op::CMP_EQ: setT(c, R[n] == R[m]); break;
    case Op::CMP_HS: setT(c, R[n] >= R[m]); break;
    case Op::CMP_GE: setT(c, int32_t(R[n]) >= int32_t(R[m])); break;
    case Op::DIV1: div1(c, n, m); break;
    case Op::DMULU: {
        uint64_t r = uint64_t(R[n]) * R[m];
        c->mach = uint32_t(r >> 32); c->macl = uint32_t(r);
        break;
    }
    case Op::CMP_HI: setT(c, R[n] > R[m]); break;
    case Op::CMP_GT: setT(c, int32_t(R[n]) > int32_t(R[m])); break;
    case Op::SUB: R[n] -= R[m]; break;
    case Op::SUBC: {
        uint32_t tmp1 = R[n] - R[m], tmp0 = R[n];
        uint32_t res = tmp1 - (c->sr & SR_T);
        setT(c, (tmp0 < tmp1) || (tmp1 < res));
        R[n] = res;
        break;
    }
    case Op::SUBV: {
        int32_t a = int32_t(R[n]), b = int32_t(R[m]);
        int64_t r = int64_t(a) - b;
        setT(c, r > INT32_MAX || r < INT32_MIN);
        R[n] = uint32_t(a) - uint32_t(b);
        break;
    }
    case Op::ADD: R[n] += R[m]; break;
    case Op::DMULS: {
        int64_t r = int64_t(int32_t(R[n])) * int64_t(int32_t(R[m]));
        c->mach = uint32_t(uint64_t(r) >> 32); c->macl = uint32_t(r);
        break;
    }
    case Op::ADDC: {
        uint32_t tmp1 = R[n] + R[m], tmp0 = R[n];
        uint32_t res = tmp1 + (c->sr & SR_T);
        setT(c, (tmp0 > tmp1) || (tmp1 > res));
        R[n] = res;
        break;
    }
    case Op::ADDV: {
        int64_t r = int64_t(int32_t(R[n])) + int32_t(R[m]);
        setT(c, r > INT32_MAX || r < INT32_MIN);
        R[n] = R[n] + R[m];
        break;
    }
    case Op::SHLL: case Op::SHAL: setT(c, R[n] >> 31); R[n] <<= 1; break;
    case Op::DT: R[n] -= 1; setT(c, R[n] == 0); break;
    case Op::SHLR: setT(c, R[n] & 1); R[n] >>= 1; break;
    case Op::CMP_PZ: setT(c, int32_t(R[n]) >= 0); break;
    case Op::SHAR: setT(c, R[n] & 1); R[n] = uint32_t(int32_t(R[n]) >> 1); break;
    case Op::STSL_MACH: R[n] -= 4; wr32(c, R[n], c->mach); break;
    case Op::STSL_MACL: R[n] -= 4; wr32(c, R[n], c->macl); break;
    case Op::STSL_PR: R[n] -= 4; wr32(c, R[n], c->pr); break;
    case Op::STCL_SR: R[n] -= 4; wr32(c, R[n], c->sr); break;
    case Op::STCL_GBR: R[n] -= 4; wr32(c, R[n], c->gbr); break;
    case Op::STCL_VBR: R[n] -= 4; wr32(c, R[n], c->vbr); break;
    case Op::ROTL: { uint32_t t = R[n] >> 31; R[n] = (R[n] << 1) | t; setT(c, t); break; }
    case Op::ROTCL: { uint32_t t = R[n] >> 31; R[n] = (R[n] << 1) | (c->sr & SR_T); setT(c, t); break; }
    case Op::ROTR: { uint32_t t = R[n] & 1; R[n] = (R[n] >> 1) | (t << 31); setT(c, t); break; }
    case Op::CMP_PL: setT(c, int32_t(R[n]) > 0); break;
    case Op::ROTCR: { uint32_t t = R[n] & 1; R[n] = (R[n] >> 1) | ((c->sr & SR_T) << 31); setT(c, t); break; }
    case Op::LDSL_MACH: c->mach = rd32(c, R[n]); R[n] += 4; break;
    case Op::LDSL_MACL: c->macl = rd32(c, R[n]); R[n] += 4; break;
    case Op::LDSL_PR: c->pr = rd32(c, R[n]); R[n] += 4; break;
    case Op::LDCL_SR: c->sr = rd32(c, R[n]) & SR_MASK; R[n] += 4; break;
    case Op::LDCL_GBR: c->gbr = rd32(c, R[n]); R[n] += 4; break;
    case Op::LDCL_VBR: c->vbr = rd32(c, R[n]); R[n] += 4; break;
    case Op::SHLL2: R[n] <<= 2; break;
    case Op::SHLL8: R[n] <<= 8; break;
    case Op::SHLL16: R[n] <<= 16; break;
    case Op::SHLR2: R[n] >>= 2; break;
    case Op::SHLR8: R[n] >>= 8; break;
    case Op::SHLR16: R[n] >>= 16; break;
    case Op::LDS_MACH: c->mach = R[n]; break;
    case Op::LDS_MACL: c->macl = R[n]; break;
    case Op::LDS_PR: c->pr = R[n]; break;
    case Op::TAS: {
        uint32_t v = rd8(c, R[n]);
        setT(c, v == 0);
        wr8(c, R[n], v | 0x80);
        break;
    }
    case Op::LDC_SR: c->sr = R[n] & SR_MASK; break;
    case Op::LDC_GBR: c->gbr = R[n]; break;
    case Op::LDC_VBR: c->vbr = R[n]; break;
    case Op::MAC_W: mac_w(c, n, m); break;
    case Op::MOVL_L4: R[n] = rd32(c, R[m] + uint32_t(in.imm)); break;
    case Op::MOVB_L: R[n] = sx8(rd8(c, R[m])); break;
    case Op::MOVW_L: R[n] = sx16(rd16(c, R[m])); break;
    case Op::MOVL_L: R[n] = rd32(c, R[m]); break;
    case Op::MOV: R[n] = R[m]; break;
    case Op::MOVB_P: { uint32_t v = sx8(rd8(c, R[m])); if (n != m) R[m] += 1; R[n] = v; break; }
    case Op::MOVW_P: { uint32_t v = sx16(rd16(c, R[m])); if (n != m) R[m] += 2; R[n] = v; break; }
    case Op::MOVL_P: { uint32_t v = rd32(c, R[m]); if (n != m) R[m] += 4; R[n] = v; break; }
    case Op::NOT: R[n] = ~R[m]; break;
    case Op::SWAP_B: R[n] = (R[m] & 0xFFFF0000u) | ((R[m] & 0xFF) << 8) | ((R[m] >> 8) & 0xFF); break;
    case Op::SWAP_W: R[n] = (R[m] << 16) | (R[m] >> 16); break;
    case Op::NEGC: {
        uint32_t tmp = 0 - R[m];
        uint32_t res = tmp - (c->sr & SR_T);
        setT(c, (0 < tmp) || (tmp < res));
        R[n] = res;
        break;
    }
    case Op::NEG: R[n] = 0 - R[m]; break;
    case Op::EXTU_B: R[n] = R[m] & 0xFF; break;
    case Op::EXTU_W: R[n] = R[m] & 0xFFFF; break;
    case Op::EXTS_B: R[n] = sx8(R[m]); break;
    case Op::EXTS_W: R[n] = sx16(R[m]); break;
    case Op::ADD_I: R[n] += uint32_t(in.imm); break;
    case Op::MOVB_S4: wr8(c, R[n] + uint32_t(in.imm), R[0]); break;
    case Op::MOVW_S4: wr16(c, R[n] + uint32_t(in.imm), R[0]); break;
    case Op::MOVB_L4: R[0] = sx8(rd8(c, R[m] + uint32_t(in.imm))); break;
    case Op::MOVW_L4: R[0] = sx16(rd16(c, R[m] + uint32_t(in.imm))); break;
    case Op::CMP_EQ_I: setT(c, R[0] == uint32_t(in.imm)); break;
    case Op::MOVW_PC: R[n] = sx16(rd16(c, in.addr)); break;
    case Op::MOVB_SG: wr8(c, c->gbr + uint32_t(in.imm), R[0]); break;
    case Op::MOVW_SG: wr16(c, c->gbr + uint32_t(in.imm), R[0]); break;
    case Op::MOVL_SG: wr32(c, c->gbr + uint32_t(in.imm), R[0]); break;
    case Op::MOVB_LG: R[0] = sx8(rd8(c, c->gbr + uint32_t(in.imm))); break;
    case Op::MOVW_LG: R[0] = sx16(rd16(c, c->gbr + uint32_t(in.imm))); break;
    case Op::MOVL_LG: R[0] = rd32(c, c->gbr + uint32_t(in.imm)); break;
    case Op::MOVA: R[0] = in.addr; break;
    case Op::TST_I: setT(c, (R[0] & uint32_t(in.imm)) == 0); break;
    case Op::AND_I: R[0] &= uint32_t(in.imm); break;
    case Op::XOR_I: R[0] ^= uint32_t(in.imm); break;
    case Op::OR_I: R[0] |= uint32_t(in.imm); break;
    case Op::TST_B: { uint32_t v = rd8(c, c->gbr + R[0]); setT(c, (v & uint32_t(in.imm)) == 0); break; }
    case Op::AND_B: { uint32_t a = c->gbr + R[0]; wr8(c, a, rd8(c, a) & uint32_t(in.imm)); break; }
    case Op::XOR_B: { uint32_t a = c->gbr + R[0]; wr8(c, a, rd8(c, a) ^ uint32_t(in.imm)); break; }
    case Op::OR_B: { uint32_t a = c->gbr + R[0]; wr8(c, a, rd8(c, a) | uint32_t(in.imm)); break; }
    case Op::MOVL_PC: R[n] = rd32(c, in.addr); break;
    case Op::MOV_I: R[n] = uint32_t(in.imm); break;
    case Op::SLEEP: c->sleeping = 1; break;
    default: break;
    }
}

// Branch target computation. Must be called BEFORE the delay slot executes.
// For conditional branches returns the target if taken, else the fall-through
// address. Calls also set PR; RTE pops PC and SR.
SH2_INLINE uint32_t branch_target(State* c, const Insn& in) {
    switch (in.op) {
    case Op::BRA: return in.addr;
    case Op::BSR: c->pr = in.pc + 4; return in.addr;
    case Op::BRAF: return in.pc + 4 + c->r[in.n];
    case Op::BSRF: c->pr = in.pc + 4; return in.pc + 4 + c->r[in.n];
    case Op::JMP: return c->r[in.n];
    case Op::JSR: c->pr = in.pc + 4; return c->r[in.n];
    case Op::RTS: return c->pr;
    case Op::RTE: {
        uint32_t pc = rd32(c, c->r[15]); c->r[15] += 4;
        c->sr = rd32(c, c->r[15]) & SR_MASK; c->r[15] += 4;
        return pc;
    }
    case Op::BT: return (c->sr & SR_T) ? in.addr : in.pc + 2;
    case Op::BF: return (c->sr & SR_T) ? in.pc + 2 : in.addr;
    case Op::BT_S: return (c->sr & SR_T) ? in.addr : in.pc + 4;
    case Op::BF_S: return (c->sr & SR_T) ? in.pc + 4 : in.addr;
    case Op::TRAPA: {
        c->r[15] -= 4; wr32(c, c->r[15], c->sr);
        c->r[15] -= 4; wr32(c, c->r[15], in.pc + 2);
        return rd32(c, c->vbr + uint32_t(in.imm) * 4);
    }
    default: {
        // Illegal instruction: general illegal instruction exception (vector 4).
        c->r[15] -= 4; wr32(c, c->r[15], c->sr);
        c->r[15] -= 4; wr32(c, c->r[15], in.pc);
        return rd32(c, c->vbr + 4 * 4);
    }
    }
}

// Non-delayed conditional branches take 3 cycles when taken, 1 when not.
SH2_INLINE int branch_cycle_adjust(const Insn& in, uint32_t target) {
    if ((in.op == Op::BT || in.op == Op::BF) && target == in.pc + 2) return 2;   // refund
    if ((in.op == Op::BT_S || in.op == Op::BF_S) && target == in.pc + 4) return 1;
    return 0;
}

} // namespace sh2
