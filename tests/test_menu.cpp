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
    const bool row = m.page_name() == "main" || m.page_name() == "front";
    const SDL_Keycode step = row ? SDLK_RIGHT : SDLK_DOWN;
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
