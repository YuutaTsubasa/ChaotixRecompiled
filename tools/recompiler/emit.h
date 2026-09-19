#pragma once
#include "rom_analyzer/analysis.h"
#include <array>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace chaotix::recompiler {

// The number of generated translation units is fixed so the build system can
// know the output file names before the recompiler runs.
constexpr size_t kM68kShards = 16;
constexpr size_t kSh2Shards = 8;

struct EmitOptions {
    std::string out_dir;
};

struct TableEntry {
    uint32_t key;         // 68K: pc | (bank+1)<<24 ; SH-2: pc
    std::string fn;
    uint32_t range_begin = 0, range_count = 0;
};

struct EmitResult {
    std::vector<std::string> files;          // generated .cpp files (relative names)
    std::vector<std::string> decls;          // function declarations
    std::vector<TableEntry> table;
    std::vector<std::array<uint32_t, 3>> ranges;  // SH-2 validation ranges
    size_t functions = 0, instructions = 0, folded_literals = 0, fast_loops = 0;
};

// Name of the generated function for an analysis function entry.
std::string fn_name(analysis::Cpu cpu, uint64_t key);
std::string label(uint32_t addr);

void emit_m68k(const analysis::Program& p, const EmitOptions& opt, EmitResult& out);
void emit_sh2(const analysis::Program& p, const EmitOptions& opt, EmitResult& out);

// Chooses, for every block, the function that the dispatcher enters.
std::map<uint64_t, uint64_t> block_owner(const analysis::Program& p);

} // namespace chaotix::recompiler
