// Lobby shortcuts (issue #44, runtime/lobby.h): which scenes take the
// player's choice and what is written. No ROM needed; the hook only writes
// 68K work RAM. The ROM tests in CMakeLists.txt play it through.
#include "runtime/lobby.h"

#include "runtime/stage_select.h"
#include "runtime/system.h"
#include "test_framework.h"

#include <algorithm>
#include <memory>

using namespace chaotix;

namespace {

uint16_t rd16(const Machine& m, uint32_t off) { return uint16_t(m.wram[off] << 8 | m.wram[off + 1]); }
void wr16(Machine& m, uint32_t off, uint16_t v) {
    m.wram[off] = uint8_t(v >> 8);
    m.wram[off + 1] = uint8_t(v);
}

// The game between scenes, about to start PLACE/LEVEL in the level mode, with
// Knuckles and Espio and the time at DAY and a half.
std::unique_ptr<Machine> about_to_start(uint16_t place, uint16_t level, uint16_t mode = 0x18) {
    auto m = std::make_unique<Machine>();
    wr16(*m, 0xDFDE, mode);
    wr16(*m, 0xDFF2, place);
    wr16(*m, 0xDFF4, level);
    wr16(*m, lobby::kTime, 3);
    wr16(*m, lobby::kPlayer, 2 * 4);
    wr16(*m, lobby::kPartner, 7 * 4);
    return m;
}

} // namespace

TEST(lobby, the_lobby_and_the_attractions_take_the_choice) {
    CHECK(lobby::takes_choice(*about_to_start(7, 0)));   // WORLD ENTRANCE
    for (uint16_t p = 0; p < 5; ++p) {
        CHECK(lobby::takes_choice(*about_to_start(p, 1)));
        CHECK(lobby::takes_choice(*about_to_start(p, 5)));
        CHECK(!lobby::takes_choice(*about_to_start(p, 0)));  // not a level
    }
    CHECK(!lobby::takes_choice(*about_to_start(5, 0)));  // TRAINING
    CHECK(!lobby::takes_choice(*about_to_start(6, 0)));  // INTRODUCTION
    CHECK(!lobby::takes_choice(*about_to_start(7, 6)));  // not the lobby
    CHECK(!lobby::takes_choice(*about_to_start(0, 1, 0x08)));  // the title
    CHECK(!lobby::takes_choice(*about_to_start(0, 1, 0x50)));  // the bonus stage
}

TEST(lobby, the_choice_is_written_as_the_game_keeps_it) {
    auto m = about_to_start(1, 2);
    m->lobby_partner = 4;  // VECTOR
    m->lobby_time = 3;     // NIGHT
    lobby::apply(*m);
    CHECK_EQ(rd16(*m, lobby::kPartner), 4u * 4);
    // The half step the game counts levels with is kept.
    CHECK_EQ(rd16(*m, lobby::kTime), 7u);
    CHECK_EQ(rd16(*m, lobby::kPlayer), 2u * 4);  // the player is not touched
    m->lobby_time = 0;
    lobby::apply(*m);
    CHECK_EQ(rd16(*m, lobby::kTime), 1u);
    // A count the game has carried past NIGHT comes back into range.
    wr16(*m, lobby::kTime, 8);
    m->lobby_time = 2;
    lobby::apply(*m);
    CHECK_EQ(rd16(*m, lobby::kTime), 4u);
}

TEST(lobby, what_the_catcher_cannot_give_is_left_alone) {
    auto m = about_to_start(7, 0);
    m->lobby_partner = 2;  // the player's own character
    lobby::apply(*m);
    CHECK_EQ(rd16(*m, lobby::kPartner), 7u * 4);
    m->lobby_partner = stage_select::kBrokenCharacter;
    lobby::apply(*m);
    CHECK_EQ(rd16(*m, lobby::kPartner), 7u * 4);
    m->lobby_partner = 8;
    lobby::apply(*m);
    CHECK_EQ(rd16(*m, lobby::kPartner), 7u * 4);
    CHECK_EQ(rd16(*m, lobby::kTime), 3u);  // no time chosen: the game's own
}

TEST(lobby, off_writes_nothing_and_other_scenes_are_left_alone) {
    auto m = about_to_start(0, 1);
    const auto before = std::make_unique<Machine>(*m);
    lobby::apply(*m);  // both -1
    CHECK(std::equal(m->wram, m->wram + sizeof m->wram, before->wram));
    auto t = about_to_start(6, 0);
    t->lobby_partner = 4;
    t->lobby_time = 3;
    lobby::apply(*t);
    CHECK_EQ(rd16(*t, lobby::kPartner), 7u * 4);
    CHECK_EQ(rd16(*t, lobby::kTime), 3u);
}
