// The 32-bit hosts' clock division (issue #54, runtime/divide.h) gives the
// quotient `/` gives, for every kind of input, for the scheduler's divisors.
#include "runtime/divide.h"

#include <cstdint>
#include <vector>

#include "runtime/system.h"
#include "test_framework.h"

using namespace chaotix;

namespace {

template <uint32_t D>
int mismatches(const std::vector<uint64_t>& xs) {
    int bad = 0;
    for (uint64_t x : xs) {
        if (div_const_digits<D>(x) != x / D) ++bad;
        if (div_const<D>(x) != x / D) ++bad;
    }
    return bad;
}

} // namespace

TEST(divide, long_division_in_digits_matches_the_divide_operator) {
    std::vector<uint64_t> xs = {0, 1, 6, 7, 8, 1007, 1008, 1009, 0xFFFF, 0x10000, 0xFFFFFFFFull, 0x100000000ull,
                                0x100000006ull, 0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFEull, 0x8000000000000000ull,
                                0x7FFFFFFFFFFFFFFFull, 0x0000FFFF0000FFFFull, 0xFFFF0000FFFF0000ull};
    // Around every multiple of the divisors near the 32-bit boundaries, and
    // a master clock's worth of hours of play (53.7 MHz, three times over).
    for (uint64_t base : {0ull, 0xFFFF0000ull, 0x100000000ull, 0x1FFFFFFFFull, 0xFFFFFFFF00000000ull,
                          53693175ull * 3600 * 10, 53693175ull * 3600 * 10 * 3})
        for (uint64_t d = 0; d < 4096; ++d) xs.push_back(base + d);
    // Pseudo-random values of every size.
    uint64_t r = 0x9E3779B97F4A7C15ull;
    for (int i = 0; i < 200000; ++i) {
        r ^= r << 13;
        r ^= r >> 7;
        r ^= r << 17;
        xs.push_back(r >> (i % 64));
    }
    CHECK_EQ(mismatches<7>(xs), 0);
    CHECK_EQ(mismatches<kMclkPerAudioSample>(xs), 0);
    CHECK_EQ(mismatches<3>(xs), 0);
    CHECK_EQ(mismatches<15>(xs), 0);
    CHECK_EQ(mismatches<1>(xs), 0);
    CHECK_EQ(mismatches<0xFFFF>(xs), 0);
    static_assert(div_const_digits<7>(0xFFFFFFFFFFFFFFFFull) == 0xFFFFFFFFFFFFFFFFull / 7, "");
}
