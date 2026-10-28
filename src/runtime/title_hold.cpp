// See title_hold.h and docs/MENU_ART.md.
#include "runtime/title_hold.h"
#include "runtime/patches.h"
#include "runtime/system.h"

namespace chaotix::title_hold {

namespace {

uint16_t rd16(const Machine& m, uint32_t off) { return uint16_t(m.wram[off] << 8 | m.wram[off + 1]); }

// The boot screens and the SEGA logo, in the order they come.
bool boot_mode(uint16_t mode) { return mode == 0x00 || mode == 0x04 || mode == 0x0C || mode == 0x10 || mode == 0x14; }

} // namespace

bool before_title(const Machine& m) {
    const uint16_t mode = rd16(m, patches::kGameMode);
    return boot_mode(mode) || (mode == kModeTitle && rd16(m, kIdleCounter) == 0);
}

bool on_title(const Machine& m) {
    return rd16(m, patches::kGameMode) == kModeTitle && rd16(m, kIdleCounter) != 0;
}

bool apply(Machine& m) {
    if (rd16(m, patches::kGameMode) != kModeTitle || rd16(m, kIdleCounter) <= kHeld) return false;
    m.wram[kIdleCounter] = uint8_t(kHeld >> 8);
    m.wram[kIdleCounter + 1] = uint8_t(kHeld);
    return true;
}

} // namespace chaotix::title_hold
