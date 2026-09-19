// 68000 -> C++ code generation.
#include "emit.h"
#include <cstdarg>
#include <set>

namespace chaotix::recompiler {

using namespace analysis;

std::string fn_name(Cpu cpu, uint64_t key) {
    char b[48];
    int bank = key_bank(key);
    if (cpu == Cpu::M68K) {
        if (bank >= 0) std::snprintf(b, sizeof b, "m68k_%06X_b%d", key_addr(key), bank);
        else std::snprintf(b, sizeof b, "m68k_%06X", key_addr(key));
    } else {
        std::snprintf(b, sizeof b, "sh2_%08X", key_addr(key));
    }
    return b;
}

std::string label(uint32_t addr) {
    char b[24];
    std::snprintf(b, sizeof b, "L_%08X", addr);
    return b;
}

std::map<uint64_t, uint64_t> block_owner(const Program& p) {
    std::map<uint64_t, uint64_t> owner;
    // Prefer the function whose entry is the block itself, then the smallest.
    for (const auto& [k, f] : p.functions) owner[k] = k;
    for (const auto& [k, f] : p.functions) {
        for (uint64_t b : f.blocks) {
            auto it = owner.find(b);
            if (it == owner.end()) owner[b] = k;
            else if (it->second != b && p.functions.at(it->second).blocks.size() > f.blocks.size()) it->second = k;
        }
    }
    return owner;
}

namespace {

struct Out {
    std::string s;
    void f(const char* fmt, ...) {
        char buf[1024];
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(buf, sizeof buf, fmt, ap);
        va_end(ap);
        s += buf;
    }
};

std::string ea_init(const m68k::Ea& e) {
    char b[128];
    std::snprintf(b, sizeof b, "{static_cast<m68k::EaMode>(%d), %u, %u, %u, %d, 0x%Xu}", int(e.mode), e.reg, e.xreg, e.xlong, e.disp, e.value);
    return b;
}

std::string insn_init(const m68k::Insn& in) {
    char b[512];
    std::snprintf(b, sizeof b, "{0x%Xu, 0x%04X, %u, static_cast<m68k::Op>(%d), %u, %u, %u, %s, %s, 0x%Xu, 0x%Xu, %u, 0x%X}", in.pc, in.opcode,
                  in.len, int(in.op), in.size, in.cc, in.reg, ea_init(in.src).c_str(), ea_init(in.dst).c_str(), in.imm, in.target,
                  in.cycles, in.flags);
    return b;
}

// Semantic call for a non-control-flow instruction (mirrors m68k::execute).
const char* op_call(m68k::Op op) {
    using m68k::Op;
    switch (op) {
    case Op::ORI_CCR: case Op::ANDI_CCR: case Op::EORI_CCR: return "m68k::op_ccr_imm(c, I)";
    case Op::ORI_SR: case Op::ANDI_SR: case Op::EORI_SR: return "m68k::op_sr_imm(c, I)";
    case Op::ORI: case Op::OR: return "m68k::op_alu<m68k::Alu::OR>(c, I)";
    case Op::ANDI: case Op::AND: return "m68k::op_alu<m68k::Alu::AND>(c, I)";
    case Op::SUBI: case Op::SUB: return "m68k::op_alu<m68k::Alu::SUB>(c, I)";
    case Op::ADDI: case Op::ADD: return "m68k::op_alu<m68k::Alu::ADD>(c, I)";
    case Op::EORI: case Op::EOR: return "m68k::op_alu<m68k::Alu::EOR>(c, I)";
    case Op::CMPI: case Op::CMP: return "m68k::op_alu<m68k::Alu::CMP>(c, I)";
    case Op::ADDX: return "m68k::op_alu<m68k::Alu::ADDX>(c, I)";
    case Op::SUBX: return "m68k::op_alu<m68k::Alu::SUBX>(c, I)";
    case Op::BTST: return "m68k::op_bit<m68k::Bit::TST>(c, I)";
    case Op::BCHG: return "m68k::op_bit<m68k::Bit::CHG>(c, I)";
    case Op::BCLR: return "m68k::op_bit<m68k::Bit::CLR>(c, I)";
    case Op::BSET: return "m68k::op_bit<m68k::Bit::SET>(c, I)";
    case Op::MOVEP_MR: return "m68k::op_movep_mr(c, I)";
    case Op::MOVEP_RM: return "m68k::op_movep_rm(c, I)";
    case Op::MOVE: return "m68k::op_move(c, I)";
    case Op::MOVEA: return "m68k::op_movea(c, I)";
    case Op::MOVE_FROM_SR: return "m68k::op_move_from_sr(c, I)";
    case Op::MOVE_TO_CCR: return "m68k::op_move_to_ccr(c, I)";
    case Op::MOVE_TO_SR: return "m68k::op_move_to_sr(c, I)";
    case Op::NEGX: return "m68k::op_unary<m68k::Un::NEGX>(c, I)";
    case Op::CLR: return "m68k::op_unary<m68k::Un::CLR>(c, I)";
    case Op::NEG: return "m68k::op_unary<m68k::Un::NEG>(c, I)";
    case Op::NOT: return "m68k::op_unary<m68k::Un::NOT>(c, I)";
    case Op::EXT: return "m68k::op_ext(c, I)";
    case Op::NBCD: return "m68k::op_nbcd(c, I)";
    case Op::SWAP: return "m68k::op_swap(c, I)";
    case Op::PEA: return "m68k::op_pea(c, I)";
    case Op::TAS: return "m68k::op_tas(c, I)";
    case Op::TST: return "m68k::op_tst(c, I)";
    case Op::LINK: return "m68k::op_link(c, I)";
    case Op::UNLK: return "m68k::op_unlk(c, I)";
    case Op::MOVE_TO_USP: case Op::MOVE_FROM_USP: return "m68k::op_move_usp(c, I)";
    case Op::RESET: return "m68k::op_reset(c, I)";
    case Op::NOP: return "(void)I";
    case Op::STOP: return "m68k::op_stop(c, I)";
    case Op::TRAPV: return "m68k::op_trapv(c, I)";
    case Op::MOVEM_RM: return "m68k::op_movem_rm(c, I)";
    case Op::MOVEM_MR: return "m68k::op_movem_mr(c, I)";
    case Op::LEA: return "m68k::op_lea(c, I)";
    case Op::CHK: return "m68k::op_chk(c, I)";
    case Op::ADDQ: return "m68k::op_addq<false>(c, I)";
    case Op::SUBQ: return "m68k::op_addq<true>(c, I)";
    case Op::SCC: return "m68k::op_scc(c, I)";
    case Op::MOVEQ: return "m68k::op_moveq(c, I)";
    case Op::DIVU: return "m68k::op_div<false>(c, I)";
    case Op::DIVS: return "m68k::op_div<true>(c, I)";
    case Op::SBCD: return "m68k::op_bcd<m68k::Bcd::SBCD>(c, I)";
    case Op::ABCD: return "m68k::op_bcd<m68k::Bcd::ABCD>(c, I)";
    case Op::SUBA: return "m68k::op_adda<1>(c, I)";
    case Op::ADDA: return "m68k::op_adda<0>(c, I)";
    case Op::CMPA: return "m68k::op_adda<2>(c, I)";
    case Op::CMPM: return "m68k::op_cmpm(c, I)";
    case Op::MULU: return "m68k::op_mulu(c, I)";
    case Op::MULS: return "m68k::op_muls(c, I)";
    case Op::EXG: return "m68k::op_exg(c, I)";
    case Op::ASL: return "m68k::op_shift<m68k::Sh::ASL>(c, I)";
    case Op::ASR: return "m68k::op_shift<m68k::Sh::ASR>(c, I)";
    case Op::LSL: return "m68k::op_shift<m68k::Sh::LSL>(c, I)";
    case Op::LSR: return "m68k::op_shift<m68k::Sh::LSR>(c, I)";
    case Op::ROXL: return "m68k::op_shift<m68k::Sh::ROXL>(c, I)";
    case Op::ROXR: return "m68k::op_shift<m68k::Sh::ROXR>(c, I)";
    case Op::ROL: return "m68k::op_shift<m68k::Sh::ROL>(c, I)";
    case Op::ROR: return "m68k::op_shift<m68k::Sh::ROR>(c, I)";
    default: return nullptr;
    }
}

struct FnCtx {
    const Program& p;
    const Function& f;
    std::set<uint64_t> blockset;
    int bank;
    Out o;
};

void jump(FnCtx& x, uint32_t t) {
    int tb = x.p.space_of(t, x.bank) >= 0 ? x.bank : -1;
    uint64_t k = key_of(t, tb);
    if (x.blockset.count(k)) x.o.f("    goto %s;\n", label(t).c_str());
    else x.o.f("    return 1;\n");
}

void call(FnCtx& x, uint32_t t, uint32_t ret, bool static_target) {
    std::string callee;
    if (static_target) {
        int tb = x.p.space_of(t, x.bank) >= 0 ? x.bank : -1;
        uint64_t k = key_of(t, tb);
        // Only call directly into the banked window from code in the same bank.
        if (x.p.functions.count(k) && (tb < 0 || tb == x.bank)) callee = fn_name(Cpu::M68K, k);
    }
    x.o.f("    {\n");
    if (!callee.empty()) {
        x.o.f("      if (::recomp::g_depth >= ::recomp::kMaxDepth) return 1;\n");
        x.o.f("      ++::recomp::g_depth; int r_ = %s(c); --::recomp::g_depth;\n", callee.c_str());
    } else {
        x.o.f("      int r_ = ::recomp::m68k_call_dynamic(c);\n");
    }
    x.o.f("      if (!r_ || c->pc != 0x%Xu) return r_;\n", ret);
    x.o.f("    }\n");
    jump(x, ret);
}

void emit_insn(FnCtx& x, const m68k::Insn& in, bool last_in_block, uint32_t block_end) {
    Out& o = x.o;
    const uint32_t next = in.pc + in.len;
    o.f("  // %06X: %s\n", in.pc, m68k::disassemble(in).c_str());
    o.f("  { [[maybe_unused]] static constexpr m68k::Insn I = %s;\n", insn_init(in).c_str());
    o.f("    c->cycles -= %u;\n", in.cycles);
    using m68k::Op;
    switch (in.op) {
    case Op::BRA:
        o.f("    c->pc = 0x%Xu; M68K_BOUNDARY();\n", in.target);
        jump(x, in.target);
        o.f("  }\n");
        return;
    case Op::BCC:
        o.f("    if (m68k::op_bcc(c, I)) { M68K_BOUNDARY();\n");
        jump(x, in.target);
        o.f("    }\n    c->pc = 0x%Xu; M68K_BOUNDARY();\n", next);
        jump(x, next);
        o.f("  }\n");
        return;
    case Op::DBCC:
        o.f("    if (m68k::op_dbcc(c, I)) { M68K_BOUNDARY();\n");
        jump(x, in.target);
        o.f("    }\n    c->pc = 0x%Xu; M68K_BOUNDARY();\n", next);
        jump(x, next);
        o.f("  }\n");
        return;
    case Op::BSR:
        o.f("    m68k::op_bsr(c, I); M68K_BOUNDARY();\n");
        call(x, in.target, next, true);
        o.f("  }\n");
        return;
    case Op::JSR:
        o.f("    m68k::op_jsr(c, I); M68K_BOUNDARY();\n");
        call(x, in.target, next, !(in.flags & m68k::IF_INDIRECT));
        o.f("  }\n");
        return;
    case Op::JMP:
        o.f("    m68k::op_jmp(c, I); M68K_BOUNDARY();\n");
        if (in.flags & m68k::IF_INDIRECT) o.f("    return 1;\n");
        else jump(x, in.target);
        o.f("  }\n");
        return;
    case Op::RTS: o.f("    m68k::op_rts(c); return ::recomp::m68k_ok(c);\n  }\n"); return;
    case Op::RTR: o.f("    m68k::op_rtr(c); return ::recomp::m68k_ok(c);\n  }\n"); return;
    case Op::RTE: o.f("    c->pc = 0x%Xu; m68k::op_rte(c, I); return ::recomp::m68k_ok(c);\n  }\n", next); return;
    case Op::TRAP: o.f("    c->pc = 0x%Xu; m68k::op_trap(c, I); return ::recomp::m68k_ok(c);\n  }\n", next); return;
    case Op::LINE_A: o.f("    m68k::exception(c, m68k::VEC_LINE_A, 0x%Xu); return ::recomp::m68k_ok(c);\n  }\n", in.pc); return;
    case Op::LINE_F: o.f("    m68k::exception(c, m68k::VEC_LINE_F, 0x%Xu); return ::recomp::m68k_ok(c);\n  }\n", in.pc); return;
    case Op::ILLEGAL: case Op::INVALID:
        o.f("    m68k::exception(c, m68k::VEC_ILLEGAL, 0x%Xu); return ::recomp::m68k_ok(c);\n  }\n", in.pc);
        return;
    default: break;
    }
    const char* call_s = op_call(in.op);
    if (m68k::ends_block(in)) {
        o.f("    c->pc = 0x%Xu; %s; M68K_BOUNDARY();\n", next, call_s);
        o.f("    if (c->pc != 0x%Xu) return 1;\n", next);
        jump(x, next);
        o.f("  }\n");
        return;
    }
    o.f("    %s; }\n", call_s);
    if (last_in_block) {
        // Block split by a label: fall through into the next block.
        o.f("  c->pc = 0x%Xu;\n", block_end);
        jump(x, block_end);
    }
}

} // namespace

void emit_m68k(const Program& p, const EmitOptions& opt, EmitResult& out) {
    auto owner = block_owner(p);
    std::vector<uint64_t> fkeys;
    for (const auto& [k, f] : p.functions) fkeys.push_back(k);
    for (const auto& [k, f] : p.functions) out.decls.push_back("int " + fn_name(Cpu::M68K, k) + "(m68k::State* c);");

    const size_t per_file = (fkeys.size() + kM68kShards - 1) / kM68kShards;
    for (size_t file_index = 0; file_index < kM68kShards; ++file_index) {
        const size_t start = file_index * per_file;
        char fname[64];
        std::snprintf(fname, sizeof fname, "recomp_m68k_%03zu.cpp", file_index);
        std::string body;
        body += "// Generated by chaotix_recomp from the user's ROM. Do not edit; do not commit.\n";
        body += "#include \"recomp_decls.h\"\n\nnamespace recomp {\n\n";
        for (size_t i = start; i < fkeys.size() && i < start + per_file; ++i) {
            const Function& f = p.functions.at(fkeys[i]);
            FnCtx x{p, f, std::set<uint64_t>(f.blocks.begin(), f.blocks.end()), key_bank(f.entry), {}};
            x.o.f("int %s(m68k::State* c) {\n", fn_name(Cpu::M68K, f.entry).c_str());
            x.o.f("  switch (c->pc) {\n");
            for (uint64_t b : f.blocks) x.o.f("  case 0x%Xu: goto %s;\n", key_addr(b), label(key_addr(b)).c_str());
            x.o.f("  default: return 1;\n  }\n");
            for (uint64_t bk : f.blocks) {
                const Block& b = p.blocks.at(bk);
                x.o.f("%s:\n", label(b.start).c_str());
                for (size_t n = 0; n < b.insns.size(); ++n) {
                    m68k::Insn in;
                    p.decode_m68k(b.insns[n], x.bank, in);
                    emit_insn(x, in, n + 1 == b.insns.size(), b.end);
                    ++out.instructions;
                }
                if (b.insns.empty()) x.o.f("  return 1;\n");
            }
            x.o.f("}\n\n");
            body += x.o.s;
            ++out.functions;
        }
        body += "} // namespace recomp\n";
        FILE* fp = std::fopen((opt.out_dir + "/" + fname).c_str(), "w");
        std::fwrite(body.data(), 1, body.size(), fp);
        std::fclose(fp);
        out.files.push_back(fname);
    }
    for (const auto& [bk, fk] : owner) {
        int bank = key_bank(bk);
        uint32_t key = key_addr(bk) | (bank >= 0 ? uint32_t(bank + 1) << 24 : 0);
        out.table.push_back({key, fn_name(Cpu::M68K, fk)});
    }
}

} // namespace chaotix::recompiler
