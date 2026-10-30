// Texas Instruments SN76489 (Sega variant) programmable sound generator.
#pragma once
#include <cstdint>

namespace chaotix {

class Sn76489 {
public:
    void reset();
    void write(uint8_t v);
    // Advances by `clocks` PSG input clocks (MCLK/15) and returns the mono
    // output averaged over that period.
    int16_t run(int clocks);

    // Host-side output level of tones 1-3 and noise (see Ym2612::gain).
    float gain[4] = {1, 1, 1, 1};
    // What is applied: eased towards gain once per output sample (run),
    // audio/gain_ramp.h.
    float gain_now[4] = {1, 1, 1, 1};

    // Save states (runtime/save_state.h): every field of the chip's state, in
    // a fixed order, through the archive `a`. Not gain, the host's.
    template <class A> void state_io(A& a) {
        a(tone_); a(vol_); a(noise_); a(latch_); a(counter_); a(out_); a(lfsr_); a(div_); a(last_);
    }
    // The last output sample: after the gains, so sound only (netplay leaves
    // it out of the states it compares, savestate::output_only_ranges).
    const int16_t* output_sample() const { return &last_; }

private:
    uint16_t tone_[3] = {0, 0, 0};
    uint8_t vol_[4] = {15, 15, 15, 15};
    uint8_t noise_ = 0;
    int latch_ = 0;
    int counter_[4] = {0, 0, 0, 0};
    uint8_t out_[4] = {0, 0, 0, 0};
    uint16_t lfsr_ = 0x8000;
    int div_ = 0;
    int16_t last_ = 0;
};

} // namespace chaotix
