#include "audio/ym2612.h"
#include "test_framework.h"

using chaotix::Ym2612;

namespace {
void write(Ym2612& ym, int reg, int value) {
    ym.write(0, 0, uint8_t(reg));
    ym.write(0, 1, uint8_t(value));
}

void tone(Ym2612& ym, int algorithm, bool moving = false) {
    // All four operators keyed on, constant envelopes, no feedback/LFO.
    // Zero frequency holds phase constant, isolating connection latency.
    for (int slot : {0, 4, 8, 12}) {
        write(ym, 0x30 + slot, 1);
        write(ym, 0x40 + slot, algorithm == 5 && (slot == 8 || slot == 12) ? 127 : 0);
        write(ym, 0x50 + slot, 31);
        write(ym, 0x60 + slot, 0);
        write(ym, 0x70 + slot, 0);
        write(ym, 0x80 + slot, 15);
    }
    write(ym, 0xb0, algorithm);
    write(ym, 0xb4, 0xc0);
    write(ym, 0xa4, moving ? 0x24 : 0);
    write(ym, 0xa0, moving ? 0x3b : 0);
    write(ym, 0x28, 0xf0);
}

std::vector<int16_t> samples(Ym2612& ym, int n) {
    std::vector<int16_t> out;
    ym.generate(n, out);
    return out;
}
}

TEST(ym2612, intermediate_modulation_arrives_one_sample_later) {
    for (int algorithm : {0, 1, 2, 3, 5}) {
        Ym2612 reference, changed;
        tone(reference, algorithm);
        tone(changed, algorithm);
        samples(reference, 32);
        samples(changed, 32);
        // Algorithms 0-3 delay operator 2 (register slot 8); algorithm 5
        // delays operator 1 on its path to operator 3 (the sole carrier here).
        write(changed, algorithm == 5 ? 0x40 : 0x48, 64);
        auto a = samples(reference, 3), b = samples(changed, 3);
        CHECK_EQ(a[0], b[0]);
        CHECK_EQ(a[1], b[1]);
        CHECK(a[2] != b[2]);
        CHECK_EQ(b[2], b[4]);
    }
}

TEST(ym2612, algorithm_switch_preserves_pending_modulation) {
    for (int bypass : {4, 6, 7}) {
        Ym2612 reference, changed, steady;
        tone(reference, 0); tone(changed, 0); tone(steady, 3);
        samples(reference, 32); samples(changed, 32); samples(steady, 32);
        // These algorithms do not use the intermediate memory. They must
        // neither erase nor overwrite it when operator 2's level changes.
        write(reference, 0xb0, bypass); write(changed, 0xb0, bypass);
        write(changed, 0x48, 64);
        samples(reference, 16); samples(changed, 16);
        write(reference, 0xb0, 3); write(changed, 0xb0, 3);
        auto a = samples(reference, 2), b = samples(changed, 2), c = samples(steady, 2);
        CHECK_EQ(b[0], c[0]);
        CHECK_EQ(a[0], b[0]);
        CHECK(a[2] != b[2]);
    }
}

TEST(ym2612, generation_chunks_preserve_modulation_history) {
    for (int algorithm = 0; algorithm < 8; ++algorithm) {
        Ym2612 whole, split;
        tone(whole, algorithm, true); tone(split, algorithm, true);
        const auto expected = samples(whole, 1024);
        std::vector<int16_t> actual;
        for (int n : {1, 2, 7, 31, 127, 256, 600}) split.generate(n, actual);
        CHECK(expected == actual);
    }
}

TEST(ym2612, reset_clears_modulation_history) {
    for (int algorithm : {0, 1, 2, 3, 5}) {
        Ym2612 fresh, used;
        tone(used, algorithm, true);
        samples(used, 97);
        used.reset();
        tone(fresh, algorithm, true); tone(used, algorithm, true);
        CHECK(samples(fresh, 128) == samples(used, 128));
    }
}
