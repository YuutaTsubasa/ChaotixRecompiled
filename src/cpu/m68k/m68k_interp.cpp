// Reference 68000 interpreter. Used for validation (lockstep against
// recompiled code) and as the fallback for code not covered by the static
// recompiler (e.g. code the game copies into RAM).
#include "m68k_interp.h"
#include "m68k_ops.h"

namespace m68k {

void reset(State* c) {
    Bus bus = c->bus;
    auto ack = c->irq_ack;
    auto ackuser = c->irq_user;
    auto rh = c->reset_hook;
    *c = State{};
    c->bus = bus;
    c->irq_ack = ack;
    c->irq_user = ackuser;
    c->reset_hook = rh;
    c->s = 1;
    c->imask = 7;
    c->a[7] = rd32(c, 0);
    c->pc = rd32(c, 4) & 0xFFFFFF;
}

void execute(State* c, const Insn& in) {
    switch (in.op) {
    case Op::ORI_CCR: case Op::ANDI_CCR: case Op::EORI_CCR: op_ccr_imm(c, in); break;
    case Op::ORI_SR: case Op::ANDI_SR: case Op::EORI_SR: op_sr_imm(c, in); break;
    case Op::ORI: case Op::OR: op_alu<Alu::OR>(c, in); break;
    case Op::ANDI: case Op::AND: op_alu<Alu::AND>(c, in); break;
    case Op::SUBI: case Op::SUB: op_alu<Alu::SUB>(c, in); break;
    case Op::ADDI: case Op::ADD: op_alu<Alu::ADD>(c, in); break;
    case Op::EORI: case Op::EOR: op_alu<Alu::EOR>(c, in); break;
    case Op::CMPI: case Op::CMP: op_alu<Alu::CMP>(c, in); break;
    case Op::ADDX: op_alu<Alu::ADDX>(c, in); break;
    case Op::SUBX: op_alu<Alu::SUBX>(c, in); break;
    case Op::BTST: op_bit<Bit::TST>(c, in); break;
    case Op::BCHG: op_bit<Bit::CHG>(c, in); break;
    case Op::BCLR: op_bit<Bit::CLR>(c, in); break;
    case Op::BSET: op_bit<Bit::SET>(c, in); break;
    case Op::MOVEP_MR: op_movep_mr(c, in); break;
    case Op::MOVEP_RM: op_movep_rm(c, in); break;
    case Op::MOVE: op_move(c, in); break;
    case Op::MOVEA: op_movea(c, in); break;
    case Op::MOVE_FROM_SR: op_move_from_sr(c, in); break;
    case Op::MOVE_TO_CCR: op_move_to_ccr(c, in); break;
    case Op::MOVE_TO_SR: op_move_to_sr(c, in); break;
    case Op::NEGX: op_unary<Un::NEGX>(c, in); break;
    case Op::CLR: op_unary<Un::CLR>(c, in); break;
    case Op::NEG: op_unary<Un::NEG>(c, in); break;
    case Op::NOT: op_unary<Un::NOT>(c, in); break;
    case Op::EXT: op_ext(c, in); break;
    case Op::NBCD: op_nbcd(c, in); break;
    case Op::SWAP: op_swap(c, in); break;
    case Op::PEA: op_pea(c, in); break;
    case Op::TAS: op_tas(c, in); break;
    case Op::TST: op_tst(c, in); break;
    case Op::TRAP: op_trap(c, in); break;
    case Op::LINK: op_link(c, in); break;
    case Op::UNLK: op_unlk(c, in); break;
    case Op::MOVE_TO_USP: case Op::MOVE_FROM_USP: op_move_usp(c, in); break;
    case Op::RESET: op_reset(c, in); break;
    case Op::NOP: break;
    case Op::STOP: op_stop(c, in); break;
    case Op::RTE: op_rte(c, in); break;
    case Op::RTS: op_rts(c); break;
    case Op::TRAPV: op_trapv(c, in); break;
    case Op::RTR: op_rtr(c); break;
    case Op::JSR: op_jsr(c, in); break;
    case Op::JMP: op_jmp(c, in); break;
    case Op::MOVEM_RM: op_movem_rm(c, in); break;
    case Op::MOVEM_MR: op_movem_mr(c, in); break;
    case Op::LEA: op_lea(c, in); break;
    case Op::CHK: op_chk(c, in); break;
    case Op::ADDQ: op_addq<false>(c, in); break;
    case Op::SUBQ: op_addq<true>(c, in); break;
    case Op::SCC: op_scc(c, in); break;
    case Op::DBCC: op_dbcc(c, in); break;
    case Op::BRA: c->pc = in.target; break;
    case Op::BSR: op_bsr(c, in); break;
    case Op::BCC: op_bcc(c, in); break;
    case Op::MOVEQ: op_moveq(c, in); break;
    case Op::DIVU: op_div<false>(c, in); break;
    case Op::DIVS: op_div<true>(c, in); break;
    case Op::SBCD: op_bcd<Bcd::SBCD>(c, in); break;
    case Op::ABCD: op_bcd<Bcd::ABCD>(c, in); break;
    case Op::SUBA: op_adda<1>(c, in); break;
    case Op::ADDA: op_adda<0>(c, in); break;
    case Op::CMPA: op_adda<2>(c, in); break;
    case Op::CMPM: op_cmpm(c, in); break;
    case Op::MULU: op_mulu(c, in); break;
    case Op::MULS: op_muls(c, in); break;
    case Op::EXG: op_exg(c, in); break;
    case Op::ASL: op_shift<Sh::ASL>(c, in); break;
    case Op::ASR: op_shift<Sh::ASR>(c, in); break;
    case Op::LSL: op_shift<Sh::LSL>(c, in); break;
    case Op::LSR: op_shift<Sh::LSR>(c, in); break;
    case Op::ROXL: op_shift<Sh::ROXL>(c, in); break;
    case Op::ROXR: op_shift<Sh::ROXR>(c, in); break;
    case Op::ROL: op_shift<Sh::ROL>(c, in); break;
    case Op::ROR: op_shift<Sh::ROR>(c, in); break;
    case Op::LINE_A: exception(c, VEC_LINE_A, in.pc); break;
    case Op::LINE_F: exception(c, VEC_LINE_F, in.pc); break;
    case Op::ILLEGAL: case Op::INVALID: default: exception(c, VEC_ILLEGAL, in.pc); break;
    }
}

uint16_t fetch_bus(void* user, uint32_t addr) { return uint16_t(rd16(static_cast<State*>(user), addr)); }

void interp_block(State* c) {
    for (;;) {
        Insn in;
        decode(c->pc, fetch_bus, c, in);
        c->pc = c->pc + in.len;
        c->cycles -= in.cycles;
        execute(c, in);
        if (ends_block(in)) break;
    }
}

} // namespace m68k
