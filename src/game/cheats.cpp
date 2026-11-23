// See cheats.h.
#include "game/cheats.h"
#include "runtime/system.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace chaotix::cheats {

const std::vector<Cheat>& builtin() {
    // Only codes whose effect was seen in the game (docs/CHEATS.md); others
    // from the same list did nothing visible and are left to the config.
    static const std::vector<Cheat> kCheats = {
        {"rings", "99 RINGS", "the ring count stays at 99", {{0xE008, 0x00}, {0xE009, 0x63}}},
        {"speed", "SPEED SHOES", "both characters keep speed shoes", {{0xADFC, 0x01}, {0xAEBC, 0x01}}},
        // The Chaos Rings held are the word at $FFE01A (special stages won, 6
        // is all of them); the high byte is left as the game has it.
        {"chaosrings", "ALL CHAOS RINGS", "you hold all six Chaos Rings",
         {{0xE01B, 0x06, true}}},
    };
    return kCheats;
}

bool parse_codes(const std::string& text, std::vector<Write>& out) {
    std::vector<Write> found;
    size_t i = 0;
    auto skip = [&] {
        while (i < text.size() && (std::isspace(uint8_t(text[i])) || text[i] == '+' || text[i] == ',')) ++i;
    };
    auto hex = [&](size_t& digits) {
        uint32_t v = 0;
        digits = 0;
        while (i < text.size() && std::isxdigit(uint8_t(text[i]))) {
            v = v * 16 + uint32_t(std::strtoul(std::string(1, text[i]).c_str(), nullptr, 16));
            ++i;
            ++digits;
        }
        return v;
    };
    skip();
    while (i < text.size()) {
        size_t ad = 0, vd = 0;
        const uint32_t addr = hex(ad);
        if (ad != 6 || (addr >> 16) != 0xFF || i >= text.size() || text[i] != ':') return false;
        ++i;
        const uint32_t value = hex(vd);
        if (vd == 2) {
            found.push_back({uint16_t(addr), uint8_t(value)});
        } else if (vd == 4) {
            found.push_back({uint16_t(addr), uint8_t(value >> 8)});
            found.push_back({uint16_t(addr + 1), uint8_t(value)});
        } else {
            return false;
        }
        skip();
    }
    if (found.empty()) return false;
    out.insert(out.end(), found.begin(), found.end());
    return true;
}

void apply(Machine& m, const std::vector<Write>& writes) {
    for (const Write& w : writes) {
        if (w.at_least) m.wram[w.addr] = std::max(m.wram[w.addr], w.value);
        else m.wram[w.addr] = w.value;
    }
}

} // namespace chaotix::cheats
