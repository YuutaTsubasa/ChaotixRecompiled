#pragma once

// The in-game menu: a horizontal row of choices over the running game, in the
// style of the era, with an options page behind it. Everything here is drawn
// by the host over the emulated image; the game's own menus are untouched.

#include <SDL3/SDL.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "frontend/jukebox.h"
#include "platform/ui.h"
#include "runtime/mod_packs.h"
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
        // Android (issue #29): the Mods folder on shared storage, empty while
        // the app may not use it, and the request for that access.
        std::function<std::string()> mods_folder;
        std::function<void()> share_mods_folder;
        // The controller's own name, so the page can say which one it means.
        std::function<std::string(int index)> pad_name;
        // Locks every achievement again and forgets the saved progress.
        std::function<void()> reset_awards;
        // Writes every frame of the characters' sprite banks as sheets to
        // Mods/Textures/Sheets.
        std::function<void()> export_sprites;
        // Starts a level straight away, the way the game's own stage select
        // would: TIME ATTACK.
        std::function<void(const stage_select::Request&)> start_stage;
        // The front menu was closed (Escape, the menu button or the pad's back
        // button) instead of START GAME: the player goes on from the game's
        // own title screen.
        std::function<void()> front_closed;
        // Save states (issue #40), in slots 1..kStateSlots: save or load one
        // (the app says how it went), what a slot holds ("EMPTY", or when it
        // was saved), and why states cannot be used just now (a TIME ATTACK
        // run), empty when they can.
        std::function<void(int slot)> save_state;
        std::function<void(int slot)> load_state;
        std::function<std::string(int slot)> state_info;
        std::function<std::string()> states_locked;
        // Mod packs (issue #42, OPTIONS > EXTRAS > MODS): the list in the
        // player's order, which the page reorders and switches in place
        // (null: no mods here); a change made there, so the app can say
        // whether it waits for a restart (mods_need_restart: patches or
        // character stats); reading the Mods folder again and applying what
        // can be applied at once, which the page also asks for when it is
        // left after a change; and a pack's preview image, with its size.
        std::function<std::vector<mod_packs::Pack>*()> mod_packs;
        std::function<void()> mods_changed;
        std::function<void()> reload_mods;
        std::function<bool()> mods_need_restart;
        std::function<SDL_Texture*(const mod_packs::Pack&, float* w, float* h)> mod_preview;
        // The music player (issue #48, frontend/jukebox.h). The app runs its
        // machine while the page is open (page_name() "music") and holds the
        // game; the page asks it to play a track (by music id), stop, fade,
        // step to the next or previous track, and what is playing, and
        // whether a music mod replaces a track.
        std::function<void(uint8_t id)> music_play;
        std::function<void()> music_stop;
        std::function<void()> music_fade;
        std::function<void(int delta)> music_step;
        std::function<jukebox::Status()> music_status;
        std::function<bool(uint8_t id)> music_replaced;
    };

    void set_hooks(Hooks h) { hooks_ = std::move(h); }
    // TIME ATTACK's character names, by id (stage_select::character_names,
    // read from the ROM); without them, stage_select::kCharacters.
    void set_character_names(std::vector<std::string> names) { characters_ = std::move(names); }
    // The music player's tracks (jukebox::track_list, read from the ROM).
    void set_music_tracks(std::vector<jukebox::Track> tracks) { tracks_ = std::move(tracks); }
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
    // The value it shows on the right, empty for an action.
    std::string selected_value() const;
    // What that entry is for, for tests and screenshots.
    std::string selected_note() const;
    void close();

    // Each returns true when the menu consumed the input.
    bool on_key(SDL_Keycode key);
    bool on_pad(Uint8 button);
    bool on_touch_down(float x, float y);
    bool on_touch_up();

    void draw(ui::Ui& g, achievements::Tracker& ach);
    // The front end shows the game beside the list rather than behind it, so
    // it says where the image goes. Returns false to fill the window as usual.
    bool art_panel(ui::Ui& g, SDL_FRect* out) const;
    // Damps the full-window backdrop before the panel is drawn over it, or
    // with a zone picture behind the menu puts it there instead.
    void draw_backdrop_dim(ui::Ui& g) const;
    // Issue #8: a picture of one of the game's zones (platform/backdrops.h)
    // for the menu's panels and backgrounds, with the translucent colour laid
    // over it and the colour for frames and markers, both taken from it. The
    // menu uses it unless MENU BACKGROUND is PLAIN; without one it keeps its
    // plain colours. The texture must outlive its use.
    void set_backdrop(SDL_Texture* texture, float width, float height, ui::Color tint, ui::Color accent);
    void clear_backdrop() { backdrop_ = nullptr; }

private:
    // OPTIONS is a list of categories; each category is a page of its own, so
    // that no page runs off a phone's screen.
    enum class Page { Closed, Front, Main, Options, Video, Sound, Input, Extras, Controls, Keys, TimeAttack,
                      Achievements, Cheats, States, StateKeys, Lobby, Mods, ModInfo, Music };

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
        // A value row that is pressed rather than stepped: activating it does
        // something (starts a capture, asks for confirmation) and left/right
        // do not change it, so it has no arrows either.
        bool pressed = false;
    };

    // Pages are built when they are opened, not when they are drawn, so the
    // first key press after opening has something to act on.
    static bool is_row_page(Page p);   // navigated left/right
    static bool is_list_page(Page p);  // navigated up/down
    void set_page(Page p);
    void build_front();
    void build_main();
    void build_options();
    void build_video();
    void build_sound();
    void build_input();
    void build_extras();
    static Page parent_of(Page p);
    void build_cheats();
    void build_lobby();
    void build_controls();
    void build_time_attack();
    void build_keys();
    void build_states();
    void build_state_keys();
    void build_mods();
    void build_mod_info();
    void build_music();
    // Leaving the MODS pages after a change: the app applies it.
    void leave_mods();
    // The setting a host-key capture writes (fast-forward, save state, ...),
    // or null when awaiting_ is a Mega Drive button.
    std::string* capture_target(const std::string& what) const;
    void move(int delta);
    void activate(int step);
    // Escape, the menu button or the pad's back button: one level up, or
    // closed.
    void back_out();
    void draw_front(ui::Ui& g);
    void draw_row_page(ui::Ui& g, const char* hint);
    void draw_options(ui::Ui& g);
    std::vector<std::string> device_choices() const;
    void draw_achievements(ui::Ui& g, achievements::Tracker& ach);
    bool backdrop_on() const;
    // The theme's blue, or the zone's colour while its picture is shown.
    ui::Color accent() const { return backdrop_on() ? zone_accent_ : ui::theme::accent; }
    // Captions and hints: a step brighter over a picture than over plain dark.
    ui::Color faint() const { return backdrop_on() ? ui::theme::text_dim : ui::theme::text_faint; }
    // Fills r with the zone's picture under its tint (or `plain` without
    // one), as the panels and bands are filled.
    void fill(ui::Ui& g, const SDL_FRect& r, ui::Color plain, uint8_t tint_alpha) const;

    SDL_Texture* backdrop_ = nullptr;
    float backdrop_w_ = 0, backdrop_h_ = 0;
    ui::Color zone_tint_{}, zone_accent_{};

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
    std::vector<std::string> characters_;  // see set_character_names
    std::vector<jukebox::Track> tracks_;   // see set_music_tracks
    Page music_back_ = Page::Sound;        // where the music player goes back to
    size_t mod_index_ = 0;         // the pack the MODS details page shows
    bool mods_dirty_ = false;      // a pack was switched or moved since the last reload
    // Lines of text under the heading (the pack's description on its page),
    // wrapped to the panel when drawn.
    std::vector<std::string> info_lines_;
    std::vector<mod_packs::Pack>* packs() const { return hooks_.mod_packs ? hooks_.mod_packs() : nullptr; }
    // Rebuilds the page in place, keeping the selection.
    void refresh();
    std::vector<Item> items_;
    int selected_ = 0;
    int scroll_ = 0;          // achievements page
    int rows_ = 1;            // rows the achievements page had room for
    float row_h_ = 0;
    // Touch: where the drawn rows were, so a tap can hit them.
    struct Hit {
        SDL_FRect r;
        int index;
        // Where a value row's "<" arrow is; zero width when it has none.
        float back_x = 0, back_w = 0;
    };
    std::vector<Hit> hits_;
    // Hit indices of the markers a list too long for the screen shows.
    static constexpr int kScrollUp = -2, kScrollDown = -3;
    float drag_y_ = 0, drag_total_ = 0;
    // Drives the little bounce on the selected entry.
    float anim_ = 0;
};

} // namespace chaotix
