// rom_analyzer: verifies the user's ROM and writes analysis reports
// (memory map, code spaces, function map, call graph, MMIO usage).
//
//   rom_analyzer --rom <file> [--coverage file.cov ...] [--out dir]
//
// Reports are derived from copyrighted data and are written outside the
// repository (default: ./analysis_out).
#include "analysis.h"
#include "runtime/rom.h"
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

using namespace chaotix;
using namespace chaotix::analysis;

namespace {

void write_program_report(const Program& p, const std::string& dir, const char* tag) {
    std::string base = dir + "/" + tag;
    // Code spaces
    {
        FILE* f = std::fopen((base + "_spaces.txt").c_str(), "w");
        for (const auto& s : p.spaces)
            std::fprintf(f, "%-16s runtime %08X-%08X  rom %06X  bank %2d  %s\n", s.name.c_str(), s.base, s.base + s.size - 1,
                         s.rom_offset, s.bank, s.ram ? "RAM copy (validated at runtime)" : "ROM");
        std::fclose(f);
    }
    // Functions
    {
        FILE* f = std::fopen((base + "_functions.txt").c_str(), "w");
        std::fprintf(f, "# entry bank blocks bytes callers\n");
        for (const auto& [k, fn] : p.functions) {
            uint32_t bytes = 0;
            for (uint64_t b : fn.blocks) bytes += p.blocks.at(b).end - p.blocks.at(b).start;
            std::fprintf(f, "%08X %2d %5zu %6u ", key_addr(k), key_bank(k), fn.blocks.size(), bytes);
            int n = 0;
            for (uint64_t c : fn.callers) { if (n++ < 8) std::fprintf(f, " %08X", key_addr(c)); }
            if (n > 8) std::fprintf(f, " ...(+%d)", n - 8);
            std::fprintf(f, "\n");
        }
        std::fclose(f);
    }
    // Hardware register / RAM references
    {
        std::map<uint32_t, std::map<char, int>> by_target;
        for (const auto& r : p.refs) by_target[r.target][r.kind]++;
        FILE* f = std::fopen((base + "_xrefs.txt").c_str(), "w");
        std::fprintf(f, "# target name kinds(count)  -- r=read w=write a=address-constant c=call j=jump\n");
        for (const auto& [t, kinds] : by_target) {
            const char* nm = mmio_name(p.cpu(), t);
            std::fprintf(f, "%08X %-22s", t, nm ? nm : "");
            for (const auto& [k, n] : kinds) std::fprintf(f, " %c:%d", k, n);
            std::fprintf(f, "\n");
        }
        std::fclose(f);
        FILE* g = std::fopen((base + "_mmio.txt").c_str(), "w");
        for (const auto& [t, kinds] : by_target) {
            const char* nm = mmio_name(p.cpu(), t);
            if (!nm) continue;
            int total = 0;
            for (const auto& [k, n] : kinds) total += n;
            std::fprintf(g, "%08X %-22s refs=%d\n", t, nm, total);
        }
        std::fclose(g);
    }
    if (!p.warnings.empty()) {
        FILE* f = std::fopen((base + "_warnings.txt").c_str(), "w");
        for (const auto& w : p.warnings) std::fprintf(f, "%s\n", w.c_str());
        std::fclose(f);
    }
}

// Runtime addresses are not ROM offsets: the 68K sees the cartridge through
// windows, so a fetch goes back through the code spaces to find the bytes.
std::string disasm_one(const Program& p, uint32_t addr, uint32_t* len = nullptr) {
    struct Ctx { const Program* p; };
    Ctx ctx{&p};
    auto fetch = [](void* user, uint32_t a) -> uint16_t {
        const Program& pr = *static_cast<Ctx*>(user)->p;
        for (const CodeSpace& sp : pr.spaces) {
            if (sp.cpu != Cpu::M68K || !sp.contains(a)) continue;
            const uint32_t off = sp.to_rom(a);
            if (off + 1 < pr.rom().data.size()) return pr.rom().read16(off);
        }
        return 0;
    };
    m68k::Insn in;
    if (!m68k::decode(addr, fetch, &ctx, in)) return "???";
    if (len) *len = in.len;
    return m68k::disassemble(in);
}

// Every instruction that touches one of these addresses, with a few
// instructions either side, so the code can be read rather than guessed at.
void report_refs_to(const Program& p, const std::vector<uint32_t>& targets) {


    for (uint32_t t : targets) {
        std::printf("\n=== instructions touching %06X ===\n", t);
        int n = 0;
        for (const DataRef& r : p.refs) {
            if (r.target != t) continue;
            ++n;
            // Name the function it sits in, which is usually the useful handle.
            uint32_t owner = 0;
            for (const auto& [entry, fn] : p.functions)
                if (entry <= r.from && uint32_t(entry) > owner) owner = uint32_t(entry);
            std::printf("\n%c at %06X   (in function %06X)\n", r.kind, r.from, owner);
            // Walk forward from a little before, so the operands line up.
            uint32_t a = r.from;
            for (int back = 0; back < 3 && a > 4; ++back) {
                uint32_t probe = a - 2;
                while (probe > 4 && p.insns.find(probe) == p.insns.end()) probe -= 2;
                if (p.insns.find(probe) == p.insns.end()) break;
                a = probe;
            }
            for (int i = 0; i < 9; ++i) {
                auto it = p.insns.find(a);
                if (it == p.insns.end()) break;
                std::printf("   %s%06X  %s\n", a == r.from ? "->" : "  ", a, disasm_one(p, a).c_str());
                a += it->second.len;
            }
        }
        if (!n) std::printf("  (no references found)\n");
    }
}

// A straight listing from an address, for reading a routine.
void report_disasm(const Program& p, const std::vector<std::pair<uint32_t, int>>& what) {
    for (const auto& [start, count] : what) {
        std::printf("\n=== %06X ===\n", start);
        // Decoded straight from the ROM rather than from the traced set, so
        // that code the analysis never reached can still be read.
        uint32_t a = start;
        for (int i = 0; i < count; ++i) {
            uint32_t len = 0;
            const std::string text = disasm_one(p, a, &len);
            std::printf("   %06X  %s\n", a, text.c_str());
            if (!len) break;
            a += len;
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    std::string rom_path, out_dir = "analysis_out";
    std::vector<std::string> cov_paths;
    // --refs-to: the instructions that touch one address, with the code around
    // each, which is where following a variable back into the game starts.
    std::vector<uint32_t> refs_to;
    // --disasm: a straight listing from one address, for reading a routine.
    std::vector<std::pair<uint32_t, int>> disasm_at_args;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--rom" && i + 1 < argc) rom_path = argv[++i];
        else if (a == "--coverage" && i + 1 < argc) cov_paths.push_back(argv[++i]);
        else if (a == "--out" && i + 1 < argc) out_dir = argv[++i];
        else if (a == "--refs-to" && i + 1 < argc)
            refs_to.push_back(uint32_t(std::strtoul(argv[++i], nullptr, 16)));
        else if (a == "--disasm" && i + 1 < argc) {
            const std::string spec = argv[++i];
            const size_t comma = spec.find(',');
            disasm_at_args.push_back({uint32_t(std::strtoul(spec.c_str(), nullptr, 16)),
                                      comma == std::string::npos ? 40 : std::atoi(spec.c_str() + comma + 1)});
        }
        else { std::fprintf(stderr, "usage: rom_analyzer --rom <file> [--coverage f.cov] [--out dir] [--refs-to <hex addr>] [--disasm <hex addr>[,count]]\n"); return 2; }
    }
    if (rom_path.empty()) { std::fprintf(stderr, "usage: rom_analyzer --rom <file> [--coverage f.cov] [--out dir] [--refs-to <hex addr>] [--disasm <hex addr>[,count]]\n"); return 2; }
    Rom rom;
    std::string err;
    if (!rom.load(rom_path, &err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    std::printf("ROM       : %s\n", rom_path.c_str());
    std::printf("size      : %zu bytes\n", rom.file_size);
    std::printf("sha1      : %s\n", rom.sha1.c_str());
    std::printf("version   : %s%s\n", rom_version_name(rom.version), rom.version == RomVersion::Unknown ? " (UNVERIFIED)" : "");
    std::printf("title     : %.48s\n", rom.domestic_name);
    std::printf("checksum  : header %04X computed %04X %s\n", rom.header_checksum, rom.compute_checksum(),
                rom.header_checksum == rom.compute_checksum() ? "OK" : "MISMATCH");
    const MarsHeader& h = rom.mars;
    std::printf("MARS hdr  : '%s' src %06X dst %06X size %06X  M entry %08X vbr %08X  S entry %08X vbr %08X\n",
                h.module_name, h.source, h.dest, h.size, h.master_entry, h.master_vbr, h.slave_entry, h.slave_vbr);

    std::vector<CoverageEntry> cov;
    for (const auto& c : cov_paths) {
        auto v = load_coverage(c, &err);
        cov.insert(cov.end(), v.begin(), v.end());
    }
    std::filesystem::create_directories(out_dir);
    for (Cpu cpu : {Cpu::M68K, Cpu::SH2}) {
        Program p(rom, cpu);
        p.add_default_spaces();
        p.add_coverage(cov);
        p.analyze();
        if (!refs_to.empty() && cpu == Cpu::M68K) report_refs_to(p, refs_to);
        if (!disasm_at_args.empty() && cpu == Cpu::M68K) report_disasm(p, disasm_at_args);
        size_t bytes = 0;
        for (const auto& [k, b] : p.blocks) bytes += b.end - b.start;
        const char* tag = cpu == Cpu::M68K ? "m68k" : "sh2";
        std::printf("%-5s     : %zu instructions, %zu blocks, %zu functions, %zu code bytes, %zu refs, %zu warnings\n", tag,
                    p.insns.size(), p.blocks.size(), p.functions.size(), bytes, p.refs.size(), p.warnings.size());
        write_program_report(p, out_dir, tag);
    }
    std::printf("reports written to %s/\n", out_dir.c_str());
    return 0;
}
