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

TEST(input, touch_joystick_follows_the_thumb) {
    // Issue #30: the directions as a joystick. A finger that lands on it keeps
    // it until lifted, steering even off the base; other fingers press only
    // buttons.
    TouchControls t;
    t.layout(1920, 1080, true);
    const TouchButton& s = t.buttons()[0];
    CHECK(s.is_dpad && t.stick());
    CHECK_EQ(t.finger(1, s.cx + s.r * 0.1f, s.cy, true), uint16_t(0));     // resting
    CHECK(t.stick_held());
    CHECK_EQ(t.finger(1, s.cx + s.r * 0.6f, s.cy, false), uint16_t(PAD_RIGHT));
    CHECK_EQ(t.finger(1, s.cx - s.r * 3.0f, s.cy - s.r * 3.0f, false), uint16_t(PAD_LEFT | PAD_UP));
    CHECK(std::fabs(t.knob_dx() * t.knob_dx() + t.knob_dy() * t.knob_dy() - s.r * s.r) < 1.0f);  // at the rim
    // A second finger on a button presses the button, not the stick.
    const TouchButton& c = t.buttons()[1];
    CHECK_EQ(t.finger(2, c.cx, c.cy, true), c.mask);
    CHECK_EQ(t.finger(2, s.cx + s.r * 0.6f, s.cy, false), uint16_t(0));
    t.lift(2);
    CHECK(t.stick_held());
    t.lift(1);
    CHECK(!t.stick_held());
    CHECK(t.knob_dx() == 0 && t.knob_dy() == 0);
    // Landing well away from it does not take it.
    CHECK_EQ(t.finger(3, s.cx + s.r * 3.0f, s.cy, true), uint16_t(0));
    CHECK(!t.stick_held());
    // The d-pad layout still works as before.
    t.layout(1920, 1080, false);
    CHECK_EQ(t.finger(4, s.cx + s.r * 0.8f, s.cy, true), uint16_t(PAD_RIGHT));
}

TEST(input, touch_sonic_cd_layout) {
    // Issue #34: Sonic CD's mobile layout. The directions, one big jump (C),
    // HOLD (B) and CALL (A), Start; no X, Y or Z, which this game ignores; and
    // the rest of the lower right jumps. (And the menu button, issue #72.)
    TouchControls t;
    t.layout(1920, 1080, false, true);
    uint16_t all = 0;
    for (const auto& b : t.buttons()) all |= b.mask;
    CHECK_EQ(all, uint16_t(PAD_A | PAD_B | PAD_C | PAD_START | TOUCH_MENU));
    const TouchButton& jump = t.buttons()[1];
    CHECK_EQ(jump.mask, uint16_t(PAD_C));
    CHECK(jump.r > t.buttons()[2].r * 2);                       // the big one
    CHECK_EQ(t.hit(jump.cx, jump.cy), uint16_t(PAD_C));
    CHECK_EQ(t.hit(1300, 700), uint16_t(PAD_C));                // empty lower right
    CHECK_EQ(t.hit(t.buttons()[2].cx, t.buttons()[2].cy), uint16_t(PAD_B));
    CHECK_EQ(t.hit(1300, 100), uint16_t(0));                    // not the top
    CHECK_EQ(t.hit(700, 700), uint16_t(0));                     // nor the left half
    // A second finger jumps while the first holds the stick.
    t.layout(1920, 1080, true, true);
    const TouchButton& s = t.buttons()[0];
    CHECK_EQ(t.finger(1, s.cx + s.r * 0.6f, s.cy, true), uint16_t(PAD_RIGHT));
    CHECK_EQ(t.finger(2, 1400, 800, true), uint16_t(PAD_C));
    // The classic layout is unchanged: no jumping from empty space.
    t.layout(1920, 1080, false, false);
    CHECK_EQ(t.hit(1300, 700), uint16_t(0));
}

TEST(input, touch_fast_forward_button) {
    // Issue #45: off unless asked for; top left when it is, clear of every
    // other control in both layouts, and outside Sonic CD's jump area.
    for (bool sonic_cd : {false, true}) {
        for (bool stick : {false, true}) {
            TouchControls t;
            t.layout(1920, 1080, stick, sonic_cd);
            for (const auto& b : t.buttons()) CHECK(b.mask != TOUCH_FAST_FORWARD);
            t.layout(1920, 1080, stick, sonic_cd, true);
            CHECK(t.buttons()[0].is_dpad);  // the joystick still comes first
            const TouchButton* ff = nullptr;
            for (const auto& b : t.buttons())
                if (b.mask == TOUCH_FAST_FORWARD) ff = &b;
            CHECK(ff != nullptr);
            if (!ff) continue;
            CHECK(ff->cx < 1920 * 0.25f && ff->cy < 1080 * 0.25f);
            CHECK_EQ(t.hit(ff->cx, ff->cy), TOUCH_FAST_FORWARD);
            for (const auto& b : t.buttons()) {
                if (&b == ff) continue;
                const float dx = b.cx - ff->cx, dy = b.cy - ff->cy;
                const float reach = (b.is_dpad ? 1.3f : 1.15f) * b.r + 1.15f * ff->r;
                CHECK(dx * dx + dy * dy > reach * reach);
            }
            // A finger elsewhere does not touch it.
            CHECK(!(t.hit(960, 540) & TOUCH_FAST_FORWARD));
        }
    }
}

TEST(input, touch_menu_button) {
    // Issue #72: a phone with no controller needs a way into the menu (BACK TO
    // TITLE, then LEVEL SELECT). Always there, top left after rewind and
    // fast-forward, clear of every other control in every layout, on a
    // landscape and a portrait screen.
    for (auto [w, h] : {std::pair{1920, 1080}, std::pair{2400, 1080}, std::pair{1080, 2400}}) {
        for (bool sonic_cd : {false, true}) {
            for (bool stick : {false, true}) {
                for (bool ff : {false, true}) {
                    for (bool rew : {false, true}) {
                        TouchControls t;
                        t.layout(w, h, stick, sonic_cd, ff, rew);
                        CHECK(t.buttons()[0].is_dpad);
                        const TouchButton* menu = nullptr;
                        for (const auto& b : t.buttons())
                            if (b.mask == TOUCH_MENU) menu = &b;
                        CHECK(menu != nullptr);
                        if (!menu) continue;
                        CHECK(menu->cx < float(w) * 0.5f && menu->cy < float(h) * 0.25f);
                        CHECK_EQ(t.hit(menu->cx, menu->cy), TOUCH_MENU);
                        for (const auto& b : t.buttons()) {
                            if (&b == menu) continue;
                            const float dx = b.cx - menu->cx, dy = b.cy - menu->cy;
                            const float reach = (b.is_dpad ? 1.3f : 1.15f) * b.r + 1.15f * menu->r;
                            CHECK(dx * dx + dy * dy > reach * reach);
                            // Rewind and fast-forward stay where they were.
                            if (b.mask == TOUCH_REWIND || b.mask == TOUCH_FAST_FORWARD) CHECK(b.cx < menu->cx);
                        }
                        CHECK(!(t.hit(float(w) * 0.5f, float(h) * 0.5f) & TOUCH_MENU));
                    }
                }
            }
        }
    }
}
