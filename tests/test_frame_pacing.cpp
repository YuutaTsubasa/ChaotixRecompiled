#include "platform/frame_pacing.h"
#include "test_framework.h"
#include <cmath>

namespace {
constexpr uint64_t frequency = 10000000; // Windows-style 100 ns counter ticks.
constexpr double frame_ticks = double(frequency) * (3420.0 * 262.0) / 53693175.0;

struct Clock {
    uint64_t now = 0, last = 0;
    double accumulator = 0;
    unsigned waits = 0;
    bool early_once = false;
    uint64_t overshoot = 0;
    void pace(bool unthrottled = false) {
        chaotix::pace_frame(accumulator, last, frame_ticks, frequency, unthrottled,
            [&] { return now; }, [&](uint64_t ns) {
                ++waits;
                // Allow a single early wakeup, then advance by whole ticks.
                uint64_t ticks = uint64_t(std::ceil(double(ns) * frequency / 1e9));
                if (early_once) { ticks /= 2; early_once = false; }
                now += ticks + overshoot;
            });
    }
};
}

TEST(frame_pacing, waits_for_a_game_frame) {
    Clock c;
    c.now = 10000; // Frontend work finished long before the next frame.
    c.pace();
    CHECK(c.accumulator >= frame_ticks);
    CHECK(c.accumulator < frame_ticks + 1);
    CHECK_EQ(c.waits, 1u);
    CHECK_EQ(c.last, c.now);
}

TEST(frame_pacing, early_wakeup_still_reaches_deadline) {
    Clock c;
    c.early_once = true;
    c.pace();
    CHECK(c.accumulator >= frame_ticks);
    CHECK_EQ(c.waits, 2u);
}

TEST(frame_pacing, fractional_deadlines_do_not_drift) {
    Clock c;
    for (int i = 0; i < 6000; ++i) {
        c.now += 13000; // 1.3 ms of simulation / render work.
        c.pace();
        c.accumulator -= frame_ticks;
    }
    CHECK(std::abs(double(c.now) - 6000 * frame_ticks) < 1.01);
    CHECK(c.accumulator >= 0 && c.accumulator < 1.01);
}

TEST(frame_pacing, stalls_remain_bounded_including_during_wait) {
    Clock c;
    c.now += frequency * 10;
    c.pace();
    CHECK(c.accumulator == frame_ticks * 8);
    CHECK_EQ(c.waits, 0u);
    c.accumulator = 0;
    c.overshoot = frequency * 10;
    c.pace();
    CHECK(c.accumulator == frame_ticks * 8);
    CHECK_EQ(c.waits, 1u);
}

TEST(frame_pacing, unthrottled_runs_do_not_wait) {
    Clock c;
    c.pace(true); // Fast-forward and automatic tests share this bypass.
    CHECK_EQ(c.waits, 0u);
    CHECK(c.accumulator == 0);
    c.accumulator = -frame_ticks * 64; // Existing automatic-test batching.
    c.pace(true);
    CHECK_EQ(c.waits, 0u);
    c.accumulator = 0; // Fast-forward resets its accumulated time each pass.
    c.now += 13000;
    c.pace();
    CHECK(c.accumulator >= frame_ticks);
    CHECK_EQ(c.waits, 1u);
}
