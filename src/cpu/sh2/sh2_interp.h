#pragma once
#include "sh2.h"
#include "sh2_decode.h"

namespace sh2 {

// Execute a branch instruction including its delay slot; sets c->pc.
void exec_branch(State* c, const Insn& in);

// Interpret exactly one basic block starting at c->pc.
void interp_block(State* c);

} // namespace sh2
