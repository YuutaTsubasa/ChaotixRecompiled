// See stage_select.h for where each address and table came from.
#include "runtime/stage_select.h"
#include "runtime/system.h"

#include <cstdio>

namespace chaotix::stage_select {

namespace {

// 68K work RAM is $FF0000-$FFFFFF, so the low word of the address indexes it.
void wr16(Machine& m, uint32_t off, uint16_t v) {
    m.wram[off] = uint8_t(v >> 8);
    m.wram[off + 1] = uint8_t(v);
}
uint16_t rd16(const Machine& m, uint32_t off) {
    return uint16_t(m.wram[off] << 8 | m.wram[off + 1]);
}

enum : uint32_t {
    kPlaceVar = 0xFBC0,    // the stage select's own PLACE
    kZone = 0xDFF2,        // what the level engine reads
    kLevel = 0xDFF4,
    kAtTime = 0xDFF6,
    kPlayer = 0xE038,
    kCombi = 0xE03A,
    kPlayers = 0xE04C,
    kPadOffset = 0xE05C,
    kMode = 0xDFDE,
    kSceneFlag = 0xDFE6,   // $8F72C8 sets this to -1 before starting
};

constexpr uint16_t kModeLevel = 0x18;

} // namespace

std::string format_time(int frames) {
    frames -= kHudLag;
    if (frames < 0) frames = 0;
    const int cs = frames * 100 / 60;
    char out[32];
    std::snprintf(out, sizeof out, "%d'%02d\"%02d", cs / 6000, (cs / 100) % 60, cs % 100);
    return out;
}

bool has_level(int place, int level) {
    if (place < 0 || place >= kPlaceCount || level < 0 || level > 7) return false;
    return (kPlaces[place].levels >> level) & 1;
}

int first_level(int place) {
    for (int l = 0; l <= 7; ++l)
        if (has_level(place, l)) return l;
    return 0;
}

bool valid(const Request& r) {
    if (!has_level(int(r.place), int(r.level))) return false;
    if (r.attime > 6 || (r.attime & 1)) return false;
    if (r.player >= kCharacterCount || r.combi >= kCharacterCount) return false;
    return true;
}

void apply(Machine& m, const Request& r) {
    wr16(m, kPlaceVar, r.place);
    wr16(m, kLevel, r.level);
    wr16(m, kAtTime, r.attime);
    wr16(m, kPlayer, r.player);
    wr16(m, kCombi, r.combi);
    m.wram[kPlayers] = r.two_players ? 1 : 0;

    // From here on this is $8F72C8 transcribed.
    wr16(m, kSceneFlag, 0xFFFF);
    // $8F7352: both character numbers become table offsets (four bytes each).
    wr16(m, kPlayer, uint16_t(rd16(m, kPlayer) << 2));
    wr16(m, kCombi, uint16_t(rd16(m, kCombi) << 2));
    // The PLAYERS setting picks where a player reads its controller from:
    // -2 (one player, both on pad 1) or +0x10 (the partner on pad 2).
    m.wram[kPlayers] = 0;
    m.wram[kPadOffset] = r.two_players ? 0x10 : 0xFE;
    // The place is the zone the level engine loads.
    wr16(m, kZone, r.place);
    // One special case in the game's own code: WORLD ENTRANCE level 1 is the
    // hub after the five attractions are open, so it seeds the progress words
    // and starts at level 0 instead.
    if (r.place == 7 && r.level == 1) {
        wr16(m, 0xDFFE, 0x0606);
        wr16(m, 0xE000, 0x0606);
        wr16(m, 0xE002, 0x0600);
        wr16(m, kLevel, 0);
    }
    // Places 8-10 (NOT USED, BONUS STAGE, SPECIAL STAGE) start in their own
    // game modes; the front end does not offer them, so only the level mode is
    // transcribed here.
    wr16(m, kMode, kModeLevel);
}

} // namespace chaotix::stage_select
