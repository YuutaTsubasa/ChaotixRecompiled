// 68000 instruction tests (known answers from the M68000 Programmer's
// Reference Manual). They exercise the shared semantics used by both the
// interpreter and the generated code.
#include "cpu/m68k/m68k.h"
#include "cpu/m68k/m68k_interp.h"
#include "cpu/m68k/m68k_ops.h"
#include "test_framework.h"
#include <cstring>
#include <initializer_list>

namespace {

struct Cpu68 {
    uint8_t mem[0x10000];
    m68k::State c{};
    Cpu68() {
        std::memset(mem, 0, sizeof mem);
        for (int i = 0; i < 256; ++i) { c.bus.rpage[i] = mem; c.bus.wpage[i] = mem; }  // 64K mirrored
        c.s = 1;
        c.imask = 7;
        c.a[7] = 0x8000;
        // exception vectors -> 0xF000 + vector*16
        for (uint32_t v = 2; v < 64; ++v) {
            uint32_t t = 0xF000 + v * 16;
            mem[v * 4] = 0; mem[v * 4 + 1] = 0; mem[v * 4 + 2] = uint8_t(t >> 8); mem[v * 4 + 3] = uint8_t(t);
        }
    }
    void load(uint32_t at, std::initializer_list<uint16_t> words) {
        for (uint16_t w : words) { mem[at] = uint8_t(w >> 8); mem[at + 1] = uint8_t(w); at += 2; }
    }
    // Executes `n` instructions starting at pc.
    void run(uint32_t pc, int n) {
        c.pc = pc;
        for (int i = 0; i < n; ++i) {
            m68k::Insn in;
            m68k::decode(c.pc, m68k::fetch_bus, &c, in);
            c.pc += in.len;
            c.cycles -= in.cycles;
            m68k::execute(&c, in);
        }
    }
    uint32_t ccr() const { return m68k::get_ccr(&c); }
};

enum { X = 0x10, N = 8, Z = 4, V = 2, C = 1 };

} // namespace

TEST(m68k, add_byte_overflow) {
    Cpu68 t;
    t.c.d[0] = 0x01; t.c.d[1] = 0x7F;
    t.load(0x100, {0xD200});  // add.b d0,d1
    t.run(0x100, 1);
    CHECK_EQ(t.c.d[1] & 0xFF, 0x80u);
    CHECK_EQ(t.ccr(), uint32_t(N | V));
}

TEST(m68k, add_word_carry_zero) {
    Cpu68 t;
    t.c.d[0] = 1; t.c.d[1] = 0x1234FFFF;
    t.load(0x100, {0xD240});  // add.w d0,d1
    t.run(0x100, 1);
    CHECK_EQ(t.c.d[1], 0x12340000u);  // upper word preserved
    CHECK_EQ(t.ccr(), uint32_t(X | Z | C));
}

TEST(m68k, sub_long_borrow) {
    Cpu68 t;
    t.c.d[0] = 1; t.c.d[1] = 0;
    t.load(0x100, {0x9280});  // sub.l d0,d1
    t.run(0x100, 1);
    CHECK_EQ(t.c.d[1], 0xFFFFFFFFu);
    CHECK_EQ(t.ccr(), uint32_t(X | N | C));
}

TEST(m68k, cmp_preserves_x) {
    Cpu68 t;
    m68k::set_ccr(&t.c, X);
    t.c.d[0] = 5; t.c.d[1] = 3;
    t.load(0x100, {0xB280});  // cmp.l d0,d1  (3 - 5)
    t.run(0x100, 1);
    CHECK_EQ(t.ccr(), uint32_t(X | N | C));
}

TEST(m68k, addx_z_sticky) {
    Cpu68 t;
    m68k::set_ccr(&t.c, Z);           // Z set before; result zero -> stays set
    t.c.d[0] = 0; t.c.d[1] = 0;
    t.load(0x100, {0xD380});          // addx.l d0,d1
    t.run(0x100, 1);
    CHECK(t.ccr() & Z);
    m68k::set_ccr(&t.c, Z | X);       // nonzero result clears Z
    t.run(0x100, 1);
    CHECK_EQ(t.c.d[1], 1u);
    CHECK(!(t.ccr() & Z));
}

TEST(m68k, abcd_sbcd_nbcd) {
    Cpu68 t;
    t.c.d[0] = 0x38; t.c.d[1] = 0x45;
    m68k::set_ccr(&t.c, Z);
    t.load(0x100, {0xC300});  // abcd d0,d1
    t.run(0x100, 1);
    CHECK_EQ(t.c.d[1] & 0xFF, 0x83u);
    CHECK(!(t.ccr() & C));
    t.c.d[0] = 0x01; t.c.d[1] = 0x99;
    m68k::set_ccr(&t.c, 0);
    t.run(0x100, 1);
    CHECK_EQ(t.c.d[1] & 0xFF, 0x00u);
    CHECK(t.ccr() & C);
    CHECK(t.ccr() & X);
    t.c.d[0] = 0x01; t.c.d[1] = 0x10;
    m68k::set_ccr(&t.c, 0);
    t.load(0x110, {0x8300});  // sbcd d0,d1
    t.run(0x110, 1);
    CHECK_EQ(t.c.d[1] & 0xFF, 0x09u);
    t.c.d[2] = 0x01;
    m68k::set_ccr(&t.c, 0);
    t.load(0x120, {0x4802});  // nbcd d2
    t.run(0x120, 1);
    CHECK_EQ(t.c.d[2] & 0xFF, 0x99u);
    CHECK(t.ccr() & C);
}

TEST(m68k, shifts_and_rotates) {
    Cpu68 t;
    t.c.d[0] = 0x40;
    t.load(0x100, {0xE300});  // asl.b #1,d0
    t.run(0x100, 1);
    CHECK_EQ(t.c.d[0] & 0xFF, 0x80u);
    CHECK(t.ccr() & V);
    t.c.d[0] = 0x8001;
    t.load(0x110, {0xE240});  // asr.w #1,d0
    t.run(0x110, 1);
    CHECK_EQ(t.c.d[0] & 0xFFFF, 0xC000u);
    CHECK((t.ccr() & (C | X)) == (C | X));
    t.c.d[0] = 1;
    t.load(0x120, {0xE288});  // lsr.l #1,d0
    t.run(0x120, 1);
    CHECK_EQ(t.c.d[0], 0u);
    CHECK((t.ccr() & (Z | C)) == (Z | C));
    t.c.d[0] = 0x81;
    t.load(0x130, {0xE318});  // rol.b #1,d0
    t.run(0x130, 1);
    CHECK_EQ(t.c.d[0] & 0xFF, 0x03u);
    CHECK(t.ccr() & C);
    t.c.d[0] = 0x80;
    m68k::set_ccr(&t.c, X);
    t.load(0x140, {0xE310});  // roxl.b #1,d0
    t.run(0x140, 1);
    CHECK_EQ(t.c.d[0] & 0xFF, 0x01u);
    CHECK((t.ccr() & (X | C)) == (X | C));
    // Shift count taken modulo 64 from a register; count 0 clears C, keeps X.
    t.c.d[0] = 0x55; t.c.d[1] = 64;
    m68k::set_ccr(&t.c, X | C);
    t.load(0x150, {0xE3A8});  // lsl.l d1,d0
    t.run(0x150, 1);
    CHECK_EQ(t.c.d[0], 0x55u);
    CHECK_EQ(t.ccr() & (X | C), uint32_t(X));
    // ASL by 8 on a byte: result 0, C = X = bit 0, V set if value was nonzero
    t.c.d[0] = 0x01; t.c.d[1] = 8;
    t.load(0x160, {0xE320 | (1 << 9)});  // asl.b d1,d0
    t.run(0x160, 1);
    CHECK_EQ(t.c.d[0] & 0xFF, 0u);
    CHECK((t.ccr() & (C | X | V | Z)) == (C | X | V | Z));
}

TEST(m68k, mul_div) {
    Cpu68 t;
    t.c.d[0] = 7; t.c.d[1] = 100;
    t.load(0x100, {0x82C0});  // divu.w d0,d1
    t.run(0x100, 1);
    CHECK_EQ(t.c.d[1], 0x0002000Eu);
    t.c.d[0] = 1; t.c.d[1] = 0x00010000;
    t.run(0x100, 1);  // overflow: register unchanged, V set
    CHECK_EQ(t.c.d[1], 0x00010000u);
    CHECK(t.ccr() & V);
    t.c.d[0] = 7; t.c.d[1] = uint32_t(-100);
    t.load(0x110, {0x83C0});  // divs.w d0,d1
    t.run(0x110, 1);
    CHECK_EQ(t.c.d[1], 0xFFFEFFF2u);  // remainder -2, quotient -14
    t.c.d[0] = 0xFFFE; t.c.d[1] = 3;
    t.load(0x120, {0xC3C0});  // muls.w d0,d1
    t.run(0x120, 1);
    CHECK_EQ(t.c.d[1], uint32_t(-6));
    CHECK(t.ccr() & N);
    t.c.d[0] = 0xFFFF; t.c.d[1] = 0xFFFF;
    t.load(0x130, {0xC2C0});  // mulu.w d0,d1
    t.run(0x130, 1);
    CHECK_EQ(t.c.d[1], 0xFFFE0001u);
}

TEST(m68k, divide_by_zero_trap) {
    Cpu68 t;
    t.c.d[0] = 0; t.c.d[1] = 5;
    t.load(0x100, {0x82C0});  // divu.w d0,d1
    t.run(0x100, 1);
    CHECK_EQ(t.c.pc, 0xF000u + 5 * 16);
    CHECK_EQ(m68k::rd32(&t.c, t.c.a[7] + 2), 0x102u);  // return address = next instruction
}

TEST(m68k, lea_abs_short_sign_extends) {
    Cpu68 t;
    t.load(0x100, {0x41F8, 0xE20A});  // lea ($E20A).w,a0
    t.run(0x100, 1);
    CHECK_EQ(t.c.a[0], 0xFFFFE20Au);
}

TEST(m68k, movem_roundtrip_predec_postinc) {
    Cpu68 t;
    for (int i = 0; i < 8; ++i) t.c.d[i] = 0x11111111u * uint32_t(i + 1);
    t.c.a[0] = 0x4000;
    t.load(0x100, {0x48E0, 0xFF00});  // movem.l d0-d7,-(a0)
    t.run(0x100, 1);
    CHECK_EQ(t.c.a[0], 0x4000u - 32);
    CHECK_EQ(m68k::rd32(&t.c, 0x4000 - 32), 0x11111111u);  // d0 stored lowest
    for (int i = 0; i < 8; ++i) t.c.d[i] = 0;
    t.load(0x110, {0x4CD8, 0x00FF});  // movem.l (a0)+,d0-d7
    t.run(0x110, 1);
    CHECK_EQ(t.c.d[7], 0x88888888u);
    CHECK_EQ(t.c.a[0], 0x4000u);
    // Word loads sign-extend into the full register.
    m68k::wr16(&t.c, 0x5000, 0x8000);
    t.c.a[1] = 0x5000;
    t.load(0x120, {0x4C91, 0x0001});  // movem.w (a1),d0
    t.run(0x120, 1);
    CHECK_EQ(t.c.d[0], 0xFFFF8000u);
}

TEST(m68k, dbf_loop_and_bsr_rts) {
    Cpu68 t;
    // moveq #3,d0 ; loop: addq.w #1,d1 ; dbf d0,loop
    t.c.d[1] = 0;
    t.load(0x100, {0x7003, 0x5241, 0x51C8, 0xFFFC});
    t.run(0x100, 1 + 4 * 2);
    CHECK_EQ(t.c.d[1], 4u);
    CHECK_EQ(t.c.d[0] & 0xFFFF, 0xFFFFu);
    CHECK_EQ(t.c.pc, 0x108u);
    // bsr to rts
    t.load(0x200, {0x6100, 0x0010});  // bsr.w $212
    t.load(0x212, {0x4E75});           // rts
    uint32_t sp = t.c.a[7];
    t.run(0x200, 2);
    CHECK_EQ(t.c.pc, 0x204u);
    CHECK_EQ(t.c.a[7], sp);
}

TEST(m68k, trap_and_rte) {
    Cpu68 t;
    t.load(0x100, {0x4E41});                   // trap #1
    t.load(0xF000 + 33 * 16, {0x4E73});        // rte
    m68k::set_sr(&t.c, 0x2715);
    t.run(0x100, 1);
    CHECK_EQ(t.c.pc, 0xF000u + 33 * 16);
    t.run(t.c.pc, 1);
    CHECK_EQ(t.c.pc, 0x102u);
    CHECK_EQ(m68k::get_sr(&t.c), 0x2715u);
}

TEST(m68k, interrupt_autovector) {
    Cpu68 t;
    m68k::set_sr(&t.c, 0x2300);
    t.c.pc = 0x100;
    t.c.irq_level = 6;
    CHECK(m68k::check_irq(&t.c));
    CHECK_EQ(t.c.pc, 0xF000u + 30 * 16);
    CHECK_EQ(t.c.imask, 6u);
    t.c.irq_level = 4;  // masked now
    CHECK(!m68k::check_irq(&t.c));
}

TEST(m68k, decoder_rejects_invalid) {
    Cpu68 t;
    t.load(0x100, {0x4AFC});  // illegal
    m68k::Insn in;
    CHECK(!m68k::decode(0x100, m68k::fetch_bus, &t.c, in));
    CHECK(in.op == m68k::Op::ILLEGAL);
    t.load(0x110, {0x60FF, 0, 0});  // bra.l (68020 only)
    CHECK(!m68k::decode(0x110, m68k::fetch_bus, &t.c, in));
}
