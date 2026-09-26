// The game's own stage select, as transcribed in runtime/stage_select.cpp.
// No ROM is needed: the tables came from the ROM once and the routine only
// writes 68K work RAM.
#include "runtime/stage_select.h"

#include "runtime/system.h"
#include "test_framework.h"

#include <memory>

using namespace chaotix;
using namespace chaotix::stage_select;

namespace {

uint16_t rd16(const Machine& m, uint32_t off) {
    return uint16_t(m.wram[off] << 8 | m.wram[off + 1]);
}

} // namespace

TEST(stage_select, the_places_offer_the_levels_the_game_says_they_do) {
    // The five attractions are numbered from 1; TRAINING from 0. This is the
    // game's own table ($8F738A), not a convention of ours.
    for (int p = 0; p < 5; ++p) {
        CHECK(!has_level(p, 0));
        for (int l = 1; l <= 5; ++l) CHECK(has_level(p, l));
        CHECK(!has_level(p, 6));
        CHECK_EQ(first_level(p), 1);
    }
    CHECK(has_level(5, 0));           // TRAINING
    CHECK(!has_level(5, 5));
    CHECK_EQ(first_level(5), 0);
    CHECK(has_level(6, 5));           // INTRODUCTION has one more
    CHECK(!has_level(6, 6));
}

TEST(stage_select, a_request_is_only_valid_for_a_level_that_exists) {
    Request r;
    CHECK(valid(r));                  // BOTANIC BASE level 1, the defaults
    r.level = 0;
    CHECK(!valid(r));
    r.level = 1;
    r.attime = 3;                     // only 0, 2, 4 and 6 mean anything
    CHECK(!valid(r));
    r.attime = 6;
    CHECK(valid(r));
    r.player = kCharacterCount;
    CHECK(!valid(r));
}

TEST(stage_select, applying_a_request_writes_what_the_games_own_start_writes) {
    auto mp = std::make_unique<Machine>();
    Machine& m = *mp;
    Request r;
    r.place = 4;                      // MARINA MADNESS
    r.level = 3;
    r.attime = 4;                     // SUNSET
    r.player = 5;                     // BOMB
    r.combi = kEspio;
    r.two_players = true;
    apply(m, r);

    CHECK_EQ(rd16(m, 0xFBC0), 4);     // PLACE, as the stage select keeps it
    CHECK_EQ(rd16(m, 0xDFF2), 4);     // and as the level engine reads it
    CHECK_EQ(rd16(m, 0xDFF4), 3);
    CHECK_EQ(rd16(m, 0xDFF6), 4);
    // Both character numbers are multiplied by four on the way in, because
    // they index a table of pointers.
    CHECK_EQ(rd16(m, 0xE038), 5 * 4);
    CHECK_EQ(rd16(m, 0xE03A), unsigned(kEspio) * 4);
    // Two players: the partner reads the second controller.
    CHECK_EQ(m.wram[0xE05C], 0x10);
    CHECK_EQ(rd16(m, 0xDFDE), 0x18);  // the level game mode

    r.two_players = false;
    apply(m, r);
    CHECK_EQ(m.wram[0xE05C], 0xFE);   // one player: both on the first pad
}
