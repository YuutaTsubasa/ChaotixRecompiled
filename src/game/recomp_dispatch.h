// Glue between the runtime and the statically recompiled game code.
#pragma once
#include "runtime/system.h"
#include <cstdint>
#include <set>
#include <string>
#include <utility>

namespace chaotix {

struct RecompStatus {
    bool active = false;
    std::string description;
};

// Installs generated-code executors on the machine. If no generated code was
// built (or enable == false) the reference interpreters are used.
RecompStatus install_recompiled_code(Machine& m, bool enable);

void print_recomp_stats(const Machine& m);

struct DispatchSnapshot {
    bool recompiled = false;
    double m68k_native_pct = 0;
    double sh2_native_pct[2] = {0, 0};
};
DispatchSnapshot dispatch_snapshot();

// Records executed basic-block entry points. The recompiler consumes this as
// additional entry points ("trace-guided" control-flow recovery), and RAM
// resident code is related back to its ROM source here.
class CoverageRecorder {
public:
    void attach(Machine& m);
    bool save(const std::string& path) const;
    size_t size() const { return entries_.size(); }

private:
    Machine* m_ = nullptr;
    std::set<std::pair<int, uint64_t>> entries_;  // (cpu, pc | bank << 32)
};

} // namespace chaotix
