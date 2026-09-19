// SH-2 -> C++ code generation.
//
// Delayed branches are emitted in hardware order: the branch target (and PR,
// or the RTE stack pop) is computed first, then the delay-slot instruction
// executes, then control transfers. Interrupts are never accepted between a
// branch and its slot because boundary checks only happen after the pair.
#include "emit.h"
#include <algorithm>
#include <cstdarg>
#include <set>

namespace chaotix::recompiler {

using namespace analysis;

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

std::string insn_init(const sh2::Insn& in) {
    char b[256];
    std::snprintf(b, sizeof b, "{0x%08Xu, 0x%04X, static_cast<sh2::Op>(%d), %u, %u, %d, 0x%08Xu, %u, 0x%X}", in.pc, in.opcode, int(in.op),
                  in.n, in.m, in.imm, in.addr, in.cycles, in.flags);
    return b;
}

struct FnCtx {
    const Program& p;
    const Function& f;
    std::set<uint64_t> blockset;
    bool ram;
    std::vector<std::pair<uint32_t, uint32_t>> ranges;  // validation ranges (addr, len)
    size_t folded = 0;
    size_t fast_loops = 0;
    Out o;
};

void jump(FnCtx& x, uint32_t t) {
    uint64_t k = key_of(t, -1);
    if (x.blockset.count(k)) x.o.f("    goto %s;\n", label(t).c_str());
    else x.o.f("    return 1;\n");
}

// Emits the semantics of a non-branch instruction. Literal-pool loads from
// ROM-derived memory are folded into constants.
void emit_simple(FnCtx& x, const sh2::Insn& in, const char* indent) {
    if (in.op == sh2::Op::MOVL_PC || in.op == sh2::Op::MOVW_PC) {
        int sp = x.p.space_of(in.addr, -1);
        if (sp >= 0) {
            bool ok = true;
            uint32_t v;
            if (in.op == sh2::Op::MOVL_PC) v = x.p.fetch32(in.addr, -1, &ok);
            else { uint16_t h = 0; ok = x.p.fetch16(in.addr, -1, h); v = uint32_t(int32_t(int16_t(h))); }
            if (ok) {
                x.o.f("%sc->r[%u] = 0x%08Xu;  // literal @%08X\n", indent, in.n, v, in.addr);
                if (x.p.spaces[size_t(sp)].ram) x.ranges.push_back({in.addr, in.op == sh2::Op::MOVL_PC ? 4u : 2u});
                ++x.folded;
                return;
            }
        }
    }
    if (in.op == sh2::Op::NOP) { x.o.f("%s/* nop */\n", indent); return; }
    x.o.f("%s{ [[maybe_unused]] static constexpr sh2::Insn I = %s; sh2::exec_simple(c, I); }\n", indent, insn_init(in).c_str());
}

// ---------------------------------------------------------------------------
// Self-loop recognition (idle-time fast-forward)
//
// Inside one SH-2 time slice nothing else in the machine changes state, so a
// loop whose iterations leave the machine in the same state can be skipped
// to the end of the slice with an exact cycle count. The interpreter still
// executes every iteration; lockstep validation proves equivalence.
// ---------------------------------------------------------------------------
struct SelfLoop {
    enum Kind { NONE, DELAY, POLL } kind = NONE;
    int reg = 0;                     // DELAY: counter register
    int cost = 0;                    // POLL: cycles per iteration
    std::vector<std::string> addrs;  // POLL: load address expressions (runtime purity check)
};

constexpr uint32_t kRegT = 1u << 16, kRegGbr = 1u << 17;

// Register effects of instructions allowed in a polling loop. Returns false
// for anything else (stores, branches, MAC, SR changes, ...).
bool poll_op(const Program& p, const sh2::Insn& in, uint32_t& rd, uint32_t& wr, uint32_t& addr_regs, std::string& addr) {
    using sh2::Op;
    const uint32_t N = 1u << in.n, M = 1u << in.m, R0 = 1u;
    char b[96];
    rd = wr = addr_regs = 0;
    addr.clear();
    switch (in.op) {
    case Op::MOVL_PC: case Op::MOVW_PC:
        wr = N;
        if (p.space_of(in.addr, -1) < 0) { std::snprintf(b, sizeof b, "0x%08Xu", in.addr); addr = b; }
        return true;
    case Op::MOV_I: wr = N; return true;
    case Op::MOV: rd = M; wr = N; return true;
    case Op::MOVB_L: case Op::MOVW_L: case Op::MOVL_L:
        rd = addr_regs = M; wr = N; std::snprintf(b, sizeof b, "c->r[%u]", in.m); addr = b; return true;
    case Op::MOVL_L4:
        rd = addr_regs = M; wr = N; std::snprintf(b, sizeof b, "c->r[%u] + %du", in.m, in.imm); addr = b; return true;
    case Op::MOVB_L4: case Op::MOVW_L4:
        rd = addr_regs = M; wr = R0; std::snprintf(b, sizeof b, "c->r[%u] + %du", in.m, in.imm); addr = b; return true;
    case Op::MOVB_L0: case Op::MOVW_L0: case Op::MOVL_L0:
        rd = addr_regs = M | R0; wr = N; std::snprintf(b, sizeof b, "c->r[%u] + c->r[0]", in.m); addr = b; return true;
    case Op::MOVB_LG: case Op::MOVW_LG: case Op::MOVL_LG:
        rd = addr_regs = kRegGbr; wr = R0; std::snprintf(b, sizeof b, "c->gbr + %du", in.imm); addr = b; return true;
    case Op::TST_B:
        rd = addr_regs = kRegGbr | R0; wr = kRegT; addr = "c->gbr + c->r[0]"; return true;
    case Op::TST: case Op::CMP_EQ: case Op::CMP_HS: case Op::CMP_GE: case Op::CMP_HI: case Op::CMP_GT: case Op::CMP_STR:
        rd = N | M; wr = kRegT; return true;
    case Op::TST_I: case Op::CMP_EQ_I: rd = R0; wr = kRegT; return true;
    case Op::CMP_PZ: case Op::CMP_PL: rd = N; wr = kRegT; return true;
    case Op::EXTU_B: case Op::EXTU_W: case Op::EXTS_B: case Op::EXTS_W: case Op::SWAP_B: case Op::SWAP_W: case Op::NOT:
        rd = M; wr = N; return true;
    case Op::AND: case Op::OR: case Op::XOR: rd = N | M; wr = N; return true;
    case Op::AND_I: case Op::OR_I: case Op::XOR_I: rd = R0; wr = R0; return true;
    case Op::SHLL2: case Op::SHLL8: case Op::SHLL16: case Op::SHLR2: case Op::SHLR8: case Op::SHLR16: rd = N; wr = N; return true;
    case Op::NOP: return true;
    default: return false;
    }
}

SelfLoop analyze_self_loop(const Program& p, const Block& b) {
    SelfLoop L;
    if (b.insns.size() < 2) return L;
    sh2::Insn last;
    p.decode_sh2(b.insns.back(), last);
    if (!(last.op == sh2::Op::BT || last.op == sh2::Op::BF) || last.addr != b.start) return L;
    if (b.insns.size() == 2) {
        sh2::Insn d;
        p.decode_sh2(b.insns[0], d);
        if (d.op == sh2::Op::DT && last.op == sh2::Op::BF) { L.kind = SelfLoop::DELAY; L.reg = d.n; return L; }
    }
    // Poll loop: no loop-carried state (every register/T read before being
    // written in the iteration must not be written anywhere in the loop).
    std::vector<sh2::Insn> body(b.insns.size() - 1);
    uint32_t all_wr = 0;
    std::vector<uint32_t> rds, wrs, ars;
    std::vector<std::string> exprs;
    int cost = int(last.cycles);
    for (size_t i = 0; i + 1 < b.insns.size(); ++i) {
        p.decode_sh2(b.insns[i], body[i]);
        uint32_t rd, wr, ar;
        std::string ex;
        if (!poll_op(p, body[i], rd, wr, ar, ex)) return L;
        rds.push_back(rd); wrs.push_back(wr); ars.push_back(ar); exprs.push_back(ex);
        all_wr |= wr;
        cost += body[i].cycles;
    }
    uint32_t written = 0;
    for (size_t i = 0; i < body.size(); ++i) {
        if (rds[i] & ~written & all_wr) return L;          // loop-carried value
        uint32_t later = 0;
        for (size_t j = i; j < body.size(); ++j) later |= wrs[j];
        if (ars[i] & later) return L;                      // address register changes after the load
        written |= wrs[i];
    }
    if (!(written & kRegT)) return L;                      // the branch must test a fresh T
    L.kind = SelfLoop::POLL;
    L.cost = cost;
    for (auto& e : exprs) if (!e.empty()) L.addrs.push_back(e);
    return L;
}

void emit_branch(FnCtx& x, const sh2::Insn& in, const SelfLoop* loop = nullptr) {
    Out& o = x.o;
    o.f("  { [[maybe_unused]] static constexpr sh2::Insn I = %s;\n", insn_init(in).c_str());
    o.f("    c->cycles -= %u;\n", in.cycles);
    o.f("    uint32_t t_ = sh2::branch_target(c, I);\n");
    if (in.op == sh2::Op::BT || in.op == sh2::Op::BF || in.op == sh2::Op::BT_S || in.op == sh2::Op::BF_S)
        o.f("    c->cycles += sh2::branch_cycle_adjust(I, t_);\n");
    if (in.flags & sh2::IF_DELAYED) {
        sh2::Insn slot;
        x.p.decode_sh2(in.pc + 2, slot);
        o.f("    // slot %08X: %s\n", slot.pc, sh2::disassemble(slot).c_str());
        o.f("    c->cycles -= %u;\n", slot.cycles);
        emit_simple(x, slot, "    ");
    }
    o.f("    c->pc = t_; SH2_BOUNDARY();\n");
    using sh2::Op;
    switch (in.op) {
    case Op::BRA:
        jump(x, in.addr);
        break;
    case Op::BT: case Op::BF: case Op::BT_S: case Op::BF_S: {
        uint32_t fall = in.pc + ((in.flags & sh2::IF_DELAYED) ? 4 : 2);
        o.f("    if (t_ == 0x%08Xu) {\n", in.addr);
        if (loop && loop->kind == SelfLoop::POLL) {
            // Idempotent polling loop: the remaining iterations of this slice
            // would all read the same values; charge their cycles at once.
            std::string cond = "true";
            for (const auto& a : loop->addrs) cond += " && ::recomp::sh2_pure_load(" + a + ")";
            o.f("      if (%s) { int32_t n_ = (c->cycles + %d) / %d; c->cycles -= n_ * %d; return 0; }\n", cond.c_str(),
                loop->cost - 1, loop->cost, loop->cost);
            ++x.fast_loops;
        }
        jump(x, in.addr);
        o.f("    }\n");
        jump(x, fall);
        break;
    }
    case Op::BSR: case Op::JSR: case Op::BSRF: {
        uint32_t ret = in.pc + 4;
        std::string callee;
        if (in.op == Op::BSR && x.p.functions.count(key_of(in.addr, -1))) callee = fn_name(Cpu::SH2, key_of(in.addr, -1));
        o.f("    {\n");
        if (!callee.empty()) {
            o.f("      if (::recomp::g_depth >= ::recomp::kMaxDepth) return 1;\n");
            o.f("      ++::recomp::g_depth; int r_ = %s(c); --::recomp::g_depth;\n", callee.c_str());
        } else {
            o.f("      int r_ = ::recomp::sh2_call_dynamic(c);\n");
        }
        o.f("      if (!r_ || c->pc != 0x%08Xu) return r_;\n    }\n", ret);
        jump(x, ret);
        break;
    }
    default:  // RTS, RTE, JMP, BRAF, TRAPA, invalid: dynamic target
        o.f("    return 1;\n");
        break;
    }
    o.f("  }\n");
}

} // namespace

void emit_sh2(const Program& p, const EmitOptions& opt, EmitResult& out) {
    auto owner = block_owner(p);
    std::vector<uint64_t> fkeys;
    for (const auto& [k, f] : p.functions) fkeys.push_back(k);
    for (const auto& [k, f] : p.functions) out.decls.push_back("int " + fn_name(Cpu::SH2, k) + "(sh2::State* c);");
    std::map<uint64_t, std::pair<uint32_t, uint32_t>> fn_ranges;  // function -> (begin, count)

    const size_t per_file = (fkeys.size() + kSh2Shards - 1) / kSh2Shards;
    for (size_t file_index = 0; file_index < kSh2Shards; ++file_index) {
        const size_t start = file_index * per_file;
        char fname[64];
        std::snprintf(fname, sizeof fname, "recomp_sh2_%03zu.cpp", file_index);
        std::string body;
        body += "// Generated by chaotix_recomp from the user's ROM. Do not edit; do not commit.\n";
        body += "#include \"recomp_decls.h\"\n\nnamespace recomp {\n\n";
        for (size_t i = start; i < fkeys.size() && i < start + per_file; ++i) {
            const Function& f = p.functions.at(fkeys[i]);
            bool ram = f.space >= 0 && p.spaces[size_t(f.space)].ram;
            FnCtx x{p, f, std::set<uint64_t>(f.blocks.begin(), f.blocks.end()), ram, {}, 0, 0, {}};
            x.o.f("int %s(sh2::State* c) {\n", fn_name(Cpu::SH2, f.entry).c_str());
            x.o.f("  switch (c->pc) {\n");
            for (uint64_t b : f.blocks) x.o.f("  case 0x%08Xu: goto %s;\n", key_addr(b), label(key_addr(b)).c_str());
            x.o.f("  default: return 1;\n  }\n");
            for (uint64_t bk : f.blocks) {
                const Block& b = p.blocks.at(bk);
                if (ram) x.ranges.push_back({b.start, b.end - b.start});
                x.o.f("%s:\n", label(b.start).c_str());
                const SelfLoop loop = analyze_self_loop(p, b);
                if (loop.kind == SelfLoop::DELAY) {
                    // Counted delay loop `dt rN ; bf self` (4 cycles per taken
                    // iteration): skip whole iterations in closed form, stopping
                    // exactly where the interpreter's slice would end.
                    x.o.f("  { uint32_t r_ = c->r[%d];\n", loop.reg);
                    x.o.f("    if (r_ > 1 && c->cycles > 0 && !(c->irq_level > ((c->sr >> 4) & 15))) {\n");
                    x.o.f("      uint32_t can_ = (uint32_t(c->cycles) + 3u) / 4u, k_ = r_ - 1 < can_ ? r_ - 1 : can_;\n");
                    x.o.f("      c->r[%d] = r_ - k_; c->cycles -= int32_t(4u * k_); c->sr &= ~sh2::SR_T;\n", loop.reg);
                    x.o.f("      if (k_ == can_) { c->pc = 0x%08Xu; return 0; }\n    } }\n", b.start);
                    ++x.fast_loops;
                }
                for (size_t n = 0; n < b.insns.size(); ++n) {
                    sh2::Insn in;
                    p.decode_sh2(b.insns[n], in);
                    ++out.instructions;
                    x.o.f("  // %08X: %s\n", in.pc, sh2::disassemble(in).c_str());
                    if (in.flags & sh2::IF_BRANCH) { emit_branch(x, in, n + 1 == b.insns.size() ? &loop : nullptr); continue; }
                    x.o.f("  c->cycles -= %u;\n", in.cycles);
                    if (sh2::ends_block(in)) {
                        x.o.f("  c->pc = 0x%08Xu;\n", in.pc + 2);
                        emit_simple(x, in, "  ");
                        x.o.f("  SH2_BOUNDARY();\n");
                        jump(x, in.pc + 2);
                        continue;
                    }
                    emit_simple(x, in, "  ");
                    if (n + 1 == b.insns.size()) {
                        x.o.f("  c->pc = 0x%08Xu;\n", b.end);
                        jump(x, b.end);
                    }
                }
                if (b.insns.empty()) x.o.f("  return 1;\n");
            }
            x.o.f("}\n\n");
            body += x.o.s;
            out.folded_literals += x.folded;
            out.fast_loops += x.fast_loops;
            ++out.functions;
            // Merge validation ranges.
            std::sort(x.ranges.begin(), x.ranges.end());
            std::vector<std::pair<uint32_t, uint32_t>> merged;
            for (auto r : x.ranges) {
                if (!merged.empty() && r.first <= merged.back().first + merged.back().second) {
                    uint32_t end = std::max(merged.back().first + merged.back().second, r.first + r.second);
                    merged.back().second = end - merged.back().first;
                } else merged.push_back(r);
            }
            uint32_t begin = uint32_t(out.ranges.size());
            for (auto r : merged) {
                int sp = p.space_of(r.first, -1);
                out.ranges.push_back({r.first, r.second, sp >= 0 ? p.spaces[size_t(sp)].to_rom(r.first) : 0});
            }
            fn_ranges[f.entry] = {begin, uint32_t(merged.size())};
        }
        body += "} // namespace recomp\n";
        FILE* fp = std::fopen((opt.out_dir + "/" + fname).c_str(), "w");
        std::fwrite(body.data(), 1, body.size(), fp);
        std::fclose(fp);
        out.files.push_back(fname);
    }
    for (const auto& [bk, fk] : owner) {
        auto r = fn_ranges[fk];
        out.table.push_back({key_addr(bk), fn_name(Cpu::SH2, fk), r.first, r.second});
    }
}

} // namespace chaotix::recompiler
