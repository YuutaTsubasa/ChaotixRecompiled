// Whether two blocks of bytes are equal, for the renderer's caches that are
// checked every line (issue #62). Unlike memcmp it never stops at the first
// difference, so the compiler compares many bytes per instruction, and it is
// inlined: no library call for a few hundred bytes.
#pragma once
#include <cstddef>
#include <cstdint>

namespace chaotix {

inline bool same_bytes(const void* a, const void* b, size_t n) {
    const uint8_t* p = static_cast<const uint8_t*>(a);
    const uint8_t* q = static_cast<const uint8_t*>(b);
    uint8_t diff = 0;
    for (size_t i = 0; i < n; ++i) diff |= uint8_t(p[i] ^ q[i]);
    return diff == 0;
}

} // namespace chaotix
