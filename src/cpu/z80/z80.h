// Zilog Z80 interpreter for the Mega Drive sound CPU. The Z80 program is data
// uploaded by the 68K at runtime (the SMPS sound driver), so it is executed by
// this interpreter as part of the audio compatibility layer.
#pragma once
#include <cstdint>

namespace z80 {

struct Bus {
    void* user = nullptr;
    uint8_t (*read)(void* user, uint16_t addr) = nullptr;
    void (*write)(void* user, uint16_t addr, uint8_t v) = nullptr;
    uint8_t (*in)(void* user, uint16_t port) = nullptr;
    void (*out)(void* user, uint16_t port, uint8_t v) = nullptr;
};

struct State {
    // Main registers (A/F, B/C, D/E, H/L as pairs), alternates, index registers.
    uint8_t a, f, b, c, d, e, h, l;
    uint8_t a2, f2, b2, c2, d2, e2, h2, l2;
    uint16_t ix, iy, sp, pc;
    uint8_t i, r;
    uint8_t iff1, iff2, im;
    uint8_t halted;
    uint8_t ei_pending;   // interrupts are enabled after the instruction following EI
    uint8_t irq_line;     // /INT level (asserted = 1)
    int32_t cycles;       // remaining T-states in the current slice
    Bus bus;
};

void reset(State* s);
// Executes one instruction (or an interrupt acknowledge); returns T-states used.
int step(State* s);
// Runs until the cycle budget is exhausted.
void run(State* s, int32_t cycles);

} // namespace z80
