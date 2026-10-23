// Character mods (docs/CHARACTER_MODS.md): Mods/Characters/<name>.ini, one
// file per character, with its colours and its movement parameters.
//
// The two halves are applied very differently, because one must not change
// the game and the other is meant to:
//
// - [Palette] (and Espio's [Cycle]) is output only. The 32X palette the
//   compositor shows is a copy of the emulated one with that character's
//   entries substituted (CharacterPalettes below); CRAM, the CPUs and the
//   frame buffers never see it, so lockstep compares the same machine.
// - [Stats] changes how the character plays. It is an in-memory data patch
//   of the character's 0x4A-byte record in the ROM's movement table
//   (stat_edits, Rom::apply_edits), applied before reset on every machine
//   that is to run the same game (the lockstep reference included), exactly
//   like a ROM patch file. The 68K reads the record as data, so it stays
//   recompiled.
//
// Where everything lives in the ROM is in docs/CHARACTERS.md.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "runtime/rom.h"

namespace chaotix {

namespace characters {

// Character ids, as in FFE038 / FFE03A divided by 4.
inline constexpr int kCount = 8;
inline constexpr int kSlot1 = 1;  // the unused slot: Mighty's record and colours, no art
inline constexpr int kEspio = 7;
inline constexpr int kColours = 18;      // pixel values 1..18
inline constexpr int kCycleSets = 16;    // Espio's colour cycle: 16 sets...
inline constexpr int kCycleColours = 5;  // ...of pixels 2..6
inline constexpr int kCycleFirstPixel = 2;
inline constexpr int kRecordSize = 0x4A;

// The file names, by id: "<name>.ini". Slot 1 has none (see kSlot1).
extern const char* const kFileNames[kCount];
// The id for a file name without its extension, any case; -1 if none.
int id_for_name(const std::string& name);

// One field of the movement record that a mod may set.
struct StatField {
    const char* name;
    uint8_t offset;
    uint8_t size;  // 1, 2 or 4 bytes, big-endian
};
extern const StatField kStatFields[];
extern const size_t kStatFieldCount;
const StatField* find_stat(const std::string& name);

// A 24-bit #RRGGBB colour as a 32X one (bits 0-4 red, 5-9 green, 10-14 blue),
// each component rounded to the nearest of the 32 levels.
uint16_t colour_from_rgb(uint32_t rgb);

} // namespace characters

// One character file, parsed.
struct CharacterMod {
    int id = -1;
    std::string path;
    // Colours for pixel values 1..18 (index 0 is pixel 1), 15-bit; only the
    // ones the file gives are set.
    std::array<uint16_t, characters::kColours> palette{};
    std::array<bool, characters::kColours> palette_set{};
    // Espio's cycle sets: colours for pixels 2..6 of each.
    std::array<std::array<uint16_t, characters::kCycleColours>, characters::kCycleSets> cycle{};
    std::array<bool, characters::kCycleSets> cycle_set{};
    // (field, value) in the order given.
    std::vector<std::pair<const characters::StatField*, uint32_t>> stats;

    bool has_palette() const;
};

// Parses an .ini text into `mod` (whose id the caller sets). Lines it cannot
// use are skipped and said so in `messages`, prefixed with `where`.
void parse_character_mod(const std::string& text, CharacterMod& mod, const std::string& where,
                         std::vector<std::string>* messages);

// Every <name>.ini in `dir`, in id order. A missing folder is no mods.
std::vector<CharacterMod> load_character_mods(const std::string& dir, std::vector<std::string>* messages);
// Mod packs (issue #42): several Characters folders, lowest priority first.
// A character's file from a later folder replaces an earlier folder's whole
// (colours and stats alike); in id order.
std::vector<CharacterMod> load_character_mods(const std::vector<std::string>& dirs, std::vector<std::string>* messages);

// The ROM offset of a character's movement record, read from the table at
// $88BE58 (so a ROM patch that moves one is followed); 0 if it is not a
// record in the ROM.
uint32_t stat_record_offset(const Rom& rom, int id);

// The data patch for the mods' [Stats]: the fields each one sets, written
// into that character's record.
std::vector<RomEdit> stat_edits(const Rom& rom, const std::vector<CharacterMod>& mods,
                                std::vector<std::string>* messages);

// The displayed 32X palette with the modded characters' colours (see the
// top of this file). Built once from the ROM's colours and the mods; asked
// for the palette to show on every line, it recomputes only when CRAM or the
// characters' colour offsets have changed since the last time.
class CharacterPalettes {
public:
    CharacterPalettes(const Rom& rom, const std::vector<CharacterMod>& mods);

    // Whether any mod has colours; without, there is nothing to do.
    bool empty() const { return chars_.empty(); }

    // The palette to show: `cram` itself, or a substituted copy. `offsets` is
    // the 16 colour offset bytes at SDRAM 0x06004B00 (one per bank).
    const uint16_t* display(const uint16_t* cram, const uint8_t* offsets);

    // Colour blocks substituted the last time the palette was worked out
    // (0, 1 or 2), and how many times it was; for tests and the log.
    int substituted() const { return substituted_; }
    uint64_t recomputed() const { return recomputed_; }
    // Blocks a modded character's offset pointed at that held colours other
    // than its own (another scene's, or a fade it does not follow), so were
    // shown as they are.
    uint64_t refused() const { return refused_; }

    // The game's colour offsets: the player's colours start at entry 0x13,
    // the partner's at 0x25.
    static constexpr uint8_t kPlayerOffset = 0x12;
    static constexpr uint8_t kPartnerOffset = 0x24;
    static constexpr uint32_t kOffsetBytes = 0x4B00;  // in SDRAM

private:
    struct Char {
        int id = 0;
        uint16_t rom[characters::kColours] = {};  // 15-bit, as the game loads them
        uint16_t mod[characters::kColours] = {};
        // Espio only: the ROM's cycle sets and the colours shown for each.
        int cycles = 0;
        uint16_t rom_cycle[characters::kCycleSets][characters::kCycleColours] = {};
        uint16_t mod_cycle[characters::kCycleSets][characters::kCycleColours] = {};
    };
    bool substitute(const Char& c, int base);

    std::vector<Char> chars_;
    uint16_t key_pal_[256] = {};
    uint8_t key_offsets_[16] = {};
    bool valid_ = false;
    bool changed_ = false;
    uint16_t out_[256] = {};
    int substituted_ = 0;
    uint64_t recomputed_ = 0;
    uint64_t refused_ = 0;
};

// How a block of CRAM colours relates to the colours it was loaded from:
// the same, or part way through one of the game's fades. Fade-out ($882F78)
// takes 1 from every component per step down to 0; fade-in ($882FCE) steps
// every component from 0 up to its target, so after n steps a component
// shows min(n, target). Exposed for the tests.
struct Fade {
    enum Kind { None, Out, In } kind = None;
    int n = 0;
};
bool match_fade(const uint16_t* shown, const uint16_t* original, int count, Fade* fade);
// `colour` (15-bit) after the same fade.
uint16_t apply_fade(uint16_t colour, const Fade& fade);

} // namespace chaotix
