// Yamaha YM2612 (OPN2) FM synthesizer + timers.
//
// Timers and the status register are what the Z80 sound driver observes, so
// they are clocked in emulated time. Sample generation runs at the chip's
// native rate (YM clock / 144 = MCLK / 1008 ≈ 53.27 kHz).
#pragma once
#include <cstdint>
#include <vector>

namespace chaotix {

class Ym2612 {
public:
    Ym2612();
    void reset();

    void write(int port, int addr_or_data, uint8_t v);  // port 0/1; addr_or_data 0 = address, 1 = data
    uint8_t read_status() const;

    // Advances the chip by `samples` native samples, appending stereo output.
    void generate(int samples, std::vector<int16_t>& out);
    // Advances only the timers (used when audio output is disabled).
    void tick_timers(int samples);

private:
    struct Op {
        uint32_t phase = 0, inc = 0;
        uint8_t dt = 0, mul = 0, tl = 0, ks = 0, ar = 0, am = 0, d1r = 0, d2r = 0, sl = 0, rr = 0, ssg = 0;
        int env = 0x3FF;     // attenuation 0 (loud) .. 0x3FF (silent)
        int state = 3;       // 0 attack, 1 decay, 2 sustain, 3 release
        bool key = false;
        int32_t out = 0;
    };
    struct Ch {
        Op op[4];            // in operator order 1,2,3,4
        uint16_t fnum = 0;
        uint8_t block = 0, fnum_hi = 0;
        uint8_t alg = 0, fb = 0, ams = 0, pms = 0;
        bool left = true, right = true;
        int32_t fb_mem[2] = {0, 0};
    };

    void write_reg(int bank, uint8_t reg, uint8_t v);
    void update_channel_freq(int ch);
    void key(int ch, int op, bool on);
    void step_envelopes();
    int op_output(Op& o, int ch, int mod, int am);
    void run_timers(int samples);

    Ch ch_[6];
    uint8_t addr_[2] = {0, 0};
    // channel 3 special mode frequencies (operators 1-3)
    uint16_t ch3_fnum_[3] = {};
    uint8_t ch3_block_[3] = {}, ch3_hi_ = 0;
    bool ch3_special_ = false;
    // LFO
    bool lfo_en_ = false;
    uint8_t lfo_rate_ = 0;
    int lfo_cnt_ = 0, lfo_step_ = 0;
    // DAC
    bool dac_en_ = false;
    int32_t dac_ = 0;
    // envelope clock
    uint32_t eg_cnt_ = 0;
    int eg_div_ = 0;
    // timers
    uint16_t ta_ = 0;
    uint8_t tb_ = 0;
    uint8_t timer_ctrl_ = 0;
    int ta_cnt_ = 0, tb_cnt_ = 0;
    uint8_t status_ = 0;
};

} // namespace chaotix
