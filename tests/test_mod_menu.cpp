// Mod packs in the settings and on OPTIONS > EXTRAS > MODS (issue #42).
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "frontend/config.h"
#include "platform/menu.h"
#include "runtime/mod_packs.h"
#include "test_framework.h"

using namespace chaotix;
namespace fs = std::filesystem;

namespace {

bool select_label(Menu& m, const std::string& label) {
    const SDL_Keycode step = m.page_name() == "main" ? SDLK_RIGHT : SDLK_DOWN;
    for (int i = 0; i < 64 && m.selected_label() != label; ++i) m.on_key(step);
    return m.selected_label() == label;
}

struct Fixture {
    Config cfg;
    Menu menu;
    std::vector<mod_packs::Pack> packs;
    int reloads = 0, changes = 0;

    Fixture() {
        cfg.set_defaults();
        std::vector<mod_packs::Pack> found(3);
        found[0].loose = true;
        found[0].textures = true;
        found[1].id = "Alpha";
        found[1].info.name = "Alpha Pack";
        found[1].info.version = "1.0";
        found[1].info.description = "Does a thing.";
        found[1].textures = true;
        found[2].id = "Beta";
        found[2].info.name = "Beta";
        found[2].patches = true;
        packs = mod_packs::arrange(found, cfg.mod_order);
        menu.bind(cfg);
        Menu::Hooks h;
        h.mod_packs = [this] { return &packs; };
        h.mods_changed = [this] { ++changes; };
        h.reload_mods = [this] { ++reloads; };
        h.mods_need_restart = [] { return false; };
        menu.set_hooks(std::move(h));
    }
};

} // namespace

TEST(mod_menu, the_order_is_kept_in_the_settings) {
    const std::string path = (fs::temp_directory_path() / "chaotix_mod_menu_config.ini").u8string();
    Config a;
    a.set_defaults();
    a.mod_order.order = {"Zh TW", "Alpha"};
    a.mod_order.disabled = {"Beta"};
    a.mod_order.loose = false;
    CHECK(a.save(path));
    Config b;
    CHECK(b.load(path));
    CHECK_STR(mod_packs::join_list(b.mod_order.order), "Zh TW, Alpha");
    CHECK_EQ(b.mod_order.disabled.size(), size_t(1));
    CHECK(b.mod_order.disabled.count("Beta") == 1);
    CHECK(!b.mod_order.loose);

    // A config from before packs: every pack on, the loose files too.
    std::ofstream(path, std::ios::trunc) << "[Mods]\nDumpGraphics = true\n";
    Config c;
    CHECK(c.load(path));
    CHECK(c.dump_graphics);
    CHECK(c.mod_order.order.empty() && c.mod_order.disabled.empty() && c.mod_order.loose);
}

TEST(mod_menu, extras_leads_to_the_mods_page) {
    Fixture f;
    CHECK(f.menu.show_page("extras"));
    CHECK(select_label(f.menu, "MODS"));
    CHECK_STR(f.menu.selected_value(), "3 ON");
    f.menu.on_key(SDLK_RETURN);
    CHECK_STR(f.menu.page_name(), "mods");
    CHECK_STR(f.menu.selected_label(), "LOOSE FILES");
    f.menu.on_key(SDLK_DOWN);
    CHECK_STR(f.menu.selected_label(), "ALPHA PACK");
    CHECK_STR(f.menu.selected_note(), "1.0");
}

TEST(mod_menu, left_right_switches_a_pack_and_leaving_applies_it) {
    Fixture f;
    CHECK(f.menu.show_page("mods"));
    CHECK(select_label(f.menu, "BETA"));
    CHECK_STR(f.menu.selected_value(), "ON");
    f.menu.on_key(SDLK_LEFT);
    CHECK_STR(f.menu.selected_value(), "OFF");
    CHECK(!f.packs[2].enabled);
    CHECK(f.cfg.mod_order.disabled.count("Beta") == 1);
    CHECK_EQ(f.changes, 1);
    CHECK_EQ(f.reloads, 0);  // not until the page is left
    f.menu.on_key(SDLK_ESCAPE);
    CHECK_STR(f.menu.page_name(), "extras");
    CHECK_EQ(f.reloads, 1);
    // Nothing changed since: leaving again reloads nothing.
    f.menu.on_key(SDLK_ESCAPE);
    CHECK_EQ(f.reloads, 1);
}

TEST(mod_menu, a_pack_page_moves_it_and_switches_it) {
    Fixture f;
    CHECK(f.menu.show_page("mods"));
    CHECK(select_label(f.menu, "BETA"));
    f.menu.on_key(SDLK_RETURN);
    CHECK_STR(f.menu.page_name(), "modinfo");
    CHECK(select_label(f.menu, "MOVE UP"));
    f.menu.on_key(SDLK_RETURN);
    CHECK_STR(f.packs[1].id, "Beta");
    CHECK_STR(mod_packs::join_list(f.cfg.mod_order.order), "Beta, Alpha");
    f.menu.on_key(SDLK_RETURN);  // already just under the loose files
    CHECK_STR(f.packs[1].id, "Beta");
    CHECK(select_label(f.menu, "ENABLED"));
    f.menu.on_key(SDLK_RIGHT);
    CHECK(!f.packs[1].enabled);
    CHECK(select_label(f.menu, "BACK"));
    f.menu.on_key(SDLK_RETURN);
    CHECK_STR(f.menu.page_name(), "mods");
    CHECK_STR(f.menu.selected_label(), "BETA");  // back on the pack it showed
    CHECK_EQ(f.reloads, 0);
    f.menu.close();
    CHECK_EQ(f.reloads, 1);
}

TEST(mod_menu, the_loose_files_stay_on_top) {
    Fixture f;
    CHECK(f.menu.show_page("mods"));
    f.menu.on_key(SDLK_RETURN);
    CHECK_STR(f.menu.page_name(), "modinfo");
    CHECK(!select_label(f.menu, "MOVE UP"));
    CHECK(select_label(f.menu, "ENABLED"));
    f.menu.on_key(SDLK_LEFT);
    CHECK(!f.cfg.mod_order.loose);
}

TEST(mod_menu, reload_mods_reads_the_folder_at_once) {
    Fixture f;
    CHECK(f.menu.show_page("mods"));
    CHECK(select_label(f.menu, "RELOAD MODS"));
    f.menu.on_key(SDLK_RETURN);
    CHECK_EQ(f.reloads, 1);
    CHECK_STR(f.menu.selected_label(), "RELOAD MODS");
}

TEST(mod_menu, no_packs_says_so) {
    Config cfg;
    cfg.set_defaults();
    Menu m;
    m.bind(cfg);
    CHECK(m.show_page("mods"));
    CHECK_STR(m.selected_label(), "RELOAD MODS");
    m.on_key(SDLK_DOWN);
    CHECK_STR(m.selected_label(), "BACK");  // the note row is stepped over
}
