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
        // Takes the game to its own OPTIONS > SOUND TEST (issue #102), from
        // the title screen, the way TIME ATTACK takes it to a stage.
        std::function<void()> game_sound_test;
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
        // LEVEL SELECT (issue #51, frontend/level_select.h): the same way in,
        // untimed and unrecorded, NO SAVE, back to the page when it is over.
        // solo: PARTNER NONE (issue #55), the session played with the ring
        // tether's PARTNER SOLO; the request still names a partner.
        std::function<void(const stage_select::Request&, bool solo)> start_level;
        // The front menu was closed (Escape, the menu button or the pad's back
        // button) instead of START GAME: the player goes on from the game's
        // own title screen.
        std::function<void()> front_closed;
        // The front end's first press before PUSH START is up (the boot
        // screens, the intro, the logo's zoom; see set_title_ready): skip to
        // the title, as Start does in the game.
        std::function<void()> skip_intro;
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
        // What the music is playing, for the keyboards beside the list (issue
        // #102); null when there is nothing to show.
        std::function<const jukebox::Keys*()> music_keys;
        // Speedrun tools (issue #47, OPTIONS > EXTRAS > SPEEDRUN): start a
        // run now, or reset the one going.
        std::function<void()> speedrun_start;
        std::function<void()> speedrun_reset;
        // Netplay (issue #46, NETPLAY page): host a game on the port in the
        // settings, join the address there, leave the session; how it is
        // going, in a few words; and whether there is one.
        std::function<void()> netplay_host;
        std::function<void()> netplay_join;
        std::function<void()> netplay_leave;
        std::function<std::string()> netplay_status;
        std::function<bool()> netplay_active;
        // TIME ATTACK and LEVEL SELECT's zone showcase (issue #53): a picture
        // of the stage asked for at its time of day (only the place, level
        // and AT-TIME count; platform/backdrops.h makes it on demand), with
        // its size, and how it stands (*state, kPicture...): ready; being
        // made, with another picture of the place standing in if there is
        // one (else null); or there is none to be had. And a character
        // standing (frontend/portrait.h), by id, null for one without
        // graphics.
        std::function<SDL_Texture*(const stage_select::Request&, float* w, float* h, int* state)> stage_picture;
        std::function<SDL_Texture*(int character, float* w, float* h)> character_sprite;
    };

    static constexpr int kPictureMaking = 0, kPictureReady = 1, kPictureNone = 2;
    void set_hooks(Hooks h) { hooks_ = std::move(h); }
    // TIME ATTACK's character names, by id (stage_select::character_names,
    // read from the ROM); without them, stage_select::kCharacters.
    void set_character_names(std::vector<std::string> names) { characters_ = std::move(names); }
    // The music player's tracks (jukebox::track_list, read from the ROM).
    void set_music_tracks(std::vector<jukebox::Track> tracks) { tracks_ = std::move(tracks); }
    // The settings the options page edits. Must outlive the menu.
    void bind(Config& cfg) { cfg_ = &cfg; }

    bool open() const { return page_ != Page::Closed; }
    // MENU SIZE (issue #49): what the drawing layer's zoom should be for the
    // next frame (ui::Ui::set_zoom) -- the setting while the menu is open,
    // NORMAL while it is not, so nothing else drawn over the game changes.
    int zoom() const;
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
    // Where the last draw put each entry's touch target, by entry, for tests
    // (MENU SIZE, issue #49). Entries scrolled out of view are not there.
    std::vector<std::pair<int, SDL_FRect>> drawn_rows() const;
    int selected_index() const { return selected_; }
    void close();

    // Each returns true when the menu consumed the input.
    bool on_key(SDL_Keycode key);
    bool on_pad(Uint8 button);
    bool on_touch_down(float x, float y);
    bool on_touch_up();
    // Typing into a text row (the NETPLAY address and port): while it lasts
    // the app has the system's text input on (the on-screen keyboard on a
    // phone) and hands the typed text here. The arrows and a pad's d-pad
    // edit too: up and down change the last character, left deletes it,
    // right adds one; Enter or the pad's confirm button keeps the text,
    // Escape or its back button puts the old one back.
    bool editing_text() const { return editing_ != nullptr; }
    bool on_text(const char* text);

    void draw(ui::Ui& g, achievements::Tracker& ach);
    // Issue #3: the front end is drawn on the game's own title screen, which
    // fills the window as the game does in play. Its selected entry sits on
    // the box the title screen shows PUSH START in, covering it, so the app
    // says where the game's picture is each frame (the image's rectangle in
    // the window, the frame buffer's size and its extra columns a side).
    // Without it the menu takes a 4:3 picture fitted to the window.
    void set_picture(const SDL_FRect& image, int fb_width, int fb_height, int fb_extra);
    // Where the title screen's PUSH START box is for that picture: the inside
    // of its frame, in window pixels.
    static SDL_FRect push_start_box(const SDL_FRect& image, int fb_width, int fb_height, int fb_extra);
    // Whether the game shows PUSH START yet (runtime/title_hold.h), told by
    // the app each frame; true until told otherwise. Before it the front end
    // draws only its hint -- the plaque would float over the city and the
    // road -- and the first press of Up, Down, Left, Right or Enter (the pad's
    // d-pad and confirm button, a tap anywhere) goes to Hooks::skip_intro
    // instead of the entries, which are shown from then on: the player has
    // seen nothing to choose yet, and every later press acts as usual, even
    // before the title is up. Escape and the back button close it as ever.
    // A page opened by name (show_page("front"), --menu) is shown at once.
    void set_title_ready(bool ready) { title_ready_ = ready; }
    bool title_ready() const { return title_ready_; }
    // Whether the menu is over the title screen: the front end, or a page
    // opened from it.
    bool on_title() const { return page_ != Page::Closed && return_to_ == Page::Front; }
    // Where the picture's top goes on a window w x h while on_title(), for
    // the top `y` the viewport gave it: on a portrait window it is raised to
    // the top, so that the front end's list has the room below it.
    static float title_picture_y(float y, float window_w, float window_h);
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
                      Achievements, Cheats, States, StateKeys, Lobby, Mods, ModInfo, Music, Speedrun, Tether, Netplay, LevelSelect };

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
    void build_tether();
    void build_speedrun();
    void build_controls();
    void build_time_attack();
    // LEVEL SELECT (issue #51).
    void build_level_select();
    // The PLAYER and COMBI rows and the PLAYERS row TIME ATTACK and LEVEL
    // SELECT share, editing the settings given.
    // no_partner (LEVEL SELECT, issue #55): COMBI offers NONE after the
    // characters where level_select::partner_none_offered.
    void add_character_rows(int* player, int* combi, bool* no_partner = nullptr);
    void add_players_row(bool* two_players);
    // TIME ATTACK and LEVEL SELECT are one layout (issue #53), the zone
    // showcase: the stage's picture with the place's name over it and rows
    // of chips for its levels and the time of day under it; beside it (under
    // it on a portrait screen) the player's and partner's cards, the records
    // (or LEVEL SELECT's note), PLAYERS and START. The entries are the pages'
    // own, in the order up and down step through them; this says what they
    // edit and which entry is drawn where.
    struct Showcase {
        bool timed = false;  // TIME ATTACK: records and a ghost
        int* place = nullptr;
        int* level = nullptr;
        int* time = nullptr;
        int* player = nullptr;
        int* combi = nullptr;
        bool* two_players = nullptr;
        bool* no_partner = nullptr;  // LEVEL SELECT's PARTNER: NONE
        int* ghost = nullptr;
        // The entries, by index into items_; -1 where the page has none.
        int place_row = -1, level_row = -1, time_row = -1, player_row = -1, combi_row = -1, best_row = -1,
            ghost_row = -1, players_row = -1, start_row = -1, back_row = -1;
    };
    bool is_showcase() const { return page_ == Page::TimeAttack || page_ == Page::LevelSelect; }
    // What START asks for, as the page stands.
    stage_select::Request showcase_request() const;
    // And whether it is without a partner: NONE chosen, and offered for the
    // place and PLAYERS chosen.
    bool showcase_no_partner() const;
    // The levels the place offers, and how a chip names one.
    std::vector<int> showcase_levels() const;
    std::string showcase_level_label(int level) const;
    void draw_showcase(ui::Ui& g);
    void build_keys();
    void build_states();
    void build_state_keys();
    void build_mods();
    void build_mod_info();
    void build_music();
    void build_netplay();
    // Text rows: start typing into `target` (only `allowed` characters, at
    // most `max_len`); `done` runs when the text is kept.
    void begin_edit(std::string* target, const char* allowed, size_t max_len, std::function<void()> done);
    void end_edit(bool keep);
    bool edit_key(int key);  // 0 keep, 1 cancel, 2 delete, 3 next char, 4 previous char, 5 add
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
    // The PUSH START box for the picture the app gave (set_picture), or for
    // a 4:3 picture fitted to the window.
    SDL_FRect title_box(ui::Ui& g) const;
    // The game's picture as set_picture gave it, or that 4:3 picture.
    SDL_FRect picture_rect(ui::Ui& g) const;
    void draw_front(ui::Ui& g);
    void draw_row_page(ui::Ui& g, const char* hint);
    void draw_options(ui::Ui& g);
    // The music player's keyboards (issue #102), in r.
    void draw_music_keys(ui::Ui& g, const SDL_FRect& r, const jukebox::Keys& k);
    std::vector<std::string> device_choices() const;
    void draw_achievements(ui::Ui& g, achievements::Tracker& ach);
    bool backdrop_on() const;
    // The awards' magenta (issue #3, platform/award_art.h), or the zone's
    // colour while its picture is shown.
    ui::Color accent() const;
    // Captions and hints: a step brighter over a picture than over plain dark.
    ui::Color faint() const { return backdrop_on() ? ui::theme::text_dim : ui::theme::text_faint; }
    // Fills r with the zone's picture under its tint (or `plain` without
    // one), as the panels and bands are filled.
    void fill(ui::Ui& g, const SDL_FRect& r, ui::Color plain, uint8_t tint_alpha) const;

    // The game's picture in the window (set_picture); zero width until set.
    SDL_FRect picture_{};
    int picture_fb_w_ = 320, picture_fb_h_ = 224, picture_extra_ = 0;
    bool title_ready_ = true;  // set_title_ready
    // A press has already been made on the front end before PUSH START (or
    // the page was asked for by name): the entries are shown from then on,
    // whether or not the title is up yet. Reset when the front end opens.
    bool revealed_ = false;
    // The front end before PUSH START is up, nothing pressed yet: only the
    // hint, and the first press skips to the title (and shows the entries).
    bool waiting_for_title() const { return page_ == Page::Front && !title_ready_ && !revealed_; }
    void skip_intro() {
        revealed_ = true;
        if (hooks_.skip_intro) hooks_.skip_intro();
    }

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
    Showcase sc_;
    // Each showcase column's scroll, for a screen too small for all of it.
    float sc_scroll_[2] = {0, 0};
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
        // A target that acts at once when tapped (the showcase's chips,
        // arrows and START), after selecting its entry; without, the first
        // tap selects and the second acts.
        std::function<void()> tap = {};
    };
    std::vector<Hit> hits_;
    // Hit indices of the markers a list too long for the screen shows.
    static constexpr int kScrollUp = -2, kScrollDown = -3;
    float drag_y_ = 0, drag_total_ = 0;
    // Drives the little bounce on the selected entry.
    float anim_ = 0;
    // The text row being typed into (begin_edit).
    std::string* editing_ = nullptr;
    std::string edit_before_;
    const char* edit_allowed_ = "";
    size_t edit_max_ = 0;
    std::function<void()> edit_done_;
    std::string port_text_;  // the PORT row's text while it is typed
};

} // namespace chaotix
