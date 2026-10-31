// See pwm_samples.h.
#include "audio/pwm_samples.h"
#include "runtime/system.h"
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace chaotix::audio::pwm {

namespace {

constexpr int kSlave = 1;
// The SH-2s' clock: the Mega Drive's master clock times 3/7.
constexpr double kSh2Clock = 53693175.0 * 3.0 / 7.0;

void put32(std::vector<uint8_t>& b, uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i)));
}
void put16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(uint8_t(v));
    b.push_back(uint8_t(v >> 8));
}

} // namespace

std::vector<Sample> read_table(Machine& m) {
    std::vector<Sample> out;
    uint32_t table = m.sh2_read(kSlave, kTablePointer, 4);
    if (!table) table = kTable;  // before the driver has started
    // The samples' bytes follow the table: it ends where the first begins
    // (22 entries in the game).
    uint32_t end = table + uint32_t(kSamples) * 16;
    for (uint32_t at = table; at < end; at += 16) {
        const Sample s{m.sh2_read(kSlave, at, 4), m.sh2_read(kSlave, at + 4, 4), m.sh2_read(kSlave, at + 8, 4),
                       m.sh2_read(kSlave, at + 12, 4)};
        if (!s.valid()) break;
        if (s.start > at && s.start < end) end = s.start;
        out.push_back(s);
    }
    return out;
}

std::vector<uint8_t> read_bytes(Machine& m, const Sample& s) {
    std::vector<uint8_t> out;
    if (!s.valid()) return out;
    out.reserve(s.length);
    for (uint32_t i = 0; i < s.length; ++i) out.push_back(uint8_t(m.sh2_read(kSlave, s.start + i, 1)));
    return out;
}

double sample_rate(const Machine& m, const Sample& s) {
    if (m.pwm.cycle <= 1) return 0;
    // Each interrupt puts two values in the FIFO, which takes one a PWM
    // cycle: with the timer at 1, as the game sets it, the driver waits for
    // room and so runs every other cycle (measured: 183.5 bytes a frame at
    // step 0x800, 11 kHz).
    int timer = (m.pwm.ctrl >> 8) & 0xF ? (m.pwm.ctrl >> 8) & 0xF : 16;
    if (timer < 2) timer = 2;
    const double interrupts = kSh2Clock / double(m.pwm.cycle - 1) / timer;
    return interrupts * double(s.step < kStepOne ? s.step : kStepOne) / kStepOne;
}

std::vector<uint8_t> wav_bytes(const std::vector<uint8_t>& pcm8, int rate) {
    std::vector<uint8_t> b;
    b.insert(b.end(), {'R', 'I', 'F', 'F'});
    put32(b, uint32_t(36 + pcm8.size()));
    b.insert(b.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put32(b, 16);
    put16(b, 1);  // PCM
    put16(b, 1);  // mono
    put32(b, uint32_t(rate));
    put32(b, uint32_t(rate));
    put16(b, 1);
    put16(b, 8);
    b.insert(b.end(), {'d', 'a', 't', 'a'});
    put32(b, uint32_t(pcm8.size()));
    b.insert(b.end(), pcm8.begin(), pcm8.end());
    if (pcm8.size() & 1) b.push_back(0);
    return b;
}

int dump(Machine& m, const std::string& dir) {
    const auto table = read_table(m);
    if (table.empty()) {
        std::printf("pwm samples: the driver has no table yet\n");
        return 0;
    }
    std::error_code ec;
    fs::create_directories(fs::u8path(dir), ec);
    int written = 0;
    std::printf("pwm samples: table at %08X, PWM cycle %u, timer %u\n", m.sh2_read(kSlave, kTablePointer, 4),
                m.pwm.cycle, (m.pwm.ctrl >> 8) & 0xF);
    for (size_t i = 0; i < table.size(); ++i) {
        const Sample& s = table[i];
        const double rate = sample_rate(m, s);
        std::printf("  %02zX: start %08X length %6u loop %08X step %04X  %.0f Hz %.3f s\n", i, s.start, s.length, s.loop,
                    s.step, rate, rate > 0 ? s.length / rate : 0.0);
        if (!s.valid() || rate <= 0) continue;
        char name[16];
        std::snprintf(name, sizeof name, "%02zX.wav", i);
        const auto bytes = wav_bytes(read_bytes(m, s), int(rate + 0.5));
        std::ofstream f(fs::u8path(dir) / name, std::ios::binary);
        if (!f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()))) continue;
        ++written;
    }
    std::printf("pwm samples: %d written to %s\n", written, dir.c_str());
    return written;
}

} // namespace chaotix::audio::pwm
