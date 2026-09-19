// SH-2 instruction tests. Division sequences follow the SH-1/SH-2
// Programming Manual examples; delay-slot tests pin down the ordering rules
// the recompiler relies on.
#include "cpu/sh2/sh2.h"
#include "cpu/sh2/sh2_interp.h"
#include "cpu/sh2/sh2_ops.h"
#include "test_framework.h"
#include <cstring>
#include <initializer_list>
#include <memory>

namespace {

struct Cpu {
    uint8_t mem[0x40000];  // mapped at 0x06000000 (SDRAM)
    sh2::State c{};
    Cpu() {
        std::memset(mem, 0, sizeof mem);
        for (uint32_t off = 0; off < sizeof mem; off += 1u << sh2::Bus::kPageShift) {
            c.bus.rpage[sh2::page_of(0x06000000 + off)] = mem + off;
            c.bus.wpage[sh2::page_of(0x06000000 + off)] = mem + off;
        }
        c.r[15] = 0x06030000;
        c.vbr = 0x06000000;
    }
    void load(uint32_t at, std::initializer_list<uint16_t> words) {
        for (uint16_t w : words) { sh2::wr16(&c, at, w); at += 2; }
    }
    // Runs blocks until pc == stop (or a block limit).
    void run(uint32_t pc, uint32_t stop, int max_blocks = 10000) {
        c.pc = pc;
        c.cycles = 1 << 30;
        for (int i = 0; i < max_blocks && c.pc != stop; ++i) sh2::interp_block(&c);
    }
};

constexpr uint32_t B = 0x06000000;

} // namespace

TEST(sh2, div1_unsigned_32_by_16) {
    // R1 (32 bits) / R0 (16 bits) -> R1 (16 bits), manual example:
    //   SHLL16 R0 ; DIV0U ; 16x DIV1 R0,R1 ; ROTCL R1 ; EXTU.W R1,R1
    auto tp = std::make_unique<Cpu>();
    Cpu& t = *tp;
    t.c.r[0] = 7;
    t.c.r[1] = 1000;
    std::initializer_list<uint16_t> seq = {0x4028, 0x0019};
    t.load(B, seq);
    uint32_t a = B + 4;
    for (int i = 0; i < 16; ++i) { t.load(a, {0x3104}); a += 2; }  // div1 r0,r1
    t.load(a, {0x4124, 0x611D});  // rotcl r1 ; extu.w r1,r1
    a += 4;
    t.load(a, {0xAFFE, 0x0009});  // bra $ (stop marker)
    t.run(B, a);
    CHECK_EQ(t.c.r[1], 142u);
}

TEST(sh2, div1_signed_16_by_16) {
    // Signed R1 (16 bits) / R0 (16 bits) -> R1 (16 bits), manual example:
    //   SHLL16 R0 ; EXTS.W R1,R1 ; XOR R2,R2 ; MOV R1,R3 ; ROTCL R3 ; SUBC R2,R1 ;
    //   DIV0S R0,R1 ; 16x DIV1 R0,R1 ; EXTS.W R1,R1 ; ROTCL R1 ; ADDC R2,R1 ; EXTS.W R1,R1
    auto tp = std::make_unique<Cpu>();
    Cpu& t = *tp;
    t.c.r[0] = 7;
    t.c.r[1] = uint32_t(-100);
    t.load(B, {0x4028, 0x611F, 0x222A, 0x6313, 0x4324, 0x312A, 0x2107});
    uint32_t a = B + 14;
    for (int i = 0; i < 16; ++i) { t.load(a, {0x3104}); a += 2; }
    t.load(a, {0x611F, 0x4124, 0x312E, 0x611F});
    a += 8;
    t.load(a, {0xAFFE, 0x0009});
    t.run(B, a);
    CHECK_EQ(int32_t(t.c.r[1]), -14);
}

TEST(sh2, addc_subc_64bit) {
    auto tp = std::make_unique<Cpu>();
    Cpu& t = *tp;
    // (r1:r0) += (r3:r2) : clrt ; addc r2,r0 ; addc r3,r1
    t.c.r[0] = 0xFFFFFFFF; t.c.r[1] = 1;
    t.c.r[2] = 1; t.c.r[3] = 2;
    t.load(B, {0x0008, 0x302E, 0x313E, 0xAFFE, 0x0009});
    t.run(B, B + 6);
    CHECK_EQ(t.c.r[0], 0u);
    CHECK_EQ(t.c.r[1], 4u);
    // (r1:r0) -= (r3:r2)
    t.c.r[0] = 0; t.c.r[1] = 4;
    t.load(B + 0x20, {0x0008, 0x302A, 0x313A, 0xAFFE, 0x0009});
    t.run(B + 0x20, B + 0x26);
    CHECK_EQ(t.c.r[0], 0xFFFFFFFFu);
    CHECK_EQ(t.c.r[1], 1u);
}

TEST(sh2, mac_l_saturation) {
    auto tp = std::make_unique<Cpu>();
    Cpu& t = *tp;
    sh2::wr32(&t.c, B + 0x1000, 0x7FFFFFFF);
    sh2::wr32(&t.c, B + 0x1004, 0x7FFFFFFF);
    t.c.r[4] = B + 0x1000;
    t.c.r[5] = B + 0x1004;
    t.c.sr |= sh2::SR_S;  // 48-bit saturation
    t.c.mach = 0x00007FFF; t.c.macl = 0xFFFFFFF0;
    t.load(B, {0x045F, 0xAFFE, 0x0009});  // mac.l @r5+,@r4+
    t.run(B, B + 2);
    CHECK_EQ(t.c.mach, 0x00007FFFu);
    CHECK_EQ(t.c.macl, 0xFFFFFFFFu);
    CHECK_EQ(t.c.r[4], B + 0x1004);
    CHECK_EQ(t.c.r[5], B + 0x1008);
}

TEST(sh2, mac_w_signed) {
    auto tp = std::make_unique<Cpu>();
    Cpu& t = *tp;
    sh2::wr16(&t.c, B + 0x1000, 0xFFFE);  // -2
    sh2::wr16(&t.c, B + 0x1002, 3);
    t.c.r[4] = B + 0x1000;
    t.c.r[5] = B + 0x1002;
    t.c.mach = 0; t.c.macl = 10;
    t.load(B, {0x445F, 0xAFFE, 0x0009});  // mac.w @r5+,@r4+
    t.run(B, B + 2);
    CHECK_EQ(t.c.macl, 4u);
    CHECK_EQ(t.c.mach, 0u);
}

TEST(sh2, delay_slot_executes_before_jump) {
    auto tp = std::make_unique<Cpu>();
    Cpu& t = *tp;
    // bra target ; add #1,r0 (slot) ... target: bra $
    t.c.r[0] = 0;
    t.load(B, {0xA004, 0x7001});  // bra B+0x0C ; add #1,r0
    t.load(B + 0x0C, {0xAFFE, 0x0009});
    t.run(B, B + 0x0C);
    CHECK_EQ(t.c.r[0], 1u);
}

TEST(sh2, jmp_target_sampled_before_slot) {
    auto tp = std::make_unique<Cpu>();
    Cpu& t = *tp;
    // jmp @r1 ; mov #0,r1 (slot must not change the target)
    t.c.r[1] = B + 0x40;
    t.load(B, {0x412B, 0xE100});
    t.load(B + 0x40, {0xAFFE, 0x0009});
    t.run(B, B + 0x40, 1);
    CHECK_EQ(t.c.pc, B + 0x40);
    CHECK_EQ(t.c.r[1], 0u);
}

TEST(sh2, bsr_sets_pr_rts_returns) {
    auto tp = std::make_unique<Cpu>();
    Cpu& t = *tp;
    t.load(B, {0xB006, 0x0009, 0xAFFE, 0x0009});  // bsr B+0x10 ; nop ; stop: bra $
    t.load(B + 0x10, {0x000B, 0x7005});            // rts ; add #5,r0
    t.c.r[0] = 0;
    t.run(B, B + 4);
    CHECK_EQ(t.c.pr, B + 4);
    CHECK_EQ(t.c.r[0], 5u);
}

TEST(sh2, bt_s_and_bf) {
    auto tp = std::make_unique<Cpu>();
    Cpu& t = *tp;
    // sett ; bt/s +4 ; add #1,r0 ; add #10,r0 ; target: bra $
    t.c.r[0] = 0;
    t.load(B, {0x0018, 0x8D01, 0x7001, 0x700A, 0xAFFE, 0x0009});
    t.run(B, B + 8);
    CHECK_EQ(t.c.r[0], 1u);  // slot executed, add #10 skipped
    // clrt ; bf (disp 0 -> pc+4, not delayed) ; add #1,r1 (skipped) ; target: bra $
    t.c.r[1] = 0;
    t.load(B + 0x20, {0x0008, 0x8B00, 0x7101, 0xAFFE, 0x0009});
    t.run(B + 0x20, B + 0x26);
    CHECK_EQ(t.c.r[1], 0u);
}

TEST(sh2, pc_relative_loads) {
    auto tp = std::make_unique<Cpu>();
    Cpu& t = *tp;
    // mov.l @(4,pc),r2 at B+2: address = ((B+2+4)&~3)+4 = B+8
    t.load(B, {0x0009, 0xD201, 0xAFFE, 0x0009});
    sh2::wr32(&t.c, B + 8, 0xCAFEBABE);
    // mova @(0,pc),r0 at B+0x10 -> (B+0x14)&~3
    t.load(B + 0x10, {0xC700});
    t.run(B, B + 4);
    CHECK_EQ(t.c.r[2], 0xCAFEBABEu);
    sh2::Insn in = sh2::decode(B + 0x10, 0xC700);
    CHECK_EQ(in.addr, B + 0x14);
    // mov.w @(disp,pc) sign-extends
    t.load(B + 0x30, {0x9301, 0xAFFE, 0x0009});
    sh2::wr16(&t.c, B + 0x36, 0x8001);
    t.run(B + 0x30, B + 0x32);
    CHECK_EQ(t.c.r[3], 0xFFFF8001u);
}

TEST(sh2, misc_alu) {
    auto tp = std::make_unique<Cpu>();
    Cpu& t = *tp;
    t.c.r[1] = 0x11223344;
    t.load(B, {0x6218, 0x6319, 0x241D, 0xAFFE, 0x0009});  // swap.b r1,r2 ; swap.w r1,r3 ; xtrct r1,r4
    t.c.r[4] = 0xAABBCCDD;
    t.run(B, B + 6);
    CHECK_EQ(t.c.r[2], 0x11224433u);
    CHECK_EQ(t.c.r[3], 0x33441122u);
    CHECK_EQ(t.c.r[4], 0x3344AABBu);
    // cmp/str: any equal byte
    t.c.r[5] = 0x12345678; t.c.r[6] = 0xFF34FFFF;
    t.load(B + 0x20, {0x256C, 0xAFFE, 0x0009});  // cmp/str r6,r5
    t.run(B + 0x20, B + 0x22);
    CHECK(t.c.sr & sh2::SR_T);
}

TEST(sh2, interrupt_entry_and_rte) {
    auto tp = std::make_unique<Cpu>();
    Cpu& t = *tp;
    sh2::wr32(&t.c, t.c.vbr + 70 * 4, B + 0x100);  // IRL 12 autovector
    t.load(B + 0x100, {0x002B, 0x0009});          // rte ; nop
    t.c.sr = 0x0F0 & ~0x0F0u;                      // mask 0
    t.c.pc = B + 0x50;
    t.c.irq_level = 12;
    t.c.irq_vector = 70;
    CHECK(sh2::check_irq(&t.c));
    CHECK_EQ(t.c.pc, B + 0x100u);
    CHECK_EQ((t.c.sr >> 4) & 15, 12u);
    t.c.irq_level = 0;
    t.c.cycles = 100;
    sh2::interp_block(&t.c);
    CHECK_EQ(t.c.pc, B + 0x50u);
    CHECK_EQ((t.c.sr >> 4) & 15, 0u);
}
