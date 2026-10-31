// See jukebox.h.
#include "frontend/jukebox.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "audio/volumes.h"
#include "game/recomp_dispatch.h"
#include "input/input.h"
#include "runtime/patches.h"
#include "runtime/system.h"

namespace chaotix::jukebox {

namespace {

// The SOUND TEST's code, as ROM offsets of the 68K's $880000 window.
constexpr uint32_t kTitlesLea = 0x00751E;   // lea $927338.l, a0: 41F9 + address
constexpr uint32_t kFirstEffect = 0x007524; // cmpi.w #$2A, d0: 0C40 + word
// The game mode of OPTIONS ($88697A) and its sub-mode for the OPTIONS list
// ($886992); 4 is SOUND TEST, 8 COLOR TEST. See jukebox.h for why the list
// rather than the SOUND TEST itself.
constexpr int kModeOptions = 0x40;
constexpr int kSubOptionsList = 0;
// The list's loop: from $8869C2 it waits for the vertical blank ($8869C8)
// and reads the pad, so the end of every frame finds its 68K in here once
// the scene has set itself up.
constexpr uint32_t kLoopFirst = 0x8869C2, kLoopEnd = 0x886A8A;
constexpr int kSettleFrames = 3;
constexpr int kBootFramesPerFrame = 6;
constexpr uint64_t kGiveUp = 900;
// Z80 RAM: the driver's three request slots ($8F6E8A writes them) and the
// byte the request routine sets for music ($8F6E96; the SOUND TEST's
// left/right-with-a-button changes it, the driver's tempo).
constexpr uint16_t kQueue = 0x1C0A;
constexpr uint16_t kMusicFlag = 0x003F;

constexpr double kFrameRate = 53693175.0 / (3420.0 * 262.0);
constexpr int kLongestName = 32;
// Quieter than this is silence (peak, of 32767); a track that has made a
// sound and then been silent this long has ended.
constexpr int kSilence = 48;
constexpr double kEndedAfterSilence = 3.0;
// A track that has made no sound at all this long is taken to have none.
constexpr double kNeverStarted = 10.0;
// The driver's fade ($F0) takes about five seconds.
constexpr double kFadeSeconds = 6.0;

bool printable_name(const std::vector<uint8_t>& d, size_t at, size_t end, std::string& out, size_t& next) {
    out.clear();
    for (size_t p = at; p < end; ++p) {
        const uint8_t ch = d[p];
        if (!ch) {
            next = p + 1;
            return !out.empty() && out.size() <= size_t(kLongestName);
        }
        if (ch < 0x20 || ch > 0x7E || out.size() > size_t(kLongestName)) return false;
        out.push_back(char(ch));
    }
    return false;
}

std::vector<Track> fallback() {
    std::vector<Track> t;
    for (int id = 1; id < patches::kFirstSoundEffect; ++id) {
        char name[16];
        std::snprintf(name, sizeof name, "MUSIC %02X", id);
        t.push_back({uint8_t(id), name});
    }
    return t;
}

} // namespace

std::vector<Track> track_list(const Rom& rom, bool* from_rom) {
    if (from_rom) *from_rom = false;
    const std::vector<uint8_t>& d = rom.data;
    const size_t end = std::min(d.size(), rom.file_size);
    if (end < kFirstEffect + 4 || rom.read16(kTitlesLea) != 0x41F9 || rom.read16(kFirstEffect) != 0x0C40) return fallback();
    const uint32_t addr = rom.read32(kTitlesLea + 2);
    const int count = rom.read16(kFirstEffect + 2);  // entries 1..count-1 are music
    if (addr < 0x900000 || addr >= 0xA00000 || count < 2 || count > patches::kFirstSoundEffect) return fallback();
    // The $900000 window shows one 1 MB bank of the ROM; the SOUND TEST's
    // titles sit in whichever bank holds all of them as text.
    for (uint32_t bank = 0; bank < 4; ++bank) {
        size_t at = bank * 0x100000u + (addr - 0x900000u);
        if (at >= end) continue;
        std::vector<Track> tracks;
        std::string name;
        size_t next = 0;
        bool ok = printable_name(d, at, end, name, next);  // the header
        for (int id = 1; ok && id < count; ++id) {
            at = next;
            ok = printable_name(d, at, end, name, next);
            if (!ok) break;
            // The font draws an apostrophe for '=' ($887586).
            std::replace(name.begin(), name.end(), '=', '\'');
            while (!name.empty() && name.back() == ' ') name.pop_back();
            tracks.push_back({uint8_t(id), name});
        }
        if (ok) {
            if (from_rom) *from_rom = true;
            return tracks;
        }
    }
    return fallback();
}

std::string track_label(const Track& t) {
    char num[8];
    std::snprintf(num, sizeof num, "%02d ", int(t.id));
    return num + t.name;
}

std::string format_time(double seconds) {
    const int s = std::max(0, int(seconds));
    char buf[16];
    std::snprintf(buf, sizeof buf, "%d:%02d", s / 60, s % 60);
    return buf;
}

const char* repeat_name(Repeat r) {
    switch (r) {
    case Repeat::Once: return "ONCE";
    case Repeat::All: return "ALL";
    default: return "LOOP";
    }
}


Player::Player() = default;
Player::~Player() { stop(); }

void Player::begin(const Rom& rom, const audio::MusicMods* music, bool recompiled, const audio::SampleMods* samples) {
    stop();
    auto m = std::make_unique<Machine>();
    m->load_rom(Rom(rom));
    m->reset();
    m->audio_enabled = true;
    music_ = music ? *music : audio::MusicMods{};
    music_.attach(*m);
    samples_ = samples ? *samples : audio::SampleMods{};
    samples_.attach(*m);
    // Whatever the copy was playing for the game stops (after_frame takes it).
    m->music_events.push_back(patches::kSoundStopMusic);
    m->mode_request = kModeOptions;
    m->submode_request = kSubOptionsList;
    m_ = std::move(m);
    recompiled_ = false;
    if (recompiled) {
        Machine* before = bind_recompiled_code(*m_);
        recompiled_ = install_recompiled_code(*m_, true).active;
        if (before) bind_recompiled_code(*before);
    }
    ready_ = failed_ = false;
    settled_ = 0;
    boot_frames_ = 0;
    fading_ = heard_ = false;
}

int Player::boot(int max_frames) {
    if (!m_) return -1;
    if (ready_) return 1;
    Machine* before = recompiled_ ? bind_recompiled_code(*m_) : nullptr;
    // To OPTIONS: the request is taken at the dispatch after the SEGA screen,
    // then the scene sets itself up.
    for (int i = 0; i < max_frames && settled_ < kSettleFrames && m_->frame_count < kGiveUp; ++i) {
        run_one(nullptr);
        const uint32_t pc = m_->m68k.pc & 0xFFFFFF;
        const bool in_loop = m_->mode_request < 0 && pc >= kLoopFirst && pc < kLoopEnd;
        settled_ = in_loop ? settled_ + 1 : 0;
    }
    if (before && before != m_.get()) bind_recompiled_code(*before);
    if (settled_ >= kSettleFrames) {
        ready_ = true;
        boot_frames_ = m_->frame_count;
        return 1;
    }
    if (m_->frame_count >= kGiveUp) {
        m_.reset();
        failed_ = true;
        return -1;
    }
    return 0;
}

bool Player::start(const Rom& rom, const audio::MusicMods* music, bool recompiled, std::string* why,
                   const audio::SampleMods* samples) {
    begin(rom, music, recompiled, samples);
    int r = 0;
    while ((r = boot(60)) == 0) {}
    if (r < 0 && why) *why = "the game did not reach its OPTIONS screen";
    return r > 0;
}

void Player::stop() {
    m_.reset();
    ready_ = false;
    queue_.clear();
    id_ = 0;
}

void Player::request(uint8_t id) { queue_.push_back(id); }

// $8F6E76 on the host, between frames (where the game's own call could have
// held the Z80's bus just the same), with the hook at kSoundRequest in it.
void Player::send(uint8_t id) {
    Machine& m = *m_;
    // The hook (runtime/patches.cpp): music and the two commands reach the
    // music mods, and a piece a mod replaces stops the driver's music instead.
    if (id != 0 && (id < patches::kFirstSoundEffect || id == patches::kSoundLowerMusic || id == patches::kSoundStopMusic)) {
        m.music_events.push_back(id);
        if (id < patches::kFirstSoundEffect && m.music_replaced[id]) id = patches::kSoundStopMusic;
    }
    // The routine: music fills all three of the driver's slots (and resets
    // the byte at $A0003F); anything else takes the first free one, or is
    // dropped when there is none.
    if (id < patches::kFirstSoundEffect) {
        m.zram[kMusicFlag] = 0x80;
        for (int i = 0; i < 3; ++i) m.zram[kQueue + i] = id;
        return;
    }
    for (int i = 0; i < 3; ++i)
        if (!m.zram[kQueue + i]) {
            m.zram[kQueue + i] = id;
            return;
        }
}

void Player::play(uint8_t id) {
    if (!m_ || !id || id >= patches::kFirstSoundEffect) return;
    request(id);
    id_ = last_id_ = id;
    started_at_ = m_->frame_count;
    heard_ = fading_ = false;
}

void Player::stop_music() {
    if (!m_) return;
    request(patches::kSoundStopMusic);
    id_ = 0;
    fading_ = false;
}

void Player::fade_out() {
    if (!m_ || !id_ || fading_) return;
    request(patches::kSoundLowerMusic);
    fading_ = true;
    fade_at_ = m_->frame_count;
}

void Player::next(int delta) {
    if (tracks_.empty()) return;
    int at = -1;
    const uint8_t from = id_ ? id_ : last_id_;
    for (size_t i = 0; i < tracks_.size(); ++i)
        if (tracks_[i].id == from) at = int(i);
    const int n = int(tracks_.size());
    at = at < 0 ? (delta > 0 ? 0 : n - 1) : ((at + delta) % n + n) % n;
    play(tracks_[size_t(at)].id);
}

void Player::set_levels(float music, float effects, int mod_percent) {
    music_level_ = music;
    effects_level_ = effects;
    music_.set_volume(mod_percent);
}

void Player::track_ended() {
    switch (repeat_) {
    case Repeat::Loop: play(id_); break;      // a jingle starts again
    case Repeat::Once: stop_music(); break;
    case Repeat::All: next(+1); break;
    }
}

int note_of(double hz) {
    if (!(hz > 8.0) || hz > 20000.0) return -1;
    return int(std::lround(69.0 + 12.0 * std::log2(hz / 440.0)));
}

void Player::read_keys() {
    const Machine& m = *m_;
    for (int c = 0; c < 6; ++c) {
        const Ym2612::Voice v = m.ym.voice(c);
        keys_.fm[c] = v.on && v.level > 0 ? Keys::Key{note_of(v.hz), v.level} : Keys::Key{};
    }
    for (int c = 0; c < 3; ++c) {
        const float level = m.psg.tone_level(c);
        keys_.psg[c] = level > 0 ? Keys::Key{note_of(m.psg.tone_hz(c)), level} : Keys::Key{};
    }
    // A struck pad fades out over about a quarter of a second.
    for (uint8_t& b : keys_.pwm) b = uint8_t(b > 16 ? b - 16 : 0);
    // The 68K writes the four voices' notes ($8F6EFA: two bytes each, the
    // second the sample) once a frame; zero is no new note.
    if (m.pwm_note_writes != pwm_writes_) {
        pwm_writes_ = m.pwm_note_writes;
        for (int v = 0; v < 4; ++v) {
            const uint8_t sample = m.pwm_notes[2 * v + 1];
            if (!m.pwm_notes[2 * v] && !sample) continue;
            keys_.pwm[sample % Keys::kPwmSamples] = 255;
            keys_.pwm_voice[sample % Keys::kPwmSamples] = uint8_t(v);
        }
    }
}

void Player::run_one(std::vector<int16_t>* audio) {
    Machine& m = *m_;
    uint16_t pad = 0;
    if (m.mode_request >= 0) {
        // Still booting: taps of Start hurry the SEGA screen along, as
        // TIME ATTACK's do (frontend/backdrop.cpp).
        if (m.frame_count % 24 < 6) pad = PAD_START;
    } else if (ready_ && !queue_.empty()) {
        for (uint8_t id : queue_) {
            send(id);
            // The time counts from when the driver has it.
            if (id && id == id_) started_at_ = m.frame_count;
        }
        queue_.clear();
    }
    m.input.pad[0] = pad;
    m.input.pad[1] = 0;
    audio::set_music_and_effect_levels(m, music_level_, effects_level_);
    const size_t first = m.audio_out.size();
    m.run_frame();
    music_.after_frame(m, first);
    samples_.after_frame(m, first);
    int peak = 0;
    for (size_t i = first; i < m.audio_out.size(); ++i) peak = std::max(peak, std::abs(int(m.audio_out[i])));
    if (audio) audio->insert(audio->end(), m.audio_out.begin() + long(first), m.audio_out.end());
    m.audio_out.clear();
    read_keys();

    if (!ready_ || !id_) return;
    if (peak > kSilence) {
        heard_ = true;
        heard_at_ = m.frame_count;
    }
    const double played = double(m.frame_count - started_at_) / kFrameRate;
    if (repeat_ != Repeat::Loop && !fading_ && fade_after_ > 0 && played >= fade_after_) fade_out();
    const bool silent_end = heard_ && double(m.frame_count - heard_at_) / kFrameRate >= kEndedAfterSilence;
    const bool faded = fading_ && double(m.frame_count - fade_at_) / kFrameRate >= kFadeSeconds;
    const bool none = !heard_ && played >= kNeverStarted;
    if (silent_end || faded || none) {
        if (faded) request(patches::kSoundStopMusic);
        track_ended();
    }
}

void Player::run_frames(int n, std::vector<int16_t>& audio) {
    if (!m_) return;
    if (!ready_) {
        // Boot frames make no sound and nobody waits on them, so a few go
        // by for each frame asked for: ready in about 0.4 s, not 2.3.
        boot(n * kBootFramesPerFrame);
        return;
    }
    Machine* before = recompiled_ ? bind_recompiled_code(*m_) : nullptr;
    for (int i = 0; i < n; ++i) run_one(&audio);
    if (before && before != m_.get()) bind_recompiled_code(*before);
}

Status Player::status() const {
    Status s;
    s.ready = ready_;
    s.failed = failed_;
    if (!m_) return s;
    s.id = id_;
    s.last = last_id_;
    s.playing = id_ != 0;
    s.fading = fading_;
    s.replaced = id_ && music_.playing() == id_;
    s.seconds = id_ ? double(m_->frame_count - std::min(started_at_, m_->frame_count)) / kFrameRate : 0;
    return s;
}

} // namespace chaotix::jukebox
