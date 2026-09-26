// Following a time attack run, and the records it leaves behind. No ROM is
// needed: the run is watched through 68K work RAM, so a Machine with the right
// words written into it is enough.
#include "frontend/time_attack.h"

#include "frontend/config.h"
#include "runtime/stage_select.h"
#include "runtime/system.h"
#include "test_framework.h"

#include <memory>

using namespace chaotix;

namespace {

void wr16(Machine& m, uint32_t off, unsigned v) {
    m.wram[off] = uint8_t(v >> 8);
    m.wram[off + 1] = uint8_t(v);
}
void wr32(Machine& m, uint32_t off, uint32_t v) {
    wr16(m, off, v >> 16);
    wr16(m, off + 2, v & 0xFFFF);
}
// Where the game is, as the run tracking reads it.
void put_scene(Machine& m, unsigned place, unsigned level) {
    wr16(m, 0xDFF2, place);
    wr16(m, 0xDFF4, level);
}

} // namespace

TEST(time_attack, the_clock_starts_when_the_level_does_not_when_it_loads) {
    auto mp = std::make_unique<Machine>();
    Machine& m = *mp;
    stage_select::Request r;
    r.place = 4;
    r.level = 3;

    time_attack::Run run;
    run.begin(r);
    // Still waiting to be taken at the mode dispatcher.
    m.stage_pending = true;
    CHECK(!run.update(m));
    CHECK(!run.started);

    // Taken, and the level's entry sequence is playing: the engine's frame
    // counter is running but the clock has not begun.
    m.stage_pending = false;
    put_scene(m, 4, 3);
    wr32(m, stage_select::kFrameCounter, 900);
    wr16(m, stage_select::kClockStart, 0);
    CHECK(!run.update(m));
    CHECK(run.started);
    CHECK(!run.clock_started);
    CHECK_EQ(run.time, 0);

    // Play begins. The base is taken here, from both readings at once.
    wr32(m, stage_select::kFrameCounter, 1000);
    wr16(m, stage_select::kClockStart, 10);
    CHECK(!run.update(m));
    CHECK(run.clock_started);
    CHECK_EQ(run.time, 10);

    // From now on only the counter matters. The player presses something, so
    // the game puts FFE052 back to 1 -- which must not touch the run's time.
    wr32(m, stage_select::kFrameCounter, 1300);
    wr16(m, stage_select::kClockStart, 1);
    CHECK(!run.update(m));
    CHECK_EQ(run.time, 310);
}

TEST(time_attack, a_run_ends_when_the_game_moves_the_player_on) {
    auto mp = std::make_unique<Machine>();
    Machine& m = *mp;
    stage_select::Request r;
    time_attack::Run run;
    run.begin(r);
    m.stage_pending = false;
    put_scene(m, r.place, r.level);
    wr32(m, stage_select::kFrameCounter, 1000);
    wr16(m, stage_select::kClockStart, 1);
    CHECK(!run.update(m));
    wr32(m, stage_select::kFrameCounter, 1600);
    CHECK(!run.update(m));
    CHECK_EQ(run.time, 601);

    // The game sends the player to its lobby, which is how a run ends however
    // it ended.
    put_scene(m, stage_select::kLobbyPlace, 0);
    CHECK(run.update(m));
    CHECK(!run.running);
    CHECK_EQ(run.time, 601);     // the time it had, not the lobby's
    CHECK(!run.timed_out());
    CHECK(!run.update(m));       // and it only ends once
}

TEST(time_attack, a_run_that_hit_the_levels_own_limit_is_not_a_time) {
    auto mp = std::make_unique<Machine>();
    Machine& m = *mp;
    stage_select::Request r;
    time_attack::Run run;
    run.begin(r);
    m.stage_pending = false;
    put_scene(m, r.place, r.level);
    wr32(m, stage_select::kFrameCounter, 100);
    wr16(m, stage_select::kClockStart, 1);
    run.update(m);
    wr32(m, stage_select::kFrameCounter, 100 + stage_select::kTimeLimit);
    run.update(m);
    put_scene(m, stage_select::kLobbyPlace, 0);
    CHECK(run.update(m));
    CHECK(run.timed_out());
}

TEST(time_attack, records_keep_only_the_quickest) {
    Config cfg;
    cfg.set_defaults();
    CHECK_EQ(best_time(cfg, 4, 3), 0);
    CHECK(record_best(cfg, 4, 3, 2000));
    CHECK_EQ(best_time(cfg, 4, 3), 2000);
    CHECK(!record_best(cfg, 4, 3, 2500));      // slower
    CHECK_EQ(best_time(cfg, 4, 3), 2000);
    CHECK(record_best(cfg, 4, 3, 1500));       // quicker
    CHECK_EQ(best_time(cfg, 4, 3), 1500);
    CHECK_EQ(best_time(cfg, 4, 4), 0);         // and each stage keeps its own
    CHECK(!record_best(cfg, 4, 4, 0));         // nothing to record
}

TEST(time_attack, times_are_written_the_way_the_game_writes_them) {
    // The offset is measured against the HUD; these read the same as the
    // clock the player watched.
    CHECK_STR(stage_select::format_time(1091), "0'18\"10");
    CHECK_STR(stage_select::format_time(1591), "0'26\"43");
    CHECK_STR(stage_select::format_time(3091), "0'51\"43");
    CHECK_STR(stage_select::format_time(0), "0'00\"00");
    CHECK_STR(stage_select::format_time(3605), "1'00\"00");
}
