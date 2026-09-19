#include "sn76489.h"

namespace chaotix {

namespace {
// 2 dB per step, 4 channels summed without clipping.
const int16_t kVolume[16] = {8191, 6507, 5168, 4105, 3261, 2590, 2057, 1642, 1298, 1031, 819, 650, 516, 410, 326, 0};
}

void Sn76489::reset() {
    *this = Sn76489{};
}

void Sn76489::write(uint8_t v) {
    if (v & 0x80) {
        latch_ = (v >> 4) & 7;
        int ch = latch_ >> 1;
        if (latch_ & 1) vol_[ch] = v & 15;
        else if (ch < 3) tone_[ch] = uint16_t((tone_[ch] & 0x3F0) | (v & 15));
        else { noise_ = v & 7; lfsr_ = 0x8000; }
        return;
    }
    int ch = latch_ >> 1;
    if (latch_ & 1) vol_[ch] = v & 15;
    else if (ch < 3) tone_[ch] = uint16_t((tone_[ch] & 15) | ((v & 0x3F) << 4));
    else { noise_ = v & 7; lfsr_ = 0x8000; }
}

int16_t Sn76489::run(int clocks) {
    int32_t acc = 0;
    int n = 0;
    // The PSG divides its input clock by 16 before the tone counters.
    div_ += clocks;
    while (div_ >= 16) {
        div_ -= 16;
        for (int c = 0; c < 3; ++c) {
            if (--counter_[c] <= 0) {
                counter_[c] = tone_[c] ? tone_[c] : 0x400;
                out_[c] ^= 1;
            }
        }
        if (--counter_[3] <= 0) {
            int rate = noise_ & 3;
            counter_[3] = rate == 3 ? (tone_[2] ? tone_[2] : 0x400) * 2 : (0x10 << rate) * 2;
            uint16_t fb = (noise_ & 4) ? uint16_t(((lfsr_ & 1) ^ ((lfsr_ >> 3) & 1))) : uint16_t(lfsr_ & 1);
            lfsr_ = uint16_t((lfsr_ >> 1) | (fb << 15));
            out_[3] = lfsr_ & 1;
        }
        int32_t s = 0;
        for (int c = 0; c < 3; ++c) s += out_[c] ? kVolume[vol_[c]] : -kVolume[vol_[c]];
        s += out_[3] ? kVolume[vol_[3]] : 0;
        acc += s;
        ++n;
    }
    if (n) last_ = int16_t(acc / n / 2);
    return last_;
}

} // namespace chaotix
