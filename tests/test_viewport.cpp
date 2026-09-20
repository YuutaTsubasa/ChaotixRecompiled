// Render viewport tests: aspect ratio presets, pillarboxing, integer scaling.
#include "renderer/viewport.h"
#include "input/input.h"
#include "input/touch_controls.h"
#include "test_framework.h"
#include <cmath>

using namespace chaotix;

TEST(viewport, fit_4_3_in_16_9_window_is_pillarboxed) {
    ViewportConfig c;
    c.aspect = AspectMode::R16_9;
    c.scale = ScaleMode::Fit;
    ViewportResult r = compute_viewport(c, 1920, 1080, 320, 224);
    CHECK_EQ(int(r.frame.w), 1920);
    CHECK_EQ(int(r.frame.h), 1080);
    CHECK_EQ(int(r.image.h), 1080);
    CHECK_EQ(int(r.image.w), 1440);  // 4:3 image, not stretched
    CHECK_EQ(int(r.image.x), 240);
}

TEST(viewport, frame_aspect_presets) {
    ViewportConfig c;
    c.scale = ScaleMode::Fit;
    c.aspect = AspectMode::R4_3;
    ViewportResult r = compute_viewport(c, 2560, 1080, 320, 224);  // 21:9-ish window
    CHECK(std::fabs(r.frame.w / r.frame.h - 4.0 / 3.0) < 1e-6);
    c.aspect = AspectMode::R21_9;
    r = compute_viewport(c, 1920, 1080, 320, 224);
    CHECK(std::fabs(r.frame.w / r.frame.h - 64.0 / 27.0) < 1e-6);
    CHECK_EQ(int(r.frame.w), 1920);
    c.aspect = AspectMode::R16_10;
    r = compute_viewport(c, 1920, 1200, 320, 224);
    CHECK_EQ(int(r.frame.h), 1200);
}

TEST(viewport, integer_scaling) {
    ViewportConfig c;
    c.aspect = AspectMode::R4_3;
    c.scale = ScaleMode::Integer;
    ViewportResult r = compute_viewport(c, 1920, 1080, 320, 224);
    CHECK_EQ(int(r.image.h), 224 * 4);  // 896 <= 1080
    CHECK_EQ(int(r.image.w), int(std::lround(896 * 4.0 / 3.0)));
    r = compute_viewport(c, 3840, 2160, 320, 224);
    CHECK_EQ(int(r.image.h), 224 * 9);  // 2016
}

TEST(viewport, stretch_fills_window) {
    ViewportConfig c;
    c.scale = ScaleMode::Stretch;
    ViewportResult r = compute_viewport(c, 1280, 720, 320, 224);
    CHECK_EQ(int(r.image.w), 1280);
    CHECK_EQ(int(r.image.h), 720);
}

TEST(viewport, parse_aspect_strings) {
    AspectMode m;
    double custom = 0;
    CHECK(parse_aspect("16:9", m, custom) && m == AspectMode::R16_9);
    CHECK(parse_aspect("Auto", m, custom) && m == AspectMode::Auto);
    CHECK(parse_aspect("32:9", m, custom) && m == AspectMode::Custom && std::fabs(custom - 32.0 / 9.0) < 1e-9);
    CHECK(!parse_aspect("banana", m, custom));
}

TEST(input, touch_dpad_directions) {
    TouchControls t;
    t.layout(1920, 1080);
    const TouchButton& d = t.buttons()[0];
    CHECK(d.is_dpad);
    CHECK_EQ(t.hit(d.cx + d.r * 0.8f, d.cy), uint16_t(PAD_RIGHT));
    CHECK_EQ(t.hit(d.cx - d.r * 0.6f, d.cy - d.r * 0.6f), uint16_t(PAD_LEFT | PAD_UP));
    CHECK_EQ(t.hit(d.cx, d.cy), uint16_t(0));  // dead zone
}

TEST(viewport, widescreen_extra_from_aspect) {
    CHECK_EQ(widescreen_extra(4.0 / 3.0, 64), 0);
    CHECK_EQ(widescreen_extra(16.0 / 10.0, 64), 32);   // 384 columns
    CHECK_EQ(widescreen_extra(16.0 / 9.0, 64), 53);    // 426 columns
    CHECK_EQ(widescreen_extra(64.0 / 27.0, 64), 64);   // 21:9 capped
    CHECK_EQ(widescreen_extra(1.0, 64), 0);            // narrower than 4:3
    // A widescreen image keeps native pixel shape: 426 of 320 columns is ~16:9.
    CHECK(std::fabs(image_aspect_for_size(426, 320, 224, 224) - 16.0 / 9.0) < 0.005);
}

TEST(viewport, extra_rows_for_tall_frames) {
    CHECK_EQ(widescreen_rows(4.0 / 3.0, 16), 0);
    CHECK_EQ(widescreen_rows(16.0 / 9.0, 16), 0);   // wider than 4:3: columns, not rows
    CHECK_EQ(widescreen_rows(5.0 / 4.0, 16), 14);   // 238 lines
    CHECK_EQ(widescreen_rows(1.0, 16), 16);         // capped
    CHECK(std::fabs(image_aspect_for_size(320, 320, 240, 224) - (4.0 / 3.0) * 224.0 / 240.0) < 1e-9);
}
