// TIME ATTACK ghosts (issue #41) without the ROM: the file round trip and its
// checks, which ghost is kept and which is raced, the recorder, how a ghost
// is drawn, and the player on a blank machine. tests/test_ghosts.py races a
// real recorded run.
#include "frontend/ghost.h"
#include "runtime/save_state.h"
#include "runtime/system.h"
#include "test_framework.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace chaotix;
namespace fs = std::filesystem;

namespace {

const char kSha[] = "0123456789abcdef0123456789abcdef01234567";

std::unique_ptr<Machine> blank_machine(const char* sha = kSha) {
    auto m = std::make_unique<Machine>();
    m->rom.data.assign(0x400000, 0);
    m->rom.sha1 = sha;
    m->reset();
    return m;
}

// A ghost as a machine would record it: its start state, `frames` pads.
ghost::Ghost make_ghost(const Machine& m, int time, int frames, uint16_t player = 2) {
    ghost::Ghost g;
    g.game_version = savestate::game_version();
    g.rom_sha1 = m.rom.sha1;
    g.rom_data_sha1 = m.rom.sha1;
    g.request.place = 0;
    g.request.level = 1;
    g.request.player = player;
    g.time = time;
    g.recorded_unix = 1700000000;
    g.start = savestate::save(m, {}, 0, 0);
    for (int i = 0; i < frames; ++i) g.inputs.push_back(uint32_t(i * 37 % 7) | uint32_t(i % 3) << 16);
    ghost::SettingsChange c;
    c.settings = ghost::Settings::of(m);
    g.settings.push_back(c);
    c.frame = uint32_t(frames / 2);
    c.settings.wide_extra = 53;
    c.settings.wide_enabled = true;
    c.settings.music_replaced = 0x5;
    g.settings.push_back(c);
    return g;
}

fs::path temp_dir(const char* name) {
    const fs::path d = fs::temp_directory_path() / name;
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d, ec);
    return d;
}

} // namespace

TEST(ghost, file_round_trip_keeps_everything) {
    auto m = blank_machine();
    const ghost::Ghost g = make_ghost(*m, 2086, 3000);
    const std::vector<uint8_t> bytes = ghost::encode(g);
    ghost::Ghost back;
    std::string why;
    CHECK(ghost::decode(bytes, back, &why));
    CHECK(back.game_version == g.game_version);
    CHECK(back.rom_sha1 == g.rom_sha1);
    CHECK(back.rom_data_sha1 == g.rom_data_sha1);
    CHECK_EQ(back.request.place, g.request.place);
    CHECK_EQ(back.request.level, g.request.level);
    CHECK_EQ(back.request.player, g.request.player);
    CHECK_EQ(back.time, 2086);
    CHECK_EQ(back.recorded_unix, g.recorded_unix);
    CHECK(back.start == g.start);
    CHECK(back.inputs == g.inputs);
    CHECK_EQ(back.settings.size(), size_t(2));
    CHECK_EQ(back.settings[1].frame, uint32_t(1500));
    CHECK(back.settings[1].settings == g.settings[1].settings);
    CHECK(back.settings[0].settings == g.settings[0].settings);
    // Small: the inputs pack to almost nothing next to the start state.
    CHECK(bytes.size() < g.start.size() + 4096);
    CHECK(ghost::compatible(back, *m, &why));
}

TEST(ghost, refuses_damaged_or_foreign_files) {
    auto m = blank_machine();
    const ghost::Ghost g = make_ghost(*m, 1000, 200);
    const std::vector<uint8_t> good = ghost::encode(g);
    ghost::Ghost out;
    std::string why;
    std::vector<uint8_t> bad = good;
    bad[0] = 'X';
    CHECK(!ghost::decode(bad, out, &why));
    bad = good;
    bad[good.size() - 3] ^= 0x40;   // in the inputs: the CRC catches it
    CHECK(!ghost::decode(bad, out, &why));
    bad = good;
    bad.resize(good.size() - 1);
    CHECK(!ghost::decode(bad, out, &why));
    bad = good;
    bad[8] = 99;                    // another format
    CHECK(!ghost::decode(bad, out, &why));
    CHECK(why.find("format") != std::string::npos);
    CHECK(!ghost::decode({}, out, &why));

    // Header validation, like a save state's: ROM, ROM image, version.
    auto other = blank_machine("fedcba9876543210fedcba9876543210fedcba98");
    CHECK(!ghost::compatible(g, *other, &why));
    CHECK(why.find("another ROM") != std::string::npos);
    ghost::Ghost h = g;
    h.rom_data_sha1 = "patched";
    CHECK(!ghost::compatible(h, *m, &why));
    CHECK(why.find("patches") != std::string::npos);
    h = g;
    h.game_version = "0.0.1";
    CHECK(!ghost::compatible(h, *m, &why));
    CHECK(why.find("version") != std::string::npos);
    CHECK(ghost::compatible(g, *m, &why));
}

TEST(ghost, the_best_run_is_kept_and_raced) {
    auto m = blank_machine();
    const fs::path d = temp_dir("chaotix_ghost_test");
    const std::string dir = d.u8string() + "/";
    CHECK(ghost::faster(make_ghost(*m, 900, 10), make_ghost(*m, 1000, 10)));
    CHECK(!ghost::faster(make_ghost(*m, 1000, 10), make_ghost(*m, 900, 10)));
    CHECK(!ghost::faster(make_ghost(*m, 1000, 10), make_ghost(*m, 1000, 10)));

    ghost::Ghost chosen;
    std::string path, why;
    CHECK(!ghost::choose(dir, ghost::Mode::Best, make_ghost(*m, 1, 1).request, *m, chosen, path, &why));

    // The first run is the best; a slower one is only the last; a faster
    // one is both.
    ghost::Stored s = ghost::store(dir, make_ghost(*m, 1000, 10), *m);
    CHECK(s.best && s.last && s.bytes > 0);
    s = ghost::store(dir, make_ghost(*m, 1200, 12), *m);
    CHECK(!s.best && s.last);
    stage_select::Request r = make_ghost(*m, 1, 1).request;
    CHECK(ghost::choose(dir, ghost::Mode::Best, r, *m, chosen, path, &why));
    CHECK_EQ(chosen.time, 1000);
    CHECK(path == dir + "0_1_2_7.ghost");
    CHECK(ghost::choose(dir, ghost::Mode::Last, r, *m, chosen, path, &why));
    CHECK_EQ(chosen.time, 1200);
    CHECK(path == dir + "0_1_last.ghost");
    CHECK(!ghost::choose(dir, ghost::Mode::Off, r, *m, chosen, path, &why));
    s = ghost::store(dir, make_ghost(*m, 800, 9), *m);
    CHECK(s.best && s.last);

    // BEST is the stage's fastest whoever ran it, and whatever the file is
    // called: another character's, or one copied in under its own name.
    CHECK(ghost::store(dir, make_ghost(*m, 700, 9, 5), *m).best);
    CHECK(ghost::choose(dir, ghost::Mode::Best, r, *m, chosen, path, &why));
    CHECK_EQ(chosen.time, 700);
    CHECK_EQ(chosen.request.player, 5);
    CHECK(savestate::write_file(dir + "0_1_from_a_friend.ghost", ghost::encode(make_ghost(*m, 650, 9, 3))));
    CHECK(ghost::choose(dir, ghost::Mode::Best, r, *m, chosen, path, &why));
    CHECK_EQ(chosen.time, 650);
    // Not another stage's, and not one this ROM cannot play.
    ghost::Ghost elsewhere = make_ghost(*m, 10, 9);
    elsewhere.request.level = 2;
    CHECK(savestate::write_file(dir + "0_1_wrong_stage.ghost", ghost::encode(elsewhere)));
    ghost::Ghost foreign = make_ghost(*m, 20, 9);
    foreign.rom_sha1 = "someone else's";
    CHECK(savestate::write_file(dir + "0_1_foreign.ghost", ghost::encode(foreign)));
    CHECK(ghost::choose(dir, ghost::Mode::Best, r, *m, chosen, path, &why));
    CHECK_EQ(chosen.time, 650);
    // A ghost put aside is never chosen again.
    CHECK(ghost::discard(dir + "0_1_from_a_friend.ghost"));
    CHECK(fs::exists(fs::u8path(dir + "0_1_from_a_friend.ghost.desynced")));
    CHECK(ghost::choose(dir, ghost::Mode::Best, r, *m, chosen, path, &why));
    CHECK_EQ(chosen.time, 700);
    std::error_code ec;
    fs::remove_all(d, ec);
}

TEST(ghost, recorder_starts_when_the_stage_is_taken) {
    auto m = blank_machine();
    ghost::Recorder rec;
    stage_select::Request r;
    r.place = 3;
    r.level = 2;
    rec.arm(r);
    m->stage_pending = true;
    rec.before_frame(*m);
    rec.after_frame(*m);
    CHECK(!rec.recording());   // still waiting for the game
    m->stage_pending = false;
    rec.after_frame(*m);
    CHECK(rec.recording());
    for (int i = 0; i < 10; ++i) {
        m->input.pad[0] = uint16_t(i);
        m->input.pad[1] = uint16_t(100 + i);
        if (i == 4) m->wide_extra = 40;
        rec.before_frame(*m);
        m->run_frame();
        rec.after_frame(*m);
    }
    ghost::Ghost g;
    CHECK(rec.finish(*m, 555, 42, g));
    CHECK(!rec.recording());
    CHECK_EQ(g.inputs.size(), size_t(10));
    CHECK_EQ(g.inputs[3], uint32_t(3) | uint32_t(103) << 16);
    CHECK_EQ(g.settings.size(), size_t(2));
    CHECK_EQ(g.settings[1].frame, uint32_t(4));
    CHECK_EQ(g.settings[1].settings.wide_extra, 40);
    CHECK_EQ(g.request.place, 3);
    CHECK_EQ(g.time, 555);
    CHECK(ghost::compatible(g, *m, nullptr));
    // Nothing recorded, nothing to keep.
    ghost::Recorder idle;
    CHECK(!idle.finish(*m, 1, 1, g));
}

TEST(ghost, drawn_where_its_world_position_is_on_the_live_screen) {
    // A 4x2 ghost at (10, 20) on its own screen, its camera at (500, 300).
    ghost::Frame f;
    f.visible = true;
    f.x = 10;
    f.y = 20;
    f.w = 4;
    f.h = 2;
    f.cam_x = 500;
    f.cam_y = 300;
    f.px.assign(8, 0xFFFFFFFFu);
    f.px[1] = 0;   // a hole
    const int W = 64, H = 48, S = 70, extra = 3;
    std::vector<uint32_t> pic(size_t(S) * H, 0xFF000000u);
    // The live camera 6 px left of the ghost's, 5 px below it: the ghost
    // shows 6 px further right and 5 px higher, plus the margin.
    ghost::Placed p = ghost::compose(pic.data(), S, W, H, extra, f, 494, 305);
    CHECK_EQ(p.pixels, 7);
    CHECK_EQ(p.x, 10 + 6 + extra);
    CHECK_EQ(p.y, 20 - 5);
    CHECK_EQ(p.w, 4);
    CHECK_EQ(p.h, 2);
    int changed = 0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < S; ++x)
            if (pic[size_t(y) * S + size_t(x)] != 0xFF000000u) {
                ++changed;
                CHECK(x >= p.x && x < p.x + 4 && y >= p.y && y < p.y + 2);
            }
    CHECK_EQ(changed, 7);
    CHECK_EQ(pic[size_t(p.y) * S + size_t(p.x + 1)], 0xFF000000u);   // the hole
    // Half see-through and tinted: neither the background nor pure white.
    const uint32_t c = pic[size_t(p.y) * S + size_t(p.x)];
    CHECK(c != 0xFF000000u && c != 0xFFFFFFFFu);
    CHECK(((c >> 16) & 0xFF) > 0x60 && ((c >> 16) & 0xFF) < 0xF0);
    // Partly off the left edge: clipped; wholly off: nothing.
    std::vector<uint32_t> pic2(size_t(S) * H, 0);
    p = ghost::compose(pic2.data(), S, W, H, 0, f, 500 + 12, 300);
    CHECK_EQ(p.x, 0);
    CHECK_EQ(p.pixels, 4);   // its two right-hand columns
    p = ghost::compose(pic2.data(), S, W, H, 0, f, 500 + 2000, 300);
    CHECK_EQ(p.pixels, 0);
    f.visible = false;
    p = ghost::compose(pic2.data(), S, W, H, 0, f, 500, 300);
    CHECK_EQ(p.pixels, 0);
}

TEST(ghost, camera_trail_reads_one_frame_back) {
    ghost::CameraTrail t;
    int x = -1, y = -1;
    t.get(x, y);
    CHECK_EQ(x, 0);
    t.push(10, 20);
    t.get(x, y);
    CHECK_EQ(x, 10);   // nothing older yet
    t.push(11, 21);
    t.push(12, 22);
    t.get(x, y);
    CHECK_EQ(x, 11);
    CHECK_EQ(y, 21);
}

TEST(ghost, player_runs_a_frame_per_live_frame_and_tells_a_desync) {
    // A blank cartridge never finishes the level, so the replay runs out of
    // inputs without ending as the recording did: a desync.
    auto m = blank_machine();
    m->wram[0xDFF3] = 0;   // zone 0, level 1: the run is in its level
    m->wram[0xDFF5] = 1;
    for (bool threaded : {false, true}) {
        ghost::Player p;
        std::string why;
        CHECK(p.start(m->rom, make_ghost(*m, 300, 5), threaded, &why));
        CHECK(p.active());
        for (int i = 0; i < 4; ++i) p.advance();
        ghost::Frame f;
        CHECK(p.latest(f, 5000));
        ghost::Status s = p.status();
        CHECK_EQ(s.frames, uint64_t(4));
        CHECK(!s.ended);
        CHECK_EQ(p.behind(), uint64_t(0));
        p.advance();
        p.advance();
        CHECK(p.latest(f, 5000));
        s = p.status();
        CHECK(s.ended);
        CHECK(s.desynced);
        CHECK_EQ(s.frames, uint64_t(6));
        p.stop();
        CHECK(!p.active());
    }
    // A start state from another ROM does not load.
    auto other = blank_machine("fedcba9876543210fedcba9876543210fedcba98");
    ghost::Player p;
    std::string why;
    CHECK(!p.start(other->rom, make_ghost(*m, 300, 5), false, &why));
}

TEST(ghost, options_round_trip) {
    CHECK(ghost::parse_mode("best", ghost::Mode::Off) == ghost::Mode::Best);
    CHECK(ghost::parse_mode("LAST", ghost::Mode::Off) == ghost::Mode::Last);
    CHECK(ghost::parse_mode("off", ghost::Mode::Best) == ghost::Mode::Off);
    CHECK(ghost::parse_mode("nonsense", ghost::Mode::Last) == ghost::Mode::Last);
    for (ghost::Mode mode : {ghost::Mode::Off, ghost::Mode::Best, ghost::Mode::Last})
        CHECK(ghost::parse_mode(ghost::mode_name(mode), ghost::Mode::Off) == mode);
}
