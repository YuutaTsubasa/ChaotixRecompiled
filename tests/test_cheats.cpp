#include "game/cheats.h"
#include "game/recomp_dispatch.h"
#include "runtime/patches.h"
#include "runtime/system.h"
#include "test_framework.h"
#include <memory>
#include <string>
#include <vector>

using namespace chaotix;

TEST(cheats, chaos_rings_only_raises_the_count) {
    const cheats::Cheat* chaos = nullptr;
    for (const auto& c : cheats::builtin())
        if (std::string(c.key) == "chaosrings") chaos = &c;
    CHECK(chaos != nullptr);
    if (!chaos) return;
    auto m = std::make_unique<Machine>();
    m->wram[0xE01A] = 0;
    m->wram[0xE01B] = 2;
    cheats::apply(*m, chaos->writes);
    CHECK_EQ(int(m->wram[0xE01B]), 6);  // raised to all six
    m->wram[0xE01B] = 9;
    cheats::apply(*m, chaos->writes);
    CHECK_EQ(int(m->wram[0xE01B]), 9);  // never lowered
}

TEST(cheats, parses_action_replay_codes) {
    std::vector<cheats::Write> w;
    CHECK(cheats::parse_codes("FFE009:63", w));
    CHECK_EQ(w.size(), size_t(1));
    CHECK_EQ(w[0].addr, uint16_t(0xE009));
    CHECK_EQ(w[0].value, uint8_t(0x63));
    w.clear();
    CHECK(cheats::parse_codes(" ffffec:FF00 + FFADFC:01,FFAEBC:01 ", w));
    CHECK_EQ(w.size(), size_t(4));  // the word is two bytes
    CHECK_EQ(w[0].addr, uint16_t(0xFFEC));
    CHECK_EQ(w[0].value, uint8_t(0xFF));
    CHECK_EQ(w[1].addr, uint16_t(0xFFED));
    CHECK_EQ(w[1].value, uint8_t(0x00));
}

TEST(cheats, rejects_anything_else_whole) {
    std::vector<cheats::Write> w;
    CHECK(!cheats::parse_codes("", w));
    CHECK(!cheats::parse_codes("SAXT-DTWA", w));  // Game Genie: patches ROM, not supported
    CHECK(!cheats::parse_codes("A15120:01", w));  // not work RAM
    CHECK(!cheats::parse_codes("FFE009:6", w));
    CHECK(!cheats::parse_codes("FFE009:63 + junk", w));
    CHECK(w.empty());
}

TEST(cheats, builtin_keys_are_unique) {
    const auto& list = cheats::builtin();
    for (size_t i = 0; i < list.size(); ++i)
        for (size_t j = i + 1; j < list.size(); ++j) CHECK(std::string(list[i].key) != list[j].key);
}

#ifdef CHAOTIX_TEST_ROM
TEST(cheats, speed_shoes_and_rings_work_with_local_rom) {
    // BOTANIC BASE 1, holding right: speed shoes take the camera further in
    // the same time, and the ring count stays at 99.
    auto run = [](const std::vector<cheats::Write>& w, int* rings) {
        auto m = std::make_unique<Machine>();
        std::string err;
        CHECK(m->load_rom(CHAOTIX_TEST_ROM, &err));
        m->reset();
        install_recompiled_code(*m, true);
        for (int f = 0; f < 1500; ++f) {
            if (f == 300) { m->stage_request.place = 0; m->stage_request.level = 1; m->stage_pending = true; }
            uint16_t pad = (f >= 300 && f < 400 && (f % 24) < 6) ? 0x80 : 0;
            if (f >= 1300) pad = 0x08;  // right
            m->input.pad[0] = pad;
            cheats::apply(*m, w);
            m->run_frame();
        }
        *rings = m->wram[0xE009];
        return patches::camera_x(*m);
    };
    int rings = 0, rings_with = 0;
    const int plain = run({}, &rings);
    std::vector<cheats::Write> speed;
    for (const auto& c : cheats::builtin())
        if (std::string(c.key) == "speed" || std::string(c.key) == "rings") speed.insert(speed.end(), c.writes.begin(), c.writes.end());
    const int fast = run(speed, &rings_with);
    CHECK(fast > plain + 100);
    CHECK_EQ(rings_with, 99);
}
#endif
