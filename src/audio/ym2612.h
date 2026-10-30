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

    // Host-side output level of each channel (1 = as the chip plays it), so
    // music and sound effects can be set apart (audio/volumes.h). Not chip
    // state: the game cannot see it.
    float gain[6] = {1, 1, 1, 1, 1, 1};
    // What is applied this sample: eased towards gain (audio/gain_ramp.h).
    float gain_now[6] = {1, 1, 1, 1, 1, 1};

    // What a channel is sounding, for display (the music player's keyboards,
    // issue #102). `on` while any operator is keyed on and it is not the DAC;
    // `hz` from its F-number and block; `level` 0 (silent) .. 1 from its
    // loudest carrier's envelope and total level. Reads only.
    struct Voice { bool on = false; double hz = 0; float level = 0; };
    Voice voice(int ch) const;

    // Save states (runtime/save_state.h): every field of the chip's state, in
    // a fixed order, through the archive `a`. Not gain, the host's.
    template <class A> void state_io(A& a) {
        for (Ch& c : ch_) {
            for (Op& o : c.op) {
                a(o.phase); a(o.inc); a(o.dt); a(o.mul); a(o.tl); a(o.ks); a(o.ar); a(o.am);
                a(o.d1r); a(o.d2r); a(o.sl); a(o.rr); a(o.ssg); a(o.env); a(o.state); a(o.key); a(o.out);
            }
            a(c.fnum); a(c.block); a(c.fnum_hi); a(c.alg); a(c.fb); a(c.ams); a(c.pms);
            a(c.left); a(c.right); a(c.fb_mem); a(c.mem);
        }
        a(addr_); a(ch3_fnum_); a(ch3_block_); a(ch3_hi_); a(ch3_special_); a(lfo_en_); a(lfo_rate_);
        a(lfo_cnt_); a(lfo_step_); a(dac_en_); a(dac_); a(eg_cnt_); a(eg_div_); a(ta_); a(tb_);
        a(timer_ctrl_); a(ta_cnt_); a(tb_cnt_); a(status_);
    }

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
        int32_t mem = 0;     // one-sample intermediate delay for algorithms 0-3 and 5
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
