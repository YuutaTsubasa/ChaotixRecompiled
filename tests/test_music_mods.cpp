#include "audio/music_mods.h"
#include "game/recomp_dispatch.h"
#include "runtime/patches.h"
#include "runtime/system.h"
#include "test_framework.h"
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace chaotix;
using audio::MusicMods;
using audio::Track;

namespace {

void put16(std::vector<uint8_t>& v, uint32_t x) { v.push_back(uint8_t(x)); v.push_back(uint8_t(x >> 8)); }
void put32(std::vector<uint8_t>& v, uint32_t x) { put16(v, x & 0xFFFF); put16(v, x >> 16); }
void chunk(std::vector<uint8_t>& v, const char* id, const std::vector<uint8_t>& body) {
    v.insert(v.end(), id, id + 4);
    put32(v, uint32_t(body.size()));
    v.insert(v.end(), body.begin(), body.end());
    if (body.size() & 1) v.push_back(0);
}

// A WAV file: format tag, channels, rate, bits, raw sample bytes, and a loop
// (first and last frame) if loop_end > 0.
std::vector<uint8_t> wav(int format, int channels, uint32_t rate, int bits, const std::vector<uint8_t>& data,
                         uint32_t loop_start = 0, uint32_t loop_end = 0) {
    std::vector<uint8_t> fmt;
    put16(fmt, uint16_t(format));
    put16(fmt, uint16_t(channels));
    put32(fmt, rate);
    put32(fmt, rate * uint32_t(channels * bits / 8));
    put16(fmt, uint16_t(channels * bits / 8));
    put16(fmt, uint16_t(bits));
    std::vector<uint8_t> body = {'W', 'A', 'V', 'E'};
    chunk(body, "fmt ", fmt);
    chunk(body, "data", data);
    if (loop_end) {
        std::vector<uint8_t> smpl;
        for (int i = 0; i < 7; ++i) put32(smpl, 0);
        put32(smpl, 1);  // one loop
        put32(smpl, 0);
        put32(smpl, 0);  // cue id
        put32(smpl, 0);  // type
        put32(smpl, loop_start);
        put32(smpl, loop_end);
        put32(smpl, 0);
        put32(smpl, 0);
        chunk(body, "smpl", smpl);
    }
    std::vector<uint8_t> file = {'R', 'I', 'F', 'F'};
    put32(file, uint32_t(body.size()));
    file.insert(file.end(), body.begin(), body.end());
    return file;
}

// Frame i holds i on the left and -i on the right.
Track ramp(size_t frames) {
    Track t;
    t.frames = frames;
    t.samples.resize(frames * 2);
    for (size_t i = 0; i < frames; ++i) {
        t.samples[i * 2] = int16_t(i);
        t.samples[i * 2 + 1] = int16_t(-int(i));
    }
    return t;
}

// One frame's worth of silent output, then the player's turn.
std::vector<int16_t> frame(MusicMods& music, Machine& m, std::vector<uint8_t> events, size_t frames = 4) {
    m.music_events = std::move(events);
    m.audio_out.assign(frames * 2, 0);
    music.after_frame(m, 0);
    return m.audio_out;
}

} // namespace

TEST(music_mods, decodes_16_bit_stereo_unchanged_at_the_same_rate) {
    std::vector<uint8_t> data;
    for (int16_t s : {1000, -1000, 2000, -2000, 3000, -3000}) put16(data, uint16_t(s));
    Track t;
    std::string err;
    CHECK(audio::decode_wav(wav(1, 2, 48000, 16, data), 48000, t, &err));
    CHECK_EQ(t.frames, size_t(3));
    CHECK_EQ(t.samples[2], 2000);
    CHECK_EQ(t.samples[5], -3000);
    CHECK(!t.loops);
}

TEST(music_mods, decodes_8_bit_mono_to_stereo_and_resamples) {
    const std::vector<uint8_t> data = {128, 192, 128, 64};
    Track t;
    CHECK(audio::decode_wav(wav(1, 1, 22050, 8, data), 44100, t, nullptr));
    CHECK_EQ(t.frames, size_t(8));
    CHECK_EQ(t.samples[0], 0);
    CHECK_EQ(t.samples[2], t.samples[3]);  // mono goes to both sides
    CHECK(t.samples[2] > 8000 && t.samples[2] < 9000);  // halfway to 0.5 at the new rate
    CHECK(t.samples[4] > 16000);
}

TEST(music_mods, decodes_24_bit_and_float) {
    std::vector<uint8_t> d24 = {0x00, 0x00, 0x40, 0x00, 0x00, 0xC0};  // +0.5, -0.5
    Track t;
    CHECK(audio::decode_wav(wav(1, 1, 1000, 24, d24), 1000, t, nullptr));
    CHECK(std::abs(t.samples[0] - 16384) <= 1);
    CHECK(std::abs(t.samples[2] + 16384) <= 1);
    std::vector<uint8_t> df;
    const float vals[] = {0.25f, -1.5f};
    for (float f : vals) { uint32_t u; std::memcpy(&u, &f, 4); put32(df, u); }
    CHECK(audio::decode_wav(wav(3, 1, 1000, 32, df), 1000, t, nullptr));
    CHECK(std::abs(t.samples[0] - 8192) <= 1);
    CHECK_EQ(t.samples[2], -32767);  // clipped
}

TEST(music_mods, takes_the_loop_from_the_sampler_chunk_and_scales_it) {
    std::vector<uint8_t> data(1000 * 2, 0);
    Track t;
    CHECK(audio::decode_wav(wav(1, 1, 1000, 16, data, 100, 899), 2000, t, nullptr));
    CHECK(t.loops);
    CHECK_EQ(t.loop_start, size_t(200));
    CHECK_EQ(t.loop_end, size_t(1800));
}

TEST(music_mods, loop_text_sets_or_rejects_a_loop) {
    Track t = ramp(2000);
    CHECK(audio::apply_loop_text("50 499\n", 1000, 2000, t));
    CHECK(t.loops);
    CHECK_EQ(t.loop_start, size_t(100));
    CHECK_EQ(t.loop_end, size_t(1000));
    Track u = ramp(2000);
    CHECK(audio::apply_loop_text("50", 1000, 2000, u));
    CHECK_EQ(u.loop_end, size_t(2000));
    Track v = ramp(2000);
    CHECK(!audio::apply_loop_text("start here", 1000, 2000, v));
    CHECK(!v.loops);
}

TEST(music_mods, rejects_what_it_cannot_play) {
    Track t;
    std::string err;
    CHECK(!audio::decode_wav({1, 2, 3}, 44100, t, &err));
    CHECK(!err.empty());
    CHECK(!audio::decode_wav(wav(2, 2, 44100, 4, std::vector<uint8_t>(64, 0)), 44100, t, &err));  // ADPCM
    CHECK(!audio::decode_wav(wav(1, 2, 44100, 16, {}), 44100, t, &err));
}

TEST(music_mods, the_machine_hands_replaced_music_to_the_player) {
    auto m = std::make_unique<Machine>();
    MusicMods music;
    music.set_volume(100);
    music.set_track(5, ramp(100));
    music.attach(*m);
    CHECK(m->music_replaced[5]);
    CHECK(!m->music_replaced[6]);
    auto out = frame(music, *m, {5});
    CHECK_EQ(music.playing(), uint8_t(5));
    CHECK_EQ(out[0], 0);
    CHECK_EQ(out[2], 1);
    CHECK_EQ(out[7], -3);
    out = frame(music, *m, {});
    CHECK_EQ(out[0], 4);  // carries on
    CHECK(m->music_events.empty());
}

TEST(music_mods, a_jingle_holds_the_piece_and_asking_again_resumes_it) {
    auto m = std::make_unique<Machine>();
    MusicMods music;
    music.set_volume(100);
    music.set_track(5, ramp(100));
    music.attach(*m);
    frame(music, *m, {5});
    auto out = frame(music, *m, {0x1F});  // a jingle the driver plays
    CHECK_EQ(music.playing(), uint8_t(0));
    CHECK_EQ(out[0], 0);
    out = frame(music, *m, {5});  // the game asks for it again ($88E962)
    CHECK_EQ(out[0], 4);
    out = frame(music, *m, {patches::kSoundStopMusic});
    CHECK_EQ(out[0], 0);
    out = frame(music, *m, {5});  // stopped: starts over
    CHECK_EQ(out[0], 0);
    CHECK_EQ(out[2], 1);
}

TEST(music_mods, pauses_with_the_game_and_loops) {
    auto m = std::make_unique<Machine>();
    MusicMods music;
    music.set_volume(100);
    Track t = ramp(10);
    t.loops = true;
    t.loop_start = 4;
    t.loop_end = 8;
    music.set_track(5, t);
    frame(music, *m, {5});  // frames 0..3
    m->zram[patches::kZ80Paused] = 2;
    auto out = frame(music, *m, {});
    CHECK_EQ(out[0], 0);
    m->zram[patches::kZ80Paused] = 0;
    out = frame(music, *m, {}, 6);  // 4..7, then back to 4
    CHECK_EQ(out[0], 4);
    CHECK_EQ(out[6], 7);
    CHECK_EQ(out[8], 4);
}

TEST(music_mods, fades_out_over_five_seconds) {
    auto m = std::make_unique<Machine>();
    MusicMods music;
    music.set_volume(100);
    Track t = ramp(1);
    t.samples = {10000, 10000};
    t.loops = true;
    t.loop_start = 0;
    t.loop_end = 1;
    music.set_track(5, t);
    frame(music, *m, {5});
    const size_t second = size_t(kAudioRate);
    auto out = frame(music, *m, {patches::kSoundLowerMusic}, second);
    CHECK(out[0] > 9900);
    CHECK(out[second * 2 - 2] > 7800 && out[second * 2 - 2] < 8200);
    frame(music, *m, {}, second * 4 + 10);
    CHECK_EQ(music.playing(), uint8_t(0));
}

TEST(music_mods, scans_only_music_ids) {
    MusicMods music;
    CHECK_EQ(music.scan("this folder does not exist"), 0);
    CHECK(!music.has(5));
}

#ifdef CHAOTIX_TEST_ROM
TEST(music_mods, replaced_music_silences_the_driver_with_local_rom) {
    // BOTANIC BASE 1 plays music 05. With it replaced (by silence here), the
    // driver is told to stop its music, so the level is quiet.
    auto level_rms = [](bool replaced) {
        auto m = std::make_unique<Machine>();
        std::string err;
        CHECK(m->load_rom(CHAOTIX_TEST_ROM, &err));
        m->reset();
        m->audio_enabled = true;
        install_recompiled_code(*m, true);
        MusicMods music;
        music.set_volume(0);
        if (replaced) music.set_track(5, ramp(100));
        music.attach(*m);
        double sum = 0;
        long n = 0;
        for (int f = 0; f < 1500; ++f) {
            if (f == 300) { m->stage_request.place = 0; m->stage_request.level = 1; m->stage_pending = true; }
            m->input.pad[0] = (f >= 300 && f < 400 && (f % 24) < 6) ? 0x80 : 0;
            m->audio_out.clear();
            m->run_frame();
            music.after_frame(*m, 0);
            if (f >= 1200)
                for (int16_t s : m->audio_out) { sum += double(s) * s; ++n; }
        }
        return std::sqrt(sum / double(n));
    };
    CHECK(level_rms(false) > 800);
    CHECK(level_rms(true) < 50);
}
#endif
