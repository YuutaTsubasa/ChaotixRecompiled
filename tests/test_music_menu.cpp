// The music player's page (issue #48), driven with key presses as the other
// menu tests are; the app's side is stood in for by the hooks.
#include "platform/menu.h"

#include <filesystem>
#include <string>
#include <vector>

#include "frontend/config.h"
#include "test_framework.h"

using namespace chaotix;

namespace {

// Walks down to the entry with this label; false if it is not there.
bool select_music_row(Menu& m, const std::string& label) {
    const std::string first = m.selected_label();
    for (int i = 0; i < 64; ++i) {
        if (m.selected_label() == label) return true;
        m.on_key(SDLK_DOWN);
        if (m.selected_label() == first) break;
    }
    return m.selected_label() == label;
}

} // namespace

TEST(music_menu, the_music_player_plays_the_chosen_track_and_steps_back) {
    Config cfg;
    cfg.set_defaults();
    Menu m;
    m.bind(cfg);
    std::vector<int> played;
    int stops = 0, fades = 0, steps = 0;
    jukebox::Status now;
    now.ready = true;
    Menu::Hooks h;
    h.music_play = [&](uint8_t id) { played.push_back(id); now.id = now.last = id; };
    h.music_stop = [&] { ++stops; now.id = 0; };
    h.music_fade = [&] { ++fades; };
    h.music_step = [&](int d) { steps += d; };
    h.music_status = [&] { return now; };
    h.music_replaced = [](uint8_t id) { return id == 5; };
    m.set_hooks(h);
    m.set_music_tracks({{1, "DOOR INTO SUMMER"}, {5, "MIDNIGHT GREENHOUSE"}, {41, "DECISION 2ND"}});

    // From the front end.
    m.open_front();
    CHECK(select_music_row(m, "MUSIC PLAYER"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK_STR(m.page_name(), "music");
    CHECK_STR(m.selected_label(), "NOW PLAYING");  // where the page opens
    CHECK_STR(m.selected_value(), "STOPPED");
    CHECK(select_music_row(m, "05 MIDNIGHT GREENHOUSE"));
    CHECK_STR(m.selected_note(), "REPLACED BY MOD");
    CHECK_STR(m.selected_value(), "");
    CHECK(m.on_key(SDLK_RIGHT));                   // left and right do nothing here
    CHECK(played.empty());
    CHECK(m.on_key(SDLK_RETURN));
    CHECK_EQ(played.size(), size_t(1));
    CHECK_EQ(played.back(), 5);
    CHECK_STR(m.selected_value(), "PLAYING");
    CHECK(select_music_row(m, "41 DECISION 2ND"));
    CHECK_STR(m.selected_note(), "");
    CHECK(select_music_row(m, "NEXT"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK(select_music_row(m, "PREVIOUS"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK_EQ(steps, -1);
    CHECK(select_music_row(m, "FADE OUT"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK_EQ(fades, 1);
    CHECK(select_music_row(m, "STOP"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK_EQ(stops, 1);
    CHECK(select_music_row(m, "PLAY"));            // the track chosen last
    CHECK(m.on_key(SDLK_RETURN));
    CHECK_EQ(played.back(), 5);
    CHECK(select_music_row(m, "REPEAT"));
    CHECK_STR(m.selected_value(), "LOOP TRACK");
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK_EQ(cfg.music_repeat, 1);
    CHECK_STR(m.selected_value(), "PLAY ONCE");
    CHECK(select_music_row(m, "FADE OUT AFTER"));
    CHECK_STR(m.selected_value(), "2:00");
    CHECK(m.on_key(SDLK_LEFT));
    CHECK_EQ(cfg.music_fade_after, 90);
    CHECK(m.on_key(SDLK_ESCAPE));
    CHECK_STR(m.page_name(), "front");

    // From the pause menu: OPTIONS > SOUND > MUSIC PLAYER, and back there.
    m.close();
    m.toggle();
    CHECK_STR(m.page_name(), "main");
    for (int i = 0; i < 16 && m.selected_label() != "OPTIONS"; ++i) m.on_key(SDLK_RIGHT);
    CHECK(m.on_key(SDLK_RETURN));
    CHECK(select_music_row(m, "SOUND"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK_STR(m.page_name(), "sound");
    CHECK(select_music_row(m, "MUSIC PLAYER"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK_STR(m.page_name(), "music");
    CHECK(select_music_row(m, "BACK"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK_STR(m.page_name(), "sound");

    // Kept across a restart.
    const std::string path = (std::filesystem::temp_directory_path() / "chaotix_music_test.ini").string();
    CHECK(cfg.save(path));
    Config back;
    CHECK(back.load(path));
    CHECK_EQ(back.music_repeat, 1);
    CHECK_EQ(back.music_fade_after, 90);
    std::filesystem::remove(path);
}

TEST(music_menu, the_page_says_when_the_machine_is_still_starting) {
    Config cfg;
    cfg.set_defaults();
    Menu m;
    m.bind(cfg);
    jukebox::Status now;  // not ready
    Menu::Hooks h;
    h.music_status = [&] { return now; };
    m.set_hooks(h);
    m.set_music_tracks({{1, "DOOR INTO SUMMER"}});
    CHECK(m.show_page("music"));
    // The page opens on NOW PLAYING, which says what the machine is doing.
    CHECK_STR(m.selected_label(), "NOW PLAYING");
    CHECK_STR(m.selected_value(), "STARTING...");
    now.ready = true;
    CHECK_STR(m.selected_value(), "STOPPED");
    now.id = 1;
    now.seconds = 83.5;
    CHECK_STR(m.selected_value(), "01 DOOR INTO SUMMER  1:23");
    now.fading = true;
    CHECK_STR(m.selected_value(), "01 DOOR INTO SUMMER  1:23  FADING");
}
