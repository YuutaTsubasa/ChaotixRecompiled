#include "sh2_decode.h"
#include <cstdio>

namespace sh2 {

Insn decode(uint32_t pc, uint16_t op) {
    Insn o;
    o.pc = pc;
    o.opcode = op;
    const uint8_t n = (op >> 8) & 15, m = (op >> 4) & 15;
    o.n = n; o.m = m;
    const uint32_t d4 = op & 15, d8 = op & 0xFF;
    auto set = [&](Op x, uint8_t cyc = 1, uint16_t fl = 0) { o.op = x; o.cycles = cyc; o.flags = fl; };
    const uint16_t BR = IF_BRANCH | IF_SLOT_ILLEGAL;

    switch (op >> 12) {
    case 0x0:
        switch (op & 0xF) {
        case 0x2:
            if (m == 0) set(Op::STC_SR); else if (m == 1) set(Op::STC_GBR); else if (m == 2) set(Op::STC_VBR);
            break;
        case 0x3:
            if (m == 0) set(Op::BSRF, 2, BR | IF_DELAYED | IF_CALL | IF_INDIRECT);
            else if (m == 2) set(Op::BRAF, 2, BR | IF_DELAYED | IF_INDIRECT | IF_NO_FALLTHROUGH);
            break;
        case 0x4: set(Op::MOVB_S0); break;
        case 0x5: set(Op::MOVW_S0); break;
        case 0x6: set(Op::MOVL_S0); break;
        case 0x7: set(Op::MUL_L, 2); break;
        case 0x8:
            if (n == 0) { if (m == 0) set(Op::CLRT); else if (m == 1) set(Op::SETT); else if (m == 2) set(Op::CLRMAC); }
            break;
        case 0x9:
            if (n == 0 && m == 0) set(Op::NOP);
            else if (n == 0 && m == 1) set(Op::DIV0U);
            else if (m == 2) set(Op::MOVT);
            break;
        case 0xA:
            if (m == 0) set(Op::STS_MACH); else if (m == 1) set(Op::STS_MACL); else if (m == 2) set(Op::STS_PR);
            break;
        case 0xB:
            if (n == 0) {
                if (m == 0) set(Op::RTS, 2, BR | IF_DELAYED | IF_RETURN | IF_INDIRECT | IF_NO_FALLTHROUGH);
                else if (m == 1) set(Op::SLEEP, 3, IF_END_BLOCK | IF_SLOT_ILLEGAL);
                else if (m == 2) set(Op::RTE, 4, BR | IF_DELAYED | IF_RETURN | IF_INDIRECT | IF_NO_FALLTHROUGH | IF_END_BLOCK);
            }
            break;
        case 0xC: set(Op::MOVB_L0); break;
        case 0xD: set(Op::MOVW_L0); break;
        case 0xE: set(Op::MOVL_L0); break;
        case 0xF: set(Op::MAC_L, 3); break;
        default: break;
        }
        break;
    case 0x1: set(Op::MOVL_S4); o.imm = int32_t(d4 * 4); break;
    case 0x2: {
        static const Op t[16] = {Op::MOVB_S, Op::MOVW_S, Op::MOVL_S, Op::INVALID, Op::MOVB_M, Op::MOVW_M, Op::MOVL_M, Op::DIV0S,
                                 Op::TST, Op::AND, Op::XOR, Op::OR, Op::CMP_STR, Op::XTRCT, Op::MULU_W, Op::MULS_W};
        set(t[op & 15]);
        break;
    }
    case 0x3: {
        static const Op t[16] = {Op::CMP_EQ, Op::INVALID, Op::CMP_HS, Op::CMP_GE, Op::DIV1, Op::DMULU, Op::CMP_HI, Op::CMP_GT,
                                 Op::SUB, Op::INVALID, Op::SUBC, Op::SUBV, Op::ADD, Op::DMULS, Op::ADDC, Op::ADDV};
        set(t[op & 15], (op & 15) == 5 || (op & 15) == 13 ? 2 : 1);
        break;
    }
    case 0x4:
        if ((op & 15) == 0xF) { set(Op::MAC_W, 2); break; }
        switch (op & 0xFF) {
        case 0x00: set(Op::SHLL); break;
        case 0x10: set(Op::DT); break;
        case 0x20: set(Op::SHAL); break;
        case 0x01: set(Op::SHLR); break;
        case 0x11: set(Op::CMP_PZ); break;
        case 0x21: set(Op::SHAR); break;
        case 0x02: set(Op::STSL_MACH); break;
        case 0x12: set(Op::STSL_MACL); break;
        case 0x22: set(Op::STSL_PR); break;
        case 0x03: set(Op::STCL_SR, 2); break;
        case 0x13: set(Op::STCL_GBR, 2); break;
        case 0x23: set(Op::STCL_VBR, 2); break;
        case 0x04: set(Op::ROTL); break;
        case 0x24: set(Op::ROTCL); break;
        case 0x05: set(Op::ROTR); break;
        case 0x15: set(Op::CMP_PL); break;
        case 0x25: set(Op::ROTCR); break;
        case 0x06: set(Op::LDSL_MACH); break;
        case 0x16: set(Op::LDSL_MACL); break;
        case 0x26: set(Op::LDSL_PR); break;
        case 0x07: set(Op::LDCL_SR, 3, IF_END_BLOCK); break;
        case 0x17: set(Op::LDCL_GBR, 3); break;
        case 0x27: set(Op::LDCL_VBR, 3); break;
        case 0x08: set(Op::SHLL2); break;
        case 0x18: set(Op::SHLL8); break;
        case 0x28: set(Op::SHLL16); break;
        case 0x09: set(Op::SHLR2); break;
        case 0x19: set(Op::SHLR8); break;
        case 0x29: set(Op::SHLR16); break;
        case 0x0A: set(Op::LDS_MACH); break;
        case 0x1A: set(Op::LDS_MACL); break;
        case 0x2A: set(Op::LDS_PR); break;
        case 0x0B: set(Op::JSR, 2, BR | IF_DELAYED | IF_CALL | IF_INDIRECT); break;
        case 0x1B: set(Op::TAS, 4); break;
        case 0x2B: set(Op::JMP, 2, BR | IF_DELAYED | IF_INDIRECT | IF_NO_FALLTHROUGH); break;
        case 0x0E: set(Op::LDC_SR, 1, IF_END_BLOCK); break;
        case 0x1E: set(Op::LDC_GBR); break;
        case 0x2E: set(Op::LDC_VBR); break;
        default: break;
        }
        break;
    case 0x5: set(Op::MOVL_L4); o.imm = int32_t(d4 * 4); break;
    case 0x6: {
        static const Op t[16] = {Op::MOVB_L, Op::MOVW_L, Op::MOVL_L, Op::MOV, Op::MOVB_P, Op::MOVW_P, Op::MOVL_P, Op::NOT,
                                 Op::SWAP_B, Op::SWAP_W, Op::NEGC, Op::NEG, Op::EXTU_B, Op::EXTU_W, Op::EXTS_B, Op::EXTS_W};
        set(t[op & 15]);
        break;
    }
    case 0x7: set(Op::ADD_I); o.imm = int8_t(d8); break;
    case 0x8:
        switch (n) {
        case 0x0: set(Op::MOVB_S4); o.n = m; o.m = 0; o.imm = int32_t(d4); break;       // MOV.B R0,@(disp,Rn): Rn in bits 7-4
        case 0x1: set(Op::MOVW_S4); o.n = m; o.m = 0; o.imm = int32_t(d4 * 2); break;
        case 0x4: set(Op::MOVB_L4); o.m = m; o.n = 0; o.imm = int32_t(d4); break;       // MOV.B @(disp,Rm),R0
        case 0x5: set(Op::MOVW_L4); o.m = m; o.n = 0; o.imm = int32_t(d4 * 2); break;
        case 0x8: set(Op::CMP_EQ_I); o.imm = int8_t(d8); break;
        case 0x9: case 0xB: case 0xD: case 0xF: {
            int32_t disp = int8_t(d8) * 2;
            o.addr = pc + 4 + uint32_t(disp);
            o.imm = disp;
            if (n == 0x9) set(Op::BT, 3, BR | IF_COND);
            else if (n == 0xB) set(Op::BF, 3, BR | IF_COND);
            else if (n == 0xD) set(Op::BT_S, 2, BR | IF_COND | IF_DELAYED);
            else set(Op::BF_S, 2, BR | IF_COND | IF_DELAYED);
            break;
        }
        default: break;
        }
        break;
    case 0x9:
        set(Op::MOVW_PC, 1, IF_PCREL);
        o.imm = int32_t(d8 * 2);
        o.addr = pc + 4 + uint32_t(o.imm);
        break;
    case 0xA: case 0xB: {
        int32_t disp = (int32_t(op << 20) >> 20) * 2;
        o.imm = disp;
        o.addr = pc + 4 + uint32_t(disp);
        if ((op >> 12) == 0xA) set(Op::BRA, 2, BR | IF_DELAYED | IF_NO_FALLTHROUGH);
        else set(Op::BSR, 2, BR | IF_DELAYED | IF_CALL);
        break;
    }
    case 0xC: {
        switch (n) {
        case 0x0: set(Op::MOVB_SG); o.imm = int32_t(d8); break;
        case 0x1: set(Op::MOVW_SG); o.imm = int32_t(d8 * 2); break;
        case 0x2: set(Op::MOVL_SG); o.imm = int32_t(d8 * 4); break;
        case 0x3: set(Op::TRAPA, 8, IF_BRANCH | IF_END_BLOCK | IF_SLOT_ILLEGAL | IF_INDIRECT | IF_NO_FALLTHROUGH); o.imm = int32_t(d8); break;
        case 0x4: set(Op::MOVB_LG); o.imm = int32_t(d8); break;
        case 0x5: set(Op::MOVW_LG); o.imm = int32_t(d8 * 2); break;
        case 0x6: set(Op::MOVL_LG); o.imm = int32_t(d8 * 4); break;
        case 0x7: set(Op::MOVA, 1, IF_PCREL); o.imm = int32_t(d8 * 4); o.addr = ((pc + 4) & ~3u) + uint32_t(o.imm); break;
        case 0x8: set(Op::TST_I); o.imm = int32_t(d8); break;
        case 0x9: set(Op::AND_I); o.imm = int32_t(d8); break;
        case 0xA: set(Op::XOR_I); o.imm = int32_t(d8); break;
        case 0xB: set(Op::OR_I); o.imm = int32_t(d8); break;
        case 0xC: set(Op::TST_B, 3); o.imm = int32_t(d8); break;
        case 0xD: set(Op::AND_B, 3); o.imm = int32_t(d8); break;
        case 0xE: set(Op::XOR_B, 3); o.imm = int32_t(d8); break;
        case 0xF: set(Op::OR_B, 3); o.imm = int32_t(d8); break;
        }
        break;
    }
    case 0xD:
        set(Op::MOVL_PC, 1, IF_PCREL);
        o.imm = int32_t(d8 * 4);
        o.addr = ((pc + 4) & ~3u) + uint32_t(o.imm);
        break;
    case 0xE: set(Op::MOV_I); o.imm = int8_t(d8); break;
    default: break;
    }
    if (o.op == Op::INVALID) { o.flags = IF_BRANCH | IF_END_BLOCK | IF_NO_FALLTHROUGH | IF_INDIRECT | IF_SLOT_ILLEGAL; o.cycles = 1; }
    return o;
}

const char* op_name(Op op) {
    static const char* names[] = {
        "invalid",
        "stc sr", "stc gbr", "stc vbr", "sts mach", "sts macl", "sts pr", "bsrf", "braf",
        "mov.b", "mov.w", "mov.l", "mul.l", "clrt", "sett", "clrmac", "nop", "div0u", "movt", "rts", "sleep", "rte",
        "mov.b", "mov.w", "mov.l", "mac.l",
        "mov.l",
        "mov.b", "mov.w", "mov.l", "mov.b", "mov.w", "mov.l",
        "div0s", "tst", "and", "xor", "or", "cmp/str", "xtrct", "mulu.w", "muls.w",
        "cmp/eq", "cmp/hs", "cmp/ge", "div1", "dmulu.l", "cmp/hi", "cmp/gt", "sub", "subc", "subv", "add", "dmuls.l", "addc", "addv",
        "shll", "dt", "shal", "shlr", "cmp/pz", "shar",
        "sts.l mach", "sts.l macl", "sts.l pr", "stc.l sr", "stc.l gbr", "stc.l vbr",
        "rotl", "rotcl", "rotr", "cmp/pl", "rotcr",
        "lds.l mach", "lds.l macl", "lds.l pr", "ldc.l sr", "ldc.l gbr", "ldc.l vbr",
        "shll2", "shll8", "shll16", "shlr2", "shlr8", "shlr16",
        "lds mach", "lds macl", "lds pr", "jsr", "tas.b", "jmp", "ldc sr", "ldc gbr", "ldc vbr", "mac.w",
        "mov.l",
        "mov.b", "mov.w", "mov.l", "mov", "mov.b", "mov.w", "mov.l", "not", "swap.b", "swap.w", "negc", "neg",
        "extu.b", "extu.w", "exts.b", "exts.w",
        "add",
        "mov.b", "mov.w", "mov.b", "mov.w", "cmp/eq", "bt", "bf", "bt/s", "bf/s",
        "mov.w",
        "bra", "bsr",
        "mov.b", "mov.w", "mov.l", "trapa", "mov.b", "mov.w", "mov.l", "mova",
        "tst", "and", "xor", "or", "tst.b", "and.b", "xor.b", "or.b",
        "mov.l",
        "mov",
    };
    static_assert(sizeof(names) / sizeof(names[0]) == size_t(Op::COUNT), "sh2 op names");
    return names[size_t(op)];
}

std::string disassemble(const Insn& in) {
    char b[96];
    const char* nm = op_name(in.op);
    int n = in.n, m = in.m;
    switch (in.op) {
    case Op::INVALID: snprintf(b, sizeof b, ".word $%04X", in.opcode); break;
    case Op::STC_SR: case Op::STC_GBR: case Op::STC_VBR: case Op::STS_MACH: case Op::STS_MACL: case Op::STS_PR:
    case Op::MOVT: snprintf(b, sizeof b, "%s,r%d", nm, n); if (in.op == Op::MOVT) snprintf(b, sizeof b, "movt r%d", n); break;
    case Op::BSRF: case Op::BRAF: case Op::JSR: case Op::JMP: snprintf(b, sizeof b, "%s %sr%d", nm, (in.op == Op::JSR || in.op == Op::JMP) ? "@" : "", n); break;
    case Op::MOVB_S0: case Op::MOVW_S0: case Op::MOVL_S0: snprintf(b, sizeof b, "%s r%d,@(r0,r%d)", nm, m, n); break;
    case Op::MOVB_L0: case Op::MOVW_L0: case Op::MOVL_L0: snprintf(b, sizeof b, "%s @(r0,r%d),r%d", nm, m, n); break;
    case Op::CLRT: case Op::SETT: case Op::CLRMAC: case Op::NOP: case Op::DIV0U: case Op::RTS: case Op::SLEEP: case Op::RTE:
        snprintf(b, sizeof b, "%s", nm); break;
    case Op::MAC_L: case Op::MAC_W: snprintf(b, sizeof b, "%s @r%d+,@r%d+", nm, m, n); break;
    case Op::MOVL_S4: snprintf(b, sizeof b, "mov.l r%d,@(%d,r%d)", m, in.imm, n); break;
    case Op::MOVL_L4: snprintf(b, sizeof b, "mov.l @(%d,r%d),r%d", in.imm, m, n); break;
    case Op::MOVB_S: case Op::MOVW_S: case Op::MOVL_S: snprintf(b, sizeof b, "%s r%d,@r%d", nm, m, n); break;
    case Op::MOVB_M: case Op::MOVW_M: case Op::MOVL_M: snprintf(b, sizeof b, "%s r%d,@-r%d", nm, m, n); break;
    case Op::MOVB_L: case Op::MOVW_L: case Op::MOVL_L: snprintf(b, sizeof b, "%s @r%d,r%d", nm, m, n); break;
    case Op::MOVB_P: case Op::MOVW_P: case Op::MOVL_P: snprintf(b, sizeof b, "%s @r%d+,r%d", nm, m, n); break;
    case Op::SHLL: case Op::DT: case Op::SHAL: case Op::SHLR: case Op::CMP_PZ: case Op::SHAR: case Op::ROTL: case Op::ROTCL:
    case Op::ROTR: case Op::CMP_PL: case Op::ROTCR: case Op::SHLL2: case Op::SHLL8: case Op::SHLL16: case Op::SHLR2:
    case Op::SHLR8: case Op::SHLR16:
        snprintf(b, sizeof b, "%s r%d", nm, n); break;
    case Op::TAS: snprintf(b, sizeof b, "tas.b @r%d", n); break;
    case Op::STSL_MACH: case Op::STSL_MACL: case Op::STSL_PR: case Op::STCL_SR: case Op::STCL_GBR: case Op::STCL_VBR:
        snprintf(b, sizeof b, "%s,@-r%d", nm, n); break;
    case Op::LDSL_MACH: case Op::LDSL_MACL: case Op::LDSL_PR: case Op::LDCL_SR: case Op::LDCL_GBR: case Op::LDCL_VBR:
        snprintf(b, sizeof b, "%.5s @r%d+,%s", nm, n, nm + 6); break;
    case Op::LDS_MACH: case Op::LDS_MACL: case Op::LDS_PR: case Op::LDC_SR: case Op::LDC_GBR: case Op::LDC_VBR:
        snprintf(b, sizeof b, "%.3s r%d,%s", nm, n, nm + 4); break;
    case Op::ADD_I: case Op::MOV_I: snprintf(b, sizeof b, "%s #%d,r%d", nm, in.imm, n); break;
    case Op::MOVB_S4: case Op::MOVW_S4: snprintf(b, sizeof b, "%s r0,@(%d,r%d)", nm, in.imm, n); break;
    case Op::MOVB_L4: case Op::MOVW_L4: snprintf(b, sizeof b, "%s @(%d,r%d),r0", nm, in.imm, m); break;
    case Op::CMP_EQ_I: snprintf(b, sizeof b, "cmp/eq #%d,r0", in.imm); break;
    case Op::BT: case Op::BF: case Op::BT_S: case Op::BF_S: case Op::BRA: case Op::BSR:
        snprintf(b, sizeof b, "%s $%08X", nm, in.addr); break;
    case Op::MOVW_PC: case Op::MOVL_PC: snprintf(b, sizeof b, "%s @($%08X),r%d", nm, in.addr, n); break;
    case Op::MOVA: snprintf(b, sizeof b, "mova @($%08X),r0", in.addr); break;
    case Op::MOVB_SG: case Op::MOVW_SG: case Op::MOVL_SG: snprintf(b, sizeof b, "%s r0,@(%d,gbr)", nm, in.imm); break;
    case Op::MOVB_LG: case Op::MOVW_LG: case Op::MOVL_LG: snprintf(b, sizeof b, "%s @(%d,gbr),r0", nm, in.imm); break;
    case Op::TRAPA: snprintf(b, sizeof b, "trapa #%d", in.imm); break;
    case Op::TST_I: case Op::AND_I: case Op::XOR_I: case Op::OR_I: snprintf(b, sizeof b, "%s #$%X,r0", nm, in.imm); break;
    case Op::TST_B: case Op::AND_B: case Op::XOR_B: case Op::OR_B: snprintf(b, sizeof b, "%s #$%X,@(r0,gbr)", nm, in.imm); break;
    default: snprintf(b, sizeof b, "%s r%d,r%d", nm, m, n); break;
    }
    return b;
}

} // namespace sh2
