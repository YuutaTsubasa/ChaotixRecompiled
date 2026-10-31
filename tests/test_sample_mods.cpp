// PWM samples replaced by files (audio/sample_mods.h, audio/pwm_samples.h).
#include "audio/pwm_samples.h"
#include "audio/sample_mods.h"
#include "frontend/jukebox.h"
#include "game/recomp_dispatch.h"
#include "runtime/system.h"
#include "test_framework.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace chaotix;
using audio::SampleMods;
using audio::Track;

TEST(sample_mods, a_note_is_a_sample_and_two_volumes) {
    // The driver's volumes: the high byte's top four bits left, bottom four
    // right, each (n * 2 + 2) / 32.
    const auto n = audio::pwm::decode_note(0xF00A);
    CHECK_EQ(int(n.sample), 0x0A);
    CHECK_EQ(n.left, 32);
    CHECK_EQ(n.right, 2);
    const auto quiet = audio::pwm::decode_note(0x0001);
    CHECK_EQ(quiet.left, 2);
    CHECK_EQ(quiet.right, 2);
}

TEST(sample_mods, an_8_bit_wav_holds_the_bytes) {
    const std::vector<uint8_t> pcm = {0x80, 0xFF, 0x00};
    const auto bytes = audio::pwm::wav_bytes(pcm, 11000);
    CHECK_EQ(bytes.size(), size_t(44 + 4));  // padded to an even size
    Track t;
    CHECK(audio::decode_wav(bytes, 11000, t, nullptr));
    CHECK_EQ(t.frames, size_t(3));
    CHECK(t.samples[2] > 30000);
    CHECK(t.samples[4] < -30000);
}

#ifdef CHAOTIX_TEST_ROM
namespace {

std::unique_ptr<Machine> machine() {
    auto m = std::make_unique<Machine>();
    std::string err;
    CHECK(m->load_rom(CHAOTIX_TEST_ROM, &err));
    m->reset();
    return m;
}

// The game's own samples as files, or silence of the same length. Only the
// samples played at full speed have files: the slower ones made from the
// same recording play their file slowed down.
SampleMods from_game(Machine& m, bool silent) {
    SampleMods s;
    const auto table = audio::pwm::read_table(m);
    for (size_t i = 0; i < table.size(); ++i) {
        if (table[i].step != audio::pwm::kStepOne) continue;
        auto pcm = audio::pwm::read_bytes(m, table[i]);
        if (silent) pcm.assign(pcm.size(), 0x80);
        Track t;
        CHECK(audio::decode_wav(audio::pwm::wav_bytes(pcm, 11000), kAudioRate, t, nullptr));
        s.set_sample(uint8_t(i), std::move(t));
    }
    return s;
}

std::vector<int16_t> play(const Rom& rom, const SampleMods* samples) {
    jukebox::Player p;
    std::string why;
    CHECK(p.start(rom, nullptr, true, &why, samples));
    p.play(5);
    std::vector<int16_t> out;
    p.run_frames(900, out);
    return out;
}

double rms(const std::vector<double>& v) {
    double s = 0;
    for (double x : v) s += x * x;
    return std::sqrt(s / double(v.size()));
}

double correlation(const std::vector<double>& a, const std::vector<double>& b) {
    double ab = 0, aa = 0, bb = 0;
    for (size_t i = 0; i < a.size(); ++i) { ab += a[i] * b[i]; aa += a[i] * a[i]; bb += b[i] * b[i]; }
    return ab / std::sqrt(aa * bb);
}

} // namespace

TEST(sample_mods, the_table_is_the_games_with_local_rom) {
    auto m = machine();
    const auto table = audio::pwm::read_table(*m);
    CHECK_EQ(table.size(), size_t(22));
    // 05 is 04's recording at a lower step; 15 is 64 bytes of silence.
    CHECK_EQ(table[5].start, table[4].start);
    CHECK_EQ(table[5].step, 0x659u);
    const auto silence = audio::pwm::read_bytes(*m, table[audio::pwm::kSilence]);
    CHECK_EQ(silence.size(), size_t(64));
    for (uint8_t b : silence) CHECK_EQ(int(b), 0x80);
}

TEST(sample_mods, files_play_in_place_of_the_samples_with_local_rom) {
    // BOTANIC BASE's music (5) on the music player's machine: as the game
    // plays it, with every sample replaced by silence, and with every sample
    // replaced by a file of the game's own sample. The player borrows the
    // recompiled code from the game's machine, as it does in the game.
    auto m = machine();
    install_recompiled_code(*m, true);
    const SampleMods silent = from_game(*m, true), same = from_game(*m, false);
    const auto game = play(m->rom, nullptr);
    const auto quiet = play(m->rom, &silent);
    const auto files = play(m->rom, &same);
    CHECK_EQ(game.size(), quiet.size());
    CHECK_EQ(game.size(), files.size());
    // The drums alone: the music less the music without them.
    std::vector<double> drums, from_files;
    for (size_t i = 4000; i < game.size(); ++i) {
        drums.push_back(double(game[i]) - quiet[i]);
        from_files.push_back(double(files[i]) - quiet[i]);
    }
    // Silenced, the drums are gone (the 32X was given the silent sample)...
    CHECK(rms(drums) > 1000);
    // ...and the files play them at the same moments, volumes and level
    // (measured: 0.96 and 1.02).
    const double match = correlation(drums, from_files), level = rms(from_files) / rms(drums);
    std::printf("drums: correlation %.3f, level %.3f\n", match, level);
    CHECK(match > 0.93);
    CHECK(level > 0.95 && level < 1.08);
}
#endif
