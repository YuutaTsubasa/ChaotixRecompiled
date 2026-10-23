// See character_mods.h, and docs/CHARACTERS.md for the ROM tables used here.
#include "runtime/character_mods.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

namespace chaotix {

namespace characters {

const char* const kFileNames[kCount] = {"mighty", nullptr, "knuckles", "charmy", "vector", "bomb", "heavy", "espio"};

// The fields of section 3 of docs/CHARACTERS.md that were verified or read
// with high confidence. Values are the record's own units (see the doc).
const StatField kStatFields[] = {
    {"abilities_a", 0x01, 1},        // bit0 jump, bit2 spin dash, bit4 wall cling, bit6 glide
    {"abilities_b", 0x02, 1},        // bit0 flight, bit3 air dash, bit6 roll, bit7 Espio's cling
    {"speed_cap", 0x04, 2},          // per axis, px/frame x 256
    {"acceleration", 0x06, 2},       // ground speed per frame, x 256; also the friction
    {"deceleration", 0x08, 2},       // pressing against the motion (skid), x 256
    {"gravity", 0x0A, 4},            // Y velocity per frame, 16.16
    {"throw_speed", 0x10, 2},        // throwing the partner, 1/4 px/frame
    {"tether_reach", 0x12, 2},       // px
    {"jump_release", 0x14, 2},       // upward speed kept when the button is let go, x 256
    {"jump", 0x16, 2},               // jump strength, px/frame x 256
    {"spin_dash_min", 0x18, 2},      // x 256
    {"spin_dash_max", 0x1A, 2},
    {"spin_dash_step", 0x1C, 2},     // added per press
    {"wall_cling_time", 0x1E, 2},    // frames
    {"wall_jump_speed", 0x20, 2},    // 1/4 px/frame
    {"glide_speed", 0x2A, 2},        // Knuckles: top X speed, x 256
    {"glide_acceleration", 0x2C, 2},
    {"glide_fall_speed", 0x2E, 2},   // x 256
    {"roll_friction", 0x36, 2},      // x 256
    {"air_dash_speed", 0x38, 2},     // Vector, x 256
};
const size_t kStatFieldCount = sizeof kStatFields / sizeof kStatFields[0];

namespace {
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
    return s;
}
} // namespace

int id_for_name(const std::string& name) {
    const std::string n = lower(name);
    for (int i = 0; i < kCount; ++i)
        if (kFileNames[i] && n == kFileNames[i]) return i;
    return -1;
}

const StatField* find_stat(const std::string& name) {
    const std::string n = lower(name);
    for (size_t i = 0; i < kStatFieldCount; ++i)
        if (n == kStatFields[i].name) return &kStatFields[i];
    return nullptr;
}

uint16_t colour_from_rgb(uint32_t rgb) {
    auto five = [](uint32_t v) { return (v * 31 + 127) / 255; };
    return uint16_t(five((rgb >> 16) & 0xFF) | five((rgb >> 8) & 0xFF) << 5 | five(rgb & 0xFF) << 10);
}

} // namespace characters

using namespace characters;

bool CharacterMod::has_palette() const {
    return std::any_of(palette_set.begin(), palette_set.end(), [](bool b) { return b; }) ||
           std::any_of(cycle_set.begin(), cycle_set.end(), [](bool b) { return b; });
}

namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// Decimal, or hex with 0x or $.
bool parse_number(const std::string& t, uint32_t* out) {
    if (t.empty()) return false;
    int base = 10;
    size_t at = 0;
    if (t[0] == '$') { base = 16; at = 1; }
    else if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) { base = 16; at = 2; }
    if (at >= t.size()) return false;
    for (size_t i = at; i < t.size(); ++i)
        if (!(base == 16 ? std::isxdigit(static_cast<unsigned char>(t[i])) : std::isdigit(static_cast<unsigned char>(t[i]))))
            return false;
    const unsigned long long v = std::strtoull(t.c_str() + at, nullptr, base);
    if (v > 0xFFFFFFFFull || t.size() - at > (base == 16 ? 8u : 10u)) return false;
    *out = uint32_t(v);
    return true;
}

// #RRGGBB, or a 32X colour as it is stored (0x0000-0x7FFF, $ also).
bool parse_colour(const std::string& t, uint16_t* out) {
    if (t.size() == 7 && t[0] == '#') {
        for (size_t i = 1; i < 7; ++i)
            if (!std::isxdigit(static_cast<unsigned char>(t[i]))) return false;
        *out = colour_from_rgb(uint32_t(std::strtoul(t.c_str() + 1, nullptr, 16)));
        return true;
    }
    uint32_t v = 0;
    if ((t[0] == '$' || t.compare(0, 2, "0x") == 0 || t.compare(0, 2, "0X") == 0) && parse_number(t, &v) && v <= 0x7FFF) {
        *out = uint16_t(v);
        return true;
    }
    return false;
}

} // namespace

void parse_character_mod(const std::string& text, CharacterMod& mod, const std::string& where,
                         std::vector<std::string>* messages) {
    auto say = [&](int line, const std::string& m) {
        if (messages) messages->push_back(where + ":" + std::to_string(line) + ": " + m);
    };
    std::istringstream in(text);
    std::string raw, section;
    int line = 0;
    while (std::getline(in, raw)) {
        ++line;
        // ';' starts a comment anywhere; '#' only at the start of a line,
        // since colours are written with one.
        std::string s = trim(raw.substr(0, raw.find(';')));
        if (s.empty() || s[0] == '#') continue;
        if (s.front() == '[') {
            if (s.back() != ']') { say(line, "a section name must end with ]"); continue; }
            section = lower(trim(s.substr(1, s.size() - 2)));
            if (section != "palette" && section != "cycle" && section != "stats")
                say(line, "unknown section [" + section + "], its lines are skipped");
            continue;
        }
        const size_t eq = s.find('=');
        if (eq == std::string::npos) { say(line, "not a key = value line"); continue; }
        const std::string key = lower(trim(s.substr(0, eq)));
        const std::string value = trim(s.substr(eq + 1));
        if (section == "palette") {
            uint32_t pixel = 0;
            uint16_t c = 0;
            if (!parse_number(key, &pixel) || pixel < 1 || pixel > uint32_t(kColours)) {
                say(line, "palette keys are the pixel values 1 to 18");
            } else if (!parse_colour(value, &c)) {
                say(line, "not a colour (#RRGGBB, or a 32X colour 0x0000-0x7FFF)");
            } else {
                mod.palette[pixel - 1] = c;
                mod.palette_set[pixel - 1] = true;
            }
        } else if (section == "cycle") {
            uint32_t set = 0;
            if (!parse_number(key, &set) || set >= uint32_t(kCycleSets)) {
                say(line, "cycle keys are the set numbers 0 to 15");
                continue;
            }
            std::istringstream vs(value);
            std::string tok;
            std::array<uint16_t, kCycleColours> cs{};
            int n = 0;
            bool ok = true;
            while (vs >> tok) {
                if (n >= kCycleColours || !parse_colour(tok, &cs[size_t(n)])) { ok = false; break; }
                ++n;
            }
            if (!ok || n != kCycleColours) {
                say(line, "a cycle set is five colours, for pixels 2 to 6");
                continue;
            }
            mod.cycle[set] = cs;
            mod.cycle_set[set] = true;
        } else if (section == "stats") {
            const StatField* f = find_stat(key);
            uint32_t v = 0;
            if (!f) {
                say(line, "unknown stat \"" + key + "\"");
            } else if (!parse_number(value, &v)) {
                say(line, "not a number (decimal, or hex as 0x... or $...)");
            } else if (f->size < 4 && v >> (8 * f->size)) {
                say(line, std::string(f->name) + " is " + (f->size == 1 ? "a byte" : "a word") + ", at most " +
                              (f->size == 1 ? "0xFF" : "0xFFFF"));
            } else {
                mod.stats.emplace_back(f, v);
            }
        } else if (section.empty()) {
            say(line, "outside any section");
        }
    }
}

std::vector<CharacterMod> load_character_mods(const std::string& dir, std::vector<std::string>* messages) {
    namespace fs = std::filesystem;
    std::vector<CharacterMod> mods;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(fs::u8path(dir), ec)) {
        if (!e.is_regular_file(ec) || lower(e.path().extension().u8string()) != ".ini") continue;
        const std::string name = e.path().filename().u8string();
        const std::string stem = e.path().stem().u8string();
        const int id = id_for_name(stem);
        if (id < 0) {
            if (messages)
                messages->push_back(name + ": not a character (" + (lower(stem) == "slot1" ?
                    "slot 1 has no art of its own and shares Mighty's colours and stats" :
                    "mighty, knuckles, charmy, vector, bomb, heavy or espio") + ")");
            continue;
        }
        std::ifstream f(e.path(), std::ios::binary);
        if (!f) {
            if (messages) messages->push_back(name + ": cannot read it");
            continue;
        }
        CharacterMod m;
        m.id = id;
        m.path = e.path().u8string();
        parse_character_mod(std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()), m, name,
                            messages);
        mods.push_back(std::move(m));
    }
    std::sort(mods.begin(), mods.end(), [](const CharacterMod& a, const CharacterMod& b) { return a.id < b.id; });
    return mods;
}

std::vector<CharacterMod> load_character_mods(const std::vector<std::string>& dirs, std::vector<std::string>* messages) {
    std::vector<CharacterMod> by_id(characters::kCount);
    for (const auto& dir : dirs)
        for (CharacterMod& m : load_character_mods(dir, messages)) by_id[size_t(m.id)] = std::move(m);
    std::vector<CharacterMod> mods;
    for (CharacterMod& m : by_id)
        if (m.id >= 0) mods.push_back(std::move(m));
    return mods;
}

namespace {

// The 68K tables indexed by the character id ($8818E4 colours, $88BE58
// movement), as ROM offsets.
constexpr uint32_t kColourTable = 0x0018E4;
constexpr uint32_t kStatTable = 0x00BE58;
// Espio's cycle sets ($88E7BA, read by $88E70A).
constexpr uint32_t kEspioCycle = 0x00E7BA;

bool in_rom(const Rom& rom, uint32_t off, uint32_t n) { return off < rom.file_size && n <= rom.file_size - off; }

// The ROM offset of a character's colours: the pointer is into the banked
// window as levels set it (bank 2: $900000 is ROM 0x200000).
uint32_t colour_offset(const Rom& rom, int id) {
    if (!in_rom(rom, kColourTable + 4 * uint32_t(id), 4)) return 0;
    const uint32_t p = rom.read32(kColourTable + 4 * uint32_t(id));
    if (p < 0x900000 || p >= 0x980000) return 0;
    const uint32_t off = p - 0x700000;
    return in_rom(rom, off, 2 * kColours) ? off : 0;
}

} // namespace

uint32_t stat_record_offset(const Rom& rom, int id) {
    if (id < 0 || id >= kCount || !in_rom(rom, kStatTable + 4 * uint32_t(id), 4)) return 0;
    const uint32_t p = rom.read32(kStatTable + 4 * uint32_t(id));
    if (p < 0x880000 || p >= 0x900000) return 0;
    const uint32_t off = p - 0x880000;
    return in_rom(rom, off, kRecordSize) ? off : 0;
}

std::vector<RomEdit> stat_edits(const Rom& rom, const std::vector<CharacterMod>& mods,
                                std::vector<std::string>* messages) {
    std::vector<RomEdit> edits;
    for (const CharacterMod& m : mods) {
        if (m.stats.empty()) continue;
        const uint32_t rec = stat_record_offset(rom, m.id);
        if (!rec) {
            if (messages) messages->push_back(std::string(kFileNames[m.id]) + ": the ROM's movement table does not point at a record");
            continue;
        }
        for (const auto& s : m.stats) {
            RomEdit e;
            e.offset = rec + s.first->offset;
            for (int i = s.first->size - 1; i >= 0; --i) e.bytes.push_back(uint8_t(s.second >> (8 * i)));
            edits.push_back(std::move(e));
        }
    }
    return edits;
}

CharacterPalettes::CharacterPalettes(const Rom& rom, const std::vector<CharacterMod>& mods) {
    for (const CharacterMod& m : mods) {
        if (m.id < 0 || m.id >= kCount || !m.has_palette()) continue;
        const uint32_t off = colour_offset(rom, m.id);
        if (!off) continue;
        Char c;
        c.id = m.id;
        for (int i = 0; i < kColours; ++i) {
            c.rom[i] = rom.read16(off + 2 * uint32_t(i)) & 0x7FFF;
            c.mod[i] = m.palette_set[size_t(i)] ? m.palette[size_t(i)] : c.rom[i];
        }
        if (m.id == kEspio && in_rom(rom, kEspioCycle, 2 * kCycleSets * kCycleColours)) {
            c.cycles = kCycleSets;
            for (int k = 0; k < kCycleSets; ++k)
                for (int j = 0; j < kCycleColours; ++j) {
                    const int pixel = kCycleFirstPixel + j;  // 2..6
                    c.rom_cycle[k][j] = rom.read16(kEspioCycle + 2 * uint32_t(k * kCycleColours + j)) & 0x7FFF;
                    // A set the file does not give shows the file's colour
                    // for that pixel if it has one (a character that does not
                    // shimmer), otherwise Espio's own.
                    c.mod_cycle[k][j] = m.cycle_set[size_t(k)] ? m.cycle[size_t(k)][size_t(j)]
                                      : m.palette_set[size_t(pixel - 1)] ? m.palette[size_t(pixel - 1)]
                                      : c.rom_cycle[k][j];
                }
        }
        chars_.push_back(c);
    }
}

bool match_fade(const uint16_t* shown, const uint16_t* original, int count, Fade* fade) {
    // Per component (5 bits each).
    auto comp = [](uint16_t c, int k) { return int(c >> (5 * k)) & 31; };
    bool exact = true, out_ok = true, in_ok = true;
    int out_n = -1, in_n = -1;
    for (int i = 0; i < count; ++i)
        for (int k = 0; k < 3; ++k) {
            const int s = comp(shown[i], k), o = comp(original[i], k);
            if (s > o) return false;
            if (s != o) exact = false;
            if (s > 0) {
                if (out_n < 0) out_n = o - s;
                else if (o - s != out_n) out_ok = false;
            }
            if (s < o) {
                if (in_n < 0) in_n = s;
                else if (s != in_n) in_ok = false;
            }
        }
    if (exact) { *fade = {Fade::None, 0}; return true; }
    // A component faded to 0 must have been at most n; one that reached its
    // target, at most n too.
    if (out_n < 0) out_n = 31;
    for (int i = 0; i < count && (out_ok || in_ok); ++i)
        for (int k = 0; k < 3; ++k) {
            const int s = comp(shown[i], k), o = comp(original[i], k);
            if (s == 0 && o > out_n) out_ok = false;
            if (s == o && o > in_n) in_ok = false;
        }
    if (out_ok) { *fade = {Fade::Out, out_n}; return true; }
    if (in_ok) { *fade = {Fade::In, in_n}; return true; }
    return false;
}

uint16_t apply_fade(uint16_t colour, const Fade& fade) {
    if (fade.kind == Fade::None) return colour & 0x7FFF;
    uint16_t out = 0;
    for (int k = 0; k < 3; ++k) {
        int c = (colour >> (5 * k)) & 31;
        c = fade.kind == Fade::Out ? std::max(c - fade.n, 0) : std::min(c, fade.n);
        out = uint16_t(out | c << (5 * k));
    }
    return out;
}

bool CharacterPalettes::substitute(const Char& c, int base) {
    uint16_t shown[kColours], orig[kColours], mod[kColours];
    for (int i = 0; i < kColours; ++i) shown[i] = key_pal_[base + 1 + i] & 0x7FFF;
    // Espio: the colours with each cycle set in pixels 2-6 first, since the
    // first set is the colours as loaded; then (everyone) those colours.
    for (int k = 0; k <= c.cycles; ++k) {
        std::memcpy(orig, c.rom, sizeof orig);
        std::memcpy(mod, c.mod, sizeof mod);
        if (k < c.cycles)
            for (int j = 0; j < kCycleColours; ++j) {
                orig[kCycleFirstPixel - 1 + j] = c.rom_cycle[k][j];
                mod[kCycleFirstPixel - 1 + j] = c.mod_cycle[k][j];
            }
        Fade f;
        if (!match_fade(shown, orig, kColours, &f)) continue;
        for (int i = 0; i < kColours; ++i) {
            const uint16_t col = apply_fade(mod[i], f);
            const int e = base + 1 + i;
            out_[e] = uint16_t((key_pal_[e] & 0x8000) | col);
            // The copy for the other palette half, with its own priority bit,
            // if it is still the copy the game wrote with this entry.
            if (((key_pal_[e + 0x80] ^ key_pal_[e]) & 0x7FFF) == 0) out_[e + 0x80] = uint16_t((key_pal_[e + 0x80] & 0x8000) | col);
        }
        return true;
    }
    return false;
}

const uint16_t* CharacterPalettes::display(const uint16_t* cram, const uint8_t* offsets) {
    if (valid_ && std::memcmp(key_pal_, cram, sizeof key_pal_) == 0 && std::memcmp(key_offsets_, offsets, sizeof key_offsets_) == 0)
        return changed_ ? out_ : cram;
    valid_ = true;
    ++recomputed_;
    std::memcpy(key_pal_, cram, sizeof key_pal_);
    std::memcpy(key_offsets_, offsets, sizeof key_offsets_);
    std::memcpy(out_, cram, sizeof out_);
    substituted_ = 0;
    // The block a character's colours are in comes from its bank's colour
    // offset; a stale offset (a character from an earlier scene) is ruled out
    // by the colours themselves not being that character's. A block no
    // offset accounts for is still a character's if it holds its colours (or
    // a fade of them): with the same character as player and partner, its
    // one offset byte can only name one of the two blocks.
    for (const uint8_t base : {kPlayerOffset, kPartnerOffset}) {
        bool claimed = false, done = false;
        for (const Char& c : chars_) {
            if (offsets[c.id] != base) continue;
            claimed = true;
            if (substitute(c, base)) { ++substituted_; done = true; break; }
        }
        // (A black block is black whoever's it is.)
        bool black = true;
        for (int i = 0; i < kColours; ++i) black = black && !(cram[base + 1 + i] & 0x7FFF);
        for (const Char& c : chars_) {
            if (done || black) break;
            if (offsets[c.id] != base && substitute(c, base)) { ++substituted_; done = true; }
        }
        if (claimed && !done) ++refused_;
    }
    changed_ = std::memcmp(out_, cram, sizeof out_) != 0;
    return changed_ ? out_ : cram;
}

} // namespace chaotix
