// See level_select.h.
#include "frontend/level_select.h"
#include "runtime/patches.h"
#include "runtime/system.h"

namespace chaotix::level_select {

namespace {

unsigned rd16(const Machine& m, uint32_t off) { return unsigned(m.wram[off] << 8 | m.wram[off + 1]); }

constexpr uint32_t kMode = 0xDFDE;
constexpr uint32_t kZone = 0xDFF2;

int place_index(int place) {
    for (int i = 0; i < kPlaceCount; ++i)
        if (kPlaces[i] == place) return i;
    return -1;
}

} // namespace

bool offered(int place) { return place_index(place) >= 0; }

bool has_level(int place, int level) {
    if (!offered(place)) return false;
    if (place == kWorldEntrance) return level == 1;
    if (place == kBonusStage) return level == 0;
    if (place == kSpecialStage) return level >= 0 && level <= 5;
    return stage_select::has_level(place, level);
}

int first_level(int place) {
    for (int l = 0; l <= 7; ++l)
        if (has_level(place, l)) return l;
    return 0;
}

int step_place(int place, int step) {
    int i = place_index(place);
    if (i < 0) return kPlaces[0];
    i = ((i + step) % kPlaceCount + kPlaceCount) % kPlaceCount;
    return kPlaces[i];
}

int step_level(int place, int level, int step) {
    if (!step) return level;
    for (int i = 0; i < 8; ++i) {
        level = ((level + step) % 8 + 8) % 8;
        if (has_level(place, level)) return level;
    }
    return first_level(place);
}

std::string level_label(int place, int level) {
    if (place == kSpecialStage) return std::to_string(level + 1);
    if (place == kBonusStage || place == kWorldEntrance) return "-";
    return std::to_string(level);
}

bool valid(const stage_select::Request& r) {
    return has_level(int(r.place), int(r.level)) && stage_select::valid(r);
}

bool partner_none_offered(int place, bool two_players) {
    return !two_players && place >= 0 && place < kAttractions;
}

void Session::begin(const stage_select::Request& r, bool solo_) {
    running = true;
    request = r;
    solo = solo_ && partner_none_offered(int(r.place), r.two_players);
    reached = false;
    scene = 0;
    reached_frame = 0;
}

bool Session::update(const Machine& m) {
    // The request is taken at the game's mode dispatcher; until then the game
    // is still on its title screen.
    if (!running || m.stage_pending) return false;
    const unsigned mode = rd16(m, kMode), zone = rd16(m, kZone);
    const int place = int(request.place);
    if (!reached) {
        const bool there = place == kSpecialStage ? mode == kModeSpecial
                           : place == kBonusStage ? mode == kModeBonus
                                                  : mode == kModeLevel && zone == unsigned(place);
        if (there) {
            reached = true;
            scene = patches::scene_key(m);
            reached_frame = m.frame_count;
        }
        return false;
    }
    const bool playing = mode == kModeLevel || mode == kModeSpecial || mode == kModeBonus;
    bool over = !playing;
    if (place == kSpecialStage) over = over || mode != kModeSpecial;
    else if (place == kBonusStage) over = over || mode != kModeBonus;
    else if (place != kWorldEntrance) over = over || zone == unsigned(stage_select::kLobbyPlace);
    if (!over) return false;
    running = false;
    return true;
}

} // namespace chaotix::level_select
