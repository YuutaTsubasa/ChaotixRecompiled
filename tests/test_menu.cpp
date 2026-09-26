// The in-game menu's behaviour, without a window: pages are built when they
// are opened, so navigation and the settings it edits can be driven by feeding
// it key presses and inspecting the Config it was bound to.
#include "platform/menu.h"

#include "frontend/config.h"
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
    open_options(m);
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
    open_options(m);
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
    open_options(m);
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

TEST(menu, resetting_awards_asks_twice) {
    Config cfg;
    cfg.set_defaults();
    int resets = 0;
    Menu m;
    m.bind(cfg);
    Menu::Hooks h;
    h.reset_awards = [&resets] { ++resets; };
    m.set_hooks(h);
    open_options(m);
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
