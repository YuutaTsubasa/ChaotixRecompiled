// The music player's track list (frontend/jukebox.h), read the way the
// game's SOUND TEST reads it. No ROM is needed: a blank image is given the
// two instructions the list is found through and a block of titles. The
// real ROM's list (41 songs, DOOR INTO SUMMER to DECISION 2ND) is checked by
// tests/test_music_player.py.
#include "frontend/jukebox.h"

#include "runtime/rom.h"
#include "test_framework.h"

#include <string>
#include <vector>

using namespace chaotix;
using namespace chaotix::jukebox;

namespace {

// A ROM image with the SOUND TEST's `lea titles, a0` at 0x751E and its
// `cmpi.w #count, d0` at 0x7524, and the titles (header first) at `at`.
Rom rom_with(const std::vector<std::string>& titles, uint32_t addr, uint32_t at, uint16_t count) {
    Rom r;
    r.data.assign(0x300000, 0);
    r.file_size = r.data.size();
    auto put16 = [&](uint32_t o, uint16_t v) { r.data[o] = uint8_t(v >> 8); r.data[o + 1] = uint8_t(v); };
    put16(0x751E, 0x41F9);
    put16(0x7520, uint16_t(addr >> 16));
    put16(0x7522, uint16_t(addr));
    put16(0x7524, 0x0C40);
    put16(0x7526, count);
    for (const std::string& t : titles) {
        for (char c : t) r.data[at++] = uint8_t(c);
        r.data[at++] = 0;
    }
    return r;
}

std::vector<std::string> titles(int songs) {
    std::vector<std::string> t{"--- SOUND  TEST ---"};
    for (int i = 1; i <= songs; ++i) t.push_back("SONG " + std::to_string(i));
    t.push_back("S.E.");
    return t;
}

} // namespace

TEST(jukebox, the_track_list_is_read_where_the_sound_test_reads_it) {
    // As in Knuckles' Chaotix: $927338 in the $900000 window, bank 2.
    const Rom rom = rom_with(titles(41), 0x927338, 0x227338, 0x2A);
    bool from_rom = false;
    const std::vector<Track> t = track_list(rom, &from_rom);
    CHECK(from_rom);
    CHECK_EQ(t.size(), size_t(41));
    CHECK_EQ(int(t.front().id), 1);
    CHECK_STR(t.front().name, "SONG 1");
    CHECK_EQ(int(t.back().id), 0x29);
    CHECK_STR(t.back().name, "SONG 41");
}

TEST(jukebox, the_count_comes_from_the_code_not_from_us) {
    // A patch that offers fewer songs (the first sound effect lower).
    const Rom rom = rom_with(titles(10), 0x927338, 0x227338, 11);
    bool from_rom = false;
    const std::vector<Track> t = track_list(rom, &from_rom);
    CHECK(from_rom);
    CHECK_EQ(t.size(), size_t(10));
    CHECK_STR(t.back().name, "SONG 10");
}

TEST(jukebox, the_fonts_equals_sign_is_an_apostrophe) {
    std::vector<std::string> list = titles(41);
    list[9] = "WALKIN=";
    list[22] = "CHILD=S SONG  ";  // trailing spaces go too
    const std::vector<Track> t = track_list(rom_with(list, 0x927338, 0x227338, 0x2A));
    CHECK_STR(t[8].name, "WALKIN'");
    CHECK_STR(t[21].name, "CHILD'S SONG");
}

TEST(jukebox, titles_in_another_bank_are_found) {
    const Rom rom = rom_with(titles(41), 0x912345, 0x112345, 0x2A);
    bool from_rom = false;
    CHECK_EQ(track_list(rom, &from_rom).size(), size_t(41));
    CHECK(from_rom);
}

TEST(jukebox, a_rom_without_the_list_gets_numbered_tracks) {
    Rom rom = rom_with(titles(41), 0x927338, 0x227338, 0x2A);
    rom.data[0x751E] = 0x4E;  // not the lea
    bool from_rom = true;
    std::vector<Track> t = track_list(rom, &from_rom);
    CHECK(!from_rom);
    CHECK_EQ(t.size(), size_t(41));
    CHECK_STR(t.front().name, "MUSIC 01");
    CHECK_STR(t.back().name, "MUSIC 29");
    // Titles that are not text: the same.
    Rom junk = rom_with(titles(41), 0x927338, 0x227338, 0x2A);
    junk.data[0x227338 + 3] = 0x01;
    t = track_list(junk, &from_rom);
    CHECK(!from_rom);
    CHECK_EQ(t.size(), size_t(41));
    // An empty image.
    Rom none;
    t = track_list(none, &from_rom);
    CHECK(!from_rom);
    CHECK_EQ(t.size(), size_t(41));
}

TEST(jukebox, labels_and_times) {
    CHECK_STR(track_label({5, "MIDNIGHT GREENHOUSE"}), "05 MIDNIGHT GREENHOUSE");
    CHECK_STR(track_label({41, "DECISION 2ND"}), "41 DECISION 2ND");
    CHECK_STR(format_time(0), "0:00");
    CHECK_STR(format_time(61.9), "1:01");
    CHECK_STR(format_time(600), "10:00");
    CHECK_STR(repeat_name(Repeat::Loop), "LOOP");
    CHECK_STR(repeat_name(Repeat::Once), "ONCE");
    CHECK_STR(repeat_name(Repeat::All), "ALL");
}
