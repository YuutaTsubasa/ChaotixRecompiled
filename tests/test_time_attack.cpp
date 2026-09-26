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
// A level counting from `base`: the engine's counter at `counter`, FFE052
// holding the frames since the clock started, and `stopped` frames of stopped
// clock behind it.
void put_clock(Machine& m, uint32_t counter, unsigned base, unsigned stopped) {
    wr32(m, stage_select::kFrameCounter, counter);
    wr16(m, stage_select::kSinceStart, counter > base ? unsigned(counter - base) : 0u);
    wr16(m, stage_select::kStoppedFrames, stopped);
}

// Enough frames of a steadily climbing clock for the start to be worked out.
void settle_start(Machine& m, time_attack::Run& run, uint32_t counter, unsigned base,
                  unsigned stopped) {
    for (int i = 0; i <= stage_select::kCalibrationFrames; ++i) {
        put_clock(m, counter + uint32_t(i), base, stopped);
        run.update(m);
    }
}

} // namespace

TEST(time_attack, the_time_is_what_the_level_counts_from) {
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

    // Taken, and the level's entry sequence is playing: the engine's counter
    // is already running but the level has not begun counting yet.
    m.stage_pending = false;
    put_scene(m, 4, 3);
    put_clock(m, 1000, 1004, 0xFF00);
    CHECK(!run.update(m));
    CHECK(run.started);
    CHECK_EQ(run.time, 0);       // never negative

    // Play. Once the clock has been climbing steadily the start is known, and
    // the time is the counter less it.
    settle_start(m, run, 1005, 1004, 0xFF00);
    put_clock(m, 1300, 1004, 0xFF00);
    CHECK(!run.update(m));
    CHECK_EQ(run.time, 296);
}

TEST(time_attack, frames_the_game_was_stopped_for_are_not_the_players_time) {
    // The engine's counter runs on while the game's own clock is stopped --
    // pause it and the HUD holds still while the counter does not. The game
    // counts those frames, and so must a run, or a record would include the
    // pause (measured: 600 frames of pause, 600 frames of counter).
    auto mp = std::make_unique<Machine>();
    Machine& m = *mp;
    stage_select::Request r;
    time_attack::Run run;
    run.begin(r);
    m.stage_pending = false;
    put_scene(m, r.place, r.level);
    put_clock(m, 1000, 1000, 0xFF00);
    CHECK(!run.update(m));
    settle_start(m, run, 1001, 1000, 0xFF00);

    put_clock(m, 1100, 1000, 0xFF00);
    CHECK(!run.update(m));
    CHECK_EQ(run.time, 100);

    // Six hundred frames during which the game was stopped: the counter moves
    // and so does the count of stopped frames, which wraps past 65535 on the
    // way (it starts near the top).
    put_clock(m, 1700, 1000, uint16_t(0xFF00 + 600));
    CHECK(!run.update(m));
    CHECK_EQ(run.time, 100);

    // And playing again.
    put_clock(m, 1750, 1000, uint16_t(0xFF00 + 600));
    CHECK(!run.update(m));
    CHECK_EQ(run.time, 150);
}

TEST(time_attack, a_run_ends_when_the_game_moves_the_player_on) {
    auto mp = std::make_unique<Machine>();
    Machine& m = *mp;
    stage_select::Request r;
    time_attack::Run run;
    run.begin(r);
    m.stage_pending = false;
    put_scene(m, r.place, r.level);
    put_clock(m, 1000, 1000, 0);
    CHECK(!run.update(m));
    settle_start(m, run, 1001, 1000, 0);
    put_clock(m, 1600, 1000, 0);
    CHECK(!run.update(m));
    CHECK_EQ(run.time, 600);

    // The game sends the player to its lobby, which is how a run ends however
    // it ended.
    put_scene(m, stage_select::kLobbyPlace, 0);
    CHECK(run.update(m));
    CHECK(!run.running);
    CHECK_EQ(run.time, 600);     // the time it had, not the lobby's
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
    put_clock(m, 100, 100, 0);
    run.update(m);
    settle_start(m, run, 101, 100, 0);
    put_clock(m, uint32_t(100 + stage_select::kTimeLimit), 100, 0);
    run.update(m);
    put_scene(m, stage_select::kLobbyPlace, 0);
    CHECK(run.update(m));
    CHECK(run.timed_out());
}

TEST(time_attack, the_tally_after_the_goal_is_not_the_players_time) {
    // Reaching the goal stops the game's clock while its frame counter runs on
    // through twenty-odd seconds of tally. A player's run showed 0'34"66 and
    // was recorded as 0'53"26 until this was noticed.
    auto mp = std::make_unique<Machine>();
    Machine& m = *mp;
    stage_select::Request r;
    time_attack::Run run;
    run.begin(r);
    m.stage_pending = false;
    put_scene(m, r.place, r.level);
    put_clock(m, 1000, 1000, 0);
    CHECK(!run.update(m));
    settle_start(m, run, 1001, 1000, 0);
    put_clock(m, 1500, 1000, 0);
    CHECK(!run.update(m));
    CHECK_EQ(run.time, 500);

    // The goal. The clock settles a few frames later, then holds.
    m.wram[stage_select::kReachedGoal] = 0xFF;
    for (int i = 1; i <= stage_select::kGoalSettle; ++i) {
        put_clock(m, uint32_t(1500 + i), 1000, 0);
        CHECK(!run.update(m));
    }
    CHECK(run.finished);
    CHECK_EQ(run.time, 500 + stage_select::kGoalSettle);

    // Everything after that is the tally, however long the game takes.
    for (int i = 0; i < 1200; ++i) {
        put_clock(m, uint32_t(1500 + stage_select::kGoalSettle + i), 1000, 0);
        CHECK(!run.update(m));
    }
    CHECK_EQ(run.time, 500 + stage_select::kGoalSettle);

    put_scene(m, stage_select::kLobbyPlace, 0);
    CHECK(run.update(m));
    CHECK_EQ(run.time, 500 + stage_select::kGoalSettle);
}

TEST(time_attack, a_goal_flag_left_set_by_whatever_came_before_is_ignored) {
    auto mp = std::make_unique<Machine>();
    Machine& m = *mp;
    stage_select::Request r;
    time_attack::Run run;
    run.begin(r);
    m.stage_pending = false;
    put_scene(m, r.place, r.level);
    m.wram[stage_select::kReachedGoal] = 0xFF;   // already set as the run starts
    put_clock(m, 1000, 1000, 0);
    CHECK(!run.update(m));
    settle_start(m, run, 1001, 1000, 0);
    for (int i = 1; i <= 600; ++i) {
        put_clock(m, uint32_t(1000 + i), 1000, 0);
        CHECK(!run.update(m));
    }
    // It only counts as the goal once it is seen to arrive, so this run is
    // still going and still being timed.
    CHECK(!run.finished);
    CHECK_EQ(run.time, 600);
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
    // The offset is measured against the HUD; these read the same as the clock
    // the player watched.
    // Both of these are readings taken off the game's own HUD in a replay of
    // a session somebody played.
    CHECK_STR(stage_select::format_time(2086), "0'34\"66");
    CHECK_STR(stage_select::format_time(307), "0'05\"01");
    CHECK_STR(stage_select::format_time(0), "0'00\"00");
    CHECK_STR(stage_select::format_time(3606), "1'00\"00");
}
