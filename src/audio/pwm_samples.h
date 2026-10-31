// The 32X's PWM samples: the drums and the other sampled sounds of the music.
//
// The slave SH-2 plays them. On each PWM timer interrupt its driver
// ($C0000060, in its cache RAM) takes a command for each of four voices from
// COMM4-7, where the 68K leaves them once a frame ($8F6EFA):
//  - the low byte is the sample, an index into a table whose address the
//    driver keeps at $C00001F0;
//  - the high byte is the volume, the left channel in its top four bits and
//    the right in its bottom four: (n * 2 + 2) / 32 each.
// A table entry is four longs: the sample's first byte, its length in bytes,
// where to carry on when it ends (0: stop) and its step, the bytes it moves
// on by on each interrupt in 1/0x800ths (a step of 0x800 or more takes one
// byte an interrupt). The bytes are unsigned 8-bit. So a sample always plays
// at its own pitch, and the four voices are summed with the volumes applied.
// The driver runs about 11,000 times a second and writes two values each
// time, the second the new byte and the first halfway to it from the last.
//
// Some entries are one recording at other steps (04-06, 0F-10, 11-12,
// 13-14): the same drum lower.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace chaotix {
class Machine;
}

namespace chaotix::audio::pwm {

// The address of the slave SH-2's pointer to the sample table, and the
// table it points to (in the ROM: the same in every scene).
constexpr uint32_t kTablePointer = 0xC00001F0;
constexpr uint32_t kTable = 0x22058000;
// Sample 15 is 64 bytes of silence: what the game strikes to stop a voice.
constexpr uint8_t kSilence = 0x15;
// The driver's step for one byte an interrupt.
constexpr uint32_t kStepOne = 0x800;
// Table entries read: the music player shows 32 pads, and the game's music
// strikes no sample past them.
constexpr int kSamples = 32;

struct Sample {
    uint32_t start = 0, length = 0, loop = 0, step = 0;
    bool valid() const { return start && length && length < 0x100000 && step; }
};

// The command the 68K passed on for a voice: its sample and volumes.
struct Note {
    uint8_t sample = 0;
    int left = 0, right = 0;  // out of 32
};
inline Note decode_note(uint16_t command) {
    const uint8_t v = uint8_t(command >> 8);
    return {uint8_t(command), (v >> 4) * 2 + 2, (v & 15) * 2 + 2};
}

// The table as the driver has it (kTable before the driver sets it up).
std::vector<Sample> read_table(Machine& m);
// A sample's bytes, unsigned 8-bit as stored.
std::vector<uint8_t> read_bytes(Machine& m, const Sample& s);
// The rate the driver plays a sample at, in bytes a second: its interrupts
// a second (the PWM's rate over its timer's count) times the step.
double sample_rate(const Machine& m, const Sample& s);

// A mono 8-bit RIFF WAVE file of the bytes at that rate.
std::vector<uint8_t> wav_bytes(const std::vector<uint8_t>& pcm8, int rate);

// Writes "<NN>.wav" (NN the sample's index in hex) for each sample in the
// table to dir, and returns how many; prints the table to stdout.
int dump(Machine& m, const std::string& dir);

} // namespace chaotix::audio::pwm
