// Achievement definitions: parsing and trigger evaluation against a fake
// memory image (no ROM needed).
#include "game/achievements.h"
#include "test_framework.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace chaotix;
using namespace chaotix::achievements;

namespace {

struct FakeRam {
    std::vector<uint8_t> bytes = std::vector<uint8_t>(0x10000, 0);
    void word(uint32_t a, uint16_t v) {
        bytes[a & 0xFFFF] = uint8_t(v >> 8);
        bytes[(a & 0xFFFF) + 1] = uint8_t(v);
    }
};

const char* kDefs = R"(
[vars]
mode  = word FFDFDE
rings = word FFE008

[achievement First Ring]
id = first_ring
description = Collect a ring.
points = 5
when = mode == 0x38 and rings >= 1

[achievement Ring Up]
id = ring_up
points = 10
when = rings > prev(rings)
)";

} // namespace

TEST(achievements, parses_definitions) {
    Tracker t;
    std::string err;
    CHECK(t.load_definitions_text(kDefs, &err));
    CHECK(err.empty());
    CHECK_EQ(int(t.list().size()), 2);
    CHECK(t.list()[0].id == "first_ring");
    CHECK_EQ(t.list()[0].points, 5);
    CHECK_EQ(t.points_total(), 15);
}

TEST(achievements, unlocks_when_conditions_hold) {
    Tracker t;
    std::string err;
    CHECK(t.load_definitions_text(kDefs, &err));
    FakeRam ram;
    std::vector<std::string> unlocked;
    auto note = [&](const Achievement& a) { unlocked.push_back(a.id); };

    // Outside a level: nothing unlocks, even with rings set.
    ram.word(0xDFDE, 0x0008);
    ram.word(0xE008, 10);
    t.update_memory(ram.bytes.data(), ram.bytes.size(), note);
    t.update_memory(ram.bytes.data(), ram.bytes.size(), note);
    CHECK_EQ(int(unlocked.size()), 0);

    // In a level with a ring: the first achievement unlocks, once.
    ram.word(0xDFDE, 0x0038);
    t.update_memory(ram.bytes.data(), ram.bytes.size(), note);
    CHECK_EQ(int(unlocked.size()), 1);
    CHECK(unlocked[0] == "first_ring");
    t.update_memory(ram.bytes.data(), ram.bytes.size(), note);
    CHECK_EQ(int(unlocked.size()), 1);
    CHECK_EQ(t.unlocked_count(), 1);
    CHECK_EQ(t.points_earned(), 5);
}

TEST(achievements, previous_frame_comparison) {
    Tracker t;
    std::string err;
    CHECK(t.load_definitions_text(kDefs, &err));
    FakeRam ram;
    std::vector<std::string> unlocked;
    auto note = [&](const Achievement& a) { unlocked.push_back(a.id); };

    ram.word(0xDFDE, 0x0008);
    ram.word(0xE008, 5);
    t.update_memory(ram.bytes.data(), ram.bytes.size(), note);  // first frame: no history
    t.update_memory(ram.bytes.data(), ram.bytes.size(), note);  // steady: no unlock
    CHECK_EQ(int(unlocked.size()), 0);
    ram.word(0xE008, 6);                                        // rings went up
    t.update_memory(ram.bytes.data(), ram.bytes.size(), note);
    CHECK_EQ(int(unlocked.size()), 1);
    CHECK(unlocked[0] == "ring_up");
}

TEST(achievements, rejects_bad_definitions) {
    Tracker t;
    std::string err;
    CHECK(!t.load_definitions_text("[vars]\nrings = word FFE008\n[achievement X]\nwhen = nope >= 1\n", &err));
    CHECK(!err.empty());
    err.clear();
    CHECK(!t.load_definitions_text("[vars]\nrings = word FFE008\n[achievement X]\nwhen = rings\n", &err));
    CHECK(!err.empty());
}

TEST(achievements, progress_round_trip) {
    Tracker t;
    std::string err;
    CHECK(t.load_definitions_text(kDefs, &err));
    FakeRam ram;
    ram.word(0xDFDE, 0x0038);
    ram.word(0xE008, 3);
    t.update_memory(ram.bytes.data(), ram.bytes.size(), nullptr);
    t.update_memory(ram.bytes.data(), ram.bytes.size(), nullptr);
    CHECK(t.unlocked_count() >= 1);

    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/chaotix_ach_test.ini";
    CHECK(t.save_progress(path));

    Tracker t2;
    CHECK(t2.load_definitions_text(kDefs, &err));
    CHECK(t2.load_progress(path));
    CHECK_EQ(t2.unlocked_count(), t.unlocked_count());
    std::remove(path.c_str());
}
