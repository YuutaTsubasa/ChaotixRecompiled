// The SH-2 memory accesses that generated code and the interpreter make
// inline (sh2::rd8..wr32) must do exactly what the machine's bus does
// (Machine::sh2_read / sh2_write) for every address they handle themselves
// (issue #62: the cache data array is now one of them).
#include <cstdint>
#include <cstring>
#include <memory>

#include "cpu/sh2/sh2.h"
#include "runtime/system.h"
#include "test_framework.h"

using namespace chaotix;

namespace {

uint32_t next(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

// An address the SH-2 may use for the cache data array, or near it.
uint32_t pick_address(uint32_t& s) {
    switch (next(s) % 4) {
    case 0: return 0xC0000000u | (next(s) & 0x1FFFFFFFu);    // any mirror of area 6
    case 1: return 0xC0000000u | (next(s) & 0xFFFu);         // the array itself
    case 2: return 0x26000000u | (next(s) & 0x3FFFFu);       // SDRAM (cache-through)
    default: return 0xA0000000u | (next(s) & 0x1FFFFFFFu);   // area 5: neither
    }
}

}  // namespace

// Random reads and writes of every size, through the inline accessors on one
// machine and through the bus on another: the same values read, the same
// memory and the same cache code epoch after each, with the array's first
// and last bytes holding recompiled code (written only through the bus).
TEST(sh2_bus, cache_array_inline_as_the_bus) {
    auto a = std::make_unique<Machine>();
    auto b = std::make_unique<Machine>();
    for (Machine* m : {a.get(), b.get()}) {
        m->cache_code_ranges = {{0x0, 0x10}, {0x7F0, 0x812}, {0xFFE, 0x1000}};
        m->code_ranges_changed();
        m->remap_sh2();
    }
    uint32_t s = 0x9E3779B9u;
    int bad = 0;
    for (int i = 0; i < 400000; ++i) {
        const int cpu = int(next(s) & 1);
        const int size = 1 << (next(s) % 3);
        const uint32_t addr = pick_address(s) & ~uint32_t(size - 1);
        sh2::State* c = &a->sh2[cpu];
        if (next(s) & 1) {
            const uint32_t v = next(s);
            if (size == 1) sh2::wr8(c, addr, v);
            else if (size == 2) sh2::wr16(c, addr, v);
            else sh2::wr32(c, addr, v);
            const uint32_t mask = size == 4 ? 0xFFFFFFFFu : (1u << (8 * size)) - 1;
            b->sh2_write(cpu, addr, v & mask, size);
        } else {
            const uint32_t x = size == 1 ? sh2::rd8(c, addr) : size == 2 ? sh2::rd16(c, addr) : sh2::rd32(c, addr);
            if (x != b->sh2_read(cpu, addr, size)) ++bad;
        }
        if (a->cache_code_epoch[cpu] != b->cache_code_epoch[cpu]) ++bad;
    }
    for (int cpu = 0; cpu < 2; ++cpu)
        if (std::memcmp(a->onchip[cpu].cache_data, b->onchip[cpu].cache_data, 4096) != 0) ++bad;
    if (std::memcmp(a->sdram, b->sdram, sizeof a->sdram) != 0) ++bad;
    CHECK_EQ(bad, 0);
}

// Writes over cache-resident code still reach the bus, which notes them.
TEST(sh2_bus, cache_code_written_inline_moves_the_epoch_on) {
    auto m = std::make_unique<Machine>();
    m->cache_code_ranges = {{0x100, 0x120}};
    m->code_ranges_changed();
    m->remap_sh2();
    const uint32_t e = m->cache_code_epoch[0];
    sh2::wr32(&m->sh2[0], 0xC0000140, 0x12345678);  // its own granule: no code
    CHECK_EQ(m->cache_code_epoch[0], e);
    sh2::wr16(&m->sh2[0], 0xC000011E, 0xABCD);
    CHECK_EQ(m->cache_code_epoch[0], e + 1);
    CHECK_EQ(sh2::rd16(&m->sh2[0], 0xC000011E), 0xABCDu);
    CHECK_EQ(m->cache_code_epoch[1], 1u);
}
