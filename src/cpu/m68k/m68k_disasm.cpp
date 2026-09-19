#include "m68k_decode.h"
#include <cstdio>

namespace m68k {

const char* op_name(Op op) {
    static const char* names[] = {
        "invalid",
        "ori", "ori", "andi", "andi", "eori", "eori",
        "ori", "andi", "subi", "addi", "eori", "cmpi",
        "btst", "bchg", "bclr", "bset",
        "movep", "movep",
        "move", "movea",
        "move", "move", "move",
        "negx", "clr", "neg", "not", "ext", "nbcd", "swap", "pea", "illegal", "tas", "tst",
        "trap", "link", "unlk", "move", "move",
        "reset", "nop", "stop", "rte", "rts", "trapv", "rtr", "jsr", "jmp",
        "movem", "movem", "lea", "chk",
        "addq", "subq", "s", "db", "bra", "bsr", "b", "moveq",
        "divu", "divs", "sbcd", "or", "sub", "subx", "suba", "eor", "cmpm", "cmp", "cmpa",
        "mulu", "muls", "abcd", "exg", "and", "add", "addx", "adda",
        "asl", "asr", "lsl", "lsr", "roxl", "roxr", "rol", "ror",
        "linea", "linef",
    };
    static_assert(sizeof(names) / sizeof(names[0]) == size_t(Op::COUNT), "op name table");
    return names[size_t(op)];
}

static const char* cc_name(int cc) {
    static const char* n[16] = {"t", "f", "hi", "ls", "cc", "cs", "ne", "eq", "vc", "vs", "pl", "mi", "ge", "lt", "gt", "le"};
    return n[cc & 15];
}

static std::string ea_str(const Ea& e) {
    char b[64];
    auto xr = [&](char* out, size_t n) {
        snprintf(out, n, "%c%d.%c", e.xreg >= 8 ? 'a' : 'd', e.xreg & 7, e.xlong ? 'l' : 'w');
    };
    char x[16];
    switch (e.mode) {
    case EA_DREG: snprintf(b, sizeof b, "d%d", e.reg); break;
    case EA_AREG: snprintf(b, sizeof b, e.reg == 7 ? "sp" : "a%d", e.reg); break;
    case EA_IND: snprintf(b, sizeof b, "(a%d)", e.reg); break;
    case EA_POSTINC: snprintf(b, sizeof b, "(a%d)+", e.reg); break;
    case EA_PREDEC: snprintf(b, sizeof b, "-(a%d)", e.reg); break;
    case EA_DISP: snprintf(b, sizeof b, "%s$%X(a%d)", e.disp < 0 ? "-" : "", e.disp < 0 ? -e.disp : e.disp, e.reg); break;
    case EA_INDEX: xr(x, sizeof x); snprintf(b, sizeof b, "%s$%X(a%d,%s)", e.disp < 0 ? "-" : "", e.disp < 0 ? -e.disp : e.disp, e.reg, x); break;
    case EA_ABSW: snprintf(b, sizeof b, "($%X).w", e.value); break;
    case EA_ABSL: snprintf(b, sizeof b, "($%X).l", e.value); break;
    case EA_PCDISP: snprintf(b, sizeof b, "$%X(pc)", e.value & 0xFFFFFF); break;
    case EA_PCINDEX: xr(x, sizeof x); snprintf(b, sizeof b, "$%X(pc,%s)", e.value & 0xFFFFFF, x); break;
    case EA_IMM: snprintf(b, sizeof b, "#$%X", e.value); break;
    default: b[0] = 0; break;
    }
    return b;
}

static std::string reglist(uint32_t mask, bool predec) {
    std::string s;
    for (int i = 0; i < 16; ++i) {
        int bit = predec ? 15 - i : i;
        if (mask & (1u << bit)) {
            if (!s.empty()) s += "/";
            char b[8];
            snprintf(b, sizeof b, "%c%d", i < 8 ? 'd' : 'a', i & 7);
            s += b;
        }
    }
    return s;
}

std::string disassemble(const Insn& in) {
    char b[160];
    const char* sz = in.size == 1 ? ".b" : in.size == 2 ? ".w" : in.size == 4 ? ".l" : "";
    std::string name = op_name(in.op);
    switch (in.op) {
    case Op::INVALID: snprintf(b, sizeof b, "dc.w $%04X", in.opcode); return b;
    case Op::ORI_CCR: case Op::ANDI_CCR: case Op::EORI_CCR:
        snprintf(b, sizeof b, "%s #$%X,ccr", name.c_str(), in.imm); return b;
    case Op::ORI_SR: case Op::ANDI_SR: case Op::EORI_SR:
        snprintf(b, sizeof b, "%s #$%X,sr", name.c_str(), in.imm); return b;
    case Op::MOVEP_MR:
        snprintf(b, sizeof b, "movep%s %s,d%d", sz, ea_str(in.dst).c_str(), in.reg); return b;
    case Op::MOVEP_RM:
        snprintf(b, sizeof b, "movep%s d%d,%s", sz, in.reg, ea_str(in.dst).c_str()); return b;
    case Op::MOVE_FROM_SR: snprintf(b, sizeof b, "move sr,%s", ea_str(in.dst).c_str()); return b;
    case Op::MOVE_TO_CCR: snprintf(b, sizeof b, "move %s,ccr", ea_str(in.src).c_str()); return b;
    case Op::MOVE_TO_SR: snprintf(b, sizeof b, "move %s,sr", ea_str(in.src).c_str()); return b;
    case Op::MOVE_TO_USP: snprintf(b, sizeof b, "move a%d,usp", in.reg); return b;
    case Op::MOVE_FROM_USP: snprintf(b, sizeof b, "move usp,a%d", in.reg); return b;
    case Op::TRAP: snprintf(b, sizeof b, "trap #%d", in.imm); return b;
    case Op::LINK: snprintf(b, sizeof b, "link a%d,#%d", in.reg, int32_t(in.imm)); return b;
    case Op::UNLK: snprintf(b, sizeof b, "unlk a%d", in.reg); return b;
    case Op::STOP: snprintf(b, sizeof b, "stop #$%X", in.imm); return b;
    case Op::RESET: case Op::NOP: case Op::RTE: case Op::RTS: case Op::TRAPV: case Op::RTR:
    case Op::ILLEGAL: case Op::LINE_A: case Op::LINE_F:
        return name;
    case Op::MOVEM_RM:
        snprintf(b, sizeof b, "movem%s %s,%s", sz, reglist(in.imm, in.dst.mode == EA_PREDEC).c_str(), ea_str(in.dst).c_str()); return b;
    case Op::MOVEM_MR:
        snprintf(b, sizeof b, "movem%s %s,%s", sz, ea_str(in.src).c_str(), reglist(in.imm, false).c_str()); return b;
    case Op::BRA: case Op::BSR:
        snprintf(b, sizeof b, "%s%s $%X", name.c_str(), in.size == 1 ? ".s" : "", in.target); return b;
    case Op::BCC:
        snprintf(b, sizeof b, "b%s%s $%X", cc_name(in.cc), in.size == 1 ? ".s" : "", in.target); return b;
    case Op::DBCC:
        snprintf(b, sizeof b, "db%s d%d,$%X", cc_name(in.cc), in.dst.reg, in.target); return b;
    case Op::SCC:
        snprintf(b, sizeof b, "s%s %s", cc_name(in.cc), ea_str(in.dst).c_str()); return b;
    case Op::JSR: case Op::JMP: case Op::PEA:
        snprintf(b, sizeof b, "%s %s", name.c_str(), ea_str(in.src).c_str()); return b;
    case Op::SWAP: case Op::EXT: case Op::CLR: case Op::NEG: case Op::NEGX: case Op::NOT:
    case Op::TST: case Op::TAS: case Op::NBCD:
        snprintf(b, sizeof b, "%s%s %s", name.c_str(), sz, ea_str(in.dst).c_str()); return b;
    default:
        break;
    }
    if (in.src.mode != EA_NONE && in.dst.mode != EA_NONE)
        snprintf(b, sizeof b, "%s%s %s,%s", name.c_str(), sz, ea_str(in.src).c_str(), ea_str(in.dst).c_str());
    else if (in.src.mode != EA_NONE)
        snprintf(b, sizeof b, "%s%s %s", name.c_str(), sz, ea_str(in.src).c_str());
    else
        snprintf(b, sizeof b, "%s%s %s", name.c_str(), sz, ea_str(in.dst).c_str());
    return b;
}

} // namespace m68k
