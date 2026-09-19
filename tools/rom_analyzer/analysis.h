// Static analysis of the Knuckles' Chaotix ROM: code spaces, control-flow
// recovery, function formation and cross references. Shared by rom_analyzer
// (reports) and chaotix_recomp (code generation).
#pragma once
#include "cpu/m68k/m68k_decode.h"
#include "cpu/sh2/sh2_decode.h"
#include "runtime/rom.h"
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace chaotix::analysis {

enum class Cpu { M68K = 0, SH2 = 1 };

// A window of the CPU address space whose bytes come from the ROM.
struct CodeSpace {
    Cpu cpu = Cpu::M68K;
    std::string name;
    uint32_t base = 0, size = 0;  // runtime address range [base, base+size)
    uint32_t rom_offset = 0;      // ROM offset corresponding to `base`
    int bank = -1;                // 68K 0x900000 window: required bank register value
    bool ram = false;             // RAM-backed copy: must be validated at runtime
    bool contains(uint32_t a) const { return a - base < size; }
    uint32_t to_rom(uint32_t a) const { return rom_offset + (a - base); }
};

// Code location key: address plus 68K bank qualifier.
inline uint64_t key_of(uint32_t addr, int bank) { return uint64_t(addr) | (uint64_t(uint32_t(bank + 1)) << 32); }
inline uint32_t key_addr(uint64_t k) { return uint32_t(k); }
inline int key_bank(uint64_t k) { return int(k >> 32) - 1; }

struct InsnInfo {
    uint32_t addr = 0;
    uint8_t len = 2;         // for SH-2 branches with delay slot: 4 (branch + slot)
    uint16_t flags = 0;      // CPU-specific IF_* flags
    bool branch = false, cond = false, call = false, indirect = false, no_fallthrough = false, ends = false;
    bool has_target = false;
    uint32_t target = 0;
    bool invalid = false;
};

struct Block {
    uint64_t key = 0;
    uint32_t start = 0, end = 0;          // [start, end)
    std::vector<uint32_t> insns;          // instruction addresses (SH-2 slots excluded)
    std::vector<uint64_t> succ;           // intra-procedural successors
    std::vector<uint64_t> calls;          // call targets
    bool indirect_exit = false;
};

struct Function {
    uint64_t entry = 0;
    int space = -1;
    std::vector<uint64_t> blocks;         // sorted block keys
    std::set<uint64_t> callers;
};

struct DataRef {
    uint32_t from;      // instruction address
    uint32_t target;    // referenced absolute address
    char kind;          // 'r' read, 'w' write, 'a' address constant, 'c' call, 'j' jump
};

struct CoverageEntry {
    int cpu;            // 0 = 68K, 1 = master SH-2, 2 = slave SH-2
    uint32_t pc;
    int bank;
    int64_t rom_source;
};

class Program {
public:
    Program(const Rom& rom, Cpu cpu);

    Cpu cpu() const { return cpu_; }
    const Rom& rom() const { return rom_; }
    std::vector<CodeSpace> spaces;
    std::map<uint64_t, InsnInfo> insns;
    std::map<uint64_t, Block> blocks;
    std::map<uint64_t, Function> functions;
    std::vector<DataRef> refs;
    std::set<uint64_t> entries;          // function entry points
    std::set<uint64_t> leaders;          // all block starts
    std::vector<std::string> warnings;

    // Setup
    void add_default_spaces();
    void add_coverage(const std::vector<CoverageEntry>& cov);
    void add_entry(uint32_t addr, int bank = -1);

    // Analysis
    void analyze();

    // Helpers
    int space_of(uint32_t addr, int bank) const;
    bool fetch16(uint32_t addr, int bank, uint16_t& out) const;
    uint32_t fetch32(uint32_t addr, int bank, bool* ok = nullptr) const;
    bool decode_m68k(uint32_t addr, int bank, m68k::Insn& out) const;
    bool decode_sh2(uint32_t addr, sh2::Insn& out) const;

private:
    InsnInfo describe(uint32_t addr, int bank) const;
    void explore();
    void form_blocks();
    void form_functions();
    void collect_refs();

    const Rom& rom_;
    Cpu cpu_;
};

std::vector<CoverageEntry> load_coverage(const std::string& path, std::string* err);

// Name of a known hardware register (MMIO) at `addr`, or nullptr.
const char* mmio_name(Cpu cpu, uint32_t addr);

} // namespace chaotix::analysis
