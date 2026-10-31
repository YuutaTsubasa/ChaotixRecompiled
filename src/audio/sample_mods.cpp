// See sample_mods.h.
#include "audio/sample_mods.h"
#include "audio/pwm_samples.h"
#include "runtime/log.h"
#include "runtime/system.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace fs = std::filesystem;

namespace chaotix::audio {

namespace {

// "00" to "1F" (either case): the sample's number, or -1.
int sample_id(const std::string& stem) {
    if (stem.size() != 2 || !std::isxdigit((unsigned char)stem[0]) || !std::isxdigit((unsigned char)stem[1])) return -1;
    const int id = int(std::strtol(stem.c_str(), nullptr, 16));
    return id < 32 ? id : -1;
}

float peak_of(const Track& t) {
    int peak = 0;
    for (int16_t s : t.samples) peak = std::max(peak, std::abs(int(s)));
    return float(peak) / 32768.0f;
}

float peak_of(const std::vector<uint8_t>& pcm8) {
    int peak = 0;
    for (uint8_t b : pcm8) peak = std::max(peak, std::abs(int(b) - 128));
    return float(peak) / 128.0f;
}

uint32_t speed(const pwm::Sample& s) { return std::min(s.step, pwm::kStepOne); }

// Output samples from the 68K's note to the 32X's sound: the driver takes
// it at its next interrupt, and the FIFO holds what it wrote before
// (measured against the game's own drums: 5 to 7).
constexpr uint64_t kLatency = 6;

} // namespace

int SampleMods::scan(const std::string& dir) {
    std::error_code ec;
    for (fs::directory_iterator it(fs::u8path(dir), ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path& p = it->path();
        std::string ext = p.extension().u8string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
        const int id = ext == ".wav" ? sample_id(p.stem().u8string()) : -1;
        if (id < 0) continue;
        std::ifstream f(p, std::ios::binary);
        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        Track t;
        std::string error;
        if (bytes.empty() || !decode_wav(bytes, kAudioRate, t, &error) || !t.frames) {
            LOGW("samples", "cannot use %s: %s", p.u8string().c_str(), error.empty() ? "cannot read it" : error.c_str());
            continue;
        }
        files_[uint8_t(id)] = std::move(t);
    }
    return int(files_.size());
}

void SampleMods::set_sample(uint8_t id, Track track) { files_[id] = std::move(track); }

void SampleMods::attach(Machine& m) {
    uses_ = {};
    voices_ = {};
    m.pwm_replaced = {};
    m.pwm_replacing = false;
    m.pwm_events.clear();
    if (files_.empty()) return;
    const auto table = pwm::read_table(m);
    for (size_t i = 0; i < table.size() && i < uses_.size(); ++i) {
        if (i == pwm::kSilence) continue;
        // Its own file, or the file of a sample made from the same recording.
        size_t from = i;
        if (!files_.count(uint8_t(i))) {
            from = table.size();
            for (size_t j = 0; j < table.size() && from == table.size(); ++j)
                if (table[j].start == table[i].start && files_.count(uint8_t(j))) from = j;
            if (from == table.size()) continue;
        }
        const Track& t = files_.at(uint8_t(from));
        const float file_peak = peak_of(t);
        const float game_peak = peak_of(pwm::read_bytes(m, table[from]));
        Use& u = uses_[i];
        u.track = &t;
        u.rate = double(speed(table[i])) / double(speed(table[from]));
        u.gain = file_peak > 0 ? game_peak / file_peak : 0.0f;
        m.pwm_replaced[i] = true;
        m.pwm_replacing = true;
        LOGI("samples", "sample %02zX played from %02zX.wav%s (level x%.2f)", i, from, from != i ? ", slowed" : "",
             double(u.gain));
    }
}

void SampleMods::strike(uint8_t voice, uint16_t command) {
    if (voice >= voices_.size()) return;
    const pwm::Note n = pwm::decode_note(command);
    Voice& v = voices_[voice];
    v = {};
    if (n.sample < uses_.size() && uses_[n.sample].track) {
        v.use = &uses_[n.sample];
        v.left = float(n.left);
        v.right = float(n.right);
    }
}

void SampleMods::after_frame(Machine& m, size_t first_sample) {
    if (!m.pwm_replacing) return;
    auto& notes = m.pwm_events;
    const size_t total = m.audio_out.size();
    if (!m.audio_enabled || first_sample > total) {
        for (const auto& n : notes) strike(n.voice, n.command);
        notes.clear();
        return;
    }
    const size_t frames = (total - first_sample) / 2;
    const uint64_t start = m.audio_pos >= frames ? m.audio_pos - frames : 0;
    // The driver adds a sample byte (+-128) times the volume (2-32) / 16 to
    // the pulse width, centred on half the PWM cycle; the machine turns
    // the width into +-32768 over the cycle and mixes it at 7/8
    // (Machine::audio_advance). A file's +-1 is the byte's +-128.
    const float unit = m.pwm.cycle > 1 ? 128.0f / 16.0f * 65536.0f / float(m.pwm.cycle) * m.pwm_gain_now * 0.875f : 0.0f;
    size_t next = 0;
    for (size_t k = 0; k < frames; ++k) {
        while (next < notes.size() && notes[next].mclk / kMclkPerAudioSample + kLatency <= start + k) {
            strike(notes[next].voice, notes[next].command);
            ++next;
        }
        float out[2] = {0, 0};
        for (Voice& v : voices_) {
            if (!v.use) continue;
            const Track& t = *v.use->track;
            const size_t i = size_t(v.pos);
            if (i + 1 >= t.frames) { v.use = nullptr; continue; }
            const float f = float(v.pos - double(i));
            for (int ch = 0; ch < 2; ++ch) {
                const float a = t.samples[i * 2 + size_t(ch)], b = t.samples[(i + 1) * 2 + size_t(ch)];
                out[ch] += (a + (b - a) * f) / 32768.0f * v.use->gain * (ch ? v.right : v.left);
            }
            v.pos += v.use->rate;
        }
        for (int ch = 0; ch < 2; ++ch) {
            int16_t& s = m.audio_out[first_sample + k * 2 + size_t(ch)];
            s = int16_t(std::clamp(int(s) + int(std::lround(out[ch] * unit)), -32768, 32767));
        }
    }
    // Notes written after the sound made so far wait for the next frame.
    notes.erase(notes.begin(), notes.begin() + std::ptrdiff_t(next));
}

} // namespace chaotix::audio
