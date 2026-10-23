// Speedrun tools (issue #47): the run timer and its splits, the best kept on
// disk, the input display's drawing and the LiveSplit block. No ROM is
// needed: the run is watched through 68K work RAM, so a Machine with the
// right words written into it is enough (as for time_attack).
#include "frontend/input_display.h"
#include "frontend/livesplit.h"
#include "frontend/speedrun.h"

#include "input/input.h"
#include "runtime/rom.h"
#include "runtime/stage_select.h"
#include "runtime/system.h"
#include "test_framework.h"

#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

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
void put_scene(Machine& m, unsigned mode, unsigned place, unsigned level) {
    wr16(m, 0xDFDE, mode);
    wr16(m, 0xDFF2, place);
    wr16(m, 0xDFF4, level);
}

// A machine and a run driven a frame at a time, 60 frames a second.
struct Driver {
    std::unique_ptr<Machine> mp = std::make_unique<Machine>();
    Machine& m = *mp;
    speedrun::Tracker run;
    uint32_t counter = 100000;   // the level engine's frame counter
    unsigned since = 0;          // FFE052: frames since the clock started

    unsigned frame() {
        ++m.frame_count;
        ++counter;
        wr32(m, stage_select::kFrameCounter, counter);
        wr16(m, stage_select::kSinceStart, since);
        if (since) ++since;
        return run.update(m, double(m.frame_count) / 60.0);
    }
    // Frames until one of them has `event`; returns how many were run, or -1.
    int until(unsigned event, int limit = 2000) {
        for (int i = 1; i <= limit; ++i)
            if (frame() & event) return i;
        return -1;
    }
};

// Story mode up to the first level: title, DATA LOAD, START.
void boot_to_game(Driver& d) {
    put_scene(d.m, speedrun::kModeTitle, 7, 5);
    d.frame();
    put_scene(d.m, speedrun::kModeDataLoad, 0, 0);
    d.frame();
}

// A level played to its goal: the clock starts, runs `frames`, the goal flag
// goes up. Returns the events of the frames that followed until a split.
void play_level(Driver& d, unsigned place, unsigned level, int frames) {
    put_scene(d.m, speedrun::kModeLevel, place, level);
    d.m.wram[stage_select::kReachedGoal] = 0;
    d.since = 0;
    for (int i = 0; i < 30; ++i) d.frame();   // the entry sequence: no clock yet
    d.since = 1;                              // play begins
    for (int i = 0; i < frames; ++i) d.frame();
    d.m.wram[stage_select::kReachedGoal] = 0xFF;
}

} // namespace

TEST(speedrun, times_are_written_the_way_runners_and_the_game_write_them) {
    CHECK_STR(speedrun::format_rta(0), "0:00.00");
    CHECK_STR(speedrun::format_rta(61234), "1:01.23");
    CHECK_STR(speedrun::format_rta(3600000 + 2 * 60000 + 3450), "1:02:03.45");
    CHECK_STR(speedrun::format_rta(-5), "0:00.00");
    CHECK_STR(speedrun::format_game(2080), "0'34\"66");   // the recorded run's results screen
    CHECK_STR(speedrun::format_game(36000), "10'00\"00");
    CHECK_STR(speedrun::format_delta(-500), "-0.50");
    CHECK_STR(speedrun::format_delta(1230), "+1.23");
    CHECK_STR(speedrun::format_delta(62300), "+1:02.30");
    CHECK_STR(speedrun::format_delta(0), "+0.00");
    CHECK(speedrun::parse_corner("bottom right", speedrun::Corner::TopLeft) == speedrun::Corner::BottomRight);
    CHECK(speedrun::parse_corner("TopLeft", speedrun::Corner::BottomRight) == speedrun::Corner::TopLeft);
    CHECK(speedrun::parse_corner("nowhere", speedrun::Corner::BottomLeft) == speedrun::Corner::BottomLeft);
    CHECK(speedrun::parse_corner(speedrun::corner_name(speedrun::Corner::BottomLeft), speedrun::Corner::TopRight) ==
          speedrun::Corner::BottomLeft);
}

TEST(speedrun, a_run_starts_when_the_game_leaves_data_load) {
    Driver d;
    put_scene(d.m, speedrun::kModeBoot, 0, 0);
    CHECK_EQ(d.frame(), 0u);                       // the very first frame is no transition
    put_scene(d.m, speedrun::kModeTitle, 7, 5);
    CHECK_EQ(d.frame(), 0u);
    CHECK(!d.run.running());
    // DATA LOAD and back to the title: no run.
    put_scene(d.m, speedrun::kModeDataLoad, 0, 0);
    CHECK_EQ(d.frame(), 0u);
    put_scene(d.m, speedrun::kModeTitle, 7, 5);
    CHECK_EQ(d.frame(), 0u);
    CHECK(!d.run.running());
    // An attract demo is not a run either.
    put_scene(d.m, 0x38, 0, 1);
    CHECK_EQ(d.frame(), 0u);
    // START on a slot: the game moves on to INTRODUCTION's opening.
    put_scene(d.m, speedrun::kModeDataLoad, 0, 0);
    d.frame();
    put_scene(d.m, 0x58, 6, 0);
    CHECK_EQ(d.frame(), unsigned(speedrun::kStarted));
    CHECK(d.run.running());
    CHECK(!d.run.time_attack());
    CHECK_EQ(d.run.run_id(), 1u);
    // Real time runs from that frame.
    for (int i = 0; i < 60; ++i) d.frame();
    CHECK_EQ(d.run.rta_ms(double(d.m.frame_count) / 60.0), int64_t(1000));
}

TEST(speedrun, a_finished_level_splits_once_with_the_time_on_its_results_screen) {
    Driver d;
    boot_to_game(d);
    put_scene(d.m, speedrun::kModeLevel, 7, 0);   // the lobby
    CHECK(d.frame() & speedrun::kStarted);
    play_level(d, 0, 1, 2000);
    CHECK(d.run.in_level());
    CHECK_STR(d.run.level_name(), "BOTANIC BASE 1");
    CHECK(d.run.igt_frames() > 1900);             // live: the level's running clock
    // The clock settles a few frames after the goal flag; the split is taken
    // then, and only then.
    const uint64_t goal_frame = d.m.frame_count + 1;
    const int after = d.until(speedrun::kSplit);
    CHECK(after > 0 && after <= stage_select::kGoalSettle);
    CHECK_EQ(d.run.splits().size(), size_t(1));
    const speedrun::Split s = d.run.splits()[0];
    CHECK_EQ(s.place, 0);
    CHECK_EQ(s.level, 1);
    CHECK_STR(s.name, "BOTANIC BASE 1");
    CHECK(s.frame >= goal_frame && s.frame < goal_frame + uint64_t(stage_select::kGoalSettle));
    // The level clock is what the HUD shows: the frames since play began,
    // less the HUD's lag.
    const int clock = int(s.frame - goal_frame) + 2000 + 1;
    CHECK(s.level_frames >= clock - stage_select::kHudLag - stage_select::kGoalSettle);
    CHECK(s.level_frames <= clock);
    CHECK_EQ(s.igt_frames, s.level_frames);
    CHECK_EQ(d.run.igt_frames(), s.level_frames);  // counted once
    // The tally runs on, then the game counts the level as finished (its
    // progress byte moves on) and goes back to the lobby: no second split.
    for (int i = 0; i < 600; ++i) CHECK_EQ(d.frame() & speedrun::kSplit, 0u);
    d.m.wram[speedrun::kProgress + 0] = 2;
    CHECK_EQ(d.frame() & speedrun::kSplit, 0u);
    put_scene(d.m, speedrun::kModeLevel, 7, 0);
    CHECK_EQ(d.frame() & speedrun::kSplit, 0u);
    CHECK_EQ(d.run.splits().size(), size_t(1));
    CHECK_EQ(d.run.igt_frames(), s.level_frames);

    // The next level adds its own time to the game time.
    play_level(d, 3, 2, 1200);
    CHECK(d.until(speedrun::kSplit) > 0);
    CHECK_EQ(d.run.splits().size(), size_t(2));
    CHECK_STR(d.run.splits()[1].name, "TECHNO TOWER 2");
    CHECK_EQ(d.run.splits()[1].igt_frames, s.level_frames + d.run.splits()[1].level_frames);
    CHECK(d.run.splits()[1].rta_ms > s.rta_ms);
}

TEST(speedrun, a_level_left_without_finishing_adds_nothing) {
    Driver d;
    boot_to_game(d);
    put_scene(d.m, speedrun::kModeLevel, 7, 0);
    d.frame();
    play_level(d, 1, 3, 500);
    d.m.wram[stage_select::kReachedGoal] = 0;     // no goal: the clock ran out
    for (int i = 0; i < 10; ++i) d.frame();
    CHECK(d.run.igt_frames() > 400);
    put_scene(d.m, speedrun::kModeLevel, 7, 0);   // back to the lobby
    CHECK_EQ(d.frame() & speedrun::kSplit, 0u);
    CHECK_EQ(d.run.igt_frames(), 0);
    CHECK(d.run.splits().empty());
}

TEST(speedrun, a_level_the_game_counts_as_finished_splits_without_a_goal) {
    Driver d;
    boot_to_game(d);
    put_scene(d.m, speedrun::kModeLevel, 7, 0);
    d.frame();
    d.m.wram[speedrun::kProgress + 2] = 4;        // AMAZING ARENA, up to level 4
    play_level(d, 2, 4, 300);
    d.m.wram[stage_select::kReachedGoal] = 0;     // a boss: no goal flag seen
    d.frame();
    d.m.wram[speedrun::kProgress + 2] = 5;        // $890F86: progress = level + 1
    CHECK(d.frame() & speedrun::kSplit);
    CHECK_STR(d.run.splits().back().name, "AMAZING ARENA 4");
    CHECK(d.run.splits().back().level_frames > 250);
}

TEST(speedrun, introduction_splits_when_it_hands_over_to_the_lobby) {
    Driver d;
    boot_to_game(d);
    put_scene(d.m, 0x58, 6, 0);
    CHECK(d.frame() & speedrun::kStarted);
    put_scene(d.m, speedrun::kModeLevel, 6, 1);
    for (int i = 0; i < 100; ++i) d.frame();
    // Not yet: the lobby without the end of INTRODUCTION's mark.
    std::memset(d.m.wram + speedrun::kProgress, 0, 5);
    put_scene(d.m, speedrun::kModeLevel, 7, 0);
    CHECK_EQ(d.frame() & speedrun::kSplit, 0u);
    put_scene(d.m, speedrun::kModeLevel, 6, 1);
    d.frame();
    // $890F28: every attraction at level 1, then WORLD ENTRANCE.
    std::memset(d.m.wram + speedrun::kProgress, 1, 4);
    put_scene(d.m, speedrun::kModeLevel, 7, 0);
    CHECK(d.frame() & speedrun::kSplit);
    CHECK_STR(d.run.splits().back().name, "INTRODUCTION");
    CHECK_EQ(d.run.splits().back().level_frames, 0);
    // Once a run.
    put_scene(d.m, speedrun::kModeLevel, 6, 1);
    d.frame();
    put_scene(d.m, speedrun::kModeLevel, 7, 0);
    CHECK_EQ(d.frame() & speedrun::kSplit, 0u);
}

TEST(speedrun, the_title_screen_resets_a_run_and_offers_it_as_a_best) {
    Driver d;
    boot_to_game(d);
    put_scene(d.m, speedrun::kModeLevel, 7, 0);
    d.frame();
    play_level(d, 0, 1, 900);
    d.until(speedrun::kSplit);
    put_scene(d.m, speedrun::kModeTitle, 7, 5);
    CHECK(d.frame() & speedrun::kReset);
    CHECK(!d.run.running());
    CHECK_EQ(d.run.igt_frames(), 0);
    CHECK(d.run.splits().empty());
    CHECK_EQ(d.run.ended_splits().size(), size_t(1));
    CHECK(d.run.ended_counts());
    CHECK_EQ(d.run.rta_ms(1e6), int64_t(0));
}

TEST(speedrun, a_cheated_run_or_time_attack_is_never_a_best) {
    Driver d;
    boot_to_game(d);
    put_scene(d.m, speedrun::kModeLevel, 7, 0);
    d.frame();
    d.run.mark_cheated();
    play_level(d, 0, 1, 900);
    d.until(speedrun::kSplit);
    CHECK(d.run.reset() & speedrun::kReset);
    CHECK(!d.run.ended_counts());

    // TIME ATTACK: started by hand, its title screen does not reset it, and
    // when its run is over the time stays.
    CHECK(d.run.start(double(d.m.frame_count) / 60.0, true) & speedrun::kStarted);
    put_scene(d.m, speedrun::kModeTitle, 7, 5);
    CHECK_EQ(d.frame() & speedrun::kReset, 0u);
    play_level(d, 4, 2, 600);
    d.until(speedrun::kSplit);
    const int igt = d.run.igt_frames();
    CHECK(igt > 500);
    d.run.stop(double(d.m.frame_count) / 60.0);
    CHECK(d.run.state() == speedrun::Tracker::State::Stopped);
    const int64_t rta = d.run.rta_ms(double(d.m.frame_count) / 60.0);
    for (int i = 0; i < 100; ++i) d.frame();
    CHECK_EQ(d.run.rta_ms(double(d.m.frame_count) / 60.0), rta);
    CHECK_EQ(d.run.igt_frames(), igt);
    CHECK(d.run.reset() & speedrun::kReset);
    CHECK(!d.run.ended_counts());
}

TEST(speedrun, the_best_is_the_run_that_got_furthest_fastest) {
    auto split = [](int64_t rta, int igt) {
        speedrun::Split s;
        s.rta_ms = rta;
        s.igt_frames = igt;
        return s;
    };
    speedrun::Best best;
    CHECK(!speedrun::better({}, best, speedrun::Compare::Real));
    CHECK(speedrun::better({split(5000, 200)}, best, speedrun::Compare::Real));
    best.splits = {split(5000, 200), split(9000, 400)};
    // Fewer splits never; more always; as many by the time chosen.
    CHECK(!speedrun::better({split(1000, 10)}, best, speedrun::Compare::Real));
    CHECK(speedrun::better({split(6000, 300), split(10000, 500), split(20000, 900)}, best, speedrun::Compare::Real));
    CHECK(speedrun::better({split(6000, 300), split(8000, 500)}, best, speedrun::Compare::Real));
    CHECK(!speedrun::better({split(6000, 300), split(8000, 500)}, best, speedrun::Compare::Game));
    CHECK(speedrun::better({split(6000, 100), split(9500, 350)}, best, speedrun::Compare::Game));
    // Deltas, split by split.
    const std::vector<speedrun::Split> run = {split(4500, 210), split(9300, 390), split(12000, 600)};
    int64_t delta = 0;
    CHECK(speedrun::delta_ms(run, 0, best, speedrun::Compare::Real, delta));
    CHECK_EQ(delta, int64_t(-500));
    CHECK(speedrun::delta_ms(run, 1, best, speedrun::Compare::Real, delta));
    CHECK_EQ(delta, int64_t(300));
    CHECK(speedrun::delta_ms(run, 0, best, speedrun::Compare::Game, delta));
    CHECK_EQ(delta, speedrun::frames_to_ms(210) - speedrun::frames_to_ms(200));
    CHECK(!speedrun::delta_ms(run, 2, best, speedrun::Compare::Real, delta));
}

TEST(speedrun, the_best_survives_a_round_trip_through_its_file) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "chaotix_speedrun_test";
    fs::remove_all(dir);
    fs::create_directories(dir / "Splits");
    const std::string root = dir.u8string() + "/";
    const std::string path = speedrun::best_path(root, "0c2fff7bc79ed26507c08ac47464c3af19f7ced7");
    CHECK_STR(path, root + "Splits/0c2fff7b.splits");

    speedrun::Best best;
    speedrun::Split a;
    a.place = 6; a.level = 0; a.name = "INTRODUCTION"; a.frame = 9000; a.rta_ms = 150123; a.igt_frames = 0;
    speedrun::Split b;
    b.place = 0; b.level = 1; b.name = "BOTANIC BASE 1"; b.frame = 12345; b.rta_ms = 200456;
    b.level_frames = 2080; b.igt_frames = 2080;
    best.splits = {a, b};
    CHECK(speedrun::save_best(path, best));
    speedrun::Best back;
    CHECK(speedrun::load_best(path, back));
    CHECK_EQ(back.splits.size(), size_t(2));
    for (size_t i = 0; i < 2; ++i) {
        CHECK_EQ(back.splits[i].place, best.splits[i].place);
        CHECK_EQ(back.splits[i].level, best.splits[i].level);
        CHECK_STR(back.splits[i].name, best.splits[i].name);
        CHECK_EQ(back.splits[i].frame, best.splits[i].frame);
        CHECK_EQ(back.splits[i].rta_ms, best.splits[i].rta_ms);
        CHECK_EQ(back.splits[i].igt_frames, best.splits[i].igt_frames);
        CHECK_EQ(back.splits[i].level_frames, best.splits[i].level_frames);
    }
    // Missing or not ours: nothing, and no best.
    CHECK(!speedrun::load_best(root + "Splits/none.splits", back));
    CHECK(back.empty());
    {
        std::FILE* f = std::fopen((root + "Splits/bad.splits").c_str(), "w");
        std::fputs("something else\n", f);
        std::fclose(f);
    }
    CHECK(!speedrun::load_best(root + "Splits/bad.splits", back));
    fs::remove_all(dir);
}

TEST(speedrun, places_are_named_from_the_rom_or_the_table) {
    Rom rom;   // nothing readable: the table's names
    const std::vector<std::string> names = stage_select::place_names(rom);
    CHECK_EQ(names.size(), size_t(stage_select::kPlaceCount));
    CHECK_STR(names[0], "BOTANIC BASE");
    CHECK_STR(names[6], "INTRODUCTION");
    // A name in the ROM's own table, padded with spaces, is read from there.
    rom.data.assign(0x080000, 0);
    rom.file_size = rom.data.size();
    const char renamed[] = "GREEN GARDEN        ";
    std::memcpy(rom.data.data() + 0x07739E, renamed, sizeof renamed);  // with its NUL
    speedrun::Tracker t;
    t.set_place_names(stage_select::place_names(rom));
    CHECK_STR(t.name_of(0, 2), "GREEN GARDEN 2");
    CHECK_STR(t.name_of(6, 0), "INTRODUCTION");
}

TEST(input_display, every_button_is_inside_the_box) {
    for (const input_display::Button& b : input_display::layout(0xFFFF)) {
        CHECK(b.x >= 0 && b.y >= 0 && b.w > 0 && b.h > 0);
        CHECK(b.x + b.w <= input_display::kWidth);
        CHECK(b.y + b.h <= input_display::kHeight);
        CHECK(b.lit);
    }
    const auto some = input_display::layout(PAD_LEFT | PAD_C);
    int lit = 0;
    for (const auto& b : some) lit += b.lit ? 1 : 0;
    CHECK_EQ(lit, 2);
    CHECK_EQ(some.size(), size_t(11));   // D-pad, START, A B C, X Y Z
}

TEST(input_display, draws_only_within_its_box) {
    constexpr int W = 400, H = 240, S = 416;
    std::vector<uint32_t> picture(size_t(S) * H);
    for (size_t i = 0; i < picture.size(); ++i) picture[i] = uint32_t(i * 2654435761u) & 0xFFFFFF;
    struct Place { int x, y, scale; };
    // Inside, at the edges (clipped by the picture) and partly off it.
    const Place places[] = {{8, 200, 1}, {100, 50, 3}, {W - 30, H - 10, 2}, {-20, -5, 2}, {W - 56, H - 20, 1}};
    for (uint16_t pad : {uint16_t(0), uint16_t(0x0FFF), uint16_t(PAD_UP | PAD_A | PAD_START)}) {
        for (const Place& p : places) {
            std::vector<uint32_t> out = picture;
            input_display::draw(out.data(), S, W, H, p.x, p.y, p.scale, pad, 255);
            int inside = 0, outside = 0;
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < S; ++x) {
                    const size_t k = size_t(y) * S + size_t(x);
                    if (out[k] == picture[k]) continue;
                    const bool in = x >= p.x && x < p.x + input_display::kWidth * p.scale && y >= p.y &&
                                    y < p.y + input_display::kHeight * p.scale && x < W;
                    (in ? inside : outside) += 1;
                }
            CHECK_EQ(outside, 0);
            CHECK(inside > 0);
        }
    }
    // Transparent: nothing at all.
    std::vector<uint32_t> out = picture;
    input_display::draw(out.data(), S, W, H, 10, 10, 2, 0xFFFF, 0);
    CHECK(out == picture);
}

TEST(livesplit, the_block_is_where_an_auto_splitter_looks_for_it) {
    const livesplit::Block& b = livesplit::block();
    // Built from pieces, so this test does not put a second copy of the magic
    // into a program that might be scanned.
    const std::string magic = std::string("CHAOTIX_") + "LIVESPLIT";
    CHECK_EQ(std::memcmp(b.magic, magic.c_str(), magic.size() + 1), 0);
    for (size_t i = magic.size(); i < sizeof b.magic; ++i) CHECK_EQ(b.magic[i], '\0');
    CHECK_EQ(b.version, livesplit::kVersion);
    CHECK_EQ(b.size, uint32_t(sizeof(livesplit::Block)));
    CHECK_EQ(reinterpret_cast<const char*>(&b.wram) - reinterpret_cast<const char*>(&b), 32);

    // After a frame it points at the machine's work RAM and tells the run.
    Driver d;
    boot_to_game(d);
    put_scene(d.m, speedrun::kModeLevel, 7, 0);
    d.frame();
    play_level(d, 0, 1, 700);
    d.until(speedrun::kSplit);
    livesplit::publish(&d.m, d.run);
    CHECK_EQ(b.wram, uint64_t(reinterpret_cast<uintptr_t>(d.m.wram)));
    CHECK_EQ(b.frame, uint32_t(d.m.frame_count));
    CHECK_EQ(b.state, 1u);
    CHECK_EQ(b.run_id, d.run.run_id());
    CHECK_EQ(b.splits, 1u);
    CHECK_EQ(b.igt_frames, uint32_t(d.run.igt_frames()));
    CHECK_EQ(b.time_attack, 0u);
    // Read as an auto-splitter does: the RAM through the pointer, words big-endian.
    const uint8_t* ram = reinterpret_cast<const uint8_t*>(uintptr_t(b.wram));
    CHECK_EQ(unsigned(ram[0xDFDE] << 8 | ram[0xDFDF]), unsigned(speedrun::kModeLevel));
    d.run.reset();
    livesplit::publish(nullptr, d.run);
    CHECK_EQ(b.wram, uint64_t(0));
    CHECK_EQ(b.state, 0u);
}
