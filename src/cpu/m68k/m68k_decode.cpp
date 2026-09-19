#include "m68k_decode.h"

namespace m68k {

namespace {

enum : uint32_t {
    M_DREG = 1u << EA_DREG, M_AREG = 1u << EA_AREG, M_IND = 1u << EA_IND,
    M_POSTINC = 1u << EA_POSTINC, M_PREDEC = 1u << EA_PREDEC, M_DISP = 1u << EA_DISP,
    M_INDEX = 1u << EA_INDEX, M_ABSW = 1u << EA_ABSW, M_ABSL = 1u << EA_ABSL,
    M_PCDISP = 1u << EA_PCDISP, M_PCINDEX = 1u << EA_PCINDEX, M_IMM = 1u << EA_IMM,
    ALL = M_DREG | M_AREG | M_IND | M_POSTINC | M_PREDEC | M_DISP | M_INDEX | M_ABSW |
          M_ABSL | M_PCDISP | M_PCINDEX | M_IMM,
    DATA = ALL & ~M_AREG,
    MEMORY = ALL & ~(M_DREG | M_AREG),
    CONTROL = M_IND | M_DISP | M_INDEX | M_ABSW | M_ABSL | M_PCDISP | M_PCINDEX,
    ALTERABLE = M_DREG | M_AREG | M_IND | M_POSTINC | M_PREDEC | M_DISP | M_INDEX | M_ABSW | M_ABSL,
    DATA_ALT = ALTERABLE & ~M_AREG,
    MEM_ALT = ALTERABLE & ~(M_DREG | M_AREG),
    CTRL_ALT = CONTROL & ~(M_PCDISP | M_PCINDEX),
};

struct Rd {
    Fetch16 f;
    void* u;
    uint32_t cur;
    uint16_t w() { uint16_t v = f(u, cur); cur += 2; return v; }
    uint32_t l() { uint32_t hi = w(); return (hi << 16) | w(); }
};

int ea_time(EaMode m, bool lng) {
    switch (m) {
    case EA_IND: case EA_POSTINC: return lng ? 8 : 4;
    case EA_PREDEC: return lng ? 10 : 6;
    case EA_DISP: case EA_ABSW: case EA_PCDISP: return lng ? 12 : 8;
    case EA_INDEX: case EA_PCINDEX: return lng ? 14 : 10;
    case EA_ABSL: return lng ? 16 : 12;
    case EA_IMM: return lng ? 8 : 4;
    default: return 0;
    }
}

bool dec_ea(Rd& r, int mode, int reg, int size, Ea& e, uint32_t allowed) {
    e = Ea{};
    e.reg = uint8_t(reg);
    switch (mode) {
    case 0: e.mode = EA_DREG; break;
    case 1: e.mode = EA_AREG; break;
    case 2: e.mode = EA_IND; break;
    case 3: e.mode = EA_POSTINC; break;
    case 4: e.mode = EA_PREDEC; break;
    case 5: e.mode = EA_DISP; e.disp = int16_t(r.w()); break;
    case 6: {
        e.mode = EA_INDEX;
        uint16_t ext = r.w();
        e.xreg = uint8_t((ext >> 12) & 15);
        e.xlong = (ext >> 11) & 1;
        e.disp = int8_t(ext & 0xFF);
        if (ext & 0x0700) return false;  // 68020 full format / scale
        break;
    }
    case 7:
        switch (reg) {
        // Absolute short is sign-extended to 32 bits (LEA/PEA expose all 32 bits;
        // the bus ignores A24-A31).
        case 0: e.mode = EA_ABSW; e.value = uint32_t(int32_t(int16_t(r.w()))); break;
        case 1: e.mode = EA_ABSL; e.value = r.l(); break;
        case 2: {
            e.mode = EA_PCDISP;
            uint32_t base = r.cur;
            e.disp = int16_t(r.w());
            e.value = base + uint32_t(e.disp);
            break;
        }
        case 3: {
            e.mode = EA_PCINDEX;
            uint32_t base = r.cur;
            uint16_t ext = r.w();
            e.xreg = uint8_t((ext >> 12) & 15);
            e.xlong = (ext >> 11) & 1;
            e.disp = int8_t(ext & 0xFF);
            e.value = base + uint32_t(e.disp);
            if (ext & 0x0700) return false;
            break;
        }
        case 4:
            e.mode = EA_IMM;
            if (size == 1) e.value = r.w() & 0xFF;
            else if (size == 2) e.value = r.w();
            else e.value = r.l();
            break;
        default: return false;
        }
        break;
    }
    return (allowed >> e.mode) & 1;
}

inline int size_from2(int s) { return s == 0 ? 1 : s == 1 ? 2 : s == 2 ? 4 : 0; }

int lea_time(EaMode m) {
    switch (m) {
    case EA_IND: return 4;
    case EA_DISP: case EA_ABSW: case EA_PCDISP: return 8;
    case EA_INDEX: case EA_PCINDEX: return 12;
    case EA_ABSL: return 12;
    default: return 4;
    }
}
int jmp_time(EaMode m) {
    switch (m) {
    case EA_IND: return 8;
    case EA_DISP: case EA_ABSW: case EA_PCDISP: return 10;
    case EA_INDEX: case EA_PCINDEX: return 14;
    case EA_ABSL: return 12;
    default: return 8;
    }
}

Ea dreg(int r) { Ea e; e.mode = EA_DREG; e.reg = uint8_t(r); return e; }
Ea areg(int r) { Ea e; e.mode = EA_AREG; e.reg = uint8_t(r); return e; }
Ea imm(uint32_t v) { Ea e; e.mode = EA_IMM; e.value = v; return e; }

bool decode_impl(Rd& r, Insn& o) {
    const uint16_t op = o.opcode;
    const int hi = op >> 12;
    const int rx = (op >> 9) & 7;   // bits 11-9
    const int ry = op & 7;          // bits 2-0
    const int mode = (op >> 3) & 7; // bits 5-3
    const int sz2 = (op >> 6) & 3;  // bits 7-6

    switch (hi) {
    case 0x0: {
        // CCR/SR immediate forms
        switch (op) {
        case 0x003C: o.op = Op::ORI_CCR; o.imm = r.w() & 0xFF; o.cycles = 20; o.flags |= IF_END_BLOCK; return true;
        case 0x007C: o.op = Op::ORI_SR; o.imm = r.w(); o.cycles = 20; o.flags |= IF_END_BLOCK | IF_PRIV; return true;
        case 0x023C: o.op = Op::ANDI_CCR; o.imm = r.w() & 0xFF; o.cycles = 20; o.flags |= IF_END_BLOCK; return true;
        case 0x027C: o.op = Op::ANDI_SR; o.imm = r.w(); o.cycles = 20; o.flags |= IF_END_BLOCK | IF_PRIV; return true;
        case 0x0A3C: o.op = Op::EORI_CCR; o.imm = r.w() & 0xFF; o.cycles = 20; o.flags |= IF_END_BLOCK; return true;
        case 0x0A7C: o.op = Op::EORI_SR; o.imm = r.w(); o.cycles = 20; o.flags |= IF_END_BLOCK | IF_PRIV; return true;
        default: break;
        }
        if (op & 0x0100) {
            if (mode == 1) {  // MOVEP
                int opm = sz2;
                o.size = (opm & 1) ? 4 : 2;
                o.op = (opm & 2) ? Op::MOVEP_RM : Op::MOVEP_MR;
                o.reg = uint8_t(rx);
                o.dst = Ea{}; o.dst.mode = EA_DISP; o.dst.reg = uint8_t(ry); o.dst.disp = int16_t(r.w());
                o.cycles = o.size == 4 ? 24 : 16;
                return true;
            }
            // dynamic bit ops
            static const Op bops[4] = {Op::BTST, Op::BCHG, Op::BCLR, Op::BSET};
            o.op = bops[sz2];
            o.src = dreg(rx);
            uint32_t allowed = (o.op == Op::BTST) ? DATA : DATA_ALT;
            if (!dec_ea(r, mode, ry, 1, o.dst, allowed)) return false;
            o.size = o.dst.mode == EA_DREG ? 4 : 1;
            if (o.dst.mode == EA_DREG)
                o.cycles = o.op == Op::BTST ? 6 : o.op == Op::BCLR ? 10 : 8;
            else
                o.cycles = (o.op == Op::BTST ? 4 : 8) + ea_time(o.dst.mode, false);
            return true;
        }
        if (rx == 4) {  // static bit ops
            static const Op bops[4] = {Op::BTST, Op::BCHG, Op::BCLR, Op::BSET};
            o.op = bops[sz2];
            uint16_t bit = r.w();
            if (bit & 0xFF00) return false;
            o.src = imm(bit & 0xFF);
            uint32_t allowed = (o.op == Op::BTST) ? (DATA & ~M_IMM) : DATA_ALT;
            if (!dec_ea(r, mode, ry, 1, o.dst, allowed)) return false;
            o.size = o.dst.mode == EA_DREG ? 4 : 1;
            if (o.dst.mode == EA_DREG)
                o.cycles = o.op == Op::BTST ? 10 : o.op == Op::BCLR ? 14 : 12;
            else
                o.cycles = (o.op == Op::BTST ? 8 : 12) + ea_time(o.dst.mode, false);
            return true;
        }
        static const Op iops[8] = {Op::ORI, Op::ANDI, Op::SUBI, Op::ADDI, Op::INVALID, Op::EORI, Op::CMPI, Op::INVALID};
        o.op = iops[rx];
        if (o.op == Op::INVALID) return false;
        o.size = uint8_t(size_from2(sz2));
        if (!o.size) return false;
        o.src = imm(o.size == 4 ? r.l() : o.size == 2 ? r.w() : (r.w() & 0xFF));
        uint32_t allowed = DATA_ALT;
        if (!dec_ea(r, mode, ry, o.size, o.dst, allowed)) return false;
        bool lng = o.size == 4;
        if (o.op == Op::CMPI)
            o.cycles = o.dst.mode == EA_DREG ? (lng ? 14 : 8) : (lng ? 12 : 8) + ea_time(o.dst.mode, lng);
        else
            o.cycles = o.dst.mode == EA_DREG ? (lng ? 16 : 8) : (lng ? 20 : 12) + ea_time(o.dst.mode, lng);
        return true;
    }
    case 0x1: case 0x2: case 0x3: {
        o.size = hi == 1 ? 1 : hi == 3 ? 2 : 4;
        int dmode = (op >> 6) & 7;
        uint32_t sallowed = o.size == 1 ? (ALL & ~M_AREG) : ALL;
        if (!dec_ea(r, mode, ry, o.size, o.src, sallowed)) return false;
        bool lng = o.size == 4;
        if (dmode == 1) {
            if (o.size == 1) return false;
            o.op = Op::MOVEA;
            o.dst = areg(rx);
            o.cycles = 4 + ea_time(o.src.mode, lng);
            return true;
        }
        o.op = Op::MOVE;
        if (!dec_ea(r, dmode, rx, o.size, o.dst, DATA_ALT)) return false;
        int dt = o.dst.mode == EA_PREDEC ? ea_time(EA_IND, lng) : ea_time(o.dst.mode, lng);
        o.cycles = 4 + ea_time(o.src.mode, lng) + dt;
        return true;
    }
    case 0x4: {
        if ((op & 0xF1C0) == 0x41C0) {  // LEA
            o.op = Op::LEA;
            if (!dec_ea(r, mode, ry, 4, o.src, CONTROL)) return false;
            o.dst = areg(rx);
            o.size = 4;
            o.cycles = uint16_t(lea_time(o.src.mode));
            return true;
        }
        if ((op & 0xF1C0) == 0x4180) {  // CHK.W
            o.op = Op::CHK;
            o.size = 2;
            if (!dec_ea(r, mode, ry, 2, o.src, DATA)) return false;
            o.dst = dreg(rx);
            o.cycles = 10 + ea_time(o.src.mode, false);
            o.flags |= IF_END_BLOCK;
            return true;
        }
        switch (op & 0xFFC0) {
        case 0x40C0:
            o.op = Op::MOVE_FROM_SR; o.size = 2;
            if (!dec_ea(r, mode, ry, 2, o.dst, DATA_ALT)) return false;
            o.cycles = o.dst.mode == EA_DREG ? 6 : 8 + ea_time(o.dst.mode, false);
            return true;
        case 0x44C0:
            o.op = Op::MOVE_TO_CCR; o.size = 2;
            if (!dec_ea(r, mode, ry, 2, o.src, DATA)) return false;
            o.cycles = 12 + ea_time(o.src.mode, false);
            o.flags |= IF_END_BLOCK;
            return true;
        case 0x46C0:
            o.op = Op::MOVE_TO_SR; o.size = 2;
            if (!dec_ea(r, mode, ry, 2, o.src, DATA)) return false;
            o.cycles = 12 + ea_time(o.src.mode, false);
            o.flags |= IF_END_BLOCK | IF_PRIV;
            return true;
        case 0x4800:
            o.op = Op::NBCD; o.size = 1;
            if (!dec_ea(r, mode, ry, 1, o.dst, DATA_ALT)) return false;
            o.cycles = o.dst.mode == EA_DREG ? 6 : 8 + ea_time(o.dst.mode, false);
            return true;
        case 0x4840:
            if (mode == 0) { o.op = Op::SWAP; o.size = 4; o.dst = dreg(ry); o.cycles = 4; return true; }
            o.op = Op::PEA; o.size = 4;
            if (!dec_ea(r, mode, ry, 4, o.src, CONTROL)) return false;
            o.cycles = uint16_t(lea_time(o.src.mode) + 8);
            return true;
        case 0x4880: case 0x48C0:
            if (mode == 0) {
                o.op = Op::EXT; o.size = (op & 0x40) ? 4 : 2; o.dst = dreg(ry); o.cycles = 4;
                return true;
            } else {
                o.op = Op::MOVEM_RM; o.size = (op & 0x40) ? 4 : 2;
                o.imm = r.w();
                if (!dec_ea(r, mode, ry, o.size, o.dst, CTRL_ALT | M_PREDEC)) return false;
                int n = 0; for (int i = 0; i < 16; ++i) n += (o.imm >> i) & 1;
                int base = 8;
                switch (o.dst.mode) {
                case EA_DISP: case EA_ABSW: base = 12; break;
                case EA_INDEX: base = 14; break;
                case EA_ABSL: base = 16; break;
                default: break;
                }
                o.cycles = uint16_t(base + n * (o.size == 4 ? 8 : 4));
                return true;
            }
        case 0x4AC0:
            if (op == 0x4AFC) { o.op = Op::ILLEGAL; o.flags |= IF_BRANCH | IF_NO_FALLTHROUGH | IF_END_BLOCK; o.cycles = 4; return true; }
            o.op = Op::TAS; o.size = 1;
            if (!dec_ea(r, mode, ry, 1, o.dst, DATA_ALT)) return false;
            o.cycles = o.dst.mode == EA_DREG ? 4 : 14 + ea_time(o.dst.mode, false);
            return true;
        case 0x4C80: case 0x4CC0: {
            o.op = Op::MOVEM_MR; o.size = (op & 0x40) ? 4 : 2;
            o.imm = r.w();
            if (!dec_ea(r, mode, ry, o.size, o.src, CONTROL | M_POSTINC)) return false;
            int n = 0; for (int i = 0; i < 16; ++i) n += (o.imm >> i) & 1;
            int base = 12;
            switch (o.src.mode) {
            case EA_DISP: case EA_ABSW: case EA_PCDISP: base = 16; break;
            case EA_INDEX: case EA_PCINDEX: base = 18; break;
            case EA_ABSL: base = 20; break;
            default: break;
            }
            o.cycles = uint16_t(base + n * (o.size == 4 ? 8 : 4));
            return true;
        }
        case 0x4E80: case 0x4EC0: {
            bool jsr = (op & 0x40) == 0;
            o.op = jsr ? Op::JSR : Op::JMP;
            if (!dec_ea(r, mode, ry, 4, o.src, CONTROL)) return false;
            o.flags |= IF_BRANCH;
            if (jsr) o.flags |= IF_CALL; else o.flags |= IF_NO_FALLTHROUGH;
            if (o.src.mode == EA_ABSW || o.src.mode == EA_ABSL || o.src.mode == EA_PCDISP) o.target = o.src.value;
            else o.flags |= IF_INDIRECT;
            o.cycles = uint16_t(jmp_time(o.src.mode) + (jsr ? 8 : 0));
            return true;
        }
        default: break;
        }
        if ((op & 0xFFF0) == 0x4E40) { o.op = Op::TRAP; o.imm = op & 15; o.cycles = 4; o.flags |= IF_END_BLOCK | IF_BRANCH | IF_NO_FALLTHROUGH | IF_INDIRECT; return true; }
        if ((op & 0xFFF8) == 0x4E50) { o.op = Op::LINK; o.reg = uint8_t(ry); o.imm = uint32_t(int32_t(int16_t(r.w()))); o.cycles = 16; return true; }
        if ((op & 0xFFF8) == 0x4E58) { o.op = Op::UNLK; o.reg = uint8_t(ry); o.cycles = 12; return true; }
        if ((op & 0xFFF8) == 0x4E60) { o.op = Op::MOVE_TO_USP; o.reg = uint8_t(ry); o.cycles = 4; o.flags |= IF_PRIV; return true; }
        if ((op & 0xFFF8) == 0x4E68) { o.op = Op::MOVE_FROM_USP; o.reg = uint8_t(ry); o.cycles = 4; o.flags |= IF_PRIV; return true; }
        switch (op) {
        case 0x4E70: o.op = Op::RESET; o.cycles = 132; o.flags |= IF_PRIV | IF_END_BLOCK; return true;
        case 0x4E71: o.op = Op::NOP; o.cycles = 4; return true;
        case 0x4E72: o.op = Op::STOP; o.imm = r.w(); o.cycles = 4; o.flags |= IF_PRIV | IF_END_BLOCK; return true;
        case 0x4E73: o.op = Op::RTE; o.cycles = 20; o.flags |= IF_PRIV | IF_BRANCH | IF_RETURN | IF_INDIRECT | IF_NO_FALLTHROUGH; return true;
        case 0x4E75: o.op = Op::RTS; o.cycles = 16; o.flags |= IF_BRANCH | IF_RETURN | IF_INDIRECT | IF_NO_FALLTHROUGH; return true;
        case 0x4E76: o.op = Op::TRAPV; o.cycles = 4; o.flags |= IF_END_BLOCK; return true;
        case 0x4E77: o.op = Op::RTR; o.cycles = 20; o.flags |= IF_BRANCH | IF_RETURN | IF_INDIRECT | IF_NO_FALLTHROUGH; return true;
        default: break;
        }
        // NEGX / CLR / NEG / NOT / TST
        {
            Op kind = Op::INVALID;
            switch (op & 0xFF00) {
            case 0x4000: kind = Op::NEGX; break;
            case 0x4200: kind = Op::CLR; break;
            case 0x4400: kind = Op::NEG; break;
            case 0x4600: kind = Op::NOT; break;
            case 0x4A00: kind = Op::TST; break;
            default: return false;
            }
            o.op = kind;
            o.size = uint8_t(size_from2(sz2));
            if (!o.size) return false;
            uint32_t allowed = kind == Op::TST ? (DATA_ALT) : DATA_ALT;
            if (!dec_ea(r, mode, ry, o.size, o.dst, allowed)) return false;
            bool lng = o.size == 4;
            if (kind == Op::TST) o.cycles = 4 + ea_time(o.dst.mode, lng);
            else if (o.dst.mode == EA_DREG) o.cycles = lng ? 6 : 4;
            else o.cycles = (lng ? 12 : 8) + ea_time(o.dst.mode, lng);
            return true;
        }
    }
    case 0x5: {
        if (sz2 == 3) {
            o.cc = uint8_t((op >> 8) & 15);
            if (mode == 1) {
                o.op = Op::DBCC;
                o.dst = dreg(ry);
                uint32_t base = r.cur;
                o.target = base + uint32_t(int32_t(int16_t(r.w())));
                o.flags |= IF_BRANCH | IF_COND;
                o.cycles = 10;
                return true;
            }
            o.op = Op::SCC; o.size = 1;
            if (!dec_ea(r, mode, ry, 1, o.dst, DATA_ALT)) return false;
            o.cycles = o.dst.mode == EA_DREG ? 4 : 8 + ea_time(o.dst.mode, false);
            return true;
        }
        o.op = (op & 0x100) ? Op::SUBQ : Op::ADDQ;
        o.size = uint8_t(size_from2(sz2));
        o.src = imm(rx == 0 ? 8 : uint32_t(rx));
        if (!dec_ea(r, mode, ry, o.size, o.dst, ALTERABLE)) return false;
        if (o.dst.mode == EA_AREG && o.size == 1) return false;
        bool lng = o.size == 4;
        if (o.dst.mode == EA_DREG) o.cycles = lng ? 8 : 4;
        else if (o.dst.mode == EA_AREG) o.cycles = 8;
        else o.cycles = (lng ? 12 : 8) + ea_time(o.dst.mode, lng);
        return true;
    }
    case 0x6: {
        int cc = (op >> 8) & 15;
        uint32_t base = r.cur;
        int32_t disp = int8_t(op & 0xFF);
        if ((op & 0xFF) == 0) disp = int16_t(r.w());
        else if ((op & 0xFF) == 0xFF) return false;  // 68020 long branch
        o.target = base + uint32_t(disp);
        o.flags |= IF_BRANCH;
        if (cc == 0) { o.op = Op::BRA; o.flags |= IF_NO_FALLTHROUGH; o.cycles = 10; }
        else if (cc == 1) { o.op = Op::BSR; o.flags |= IF_CALL; o.cycles = 18; }
        else { o.op = Op::BCC; o.cc = uint8_t(cc); o.flags |= IF_COND; o.cycles = 10; }
        o.size = (op & 0xFF) == 0 ? 2 : 1;
        return true;
    }
    case 0x7:
        if (op & 0x100) return false;
        o.op = Op::MOVEQ; o.size = 4;
        o.src = imm(uint32_t(int32_t(int8_t(op & 0xFF))));
        o.dst = dreg(rx);
        o.cycles = 4;
        return true;
    case 0x8: case 0xC: {
        bool isand = hi == 0xC;
        if (sz2 == 3) {  // DIVU/DIVS or MULU/MULS
            bool sgn = (op & 0x100) != 0;
            if (isand) o.op = sgn ? Op::MULS : Op::MULU;
            else o.op = sgn ? Op::DIVS : Op::DIVU;
            o.size = 2;
            if (!dec_ea(r, mode, ry, 2, o.src, DATA)) return false;
            o.dst = dreg(rx);
            if (isand) o.cycles = uint16_t(ea_time(o.src.mode, false));  // + dynamic
            else { o.cycles = uint16_t((sgn ? 158 : 140) + ea_time(o.src.mode, false)); o.flags |= IF_END_BLOCK; }
            return true;
        }
        if ((op & 0x1F0) == 0x100) {  // ABCD/SBCD
            o.op = isand ? Op::ABCD : Op::SBCD;
            o.size = 1;
            if (op & 8) {
                o.src = Ea{}; o.src.mode = EA_PREDEC; o.src.reg = uint8_t(ry);
                o.dst = Ea{}; o.dst.mode = EA_PREDEC; o.dst.reg = uint8_t(rx);
                o.cycles = 18;
            } else {
                o.src = dreg(ry); o.dst = dreg(rx); o.cycles = 6;
            }
            return true;
        }
        if (isand && (op & 0x1F8) == 0x140) { o.op = Op::EXG; o.src = dreg(rx); o.dst = dreg(ry); o.cycles = 6; o.size = 4; return true; }
        if (isand && (op & 0x1F8) == 0x148) { o.op = Op::EXG; o.src = areg(rx); o.dst = areg(ry); o.cycles = 6; o.size = 4; return true; }
        if (isand && (op & 0x1F8) == 0x188) { o.op = Op::EXG; o.src = dreg(rx); o.dst = areg(ry); o.cycles = 6; o.size = 4; return true; }
        o.op = isand ? Op::AND : Op::OR;
        o.size = uint8_t(size_from2(sz2));
        bool lng = o.size == 4;
        if (op & 0x100) {  // Dn,<ea>
            o.src = dreg(rx);
            if (!dec_ea(r, mode, ry, o.size, o.dst, MEM_ALT)) return false;
            o.cycles = (lng ? 12 : 8) + ea_time(o.dst.mode, lng);
        } else {
            if (!dec_ea(r, mode, ry, o.size, o.src, DATA)) return false;
            o.dst = dreg(rx);
            o.cycles = 4 + ea_time(o.src.mode, lng) + (lng ? ((o.src.mode == EA_DREG || o.src.mode == EA_IMM) ? 4 : 2) : 0);
        }
        return true;
    }
    case 0x9: case 0xD: {
        bool isadd = hi == 0xD;
        if (sz2 == 3) {  // ADDA/SUBA
            o.op = isadd ? Op::ADDA : Op::SUBA;
            o.size = (op & 0x100) ? 4 : 2;
            if (!dec_ea(r, mode, ry, o.size, o.src, ALL)) return false;
            o.dst = areg(rx);
            bool lng = o.size == 4;
            o.cycles = uint16_t((lng ? ((o.src.mode == EA_DREG || o.src.mode == EA_AREG || o.src.mode == EA_IMM) ? 8 : 6) : 8) + ea_time(o.src.mode, lng));
            return true;
        }
        o.size = uint8_t(size_from2(sz2));
        bool lng = o.size == 4;
        if ((op & 0x130) == 0x100) {  // ADDX/SUBX
            o.op = isadd ? Op::ADDX : Op::SUBX;
            if (op & 8) {
                o.src = Ea{}; o.src.mode = EA_PREDEC; o.src.reg = uint8_t(ry);
                o.dst = Ea{}; o.dst.mode = EA_PREDEC; o.dst.reg = uint8_t(rx);
                o.cycles = lng ? 30 : 18;
            } else {
                o.src = dreg(ry); o.dst = dreg(rx); o.cycles = lng ? 8 : 4;
            }
            return true;
        }
        o.op = isadd ? Op::ADD : Op::SUB;
        if (op & 0x100) {
            o.src = dreg(rx);
            if (!dec_ea(r, mode, ry, o.size, o.dst, MEM_ALT)) return false;
            o.cycles = (lng ? 12 : 8) + ea_time(o.dst.mode, lng);
        } else {
            uint32_t allowed = o.size == 1 ? DATA : ALL;
            if (!dec_ea(r, mode, ry, o.size, o.src, allowed)) return false;
            o.dst = dreg(rx);
            o.cycles = 4 + ea_time(o.src.mode, lng) + (lng ? ((o.src.mode == EA_DREG || o.src.mode == EA_AREG || o.src.mode == EA_IMM) ? 4 : 2) : 0);
        }
        return true;
    }
    case 0xB: {
        if (sz2 == 3) {
            o.op = Op::CMPA;
            o.size = (op & 0x100) ? 4 : 2;
            if (!dec_ea(r, mode, ry, o.size, o.src, ALL)) return false;
            o.dst = areg(rx);
            o.cycles = uint16_t(6 + ea_time(o.src.mode, o.size == 4));
            return true;
        }
        o.size = uint8_t(size_from2(sz2));
        bool lng = o.size == 4;
        if (op & 0x100) {
            if (mode == 1) {
                o.op = Op::CMPM;
                o.src = Ea{}; o.src.mode = EA_POSTINC; o.src.reg = uint8_t(ry);
                o.dst = Ea{}; o.dst.mode = EA_POSTINC; o.dst.reg = uint8_t(rx);
                o.cycles = lng ? 20 : 12;
                return true;
            }
            o.op = Op::EOR;
            o.src = dreg(rx);
            if (!dec_ea(r, mode, ry, o.size, o.dst, DATA_ALT)) return false;
            o.cycles = o.dst.mode == EA_DREG ? (lng ? 8 : 4) : (lng ? 12 : 8) + ea_time(o.dst.mode, lng);
            return true;
        }
        o.op = Op::CMP;
        uint32_t allowed = o.size == 1 ? DATA : ALL;
        if (!dec_ea(r, mode, ry, o.size, o.src, allowed)) return false;
        o.dst = dreg(rx);
        o.cycles = uint16_t((lng ? 6 : 4) + ea_time(o.src.mode, lng));
        return true;
    }
    case 0xE: {
        static const Op left[4] = {Op::ASL, Op::LSL, Op::ROXL, Op::ROL};
        static const Op right[4] = {Op::ASR, Op::LSR, Op::ROXR, Op::ROR};
        bool dirl = (op & 0x100) != 0;
        if (sz2 == 3) {  // memory shift by 1
            int type = (op >> 9) & 7;
            if (type > 3) return false;
            o.op = dirl ? left[type] : right[type];
            o.size = 2;
            o.src = imm(1);
            if (!dec_ea(r, mode, ry, 2, o.dst, MEM_ALT)) return false;
            o.cycles = uint16_t(8 + ea_time(o.dst.mode, false));
            return true;
        }
        int type = (op >> 3) & 3;
        o.op = dirl ? left[type] : right[type];
        o.size = uint8_t(size_from2(sz2));
        if (op & 0x20) o.src = dreg(rx);
        else o.src = imm(rx == 0 ? 8 : uint32_t(rx));
        o.dst = dreg(ry);
        o.cycles = o.size == 4 ? 8 : 6;  // + 2 * count at runtime
        return true;
    }
    case 0xA:
        o.op = Op::LINE_A; o.cycles = 4; o.flags |= IF_BRANCH | IF_NO_FALLTHROUGH | IF_END_BLOCK | IF_INDIRECT;
        return true;
    case 0xF:
        o.op = Op::LINE_F; o.cycles = 4; o.flags |= IF_BRANCH | IF_NO_FALLTHROUGH | IF_END_BLOCK | IF_INDIRECT;
        return true;
    }
    return false;
}

} // namespace

bool decode(uint32_t pc, Fetch16 fetch, void* user, Insn& out) {
    out = Insn{};
    out.pc = pc;
    Rd r{fetch, user, pc};
    out.opcode = r.w();
    bool ok = decode_impl(r, out);
    out.len = uint8_t(r.cur - pc);
    if (!ok) {
        out.op = Op::INVALID;
        out.flags = IF_BRANCH | IF_NO_FALLTHROUGH | IF_END_BLOCK | IF_INDIRECT;
        out.len = 2;
        out.cycles = 4;
    }
    return ok && out.op != Op::ILLEGAL && out.op != Op::LINE_A && out.op != Op::LINE_F;
}

} // namespace m68k
