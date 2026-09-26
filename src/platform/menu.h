#pragma once

// The in-game menu: a horizontal row of choices over the running game, in the
// style of the era, with an options page behind it. Everything here is drawn
// by the host over the emulated image; the game's own menus are untouched.

#include <SDL3/SDL.h>

#include <functional>
#include <string>
#include <vector>

#include "platform/ui.h"
#include "runtime/stage_select.h"

namespace chaotix {

struct Config;
namespace achievements { class Tracker; }

class Menu {
public:
    // Things only the application can carry out; the menu just asks.
    struct Hooks {
        std::function<void()> apply_video;  // window mode / filter changed
        std::function<void()> quit;
        // Hands control to the game's own front end, as pressing Start on the
        // title screen would have done.
        std::function<void()> start_game;
        // Puts the title screen back and returns the front end to us.
        std::function<void()> back_to_title;
        // How many gamepads are plugged in, so the controls page can offer
        // them by number.
        std::function<int()> pad_count;
        // The controller's own name, so the page can say which one it means.
        std::function<std::string(int index)> pad_name;
        // Locks every achievement again and forgets the saved progress.
        std::function<void()> reset_awards;
        // Starts a level straight away, the way the game's own stage select
        // would: TIME ATTACK.
        std::function<void(const stage_select::Request&)> start_stage;
    };

    void set_hooks(Hooks h) { hooks_ = std::move(h); }
    // The settings the options page edits. Must outlive the menu.
    void bind(Config& cfg) { cfg_ = &cfg; }

    bool open() const { return page_ != Page::Closed; }
    // The menu the title screen leads to, with the game still running behind.
    void open_front();
    void toggle();
    void show_achievements();
    // For screenshots and tests: "main", "options" or "awards".
    bool show_page(const std::string& name);
    std::string page_name() const;
    // The highlighted entry's label, empty when the page has no entries.
    std::string selected_label() const;
    // What that entry is for, for tests and screenshots.
    std::string selected_note() const;
    void close() { page_ = Page::Closed; }

    // Each returns true when the menu consumed the input.
    bool on_key(SDL_Keycode key);
    bool on_pad(Uint8 button);
    bool on_touch_down(float x, float y);
    bool on_touch_up();

    void draw(ui::Ui& g, achievements::Tracker& ach);
    // The front end shows the game beside the list rather than behind it, so
    // it says where the image goes. Returns false to fill the window as usual.
    bool art_panel(ui::Ui& g, SDL_FRect* out) const;
    // Damps the full-window backdrop before the panel is drawn over it.
    void draw_backdrop_dim(ui::Ui& g) const;

private:
    enum class Page { Closed, Front, Main, Options, Controls, Keys, TimeAttack, Achievements };

    struct Item {
        std::string label;
        // Empty for an action; otherwise the value shown on the right.
        std::function<std::string()> value;
        // step is -1 or +1 to change a value, 0 to activate an action.
        std::function<void(int)> act;
        // What the entry is for, drawn small beside the label. The binding
        // pages use it to say what each button does in this game. Last, and
        // with an initialiser, so the entries that leave it out keep both
        // their order and a quiet build.
        std::string note = {};
        // A value that is shown but not edited (a record, say): no arrows.
        bool read_only = false;
    };

    // Pages are built when they are opened, not when they are drawn, so the
    // first key press after opening has something to act on.
    static bool is_row_page(Page p);   // navigated left/right
    static bool is_list_page(Page p);  // navigated up/down
    void set_page(Page p);
    void build_front();
    void build_main();
    void build_options();
    void build_controls();
    void build_time_attack();
    void build_keys();
    void move(int delta);
    void activate(int step);
    void draw_front(ui::Ui& g);
    void draw_row_page(ui::Ui& g, const char* hint);
    void draw_options(ui::Ui& g);
    std::vector<std::string> device_choices() const;
    void draw_achievements(ui::Ui& g, achievements::Tracker& ach);

    Hooks hooks_;
    Config* cfg_ = nullptr;
    Page page_ = Page::Closed;
    // Sub-pages return to whichever page opened them, so Options reached from
    // the title screen goes back there rather than to the pause menu.
    Page return_to_ = Page::Main;
    int keys_player_ = 0;          // which player the binding page is editing
    bool keys_pad_ = false;        // and whether it is their gamepad or keyboard
    std::string awaiting_;         // button being rebound, empty when not capturing
    bool confirm_reset_ = false;   // the reset row is asking a second time
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
