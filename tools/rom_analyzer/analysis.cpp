#include "analysis.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <sstream>

namespace chaotix::analysis {

Program::Program(const Rom& rom, Cpu cpu) : rom_(rom), cpu_(cpu) {}

void Program::add_default_spaces() {
    const uint32_t romsize = uint32_t(rom_.file_size);
    if (cpu_ == Cpu::M68K) {
        // Before the adapter is enabled the cartridge is at 0 (boot code).
        // 0x000000-0x0000FF is excluded: after ADEN it is the 32X vector ROM.
        spaces.push_back({Cpu::M68K, "rom_lo", 0x000100, std::min<uint32_t>(0x400000, romsize) - 0x100, 0x100, -1, false});
        spaces.push_back({Cpu::M68K, "rom_880000", 0x880000, std::min<uint32_t>(0x80000, romsize), 0, -1, false});
        for (int b = 0; b < 4; ++b) {
            uint32_t off = uint32_t(b) << 20;
            if (off >= romsize) break;
            spaces.push_back({Cpu::M68K, "rom_bank" + std::to_string(b), 0x900000, std::min<uint32_t>(0x100000, romsize - off), off, b, false});
        }
    } else {
        const MarsHeader& h = rom_.mars;
        uint32_t sd = 0x06000000 | (h.dest & 0x3FFFF);
        spaces.push_back({Cpu::SH2, "sdram_image", sd, h.size, h.source, -1, true});
        spaces.push_back({Cpu::SH2, "sdram_image_ct", sd | 0x20000000, h.size, h.source, -1, true});
        uint32_t rs = std::min<uint32_t>(0x400000, romsize);
        spaces.push_back({Cpu::SH2, "rom", 0x02000000, rs, 0, -1, false});
        spaces.push_back({Cpu::SH2, "rom_ct", 0x22000000, rs, 0, -1, false});
    }
}

int Program::space_of(uint32_t addr, int bank) const {
    for (size_t i = 0; i < spaces.size(); ++i) {
        const CodeSpace& s = spaces[i];
        if (s.contains(addr) && s.bank == bank) return int(i);
    }
    return -1;
}

bool Program::fetch16(uint32_t addr, int bank, uint16_t& out) const {
    int s = space_of(addr, bank);
    if (s < 0) return false;
    uint32_t off = spaces[size_t(s)].to_rom(addr);
    if (off + 1 >= rom_.file_size) return false;
    out = rom_.read16(off);
    return true;
}

uint32_t Program::fetch32(uint32_t addr, int bank, bool* ok) const {
    uint16_t hi = 0, lo = 0;
    bool a = fetch16(addr, bank, hi), b = fetch16(addr + 2, bank, lo);
    if (ok) *ok = a && b;
    return (uint32_t(hi) << 16) | lo;
}

namespace {
struct FetchCtx { const Program* p; int bank; bool ok; };
uint16_t fetch_cb(void* u, uint32_t a) {
    auto* f = static_cast<FetchCtx*>(u);
    uint16_t v = 0;
    if (!f->p->fetch16(a, f->bank, v)) f->ok = false;
    return v;
}
} // namespace

bool Program::decode_m68k(uint32_t addr, int bank, m68k::Insn& out) const {
    FetchCtx ctx{this, bank, true};
    m68k::decode(addr, fetch_cb, &ctx, out);
    return ctx.ok;
}

bool Program::decode_sh2(uint32_t addr, sh2::Insn& out) const {
    uint16_t op = 0;
    if (!fetch16(addr, -1, op)) return false;
    out = sh2::decode(addr, op);
    return true;
}

InsnInfo Program::describe(uint32_t addr, int bank) const {
    InsnInfo i;
    i.addr = addr;
    if (cpu_ == Cpu::M68K) {
        m68k::Insn in;
        if (!decode_m68k(addr, bank, in) || in.op == m68k::Op::INVALID) { i.invalid = true; i.ends = true; return i; }
        i.len = in.len;
        i.flags = in.flags;
        i.branch = (in.flags & m68k::IF_BRANCH) != 0;
        i.cond = (in.flags & m68k::IF_COND) != 0;
        i.call = (in.flags & m68k::IF_CALL) != 0;
        i.indirect = (in.flags & m68k::IF_INDIRECT) != 0;
        i.no_fallthrough = (in.flags & m68k::IF_NO_FALLTHROUGH) != 0;
        i.ends = m68k::ends_block(in);
        if ((i.branch) && !i.indirect) { i.has_target = true; i.target = in.target; }
    } else {
        sh2::Insn in;
        if (!decode_sh2(addr, in) || in.op == sh2::Op::INVALID) { i.invalid = true; i.ends = true; return i; }
        i.flags = in.flags;
        i.branch = (in.flags & sh2::IF_BRANCH) != 0;
        i.cond = (in.flags & sh2::IF_COND) != 0;
        i.call = (in.flags & sh2::IF_CALL) != 0;
        i.indirect = (in.flags & sh2::IF_INDIRECT) != 0;
        i.no_fallthrough = (in.flags & sh2::IF_NO_FALLTHROUGH) != 0;
        i.ends = sh2::ends_block(in);
        i.len = (in.flags & sh2::IF_DELAYED) ? 4 : 2;
        if (i.branch && !i.indirect) { i.has_target = true; i.target = in.addr; }
    }
    return i;
}

void Program::add_entry(uint32_t addr, int bank) {
    uint64_t k = key_of(addr, bank);
    entries.insert(k);
    leaders.insert(k);
}

void Program::add_coverage(const std::vector<CoverageEntry>& cov) {
    for (const auto& e : cov) {
        bool mine = (cpu_ == Cpu::M68K) == (e.cpu == 0);
        if (!mine) continue;
        uint32_t pc = e.pc;
        if (cpu_ == Cpu::M68K) {
            if (pc >= 0xE00000) continue;  // RAM stubs are interpreted
            int bank = (pc >= 0x900000 && pc < 0xA00000) ? e.bank : -1;
            if (space_of(pc, bank) >= 0) leaders.insert(key_of(pc, bank));
            continue;
        }
        if (space_of(pc, -1) < 0 && e.rom_source >= 0) {
            // RAM-resident SH-2 code not covered by the static map: derive a
            // mapping for the containing region from this anchor.
            uint32_t region_base, region_size;
            if ((pc >> 29) == 6) { region_base = pc & 0xFFFFF000; region_size = 0x1000; }
            else { region_base = pc & 0xFFFC0000; region_size = 0x40000; }
            int64_t rom_base = e.rom_source - int64_t(pc - region_base);
            if (rom_base < 0) { region_base += uint32_t(-rom_base); region_size -= uint32_t(-rom_base); rom_base = 0; }
            char nm[64];
            std::snprintf(nm, sizeof nm, "ram_%08X", region_base);
            spaces.push_back({Cpu::SH2, nm, region_base, region_size, uint32_t(rom_base), -1, true});
        }
        if (space_of(pc, -1) >= 0) leaders.insert(key_of(pc, -1));  // dynamic entry; becomes a function only if uncovered
        else warnings.push_back("coverage entry outside known code spaces: " + std::to_string(pc));
    }
}

void Program::explore() {
    std::deque<uint64_t> work(leaders.begin(), leaders.end());
    std::set<uint64_t> walked;
    while (!work.empty()) {
        uint64_t k = work.front();
        work.pop_front();
        if (walked.count(k)) continue;
        walked.insert(k);
        uint32_t addr = key_addr(k);
        int bank = key_bank(k);
        int sp = space_of(addr, bank);
        if (sp < 0) continue;
        // A leader inside an already decoded straight-line run only splits
        // that run; it does not need to be walked again.
        if (insns.count(k)) continue;
        for (;;) {
            uint64_t ck = key_of(addr, bank);
            if (insns.count(ck)) { leaders.insert(ck); break; }  // joined a known path
            InsnInfo info = describe(addr, bank);
            insns[ck] = info;
            if (info.invalid) break;
            auto push = [&](uint32_t t) {
                int tb = bank;
                if (space_of(t, tb) < 0) tb = -1;
                if (space_of(t, tb) < 0) return;
                uint64_t tk = key_of(t, tb);
                leaders.insert(tk);
                work.push_back(tk);
            };
            if (info.call) {
                if (info.has_target) {
                    int tb = space_of(info.target, bank) >= 0 ? bank : -1;
                    if (space_of(info.target, tb) >= 0) entries.insert(key_of(info.target, tb));
                    push(info.target);
                }
                push(addr + info.len);
                break;
            }
            if (info.branch) {
                if (info.has_target) push(info.target);
                if (!info.no_fallthrough) push(addr + info.len);
                break;
            }
            if (info.ends) { push(addr + info.len); break; }
            addr += info.len;
            if (!spaces[size_t(sp)].contains(addr)) break;
        }
    }
}

void Program::form_blocks() {
    blocks.clear();
    for (uint64_t lk : leaders) {
        if (!insns.count(lk)) continue;
        uint32_t addr = key_addr(lk);
        int bank = key_bank(lk);
        Block b;
        b.key = lk;
        b.start = addr;
        for (;;) {
            uint64_t ck = key_of(addr, bank);
            auto it = insns.find(ck);
            if (it == insns.end()) { b.indirect_exit = true; break; }
            if (addr != b.start && leaders.count(ck)) { b.succ.push_back(ck); break; }
            const InsnInfo& info = it->second;
            b.insns.push_back(addr);
            if (info.invalid) { b.indirect_exit = true; addr += 2; break; }
            uint32_t next = addr + info.len;
            auto tkey = [&](uint32_t t) -> int64_t {
                int tb = space_of(t, bank) >= 0 ? bank : -1;
                if (space_of(t, tb) < 0) return -1;
                return int64_t(key_of(t, tb));
            };
            if (info.call) {
                if (info.has_target) { int64_t t = tkey(info.target); if (t >= 0) b.calls.push_back(uint64_t(t)); }
                else b.indirect_exit = true;
                int64_t f = tkey(next); if (f >= 0) b.succ.push_back(uint64_t(f));
                addr = next;
                break;
            }
            if (info.branch) {
                if (info.has_target) { int64_t t = tkey(info.target); if (t >= 0) b.succ.push_back(uint64_t(t)); else b.indirect_exit = true; }
                else b.indirect_exit = true;
                if (!info.no_fallthrough) { int64_t f = tkey(next); if (f >= 0) b.succ.push_back(uint64_t(f)); }
                addr = next;
                break;
            }
            if (info.ends) {
                int64_t f = tkey(next);
                if (f >= 0) b.succ.push_back(uint64_t(f));
                addr = next;
                break;
            }
            addr = next;
        }
        b.end = addr;
        blocks[lk] = b;
    }
}

void Program::form_functions() {
    functions.clear();
    std::set<uint64_t> covered;
    std::vector<uint64_t> order(entries.begin(), entries.end());
    // Leaders that are only reached dynamically (coverage) become entries if no
    // function contains them.
    auto build = [&](uint64_t e) {
        if (!blocks.count(e) || functions.count(e)) return;
        Function f;
        f.entry = e;
        f.space = space_of(key_addr(e), key_bank(e));
        std::set<uint64_t> seen;
        std::deque<uint64_t> q{e};
        while (!q.empty()) {
            uint64_t k = q.front();
            q.pop_front();
            if (seen.count(k) || !blocks.count(k)) continue;
            seen.insert(k);
            for (uint64_t s : blocks[k].succ)
                if (space_of(key_addr(s), key_bank(s)) == f.space) q.push_back(s);
        }
        f.blocks.assign(seen.begin(), seen.end());
        for (uint64_t b : f.blocks) covered.insert(b);
        functions[e] = f;
    };
    for (uint64_t e : order) build(e);
    for (uint64_t lk : leaders)
        if (!covered.count(lk) && blocks.count(lk)) { entries.insert(lk); build(lk); }
    for (auto& [k, f] : functions)
        for (uint64_t b : f.blocks)
            for (uint64_t c : blocks[b].calls)
                if (functions.count(c)) functions[c].callers.insert(k);
}

void Program::collect_refs() {
    refs.clear();
    for (const auto& [k, info] : insns) {
        if (info.invalid) continue;
        uint32_t a = key_addr(k);
        int bank = key_bank(k);
        if (cpu_ == Cpu::M68K) {
            m68k::Insn in;
            if (!decode_m68k(a, bank, in)) continue;
            auto ea_ref = [&](const m68k::Ea& e, char kind) {
                if (e.mode == m68k::EA_ABSW || e.mode == m68k::EA_ABSL) refs.push_back({a, e.value & 0xFFFFFF, kind});
            };
            if (in.op == m68k::Op::LEA || in.op == m68k::Op::PEA) { ea_ref(in.src, 'a'); continue; }
            if (in.op == m68k::Op::JSR || in.op == m68k::Op::BSR) { if (!(in.flags & m68k::IF_INDIRECT)) refs.push_back({a, in.target, 'c'}); continue; }
            if (in.op == m68k::Op::JMP) { if (!(in.flags & m68k::IF_INDIRECT)) refs.push_back({a, in.target, 'j'}); continue; }
            ea_ref(in.src, 'r');
            ea_ref(in.dst, (in.op == m68k::Op::CMP || in.op == m68k::Op::CMPI || in.op == m68k::Op::TST || in.op == m68k::Op::BTST) ? 'r' : 'w');
        } else {
            sh2::Insn in;
            if (!decode_sh2(a, in)) continue;
            if (in.op == sh2::Op::MOVL_PC) {
                bool ok = false;
                uint32_t v = fetch32(in.addr, -1, &ok);
                if (ok) refs.push_back({a, v, 'a'});
            } else if (in.op == sh2::Op::MOVW_PC) {
                uint16_t v = 0;
                if (fetch16(in.addr, -1, v)) refs.push_back({a, uint32_t(int32_t(int16_t(v))), 'a'});
            } else if (in.op == sh2::Op::BSR) {
                refs.push_back({a, in.addr, 'c'});
            }
        }
    }
}

void Program::analyze() {
    // Always-known entry points.
    if (cpu_ == Cpu::M68K) {
        add_entry(rom_.read32(4) & 0xFFFFFF);  // reset (runs before the adapter is enabled)
        // 32X exception jump table in the cartridge (0x880200 + 6*n): JMP abs.l
        for (uint32_t a = 0x880200; a < 0x8803C0; a += 6) {
            m68k::Insn in;
            if (decode_m68k(a, -1, in) && in.op == m68k::Op::JMP) add_entry(a);
        }
    } else {
        const MarsHeader& h = rom_.mars;
        add_entry(h.master_entry);
        add_entry(h.slave_entry);
        for (uint32_t vbr : {h.master_vbr, h.slave_vbr}) {
            for (uint32_t v = 0; v < 128; ++v) {
                bool ok = false;
                uint32_t t = fetch32(vbr + v * 4, -1, &ok);
                if (ok && space_of(t, -1) >= 0 && (t & 1) == 0 && v != 1 && v != 3) add_entry(t);
            }
        }
    }
    explore();
    form_blocks();
    form_functions();
    collect_refs();
    // Warnings for SH-2 delay-slot corner cases (semantics require care).
    if (cpu_ == Cpu::SH2) {
        for (const auto& [k, info] : insns) {
            if (!(info.flags & sh2::IF_DELAYED)) continue;
            sh2::Insn slot;
            if (!decode_sh2(key_addr(k) + 2, slot)) continue;
            char buf[160];
            if (slot.flags & sh2::IF_SLOT_ILLEGAL) {
                std::snprintf(buf, sizeof buf, "illegal slot instruction at %08X", key_addr(k) + 2);
                warnings.push_back(buf);
            }
            if (slot.flags & sh2::IF_PCREL) {
                std::snprintf(buf, sizeof buf, "PC-relative instruction in delay slot at %08X", key_addr(k) + 2);
                warnings.push_back(buf);
            }
        }
    }
}

std::vector<CoverageEntry> load_coverage(const std::string& path, std::string* err) {
    std::vector<CoverageEntry> out;
    std::ifstream f(path);
    if (!f) { if (err) *err = "cannot open " + path; return out; }
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream is(line);
        CoverageEntry e{};
        std::string pc;
        long long src = -1;
        int bank = 0;
        is >> e.cpu >> pc;
        if (!(is >> bank)) bank = 0;
        if (!(is >> src)) src = -1;
        e.pc = uint32_t(std::strtoul(pc.c_str(), nullptr, 16));
        e.bank = bank;
        e.rom_source = src;
        out.push_back(e);
    }
    return out;
}

const char* mmio_name(Cpu cpu, uint32_t a) {
    struct R { uint32_t addr; const char* name; };
    static const R m68k_regs[] = {
        {0xA10001, "IO_VERSION"}, {0xA10003, "IO_DATA1"}, {0xA10005, "IO_DATA2"}, {0xA10007, "IO_DATA3"},
        {0xA10009, "IO_CTRL1"}, {0xA1000B, "IO_CTRL2"}, {0xA1000D, "IO_CTRL3"},
        {0xA11100, "Z80_BUSREQ"}, {0xA11200, "Z80_RESET"}, {0xA130EC, "MARS_ID"}, {0xA130F1, "SRAM_CTRL"},
        {0xA14000, "TMSS"},
        {0xA15100, "MARS_ADAPTER_CTRL"}, {0xA15101, "MARS_ADAPTER_CTRL_L"}, {0xA15102, "MARS_INT_CTRL"}, {0xA15103, "MARS_INT_CTRL_L"},
        {0xA15104, "MARS_BANK"}, {0xA15105, "MARS_BANK_L"}, {0xA15106, "MARS_DREQ_CTRL"}, {0xA15107, "MARS_DREQ_CTRL_L"},
        {0xA15108, "MARS_DREQ_SRC_H"}, {0xA1510A, "MARS_DREQ_SRC_L"}, {0xA1510C, "MARS_DREQ_DST_H"}, {0xA1510E, "MARS_DREQ_DST_L"},
        {0xA15110, "MARS_DREQ_LEN"}, {0xA15112, "MARS_FIFO"}, {0xA1511A, "MARS_SEGA_TV"},
        {0xA15120, "MARS_COMM0"}, {0xA15122, "MARS_COMM2"}, {0xA15124, "MARS_COMM4"}, {0xA15126, "MARS_COMM6"},
        {0xA15128, "MARS_COMM8"}, {0xA1512A, "MARS_COMM10"}, {0xA1512C, "MARS_COMM12"}, {0xA1512E, "MARS_COMM14"},
        {0xA15130, "PWM_CTRL"}, {0xA15132, "PWM_CYCLE"}, {0xA15134, "PWM_LCH"}, {0xA15136, "PWM_RCH"}, {0xA15138, "PWM_MONO"},
        {0xA15180, "VDP32X_MODE"}, {0xA15182, "VDP32X_SHIFT"}, {0xA15184, "VDP32X_FILL_LEN"}, {0xA15186, "VDP32X_FILL_ADDR"},
        {0xA15188, "VDP32X_FILL_DATA"}, {0xA1518A, "VDP32X_FBCTRL"}, {0xA1518B, "VDP32X_FBCTRL_L"},
        {0xC00000, "VDP_DATA"}, {0xC00002, "VDP_DATA"}, {0xC00004, "VDP_CTRL"}, {0xC00006, "VDP_CTRL"}, {0xC00008, "VDP_HV"},
        {0xC00011, "PSG"},
    };
    static const R sh2_regs[] = {
        {0x4000, "SYS_INTMASK"}, {0x4002, "SYS_STANDBY"}, {0x4004, "SYS_HCOUNT"}, {0x4006, "SYS_DREQ_CTRL"},
        {0x4008, "SYS_DREQ_SRC"}, {0x400C, "SYS_DREQ_DST"}, {0x4010, "SYS_DREQ_LEN"}, {0x4012, "SYS_FIFO"},
        {0x4014, "SYS_VRES_CLR"}, {0x4016, "SYS_VINT_CLR"}, {0x4018, "SYS_HINT_CLR"}, {0x401A, "SYS_CMD_CLR"}, {0x401C, "SYS_PWM_CLR"},
        {0x4020, "COMM0"}, {0x4022, "COMM2"}, {0x4024, "COMM4"}, {0x4026, "COMM6"}, {0x4028, "COMM8"}, {0x402A, "COMM10"},
        {0x402C, "COMM12"}, {0x402E, "COMM14"},
        {0x4030, "PWM_CTRL"}, {0x4032, "PWM_CYCLE"}, {0x4034, "PWM_LCH"}, {0x4036, "PWM_RCH"}, {0x4038, "PWM_MONO"},
        {0x4100, "VDP_MODE"}, {0x4102, "VDP_SHIFT"}, {0x4104, "VDP_FILL_LEN"}, {0x4106, "VDP_FILL_ADDR"}, {0x4108, "VDP_FILL_DATA"},
        {0x410A, "VDP_FBCTRL"},
    };
    static const R onchip[] = {
        {0xFFFFFE10, "FRT_TIER"}, {0xFFFFFE11, "FRT_FTCSR"}, {0xFFFFFE12, "FRT_FRC"}, {0xFFFFFE14, "FRT_OCR"},
        {0xFFFFFE16, "FRT_TCR"}, {0xFFFFFE17, "FRT_TOCR"}, {0xFFFFFE60, "INTC_IPRB"}, {0xFFFFFE62, "INTC_VCRA"},
        {0xFFFFFE64, "INTC_VCRB"}, {0xFFFFFE66, "INTC_VCRC"}, {0xFFFFFE68, "INTC_VCRD"}, {0xFFFFFE80, "WDT_WTCSR"},
        {0xFFFFFE91, "SBYCR"}, {0xFFFFFE92, "CCR"}, {0xFFFFFEE0, "INTC_ICR"}, {0xFFFFFEE2, "INTC_IPRA"}, {0xFFFFFEE4, "INTC_VCRWDT"},
        {0xFFFFFF00, "DIVU_DVSR"}, {0xFFFFFF04, "DIVU_DVDNT"}, {0xFFFFFF08, "DIVU_DVCR"}, {0xFFFFFF0C, "DIVU_VCRDIV"},
        {0xFFFFFF10, "DIVU_DVDNTH"}, {0xFFFFFF14, "DIVU_DVDNTL"},
        {0xFFFFFF80, "DMAC_SAR0"}, {0xFFFFFF84, "DMAC_DAR0"}, {0xFFFFFF88, "DMAC_TCR0"}, {0xFFFFFF8C, "DMAC_CHCR0"},
        {0xFFFFFF90, "DMAC_SAR1"}, {0xFFFFFF94, "DMAC_DAR1"}, {0xFFFFFF98, "DMAC_TCR1"}, {0xFFFFFF9C, "DMAC_CHCR1"},
        {0xFFFFFFA0, "DMAC_VCR0"}, {0xFFFFFFA8, "DMAC_VCR1"}, {0xFFFFFFB0, "DMAC_DMAOR"},
        {0xFFFFFFE0, "BSC_BCR1"}, {0xFFFFFFE4, "BSC_BCR2"}, {0xFFFFFFE8, "BSC_WCR"}, {0xFFFFFFEC, "BSC_MCR"},
    };
    if (cpu == Cpu::M68K) {
        a &= 0xFFFFFF;
        for (const auto& r : m68k_regs) if (r.addr == a) return r.name;
        return nullptr;
    }
    for (const auto& r : onchip) if (r.addr == a) return r.name;
    if ((a >> 29) <= 1) {
        uint32_t p = a & 0x1FFFFFFF;
        for (const auto& r : sh2_regs) if (r.addr == p) return r.name;
    }
    return nullptr;
}

} // namespace chaotix::analysis
