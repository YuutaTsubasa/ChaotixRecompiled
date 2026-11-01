// See music_mods.h.
#include "audio/music_mods.h"
#include "runtime/log.h"
#include "runtime/patches.h"
#include "runtime/system.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

namespace fs = std::filesystem;

namespace chaotix::audio {

namespace {

uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }

// The driver fades its music out over about five seconds ($F0, measured).
constexpr double kFadeSeconds = 5.0;

bool fail(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}

bool read_file(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

// "05" -> 0x05; -1 for anything that is not a music id.
int music_id(const std::string& stem) {
    if (stem.size() != 2 || !std::isxdigit(uint8_t(stem[0])) || !std::isxdigit(uint8_t(stem[1]))) return -1;
    const int id = int(std::strtol(stem.c_str(), nullptr, 16));
    return id >= 1 && id < patches::kFirstSoundEffect ? id : -1;
}

struct WavFormat {
    int format = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
};

// The "fmt " chunk's body (at least 16 bytes); an extensible one's sub-format.
WavFormat read_fmt(const uint8_t* body, size_t avail) {
    WavFormat f;
    f.format = le16(body);
    f.channels = le16(body + 2);
    f.rate = le32(body + 4);
    f.bits = le16(body + 14);
    if (f.format == 0xFFFE && avail >= 26) f.format = le16(body + 24);
    return f;
}

// Whether decode_wav can play this format and amount of data.
bool check_format(const WavFormat& f, size_t data_size, std::string* error) {
    if (f.format != 1 && f.format != 3) return fail(error, "unsupported encoding (only PCM and float WAV)");
    if (f.channels < 1 || f.rate == 0) return fail(error, "bad format chunk");
    if (f.format == 1 && f.bits != 8 && f.bits != 16 && f.bits != 24 && f.bits != 32)
        return fail(error, "unsupported sample size");
    if (f.format == 3 && f.bits != 32) return fail(error, "unsupported float sample size");
    if (data_size / (size_t(f.bits / 8) * size_t(f.channels)) == 0) return fail(error, "no audio data");
    return true;
}

} // namespace

bool check_wav_file(const std::string& path, std::string* error) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f) return fail(error, "cannot read it");
    f.seekg(0, std::ios::end);
    const std::streamoff end = f.tellg();
    f.seekg(0);
    uint8_t head[12];
    if (end < 12 || !f.read(reinterpret_cast<char*>(head), 12) || std::memcmp(head, "RIFF", 4) != 0 ||
        std::memcmp(head + 8, "WAVE", 4) != 0)
        return fail(error, "not a WAV file");
    // The chunks as decode_wav walks them, reading only their headers and
    // the format, not the samples.
    WavFormat format;
    bool have_data = false;
    size_t data_size = 0;
    for (std::streamoff p = 12; p + 8 <= end;) {
        uint8_t h[8];
        f.seekg(p);
        if (!f.read(reinterpret_cast<char*>(h), 8)) break;
        const size_t size = le32(h + 4);
        const std::streamoff body = p + 8;
        const size_t avail = std::min(size, size_t(end - body));
        if (std::memcmp(h, "fmt ", 4) == 0 && avail >= 16) {
            uint8_t b[26] = {};
            const size_t n = std::min(avail, sizeof b);
            if (!f.read(reinterpret_cast<char*>(b), std::streamsize(n))) break;
            format = read_fmt(b, n);
        } else if (std::memcmp(h, "data", 4) == 0) {
            have_data = true;
            data_size = avail;
        }
        p = body + std::streamoff(size) + std::streamoff(size & 1);
    }
    if (!have_data) return fail(error, "no audio data");
    return check_format(format, data_size, error);
}

bool decode_wav(const std::vector<uint8_t>& b, double out_rate, Track& out, std::string* error) {
    if (b.size() < 12 || std::string(b.begin(), b.begin() + 4) != "RIFF" || std::string(b.begin() + 8, b.begin() + 12) != "WAVE")
        return fail(error, "not a WAV file");
    WavFormat fmt;
    const uint8_t* data = nullptr;
    size_t data_size = 0;
    bool have_loop = false;
    uint32_t loop_start = 0, loop_end = 0;
    for (size_t p = 12; p + 8 <= b.size();) {
        const std::string id(b.begin() + long(p), b.begin() + long(p) + 4);
        const size_t size = le32(&b[p + 4]);
        const size_t body = p + 8;
        const size_t avail = std::min(size, b.size() - body);
        if (id == "fmt " && avail >= 16) {
            fmt = read_fmt(&b[body], avail);
        } else if (id == "data") {
            data = &b[body];
            data_size = avail;
        } else if (id == "smpl" && avail >= 36 + 24 && le32(&b[body + 28]) > 0) {
            have_loop = true;
            loop_start = le32(&b[body + 36 + 8]);
            loop_end = le32(&b[body + 36 + 12]);  // inclusive
        }
        p = body + size + (size & 1);
    }
    if (!data) return fail(error, "no audio data");
    if (!check_format(fmt, data_size, error)) return false;
    const int format = fmt.format, channels = fmt.channels, bits = fmt.bits;
    const uint32_t rate = fmt.rate;
    const size_t bytes = size_t(bits / 8);
    const size_t in_frames = data_size / (bytes * size_t(channels));
    auto sample = [&](size_t frame, int ch) -> float {
        const uint8_t* s = data + (frame * size_t(channels) + size_t(std::min(ch, channels - 1))) * bytes;
        if (format == 3) {
            float f;
            const uint32_t u = le32(s);
            std::memcpy(&f, &u, 4);
            return f;
        }
        switch (bits) {
        case 8: return (float(s[0]) - 128.0f) / 128.0f;
        case 16: return float(int16_t(le16(s))) / 32768.0f;
        case 24: return float(int32_t(uint32_t(s[0]) << 8 | uint32_t(s[1]) << 16 | uint32_t(s[2]) << 24) >> 8) / 8388608.0f;
        default: return float(int32_t(le32(s))) / 2147483648.0f;
        }
    };
    // Linear resampling, once, at load.
    const double step = double(rate) / out_rate;
    const size_t out_frames = size_t(double(in_frames) / step);
    out.samples.assign(out_frames * 2, 0);
    for (size_t i = 0; i < out_frames; ++i) {
        const double at = double(i) * step;
        const size_t a = size_t(at);
        const size_t c = std::min(a + 1, in_frames - 1);
        const float t = float(at - double(a));
        for (int ch = 0; ch < 2; ++ch) {
            const float v = sample(a, ch) * (1.0f - t) + sample(c, ch) * t;
            out.samples[i * 2 + size_t(ch)] = int16_t(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f));
        }
    }
    out.frames = out_frames;
    out.loops = false;
    if (have_loop && loop_start < loop_end && loop_start < in_frames) {
        out.loops = true;
        out.loop_start = std::min(size_t(double(loop_start) / step), out_frames - 1);
        out.loop_end = std::clamp(size_t(double(uint64_t(loop_end) + 1) / step), out.loop_start + 1, out_frames);
    }
    return true;
}

bool apply_loop_text(const std::string& text, double in_rate, double out_rate, Track& track) {
    std::istringstream in(text);
    long long start = -1, end = -1;
    if (!(in >> start) || start < 0 || track.frames == 0) return false;
    if (!(in >> end)) end = -1;
    const double scale = out_rate / in_rate;
    track.loops = true;
    track.loop_start = std::min(size_t(double(start) * scale), track.frames - 1);
    track.loop_end = end > start ? std::clamp(size_t(double(end + 1) * scale), track.loop_start + 1, track.frames) : track.frames;
    return true;
}

int MusicMods::scan(const std::string& dir) {
    std::error_code ec;
    for (fs::directory_iterator it(fs::u8path(dir), ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path& p = it->path();
        std::string ext = p.extension().u8string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
        const int id = ext == ".wav" ? music_id(p.stem().u8string()) : -1;
        if (id <= 0) continue;
        // A file it could not play would still tell the game to leave the
        // piece to it, and the piece would be silent (issue #122): only one
        // that can be played replaces the game's music.
        std::string error;
        if (!check_wav_file(p.u8string(), &error)) {
            LOGW("music", "not using %s: %s", p.u8string().c_str(), error.c_str());
            continue;
        }
        files_[uint8_t(id)] = p.u8string();
    }
    return int(files_.size());
}

void MusicMods::set_track(uint8_t id, Track t) {
    files_.erase(id);  // a track set here is never evicted
    tracks_[id] = std::move(t);
}

bool MusicMods::has(uint8_t id) const { return files_.count(id) || tracks_.count(id); }

void MusicMods::attach(Machine& m) const {
    for (size_t id = 0; id < m.music_replaced.size(); ++id) m.music_replaced[id] = has(uint8_t(id));
}

const Track* MusicMods::track(uint8_t id) {
    auto t = tracks_.find(id);
    if (t != tracks_.end()) return &t->second;
    auto f = files_.find(id);
    if (f == files_.end()) return nullptr;
    // Decoded when first asked for. Keep what is playing and what a jingle
    // interrupted; any other file can be decoded again when it is wanted.
    for (auto it = tracks_.begin(); it != tracks_.end();) {
        if (it->first != current_ && it->first != held_ && files_.count(it->first)) it = tracks_.erase(it);
        else ++it;
    }
    std::vector<uint8_t> bytes;
    Track loaded;
    std::string error;
    if (!read_file(f->second, bytes) || !decode_wav(bytes, kAudioRate, loaded, &error)) {
        LOGW("music", "cannot use %s: %s", f->second.c_str(), error.empty() ? "cannot read it" : error.c_str());
        files_.erase(f);
        return nullptr;
    }
    const fs::path loop_path = fs::u8path(f->second).replace_extension(".loop");
    std::vector<uint8_t> loop_bytes;
    if (read_file(loop_path.u8string(), loop_bytes)) {
        const uint32_t in_rate = bytes.size() >= 28 ? le32(&bytes[24]) : 0;
        if (!in_rate || !apply_loop_text(std::string(loop_bytes.begin(), loop_bytes.end()), in_rate, kAudioRate, loaded))
            LOGW("music", "ignoring %s: expected \"<first frame> [<last frame>]\"", loop_path.u8string().c_str());
    }
    LOGI("music", "loaded %s (%.1f s%s)", f->second.c_str(), double(loaded.frames) / kAudioRate, loaded.loops ? ", loops" : "");
    return &(tracks_[id] = std::move(loaded));
}

void MusicMods::on_request(uint8_t id) {
    if (id == patches::kSoundStopMusic) {
        current_ = held_ = 0;
        fade_frames_ = 0;
        return;
    }
    if (id == patches::kSoundLowerMusic) {
        if (current_ && !fade_frames_) fade_frames_ = fade_left_ = int(kFadeSeconds * kAudioRate);
        return;
    }
    if (!has(id)) {
        // The driver plays this one; keep our place in the replaced piece.
        LOGI("music", "the game asks for music %02X", id);
        if (current_) { held_ = current_; held_position_ = position_; }
        current_ = 0;
        fade_frames_ = 0;
        return;
    }
    LOGI("music", "the game asks for music %02X: playing %02X.wav instead", id, id);
    fade_frames_ = 0;
    if (id == current_) return;
    if (id == held_) {
        current_ = id;
        position_ = held_position_;
    } else {
        current_ = id;
        position_ = 0;
    }
    held_ = 0;
    if (!track(id)) current_ = 0;
}

namespace {
constexpr uint8_t kPositionVersion = 1;
constexpr size_t kPositionSize = 1 + 1 + 1 + 8 + 8 + 4 + 4;
} // namespace

std::vector<uint8_t> MusicMods::save_position() const {
    std::vector<uint8_t> b;
    auto put = [&b](uint64_t v, int n) { for (int i = 0; i < n; ++i) b.push_back(uint8_t(v >> (8 * i))); };
    put(kPositionVersion, 1);
    put(current_, 1);
    put(held_, 1);
    put(position_, 8);
    put(held_position_, 8);
    put(uint32_t(fade_frames_), 4);
    put(uint32_t(fade_left_), 4);
    return b;
}

void MusicMods::load_position(const std::vector<uint8_t>& b) {
    if (b.size() != kPositionSize || b[0] != kPositionVersion) return;
    size_t at = 1;
    auto get = [&](int n) { uint64_t v = 0; for (int i = 0; i < n; ++i) v |= uint64_t(b[at++]) << (8 * i); return v; };
    current_ = uint8_t(get(1));
    held_ = uint8_t(get(1));
    position_ = size_t(get(8));
    held_position_ = size_t(get(8));
    fade_frames_ = int(int32_t(get(4)));
    fade_left_ = int(int32_t(get(4)));
    // A piece that has no file now (the folder changed) plays nothing; one
    // past its end starts again from its loop or stops, as after_frame does.
    if (current_ && !track(current_)) current_ = 0;
}

void MusicMods::after_frame(Machine& m, size_t first_sample) {
    for (uint8_t id : m.music_events) on_request(id);
    m.music_events.clear();
    if (!current_ || m.zram[patches::kZ80Paused]) return;
    const Track* t = track(current_);
    if (!t) { current_ = 0; return; }
    const float gain = float(volume_) / 100.0f;
    for (size_t i = first_sample; i + 1 < m.audio_out.size(); i += 2) {
        if (position_ >= (t->loops ? t->loop_end : t->frames)) {
            if (!t->loops) { current_ = 0; return; }
            position_ = t->loop_start;
        }
        float g = gain;
        if (fade_frames_) {
            g *= float(fade_left_) / float(fade_frames_);
            if (--fade_left_ <= 0) { current_ = 0; fade_frames_ = 0; return; }
        }
        for (int ch = 0; ch < 2; ++ch) {
            const int v = m.audio_out[i + size_t(ch)] + int(std::lround(float(t->samples[position_ * 2 + size_t(ch)]) * g));
            m.audio_out[i + size_t(ch)] = int16_t(std::clamp(v, -32768, 32767));
        }
        ++position_;
    }
}

} // namespace chaotix::audio
