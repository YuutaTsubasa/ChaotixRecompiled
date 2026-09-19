// SH-2 instruction decoder shared by interpreter, disassembler, analyzer and recompiler.
#pragma once
#include <cstdint>
#include <string>

namespace sh2 {

enum class Op : uint8_t {
    INVALID,
    // 0000
    STC_SR, STC_GBR, STC_VBR, STS_MACH, STS_MACL, STS_PR, BSRF, BRAF,
    MOVB_S0, MOVW_S0, MOVL_S0,     // MOV.x Rm,@(R0,Rn)
    MUL_L, CLRT, SETT, CLRMAC, NOP, DIV0U, MOVT, RTS, SLEEP, RTE,
    MOVB_L0, MOVW_L0, MOVL_L0,     // MOV.x @(R0,Rm),Rn
    MAC_L,
    // 0001
    MOVL_S4,                       // MOV.L Rm,@(disp,Rn)
    // 0010
    MOVB_S, MOVW_S, MOVL_S,        // MOV.x Rm,@Rn
    MOVB_M, MOVW_M, MOVL_M,        // MOV.x Rm,@-Rn
    DIV0S, TST, AND, XOR, OR, CMP_STR, XTRCT, MULU_W, MULS_W,
    // 0011
    CMP_EQ, CMP_HS, CMP_GE, DIV1, DMULU, CMP_HI, CMP_GT, SUB, SUBC, SUBV, ADD, DMULS, ADDC, ADDV,
    // 0100
    SHLL, DT, SHAL, SHLR, CMP_PZ, SHAR,
    STSL_MACH, STSL_MACL, STSL_PR, STCL_SR, STCL_GBR, STCL_VBR,
    ROTL, ROTCL, ROTR, CMP_PL, ROTCR,
    LDSL_MACH, LDSL_MACL, LDSL_PR, LDCL_SR, LDCL_GBR, LDCL_VBR,
    SHLL2, SHLL8, SHLL16, SHLR2, SHLR8, SHLR16,
    LDS_MACH, LDS_MACL, LDS_PR, JSR, TAS, JMP, LDC_SR, LDC_GBR, LDC_VBR, MAC_W,
    // 0101
    MOVL_L4,                       // MOV.L @(disp,Rm),Rn
    // 0110
    MOVB_L, MOVW_L, MOVL_L, MOV, MOVB_P, MOVW_P, MOVL_P, NOT, SWAP_B, SWAP_W, NEGC, NEG,
    EXTU_B, EXTU_W, EXTS_B, EXTS_W,
    // 0111
    ADD_I,
    // 1000
    MOVB_S4, MOVW_S4, MOVB_L4, MOVW_L4, CMP_EQ_I, BT, BF, BT_S, BF_S,
    // 1001
    MOVW_PC,
    // 1010/1011
    BRA, BSR,
    // 1100
    MOVB_SG, MOVW_SG, MOVL_SG, TRAPA, MOVB_LG, MOVW_LG, MOVL_LG, MOVA,
    TST_I, AND_I, XOR_I, OR_I, TST_B, AND_B, XOR_B, OR_B,
    // 1101
    MOVL_PC,
    // 1110
    MOV_I,
    COUNT
};

enum InsnFlags : uint16_t {
    IF_BRANCH = 1,        // control transfer (ends block)
    IF_DELAYED = 2,       // has a delay slot
    IF_COND = 4,          // conditional
    IF_CALL = 8,          // sets PR
    IF_RETURN = 16,       // RTS / RTE
    IF_INDIRECT = 32,     // target from register
    IF_END_BLOCK = 64,    // modifies SR / traps / sleeps
    IF_NO_FALLTHROUGH = 128,
    IF_SLOT_ILLEGAL = 256, // may not appear in a delay slot
    IF_PCREL = 512,        // uses PC-relative addressing
};

struct Insn {
    uint32_t pc = 0;
    uint16_t opcode = 0;
    Op op = Op::INVALID;
    uint8_t n = 0, m = 0;
    int32_t imm = 0;      // immediate or scaled displacement
    uint32_t addr = 0;    // resolved PC-relative address or branch target
    uint8_t cycles = 1;
    uint16_t flags = 0;
};

// The decoder is a pure function of the opcode and its address.
Insn decode(uint32_t pc, uint16_t opcode);

inline bool ends_block(const Insn& in) {
    return (in.flags & (IF_BRANCH | IF_END_BLOCK)) != 0 || in.op == Op::INVALID;
}

std::string disassemble(const Insn& in);
const char* op_name(Op op);

} // namespace sh2
