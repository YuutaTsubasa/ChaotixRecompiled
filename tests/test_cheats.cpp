#include "game/cheats.h"
#include "game/recomp_dispatch.h"
#include "runtime/chaos_forms.h"
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

TEST(cheats, chaos_forms_end_at_once_when_turned_off) {
    // Issue #146 (Codex review): a form must not outlive the setting, or it
    // would run on with awards and records no longer paused. A monitor's
    // timers (under kMark) are the game's and stay.
    auto m = std::make_unique<Machine>();
    auto w16 = [&](uint32_t a, uint16_t v) { m->wram[a] = uint8_t(v >> 8); m->wram[a + 1] = uint8_t(v); };
    auto r16 = [&](uint32_t a) { return uint16_t(m->wram[a] << 8 | m->wram[a + 1]); };
    w16(0xDFDE, 0x18);   // a level
    w16(0xDFF2, 0);      // BOTANIC BASE
    w16(0xE034, 0xAD48); // the player's object
    w16(0xE036, 0xAE08); // the partner's
    w16(0xAD48 + 0x3A, 0xB000);
    w16(0xAE08 + 0x3A, 0xB040);
    w16(0xB000 + 0x32, 0x7000);  // the player in a form
    w16(0xB000 + 0x34, 0x7000);
    w16(0xB040 + 0x32, 0x0300);  // the partner with a monitor's invincibility
    m->chaos_forms = true;
    m->wram[0xE009] = 60;
    chaos_forms::begin_frame(*m);
    CHECK(chaos_forms::active(*m, 0));
    CHECK(chaos_forms::active(*m, 1));  // the partner is held in the form too
    w16(0xB040 + 0x32, 0x0300);         // say the partner only had a monitor's
    m->chaos_forms = false;
    chaos_forms::begin_frame(*m);
    CHECK(!chaos_forms::active(*m, 0));
    // The partner's monitor was running when the form began, so its music is
    // on: the form's timers end at 1, for the game to run them out itself
    // the next frame and ask for the level's music again.
    CHECK_EQ(int(r16(0xB000 + 0x32)), 1);
    CHECK_EQ(int(r16(0xB000 + 0x34)), 1);
    CHECK_EQ(int(r16(0xB040 + 0x32)), 0x0300);

    // No monitor at all: the timers are cleared, and the music left alone.
    // (The game would have run the 1s above out by now; no game runs here.)
    w16(0xB040 + 0x32, 0);
    w16(0xB040 + 0x34, 0);
    w16(0xB000 + 0x32, 0x7000);
    w16(0xB000 + 0x34, 0x7000);
    m->chaos_drain = 0x80;  // as a transformation leaves it
    m->chaos_forms = true;
    chaos_forms::begin_frame(*m);
    CHECK(chaos_forms::active(*m, 1));
    m->chaos_forms = false;
    chaos_forms::begin_frame(*m);
    CHECK_EQ(int(r16(0xB000 + 0x32)), 0);
    CHECK_EQ(int(r16(0xB000 + 0x34)), 0);
    CHECK_EQ(int(r16(0xB040 + 0x32)), 0);
}
