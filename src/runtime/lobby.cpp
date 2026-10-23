// See lobby.h and docs/LOBBY_SHORTCUTS.md.
#include "runtime/lobby.h"
#include "runtime/stage_select.h"
#include "runtime/system.h"

namespace chaotix::lobby {

namespace {

uint16_t rd16(const Machine& m, uint32_t off) { return uint16_t(m.wram[off] << 8 | m.wram[off + 1]); }
void wr16(Machine& m, uint32_t off, uint16_t v) {
    m.wram[off] = uint8_t(v >> 8);
    m.wram[off + 1] = uint8_t(v);
}

constexpr uint32_t kMode = 0xDFDE;
constexpr uint32_t kZone = 0xDFF2;
constexpr uint32_t kLevel = 0xDFF4;
constexpr uint16_t kModeLevel = 0x18;
constexpr int kAttractions = 5;  // BOTANIC BASE .. MARINA MADNESS

} // namespace

bool takes_choice(const Machine& m) {
    if (rd16(m, kMode) != kModeLevel) return false;
    const unsigned zone = rd16(m, kZone), level = rd16(m, kLevel);
    if (zone == unsigned(stage_select::kLobbyPlace)) return level == 0;
    return zone < unsigned(kAttractions) && stage_select::has_level(int(zone), int(level));
}

void apply(Machine& m) {
    if (m.lobby_partner < 0 && m.lobby_time < 0) return;
    if (!takes_choice(m)) return;
    const int partner = m.lobby_partner;
    if (partner >= 0 && partner < stage_select::kCharacterCount && partner != stage_select::kBrokenCharacter &&
        partner != rd16(m, kPlayer) >> 2)
        wr16(m, kPartner, uint16_t(partner << 2));
    const int t = m.lobby_time;
    if (t >= 0 && t < kTimeCount) wr16(m, kTime, uint16_t((rd16(m, kTime) & 1) | t << 1));
}

} // namespace chaotix::lobby
