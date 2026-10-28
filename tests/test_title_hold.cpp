// The title held under the front end (issue #3, runtime/title_hold.h): which
// scenes count as before PUSH START and what the hold writes. No ROM needed;
// it only reads and writes 68K work RAM. The ROM tests in CMakeLists.txt
// (title_*) play it through, past the point the attract demo would start.
#include "runtime/title_hold.h"

#include "runtime/patches.h"
#include "runtime/system.h"
#include "test_framework.h"

#include <memory>

using namespace chaotix;

namespace {

uint16_t rd16(const Machine& m, uint32_t off) { return uint16_t(m.wram[off] << 8 | m.wram[off + 1]); }
void wr16(Machine& m, uint32_t off, uint16_t v) {
    m.wram[off] = uint8_t(v >> 8);
    m.wram[off + 1] = uint8_t(v);
}

std::unique_ptr<Machine> scene(uint16_t mode, uint16_t counter) {
    auto m = std::make_unique<Machine>();
    wr16(*m, patches::kGameMode, mode);
    wr16(*m, title_hold::kIdleCounter, counter);
    return m;
}

} // namespace

TEST(title_hold, before_push_start_is_the_boot_screens_the_intro_and_the_zoom) {
    for (uint16_t mode : {0x00, 0x04, 0x0C, 0x10, 0x14}) {
        CHECK(title_hold::before_title(*scene(mode, 0)));
        CHECK(!title_hold::on_title(*scene(mode, 0)));
    }
    // The title's mode with its counter at 0: the intro, or the logo zooming.
    CHECK(title_hold::before_title(*scene(0x08, 0)));
    CHECK(!title_hold::on_title(*scene(0x08, 0)));
    // Counting: PUSH START is up.
    CHECK(!title_hold::before_title(*scene(0x08, 1)));
    CHECK(title_hold::on_title(*scene(0x08, 1)));
    // A level, the lobby or the attract demo is neither.
    for (uint16_t mode : {0x18, 0x38}) {
        CHECK(!title_hold::before_title(*scene(mode, 0)));
        CHECK(!title_hold::on_title(*scene(mode, 0x100)));
    }
}

TEST(title_hold, the_counter_stops_where_start_is_taken_and_short_of_the_demo) {
    // Under $40 it counts on: Start there only finishes the logo's zoom.
    for (uint16_t c : {0x00, 0x01, 0x3F, 0x40}) {
        auto m = scene(0x08, c);
        CHECK(!title_hold::apply(*m));
        CHECK_EQ(rd16(*m, title_hold::kIdleCounter), c);
    }
    // Past it, it goes back to $40 before every frame, so it never reaches
    // $400, where the game starts the attract demo.
    for (uint16_t c : {0x41, 0x42, 0x3FE, 0x3FF}) {
        auto m = scene(0x08, c);
        CHECK(title_hold::apply(*m));
        CHECK_EQ(rd16(*m, title_hold::kIdleCounter), title_hold::kHeld);
    }
    CHECK(title_hold::kHeld + 2 < title_hold::kDemoAt);  // two a frame
    CHECK_EQ(title_hold::kHeld, title_hold::kStartFrom);
    // Any other scene keeps the word, which it may use for something else.
    for (uint16_t mode : {0x00, 0x18, 0x38}) {
        auto m = scene(mode, 0x300);
        CHECK(!title_hold::apply(*m));
        CHECK_EQ(rd16(*m, title_hold::kIdleCounter), 0x300);
    }
}
