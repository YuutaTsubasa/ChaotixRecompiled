// PWM samples replaced by files: the drums of the music, played from WAV
// files instead of the 32X's 11 kHz 8-bit samples.
//
// The 68K passes the sound driver's PWM notes to the 32X once a frame
// (audio/pwm_samples.h). When a note strikes a sample a file replaces, the
// machine gives the 32X the game's silent sample instead, which stops what
// that voice was playing as the note would have, and keeps the note with
// the moment it was written. After each frame this player takes the notes
// and plays the files into the machine's audio output from those moments,
// at the note's volumes, one sound a voice as the driver does: a later note
// on the voice, replaced or not, ends it.
//
// Files are "<NN>.wav", NN the sample's number in hex as the music player's
// pads and --dump-pwm-samples number them ("00.wav"). A sample the game
// plays lower from the same recording (04-06, 0F-10, 11-12, 13-14) uses the
// file of the one it shares the recording with, slowed down the same, unless
// it has a file of its own. A file is brought to the level of the sample it
// replaces (their peaks matched), so the music keeps its balance.
#pragma once
#include "audio/music_mods.h"
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace chaotix {
class Machine;
}

namespace chaotix::audio {

class SampleMods {
public:
    // Finds "<NN>.wav" files in dir (00-1F) and decodes them. A later folder's
    // file takes the place of an earlier one's. Returns how many it holds.
    int scan(const std::string& dir);
    // Sets a sample directly (tests).
    void set_sample(uint8_t id, Track track);
    bool empty() const { return files_.empty(); }
    // Works out which of the game's samples are replaced, and how loud each
    // file is to be, from the sample table, and tells the machine.
    void attach(Machine& m);
    // After each emulated frame: takes the notes and mixes this frame's share
    // of the files into m.audio_out from first_sample (an index into it, in
    // int16 values) on.
    void after_frame(Machine& m, size_t first_sample);

private:
    struct Use {
        const Track* track = nullptr;
        double rate = 1;   // file frames a frame of output
        float gain = 1;    // to the level of the sample it replaces
    };
    struct Voice {
        const Use* use = nullptr;
        double pos = 0;
        float left = 0, right = 0;
    };
    void strike(uint8_t voice, uint16_t command);

    std::map<uint8_t, Track> files_;
    std::array<Use, 32> uses_{};
    std::array<Voice, 4> voices_{};
};

} // namespace chaotix::audio
