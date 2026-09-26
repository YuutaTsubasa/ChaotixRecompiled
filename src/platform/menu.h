#pragma once

// The in-game menu: a horizontal row of choices over the running game, in the
// style of the era, with an options page behind it. Everything here is drawn
// by the host over the emulated image; the game's own menus are untouched.

#include <SDL3/SDL.h>

#include <functional>
#include <string>
#include <vector>

#include "platform/ui.h"

namespace chaotix {

struct Config;
namespace achievements { class Tracker; }

class Menu {
public:
    // Things only the application can carry out; the menu just asks.
    struct Hooks {
        std::function<void()> apply_video;  // window mode / filter changed
        std::function<void()> quit;
    };

    void set_hooks(Hooks h) { hooks_ = std::move(h); }
    // The settings the options page edits. Must outlive the menu.
    void bind(Config& cfg) { cfg_ = &cfg; }

    bool open() const { return page_ != Page::Closed; }
    void toggle();
    void show_achievements();
    // For screenshots and tests: "main", "options" or "awards".
    bool show_page(const std::string& name);
    std::string page_name() const;
    // The highlighted entry's label, empty when the page has no entries.
    std::string selected_label() const;
    void close() { page_ = Page::Closed; }

    // Each returns true when the menu consumed the input.
    bool on_key(SDL_Keycode key);
    bool on_pad(Uint8 button);
    bool on_touch_down(float x, float y);
    bool on_touch_up();

    void draw(ui::Ui& g, achievements::Tracker& ach);

private:
    enum class Page { Closed, Main, Options, Achievements };

    struct Item {
        std::string label;
        // Empty for an action; otherwise the value shown on the right.
        std::function<std::string()> value;
        // step is -1 or +1 to change a value, 0 to activate an action.
        std::function<void(int)> act;
    };

    // Pages are built when they are opened, not when they are drawn, so the
    // first key press after opening has something to act on.
    void set_page(Page p);
    void build_main();
    void build_options();
    void move(int delta);
    void activate(int step);
    void draw_main(ui::Ui& g);
    void draw_options(ui::Ui& g);
    void draw_achievements(ui::Ui& g, achievements::Tracker& ach);

    Hooks hooks_;
    Config* cfg_ = nullptr;
    Page page_ = Page::Closed;
    std::vector<Item> items_;
    int selected_ = 0;
    int scroll_ = 0;          // achievements page
    int rows_ = 1;            // rows the achievements page had room for
    float row_h_ = 0;
    // Touch: where the drawn rows were, so a tap can hit them.
    struct Hit { SDL_FRect r; int index; };
    std::vector<Hit> hits_;
    float drag_y_ = 0, drag_total_ = 0;
    // Drives the little bounce on the selected entry.
    float anim_ = 0;
};

} // namespace chaotix
