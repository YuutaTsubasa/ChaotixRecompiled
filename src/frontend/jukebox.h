// The music player (issue #48, docs/MUSIC_PLAYER.md): the game's own SOUND
// TEST song list, played on a machine of its own.
//
// The track list is the game's: the SOUND TEST ($887502) draws entry N as the
// Nth of the NUL-terminated strings its `lea` at $88751E points to (entry 0
// is the "--- SOUND  TEST ---" header), and entries from its `cmpi.w` at
// $887524 on are sound effects ("S.E." and a number). Pressing a button there
// asks for the entry's number as a sound ($8F6E76), so entry N is music id N.
// Nothing of it is shipped: a ROM patch that renames a song shows up.
//
// The music is the game's too. A second Machine, never the one being played,
// boots into the game's OPTIONS screen (the mode dispatcher hook puts it
// there once the SEGA screen has started the sound driver, as TIME ATTACK
// starts a stage), a quiet scene whose vertical-blank routine passes the
// driver's PWM notes on to the 32X like every other scene's. A track is asked
// for exactly as $8F6E76 asks, between frames: the request in the driver's
// slots, and the hook at kSoundRequest (patches.h) applied on the way, so a
// track a music mod replaces stops the driver's music and plays the mod's
// file. (Not the SOUND TEST itself: there the driver never takes a request,
// in the player's game too; its vertical-blank routine holds the Z80's bus
// for a long copy from the line the Z80 is interrupted on, and this
// emulator's slice order is the likely cause. See docs/MUSIC_PLAYER.md.)
//
// The player's machine is held, not run, while the page is open, and nothing
// of it is read or written. This machine runs on the recompiled code when
// there is some: the game is held, so the code is pointed at this machine for
// its frames and back again afterwards (bind_recompiled_code). Otherwise,
// the interpreters (about four times the cost).
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "audio/music_mods.h"

namespace chaotix {

class Machine;
struct Rom;

namespace jukebox {

struct Track {
    uint8_t id = 0;     // the music id the game asks for
    std::string name;   // as the SOUND TEST shows it, ' for the font's =
};

// The SOUND TEST's songs, in its order (41 in Knuckles' Chaotix). If the ROM
// does not hold them where that code says (another ROM, a patch that moved
// them), ids 01..29 named "MUSIC 01" and so on, and *from_rom is false.
std::vector<Track> track_list(const Rom& rom, bool* from_rom = nullptr);

// "05 SEASCAPE"-style label (the number in decimal, as the list counts) and
// m:ss.
std::string track_label(const Track& t);
std::string format_time(double seconds);

// What happens when a track ends (silence for a few seconds, or the fade
// after fade_after seconds has run its course).
enum class Repeat { Loop, Once, All };
const char* repeat_name(Repeat r);   // "LOOP", "ONCE", "ALL"

struct Status {
    bool ready = false;       // the machine is up and takes requests
    bool failed = false;      // it never got there (a ROM that does not boot to OPTIONS)
    uint8_t id = 0;           // the track playing, 0 when stopped
    uint8_t last = 0;         // the track asked for last, playing or not
    bool playing = false;
    bool fading = false;
    bool replaced = false;    // a music mod's file is playing it
    double seconds = 0;       // since it started
};

// What the music is playing, frame by frame, for the keyboards the game's
// own SOUND TEST shows (issue #102): a key for each FM and PSG channel and a
// pad for each PWM sample. Read from the music player's machine after each
// of its frames: the sound chips' registers, and the PWM notes the 68K passes
// to the 32X.
struct Keys {
    struct Key {
        int note = -1;      // MIDI note number (60 = middle C), -1 when silent
        float level = 0;    // 0 .. 1
    };
    Key fm[6], psg[3];
    static constexpr int kPwmSamples = 32;
    uint8_t pwm[kPwmSamples] = {};      // brightness, 255 when struck, fading
    uint8_t pwm_voice[kPwmSamples] = {};  // the PWM voice (0-3) that struck it last
};
// The MIDI note nearest a frequency, or -1 for none.
int note_of(double hz);

class Player {
public:
    Player();
    ~Player();
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    // Makes the machine (from a copy of the ROM, patches and all) and starts
    // its boot; boot() then runs it to the OPTIONS screen a few frames at a
    // time, so a caller with a picture to keep moving is not held up. `music`
    // is the player's music mods, copied; may be null. recompiled: use the
    // generated code, pointed at this machine only for its own frames.
    void begin(const Rom& rom, const audio::MusicMods* music, bool recompiled);
    // Runs up to max_frames boot frames: 1 ready, 0 not yet, -1 it never got
    // there (it gives up after kGiveUp frames).
    int boot(int max_frames);
    // begin() and boot() to the end: false and `why` if it never got there.
    bool start(const Rom& rom, const audio::MusicMods* music, bool recompiled, std::string* why);
    void stop();
    bool started() const { return m_ != nullptr; }
    bool ready() const { return ready_; }

    // Requests made before ready() wait for it.
    void play(uint8_t id);
    void stop_music();
    void fade_out();
    void set_repeat(Repeat r) { repeat_ = r; }
    void set_fade_after(double seconds) { fade_after_ = seconds; }   // 0: never
    void set_tracks(std::vector<Track> tracks) { tracks_ = std::move(tracks); }
    // Steps through the tracks from the one playing (or the last asked for).
    void next(int delta);

    // Levels, as the game's: music and effects 0-1, the mods' percent.
    void set_levels(float music, float effects, int mod_percent);

    // Runs n frames once ready (boot frames before), adding their sound
    // (interleaved stereo at kAudioRate) to `audio`.
    void run_frames(int n, std::vector<int16_t>& audio);

    Status status() const;
    const Keys& keys() const { return keys_; }
    Machine* machine() { return m_.get(); }
    bool replaced(uint8_t id) const { return music_.has(id); }
    bool recompiled() const { return recompiled_; }
    // Frames the machine took to get ready (logs, tests).
    uint64_t boot_frames() const { return boot_frames_; }

private:
    void run_one(std::vector<int16_t>* audio);
    void request(uint8_t id);      // queued until the machine is ready
    void send(uint8_t id);         // what $8F6E76 does
    void track_ended();
    void read_keys();

    std::unique_ptr<Machine> m_;
    audio::MusicMods music_;
    bool recompiled_ = false;
    bool ready_ = false;
    bool failed_ = false;
    int settled_ = 0;
    std::vector<Track> tracks_;
    Repeat repeat_ = Repeat::Loop;
    double fade_after_ = 120;
    std::vector<uint8_t> queue_;
    uint8_t id_ = 0, last_id_ = 0;
    uint64_t started_at_ = 0;     // machine frame the track was sent
    uint64_t heard_at_ = 0;       // last frame with sound
    bool heard_ = false;
    bool fading_ = false;
    uint64_t fade_at_ = 0;
    float music_level_ = 1, effects_level_ = 1;
    uint64_t boot_frames_ = 0;
    Keys keys_;
    uint32_t pwm_writes_ = 0;
};

} // namespace jukebox
} // namespace chaotix
