// Save states (issue #40): a Machine's emulation state as bytes, and back.
//
// What is in a state is everything the emulated hardware's future depends
// on: the CPUs, RAM, VRAM, the 32X frame buffers, the sound chips, the timing
// counters, and the host patches' latched state (runtime/patches.h). A
// restored machine runs on bit-identically to one that was never stopped
// (tests/CMakeLists.txt, save_state_*).
//
// What is not:
//  - the ROM: the header names it (its SHA-1, and that of the image after
//    ROM patches), and a state is refused by any other.
//  - the cartridge SRAM (the DATA slots): loading a state never rolls a save
//    back, as BACK TO TITLE keeps it too. sram_dirty is left alone as well.
//  - the host's resources and settings that are not hardware: tile and
//    sprite replacements, character colours, the sprite recorder, tooling
//    hooks, executors, input, volume gains, which music the host replaces,
//    audio output not yet played, the vestigial PWM sample log, and the
//    lobby shortcuts' choice (lobby_partner/lobby_time), which nothing
//    latches: the app gives it again before every frame.
//  - the recompiled code's SH-2 validation epochs: a load moves them on to a
//    value no validation has seen, so code in SDRAM is checked again
//    (game/recomp_dispatch.cpp caches results by epoch).
// Widescreen settings that change what the game streams (wide_extra, ...)
// are in the state, so a test reloads exactly what it saved; the app writes
// its own settings over them before the next frame anyway.
//
// The raw form is fixed-size fields in a fixed order, little-endian, so the
// same state loads on every platform. A file is a header, the raw state
// compressed (runtime/lz.h), and optional bytes the host keeps with it.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace chaotix {

class Machine;

namespace savestate {

// Bumped whenever the raw layout changes (a field added, moved or removed).
constexpr uint32_t kFormatVersion = 1;

// The output picture (framebuffer) is optional: a file keeps it so the
// game is on screen at once after a load; rewind snapshots leave it out and
// run a frame instead (much smaller deltas).
enum Flags : unsigned { kPicture = 1 };

// This build's version, as the header records it ("0.7.1").
const char* game_version();

// Raw state, uncompressed. raw_size is the same for every machine.
size_t raw_size(unsigned flags);
void serialize(const Machine& m, std::vector<uint8_t>& out, unsigned flags);
// False (and the machine untouched) unless n is exactly raw_size(flags).
bool deserialize(Machine& m, const uint8_t* data, size_t n, unsigned flags, std::string* error);

struct Info {
    uint32_t format = 0;
    std::string game_version;
    std::string rom_sha1;       // the user's ROM
    std::string rom_data_sha1;  // the image that ran: after ROM patches, if any
    unsigned flags = 0;
    uint64_t frame = 0;
    int64_t saved_unix = 0;     // seconds since 1970, 0 if unknown
    size_t raw_size = 0;
    size_t compressed_size = 0;
};

// A state file's bytes. `host` is kept alongside, untouched (the app stores
// the replacement music's position there). Without kPicture in `flags` the
// file leaves the output picture out (a TIME ATTACK ghost's starting point,
// frontend/ghost.h, never shows it); a load then keeps the machine's own.
std::vector<uint8_t> save(const Machine& m, const std::vector<uint8_t>& host = {}, int64_t saved_unix = 0,
                          unsigned flags = kPicture);
// Reads the header only. False, with the reason, for a file that is not a
// state of this format.
bool read_info(const std::vector<uint8_t>& file, Info& info, std::string* error);
// Loads a state into m, which must already be running the same ROM. Refused
// (m untouched, the reason in error) for another ROM, another ROM patch set,
// another version of the program or format, or a damaged file. The SRAM is
// never changed.
bool load(Machine& m, const std::vector<uint8_t>& file, std::string* error, std::vector<uint8_t>* host = nullptr,
          Info* info = nullptr);

bool write_file(const std::string& path, const std::vector<uint8_t>& bytes);
bool read_file(const std::string& path, std::vector<uint8_t>& bytes);

// FNV-1a over the raw state with the picture: a whole-machine fingerprint
// for tests (chaotix_headless --state-hash-every).
uint64_t state_hash(const Machine& m);

// Where the raw state (of these flags) holds fields that only shape the
// sound on its way out, after the host's volume levels (the output DC
// filter, dc_x and dc_y, and the PSG's last sample): two machines that run
// exactly the same game but
// at different volumes differ there and nowhere else. (offset, size) pairs.
// Netplay leaves them out of the states it compares (net/machine_game.h).
std::vector<std::pair<size_t, size_t>> output_only_ranges(unsigned flags);

} // namespace savestate
} // namespace chaotix
