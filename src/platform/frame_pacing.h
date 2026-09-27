#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace chaotix {

// Counter and delay are supplied by the platform, so the same scheduling
// policy can be tested without relying on a real OS timer.
template<class Counter, class Delay>
uint64_t pace_frame(double& accumulator, uint64_t& last, double frame_ticks,
                    uint64_t frequency, bool unthrottled, Counter counter, Delay delay) {
    for (;;) {
        const uint64_t now = counter();
        // Apply the cap again after waking: suspension can happen in a delay.
        accumulator = std::min(accumulator + double(now - last), frame_ticks * 8);
        last = now;
        if (unthrottled || accumulator >= frame_ticks) return now;

        // Keep the fractional remainder, and round the wait up so a deadline
        // between counter ticks cannot produce another unchanged presentation.
        const auto ns = uint64_t(std::ceil((frame_ticks - accumulator) * 1e9 / double(frequency)));
        delay(ns);
    }
}

} // namespace chaotix
