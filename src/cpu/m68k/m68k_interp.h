#pragma once
#include "m68k.h"
#include "m68k_decode.h"

namespace m68k {

// Fetch callback that reads code through the CPU's own bus (user = State*).
uint16_t fetch_bus(void* user, uint32_t addr);

// Interpret exactly one basic block starting at c->pc (see ends_block()).
void interp_block(State* c);

} // namespace m68k
