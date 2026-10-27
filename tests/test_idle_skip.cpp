// The polling-loop fast-forward (issue #62, recomp_api.h) skips iterations
// whose reads would return what the last one read. For the 32X VDP status,
// whose H-blank bits follow the reader's time, Machine::sh2_read_stable_to
// says until when that holds: checked here against the reads themselves, at
// every remaining cycle count, around both edges of H-blank.
#include <climits>
#include <cstdint>
#include <memory>

#include "runtime/system.h"
#include "test_framework.h"

using namespace chaotix;

namespace {

// Checks the answer for a read of `a` made at remaining count `at`: every
// read down to it returns the same value, and (unless it holds for the whole
// slice) the next one does not. Returns the number of failures.
int check(Machine& m, uint32_t a, int size, int32_t at, int32_t lowest) {
    auto read = [&](int32_t c) {
        m.sh2[0].cycles = c;
        return m.sh2_read(0, a, size);
    };
    const uint32_t v = read(at);
    const int32_t last = m.sh2_read_stable_to(0, a, size, at);
    int bad = 0;
    if (last > at) return 1;
    for (int32_t c = at; c >= lowest && c >= last; --c)
        if (read(c) != v) ++bad;
    if (last != INT32_MIN && last - 1 >= lowest && read(last - 1) == v) ++bad;
    return bad;
}

} // namespace

TEST(idle_skip, vdp_status_stays_as_read_until_hblank_changes) {
    auto m = std::make_unique<Machine>();
    m->mars.bitmap_mode = 1;
    int bad = 0, cases = 0;
    // Lines in the picture and in V-blank, slices starting before, at and
    // after the line start (the SH-2 clock is the master clock * 3 / 7,
    // rounded), budgets of a slice and more.
    for (int line : {10, 230}) {
        for (uint64_t line_start : {uint64_t(855) * 4 * 100, uint64_t(855) * 4 * 100 + 1, uint64_t(3420) * 262 * 1000 + 5}) {
            for (int64_t skew : {-9, -1, 0, 1, 2, 3, 700, 1100}) {
                for (int32_t budget : {366, 367, 1500}) {
                    m->line = line;
                    m->line_start_mclk = line_start;
                    SliceInfo& s = m->sh2_slice_for_tests(0);
                    s = SliceInfo{true, uint64_t(int64_t(line_start * 3 / 7) + skew), budget};
                    for (int32_t at = budget; at > -40; at -= 7) {
                        for (uint32_t a : {0x2000410Au, 0x0000411Au, 0x200041FAu})
                            bad += check(*m, a, 2, at, -60);
                        bad += check(*m, 0x2000410Bu, 1, at, -60);
                        bad += check(*m, 0x20004108u, 4, at, -60);
                        cases += 5;
                    }
                }
            }
        }
    }
    CHECK(cases > 1000);
    CHECK_EQ(bad, 0);
}

// Writes over recompiled code in SDRAM or the cache data array move the code
// epoch on (the generated code is then checked again), found with one bit per
// byte instead of scanning every range: exactly the writes that overlap a
// range, at every offset and size around their edges.
TEST(idle_skip, code_writes_found_by_bits_as_by_ranges) {
    auto m = std::make_unique<Machine>();
    m->sdram_code_ranges = {{0x100, 0x104}, {0x200, 0x2FF}, {0x3FFF0, 0x40000}};
    m->cache_code_ranges = {{0x0, 0x10}, {0xFFE, 0x1000}};
    m->code_ranges_changed();
    int bad = 0;
    for (int size : {1, 2, 4}) {
        for (uint32_t off = 0; off < 0x40000; off += (off < 0x400 || off >= 0x3FF00) ? 1 : 0x100) {
            if (off % uint32_t(size)) continue;
            bool overlap = false;
            for (const auto& r : m->sdram_code_ranges) overlap = overlap || (off < r.second && off + uint32_t(size) > r.first);
            const uint32_t before = m->sdram_code_epoch;
            m->sh2_write(0, 0x26000000 + off, 0, size);
            if ((m->sdram_code_epoch != before) != overlap) ++bad;
            if (off >= 0x1000) continue;
            overlap = false;
            for (const auto& r : m->cache_code_ranges) overlap = overlap || (off < r.second && off + uint32_t(size) > r.first);
            const uint32_t cbefore = m->cache_code_epoch[1];
            m->sh2_write(1, 0xC0000000 + off, 0, size);
            if ((m->cache_code_epoch[1] != cbefore) != overlap) ++bad;
        }
    }
    CHECK_EQ(bad, 0);
}

TEST(idle_skip, other_reads_are_stable_or_unknown) {
    auto m = std::make_unique<Machine>();
    m->sh2_slice_for_tests(0) = SliceInfo{true, 1000, 366};
    // The other VDP registers do not change in a slice.
    CHECK_EQ(m->sh2_read_stable_to(0, 0x20004100, 2, 100), INT32_MIN);
    CHECK_EQ(m->sh2_read_stable_to(0, 0x20004106, 2, 100), INT32_MIN);
    CHECK_EQ(m->sh2_read_stable_to(0, 0x2000410C, 4, 100), INT32_MIN);
    // Anything else: no promise (the FIFO read has side effects, PWM and the
    // on-chip timers are not covered).
    CHECK_EQ(m->sh2_read_stable_to(0, 0x20004012, 2, 100), 100);
    CHECK_EQ(m->sh2_read_stable_to(0, 0x20004034, 2, 100), 100);
    CHECK_EQ(m->sh2_read_stable_to(0, 0xFFFFFE10, 1, 100), 100);
    // Outside a slice, nothing.
    m->sh2_slice_for_tests(0).running = false;
    CHECK_EQ(m->sh2_read_stable_to(0, 0x2000410A, 2, 100), 100);
}
