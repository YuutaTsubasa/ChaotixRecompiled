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

namespace {

// An attempt: begun while the clock reads zero, spoiled by a ring lost,
// counted at the goal.
const char* kAttempt = R"(
[vars]
mode  = word FFDFDE
rings = word FFE008
clock = word FFE010
goal  = byte FFFDC5

[rules]
require = mode != 0x08

[achievement Clean Run]
id = clean
icon = Goal
points = 30
start = clock == 0
fail = rings < prev(rings)
when = goal == 0xFF and prev(goal) != 0xFF
)";

struct Level {
    FakeRam ram;
    Tracker t;
    std::vector<std::string> unlocked;
    Level() {
        std::string err;
        CHECK(t.load_definitions_text(kAttempt, &err));
        ram.word(0xDFDE, 0x0018);
    }
    void frame(uint16_t rings, uint16_t clock, uint8_t goal) {
        ram.word(0xE008, rings);
        ram.word(0xE010, clock);
        ram.bytes[0xFDC5] = goal;
        t.update_memory(ram.bytes.data(), ram.bytes.size(),
                        [&](const Achievement& a) { unlocked.push_back(a.id); });
    }
};

} // namespace

TEST(achievements, attempt_counts_a_clean_run) {
    Level l;
    CHECK(l.t.list()[0].icon == "goal");
    l.frame(30, 900, 0);       // the end of the last level: no attempt yet
    l.frame(0, 0, 0);          // a new level: the count drops and the clock is zero
    l.frame(0, 1, 0);
    l.frame(5, 60, 0);
    l.frame(12, 120, 0);
    CHECK_EQ(int(l.unlocked.size()), 0);
    l.frame(12, 121, 0xFF);    // the goal
    CHECK_EQ(int(l.unlocked.size()), 1);
}

TEST(achievements, attempt_is_spoiled_by_a_lost_ring) {
    Level l;
    l.frame(0, 0, 0);
    l.frame(0, 0, 0);
    l.frame(10, 60, 0);
    l.frame(0, 61, 0);         // hit: the rings scatter
    l.frame(4, 90, 0);
    l.frame(4, 91, 0xFF);
    CHECK_EQ(int(l.unlocked.size()), 0);
    // The next level begins a new attempt.
    l.frame(4, 91, 0xFF);
    l.frame(0, 0, 0);
    l.frame(3, 40, 0);
    l.frame(3, 41, 0xFF);
    CHECK_EQ(int(l.unlocked.size()), 1);
}

TEST(achievements, attempt_needs_a_start) {
    // Reaching the goal without having been seen to start the level (a save
    // state loaded in the middle of it) does not count.
    Level l;
    l.frame(10, 500, 0);
    l.frame(11, 501, 0);
    l.frame(11, 502, 0xFF);
    CHECK_EQ(int(l.unlocked.size()), 0);
    l.frame(0, 0, 0);
    l.frame(0, 0, 0);
    l.frame(2, 30, 0);
    l.t.forget_previous();     // a save state loaded: whatever happened is unknown
    l.frame(2, 31, 0);
    l.frame(2, 32, 0xFF);
    CHECK_EQ(int(l.unlocked.size()), 0);
}

TEST(achievements, fail_without_start_is_rejected) {
    Tracker t;
    std::string err;
    CHECK(!t.load_definitions_text("[vars]\nrings = word FFE008\n[achievement X]\nfail = rings < prev(rings)\n"
                                   "when = rings >= 40\n", &err));
    CHECK(err.find("without 'start'") != std::string::npos);
}

#ifdef CHAOTIX_ACHIEVEMENTS_INI
namespace {

// The shipped definitions, fed a level's worth of memory.
struct Shipped {
    FakeRam ram;
    Tracker t;
    std::vector<std::string> unlocked;
    Shipped() {
        std::string err;
        CHECK(t.load_definitions(CHAOTIX_ACHIEVEMENTS_INI, &err));
        CHECK(err.empty());
        ram.word(0xDFDE, 0x0018);  // a level
        ram.word(0xDFF2, 7);       // the lobby
        ram.word(0xFFFDD0, 0xFFFF);
        ram.word(0xE038, 8);       // Knuckles
    }
    void frame() {
        t.update_memory(ram.bytes.data(), ram.bytes.size(), [&](const Achievement& a) { unlocked.push_back(a.id); });
    }
    bool got(const std::string& id) const {
        for (const std::string& u : unlocked)
            if (u == id) return true;
        return false;
    }
};

} // namespace

TEST(achievements, shipped_definitions_are_sound) {
    Shipped s;
    const auto& all = s.t.list();
    // Issue #95: 16 that came for nothing went, 6 that take some doing came.
    CHECK_EQ(int(all.size()), 28);
    for (size_t i = 0; i < all.size(); ++i) {
        // Every id is its own: it is what a save file keeps.
        for (size_t j = i + 1; j < all.size(); ++j) CHECK(all[i].id != all[j].id);
        CHECK(!all[i].description.empty());
        CHECK(all[i].points > 0);
        // The titles are for the game's own letters: upper case, digits and
        // the few marks it has.
        for (char c : all[i].title)
            CHECK((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || std::strchr(" !?-,.'", c));
    }
    // The ids players' save files already hold are still there, but for the
    // ones issue #95 took out: entering a place, playing as someone, the
    // first ring and the claw, all earned by just playing on.
    auto has = [&all](const char* id) {
        for (const auto& a : all)
            if (a.id == id) return true;
        return false;
    };
    for (const char* id : {"rings_25", "rings_50", "rings_100", "rings_no_loss", "level_3", "level_5", "two_players"})
        CHECK(has(id));
    for (const char* id : {"first_ring", "zone_botanic_base", "zone_training", "zone_isolated_island", "play_knuckles",
                           "play_espio", "combi_catcher"})
        CHECK(!has(id));
    for (const char* id : {"clear_every_attraction", "clear_as_everyone", "clear_with_everyone", "clear_30s",
                           "clear_100_rings", "ending"})
        CHECK(has(id));
}

TEST(achievements, shipped_attractions_count_their_own_progress) {
    // Each attraction's progress byte (FFDFFE + place) reaching 6 is that
    // attraction cleared, and only that one. Botanic Base's is also earned by
    // the game itself in achievements_attraction_cleared (tests/CMakeLists.txt).
    const char* ids[5] = {"done_botanic_base", "done_speed_slider", "done_amazing_arena", "done_techno_tower",
                          "done_marina_madness"};
    for (int place = 0; place < 5; ++place) {
        Shipped s;
        s.frame();
        s.frame();
        s.ram.bytes[0xDFFE + place] = 5;  // up to its fifth level
        s.frame();
        for (const char* id : ids) CHECK(!s.got(id));
        s.ram.bytes[0xDFFE + place] = 6;  // and past it
        s.frame();
        for (int other = 0; other < 5; ++other) CHECK_EQ(s.got(ids[other]), other == place);
    }
}

TEST(achievements, shipped_bonus_stage_is_not_the_ending) {
    // The bonus stage loads itself as WORLD ENTRANCE 3 straight from a level;
    // the ending's first scene is the same number, reached from the final
    // battle. Only the first is the bonus stage.
    Shipped s;
    s.ram.word(0xDFF2, 7);
    s.ram.word(0xDFF4, 2);
    s.frame();
    s.frame();
    CHECK(s.got("final_battle"));
    s.ram.word(0xDFF4, 3);
    s.frame();
    CHECK(!s.got("bonus_stage"));
    CHECK(!s.got("level_3"));  // nor is it an attraction's third level

    Shipped b;
    b.ram.word(0xDFF2, 2);
    b.ram.word(0xDFF4, 1);
    b.frame();
    b.frame();
    b.ram.word(0xDFF2, 7);
    b.ram.word(0xDFF4, 3);
    b.frame();
    CHECK(b.got("bonus_stage"));
    CHECK(!b.got("level_3"));
    CHECK(!b.got("final_battle"));
}

namespace {
// The goal of a level of `place`, played as `player` with `partner`.
void reach_goal(Shipped& s, int place, int player, int partner) {
    s.ram.word(0xDFF2, uint16_t(place));
    s.ram.word(0xDFF4, 1);
    s.ram.word(0xE038, uint16_t(player));
    s.ram.word(0xE03A, uint16_t(partner));
    s.ram.bytes[0xFDC5] = 0;
    s.frame();
    s.ram.bytes[0xFDC5] = 0xFF;
    s.frame();
}
} // namespace

TEST(achievements, shipped_world_tour_takes_every_attraction) {
    // A goal in each attraction in turn: four earn nothing, the fifth does.
    // The same place twice counts once.
    Shipped s;
    s.frame();
    for (int place : {0, 1, 1, 2, 3}) reach_goal(s, place, 8, 28);
    CHECK(!s.got("clear_every_attraction"));
    reach_goal(s, 4, 8, 28);
    CHECK(s.got("clear_every_attraction"));
}

TEST(achievements, shipped_whole_team_takes_five_leaders) {
    Shipped s;
    s.frame();
    for (int who : {8, 28, 16, 12}) reach_goal(s, 0, who, 24);
    CHECK(!s.got("clear_as_everyone"));
    reach_goal(s, 0, 0, 24);
    CHECK(s.got("clear_as_everyone"));
    CHECK(!s.got("clear_with_everyone"));  // one partner so far
}

TEST(achievements, shipped_sets_last_across_sessions) {
    // Three leaders, saved; the other two in a new session finish the set.
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/chaotix_ach_set.ini";
    {
        Shipped s;
        s.frame();
        for (int who : {8, 28, 16}) reach_goal(s, 0, who, 24);
        CHECK(s.t.take_progress_changed());
        CHECK(!s.t.take_progress_changed());
        CHECK(s.t.save_progress(path));
    }
    Shipped s;
    CHECK(s.t.load_progress(path));
    for (const auto& a : s.t.list())
        if (a.id == "clear_as_everyone") CHECK_EQ(Tracker::seen(a), 3);
    s.frame();
    reach_goal(s, 0, 12, 24);
    CHECK(!s.got("clear_as_everyone"));
    reach_goal(s, 0, 0, 24);
    CHECK(s.got("clear_as_everyone"));
    std::remove(path.c_str());
}

TEST(achievements, removed_awards_stay_in_the_save_file) {
    // An award the definitions no longer have (issue #95 took some out) is
    // not listed, but its line is written back as it was.
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/chaotix_ach_kept.ini";
    {
        FILE* f = std::fopen(path.c_str(), "w");
        CHECK(f != nullptr);
        if (!f) return;
        std::fprintf(f, "rings_25 = 1700000000\nplay_knuckles = 1700000001\n");
        std::fclose(f);
    }
    Shipped s;
    CHECK(s.t.load_progress(path));
    CHECK_EQ(s.t.unlocked_count(), 1);
    CHECK(s.t.save_progress(path));
    std::string text;
    if (FILE* f = std::fopen(path.c_str(), "r")) {
        char buf[512];
        size_t n = std::fread(buf, 1, sizeof buf - 1, f);
        buf[n] = 0;
        text = buf;
        std::fclose(f);
    }
    CHECK(text.find("play_knuckles = 1700000001") != std::string::npos);
    CHECK(text.find("rings_25 = 1700000000") != std::string::npos);
    std::remove(path.c_str());
}

TEST(achievements, shipped_ending_is_not_the_bonus_stage) {
    // The ending's later scenes, WORLD ENTRANCE 4 on; not the bonus stage's 3.
    Shipped b;
    b.ram.word(0xDFF2, 0);
    b.ram.word(0xDFF4, 1);
    b.frame();
    b.ram.word(0xDFF2, 7);
    b.ram.word(0xDFF4, 3);
    b.frame();
    CHECK(!b.got("ending"));
    b.ram.word(0xDFF4, 4);
    b.frame();
    CHECK(b.got("ending"));
}
#endif
