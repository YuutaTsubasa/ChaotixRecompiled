// The in-game menu's behaviour, without a window: pages are built when they
// are opened, so navigation and the settings it edits can be driven by feeding
// it key presses and inspecting the Config it was bound to.
#include "platform/menu.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iterator>
#include <string>
#include <vector>

#include "frontend/config.h"
#include "frontend/level_select.h"
#include "frontend/portrait.h"
#include "game/achievements.h"
#include "platform/backdrops.h"
#include "renderer/game_font.h"
#include "runtime/character_mods.h"
#include "runtime/lobby.h"
#include "runtime/rom.h"
#include "runtime/sprite_replace.h"
#include "runtime/tether.h"
#include "test_framework.h"

using namespace chaotix;

namespace {

// Walks to the entry with this label, so the tests do not break simply
// because one was added before it. The main page is a row (left/right), the
// options page a column (up/down). Returns false if it is not there.
bool select_row(Menu& m, const std::string& label) {
    // The pause menu is a row across the screen; every other page is a column.
    const SDL_Keycode step = m.page_name() == "main" ? SDLK_RIGHT : SDLK_DOWN;
    const std::string first = m.selected_label();
    for (int i = 0; i < 64; ++i) {
        if (m.selected_label() == label) return true;
        m.on_key(step);
        if (m.selected_label() == first) break;  // wrapped all the way round
    }
    return m.selected_label() == label;
}

void open_options(Menu& m) {
    m.toggle();
    CHECK(select_row(m, "OPTIONS"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK_STR(m.page_name(), "options");
}

// OPTIONS lists categories; this opens one of them.
void open_category(Menu& m, const std::string& label) {
    open_options(m);
    CHECK(select_row(m, label));
    CHECK(m.on_key(SDLK_RETURN));
    std::string page = label;
    for (char& ch : page) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    CHECK_STR(m.page_name(), page);
}

} // namespace

TEST(menu, closed_menu_ignores_keys) {
    Config cfg;
    cfg.set_defaults();
    Menu m;
    m.bind(cfg);
    CHECK(!m.open());
    CHECK(!m.on_key(SDLK_LEFT));
    CHECK(!m.on_key(SDLK_RETURN));
}

TEST(menu, first_key_after_opening_works) {
    // Pages used to be built while drawing, which left the first press after
    // opening with nothing to act on.
    Config cfg;
    cfg.set_defaults();
    Menu m;
    m.bind(cfg);
    m.toggle();
    CHECK_STR(m.page_name(), "main");
    CHECK(!m.selected_label().empty());
}

TEST(menu, escape_walks_back_one_page_then_closes) {
    Config cfg;
    cfg.set_defaults();
    Menu m;
    m.bind(cfg);
    open_options(m);
    CHECK(m.on_key(SDLK_ESCAPE));
    CHECK_STR(m.page_name(), "main");
    CHECK(m.on_key(SDLK_ESCAPE));
    CHECK(!m.open());
}

TEST(menu, left_right_changes_a_setting) {
    Config cfg;
    cfg.set_defaults();
    Menu m;
    m.bind(cfg);
    open_category(m, "VIDEO");
    CHECK(select_row(m, "WIDESCREEN"));

    const bool was = cfg.widescreen;
    CHECK(m.on_key(SDLK_LEFT));
    CHECK(cfg.widescreen != was);
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK(cfg.widescreen == was);
}

TEST(menu, volume_steps_and_clamps) {
    Config cfg;
    cfg.set_defaults();
    cfg.volume = 0;
    Menu m;
    m.bind(cfg);
    open_category(m, "SOUND");
    CHECK(select_row(m, "VOLUME"));

    CHECK(m.on_key(SDLK_LEFT));
    CHECK_EQ(cfg.volume, 0);    // already at the floor
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK_EQ(cfg.volume, 5);
    for (int i = 0; i < 30; ++i) CHECK(m.on_key(SDLK_RIGHT));
    CHECK_EQ(cfg.volume, 100);  // and at the ceiling
}

TEST(menu, aspect_ratio_cycles_both_ways) {
    Config cfg;
    cfg.set_defaults();
    Menu m;
    m.bind(cfg);
    open_category(m, "VIDEO");
    CHECK(select_row(m, "ASPECT RATIO"));

    const AspectMode start = cfg.viewport.aspect;
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK(cfg.viewport.aspect != start);
    CHECK(m.on_key(SDLK_LEFT));
    CHECK(cfg.viewport.aspect == start);
    // Stepping left off the first entry wraps to the last rather than
    // running off the end of the name table.
    CHECK(m.on_key(SDLK_LEFT));
    CHECK(int(cfg.viewport.aspect) >= 0 && int(cfg.viewport.aspect) < 5);
}

TEST(menu, selection_wraps_both_ways) {
    Config cfg;
    cfg.set_defaults();
    bool quit = false;
    Menu m;
    m.bind(cfg);
    Menu::Hooks h;
    h.quit = [&quit] { quit = true; };
    m.set_hooks(h);
    m.toggle();
    // QUIT is the last entry, so one step left from the first reaches it.
    CHECK(m.on_key(SDLK_LEFT));
    CHECK_STR(m.selected_label(), "QUIT");
    CHECK(m.on_key(SDLK_RETURN));
    CHECK(quit);
}

TEST(menu, an_action_row_ignores_left_and_right) {
    Config cfg;
    cfg.set_defaults();
    Menu m;
    m.bind(cfg);
    open_options(m);
    CHECK(select_row(m, "BACK"));

    CHECK(m.on_key(SDLK_LEFT));
    CHECK_STR(m.page_name(), "options");  // nothing happened
    CHECK(m.on_key(SDLK_RETURN));
    CHECK_STR(m.page_name(), "main");
}

TEST(menu, front_page_starts_the_game) {
    Config cfg;
    cfg.set_defaults();
    bool started = false;
    Menu m;
    m.bind(cfg);
    Menu::Hooks h;
    h.start_game = [&started] { started = true; };
    m.set_hooks(h);
    m.open_front();
    CHECK_STR(m.page_name(), "front");
    CHECK_STR(m.selected_label(), "START GAME");
    CHECK(m.on_key(SDLK_RETURN));
    CHECK(started);
    CHECK(!m.open());   // the menu gets out of the way
}

TEST(menu, sub_pages_return_to_the_page_that_opened_them) {
    // Options reached from the title screen must go back to the title screen,
    // not to the pause menu.
    Config cfg;
    cfg.set_defaults();
    Menu m;
    m.bind(cfg);
    m.open_front();
    CHECK(select_row(m, "OPTIONS"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK_STR(m.page_name(), "options");
    CHECK(m.on_key(SDLK_ESCAPE));
    CHECK_STR(m.page_name(), "front");

    m.toggle();          // close
    m.toggle();          // reopen, this time as the pause menu
    CHECK_STR(m.page_name(), "main");
    CHECK(select_row(m, "OPTIONS"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK(select_row(m, "BACK"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK_STR(m.page_name(), "main");
}

TEST(menu, controls_assigns_a_device_per_player) {
    Config cfg;
    cfg.set_defaults();
    Menu m;
    m.bind(cfg);
    // Two pads plugged in, so the choices are keyboard, pad1, pad2, none.
    Menu::Hooks h;
    h.pad_count = [] { return 2; };
    m.set_hooks(h);
    m.open_front();
    CHECK(select_row(m, "CONTROLS"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK_STR(m.page_name(), "controls");

    CHECK(select_row(m, "2P DEVICE"));
    CHECK_STR(cfg.device[1], "none");
    CHECK(m.on_key(SDLK_RIGHT));          // none wraps round to the first choice
    CHECK_STR(cfg.device[1], "auto");
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK_STR(cfg.device[1], "keyboard");
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK_STR(cfg.device[1], "pad1");
    CHECK(m.on_key(SDLK_LEFT));
    CHECK_STR(cfg.device[1], "keyboard");
    // Player 1 is untouched by any of that, and starts on auto so that a
    // gamepad works without anyone opening this page.
    CHECK_STR(cfg.device[0], "auto");
}

TEST(menu, rebinding_a_key_takes_the_next_press) {
    Config cfg;
    cfg.set_defaults();
    Menu m;
    m.bind(cfg);
    Menu::Hooks h;
    h.pad_count = [] { return 0; };
    m.set_hooks(h);
    m.open_front();
    CHECK(select_row(m, "CONTROLS"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK(select_row(m, "2P KEYBOARD"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK_STR(m.page_name(), "keys");

    CHECK(select_row(m, "A"));
    CHECK(m.on_key(SDLK_RETURN));          // start capturing
    CHECK(m.on_key(SDLK_K));               // this is the new binding
    CHECK_STR(cfg.keys2["a"], "K");
    // Player 1 keeps its own set.
    CHECK_STR(cfg.keys["a"], "Z");

    // Escape during a capture leaves the binding alone.
    CHECK(m.on_key(SDLK_RETURN));
    CHECK(m.on_key(SDLK_ESCAPE));
    CHECK_STR(cfg.keys2["a"], "K");
    CHECK_STR(m.page_name(), "keys");      // and does not navigate away
}

TEST(menu, closing_the_front_menu_hands_the_game_over) {
    // Escape, the menu button and the pad's back button (East) back out of
    // the front menu the same way, and each tells the program so: playing on
    // from the game's own title screen is then a hand-over, as START GAME is.
    for (int way = 0; way < 3; ++way) {
        Config config;
        config.set_defaults();
        Menu menu;
        menu.bind(config);
        int closed = 0;
        Menu::Hooks hooks;
        hooks.front_closed = [&closed] { ++closed; };
        menu.set_hooks(hooks);
        menu.open_front();
        if (way == 0) {
            CHECK(menu.on_key(SDLK_ESCAPE));
        } else if (way == 1) {
            CHECK(menu.on_pad(SDL_GAMEPAD_BUTTON_LEFT_STICK));
        } else {
            CHECK(menu.on_pad(SDL_GAMEPAD_BUTTON_EAST));
        }
        CHECK(!menu.open());
        CHECK_EQ(closed, 1);
    }
}

TEST(menu, leaving_the_pause_menu_is_not_a_hand_over) {
    Config config;
    config.set_defaults();
    Menu menu;
    menu.bind(config);
    int closed = 0;
    Menu::Hooks hooks;
    hooks.front_closed = [&closed] { ++closed; };
    menu.set_hooks(hooks);
    menu.toggle();  // the pause menu
    CHECK(menu.on_key(SDLK_ESCAPE));
    CHECK(!menu.open());
    menu.toggle();
    CHECK(menu.on_pad(SDL_GAMEPAD_BUTTON_EAST));
    CHECK(!menu.open());
    CHECK_EQ(closed, 0);
}

TEST(menu, dump_graphics_dumps_both_kinds_and_reads_its_older_name) {
    Config cfg;
    cfg.set_defaults();
    Menu m;
    m.bind(cfg);
    open_category(m, "EXTRAS");
    CHECK(select_row(m, "DUMP GRAPHICS"));
    CHECK(!cfg.dump_graphics);
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK(cfg.dump_graphics);
    // Saved under its new name; a file from before (DumpTiles) still counts.
    const std::string path = (std::filesystem::temp_directory_path() / "chaotix_test_dump.ini").string();
    CHECK(cfg.save(path));
    Config back;
    back.set_defaults();
    CHECK(back.load(path));
    CHECK(back.dump_graphics);
    if (FILE* f = std::fopen(path.c_str(), "w")) {
        std::fputs("[Mods]\nDumpTiles = true\n", f);
        std::fclose(f);
    }
    Config old;
    old.set_defaults();
    CHECK(old.load(path));
    CHECK(old.dump_graphics);
    std::remove(path.c_str());
}

TEST(menu, export_sprites_writes_the_sheets_when_pressed) {
    Config cfg;
    cfg.set_defaults();
    int exports = 0;
    Menu m;
    m.bind(cfg);
    Menu::Hooks h;
    h.export_sprites = [&exports] { ++exports; };
    m.set_hooks(h);
    open_category(m, "EXTRAS");
    CHECK(select_row(m, "EXPORT SPRITES"));
    CHECK(m.on_key(SDLK_RIGHT));  // not a value: stepping does nothing
    CHECK_EQ(exports, 0);
    CHECK(m.on_key(SDLK_RETURN));
    CHECK_EQ(exports, 1);
}

TEST(menu, resetting_awards_asks_twice) {
    Config cfg;
    cfg.set_defaults();
    int resets = 0;
    Menu m;
    m.bind(cfg);
    Menu::Hooks h;
    h.reset_awards = [&resets] { ++resets; };
    m.set_hooks(h);
    open_category(m, "EXTRAS");
    CHECK(select_row(m, "RESET AWARDS"));

    CHECK(m.on_key(SDLK_RETURN));   // asks for confirmation
    CHECK_EQ(resets, 0);
    CHECK(m.on_key(SDLK_RETURN));   // and only then does it
    CHECK_EQ(resets, 1);

    // Moving away cancels rather than leaving it armed.
    CHECK(m.on_key(SDLK_RETURN));
    CHECK(m.on_key(SDLK_DOWN));
    CHECK(select_row(m, "RESET AWARDS"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK_EQ(resets, 1);
}

TEST(menu, the_pause_menu_can_hand_the_title_back) {
    // The game has no way back to its title screen, so the pause menu carries
    // the only route there.
    Config cfg;
    cfg.set_defaults();
    bool back = false;
    Menu m;
    m.bind(cfg);
    Menu::Hooks h;
    h.back_to_title = [&back] { back = true; };
    m.set_hooks(h);

    m.toggle();                       // the pause menu, not the front end
    CHECK_STR(m.page_name(), "main");
    CHECK(select_row(m, "BACK TO TITLE"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK(back);
    CHECK(!m.open());                 // and it gets out of the way

    // The front end has no such entry: there is nothing to go back to.
    Menu f;
    f.bind(cfg);
    f.open_front();
    CHECK(!select_row(f, "BACK TO TITLE"));
}

TEST(menu, awards_page_is_reachable_and_returns) {
    Config cfg;
    cfg.set_defaults();
    Menu m;
    m.bind(cfg);
    m.toggle();
    CHECK(select_row(m, "AWARDS"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK_STR(m.page_name(), "awards");
    CHECK(m.on_key(SDLK_RETURN));
    CHECK_STR(m.page_name(), "main");
}

TEST(menu, time_attack_page_carries_the_choices_into_the_request) {
    // The front end's TIME ATTACK page collects the same fields the game's own
    // stage select offers; START must hand them over unchanged.
    Config cfg;
    cfg.set_defaults();
    Menu m;
    m.bind(cfg);
    bool started = false;
    stage_select::Request got;
    Menu::Hooks h;
    h.start_stage = [&](const stage_select::Request& r) { started = true; got = r; };
    m.set_hooks(h);

    m.open_front();
    CHECK(select_row(m, "TIME ATTACK"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK_STR(m.page_name(), "timeattack");

    CHECK(select_row(m, "PLACE"));
    m.on_key(SDLK_RIGHT);             // BOTANIC BASE -> SPEED SLIDER
    CHECK(select_row(m, "AT-TIME"));
    m.on_key(SDLK_RIGHT);             // MORNING -> DAY
    CHECK(select_row(m, "PLAYERS"));
    m.on_key(SDLK_RIGHT);             // 1 PLAYER -> 2 PLAYERS

    CHECK(select_row(m, "START"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK(started);
    CHECK_EQ(got.place, 1);
    CHECK_EQ(got.attime, 2);
    CHECK(got.two_players);
    CHECK(stage_select::valid(got));
    CHECK(!m.open());                 // and it gets out of the way of the game
}

TEST(menu, time_attack_offers_slot_1_only_once_the_rom_names_it) {
    // The names come from the ROM (stage_select::character_names); the unused
    // slot is skipped while it has only the game's placeholder.
    Config cfg;
    cfg.set_defaults();
    Menu m;
    m.bind(cfg);
    std::vector<std::string> names(std::begin(stage_select::kCharacters), std::end(stage_select::kCharacters));
    m.set_character_names(names);
    m.open_front();
    CHECK(select_row(m, "TIME ATTACK"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK(select_row(m, "PLAYER"));
    cfg.ta_player = 0;
    m.on_key(SDLK_RIGHT);
    CHECK_EQ(cfg.ta_player, 2);         // MIGHTY -> KNUCKLES, past the gap
    m.on_key(SDLK_LEFT);
    CHECK_EQ(cfg.ta_player, 0);
    // A patched ROM that gives slot 1 a name: offered.
    names[1] = "SONIC";
    m.set_character_names(names);
    m.on_key(SDLK_ESCAPE);
    CHECK(select_row(m, "TIME ATTACK"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK(select_row(m, "PLAYER"));
    m.on_key(SDLK_RIGHT);
    CHECK_EQ(cfg.ta_player, 1);
}

TEST(menu, time_attack_level_follows_what_the_place_has) {
    // The places do not all offer the same levels: the attractions start at 1,
    // TRAINING at 0. Changing the place must not leave a level behind that the
    // place has no such thing as.
    Config cfg;
    cfg.set_defaults();
    Menu m;
    m.bind(cfg);
    m.open_front();
    CHECK(select_row(m, "TIME ATTACK"));
    CHECK(m.on_key(SDLK_RETURN));

    CHECK(select_row(m, "LEVEL"));
    CHECK_STR(m.selected_label(), "LEVEL");
    for (int i = 0; i < 8; ++i) {
        // Every level the page will step to must be one BOTANIC BASE has.
        CHECK(stage_select::has_level(cfg.ta_place, cfg.ta_level));
        m.on_key(SDLK_RIGHT);
    }
    // TRAINING has level 0 and not level 5; BOTANIC BASE is the other way
    // round, so walking between them must move the level.
    cfg.ta_level = 5;
    CHECK(select_row(m, "PLACE"));
    for (int i = 0; i < 5; ++i) m.on_key(SDLK_RIGHT);   // -> TRAINING
    CHECK_STR(stage_select::kPlaces[cfg.ta_place].name, "TRAINING");
    CHECK(stage_select::has_level(cfg.ta_place, cfg.ta_level));
}

TEST(menu, the_gamepad_button_that_opens_the_menu_also_backs_out_of_it) {
    // Escape's counterpart on a controller. It is a setting, because which
    // spare button a pad has depends on the pad.
    Config cfg;
    cfg.set_defaults();
    CHECK_STR(cfg.menu_button, "leftstick");
    Menu m;
    m.bind(cfg);

    m.toggle();                       // the pause menu
    CHECK_STR(m.page_name(), "main");
    CHECK(select_row(m, "OPTIONS"));
    CHECK(m.on_pad(SDL_GAMEPAD_BUTTON_SOUTH));
    CHECK_STR(m.page_name(), "options");
    CHECK(m.on_pad(SDL_GAMEPAD_BUTTON_LEFT_STICK));
    CHECK_STR(m.page_name(), "main");  // one page back
    CHECK(m.on_pad(SDL_GAMEPAD_BUTTON_LEFT_STICK));
    CHECK(!m.open());                  // then out

    // Choose another button and the old one goes back to being the game's.
    cfg.menu_button = "rightstick";
    m.toggle();
    CHECK(m.on_pad(SDL_GAMEPAD_BUTTON_RIGHT_STICK));
    CHECK(!m.open());
}

TEST(menu, the_menu_button_can_be_chosen_on_the_controls_page) {
    Config cfg;
    cfg.set_defaults();
    Menu m;
    m.bind(cfg);
    m.toggle();
    CHECK(select_row(m, "CONTROLS"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK(select_row(m, "MENU BUTTON"));
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK_STR(cfg.menu_button, "rightstick");
    CHECK(m.on_key(SDLK_LEFT));
    CHECK_STR(cfg.menu_button, "leftstick");
    // Every offered button must be one SDL knows and the emulated pad does
    // not use, or choosing it would either do nothing or cost the game a
    // button.
    for (int i = 0; i < 8; ++i) {
        m.on_key(SDLK_RIGHT);
        CHECK(SDL_GetGamepadButtonFromString(cfg.menu_button.c_str()) != SDL_GAMEPAD_BUTTON_INVALID);
        for (const auto& [button, bound] : cfg.pads) CHECK(bound != cfg.menu_button);
        for (const auto& [button, bound] : cfg.pads2) CHECK(bound != cfg.menu_button);
    }
}

TEST(menu, the_binding_page_says_what_each_button_does) {
    // "A" on its own tells nobody anything. Every button of the pad carries a
    // note, and the ones this game ignores say so.
    Config cfg;
    cfg.set_defaults();
    Menu m;
    m.bind(cfg);
    m.toggle();
    CHECK(select_row(m, "CONTROLS"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK(select_row(m, "1P GAMEPAD"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK_STR(m.page_name(), "keys");

    CHECK(select_row(m, "C"));
    CHECK_STR(m.selected_note(), "JUMP");
    CHECK(select_row(m, "B"));
    CHECK_STR(m.selected_note(), "HOLD PARTNER STILL");
    CHECK(select_row(m, "START"));
    CHECK_STR(m.selected_note(), "PAUSE");
    for (const char* unused : {"X", "Y", "Z", "MODE"}) {
        CHECK(select_row(m, unused));
        CHECK_STR(m.selected_note(), "NOT USED BY THIS GAME");
    }
    // Only BACK is without one.
    for (const char* b : {"UP", "DOWN", "LEFT", "RIGHT", "A"}) {
        CHECK(select_row(m, b));
        CHECK(!m.selected_note().empty());
    }
    CHECK(select_row(m, "BACK"));
    CHECK(m.selected_note().empty());
}

TEST(menu, confirm_changes_a_setting) {
    // Enter (or the pad's confirm button) on a value steps it forward, as
    // Right would, instead of doing nothing.
    Config cfg;
    cfg.set_defaults();
    Menu m;
    m.bind(cfg);
    open_category(m, "VIDEO");
    CHECK(select_row(m, "WIDESCREEN"));
    const bool was = cfg.widescreen;
    CHECK(m.on_key(SDLK_RETURN));
    CHECK(cfg.widescreen != was);
    CHECK(m.on_pad(SDL_GAMEPAD_BUTTON_SOUTH));
    CHECK(cfg.widescreen == was);
    CHECK_STR(m.page_name(), "video");
}

namespace {

// Lays the menu out on an off-screen software renderer, so taps have rows to
// land on, the way the phone build sees them.
struct Drawn {
    SDL_Surface* surface = nullptr;
    SDL_Renderer* renderer = nullptr;
    ui::Ui g;
    achievements::Tracker ach;
    bool ok = false;
    explicit Drawn(int w = 1280, int h = 720) {
        surface = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ARGB8888);
        renderer = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
        ok = renderer && g.init(renderer);
    }
    ~Drawn() {
        g.shutdown();
        if (renderer) SDL_DestroyRenderer(renderer);
        if (surface) SDL_DestroySurface(surface);
    }
    void draw(Menu& m) {
        g.set_zoom(m.zoom());  // as the app does (MENU SIZE)
        g.begin_frame();
        m.draw(g, ach);
        g.end_frame();
    }
    void tap(Menu& m, float x, float y) {
        m.on_touch_down(x, y);
        m.on_touch_up();
        draw(m);
    }
};

// Taps down the middle of the screen until the entry is selected; returns the
// height it was found at, or a negative number.
float tap_select(Drawn& d, Menu& m, const std::string& label) {
    for (float y = 0; y < 720; y += 4) {
        if (m.selected_label() == label) return y - 4;
        d.tap(m, 640, y);
    }
    return m.selected_label() == label ? 716.0f : -1.0f;
}

} // namespace

TEST(menu, tapping_an_option_changes_it) {
    // Issue #9: on a phone a second tap on an option did nothing at all.
    Config cfg;
    cfg.set_defaults();
    Menu m;
    m.bind(cfg);
    open_category(m, "VIDEO");
    Drawn d;
    CHECK(d.ok);
    if (!d.ok) return;
    d.draw(m);

    CHECK(select_row(m, "SCALING"));  // away from the row under test
    const bool was = cfg.widescreen;
    const float y = tap_select(d, m, "WIDESCREEN");
    CHECK(y >= 0);
    CHECK(cfg.widescreen == was);  // the first tap only selects
    d.tap(m, 640, y);
    CHECK(cfg.widescreen != was);  // the second changes it
    d.tap(m, 640, y);
    CHECK(cfg.widescreen == was);
}

TEST(menu, tapping_the_back_arrow_steps_backwards) {
    Config cfg;
    cfg.set_defaults();
    Menu m;
    m.bind(cfg);
    open_category(m, "VIDEO");
    Drawn d;
    CHECK(d.ok);
    if (!d.ok) return;
    d.draw(m);

    const float y = tap_select(d, m, "ASPECT RATIO");
    CHECK(y >= 0);
    // Somewhere along the row a tap steps the value back rather than forward:
    // the "<" arrow. Everywhere else steps it forward.
    bool stepped_back = false, stepped_forward = false;
    for (float x = 1280; x > 0 && !stepped_back; x -= 4) {
        const int before = int(cfg.viewport.aspect);
        d.tap(m, x, y);
        const int after = int(cfg.viewport.aspect);
        if (after == (before + 4) % 5) stepped_back = true;
        else if (after == (before + 1) % 5) stepped_forward = true;
    }
    CHECK(stepped_back);
    CHECK(stepped_forward);
}

TEST(menu, cheats_are_switched_on_from_options) {
    // Issue #20: OPTIONS > EXTRAS > CHEATS lists the built-in cheats as on/off rows.
    Config cfg;
    cfg.set_defaults();
    Menu m;
    m.bind(cfg);
    m.open_front();
    CHECK(select_row(m, "OPTIONS"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK(select_row(m, "EXTRAS"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK(select_row(m, "CHEATS"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK_STR(m.page_name(), "cheats");
    CHECK(select_row(m, "SPEED SHOES"));
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK(cfg.cheats.count("speed") == 1);
    CHECK(m.on_key(SDLK_RETURN));
    CHECK(cfg.cheats.count("speed") == 0);
}

TEST(menu, lobby_shortcuts_are_set_from_extras_and_kept) {
    // Issue #44: OPTIONS > EXTRAS > LOBBY SHORTCUTS, from the pause menu too,
    // so the choice can be changed while walking the lobby.
    Config cfg;
    cfg.set_defaults();
    CHECK(!cfg.lobby_shortcuts);
    CHECK_EQ(cfg.lobby_partner, -1);
    CHECK_EQ(cfg.lobby_time, -1);
    Menu m;
    m.bind(cfg);
    open_category(m, "EXTRAS");
    CHECK(select_row(m, "LOBBY SHORTCUTS"));
    CHECK_STR(m.selected_value(), "OFF");
    CHECK(m.on_key(SDLK_RETURN));
    CHECK_STR(m.page_name(), "lobby");
    CHECK(select_row(m, "SHORTCUTS"));
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK(cfg.lobby_shortcuts);
    CHECK(select_row(m, "PARTNER"));
    CHECK_STR(m.selected_value(), "CATCHER");
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK_STR(m.selected_value(), "MIGHTY");
    CHECK(m.on_key(SDLK_RIGHT));  // slot 1, the placeholder, is skipped
    CHECK_EQ(cfg.lobby_partner, 2);
    CHECK(m.on_key(SDLK_LEFT));
    CHECK(m.on_key(SDLK_LEFT));
    CHECK_EQ(cfg.lobby_partner, -1);
    CHECK(m.on_key(SDLK_LEFT));  // wraps to NONE (issue #55), after the characters
    CHECK_STR(m.selected_value(), "NONE");
    CHECK_EQ(cfg.lobby_partner, lobby::kPartnerNone);
    CHECK(m.on_key(SDLK_LEFT));  // the last character
    CHECK_STR(m.selected_value(), "ESPIO");
    CHECK(select_row(m, "TIME OF DAY"));
    CHECK_STR(m.selected_value(), "GAME");
    CHECK(m.on_key(SDLK_LEFT));
    CHECK_STR(m.selected_value(), "NIGHT");
    CHECK_EQ(cfg.lobby_time, 3);
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK_STR(m.selected_value(), "MORNING");
    CHECK(m.on_key(SDLK_ESCAPE));
    CHECK_STR(m.page_name(), "extras");
    CHECK(select_row(m, "LOBBY SHORTCUTS"));
    CHECK_STR(m.selected_value(), "ON");
    // Kept across a restart.
    const std::string path = (std::filesystem::temp_directory_path() / "chaotix_lobby_test.ini").string();
    CHECK(cfg.save(path));
    Config back;
    CHECK(back.load(path));
    CHECK(back.lobby_shortcuts);
    CHECK_EQ(back.lobby_partner, 7);
    CHECK_EQ(back.lobby_time, 0);
    std::filesystem::remove(path);
}

TEST(menu, the_lobby_shortcut_partner_can_be_none) {
    // Issue #55: CATCHER, the characters, NONE, round again; kept as -2.
    Config cfg;
    cfg.set_defaults();
    cfg.lobby_shortcuts = true;
    cfg.lobby_partner = stage_select::kEspio;
    Menu m;
    m.bind(cfg);
    CHECK(m.show_page("lobby"));
    CHECK(select_row(m, "PARTNER"));
    CHECK_STR(m.selected_value(), "ESPIO");
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK_STR(m.selected_value(), "NONE");
    CHECK_EQ(cfg.lobby_partner, lobby::kPartnerNone);
    const std::string path = (std::filesystem::temp_directory_path() / "chaotix_lobby_none.ini").string();
    CHECK(cfg.save(path));
    Config back;
    CHECK(back.load(path));
    CHECK_EQ(back.lobby_partner, lobby::kPartnerNone);
    std::filesystem::remove(path);
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK_STR(m.selected_value(), "CATCHER");
    CHECK_EQ(cfg.lobby_partner, -1);
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK_STR(m.selected_value(), "MIGHTY");
    // NONE plays the attractions SOLO, whatever GAMEPLAY says; otherwise
    // GAMEPLAY's setting applies.
    CHECK(tether::partner_in_force(cfg.tether_partner, false, true) == tether::Partner::Solo);
    CHECK(tether::partner_in_force(int(tether::Partner::Follow), false, true) == tether::Partner::Solo);
    CHECK(tether::partner_in_force(int(tether::Partner::Follow), false, false) == tether::Partner::Follow);
}

TEST(menu, ring_tether_options_are_set_from_extras_and_kept) {
    // Issue #43: OPTIONS > GAMEPLAY, the original game by default.
    Config cfg;
    cfg.set_defaults();
    CHECK_EQ(cfg.tether_strength, 0);
    CHECK_EQ(cfg.tether_partner, 0);
    Menu m;
    m.bind(cfg);
    open_category(m, "GAMEPLAY");
    CHECK_STR(m.page_name(), "gameplay");
    CHECK(select_row(m, "TETHER"));
    CHECK_STR(m.selected_value(), "NORMAL");
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK_STR(m.selected_value(), "WEAK");
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK_STR(m.selected_value(), "STRONG");
    CHECK(m.on_key(SDLK_RIGHT));  // wraps
    CHECK_STR(m.selected_value(), "NORMAL");
    CHECK(m.on_key(SDLK_LEFT));
    CHECK_STR(m.selected_value(), "STRONG");
    CHECK_EQ(cfg.tether_strength, 2);
    CHECK(select_row(m, "PARTNER"));
    CHECK_STR(m.selected_value(), "NORMAL");
    CHECK(m.on_key(SDLK_LEFT));
    CHECK_STR(m.selected_value(), "SOLO");
    CHECK(m.on_key(SDLK_LEFT));
    CHECK_STR(m.selected_value(), "FOLLOW");
    CHECK_EQ(cfg.tether_partner, 1);
    CHECK(m.on_key(SDLK_ESCAPE));
    CHECK_STR(m.page_name(), "options");
    // Kept across a restart, by name; a number or another case reads too.
    const std::string path = (std::filesystem::temp_directory_path() / "chaotix_tether_test.ini").string();
    CHECK(cfg.save(path));
    Config back;
    CHECK(back.load(path));
    CHECK_EQ(back.tether_strength, 2);
    CHECK_EQ(back.tether_partner, 1);
    {
        FILE* f = std::fopen(path.c_str(), "w");
        CHECK(f != nullptr);
        if (f) {
            std::fputs("[Tether]\nStrength = weak\nPartner = 2\n", f);
            std::fclose(f);
        }
    }
    Config other;
    CHECK(other.load(path));
    CHECK_EQ(other.tether_strength, 1);
    CHECK_EQ(other.tether_partner, 2);
    {
        FILE* f = std::fopen(path.c_str(), "w");
        if (f) {
            std::fputs("[Tether]\nStrength = tight\nPartner = 9\n", f);
            std::fclose(f);
        }
    }
    Config bad;
    CHECK(bad.load(path));
    CHECK_EQ(bad.tether_strength, 0);  // not a setting: the original
    CHECK_EQ(bad.tether_partner, 0);
    std::filesystem::remove(path);
}

TEST(menu, speedrun_tools_are_set_from_extras_and_kept) {
    // Issue #47: OPTIONS > EXTRAS > SPEEDRUN. Everything off to begin with,
    // and an old settings file without the section keeps it that way.
    Config cfg;
    cfg.set_defaults();
    CHECK(!cfg.sr_timer && !cfg.sr_splits && !cfg.sr_input && !cfg.sr_compare_game);
    int started = 0, reset = 0;
    Menu m;
    Menu::Hooks hooks;
    hooks.speedrun_start = [&] { ++started; };
    hooks.speedrun_reset = [&] { ++reset; };
    m.set_hooks(hooks);
    m.bind(cfg);
    open_category(m, "EXTRAS");
    CHECK(select_row(m, "SPEEDRUN"));
    CHECK_STR(m.selected_value(), "OFF");
    CHECK(m.on_key(SDLK_RETURN));
    CHECK_STR(m.page_name(), "speedrun");
    CHECK(select_row(m, "TIMER"));
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK(cfg.sr_timer);
    CHECK(select_row(m, "SPLITS"));
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK(cfg.sr_splits);
    CHECK(select_row(m, "COMPARE"));
    CHECK_STR(m.selected_value(), "REAL TIME");
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK(cfg.sr_compare_game);
    CHECK(select_row(m, "INPUT DISPLAY"));
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK(cfg.sr_input);
    CHECK(select_row(m, "TIMER CORNER"));
    CHECK_STR(m.selected_value(), "TOP RIGHT");
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK_STR(m.selected_value(), "BOTTOM LEFT");
    CHECK_STR(cfg.sr_timer_corner, "BottomLeft");
    CHECK(select_row(m, "INPUT CORNER"));
    CHECK_STR(m.selected_value(), "BOTTOM LEFT");
    CHECK(m.on_key(SDLK_LEFT));
    CHECK_STR(cfg.sr_input_corner, "TopRight");
    CHECK(select_row(m, "OPACITY"));
    CHECK(m.on_key(SDLK_LEFT));
    CHECK_EQ(cfg.sr_opacity, 80);
    // The keys are captured like the other host keys.
    CHECK(select_row(m, "RESET RUN KEY"));
    CHECK_STR(m.selected_value(), "END");
    CHECK(m.on_key(SDLK_RETURN));
    CHECK_STR(m.selected_value(), "PRESS A KEY");
    CHECK(m.on_key(SDLK_DELETE));
    CHECK_STR(cfg.sr_reset_key, "Delete");
    // Starting and resetting by hand close the menu, for the game.
    CHECK(select_row(m, "START RUN"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK_EQ(started, 1);
    CHECK(!m.open());
    CHECK(m.show_page("speedrun"));
    CHECK(select_row(m, "RESET RUN"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK_EQ(reset, 1);
    CHECK(m.show_page("speedrun"));
    CHECK(m.on_key(SDLK_ESCAPE));
    CHECK_STR(m.page_name(), "extras");
    CHECK(select_row(m, "SPEEDRUN"));
    CHECK_STR(m.selected_value(), "ON");
    // Kept across a restart.
    const std::string path = (std::filesystem::temp_directory_path() / "chaotix_speedrun_test.ini").string();
    CHECK(cfg.save(path));
    Config back;
    CHECK(back.load(path));
    CHECK(back.sr_timer && back.sr_splits && back.sr_input && back.sr_compare_game);
    CHECK_STR(back.sr_timer_corner, "BottomLeft");
    CHECK_STR(back.sr_input_corner, "TopRight");
    CHECK_EQ(back.sr_opacity, 80);
    CHECK_STR(back.sr_start_key, "Home");
    CHECK_STR(back.sr_reset_key, "Delete");
    std::filesystem::remove(path);
}

TEST(menu, options_is_split_into_pages_that_fit) {
    // OPTIONS had grown to about twenty rows and ran off a phone's screen; it
    // is now a list of categories, each a short page that steps back to it.
    Config cfg;
    cfg.set_defaults();
    Menu m;
    m.bind(cfg);
    Menu::Hooks h;
    h.share_mods_folder = [] {};  // the longest EXTRAS, as on Android
    m.set_hooks(h);
    open_options(m);
    CHECK(select_row(m, "VIDEO"));
    CHECK(!select_row(m, "WIDESCREEN"));
    for (const char* cat : {"VIDEO", "SOUND", "INPUT", "EXTRAS", "GAMEPLAY"}) {
        CHECK(select_row(m, cat));
        CHECK(m.on_key(SDLK_RETURN));
        int rows = 0;
        const std::string first = m.selected_label();
        do { ++rows; m.on_key(SDLK_DOWN); } while (m.selected_label() != first && rows < 64);
        // Ten rows and BACK at most: VIDEO has that many since SHOW FPS
        // (issue #54); a page longer than the screen scrolls (issue #49).
        CHECK(rows <= 11);
        CHECK(m.on_key(SDLK_ESCAPE));
        CHECK_STR(m.page_name(), "options");
    }
    // CHEATS sits under EXTRAS and steps back there, both ways.
    CHECK(select_row(m, "EXTRAS"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK(select_row(m, "CHEATS"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK(m.on_key(SDLK_ESCAPE));
    CHECK_STR(m.page_name(), "extras");
    CHECK(select_row(m, "CHEATS"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK(select_row(m, "BACK"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK_STR(m.page_name(), "extras");
}

TEST(menu, the_cheats_page_is_drawn_as_a_list) {
    // It used to be drawn as the awards page, so its rows could not be seen
    // or tapped.
    Config cfg;
    cfg.set_defaults();
    Menu m;
    m.bind(cfg);
    m.show_page("cheats");
    Drawn d;
    CHECK(d.ok);
    if (!d.ok) return;
    d.draw(m);
    const float y = tap_select(d, m, "SPEED SHOES");
    CHECK(y >= 0);
    d.tap(m, 640, y);
    CHECK(cfg.cheats.count("speed") == 1);
}

TEST(menu, text_lands_on_whole_pixels) {
    // Issue #5 (a comment from 0.4.3): at fractional positions the pixel face
    // was sampled between texels and showed thin lines from its neighbours in
    // the glyph atlas. Text at x + 0.4 must look exactly like text at x.
    Drawn d;
    CHECK(d.ok);
    if (!d.ok) return;
    auto grab = [&d](float x, float y) {
        SDL_SetRenderDrawColor(d.renderer, 0, 0, 0, 255);
        SDL_RenderClear(d.renderer);
        d.g.begin_frame();
        d.g.text(ui::Font::Pixel, x, y, "OPTIONS CONTROLS", ui::theme::accent);
        d.g.end_frame();
        SDL_RenderPresent(d.renderer);
        SDL_Surface* s = SDL_RenderReadPixels(d.renderer, nullptr);
        std::vector<uint8_t> px;
        if (s) {
            const auto* p = static_cast<const uint8_t*>(s->pixels);
            px.assign(p, p + size_t(s->pitch) * size_t(s->h));
            SDL_DestroySurface(s);
        }
        return px;
    };
    const auto whole = grab(100.0f, 100.0f);
    CHECK(!whole.empty());
    CHECK(grab(100.4f, 100.3f) == whole);
}

TEST(menu, touch_opacity_is_set_and_kept) {
    // Issue #33: how opaque the touch controls are, at rest and held.
    Config cfg;
    cfg.set_defaults();
    Menu m;
    m.bind(cfg);
    open_category(m, "INPUT");
    CHECK(select_row(m, "TOUCH OPACITY"));
    const int rest = cfg.touch_opacity;
    CHECK(m.on_key(SDLK_LEFT));
    CHECK_EQ(cfg.touch_opacity, rest - 5);
    for (int i = 0; i < 30; ++i) m.on_key(SDLK_LEFT);
    CHECK_EQ(cfg.touch_opacity, 0);   // the floor
    CHECK(select_row(m, "PRESSED OPACITY"));
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK_EQ(cfg.touch_pressed_opacity, 100);  // already the ceiling
    CHECK(m.on_key(SDLK_LEFT));
    CHECK_EQ(cfg.touch_pressed_opacity, 95);

    const std::string path = "touch_opacity_test.ini";
    CHECK(cfg.save(path));
    Config back;
    CHECK(back.load(path));
    CHECK_EQ(back.touch_opacity, 0);
    CHECK_EQ(back.touch_pressed_opacity, 95);
    std::remove(path.c_str());
}

TEST(menu, fast_forward_settings_are_set_and_kept) {
    // Issue #45: the key and pad button on the CONTROLS page, the on-screen
    // button, HOLD / TOGGLE and the speed under INPUT; all in the settings.
    Config cfg;
    cfg.set_defaults();
    CHECK_STR(cfg.ff_key, "Tab");
    CHECK_STR(cfg.ff_button, "righttrigger");
    CHECK(!cfg.touch_ff);
    CHECK(cfg.ff_mode == FastForwardMode::Hold);
    CHECK(cfg.ff_speed == FastForwardSpeed::Max);

    Menu m;
    m.bind(cfg);
    m.toggle();
    CHECK(select_row(m, "CONTROLS"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK(select_row(m, "FAST FORWARD KEY"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK(m.on_key(SDLK_GRAVE));
    CHECK_STR(cfg.ff_key, SDL_GetScancodeName(SDL_SCANCODE_GRAVE));
    // Escape gives up on a capture without changing anything.
    CHECK(m.on_key(SDLK_RETURN));
    CHECK(m.on_key(SDLK_ESCAPE));
    CHECK_STR(cfg.ff_key, SDL_GetScancodeName(SDL_SCANCODE_GRAVE));
    CHECK_STR(m.page_name(), "controls");
    // The pad button never lands on the menu button nor on one the emulated
    // pad uses.
    CHECK(select_row(m, "FAST FORWARD PAD"));
    for (int i = 0; i < 10; ++i) {
        m.on_key(SDLK_RIGHT);
        CHECK(cfg.ff_button != cfg.menu_button);
        for (const auto& [button, bound] : cfg.pads) CHECK(bound != cfg.ff_button);
    }
    cfg.ff_button = "rightstick";
    // And the menu button steps over the fast-forward one.
    CHECK(select_row(m, "MENU BUTTON"));
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK(cfg.menu_button != cfg.ff_button);
    m.close();

    open_category(m, "INPUT");
    CHECK(select_row(m, "TOUCH FAST FORWARD"));
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK(cfg.touch_ff);
    CHECK(select_row(m, "FAST FORWARD"));
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK(cfg.ff_mode == FastForwardMode::Toggle);
    CHECK(select_row(m, "FAST FORWARD SPEED"));
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK(cfg.ff_speed == FastForwardSpeed::X2);  // MAX wraps round to 2X
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK(cfg.ff_speed == FastForwardSpeed::X4);

    const std::string path = "fast_forward_test.ini";
    CHECK(cfg.save(path));
    Config back;
    CHECK(back.load(path));
    CHECK_STR(back.ff_key, cfg.ff_key);
    CHECK_STR(back.ff_button, "rightstick");
    CHECK(back.touch_ff);
    CHECK(back.ff_mode == FastForwardMode::Toggle);
    CHECK(back.ff_speed == FastForwardSpeed::X4);
    cfg.ff_speed = FastForwardSpeed::X2;
    cfg.ff_mode = FastForwardMode::Hold;
    CHECK(cfg.save(path));
    CHECK(back.load(path));
    CHECK(back.ff_speed == FastForwardSpeed::X2);
    CHECK(back.ff_mode == FastForwardMode::Hold);
    std::remove(path.c_str());

    // A settings file from before fast-forward had settings keeps Tab, held,
    // at full speed, and no on-screen button.
    if (FILE* f = std::fopen(path.c_str(), "w")) {
        std::fputs("[Input]\nMenuButton = leftstick\n", f);
        std::fclose(f);
    }
    CHECK(back.load(path));
    CHECK_STR(back.ff_key, "Tab");
    CHECK_STR(back.ff_button, "righttrigger");
    CHECK(back.ff_mode == FastForwardMode::Hold);
    CHECK(back.ff_speed == FastForwardSpeed::Max);
    CHECK(!back.touch_ff);
    std::remove(path.c_str());
}

TEST(menu, the_menus_look_is_an_extras_setting_and_is_kept) {
    // Issue #8: the game's font and the zones behind the menu, on by
    // default; PIXEL and PLAIN give the earlier look back.
    Config cfg;
    cfg.set_defaults();
    CHECK(cfg.menu_game_font);
    CHECK(cfg.menu_backdrop_on());
    CHECK_STR(cfg.menu_background, "Cycle");
    Menu m;
    m.bind(cfg);
    open_category(m, "EXTRAS");
    CHECK(select_row(m, "MENU FONT"));
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK(!cfg.menu_game_font);
    CHECK(select_row(m, "MENU BACKGROUND"));
    CHECK_STR(m.selected_value(), "CYCLE");
    CHECK(m.on_key(SDLK_LEFT));  // wraps round to the last choice
    CHECK(!cfg.menu_backdrop_on());
    CHECK_STR(m.selected_value(), "PLAIN");

    const std::string path = "menu_look_test.ini";
    CHECK(cfg.save(path));
    Config back;
    CHECK(back.load(path));
    CHECK(!back.menu_game_font);
    CHECK(!back.menu_backdrop_on());
    std::remove(path.c_str());
}

TEST(menu, the_menu_background_can_be_any_one_zone) {
    // Issue #37: CYCLE, each of the seven zones by name, PLAIN; left and
    // right step through them and wrap.
    Config cfg;
    cfg.set_defaults();
    Menu m;
    m.bind(cfg);
    open_category(m, "EXTRAS");
    CHECK(select_row(m, "MENU BACKGROUND"));
    const char* const shown[] = {"WORLD ENTRANCE", "ISOLATED ISLAND", "BOTANIC BASE", "MARINA MADNESS",
                                 "SPEED SLIDER", "AMAZING ARENA", "TECHNO TOWER", "PLAIN", "CYCLE"};
    const char* const kept[] = {"WorldEntrance", "IsolatedIsland", "BotanicBase", "MarinaMadness",
                                "SpeedSlider", "AmazingArena", "TechnoTower", "Plain", "Cycle"};
    for (size_t i = 0; i < std::size(shown); ++i) {
        CHECK(m.on_key(SDLK_RIGHT));
        CHECK_STR(m.selected_value(), shown[i]);
        CHECK_STR(cfg.menu_background, kept[i]);
        CHECK_EQ(cfg.menu_backdrop_on(), i != 7);
        // A zone's index is its picture's; CYCLE and PLAIN have none.
        const int zone = cfg.menu_backdrop_zone();
        CHECK_EQ(zone >= 0, i < 7);
        if (zone >= 0) CHECK_STR(backdrop_specs()[size_t(zone)].name, shown[i]);
    }
    CHECK(m.on_key(SDLK_LEFT));
    CHECK_STR(m.selected_value(), "PLAIN");

    // Every value is saved as itself and read back.
    const std::string path = "menu_background_test.ini";
    for (const char* v : kept) {
        cfg.menu_background = v;
        CHECK(cfg.save(path));
        Config back;
        CHECK(back.load(path));
        CHECK_STR(back.menu_background, v);
    }
    std::remove(path.c_str());

    // How a value is read: case does not matter, the earlier ZONE is CYCLE,
    // and anything unknown is CYCLE too.
    CHECK_STR(parse_menu_background("Zone"), "Cycle");
    CHECK_STR(parse_menu_background("zone"), "Cycle");
    CHECK_STR(parse_menu_background("plain"), "Plain");
    CHECK_STR(parse_menu_background("worldentrance"), "WorldEntrance");
    CHECK_STR(parse_menu_background("MARINAMADNESS"), "MarinaMadness");
    CHECK_STR(parse_menu_background("Moon"), "Cycle");
    CHECK_STR(parse_menu_background(""), "Cycle");
    for (const char* legacy : {"Zone", "Plain"}) {
        std::FILE* f = std::fopen(path.c_str(), "w");
        CHECK(f != nullptr);
        if (!f) return;
        std::fprintf(f, "[Video]\nMenuBackground = %s\n", legacy);
        std::fclose(f);
        Config old;
        CHECK(old.load(path));
        CHECK_STR(old.menu_background, std::string(legacy) == "Zone" ? "Cycle" : "Plain");
    }
    std::remove(path.c_str());
}

TEST(menu, a_chosen_zone_stays_and_cycle_moves_on) {
    // MenuBackdrops without a ROM or renderer: only the choosing, which needs
    // neither. A chosen zone is not left by advance().
    MenuBackdrops b;
    CHECK_EQ(b.chosen(), -1);
    b.choose(backdrop_find_zone("WorldEntrance"));
    CHECK_EQ(b.chosen(), backdrop_find_zone("WorldEntrance"));
    b.choose(99);  // not a zone: CYCLE
    CHECK_EQ(b.chosen(), -1);
    CHECK_EQ(backdrop_find_zone("Moon"), -1);
    CHECK_EQ(int(backdrop_menu_order().size()), int(backdrop_specs().size()));
}

namespace {

// A stand-in for the ROM's font: every glyph a block of colour 1 (white)
// with a clear left column.
GameFont block_font() {
    std::vector<uint8_t> tiles(size_t(kGameFontTileCount) * 32);
    for (size_t r = 0; r < tiles.size() / 4; ++r) {
        tiles[r * 4] = 0x01;
        tiles[r * 4 + 1] = tiles[r * 4 + 2] = tiles[r * 4 + 3] = 0x11;
    }
    std::array<uint16_t, 16> palette{};
    palette[1] = 0x0EEE;
    GameFont f;
    std::string why;
    CHECK(game_font_from_tiles(tiles, palette, f, &why));
    return f;
}

} // namespace

TEST(menu, the_game_font_lays_out_in_whole_cells) {
    Drawn d;
    CHECK(d.ok);
    if (!d.ok) return;
    CHECK(d.g.set_game_font(block_font()));
    d.g.begin_frame();  // 1280x720: scale 1, the pixel face at 16 and 32
    const float pixel_width = d.g.text_width(ui::Font::Pixel, "AB");
    d.g.enable_game_font(true);
    CHECK(d.g.uses_game_font(ui::Font::Pixel, "BACK TO TITLE"));
    CHECK(d.g.uses_game_font(ui::Font::Pixel, "Resume"));
    CHECK(!d.g.uses_game_font(ui::Font::Pixel, "65%"));      // no '%': the pixel face
    CHECK(!d.g.uses_game_font(ui::Font::Small, "AB"));       // captions keep their face
    CHECK_EQ(int(d.g.text_width(ui::Font::Pixel, "AB")), 16);     // 8 a character
    CHECK_EQ(int(d.g.text_width(ui::Font::PixelBig, "AB")), 32);  // twice that
    CHECK_EQ(int(d.g.line_height(ui::Font::Pixel)), 16);
    CHECK_EQ(int(d.g.line_height(ui::Font::PixelBig)), 32);
    CHECK_EQ(ui::game_font_scale(24), 2);  // 1080p's pixel face
    CHECK_EQ(ui::game_font_scale(8), 1);
    CHECK_EQ(ui::game_font_scale(48), 3);
    d.g.enable_game_font(false);
    CHECK_EQ(d.g.text_width(ui::Font::Pixel, "AB"), pixel_width);

    // Drawn at a fractional position, a glyph still lands on whole pixels in
    // its own colour: nothing in between, and the clear column stays clear.
    d.g.enable_game_font(true);
    SDL_SetRenderDrawColor(d.renderer, 0, 0, 0, 255);
    SDL_RenderClear(d.renderer);
    d.g.text(ui::Font::PixelBig, 100.4f, 100.3f, "A", ui::theme::text);
    SDL_RenderPresent(d.renderer);
    SDL_Surface* s = SDL_RenderReadPixels(d.renderer, nullptr);
    CHECK(s != nullptr);
    if (!s) return;
    SDL_Surface* c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_XRGB8888);
    SDL_DestroySurface(s);
    if (!c) return;
    auto at = [c](int x, int y) {
        return static_cast<const uint32_t*>(c->pixels)[size_t(y) * size_t(c->pitch / 4) + size_t(x)] & 0xFFFFFF;
    };
    int lit = 0, other = 0;
    for (int y = 96; y < 140; ++y)
        for (int x = 96; x < 120; ++x) {
            const uint32_t p = at(x, y);
            if (p == 0xFFFFFF) ++lit;
            else if (p != 0) ++other;
        }
    CHECK_EQ(lit, 14 * 32);  // seven columns of the glyph, doubled, 32 lines
    CHECK_EQ(other, 0);
    CHECK_EQ(at(100, 110), 0u);       // the clear column, two pixels wide
    CHECK_EQ(at(101, 110), 0u);
    CHECK_EQ(at(102, 110), 0xFFFFFFu);
    SDL_DestroySurface(c);
}

TEST(menu, a_zone_picture_fills_the_panel_when_asked) {
    Drawn d;
    CHECK(d.ok);
    if (!d.ok) return;
    Config cfg;
    cfg.set_defaults();
    Menu m;
    m.bind(cfg);
    // A plain red "zone", under a black tint.
    SDL_Texture* red = SDL_CreateTexture(d.renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STATIC, 4, 4);
    CHECK(red != nullptr);
    if (!red) return;
    std::vector<uint32_t> px(16, 0xFF0000);
    SDL_UpdateTexture(red, nullptr, px.data(), 16);
    m.set_backdrop(red, 4, 4, ui::Color{0, 0, 0, 255}, ui::Color{0, 255, 0, 255});
    open_options(m);
    auto panel_red = [&] {
        SDL_SetRenderDrawColor(d.renderer, 0, 0, 0, 255);
        SDL_RenderClear(d.renderer);
        d.draw(m);
        SDL_RenderPresent(d.renderer);
        SDL_Surface* s = SDL_RenderReadPixels(d.renderer, nullptr);
        if (!s) return -1;
        SDL_Surface* c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_XRGB8888);
        SDL_DestroySurface(s);
        if (!c) return -1;
        // Inside the panel, right of its labels: only the panel's fill.
        const uint32_t p = static_cast<const uint32_t*>(c->pixels)[size_t(360) * size_t(c->pitch / 4) + 930];
        SDL_DestroySurface(c);
        return int((p >> 16) & 0xFF);
    };
    const int zone = panel_red();
    cfg.menu_background = "Plain";
    const int plain = panel_red();
    CHECK(zone > 30);    // the red shows through the tint
    CHECK(plain < 30);   // the theme's near-black
    SDL_DestroyTexture(red);
}

// Issue #46: the NETPLAY page, from the front end: HOST and JOIN ask the app,
// the address is typed (keyboard text, or the arrows as a pad would), the
// port and input delay step, and the status comes from the app.
TEST(menu, netplay_page_hosts_joins_and_takes_an_address) {
    Config cfg;
    cfg.set_defaults();
    Menu m;
    m.bind(cfg);
    Menu::Hooks h;
    int hosted = 0, joined = 0, left = 0;
    bool active = false;
    h.netplay_host = [&] { ++hosted; active = true; };
    h.netplay_join = [&] { ++joined; };
    h.netplay_leave = [&] { ++left; active = false; };
    h.netplay_active = [&] { return active; };
    h.netplay_status = [&] { return std::string(active ? "WAITING FOR PLAYER 2" : "OFF"); };
    m.set_hooks(h);
    m.open_front();
    CHECK(select_row(m, "NETPLAY"));
    m.on_key(SDLK_RETURN);
    CHECK_STR(m.page_name(), "netplay");
    CHECK_STR(m.selected_label(), "HOST A GAME");
    m.on_key(SDLK_RETURN);
    CHECK_EQ(hosted, 1);
    // STATUS only shows: the selection steps over it.
    CHECK(!select_row(m, "STATUS"));
    CHECK(select_row(m, "DISCONNECT"));
    m.on_key(SDLK_RETURN);
    CHECK_EQ(left, 1);

    // The address: typed (a space is not taken), with a backspace; Enter
    // keeps it.
    CHECK(select_row(m, "ADDRESS"));
    CHECK_STR(m.selected_value(), "TYPE ONE");
    m.on_key(SDLK_RETURN);
    CHECK(m.editing_text());
    CHECK(m.on_text("192.168.1.2x"));
    m.on_key(SDLK_BACKSPACE);
    CHECK(m.on_text("0:7845 "));
    m.on_key(SDLK_RETURN);
    CHECK(!m.editing_text());
    CHECK_STR(cfg.netplay_address, "192.168.1.20:7845");
    // Escape puts the old text back, and leaves the page where it was.
    m.on_key(SDLK_RETURN);
    m.on_text("junk");
    m.on_key(SDLK_ESCAPE);
    CHECK(!m.editing_text());
    CHECK_STR(cfg.netplay_address, "192.168.1.20:7845");
    CHECK_STR(m.page_name(), "netplay");
    // A pad: left deletes, up and down step the last character through the
    // ones allowed, right adds one, and the confirm button keeps it.
    m.on_pad(SDL_GAMEPAD_BUTTON_SOUTH);
    CHECK(m.editing_text());
    m.on_pad(SDL_GAMEPAD_BUTTON_DPAD_LEFT);   // ...:784
    m.on_pad(SDL_GAMEPAD_BUTTON_DPAD_UP);     // ...:785
    m.on_pad(SDL_GAMEPAD_BUTTON_DPAD_RIGHT);  // ...:7855
    m.on_pad(SDL_GAMEPAD_BUTTON_DPAD_DOWN);   // ...:7854
    m.on_pad(SDL_GAMEPAD_BUTTON_SOUTH);
    CHECK_STR(cfg.netplay_address, "192.168.1.20:7854");

    // The port: digits only, kept in range; left and right step it.
    CHECK(select_row(m, "PORT"));
    m.on_key(SDLK_RETURN);
    for (int i = 0; i < 4; ++i) m.on_key(SDLK_BACKSPACE);
    m.on_text("9a0b0c1");
    m.on_key(SDLK_RETURN);
    CHECK_EQ(cfg.netplay_port, 9001);
    m.on_key(SDLK_RIGHT);
    CHECK_EQ(cfg.netplay_port, 9002);
    m.on_key(SDLK_RETURN);
    m.on_text("99999");  // 90029: too high, the old port stays
    m.on_key(SDLK_RETURN);
    CHECK_EQ(cfg.netplay_port, 9002);

    CHECK(select_row(m, "INPUT DELAY"));
    CHECK_STR(m.selected_value(), "2 FRAMES");
    for (int i = 0; i < 6; ++i) m.on_key(SDLK_RIGHT);
    CHECK_EQ(cfg.netplay_delay, 4);
    for (int i = 0; i < 3; ++i) m.on_key(SDLK_LEFT);
    CHECK_STR(m.selected_value(), "1 FRAME");

    CHECK(select_row(m, "JOIN A GAME"));
    m.on_key(SDLK_RETURN);
    CHECK_EQ(joined, 1);
    // BACK returns to the front end it came from.
    CHECK(select_row(m, "BACK"));
    m.on_key(SDLK_RETURN);
    CHECK_STR(m.page_name(), "front");
}

#ifdef CHAOTIX_TEST_ROM
TEST(menu, zone_pictures_are_made_only_while_a_menu_is_open) {
    // The worker must not compete with the game (issue #31's slow machines):
    // it starts only with a menu open, holds still between its frames while
    // the game is played, and goes on when a menu opens again.
    Drawn d;
    CHECK(d.ok);
    if (!d.ok) return;
    Rom rom;
    std::string err;
    CHECK(rom.load(CHAOTIX_TEST_ROM, &err));
    const std::string dir = "backdrop_cache_test/";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    auto wait_ms = [](int ms) { SDL_Delay(Uint32(ms)); };
    {
        MenuBackdrops b;
        b.configure(rom, dir);
        b.update(d.renderer, true, false);  // playing: nothing starts
        CHECK(!b.busy());
        CHECK(b.paused());
        b.update(d.renderer, true, true);   // a menu opened
        CHECK(b.busy());
        CHECK(!b.paused());
        for (int i = 0; i < 400 && b.frames_emulated() < 30; ++i) wait_ms(5);
        CHECK(b.frames_emulated() >= 30);
        b.update(d.renderer, true, false);  // closed again: held, not cancelled
        CHECK(b.paused());
        CHECK(b.busy());
        wait_ms(50);
        const uint64_t held = b.frames_emulated();
        wait_ms(300);
        CHECK_EQ(b.frames_emulated(), held);
        b.update(d.renderer, true, true);
        for (int i = 0; i < 400 && b.frames_emulated() < held + 30; ++i) wait_ms(5);
        CHECK(b.frames_emulated() >= held + 30);
        // MENU BACKGROUND on PLAIN holds it too, even with a menu open.
        b.update(d.renderer, false, true);
        CHECK(b.paused());
        b.shutdown();                         // a held worker still stops
        CHECK(!b.busy());
    }
    std::filesystem::remove_all(dir, ec);
}

TEST(menu, a_chosen_zone_is_made_first_and_alone) {
    // Issue #37: with one zone chosen, its picture is made ahead of the
    // others (a picture already being made for CYCLE is given up for it),
    // shown alone, and nothing else is made.
    Drawn d;
    CHECK(d.ok);
    if (!d.ok) return;
    Rom rom;
    std::string err;
    CHECK(rom.load(CHAOTIX_TEST_ROM, &err));
    const std::string dir = "backdrop_choice_test/";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    {
        MenuBackdrops b;
        b.configure(rom, dir);
        b.update(d.renderer, true, true);  // CYCLE: the first zone is started
        CHECK(b.busy());
        for (int i = 0; i < 400 && b.frames_emulated() < 30; ++i) SDL_Delay(5);
        const int lobby = backdrop_find_zone("WorldEntrance");
        b.choose(lobby);
        MenuBackdrops::Shown shown;
        CHECK(!b.current(shown));          // plain until it is made
        for (int i = 0; i < 6000 && !b.current(shown); ++i) {
            b.update(d.renderer, true, true);
            SDL_Delay(5);
        }
        CHECK(b.current(shown));
        CHECK_EQ(int(shown.width), 320);   // the lobby, 4:3
        CHECK_EQ(b.ready_count(), 1);      // the first zone was given up
        b.advance();                       // a chosen zone stays
        CHECK(b.current(shown));
        CHECK_EQ(int(shown.width), 320);
        for (int i = 0; i < 20; ++i) b.update(d.renderer, true, true);
        CHECK(!b.busy());                  // and nothing else is made
        // Back to CYCLE: the others are made, the lobby still shown meanwhile.
        b.choose(-1);
        b.update(d.renderer, true, true);
        CHECK(b.busy());
        CHECK(b.current(shown));
        b.shutdown();
    }
    std::filesystem::remove_all(dir, ec);
}
#endif

// ---------------------------------------------------------------------------
// MENU SIZE (issue #49)
// ---------------------------------------------------------------------------

TEST(menu_size, every_size_is_larger_than_the_one_before) {
    // The pixel faces come in steps of 8 and the game's font in whole scales,
    // so a factor alone could round two sizes to the same letters. From a
    // phone-sized window to 4K, each size is a whole game-font scale and a
    // grid step past the one before, and NORMAL is what it always was.
    for (float scale : {0.62f, 0.75f, 1.0f, 1.19f, 1.5f, 2.0f, 3.0f}) {
        for (float design : {16.0f, 32.0f}) {
            const ui::PixelSize n = ui::pixel_steps(design, scale, 0);
            CHECK_EQ(int(n.face), int(std::max(8.0f, std::round(design * scale / 8.0f) * 8.0f)));
            CHECK_EQ(n.game, ui::game_font_scale(n.face));
            ui::PixelSize before = n;
            for (int z = 1; z < ui::kMenuSizes; ++z) {
                const ui::PixelSize at = ui::pixel_steps(design, scale, z);
                CHECK(at.game > before.game);
                CHECK(at.face > before.face);
                CHECK_EQ(int(at.face) % 8, 0);  // still on the face's grid
                before = at;
            }
        }
    }
    // A 1080-line phone: the rows' letters 32, 48 and 64 pixels tall.
    CHECK_EQ(ui::pixel_steps(16, 1.5f, 0).game, 2);
    CHECK_EQ(ui::pixel_steps(16, 1.5f, 1).game, 3);
    CHECK_EQ(ui::pixel_steps(16, 1.5f, 2).game, 4);
}

TEST(menu_size, is_a_video_setting_and_is_kept) {
    Config cfg;
    cfg.set_defaults();
    CHECK_EQ(cfg.menu_size, -1);  // AUTO
    CHECK_EQ(cfg.menu_zoom(), menu_size_default());
    Menu m;
    m.bind(cfg);
    open_category(m, "VIDEO");
    CHECK(select_row(m, "MENU SIZE"));
    CHECK_STR(m.selected_value(), menu_size_default() ? "AUTO (LARGE)" : "AUTO (NORMAL)");
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK_STR(m.selected_value(), "NORMAL");
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK_STR(m.selected_value(), "LARGE");
    CHECK_EQ(m.zoom(), 1);  // the drawing layer is told at once
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK_STR(m.selected_value(), "EXTRA LARGE");
    CHECK_EQ(cfg.menu_size, 2);
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK_EQ(cfg.menu_size, -1);  // round to AUTO again
    CHECK(m.on_key(SDLK_LEFT));
    CHECK_EQ(cfg.menu_size, 2);
    m.close();
    CHECK_EQ(m.zoom(), 0);  // nothing but the menu is drawn larger

    const std::string path = (std::filesystem::temp_directory_path() / "chaotix_menu_size_test.ini").string();
    for (int size : {-1, 0, 1, 2}) {
        cfg.menu_size = size;
        CHECK(cfg.save(path));
        Config back;
        back.set_defaults();
        back.menu_size = 5;
        CHECK(back.load(path));
        CHECK_EQ(back.menu_size, size);
    }
    std::filesystem::remove(path);
    CHECK_EQ(parse_menu_size("Extra Large"), 2);
    CHECK_EQ(parse_menu_size("large"), 1);
    CHECK_EQ(parse_menu_size("huge"), -1);
}

TEST(show_fps, is_a_video_setting_off_by_default_and_kept) {
    // Issue #54: OPTIONS > VIDEO > SHOW FPS, OFF or ON, in [Video] ShowFps.
    Config cfg;
    cfg.set_defaults();
    CHECK(!cfg.show_fps);
    Menu m;
    m.bind(cfg);
    open_category(m, "VIDEO");
    CHECK(select_row(m, "SHOW FPS"));
    CHECK_STR(m.selected_value(), "OFF");
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK(cfg.show_fps);
    CHECK_STR(m.selected_value(), "ON");
    CHECK(m.on_key(SDLK_LEFT));
    CHECK(!cfg.show_fps);
    CHECK(m.on_key(SDLK_RETURN));
    CHECK(cfg.show_fps);

    const std::string path = (std::filesystem::temp_directory_path() / "chaotix_show_fps_test.ini").string();
    for (bool on : {true, false}) {
        cfg.show_fps = on;
        CHECK(cfg.save(path));
        Config back;
        back.set_defaults();
        back.show_fps = !on;
        CHECK(back.load(path));
        CHECK_EQ(back.show_fps, on);
    }
    // Written as a [Video] key, and read from one.
    std::FILE* f = std::fopen(path.c_str(), "w");
    CHECK(f != nullptr);
    if (!f) return;
    std::fprintf(f, "[Video]\nShowFps = true\n");
    std::fclose(f);
    Config old;
    CHECK(old.load(path));
    CHECK(old.show_fps);
    std::filesystem::remove(path);
}

namespace {

// Draws `page` at each MENU SIZE in a window of w x h, stepping through every
// entry so a list that scrolls is seen all the way down, and checks each touch
// target is on the screen and the selected entry is among them. Returns the
// targets' mean height at each size.
std::vector<float> row_heights(int w, int h, const std::string& page, bool game_font) {
    std::vector<float> out;
    Drawn d(w, h);
    CHECK(d.ok);
    if (!d.ok) return out;
    if (game_font) CHECK(d.g.set_game_font(block_font()));
    for (int z = 0; z < ui::kMenuSizes; ++z) {
        Config cfg;
        cfg.set_defaults();
        cfg.menu_size = z;
        cfg.menu_game_font = game_font;
        Menu m;
        m.bind(cfg);
        CHECK(m.show_page(page));
        const SDL_Keycode step = page == "main" ? SDLK_RIGHT : SDLK_DOWN;
        float sum = 0;
        int count = 0;
        for (int i = 0; i < 16; ++i) {
            d.draw(m);
            const auto rows = m.drawn_rows();
            CHECK(!rows.empty());
            bool selected_shown = false;
            for (const auto& [index, r] : rows) {
                if (r.x < -0.5f || r.y < -0.5f || r.x + r.w > float(w) + 0.5f || r.y + r.h > float(h) + 0.5f) {
                    std::printf("    %s %dx%d size %d: row %d at %.1f,%.1f %.1fx%.1f is off the screen\n",
                                page.c_str(), w, h, z, index, double(r.x), double(r.y), double(r.w), double(r.h));
                    CHECK(false);
                }
                selected_shown = selected_shown || index == m.selected_index();
                sum += r.h;
                ++count;
            }
            CHECK(selected_shown);
            m.on_key(step);
        }
        out.push_back(count ? sum / float(count) : 0.0f);
    }
    return out;
}

} // namespace

TEST(menu_size, rows_grow_and_stay_on_the_screen) {
    // A desktop window, a phone in landscape (in its own pixels, and as an
    // 800x360 window) and a portrait window; the front end, the pause menu's
    // row, two lists of settings and the zone showcase (LEVEL SELECT and TIME
    // ATTACK, issue #53); the game's font and the
    // pixel face.
    const std::pair<int, int> windows[] = {{1280, 720}, {800, 360}, {2400, 1080}, {412, 860}};
    for (const auto& [w, h] : windows) {
        for (const char* page : {"front", "main", "video", "extras", "levelselect", "timeattack"}) {
            for (bool game_font : {true, false}) {
                const std::vector<float> hs = row_heights(w, h, page, game_font);
                CHECK_EQ(int(hs.size()), ui::kMenuSizes);
                if (hs.size() != size_t(ui::kMenuSizes)) continue;
                // The touch targets grow with the rows.
                for (int z = 1; z < ui::kMenuSizes; ++z) {
                    if (hs[size_t(z)] <= hs[size_t(z - 1)] + 1.0f)
                        std::printf("    %s %dx%d%s: size %d rows %.1f, size %d rows %.1f\n", page, w, h,
                                    game_font ? " (game font)" : "", z - 1, double(hs[size_t(z - 1)]), z,
                                    double(hs[size_t(z)]));
                    CHECK(hs[size_t(z)] > hs[size_t(z - 1)] + 1.0f);
                }
            }
        }
    }
}

TEST(menu_size, the_front_end_picture_gives_way_on_a_portrait_window) {
    // On a portrait window the list used to run off the right-hand side
    // beside a postage stamp of a picture: there the picture goes across the
    // top and the list below it.
    Drawn d(412, 860);
    CHECK(d.ok);
    if (!d.ok) return;
    Config cfg;
    cfg.set_defaults();
    Menu m;
    m.bind(cfg);
    m.open_front();
    d.draw(m);
    SDL_FRect art{};
    CHECK(m.art_panel(d.g, &art));
    CHECK(art.w > 412 * 0.6f);
    for (const auto& [index, r] : m.drawn_rows()) CHECK(r.y >= art.y + art.h);
}

// ---------------------------------------------------------------------------
// LEVEL SELECT (issue #51)
// ---------------------------------------------------------------------------

TEST(level_select, the_page_starts_the_level_chosen) {
    Config cfg;
    cfg.set_defaults();
    Menu m;
    m.bind(cfg);
    int started = 0, timed = 0;
    stage_select::Request got;
    Menu::Hooks h;
    h.start_level = [&](const stage_select::Request& r, bool) { ++started; got = r; };
    h.start_stage = [&](const stage_select::Request&) { ++timed; };
    m.set_hooks(h);

    m.open_front();
    CHECK(select_row(m, "LEVEL SELECT"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK_STR(m.page_name(), "levelselect");
    CHECK(select_row(m, "PLACE"));
    CHECK_STR(m.selected_value(), "BOTANIC BASE");
    // Back from BOTANIC BASE: the last place, a special stage, then the bonus
    // stage and the hub; NOT USED is never offered.
    CHECK(m.on_key(SDLK_LEFT));
    CHECK_STR(m.selected_value(), "SPECIAL STAGE");
    CHECK(select_row(m, "LEVEL"));
    CHECK_STR(m.selected_value(), "2");  // level 1 kept: the second stage
    for (int i = 0; i < 4; ++i) m.on_key(SDLK_RIGHT);
    CHECK_STR(m.selected_value(), "6");
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK_STR(m.selected_value(), "1");  // six special stages, no more
    CHECK(m.on_key(SDLK_LEFT));
    CHECK(select_row(m, "PLACE"));
    CHECK(m.on_key(SDLK_LEFT));
    CHECK_STR(m.selected_value(), "BONUS STAGE");
    CHECK(m.on_key(SDLK_LEFT));
    CHECK_STR(m.selected_value(), "WORLD ENTRANCE");
    CHECK_EQ(cfg.ls_level, 1);  // the hub
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK_STR(m.selected_value(), "BOTANIC BASE");
    CHECK_EQ(cfg.ls_level, 1);
    for (int i = 0; i < 4; ++i) m.on_key(SDLK_RIGHT);
    CHECK_STR(m.selected_value(), "MARINA MADNESS");
    CHECK(select_row(m, "LEVEL"));
    m.on_key(SDLK_LEFT);  // 1 -> 5
    CHECK_STR(m.selected_value(), "5");
    CHECK(select_row(m, "AT-TIME"));
    m.on_key(SDLK_LEFT);
    CHECK_STR(m.selected_value(), "NIGHT");
    CHECK(select_row(m, "COMBI"));
    m.on_key(SDLK_LEFT);
    CHECK_STR(m.selected_value(), "HEAVY");
    // TIME ATTACK's own choice is left alone.
    CHECK_EQ(cfg.ta_place, 0);
    CHECK_EQ(cfg.ta_combi, 7);

    CHECK(select_row(m, "START"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK_EQ(started, 1);
    CHECK_EQ(timed, 0);  // not a TIME ATTACK run
    CHECK_EQ(got.place, 4);
    CHECK_EQ(got.level, 5);
    CHECK_EQ(got.attime, 6);
    CHECK_EQ(got.player, 2);
    CHECK_EQ(got.combi, 6);
    CHECK(level_select::valid(got));
    CHECK(!m.open());

    // Kept, apart from TIME ATTACK's.
    const std::string path = (std::filesystem::temp_directory_path() / "chaotix_level_select_test.ini").string();
    CHECK(cfg.save(path));
    Config back;
    back.set_defaults();
    CHECK(back.load(path));
    CHECK_EQ(back.ls_place, 4);
    CHECK_EQ(back.ls_level, 5);
    CHECK_EQ(back.ls_time, 6);
    CHECK_EQ(back.ls_combi, 6);
    CHECK_EQ(back.ta_place, 0);
    std::filesystem::remove(path);
}

TEST(level_select, a_kept_choice_it_does_not_offer_is_put_right) {
    // A hand-edited settings file: NOT USED, or a level the place lacks.
    Config cfg;
    cfg.set_defaults();
    cfg.ls_place = 8;
    cfg.ls_level = 0;
    Menu m;
    m.bind(cfg);
    CHECK(m.show_page("levelselect"));
    CHECK_EQ(cfg.ls_place, 0);
    CHECK_EQ(cfg.ls_level, 1);  // BOTANIC BASE has no level 0
    cfg.ls_place = 10;
    cfg.ls_level = 7;  // the wireframe copy of the second stage
    CHECK(m.show_page("levelselect"));
    CHECK_EQ(cfg.ls_place, 10);
    CHECK_EQ(cfg.ls_level, 0);
}

// Issue #55: PARTNER NONE on LEVEL SELECT.
TEST(level_select, partner_none_is_offered_after_the_characters_for_one_player) {
    Config cfg;
    cfg.set_defaults();
    Menu m;
    m.bind(cfg);
    int started = 0;
    bool solo = false;
    stage_select::Request got;
    Menu::Hooks h;
    h.start_level = [&](const stage_select::Request& r, bool s) { ++started; got = r; solo = s; };
    m.set_hooks(h);
    CHECK(m.show_page("levelselect"));
    CHECK(select_row(m, "COMBI"));
    CHECK_STR(m.selected_value(), "ESPIO");
    // Past the last character: NONE, then round to the first.
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK_STR(m.selected_value(), "NONE");
    CHECK(cfg.ls_no_partner);
    CHECK_EQ(cfg.ls_combi, stage_select::kEspio);  // still the partner the request names
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK_STR(m.selected_value(), "MIGHTY");
    CHECK(!cfg.ls_no_partner);
    // And back the other way: before the first character, NONE, then the last.
    CHECK(m.on_key(SDLK_LEFT));
    CHECK_STR(m.selected_value(), "NONE");
    CHECK_EQ(cfg.ls_combi, 0);
    CHECK(m.on_key(SDLK_LEFT));
    CHECK_STR(m.selected_value(), "ESPIO");
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK_STR(m.selected_value(), "NONE");
    // START: a request with a partner the game accepts, and solo.
    CHECK(select_row(m, "START"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK_EQ(started, 1);
    CHECK(solo);
    CHECK_EQ(got.place, 0);
    CHECK_EQ(got.combi, stage_select::kEspio);
    CHECK(!got.two_players);
    CHECK(level_select::valid(got));
    // Kept.
    const std::string path = (std::filesystem::temp_directory_path() / "chaotix_level_select_none.ini").string();
    CHECK(cfg.save(path));
    Config back;
    back.set_defaults();
    CHECK(!back.ls_no_partner);
    CHECK(back.load(path));
    CHECK(back.ls_no_partner);
    CHECK_EQ(back.ls_combi, stage_select::kEspio);
    std::filesystem::remove(path);
}

TEST(level_select, partner_none_is_not_offered_with_two_players_or_outside_the_attractions) {
    Config cfg;
    cfg.set_defaults();
    Menu m;
    m.bind(cfg);
    int started = 0;
    bool solo = true;
    stage_select::Request got;
    Menu::Hooks h;
    h.start_level = [&](const stage_select::Request& r, bool s) { ++started; got = r; solo = s; };
    m.set_hooks(h);
    // 2 PLAYERS: the characters only.
    cfg.ls_two_players = true;
    CHECK(m.show_page("levelselect"));
    CHECK(select_row(m, "COMBI"));
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK_STR(m.selected_value(), "MIGHTY");
    CHECK(m.on_key(SDLK_LEFT));
    CHECK_STR(m.selected_value(), "ESPIO");
    CHECK(!cfg.ls_no_partner);
    // NONE kept from 1 PLAYER shows the partner the game gets, and START
    // plays with it.
    cfg.ls_no_partner = true;
    CHECK_STR(m.selected_value(), "ESPIO");
    CHECK(select_row(m, "START"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK_EQ(started, 1);
    CHECK(!solo);
    CHECK(got.two_players);
    // Back to 1 PLAYER, it is NONE again; in TRAINING, INTRODUCTION, the hub
    // and the special and bonus stages it is not.
    cfg.ls_two_players = false;
    CHECK(m.show_page("levelselect"));
    CHECK(select_row(m, "COMBI"));
    CHECK_STR(m.selected_value(), "NONE");
    for (int place : {5, 6, 7, 9, 10}) {
        cfg.ls_place = place;
        cfg.ls_level = level_select::first_level(place);
        CHECK(m.show_page("levelselect"));
        CHECK(select_row(m, "COMBI"));
        CHECK_STR(m.selected_value(), "ESPIO");
        CHECK(!level_select::partner_none_offered(place, false));
    }
    for (int place = 0; place < 5; ++place) {
        CHECK(level_select::partner_none_offered(place, false));
        CHECK(!level_select::partner_none_offered(place, true));
    }
    // Stepping there picks a character, and NONE is not among them.
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK_STR(m.selected_value(), "MIGHTY");
    CHECK(!cfg.ls_no_partner);
    CHECK(m.on_key(SDLK_LEFT));
    CHECK_STR(m.selected_value(), "ESPIO");
}

TEST(level_select, a_session_with_no_partner_plays_solo_only_while_it_runs) {
    stage_select::Request r;  // BOTANIC BASE 1, one player
    level_select::Session ls;
    CHECK(!ls.solo_now());
    ls.begin(r, true);
    CHECK(ls.solo_now());
    // GAMEPLAY's setting gives way to NONE, and is back once it is over.
    CHECK(tether::partner_in_force(1, false, ls.solo_now()) == tether::Partner::Solo);
    ls.cancel();
    CHECK(!ls.solo_now());
    CHECK(tether::partner_in_force(1, false, ls.solo_now()) == tether::Partner::Follow);
    // Where NONE does not apply, the session keeps its partner.
    r.place = 5;
    r.level = 0;
    ls.begin(r, true);
    CHECK(!ls.solo_now());
    r.place = 0;
    r.level = 1;
    r.two_players = true;
    ls.begin(r, true);
    CHECK(!ls.solo_now());
    r.two_players = false;
    ls.begin(r, false);
    CHECK(!ls.solo_now());
    // And TIME ATTACK never changes anything.
    CHECK(tether::partner_in_force(2, true, true) == tether::Partner::Normal);
    CHECK(tether::partner_in_force(2, false, false) == tether::Partner::Solo);
    CHECK(tether::partner_in_force(0, false, false) == tether::Partner::Normal);
    CHECK(tether::partner_in_force(9, false, false) == tether::Partner::Normal);
}


// ---------------------------------------------------------------------------
// The zone showcase: TIME ATTACK and LEVEL SELECT's layout (issue #53)
// ---------------------------------------------------------------------------

namespace {

// The touch targets of one entry, in the order they were drawn (a row's
// chips, then the row; an arrow, then the card or band it is on).
std::vector<SDL_FRect> targets_of(const Menu& m, int index) {
    std::vector<SDL_FRect> out;
    for (const auto& [i, r] : m.drawn_rows())
        if (i == index) out.push_back(r);
    return out;
}

// The index of the entry with this label (walking to it).
int index_of(Menu& m, const std::string& label) {
    CHECK(select_row(m, label));
    return m.selected_index();
}

void tap_centre(Drawn& d, Menu& m, const SDL_FRect& r) { d.tap(m, r.x + r.w * 0.5f, r.y + r.h * 0.5f); }

} // namespace

TEST(showcase, the_cards_go_beside_the_picture_or_under_it) {
    // Landscape: the picture's column on the left, the cards on the right.
    // Portrait: one column, the cards under the picture and its rows. At
    // every MENU SIZE, for both pages.
    struct Window { int w, h; bool portrait; };
    for (const Window win : {Window{1280, 720, false}, Window{2400, 1080, false}, Window{412, 860, true}}) {
        for (const char* page : {"timeattack", "levelselect"}) {
            for (int z = 0; z < ui::kMenuSizes; ++z) {
                Drawn d(win.w, win.h);
                CHECK(d.ok);
                if (!d.ok) return;
                Config cfg;
                cfg.set_defaults();
                cfg.menu_size = z;
                Menu m;
                m.bind(cfg);
                CHECK(m.show_page(page));
                const int place = index_of(m, "PLACE");
                const int player = index_of(m, "PLAYER");
                const int time = index_of(m, "AT-TIME");
                CHECK(select_row(m, "PLACE"));
                d.draw(m);
                const auto picture = targets_of(m, place);
                CHECK(!picture.empty());
                if (picture.empty()) continue;
                const SDL_FRect band = picture.back();
                CHECK(select_row(m, "PLAYER"));
                d.draw(m);
                const auto card = targets_of(m, player);
                CHECK(!card.empty());
                if (card.empty()) continue;
                const SDL_FRect c = card.back();
                if (win.portrait) {
                    // Under it (scrolled to, at a larger size).
                    CHECK(c.y >= band.y + band.h || z > 0);
                    CHECK(c.x < band.x + band.w);
                } else if (z == 0) {
                    CHECK(c.x >= band.x + band.w);  // beside it
                    CHECK(c.y < band.y + band.h);
                }
                CHECK(select_row(m, "AT-TIME"));
                d.draw(m);
                // Four chips and the row itself.
                CHECK_EQ(int(targets_of(m, time).size()), stage_select::kTimeCount + 1);
            }
        }
    }
}

TEST(showcase, chips_arrows_and_start_act_on_one_tap) {
    // Touch drives the page directly: the place's arrows step it, a chip
    // chooses its level, time of day or player count, and START starts --
    // with the same request the keys make.
    for (const auto& [w, h] : {std::pair<int, int>{1280, 720}, {412, 860}, {800, 360}}) {
        Drawn d(w, h);
        CHECK(d.ok);
        if (!d.ok) return;
        Config cfg;
        cfg.set_defaults();
        Menu m;
        m.bind(cfg);
        int started = 0;
        stage_select::Request got;
        Menu::Hooks hooks;
        hooks.start_stage = [&](const stage_select::Request& r) { ++started; got = r; };
        m.set_hooks(hooks);
        m.open_front();
        CHECK(select_row(m, "TIME ATTACK"));
        CHECK(m.on_key(SDLK_RETURN));
        const int place = index_of(m, "PLACE");
        const int level = index_of(m, "LEVEL");
        const int time = index_of(m, "AT-TIME");
        const int players = index_of(m, "PLAYERS");
        const int start = index_of(m, "START");

        CHECK(select_row(m, "PLACE"));
        d.draw(m);
        auto t = targets_of(m, place);  // "<", ">", the band
        CHECK_EQ(int(t.size()), 3);
        if (t.size() != 3) continue;
        tap_centre(d, m, t[1]);
        CHECK_EQ(cfg.ta_place, 1);  // BOTANIC BASE -> SPEED SLIDER
        tap_centre(d, m, t[0]);
        CHECK_EQ(cfg.ta_place, 0);
        tap_centre(d, m, t[1]);
        CHECK_EQ(cfg.ta_place, 1);

        CHECK(select_row(m, "LEVEL"));
        d.draw(m);
        t = targets_of(m, level);  // 1..5, the row
        CHECK_EQ(int(t.size()), 6);
        if (t.size() == 6) tap_centre(d, m, t[2]);
        CHECK_EQ(cfg.ta_level, 3);
        CHECK_EQ(m.selected_index(), level);

        CHECK(select_row(m, "AT-TIME"));
        d.draw(m);
        t = targets_of(m, time);
        if (t.size() == 5) tap_centre(d, m, t[1]);
        CHECK_EQ(cfg.ta_time, 2);  // DAY

        CHECK(select_row(m, "PLAYERS"));
        d.draw(m);
        t = targets_of(m, players);
        if (t.size() == 3) tap_centre(d, m, t[1]);
        CHECK(cfg.ta_two_players);

        CHECK(select_row(m, "START"));
        d.draw(m);
        t = targets_of(m, start);
        CHECK_EQ(int(t.size()), 1);
        if (!t.empty()) tap_centre(d, m, t[0]);
        CHECK_EQ(started, 1);
        CHECK_EQ(got.place, 1);
        CHECK_EQ(got.level, 3);
        CHECK_EQ(got.attime, 2);
        CHECK(got.two_players);
        CHECK_EQ(got.player, stage_select::kKnuckles);
        CHECK_EQ(got.combi, stage_select::kEspio);
        CHECK(!m.open());
    }
}

TEST(showcase, the_keys_and_the_taps_make_the_same_request) {
    // LEVEL SELECT, chosen once with the keys and once with taps, asks for
    // the same stage (and still through start_level, NO SAVE).
    auto run = [](bool touch) {
        Drawn d(1280, 720);
        Config cfg;
        cfg.set_defaults();
        Menu m;
        m.bind(cfg);
        stage_select::Request got;
        int started = 0, timed = 0;
        Menu::Hooks hooks;
        hooks.start_level = [&](const stage_select::Request& r, bool) { ++started; got = r; };
        hooks.start_stage = [&](const stage_select::Request&) { ++timed; };
        m.set_hooks(hooks);
        CHECK(m.show_page("levelselect"));
        const int place = index_of(m, "PLACE"), level = index_of(m, "LEVEL"), time = index_of(m, "AT-TIME");
        const int combi = index_of(m, "COMBI"), start = index_of(m, "START");
        if (touch) {
            CHECK(select_row(m, "PLACE"));
            d.draw(m);
            for (int i = 0; i < 4; ++i) {  // -> MARINA MADNESS
                const auto t = targets_of(m, place);
                if (t.size() == 3) tap_centre(d, m, t[1]);
            }
            CHECK(select_row(m, "LEVEL"));
            d.draw(m);
            auto t = targets_of(m, level);
            if (t.size() == 6) tap_centre(d, m, t[4]);  // 5
            CHECK(select_row(m, "AT-TIME"));
            d.draw(m);
            t = targets_of(m, time);
            if (t.size() == 5) tap_centre(d, m, t[3]);  // NIGHT
            CHECK(select_row(m, "COMBI"));
            d.draw(m);
            t = targets_of(m, combi);  // "<", ">", the card
            if (t.size() == 3) tap_centre(d, m, t[0]);
            CHECK(select_row(m, "START"));
            d.draw(m);
            t = targets_of(m, start);
            if (!t.empty()) tap_centre(d, m, t[0]);
        } else {
            CHECK(select_row(m, "PLACE"));
            for (int i = 0; i < 4; ++i) m.on_key(SDLK_RIGHT);
            CHECK(select_row(m, "LEVEL"));
            m.on_key(SDLK_LEFT);
            CHECK(select_row(m, "AT-TIME"));
            m.on_key(SDLK_LEFT);
            CHECK(select_row(m, "COMBI"));
            m.on_key(SDLK_LEFT);
            CHECK(select_row(m, "START"));
            m.on_key(SDLK_RETURN);
        }
        CHECK_EQ(started, 1);
        CHECK_EQ(timed, 0);
        return got;
    };
    const stage_select::Request keys = run(false), taps = run(true);
    CHECK_EQ(keys.place, 4);
    CHECK_EQ(keys.level, 5);
    CHECK_EQ(keys.attime, 6);
    CHECK_EQ(keys.combi, 6);  // ESPIO -> HEAVY
    CHECK_EQ(taps.place, keys.place);
    CHECK_EQ(taps.level, keys.level);
    CHECK_EQ(taps.attime, keys.attime);
    CHECK_EQ(taps.player, keys.player);
    CHECK_EQ(taps.combi, keys.combi);
    CHECK(taps.two_players == keys.two_players);
    CHECK(level_select::valid(taps));
}

TEST(showcase, it_asks_for_the_picture_and_sprites_of_what_is_chosen) {
    // The picture follows the place, level and time of day; the cards ask
    // for the two characters chosen.
    Drawn d(1280, 720);
    CHECK(d.ok);
    if (!d.ok) return;
    Config cfg;
    cfg.set_defaults();
    Menu m;
    m.bind(cfg);
    stage_select::Request asked;
    int pictures = 0;
    std::vector<int> sprites;
    Menu::Hooks hooks;
    hooks.stage_picture = [&](const stage_select::Request& r, float*, float*, int* state) -> SDL_Texture* {
        ++pictures;
        asked = r;
        *state = Menu::kPictureMaking;
        return nullptr;
    };
    hooks.character_sprite = [&](int who, float*, float*) -> SDL_Texture* {
        sprites.push_back(who);
        return nullptr;
    };
    m.set_hooks(hooks);
    CHECK(m.show_page("timeattack"));
    cfg.ta_place = 3;
    cfg.ta_level = 4;
    cfg.ta_time = 6;
    cfg.ta_player = 4;
    cfg.ta_combi = 5;
    d.draw(m);
    CHECK(pictures >= 1);
    CHECK_EQ(asked.place, 3);
    CHECK_EQ(asked.level, 4);
    CHECK_EQ(asked.attime, 6);
    CHECK(std::find(sprites.begin(), sprites.end(), 4) != sprites.end());
    CHECK(std::find(sprites.begin(), sprites.end(), 5) != sprites.end());
}

#ifdef CHAOTIX_TEST_ROM
TEST(showcase, character_portraits_come_from_the_rom_with_the_mods) {
    Rom rom;
    std::string err;
    CHECK(rom.load(CHAOTIX_TEST_ROM, &err));
    for (int id = 0; id < 8; ++id) {
        Portrait p;
        std::string why;
        const bool ok = character_portrait(rom, id, nullptr, nullptr, p, &why);
        // Slot 1 has no graphics; everyone else stands.
        CHECK(ok == (id != stage_select::kBrokenCharacter));
        if (!ok) continue;
        CHECK_EQ(p.frame, kPortraitFrame);
        CHECK(p.width > 10 && p.width < 64 && p.height > 10 && p.height < 64);
        CHECK_EQ(int(p.argb.size()), p.width * p.height);
        int opaque = 0;
        for (uint32_t px : p.argb) opaque += px != 0;
        CHECK(opaque > p.width * p.height / 5);
        CHECK(!p.replaced);
    }
    // A character mod's colours, and a sprite mod's image.
    Portrait plain;
    CHECK(character_portrait(rom, stage_select::kKnuckles, nullptr, nullptr, plain, nullptr));
    CharacterMod mod;
    mod.id = stage_select::kKnuckles;
    for (int i = 0; i < characters::kColours; ++i) {
        mod.palette[size_t(i)] = 0x001F;  // pure red
        mod.palette_set[size_t(i)] = true;
    }
    Portrait red;
    CHECK(character_portrait(rom, stage_select::kKnuckles, nullptr, &mod, red, nullptr));
    CHECK_EQ(red.width, plain.width);
    bool all_red = true, changed = false;
    for (size_t i = 0; i < red.argb.size(); ++i) {
        if (!red.argb[i]) continue;
        all_red = all_red && red.argb[i] == 0xFFFF0000u;
        changed = changed || red.argb[i] != plain.argb[i];
    }
    CHECK(all_red);
    CHECK(changed);
    SpritePack pack;
    SpriteImage box;
    box.width = 8;
    box.height = 4;
    box.px.assign(32, 1);
    pack.add(stage_select::kKnuckles, kPortraitFrame, box);
    Portrait modded;
    CHECK(character_portrait(rom, stage_select::kKnuckles, &pack, nullptr, modded, nullptr));
    CHECK(modded.replaced);
    CHECK_EQ(modded.width, 8);
    CHECK_EQ(modded.height, 4);
    CHECK(!modded.argb.empty() && modded.argb[0] != 0);
}

TEST(showcase, stage_pictures_are_made_on_demand_and_cached) {
    // Any stage at any time of day: made when asked for -- even with MENU
    // BACKGROUND on PLAIN -- kept in the cache, and from there at once the
    // next time. A stage that cannot be pictured (the bonus stage, whose
    // picture is not in the frame buffer) says so rather than loading for
    // ever.
    Drawn d;
    CHECK(d.ok);
    if (!d.ok) return;
    Rom rom;
    std::string err;
    CHECK(rom.load(CHAOTIX_TEST_ROM, &err));
    const std::string dir = "showcase_cache_test/";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    {
        MenuBackdrops b;
        b.configure(rom, dir);
        b.update(d.renderer, false, true);
        MenuBackdrops::Shown shown;
        bool exact = true, none = true;
        CHECK(!b.stage(3, 2, 6, shown, &exact, &none));  // nothing of the place yet
        CHECK(!exact);
        CHECK(!none);
        for (int i = 0; i < 12000 && !(b.stage(3, 2, 6, shown, &exact, &none) && exact); ++i) {
            b.update(d.renderer, false, true);
            SDL_Delay(2);
        }
        CHECK(exact);
        CHECK_EQ(int(shown.width), 320);  // 4:3
        CHECK_EQ(int(shown.height), 224);
        CHECK_EQ(b.ready_count(), 0);     // not one of the menu's own
        // Another time of day of the place: the one made stands in meanwhile.
        CHECK(b.stage(3, 2, 0, shown, &exact, &none));
        CHECK(!exact);
        // (The worker that made the first may take a moment to be done.)
        for (int i = 0; i < 500 && b.making() != stage_backdrop_key(3, 2, 0); ++i) {
            b.stage(3, 2, 0, shown, &exact, &none);
            b.update(d.renderer, false, true);
            SDL_Delay(2);
        }
        CHECK_STR(b.making(), stage_backdrop_key(3, 2, 0));
        // The bonus stage.
        for (int i = 0; i < 20000 && !none; ++i) {
            b.stage(9, 0, 0, shown, &exact, &none);
            b.update(d.renderer, false, true);
            SDL_Delay(2);
        }
        CHECK(none);
        b.shutdown();
    }
    {
        MenuBackdrops b;
        b.configure(rom, dir);
        b.update(d.renderer, false, true);
        MenuBackdrops::Shown shown;
        bool exact = false;
        CHECK(b.stage(3, 2, 6, shown, &exact));
        CHECK(exact);  // from the cache, at once
        CHECK(!b.busy());
        b.shutdown();
    }
    std::filesystem::remove_all(dir, ec);
}
#endif

TEST(level_select, the_no_partner_card_is_drawn_without_a_character) {
    // The card asks for no sprite for NONE, at both window shapes, and the
    // START tapped plays solo.
    for (const auto& [w, h] : {std::pair{1280, 720}, std::pair{412, 860}}) {
        Drawn d(w, h);
        CHECK(d.ok);
        Config cfg;
        cfg.set_defaults();
        cfg.ls_no_partner = true;
        Menu m;
        m.bind(cfg);
        std::vector<int> asked;
        int started = 0;
        bool solo = false;
        Menu::Hooks hooks;
        hooks.character_sprite = [&](int who, float*, float*) -> SDL_Texture* { asked.push_back(who); return nullptr; };
        hooks.start_level = [&](const stage_select::Request&, bool s) { ++started; solo = s; };
        m.set_hooks(hooks);
        CHECK(m.show_page("levelselect"));
        CHECK(select_row(m, "COMBI"));
        CHECK_STR(m.selected_value(), "NONE");
        d.draw(m);
        CHECK(!asked.empty());
        for (int who : asked) CHECK(who >= 0);
        const int start = index_of(m, "START");
        d.draw(m);
        const auto t = targets_of(m, start);
        CHECK(!t.empty());
        if (!t.empty()) tap_centre(d, m, t[0]);
        CHECK_EQ(started, 1);
        CHECK(solo);
    }
}
