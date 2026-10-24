// 64-bit clock arithmetic on 32-bit hosts (issue #54).
//
// The scheduler divides 64-bit master clock counts by small constants (7 for
// the 68000, 3/7 for the SH-2s, 1008 for an audio sample) several times per
// slice, a few thousand times a frame. A 64-bit host does each with a
// multiply. A 32-bit one has no 64-bit divide instruction, and for divisors
// the compiler cannot reduce it calls a library routine (__aeabi_uldivmod on
// 32-bit ARM, the armeabi-v7a build), which is slow. div_const<D> gives the
// same quotient, exactly, for every input, as long division in 16-bit digits:
// each step is a 32-bit division by the constant, which the compiler turns
// into a multiply.
#pragma once
#include <cstdint>

namespace chaotix {

// The 32-bit hosts' way, on every host so it can be tested against `/`.
template <uint32_t D>
constexpr uint64_t div_const_digits(uint64_t x) {
    static_assert(D > 0 && D <= 0xFFFFu, "each step must fit in 32 bits");
    const uint32_t hi = uint32_t(x >> 32), lo = uint32_t(x);
    const uint32_t q2 = hi / D;
    uint32_t n = (hi % D) << 16 | lo >> 16;  // < D << 16
    const uint32_t q1 = n / D;
    n = (n % D) << 16 | (lo & 0xFFFFu);
    const uint32_t q0 = n / D;
    return uint64_t(q2) << 32 | uint64_t(q1) << 16 | q0;
}

template <uint32_t D>
constexpr uint64_t div_const(uint64_t x) {
#if UINTPTR_MAX > 0xFFFFFFFFu
    return x / D;
#else
    return div_const_digits<D>(x);
#endif
}

} // namespace chaotix
