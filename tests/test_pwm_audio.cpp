#include "runtime/system.h"
#include "test_framework.h"
#include <algorithm>
#include <cstdlib>
#include <memory>

using namespace chaotix;

namespace {
std::vector<int16_t> render_pwm(uint16_t control, uint16_t cycle = 2000) {
    auto m = std::make_unique<Machine>();
    // Synthetic address space only. Halt the CPUs and hold two distinct PWM
    // output levels while the real frame scheduler and audio mixer run.
    m->rom.data.resize(0x400000);
    m->reset();
    m->m68k.stopped = true;
    m->audio_enabled = true;
    m->pwm.ctrl = control;
    m->pwm.cycle = cycle;
    m->pwm.out_l = 500;
    m->pwm.out_r = 1500;
    m->run_frame();
    CHECK_EQ(m->audio_out.size(), size_t(2 * (kMclkPerLine * kLinesPerFrameNTSC / kMclkPerAudioSample)));
    return m->audio_out;
}

void check_first(const std::vector<int16_t>& samples, int left, int right) {
    CHECK(samples.size() >= 2);
    if (samples.size() < 2) return;
    // The first sample has no DC-filter history: (500 - 1000) * 65536 / 2000
    // less an eighth is -14336. These values also ensure
    // that changing the route does not change PWM gain or centering.
    CHECK_EQ(samples[0], left);
    CHECK_EQ(samples[1], right);
}
}

TEST(pwm_audio, normal_stereo_keeps_both_sources) {
    const auto samples = render_pwm(0x0105);
    check_first(samples, -14336, 14336);
}

TEST(pwm_audio, swapped_stereo_exchanges_the_complete_outputs) {
    const auto normal = render_pwm(0x0105);
    const auto swapped = render_pwm(0x010a);
    check_first(swapped, 14336, -14336);
    CHECK_EQ(normal.size(), swapped.size());
    bool exchanged = true;
    for (size_t i = 0; i + 1 < normal.size() && i + 1 < swapped.size(); i += 2)
        exchanged &= normal[i] == swapped[i + 1] && normal[i + 1] == swapped[i];
    CHECK(exchanged);
}

TEST(pwm_audio, disabled_output_and_inactive_cycle_are_silent) {
    for (const auto& samples : {render_pwm(0x00), render_pwm(0x05, 0), render_pwm(0x05, 1)})
        for (int16_t sample : samples) CHECK_EQ(sample, 0);
}

TEST(pwm_audio, a_level_change_eases_in_without_a_jump) {
    // The game parks PWM off its centre (465 of a 1047 cycle in a level), so
    // muting it takes a steady offset away. Eased over 38 ms, that leaves no
    // jump between two samples that could click: the biggest step is a few
    // units, where cutting the level at once stepped by the whole offset.
    auto m = std::make_unique<Machine>();
    m->rom.data.resize(0x400000);
    m->reset();
    m->m68k.stopped = true;
    m->audio_enabled = true;
    m->pwm.ctrl = 0x0105;
    m->pwm.cycle = 1047;
    m->pwm.out_l = m->pwm.out_r = 465;
    for (int f = 0; f < 30; ++f) { m->audio_out.clear(); m->run_frame(); }
    int16_t last = m->audio_out.empty() ? 0 : m->audio_out[m->audio_out.size() - 2];
    m->pwm_gain = 0;
    int jump = 0;
    for (int f = 0; f < 4; ++f) {
        m->audio_out.clear();
        m->run_frame();
        for (size_t i = 0; i < m->audio_out.size(); i += 2) {
            jump = std::max(jump, std::abs(int(m->audio_out[i]) - int(last)));
            last = m->audio_out[i];
        }
    }
    CHECK(jump < 8);
    CHECK_EQ(m->pwm_gain_now, 0.0f);
}
