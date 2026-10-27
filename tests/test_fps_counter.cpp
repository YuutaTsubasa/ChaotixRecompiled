// SHOW FPS (issue #54): the counter's text, size and place, without a window.
#include "frontend/fps_counter.h"

#include <string>
#include <vector>

#include "test_framework.h"

using namespace chaotix;

TEST(fps_counter, lines_keep_their_width_whatever_the_numbers) {
    fps_counter::Stats s;
    s.presented_fps = 59.94;
    s.emulated_fps = 60.0;
    s.emu_avg_ms = 4.21;
    s.emu_worst_ms = 9.8;
    auto l = fps_counter::lines(s);
    CHECK_EQ(l.size(), size_t(2));
    CHECK_STR(l[0], "FPS  59.9  EMU  60.0");
    CHECK_STR(l[1], "MS    4.2  MAX   9.8");
    // Slow, huge and nonsense values keep the same width.
    s.presented_fps = 7.5;
    s.emu_avg_ms = 123.4;
    s.emu_worst_ms = 1234567.0;
    s.emulated_fps = -3;
    l = fps_counter::lines(s);
    CHECK_STR(l[0], "FPS   7.5  EMU   0.0");
    CHECK_STR(l[1], "MS  123.4  MAX 99999");
    for (const std::string& line : l) CHECK_EQ(int(line.size()), fps_counter::kColumns);
    // The interpreter is marked on a line of its own.
    s.interpreter = true;
    l = fps_counter::lines(s);
    CHECK_EQ(l.size(), size_t(3));
    CHECK_STR(l[2], "INTERPRETER");
    for (const std::string& line : l) CHECK(int(line.size()) <= fps_counter::kColumns);
    // FRAME SKIP (issue #62): the pictures it left out, while it is on.
    s.interpreter = false;
    s.frame_skip = true;
    s.skipped_fps = 19.96;
    l = fps_counter::lines(s);
    CHECK_EQ(l.size(), size_t(3));
    CHECK_STR(l[2], "SKIPPED   20.0");
    s.interpreter = true;
    l = fps_counter::lines(s);
    CHECK_EQ(l.size(), size_t(4));
    CHECK_STR(l[3], "INTERPRETER");
    for (const std::string& line : l) CHECK(int(line.size()) <= fps_counter::kColumns);
}

TEST(fps_counter, grows_with_the_window_and_the_menu_size_and_fits) {
    // A phone in landscape (2400x1080) and portrait, a desktop window, a
    // small window: whole numbers, larger at each MENU SIZE.
    CHECK_EQ(fps_counter::scale_for(1280, 720, 0), 2);
    CHECK_EQ(fps_counter::scale_for(800, 360, 0), 1);
    CHECK_EQ(fps_counter::scale_for(800, 360, 2), 2);
    CHECK_EQ(fps_counter::scale_for(2400, 1080, 1), 3);
    CHECK_EQ(fps_counter::scale_for(1080, 2400, 1), 3);  // the shorter side, as in landscape
    CHECK(fps_counter::scale_for(2400, 1080, 2) > fps_counter::scale_for(2400, 1080, 1));
    CHECK(fps_counter::scale_for(2400, 1080, 1) >= fps_counter::scale_for(2400, 1080, 0));
    CHECK_EQ(fps_counter::scale_for(100, 50, 2), 1);
    CHECK_EQ(fps_counter::scale_for(0, 0, 0), 1);
    for (int w : {320, 412, 640, 800, 1080, 1920})
        for (int z = 0; z < 3; ++z) {
            const int s = fps_counter::scale_for(w, 2400, z);
            if (s > 1) CHECK(fps_counter::size(3, s).w + float(8 * s) <= float(w));
        }
}

TEST(fps_counter, sits_top_right_inside_the_window_and_clear_of_others) {
    const int s = 2;
    const fps_counter::Box size = fps_counter::size(2, s);
    CHECK_EQ(size.w, float((20 * 8 + 6) * 2));
    CHECK_EQ(size.h, float((2 * 10 - 2 + 6) * 2));
    CHECK(fps_counter::size(3, s).h > size.h);

    fps_counter::Box b = fps_counter::place(1280, 720, 2, s, {});
    CHECK_EQ(b.x + b.w, 1280.0f - 8.0f);
    CHECK_EQ(b.y, 8.0f);
    CHECK_EQ(b.w, size.w);
    CHECK_EQ(b.h, size.h);

    // Below the award counter, then below a speedrun timer under that too;
    // something elsewhere does not move it.
    const fps_counter::Box pill{1200, 10, 70, 30}, timer{1000, 44, 270, 100}, left{0, 0, 300, 300};
    b = fps_counter::place(1280, 720, 2, s, {left, pill});
    CHECK(!fps_counter::overlaps(b, pill));
    CHECK(b.y >= 40.0f);
    b = fps_counter::place(1280, 720, 2, s, {timer, pill, left});
    CHECK(!fps_counter::overlaps(b, pill));
    CHECK(!fps_counter::overlaps(b, timer));
    CHECK(!fps_counter::overlaps(b, left));
    CHECK(b.y >= 144.0f);
    // On whole multiples of the scale.
    CHECK_EQ(float(int(b.x) / s * s), b.x);
    CHECK_EQ(float(int(b.y) / s * s), b.y);

    // Never outside the window, even when it is crowded or tiny.
    const fps_counter::Box wall{0, 0, 1280, 700};
    b = fps_counter::place(1280, 720, 3, s, {wall});
    CHECK(b.y + b.h <= 720.0f);
    CHECK(b.y >= 0.0f);
    b = fps_counter::place(100, 20, 3, 1, {});
    CHECK(b.x >= 0.0f);
    CHECK(b.y >= 0.0f);
}
