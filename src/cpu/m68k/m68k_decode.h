// 68000 instruction decoder. The decoded form is shared by the reference
// interpreter, the disassembler, the ROM analyzer and the static recompiler.
#pragma once
#include <cstdint>
#include <string>

namespace m68k {

enum class Op : uint8_t {
    INVALID,
    ORI_CCR, ORI_SR, ANDI_CCR, ANDI_SR, EORI_CCR, EORI_SR,
    ORI, ANDI, SUBI, ADDI, EORI, CMPI,
    BTST, BCHG, BCLR, BSET,
    MOVEP_MR, MOVEP_RM,
    MOVE, MOVEA,
    MOVE_FROM_SR, MOVE_TO_CCR, MOVE_TO_SR,
    NEGX, CLR, NEG, NOT, EXT, NBCD, SWAP, PEA, ILLEGAL, TAS, TST,
    TRAP, LINK, UNLK, MOVE_TO_USP, MOVE_FROM_USP,
    RESET, NOP, STOP, RTE, RTS, TRAPV, RTR, JSR, JMP,
    MOVEM_RM, MOVEM_MR, LEA, CHK,
    ADDQ, SUBQ, SCC, DBCC, BRA, BSR, BCC, MOVEQ,
    DIVU, DIVS, SBCD, OR, SUB, SUBX, SUBA, EOR, CMPM, CMP, CMPA,
    MULU, MULS, ABCD, EXG, AND, ADD, ADDX, ADDA,
    ASL, ASR, LSL, LSR, ROXL, ROXR, ROL, ROR,
    LINE_A, LINE_F,
    COUNT
};

enum EaMode : uint8_t {
    EA_NONE, EA_DREG, EA_AREG, EA_IND, EA_POSTINC, EA_PREDEC, EA_DISP,
    EA_INDEX, EA_ABSW, EA_ABSL, EA_PCDISP, EA_PCINDEX, EA_IMM
};

struct Ea {
    EaMode mode = EA_NONE;
    uint8_t reg = 0;     // An / Dn number
    uint8_t xreg = 0;    // index register 0-7 = D0-D7, 8-15 = A0-A7
    uint8_t xlong = 0;   // index size: 1 = long, 0 = sign-extended word
    int32_t disp = 0;    // displacement for DISP/INDEX/PCINDEX
    uint32_t value = 0;  // absolute address, immediate, or resolved PC-relative address
};

enum InsnFlags : uint16_t {
    IF_BRANCH = 1,      // may transfer control (ends basic block)
    IF_COND = 2,        // conditional branch (falls through too)
    IF_CALL = 4,        // subroutine call (BSR/JSR)
    IF_RETURN = 8,      // RTS/RTE/RTR
    IF_INDIRECT = 16,   // target not statically known
    IF_END_BLOCK = 32,  // ends block for other reasons (SR change, STOP, trap...)
    IF_PRIV = 64,       // privileged
    IF_NO_FALLTHROUGH = 128,
};

struct Insn {
    uint32_t pc = 0;
    uint16_t opcode = 0;
    uint8_t len = 2;
    Op op = Op::INVALID;
    uint8_t size = 0;    // 1, 2, 4 (0 if n/a)
    uint8_t cc = 0;      // condition code
    uint8_t reg = 0;     // auxiliary register (MOVEP Dn, LINK An, EXG...)
    Ea src, dst;
    uint32_t imm = 0;    // quick data, mask, vector, stop value...
    uint32_t target = 0; // static branch target
    uint16_t cycles = 4; // base cycle count
    uint16_t flags = 0;
};

// Basic-block boundary rule shared by the interpreter and the recompiler.
// Interrupts and time-slice ends are only observed at these boundaries, which
// is what makes interpreted and recompiled execution deterministic-equivalent.
inline bool ends_block(const Insn& in) {
    return (in.flags & (IF_BRANCH | IF_END_BLOCK | IF_PRIV)) != 0 || in.op == Op::INVALID;
}

using Fetch16 = uint16_t (*)(void* user, uint32_t addr);

// Decodes the instruction at `pc`. Returns false if the encoding is invalid
// on a 68000 (out.op is set to INVALID/ILLEGAL/LINE_A/LINE_F accordingly).
bool decode(uint32_t pc, Fetch16 fetch, void* user, Insn& out);

std::string disassemble(const Insn& in);
const char* op_name(Op op);

} // namespace m68k
