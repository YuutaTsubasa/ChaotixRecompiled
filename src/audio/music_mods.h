// Music replaced by files: a folder of WAV files, one per piece of the
// game's music, played instead of the sound driver's.
//
// The game asks for music through one routine ($8F6E76, runtime/patches.h).
// When a file exists for the requested id, the patch tells the Z80 driver to
// stop its music instead, so the game's sound effects still play, and the
// request reaches this player, which mixes the file into the machine's audio
// output at the same rate. Everything else follows the game:
//  - another piece without a file: the file pauses and the driver plays;
//    when the game asks for the replaced piece again (it does, after a
//    jingle or invincibility, $88E962), it carries on where it stopped
//  - $F0 fades the music out over about five seconds, as the driver does;
//    $F1 stops it
//  - while the game is paused (the driver's flag at Z80 RAM $1C10) the file
//    pauses too.
//
// Files are "<id>.wav", the id in hex as the game uses it ("05.wav"); every
// request is logged with its id, so playing the game shows which is which.
// A loop is taken from the WAV's sampler chunk ("smpl", which most audio
// editors write) or from "<id>.loop", a text file with the loop's first and,
// optionally, last sample frame in the file's own rate. Without either the
// piece plays once.
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace chaotix {
class Machine;
}

namespace chaotix::audio {

// A piece decoded and resampled to the machine's output rate.
struct Track {
    std::vector<int16_t> samples;  // interleaved stereo
    size_t frames = 0;
    bool loops = false;
    size_t loop_start = 0;         // frames
    size_t loop_end = 0;           // frames, exclusive
};

// Decodes RIFF WAVE (PCM 8/16/24/32-bit, IEEE float; mono or more channels,
// the first two used) and resamples it to out_rate. Takes the first loop of a
// "smpl" chunk, if there is one.
bool decode_wav(const std::vector<uint8_t>& bytes, double out_rate, Track& out, std::string* error);

// "<start> [<end>]" in sample frames of a file at in_rate; applied to a track
// resampled from that rate. False if the text holds no start.
bool apply_loop_text(const std::string& text, double in_rate, double out_rate, Track& track);

class MusicMods {
public:
    // Finds "<id>.wav" files in dir (ids 01-29). Returns how many.
    int scan(const std::string& dir);
    // Sets a piece directly (tests, or a host with its own loader).
    void set_track(uint8_t id, Track track);
    bool has(uint8_t id) const;
    // Tells the machine which music it should leave to this player.
    void attach(Machine& m) const;
    // Percent; files are mastered louder than the game's own music.
    void set_volume(int percent) { volume_ = percent; }
    // After each emulated frame: takes the game's music requests and mixes
    // this frame's share of the piece into m.audio_out from first_sample (an
    // index into it, in int16 values) on.
    void after_frame(Machine& m, size_t first_sample);
    uint8_t playing() const { return current_; }

private:
    const Track* track(uint8_t id);
    void on_request(uint8_t id);

    std::map<uint8_t, std::string> files_;  // id -> path
    std::map<uint8_t, Track> tracks_;       // decoded (files) or given (set_track)
    int volume_ = 25;
    uint8_t current_ = 0;
    size_t position_ = 0;
    uint8_t held_ = 0;            // the piece a jingle interrupted
    size_t held_position_ = 0;
    int fade_frames_ = 0;         // > 0 while fading out
    int fade_left_ = 0;
};

} // namespace chaotix::audio
