#include "z80.h"

namespace z80 {

namespace {

enum : uint8_t { FC = 0x01, FN = 0x02, FP = 0x04, FX = 0x08, FH = 0x10, FY = 0x20, FZ = 0x40, FS = 0x80 };

const uint8_t kCycles[256] = {
    4, 10, 7, 6, 4, 4, 7, 4, 4, 11, 7, 6, 4, 4, 7, 4,
    8, 10, 7, 6, 4, 4, 7, 4, 12, 11, 7, 6, 4, 4, 7, 4,
    7, 10, 16, 6, 4, 4, 7, 4, 7, 11, 16, 6, 4, 4, 7, 4,
    7, 10, 13, 6, 11, 11, 10, 4, 7, 11, 13, 6, 4, 4, 7, 4,
    4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
    4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
    4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
    7, 7, 7, 7, 7, 7, 4, 7, 4, 4, 4, 4, 4, 4, 7, 4,
    4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
    4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
    4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
    4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
    5, 10, 10, 10, 10, 11, 7, 11, 5, 10, 10, 0, 10, 17, 7, 11,
    5, 10, 10, 11, 10, 11, 7, 11, 5, 4, 10, 11, 10, 0, 7, 11,
    5, 10, 10, 19, 10, 11, 7, 11, 5, 4, 10, 4, 10, 0, 7, 11,
    5, 10, 10, 4, 10, 11, 7, 11, 5, 6, 10, 4, 10, 0, 7, 11,
};

inline bool parity(uint8_t v) {
    v ^= v >> 4; v ^= v >> 2; v ^= v >> 1;
    return !(v & 1);
}
inline uint8_t szp(uint8_t v) { return uint8_t((v & (FS | FY | FX)) | (v ? 0 : FZ) | (parity(v) ? FP : 0)); }

struct Cpu {
    State* s;
    int extra;      // additional cycles (taken branches, repeats, prefixes)
    int ixmode;     // 0 = HL, 1 = IX, 2 = IY

    uint8_t rd(uint16_t a) { return s->bus.read(s->bus.user, a); }
    void wr(uint16_t a, uint8_t v) { s->bus.write(s->bus.user, a, v); }
    uint16_t rd16(uint16_t a) { return uint16_t(rd(a) | (rd(uint16_t(a + 1)) << 8)); }
    void wr16(uint16_t a, uint16_t v) { wr(a, uint8_t(v)); wr(uint16_t(a + 1), uint8_t(v >> 8)); }
    uint8_t imm8() { return rd(s->pc++); }
    uint16_t imm16() { uint16_t v = rd16(s->pc); s->pc += 2; return v; }
    void push(uint16_t v) { s->sp -= 2; wr16(s->sp, v); }
    uint16_t pop() { uint16_t v = rd16(s->sp); s->sp += 2; return v; }
    void inc_r() { s->r = uint8_t((s->r & 0x80) | ((s->r + 1) & 0x7F)); }

    uint16_t bc() const { return uint16_t((s->b << 8) | s->c); }
    uint16_t de() const { return uint16_t((s->d << 8) | s->e); }
    uint16_t hl() const { return uint16_t((s->h << 8) | s->l); }
    void set_bc(uint16_t v) { s->b = uint8_t(v >> 8); s->c = uint8_t(v); }
    void set_de(uint16_t v) { s->d = uint8_t(v >> 8); s->e = uint8_t(v); }
    void set_hl(uint16_t v) { s->h = uint8_t(v >> 8); s->l = uint8_t(v); }
    uint16_t af() const { return uint16_t((s->a << 8) | s->f); }
    void set_af(uint16_t v) { s->a = uint8_t(v >> 8); s->f = uint8_t(v); }

    // HL / IX / IY as selected by the prefix
    uint16_t xy() const { return ixmode == 0 ? hl() : ixmode == 1 ? s->ix : s->iy; }
    void set_xy(uint16_t v) { if (ixmode == 0) set_hl(v); else if (ixmode == 1) s->ix = v; else s->iy = v; }

    uint16_t rp(int p) {  // BC DE HL SP
        switch (p) { case 0: return bc(); case 1: return de(); case 2: return xy(); default: return s->sp; }
    }
    void set_rp(int p, uint16_t v) {
        switch (p) { case 0: set_bc(v); break; case 1: set_de(v); break; case 2: set_xy(v); break; default: s->sp = v; break; }
    }
    uint16_t rp2(int p) { return p == 3 ? af() : rp(p); }
    void set_rp2(int p, uint16_t v) { if (p == 3) set_af(v); else set_rp(p, v); }

    // Memory operand address for (HL) or (IX+d); fetches d when indexed.
    uint16_t mem_addr() {
        if (ixmode == 0) return hl();
        int8_t d = int8_t(imm8());
        extra += 8;
        return uint16_t((ixmode == 1 ? s->ix : s->iy) + d);
    }
    // 8-bit register read/write; r = 6 is memory. With an index prefix H/L map
    // to IXH/IXL except when the other operand is memory (handled by caller).
    uint8_t get_r(int r, uint16_t maddr, bool use_ix_hl = true) {
        switch (r) {
        case 0: return s->b; case 1: return s->c; case 2: return s->d; case 3: return s->e;
        case 4: return (use_ix_hl && ixmode) ? uint8_t((ixmode == 1 ? s->ix : s->iy) >> 8) : s->h;
        case 5: return (use_ix_hl && ixmode) ? uint8_t(ixmode == 1 ? s->ix : s->iy) : s->l;
        case 6: return rd(maddr);
        default: return s->a;
        }
    }
    void set_r(int r, uint8_t v, uint16_t maddr, bool use_ix_hl = true) {
        switch (r) {
        case 0: s->b = v; break; case 1: s->c = v; break; case 2: s->d = v; break; case 3: s->e = v; break;
        case 4:
            if (use_ix_hl && ixmode == 1) s->ix = uint16_t((s->ix & 0xFF) | (v << 8));
            else if (use_ix_hl && ixmode == 2) s->iy = uint16_t((s->iy & 0xFF) | (v << 8));
            else s->h = v;
            break;
        case 5:
            if (use_ix_hl && ixmode == 1) s->ix = uint16_t((s->ix & 0xFF00) | v);
            else if (use_ix_hl && ixmode == 2) s->iy = uint16_t((s->iy & 0xFF00) | v);
            else s->l = v;
            break;
        case 6: wr(maddr, v); break;
        default: s->a = v; break;
        }
    }

    bool cond(int cc) const {
        switch (cc) {
        case 0: return !(s->f & FZ); case 1: return s->f & FZ;
        case 2: return !(s->f & FC); case 3: return s->f & FC;
        case 4: return !(s->f & FP); case 5: return s->f & FP;
        case 6: return !(s->f & FS); default: return s->f & FS;
        }
    }

    // ---- ALU ----
    void add8(uint8_t v, int carry) {
        unsigned r = unsigned(s->a) + v + unsigned(carry);
        uint8_t res = uint8_t(r);
        s->f = uint8_t((res & (FS | FY | FX)) | (res ? 0 : FZ) | ((s->a ^ v ^ res) & FH) |
                       ((((s->a ^ ~v) & (s->a ^ res)) & 0x80) ? FP : 0) | ((r >> 8) & FC));
        s->a = res;
    }
    uint8_t sub8(uint8_t v, int carry, bool store = true) {
        unsigned r = unsigned(s->a) - v - unsigned(carry);
        uint8_t res = uint8_t(r);
        s->f = uint8_t((res & (FS | FY | FX)) | (res ? 0 : FZ) | ((s->a ^ v ^ res) & FH) |
                       ((((s->a ^ v) & (s->a ^ res)) & 0x80) ? FP : 0) | FN | ((r >> 8) & FC));
        if (store) s->a = res;
        return res;
    }
    void alu(int op, uint8_t v) {
        switch (op) {
        case 0: add8(v, 0); break;
        case 1: add8(v, s->f & FC); break;
        case 2: sub8(v, 0); break;
        case 3: sub8(v, s->f & FC); break;
        case 4: s->a &= v; s->f = uint8_t(szp(s->a) | FH); break;
        case 5: s->a ^= v; s->f = szp(s->a); break;
        case 6: s->a |= v; s->f = szp(s->a); break;
        default:
            sub8(v, 0, false);
            s->f = uint8_t((s->f & ~(FY | FX)) | (v & (FY | FX)));
            break;
        }
    }
    uint8_t inc8(uint8_t v) {
        uint8_t r = uint8_t(v + 1);
        s->f = uint8_t((s->f & FC) | (r & (FS | FY | FX)) | (r ? 0 : FZ) | ((r & 0x0F) == 0 ? FH : 0) | (r == 0x80 ? FP : 0));
        return r;
    }
    uint8_t dec8(uint8_t v) {
        uint8_t r = uint8_t(v - 1);
        s->f = uint8_t((s->f & FC) | (r & (FS | FY | FX)) | (r ? 0 : FZ) | ((r & 0x0F) == 0x0F ? FH : 0) | (r == 0x7F ? FP : 0) | FN);
        return r;
    }
    uint16_t add16(uint16_t a, uint16_t b) {
        uint32_t r = uint32_t(a) + b;
        s->f = uint8_t((s->f & (FS | FZ | FP)) | ((r >> 8) & (FY | FX)) | (((a ^ b ^ r) >> 8) & FH) | ((r >> 16) & FC));
        return uint16_t(r);
    }
    uint16_t adc16(uint16_t a, uint16_t b) {
        uint32_t r = uint32_t(a) + b + (s->f & FC);
        uint16_t res = uint16_t(r);
        s->f = uint8_t(((res >> 8) & (FS | FY | FX)) | (res ? 0 : FZ) | (((a ^ b ^ r) >> 8) & FH) |
                       ((((a ^ ~b) & (a ^ res)) & 0x8000) ? FP : 0) | ((r >> 16) & FC));
        return res;
    }
    uint16_t sbc16(uint16_t a, uint16_t b) {
        uint32_t r = uint32_t(a) - b - (s->f & FC);
        uint16_t res = uint16_t(r);
        s->f = uint8_t(((res >> 8) & (FS | FY | FX)) | (res ? 0 : FZ) | (((a ^ b ^ r) >> 8) & FH) |
                       ((((a ^ b) & (a ^ res)) & 0x8000) ? FP : 0) | FN | ((r >> 16) & FC));
        return res;
    }
    uint8_t rot(int op, uint8_t v) {
        uint8_t c = 0, r = 0;
        switch (op) {
        case 0: c = v >> 7; r = uint8_t((v << 1) | c); break;             // RLC
        case 1: c = v & 1; r = uint8_t((v >> 1) | (c << 7)); break;        // RRC
        case 2: c = v >> 7; r = uint8_t((v << 1) | (s->f & FC)); break;    // RL
        case 3: c = v & 1; r = uint8_t((v >> 1) | ((s->f & FC) << 7)); break;  // RR
        case 4: c = v >> 7; r = uint8_t(v << 1); break;                    // SLA
        case 5: c = v & 1; r = uint8_t((v >> 1) | (v & 0x80)); break;      // SRA
        case 6: c = v >> 7; r = uint8_t((v << 1) | 1); break;              // SLL (undocumented)
        default: c = v & 1; r = uint8_t(v >> 1); break;                    // SRL
        }
        s->f = uint8_t(szp(r) | c);
        return r;
    }
    void daa() {
        uint8_t a = s->a, corr = 0;
        uint8_t c = s->f & FC;
        if ((s->f & FH) || (a & 0x0F) > 9) corr |= 0x06;
        if (c || a > 0x99) { corr |= 0x60; c = FC; }
        uint8_t r = (s->f & FN) ? uint8_t(a - corr) : uint8_t(a + corr);
        uint8_t h = uint8_t((a ^ r) & FH);
        s->a = r;
        s->f = uint8_t(szp(r) | h | (s->f & FN) | c);
    }

    // ---- prefixed groups ----
    void exec_cb() {
        // With DD/FD the displacement precedes the opcode.
        uint16_t addr = 0;
        uint8_t op;
        if (ixmode) {
            int8_t d = int8_t(imm8());
            addr = uint16_t((ixmode == 1 ? s->ix : s->iy) + d);
            op = imm8();
        } else {
            op = imm8();
            inc_r();
            addr = hl();
        }
        int x = op >> 6, y = (op >> 3) & 7, z = op & 7;
        bool mem = ixmode || z == 6;
        uint8_t v = mem ? rd(addr) : get_r(z, 0, false);
        int cyc = ixmode ? 23 : (z == 6 ? 15 : 8);
        if (x == 1) {  // BIT
            uint8_t r = v & (1u << y);
            s->f = uint8_t((s->f & FC) | FH | (r ? 0 : (FZ | FP)) | (r & FS) | (mem ? (uint8_t(addr >> 8) & (FY | FX)) : (v & (FY | FX))));
            cyc = ixmode ? 20 : (z == 6 ? 12 : 8);
            extra += cyc - 4 - (ixmode ? 4 : 0);  // the DD/FD prefix was already counted
            return;
        }
        uint8_t r;
        if (x == 0) r = rot(y, v);
        else if (x == 2) r = uint8_t(v & ~(1u << y));
        else r = uint8_t(v | (1u << y));
        if (mem) {
            wr(addr, r);
            if (ixmode && z != 6) set_r(z, r, 0, false);  // undocumented copy to register
        } else {
            set_r(z, r, 0, false);
        }
        extra += cyc - 4 - (ixmode ? 4 : 0);
    }

    void exec_ed() {
        uint8_t op = imm8();
        inc_r();
        int x = op >> 6, y = (op >> 3) & 7, z = op & 7, p = y >> 1, q = y & 1;
        int cyc = 8;
        ixmode = 0;  // ED ignores DD/FD
        if (x == 1) {
            switch (z) {
            case 0: {  // IN r,(C)
                uint8_t v = s->bus.in(s->bus.user, bc());
                if (y != 6) set_r(y, v, 0, false);
                s->f = uint8_t((s->f & FC) | szp(v));
                cyc = 12;
                break;
            }
            case 1:  // OUT (C),r
                s->bus.out(s->bus.user, bc(), y == 6 ? 0 : get_r(y, 0, false));
                cyc = 12;
                break;
            case 2:
                if (q == 0) set_hl(sbc16(hl(), rp(p)));
                else set_hl(adc16(hl(), rp(p)));
                cyc = 15;
                break;
            case 3: {
                uint16_t a = imm16();
                if (q == 0) wr16(a, rp(p)); else set_rp(p, rd16(a));
                cyc = 20;
                break;
            }
            case 4: {  // NEG
                uint8_t v = s->a;
                s->a = 0;
                sub8(v, 0);
                break;
            }
            case 5:  // RETN / RETI
                s->pc = pop();
                s->iff1 = s->iff2;
                cyc = 14;
                break;
            case 6: {
                static const uint8_t modes[8] = {0, 0, 1, 2, 0, 0, 1, 2};
                s->im = modes[y];
                break;
            }
            default:
                switch (y) {
                case 0: s->i = s->a; cyc = 9; break;
                case 1: s->r = s->a; cyc = 9; break;
                case 2: s->a = s->i; s->f = uint8_t((s->f & FC) | (s->a & (FS | FY | FX)) | (s->a ? 0 : FZ) | (s->iff2 ? FP : 0)); cyc = 9; break;
                case 3: s->a = s->r; s->f = uint8_t((s->f & FC) | (s->a & (FS | FY | FX)) | (s->a ? 0 : FZ) | (s->iff2 ? FP : 0)); cyc = 9; break;
                case 4: {  // RRD
                    uint8_t m = rd(hl());
                    wr(hl(), uint8_t((s->a << 4) | (m >> 4)));
                    s->a = uint8_t((s->a & 0xF0) | (m & 0x0F));
                    s->f = uint8_t((s->f & FC) | szp(s->a));
                    cyc = 18;
                    break;
                }
                case 5: {  // RLD
                    uint8_t m = rd(hl());
                    wr(hl(), uint8_t((m << 4) | (s->a & 0x0F)));
                    s->a = uint8_t((s->a & 0xF0) | (m >> 4));
                    s->f = uint8_t((s->f & FC) | szp(s->a));
                    cyc = 18;
                    break;
                }
                default: break;
                }
                break;
            }
        } else if (x == 2 && z <= 3 && y >= 4) {
            // Block instructions
            const bool dec = (y & 1) != 0, rep = y >= 6;
            const int step = dec ? -1 : 1;
            cyc = 16;
            switch (z) {
            case 0: {  // LDI/LDD/LDIR/LDDR
                uint8_t v = rd(hl());
                wr(de(), v);
                set_hl(uint16_t(hl() + step));
                set_de(uint16_t(de() + step));
                set_bc(uint16_t(bc() - 1));
                uint8_t n = uint8_t(v + s->a);
                s->f = uint8_t((s->f & (FS | FZ | FC)) | (bc() ? FP : 0) | (n & FX) | ((n << 4) & FY));
                if (rep && bc()) { s->pc -= 2; cyc = 21; }
                break;
            }
            case 1: {  // CPI/CPD/CPIR/CPDR
                uint8_t v = rd(hl());
                uint8_t r = uint8_t(s->a - v);
                uint8_t h = uint8_t((s->a ^ v ^ r) & FH);
                set_hl(uint16_t(hl() + step));
                set_bc(uint16_t(bc() - 1));
                uint8_t n = uint8_t(r - (h ? 1 : 0));
                s->f = uint8_t((s->f & FC) | (r & FS) | (r ? 0 : FZ) | h | (bc() ? FP : 0) | FN | (n & FX) | ((n << 4) & FY));
                if (rep && bc() && r) { s->pc -= 2; cyc = 21; }
                break;
            }
            case 2: {  // INI/IND/INIR/INDR
                uint8_t v = s->bus.in(s->bus.user, bc());
                wr(hl(), v);
                set_hl(uint16_t(hl() + step));
                s->b = uint8_t(s->b - 1);
                s->f = uint8_t((s->b ? 0 : FZ) | FN | (s->b & (FS | FY | FX)));
                if (rep && s->b) { s->pc -= 2; cyc = 21; }
                break;
            }
            default: {  // OUTI/OUTD/OTIR/OTDR
                uint8_t v = rd(hl());
                s->b = uint8_t(s->b - 1);
                s->bus.out(s->bus.user, bc(), v);
                set_hl(uint16_t(hl() + step));
                s->f = uint8_t((s->b ? 0 : FZ) | FN | (s->b & (FS | FY | FX)));
                if (rep && s->b) { s->pc -= 2; cyc = 21; }
                break;
            }
            }
        }
        extra += cyc - 4;
    }

    // Executes one (possibly prefixed) instruction; returns T-states.
    int exec() {
        extra = 0;
        ixmode = 0;
        uint8_t op = imm8();
        inc_r();
        while (op == 0xDD || op == 0xFD) {
            ixmode = op == 0xDD ? 1 : 2;
            extra += 4;
            op = imm8();
            inc_r();
        }
        if (op == 0xCB) { exec_cb(); return 4 + extra; }
        if (op == 0xED) { exec_ed(); return 4 + extra; }
        int cyc = kCycles[op];
        const int x = op >> 6, y = (op >> 3) & 7, z = op & 7, p = y >> 1, q = y & 1;
        switch (x) {
        case 0:
            switch (z) {
            case 0:
                switch (y) {
                case 0: break;  // NOP
                case 1: {       // EX AF,AF'
                    uint8_t t = s->a; s->a = s->a2; s->a2 = t;
                    t = s->f; s->f = s->f2; s->f2 = t;
                    break;
                }
                case 2: {  // DJNZ
                    int8_t d = int8_t(imm8());
                    s->b = uint8_t(s->b - 1);
                    if (s->b) { s->pc = uint16_t(s->pc + d); cyc += 5; }
                    break;
                }
                case 3: { int8_t d = int8_t(imm8()); s->pc = uint16_t(s->pc + d); break; }  // JR
                default: {  // JR cc
                    int8_t d = int8_t(imm8());
                    if (cond(y - 4)) { s->pc = uint16_t(s->pc + d); cyc += 5; }
                    break;
                }
                }
                break;
            case 1:
                if (q == 0) set_rp(p, imm16());
                else set_xy(add16(xy(), rp(p)));
                break;
            case 2:
                switch (y) {
                case 0: wr(bc(), s->a); break;
                case 1: s->a = rd(bc()); break;
                case 2: wr(de(), s->a); break;
                case 3: s->a = rd(de()); break;
                case 4: wr16(imm16(), xy()); break;
                case 5: set_xy(rd16(imm16())); break;
                case 6: wr(imm16(), s->a); break;
                default: s->a = rd(imm16()); break;
                }
                break;
            case 3:
                if (q == 0) set_rp(p, uint16_t(rp(p) + 1));
                else set_rp(p, uint16_t(rp(p) - 1));
                break;
            case 4: case 5: {
                uint16_t a = y == 6 ? mem_addr() : 0;
                uint8_t v = get_r(y, a);
                v = z == 4 ? inc8(v) : dec8(v);
                set_r(y, v, a);
                break;
            }
            case 6: {
                uint16_t a = y == 6 ? mem_addr() : 0;
                if (y == 6 && ixmode) extra -= 3;  // LD (IX+d),n is 19 T-states
                set_r(y, imm8(), a);
                break;
            }
            default:
                switch (y) {
                case 0: { uint8_t c = s->a >> 7; s->a = uint8_t((s->a << 1) | c); s->f = uint8_t((s->f & (FS | FZ | FP)) | (s->a & (FY | FX)) | c); break; }
                case 1: { uint8_t c = s->a & 1; s->a = uint8_t((s->a >> 1) | (c << 7)); s->f = uint8_t((s->f & (FS | FZ | FP)) | (s->a & (FY | FX)) | c); break; }
                case 2: { uint8_t c = s->a >> 7; s->a = uint8_t((s->a << 1) | (s->f & FC)); s->f = uint8_t((s->f & (FS | FZ | FP)) | (s->a & (FY | FX)) | c); break; }
                case 3: { uint8_t c = s->a & 1; s->a = uint8_t((s->a >> 1) | ((s->f & FC) << 7)); s->f = uint8_t((s->f & (FS | FZ | FP)) | (s->a & (FY | FX)) | c); break; }
                case 4: daa(); break;
                case 5: s->a = uint8_t(~s->a); s->f = uint8_t((s->f & (FS | FZ | FP | FC)) | FH | FN | (s->a & (FY | FX))); break;
                case 6: s->f = uint8_t((s->f & (FS | FZ | FP)) | FC | (s->a & (FY | FX))); break;
                default: s->f = uint8_t(((s->f & (FS | FZ | FP | FC)) | ((s->f & FC) ? FH : 0) | (s->a & (FY | FX))) ^ FC); break;
                }
                break;
            }
            break;
        case 1:
            if (y == 6 && z == 6) {  // HALT
                s->halted = 1;
                s->pc--;
            } else {
                // LD r,r'. With (IX+d) the other operand uses plain H/L.
                bool m = y == 6 || z == 6;
                uint16_t a = m ? mem_addr() : 0;
                set_r(y, get_r(z, a, !m), a, !m);
            }
            break;
        case 2: {
            uint16_t a = z == 6 ? mem_addr() : 0;
            alu(y, get_r(z, a));
            break;
        }
        default:
            switch (z) {
            case 0: if (cond(y)) { s->pc = pop(); cyc += 6; } break;
            case 1:
                if (q == 0) set_rp2(p, pop());
                else {
                    switch (p) {
                    case 0: s->pc = pop(); break;  // RET
                    case 1: {  // EXX
                        uint8_t t;
                        t = s->b; s->b = s->b2; s->b2 = t; t = s->c; s->c = s->c2; s->c2 = t;
                        t = s->d; s->d = s->d2; s->d2 = t; t = s->e; s->e = s->e2; s->e2 = t;
                        t = s->h; s->h = s->h2; s->h2 = t; t = s->l; s->l = s->l2; s->l2 = t;
                        break;
                    }
                    case 2: s->pc = xy(); break;  // JP (HL)
                    default: s->sp = xy(); break;  // LD SP,HL
                    }
                }
                break;
            case 2: { uint16_t a = imm16(); if (cond(y)) s->pc = a; break; }
            case 3:
                switch (y) {
                case 0: s->pc = imm16(); break;
                case 2: s->bus.out(s->bus.user, uint16_t((s->a << 8) | imm8()), s->a); break;
                case 3: s->a = s->bus.in(s->bus.user, uint16_t((s->a << 8) | imm8())); break;
                case 4: { uint16_t t = rd16(s->sp); wr16(s->sp, xy()); set_xy(t); break; }
                case 5: { uint16_t t = de(); set_de(hl()); set_hl(t); break; }  // EX DE,HL (unaffected by prefix)
                case 6: s->iff1 = s->iff2 = 0; break;                          // DI
                default: s->iff1 = s->iff2 = 1; s->ei_pending = 1; break;      // EI
                }
                break;
            case 4: { uint16_t a = imm16(); if (cond(y)) { push(s->pc); s->pc = a; cyc += 7; } break; }
            case 5:
                if (q == 0) push(rp2(p));
                else if (p == 0) { uint16_t a = imm16(); push(s->pc); s->pc = a; }  // CALL
                break;
            case 6: alu(y, imm8()); break;
            default: push(s->pc); s->pc = uint16_t(y * 8); break;  // RST
            }
            break;
        }
        return cyc + extra;
    }
};

} // namespace

void reset(State* s) {
    Bus bus = s->bus;
    *s = State{};
    s->bus = bus;
    s->a = s->f = 0xFF;
    s->sp = 0xFFFF;
    s->im = 0;
}

int step(State* s) {
    if (s->irq_line && s->iff1 && !s->ei_pending) {
        s->iff1 = s->iff2 = 0;
        if (s->halted) { s->halted = 0; s->pc++; }
        Cpu cpu{s, 0, 0};
        cpu.inc_r();
        cpu.push(s->pc);
        if (s->im == 2) {
            uint16_t vec = uint16_t((s->i << 8) | 0xFF);
            s->pc = cpu.rd16(vec);
            return 19;
        }
        s->pc = 0x38;  // IM 0 with RST 38h on the bus / IM 1
        return 13;
    }
    s->ei_pending = 0;
    if (s->halted) {
        Cpu cpu{s, 0, 0};
        cpu.inc_r();
        return 4;
    }
    Cpu cpu{s, 0, 0};
    return cpu.exec();
}

void run(State* s, int32_t cycles) {
    s->cycles += cycles;
    while (s->cycles > 0) s->cycles -= step(s);
}

} // namespace z80
