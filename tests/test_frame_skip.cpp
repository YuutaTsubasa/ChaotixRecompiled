// Frame skip (issue #62): which pictures are drawn, and that a skipped
// line leaves the VDP's status exactly as drawing it would have.
#include "frontend/frame_skip.h"
#include "runtime/patches.h"
#include "runtime/vdp.h"
#include "test_framework.h"

#include <algorithm>
#include <memory>
#include <vector>

using namespace chaotix;

namespace {

// The pictures drawn of `passes` passes of `count` frames each.
std::vector<bool> drawn(FrameSkip mode, int count, int passes) {
    FrameSkipper s;
    std::vector<bool> out;
    for (int p = 0; p < passes; ++p)
        for (int i = 0; i < count; ++i) out.push_back(s.draw(mode, i, count));
    return out;
}

int longest_skip(const std::vector<bool>& d) {
    int run = 0, worst = 0;
    for (bool x : d) {
        run = x ? 0 : run + 1;
        worst = std::max(worst, run);
    }
    return worst;
}

uint32_t rnd(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

} // namespace

TEST(frame_skip, off_draws_every_frame) {
    for (int count : {1, 2, 3, 8})
        for (bool d : drawn(FrameSkip::Off, count, 10)) CHECK(d);
}

TEST(frame_skip, auto_draws_exactly_the_pictures_that_are_shown) {
    // One frame a pass (the device keeps up): every picture. Several (it
    // fell behind, or fast-forward): only the last, the one presented.
    for (bool d : drawn(FrameSkip::Auto, 1, 30)) CHECK(d);
    for (int count : {2, 3, 4, 8}) {
        const auto d = drawn(FrameSkip::Auto, count, 10);
        for (size_t k = 0; k < d.size(); ++k) CHECK_EQ(d[k], int(k % size_t(count)) == count - 1);
    }
    // Catching up is three frames a pass at most (plan_pass): never more than
    // two pictures in a row left out, so the picture keeps moving.
    CHECK_EQ(longest_skip(drawn(FrameSkip::Auto, 3, 20)), 2);
}

TEST(frame_skip, one_and_two_draw_at_most_every_second_or_third_frame) {
    for (FrameSkip mode : {FrameSkip::One, FrameSkip::Two}) {
        const int n = frame_skip_fixed(mode);
        // Keeping up: exactly one in n + 1, steadily.
        const auto d = drawn(mode, 1, 30);
        for (size_t k = 0; k < d.size(); ++k) CHECK_EQ(d[k], int(k % size_t(n + 1)) == n);
        // Behind: still only the last of a pass, never more than n in a row
        // skipped when a pass is as long as the gap.
        for (int count : {2, 3}) {
            const auto e = drawn(mode, count, 12);
            for (size_t k = 0; k < e.size(); ++k)
                if (e[k]) CHECK_EQ(int(k % size_t(count)), count - 1);
            CHECK(longest_skip(e) <= n + count - 1);
        }
    }
}

TEST(frame_skip, names_round_trip_and_unknown_is_the_default) {
    for (int k = 0; k < kFrameSkipModes; ++k)
        CHECK(parse_frame_skip(frame_skip_name(FrameSkip(k))) == FrameSkip(k));
    CHECK(parse_frame_skip("AUTO") == FrameSkip::Auto);
    CHECK(parse_frame_skip("off") == FrameSkip::Off);
    CHECK(parse_frame_skip("sometimes") == frame_skip_default());
#if defined(__ANDROID__) || (defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    CHECK(frame_skip_default() == FrameSkip::Auto);
#else
    CHECK(frame_skip_default() == FrameSkip::Off);
#endif
}

TEST(frame_skip, a_skipped_line_sets_the_sprite_flags_as_drawing_it_does) {
    // Random sprite tables, crowded onto the same lines so the per-line and
    // pixel limits and collisions all happen, in H32 and H40, the level's
    // plane size (extended ring X) or not, at each widescreen width, on the
    // active lines and the widescreen rows below; a status read (which
    // clears both flags) now and then.
    uint32_t seed = 0x6202u;
    std::vector<uint32_t> rgb(Vdp::kMaxWidth);
    std::vector<uint8_t> bg(Vdp::kMaxWidth);
    int overflows = 0, collisions = 0, compared = 0;
    for (int trial = 0; trial < 160; ++trial) {
        auto drawn_vdp = std::make_unique<Vdp>();
        Vdp& a = *drawn_vdp;
        a.reset();
        for (uint8_t& b : a.vram) b = uint8_t(rnd(seed) % 4 ? 0 : rnd(seed));  // sparse pixels
        a.reg[1] = (trial % 17) ? 0x40 : 0x00;  // now and then the display off
        a.reg[5] = 0x70;  // sprites at E000
        a.reg[12] = uint8_t(trial & 1);
        a.reg[16] = (trial & 2) ? patches::kLevelPlaneSize : 0x11;
        const int band = 8 + int(rnd(seed) % 64);
        for (int i = 0; i < 80; ++i) {
            const uint32_t e = 0xE000 + uint32_t(i) * 8;
            const uint16_t y = uint16_t(128 + band + int(rnd(seed) % 48) - 24);
            const uint16_t size = uint16_t(rnd(seed) & 0x0F00);
            const uint16_t link = uint16_t(i + 1 < 80 && rnd(seed) % 32 ? i + 1 : 0);
            const uint16_t pat = uint16_t(rnd(seed) & 0xF9FF);
            uint16_t x = uint16_t(rnd(seed) % 600);
            if (rnd(seed) % 24 == 0) x = 0;  // masks
            const uint16_t words[4] = {y, uint16_t(size | link), pat, x};
            for (int w = 0; w < 4; ++w) {
                a.vram[e + uint32_t(w) * 2] = uint8_t(words[w] >> 8);
                a.vram[e + uint32_t(w) * 2 + 1] = uint8_t(words[w]);
            }
        }
        auto skipped_vdp = std::make_unique<Vdp>(a);
        Vdp& b = *skipped_vdp;
        static const int kExtras[] = {0, 53, 80, 128};
        const int extra = kExtras[(trial >> 2) % 4];
        for (int line = 0; line < 240; ++line) {
            a.render_line(line, rgb.data(), bg.data(), extra);
            b.sprite_status_line(line, extra);
            CHECK_EQ(a.sprite_overflow, b.sprite_overflow);
            CHECK_EQ(a.sprite_collision, b.sprite_collision);
            overflows += a.sprite_overflow;
            collisions += a.sprite_collision;
            ++compared;
            if (rnd(seed) % 3 == 0) {
                a.read_status(false);
                b.read_status(false);
            }
        }
    }
    // The table really did exercise both flags.
    CHECK(overflows > 100);
    CHECK(collisions > 100);
    CHECK_EQ(compared, 160 * 240);
}
