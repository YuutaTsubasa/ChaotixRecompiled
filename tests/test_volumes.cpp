#include "audio/volumes.h"
#include "game/recomp_dispatch.h"
#include "runtime/system.h"
#include "test_framework.h"
#include <cmath>
#include <memory>
#include <string>

using namespace chaotix;

namespace {
void track(Machine& m, int t, uint8_t ctrl, uint8_t voice) {
    m.zram[0x1C40 + t * 0x30] = ctrl;
    m.zram[0x1C40 + t * 0x30 + 1] = voice;
}
}

TEST(volumes, channels_a_sound_effect_drives_take_its_level) {
    auto m = std::make_unique<Machine>();
    track(*m, 5, 0x84, 0x06);   // music on FM6, taken over
    track(*m, 15, 0x80, 0x06);  // a sound effect on FM6
    track(*m, 17, 0x80, 0xC0);  // and one on PSG3
    track(*m, 14, 0x00, 0x05);  // a stopped slot
    audio::set_music_and_effect_levels(*m, 0.25f, 0.75f);
    CHECK_EQ(m->ym.gain[5], 0.75f);
    CHECK_EQ(m->ym.gain[4], 0.25f);
    CHECK_EQ(m->ym.gain[0], 0.25f);
    CHECK_EQ(m->psg.gain[2], 0.75f);
    CHECK_EQ(m->psg.gain[3], 0.75f);  // noise follows PSG3
    CHECK_EQ(m->psg.gain[0], 0.25f);
    CHECK_EQ(m->pwm_gain, 0.25f);
    track(*m, 15, 0x00, 0x06);  // the effect ends
    audio::set_music_and_effect_levels(*m, 0.25f, 0.75f);
    CHECK_EQ(m->ym.gain[5], 0.25f);
}

#ifdef CHAOTIX_TEST_ROM
TEST(volumes, music_off_keeps_the_sound_effects_with_local_rom) {
    // BOTANIC BASE 1 with the music at 0: quiet, until a sound effect plays.
    auto m = std::make_unique<Machine>();
    std::string err;
    CHECK(m->load_rom(CHAOTIX_TEST_ROM, &err));
    m->reset();
    m->audio_enabled = true;
    install_recompiled_code(*m, true);
    auto rms = [&](int frames) {
        double s = 0;
        long n = 0;
        for (int f = 0; f < frames; ++f) {
            audio::set_music_and_effect_levels(*m, 0.0f, 1.0f);
            m->audio_out.clear();
            m->run_frame();
            for (int16_t v : m->audio_out) { s += double(v) * v; ++n; }
        }
        return std::sqrt(s / double(n));
    };
    for (int f = 0; f < 1500; ++f) {
        if (f == 300) { m->stage_request.place = 0; m->stage_request.level = 1; m->stage_pending = true; }
        m->input.pad[0] = (f >= 300 && f < 400 && (f % 24) < 6) ? 0x80 : 0;
        m->audio_out.clear();
        m->run_frame();
    }
    // Muting takes the music's DC level away at once, and the DC blocker's
    // tail of that step lasts a few frames (950 RMS, then 160, 27, 4, 0.5);
    // what is checked is the quiet after it.
    rms(6);
    const double quiet = rms(60);
    m->zram[0x1C0A] = 0x34;  // a sound effect, queued as $8F6E76 does
    const double effect = rms(20);
    CHECK(quiet < 60);
    CHECK(effect > 300);
}
#endif
