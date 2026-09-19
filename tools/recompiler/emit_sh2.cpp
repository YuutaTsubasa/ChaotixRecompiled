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

void emit_branch(FnCtx& x, const sh2::Insn& in) {
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
            FnCtx x{p, f, std::set<uint64_t>(f.blocks.begin(), f.blocks.end()), ram, {}, 0, {}};
            x.o.f("int %s(sh2::State* c) {\n", fn_name(Cpu::SH2, f.entry).c_str());
            x.o.f("  switch (c->pc) {\n");
            for (uint64_t b : f.blocks) x.o.f("  case 0x%08Xu: goto %s;\n", key_addr(b), label(key_addr(b)).c_str());
            x.o.f("  default: return 1;\n  }\n");
            for (uint64_t bk : f.blocks) {
                const Block& b = p.blocks.at(bk);
                if (ram) x.ranges.push_back({b.start, b.end - b.start});
                x.o.f("%s:\n", label(b.start).c_str());
                for (size_t n = 0; n < b.insns.size(); ++n) {
                    sh2::Insn in;
                    p.decode_sh2(b.insns[n], in);
                    ++out.instructions;
                    x.o.f("  // %08X: %s\n", in.pc, sh2::disassemble(in).c_str());
                    if (in.flags & sh2::IF_BRANCH) { emit_branch(x, in); continue; }
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
