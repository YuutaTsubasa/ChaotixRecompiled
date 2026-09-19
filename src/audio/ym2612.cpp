#include "ym2612.h"
#include <algorithm>
#include <cmath>

namespace chaotix {

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Tables {
    uint16_t logsin[256];
    uint16_t exptab[256];
    Tables() {
        for (int i = 0; i < 256; ++i) {
            double s = std::sin((2.0 * i + 1.0) * kPi / 1024.0);
            logsin[i] = uint16_t(std::lround(-std::log2(s) * 256.0));
            exptab[i] = uint16_t(std::lround((std::pow(2.0, i / 256.0) - 1.0) * 1024.0));
        }
    }
};
const Tables& tables() {
    static const Tables t;
    return t;
}

// Detune (phase increment adjustment) by FD (0-3) and key code (0-31).
const uint8_t kDetune[4][32] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 8, 8, 8, 8},
    {1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 8, 8, 9, 10, 11, 12, 13, 14, 16, 16, 16, 16},
    {2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 8, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 20, 22, 22, 22, 22},
};

// Envelope increment patterns (MAME/Nemesis tables).
const uint8_t kEgInc[19][8] = {
    {0, 1, 0, 1, 0, 1, 0, 1}, {0, 1, 0, 1, 1, 1, 0, 1}, {0, 1, 1, 1, 0, 1, 1, 1}, {0, 1, 1, 1, 1, 1, 1, 1},
    {1, 1, 1, 1, 1, 1, 1, 1}, {1, 1, 1, 2, 1, 1, 1, 2}, {1, 2, 1, 2, 1, 2, 1, 2}, {1, 2, 2, 2, 1, 2, 2, 2},
    {2, 2, 2, 2, 2, 2, 2, 2}, {2, 2, 2, 4, 2, 2, 2, 4}, {2, 4, 2, 4, 2, 4, 2, 4}, {2, 4, 4, 4, 2, 4, 4, 4},
    {4, 4, 4, 4, 4, 4, 4, 4}, {4, 4, 4, 8, 4, 4, 4, 8}, {4, 8, 4, 8, 4, 8, 4, 8}, {4, 8, 8, 8, 4, 8, 8, 8},
    {8, 8, 8, 8, 8, 8, 8, 8}, {16, 16, 16, 16, 16, 16, 16, 16}, {0, 0, 0, 0, 0, 0, 0, 0},
};

inline int eg_row(int rate) {
    if (rate < 2) return 18;
    if (rate < 4) return rate;
    if (rate < 48) return rate & 3;
    if (rate < 60) return 4 + (rate - 48);
    return 16;
}
inline int eg_shift(int rate) { return rate < 48 ? 11 - (rate >> 2) : 0; }

const int kLfoPeriod[8] = {108, 77, 71, 67, 62, 44, 8, 5};
const double kPmCents[8] = {0, 3.4, 6.7, 10, 14, 20, 40, 80};
const int kAmShift[4] = {8, 3, 1, 0};

inline int keycode(uint16_t fnum, uint8_t block) {
    int n4 = (fnum >> 10) & 1, n3 = (fnum >> 9) & 1, n2 = (fnum >> 8) & 1, n1 = (fnum >> 7) & 1;
    int note = (n4 << 1) | ((n4 & (n3 | n2 | n1)) | ((n4 ^ 1) & n3 & n2 & n1));
    return (block << 2) | note;
}

} // namespace

Ym2612::Ym2612() { reset(); }

void Ym2612::reset() {
    for (auto& c : ch_) c = Ch{};
    addr_[0] = addr_[1] = 0;
    ch3_special_ = false;
    lfo_en_ = false;
    lfo_rate_ = 0;
    lfo_cnt_ = lfo_step_ = 0;
    dac_en_ = false;
    dac_ = 0;
    eg_cnt_ = 0;
    eg_div_ = 0;
    ta_ = 0; tb_ = 0; timer_ctrl_ = 0; ta_cnt_ = tb_cnt_ = 0; status_ = 0;
    for (int i = 0; i < 6; ++i) update_channel_freq(i);
}

uint8_t Ym2612::read_status() const { return status_ & 3; }

void Ym2612::write(int port, int addr_or_data, uint8_t v) {
    port &= 1;
    if (addr_or_data == 0) { addr_[port] = v; return; }
    write_reg(port, addr_[port], v);
}

void Ym2612::key(int c, int o, bool on) {
    Op& op = ch_[c].op[o];
    if (on && !op.key) {
        op.phase = 0;
        op.state = 0;
        int kc = keycode(ch_[c].fnum, ch_[c].block);
        int rate = op.ar ? std::min(63, op.ar * 2 + (kc >> (3 - op.ks))) : 0;
        if (rate >= 62) { op.env = 0; op.state = 1; }
    } else if (!on && op.key) {
        op.state = 3;
    }
    op.key = on;
}

void Ym2612::update_channel_freq(int c) {
    Ch& ch = ch_[c];
    for (int o = 0; o < 4; ++o) {
        uint16_t fnum = ch.fnum;
        uint8_t block = ch.block;
        if (c == 2 && ch3_special_ && o < 3) { fnum = ch3_fnum_[o]; block = ch3_block_[o]; }
        Op& op = ch.op[o];
        int kc = keycode(fnum, block);
        int32_t dt = kDetune[op.dt & 3][kc];
        if (op.dt & 4) dt = -dt;
        uint32_t base = (uint32_t(fnum) << block) >> 1;
        uint32_t inc = (base + uint32_t(dt)) & 0x1FFFF;
        inc = op.mul ? inc * op.mul : inc >> 1;
        op.inc = inc & 0xFFFFF;
    }
}

void Ym2612::write_reg(int bank, uint8_t reg, uint8_t v) {
    if (reg < 0x30) {
        if (bank != 0) return;
        switch (reg) {
        case 0x22: lfo_en_ = (v & 8) != 0; lfo_rate_ = v & 7; if (!lfo_en_) lfo_step_ = 0; break;
        case 0x24: ta_ = uint16_t((ta_ & 3) | (v << 2)); break;
        case 0x25: ta_ = uint16_t((ta_ & 0x3FC) | (v & 3)); break;
        case 0x26: tb_ = v; break;
        case 0x27: {
            bool special = (v & 0xC0) != 0;
            if (special != ch3_special_) { ch3_special_ = special; update_channel_freq(2); }
            if ((v & 1) && !(timer_ctrl_ & 1)) ta_cnt_ = 0;
            if ((v & 2) && !(timer_ctrl_ & 2)) tb_cnt_ = 0;
            if (v & 0x10) status_ &= ~1;
            if (v & 0x20) status_ &= ~2;
            timer_ctrl_ = v;
            break;
        }
        case 0x28: {
            int c = v & 3;
            if (c == 3) break;
            if (v & 4) c += 3;
            for (int o = 0; o < 4; ++o) key(c, o, (v >> (4 + o)) & 1);
            break;
        }
        case 0x2A: dac_ = (int32_t(v) - 128) << 6; break;
        case 0x2B: dac_en_ = (v & 0x80) != 0; break;
        default: break;
        }
        return;
    }
    int c = reg & 3;
    if (c == 3) return;
    c += bank * 3;
    Ch& ch = ch_[c];
    if (reg < 0xA0) {
        static const int slot_to_op[4] = {0, 2, 1, 3};
        Op& op = ch.op[slot_to_op[(reg >> 2) & 3]];
        switch (reg & 0xF0) {
        case 0x30: op.dt = (v >> 4) & 7; op.mul = v & 15; update_channel_freq(c); break;
        case 0x40: op.tl = v & 0x7F; break;
        case 0x50: op.ks = v >> 6; op.ar = v & 0x1F; break;
        case 0x60: op.am = v >> 7; op.d1r = v & 0x1F; break;
        case 0x70: op.d2r = v & 0x1F; break;
        case 0x80: op.sl = v >> 4; op.rr = v & 15; break;
        case 0x90: op.ssg = v & 15; break;
        default: break;
        }
        return;
    }
    switch (reg & 0xFC) {
    case 0xA0: ch.fnum = uint16_t(((ch.fnum_hi & 7) << 8) | v); ch.block = (ch.fnum_hi >> 3) & 7; update_channel_freq(c); break;
    case 0xA4: ch.fnum_hi = v & 0x3F; break;
    case 0xA8:
        if (bank == 0) {
            // A9 -> operator 1, AA -> operator 2, A8 -> operator 3
            int idx = (reg & 3) == 1 ? 0 : (reg & 3) == 2 ? 1 : 2;
            ch3_fnum_[idx] = uint16_t(((ch3_hi_ & 7) << 8) | v);
            ch3_block_[idx] = (ch3_hi_ >> 3) & 7;
            update_channel_freq(2);
        }
        break;
    case 0xAC: if (bank == 0) ch3_hi_ = v & 0x3F; break;
    case 0xB0: ch.fb = (v >> 3) & 7; ch.alg = v & 7; break;
    case 0xB4: ch.left = (v & 0x80) != 0; ch.right = (v & 0x40) != 0; ch.ams = (v >> 4) & 3; ch.pms = v & 7; break;
    default: break;
    }
}

void Ym2612::step_envelopes() {
    ++eg_cnt_;
    for (int c = 0; c < 6; ++c) {
        Ch& ch = ch_[c];
        int kc = keycode(ch.fnum, ch.block);
        for (auto& op : ch.op) {
            int r;
            switch (op.state) {
            case 0: r = op.ar; break;
            case 1: r = op.d1r; break;
            case 2: r = op.d2r; break;
            default: r = op.rr * 2 + 1; break;
            }
            int rate = r ? std::min(63, r * 2 + (kc >> (3 - op.ks))) : 0;
            int shift = eg_shift(rate);
            if (eg_cnt_ & ((1u << shift) - 1)) continue;
            int inc = kEgInc[eg_row(rate)][(eg_cnt_ >> shift) & 7];
            switch (op.state) {
            case 0:
                if (rate >= 62) op.env = 0;
                else op.env += ((~op.env) * inc) >> 4;
                if (op.env <= 0) { op.env = 0; op.state = 1; }
                break;
            case 1: {
                op.env += inc;
                int sl = op.sl == 15 ? 0x3E0 : op.sl << 5;
                if (op.env >= sl) op.state = 2;
                break;
            }
            default:
                op.env += inc;
                break;
            }
            if (op.env > 0x3FF) op.env = 0x3FF;
        }
    }
}

int Ym2612::op_output(Op& o, int c, int mod, int am) {
    const Tables& t = tables();
    int level = o.env + (o.tl << 3) + (o.am ? am : 0);
    if (level > 0x3FF) level = 0x3FF;
    uint32_t phase = ((o.phase >> 10) + uint32_t(mod)) & 0x3FF;
    uint32_t q = phase & 0xFF;
    if (phase & 0x100) q ^= 0xFF;
    uint32_t l = t.logsin[q] + (uint32_t(level) << 2);
    int32_t out;
    if ((l >> 8) >= 13) out = 0;
    else out = int32_t(((t.exptab[(l & 0xFF) ^ 0xFF] | 0x400) << 2) >> (l >> 8));
    if (phase & 0x200) out = -out;
    (void)c;
    return out;
}

void Ym2612::run_timers(int samples) {
    if (timer_ctrl_ & 1) {
        int period = 1024 - ta_;
        ta_cnt_ += samples;
        while (ta_cnt_ >= period) {
            ta_cnt_ -= period;
            if (timer_ctrl_ & 4) status_ |= 1;
            if ((timer_ctrl_ & 0xC0) == 0x80) {  // CSM: key on/off channel 3
                for (int o = 0; o < 4; ++o) { key(2, o, true); key(2, o, false); }
            }
        }
    }
    if (timer_ctrl_ & 2) {
        int period = (256 - tb_) * 16;
        tb_cnt_ += samples;
        while (tb_cnt_ >= period) {
            tb_cnt_ -= period;
            if (timer_ctrl_ & 8) status_ |= 2;
        }
    }
}

void Ym2612::tick_timers(int samples) {
    run_timers(samples);
}

void Ym2612::generate(int samples, std::vector<int16_t>& out) {
    run_timers(samples);
    for (int s = 0; s < samples; ++s) {
        // LFO
        int am_val = 0;
        double pm_tri = 0;
        if (lfo_en_) {
            if (++lfo_cnt_ >= kLfoPeriod[lfo_rate_]) { lfo_cnt_ = 0; lfo_step_ = (lfo_step_ + 1) & 127; }
            am_val = lfo_step_ < 64 ? lfo_step_ * 2 : (127 - lfo_step_) * 2;
            int p = lfo_step_ & 127;  // triangle -1..1 over 128 steps
            pm_tri = p < 32 ? p / 32.0 : p < 96 ? (64 - p) / 32.0 : (p - 128) / 32.0;
        }
        if (++eg_div_ >= 3) { eg_div_ = 0; step_envelopes(); }
        int32_t left = 0, right = 0;
        for (int c = 0; c < 6; ++c) {
            Ch& ch = ch_[c];
            int am = kAmShift[ch.ams] >= 8 ? 0 : (am_val >> kAmShift[ch.ams]);
            // Phase advance (with PM applied as a scaled increment).
            double pm = (lfo_en_ && ch.pms) ? std::pow(2.0, kPmCents[ch.pms] * pm_tri / 1200.0) : 1.0;
            for (auto& op : ch.op) op.phase = (op.phase + uint32_t(op.inc * pm)) & 0xFFFFF;
            int32_t fbmod = ch.fb ? (ch.fb_mem[0] + ch.fb_mem[1]) >> (10 - ch.fb) : 0;
            int32_t o1 = op_output(ch.op[0], c, fbmod, am);
            ch.fb_mem[0] = ch.fb_mem[1];
            ch.fb_mem[1] = o1;
            int32_t o2, o3, o4, acc;
            switch (ch.alg) {
            case 0: o2 = op_output(ch.op[1], c, o1 >> 1, am); o3 = op_output(ch.op[2], c, o2 >> 1, am); o4 = op_output(ch.op[3], c, o3 >> 1, am); acc = o4; break;
            case 1: o2 = op_output(ch.op[1], c, 0, am); o3 = op_output(ch.op[2], c, (o1 + o2) >> 1, am); o4 = op_output(ch.op[3], c, o3 >> 1, am); acc = o4; break;
            case 2: o2 = op_output(ch.op[1], c, 0, am); o3 = op_output(ch.op[2], c, o2 >> 1, am); o4 = op_output(ch.op[3], c, (o1 + o3) >> 1, am); acc = o4; break;
            case 3: o2 = op_output(ch.op[1], c, o1 >> 1, am); o3 = op_output(ch.op[2], c, 0, am); o4 = op_output(ch.op[3], c, (o2 + o3) >> 1, am); acc = o4; break;
            case 4: o2 = op_output(ch.op[1], c, o1 >> 1, am); o3 = op_output(ch.op[2], c, 0, am); o4 = op_output(ch.op[3], c, o3 >> 1, am); acc = o2 + o4; break;
            case 5: o2 = op_output(ch.op[1], c, o1 >> 1, am); o3 = op_output(ch.op[2], c, o1 >> 1, am); o4 = op_output(ch.op[3], c, o1 >> 1, am); acc = o2 + o3 + o4; break;
            case 6: o2 = op_output(ch.op[1], c, o1 >> 1, am); o3 = op_output(ch.op[2], c, 0, am); o4 = op_output(ch.op[3], c, 0, am); acc = o2 + o3 + o4; break;
            default: o2 = op_output(ch.op[1], c, 0, am); o3 = op_output(ch.op[2], c, 0, am); o4 = op_output(ch.op[3], c, 0, am); acc = o1 + o2 + o3 + o4; break;
            }
            if (c == 5 && dac_en_) acc = dac_;
            acc = std::clamp(acc, -8192, 8191);
            int32_t v = acc >> 5;  // 9-bit DAC
            if (ch.left) left += v;
            if (ch.right) right += v;
        }
        out.push_back(int16_t(std::clamp(left * 12, -32768, 32767)));
        out.push_back(int16_t(std::clamp(right * 12, -32768, 32767)));
    }
}

} // namespace chaotix
