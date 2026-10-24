// User settings (Config/chaotix.ini). Plain "key = value" lines grouped in
// [Sections]; unknown keys are preserved on save.
#pragma once
#include "frontend/fast_forward.h"
#include "renderer/viewport.h"
#include "runtime/mod_packs.h"
#include <map>
#include <set>
#include <string>

namespace chaotix {

enum class WindowMode { Windowed, Borderless, Fullscreen };
enum class TouchMode { Auto, On, Off };

// Rewind's default (issue #40): on where memory and power are plentiful, off
// on phones and tablets, where it costs battery and up to 32 MB every frame
// of play whether or not it is ever used, and needs a button the touch
// layout does not show unless asked.
bool rewind_default();
// TIME ATTACK ghosts (issue #41): ghost::Mode as a number, off on phones.
int ghost_default();
// MENU SIZE (issue #49): what AUTO means here. LARGE on phones and tablets,
// whose screens are a few centimetres tall in landscape, so NORMAL's rows
// come out a few millimetres high -- small to read and smaller than a finger;
// NORMAL elsewhere.
int menu_size_default();
// The MenuSize setting's names, by value (0 NORMAL, 1 LARGE, 2 EXTRA LARGE;
// -1 is AUTO), and back; anything unknown is AUTO.
const char* menu_size_name(int size);
int parse_menu_size(const std::string& v);
constexpr int kStateSlots = 4;

struct Config {
    // [Game]
    std::string rom_path;
    // First-run setup: set once the user's ROM has been copied into the user
    // data directory, with the hash it was verified against.
    bool installed = false;
    std::string rom_sha1;
    // [Video]
    ViewportConfig viewport;
    bool linear_filter = false;
    WindowMode window_mode = WindowMode::Windowed;
    int window_scale = 3;
    bool vsync = true;
    // SHOW FPS (issue #54): a small counter in a corner with the frame rates and
    // the emulation time per frame (frontend/fps_counter.h). Off by default.
    bool show_fps = false;
    bool widescreen = true;  // true widescreen in levels (margins follow AspectRatio)
    // Widescreen in the scenes kept 4:3 because they show glitches there: the
    // lobby and the INTRODUCTION Eggman scene.
    bool wide_everywhere = false;
    // In widened levels, the HUD at the picture's left edge (issue #28).
    bool wide_hud = true;
    // Issue #8: the menu's labels in the game's own font (read from the ROM)
    // rather than the bundled pixel face, and pictures of the game's zones
    // (rendered from the ROM and cached) behind it rather than plain colour.
    bool menu_game_font = true;
    // MENU BACKGROUND (issue #37): "Cycle" (the zones in turn, the next one
    // each time the menu opens), "Plain", or one zone's `zone` key from
    // frontend/backdrop.h ("WorldEntrance", "MarinaMadness", ...).
    std::string menu_background = "Cycle";
    // MENU SIZE (issue #49): -1 AUTO (menu_size_default), 0 NORMAL, 1 LARGE,
    // 2 EXTRA LARGE: the menu's rows, letters and panels, and so its touch
    // targets. menu_zoom() is the size in effect.
    int menu_size = -1;
    int menu_zoom() const { return menu_size < 0 ? menu_size_default() : menu_size; }
    bool menu_backdrop_on() const { return menu_background != "Plain"; }
    // The chosen zone's index in backdrop_specs(); -1 for Cycle and Plain.
    int menu_backdrop_zone() const;
    bool achievements = true;  // local achievement tracking (assets/achievements.ini)
    // Write each graphic drawn (Mega Drive tiles, 32X sprites) to
    // Mods/Textures/Dump (issue #35).
    bool dump_graphics = false;
    // Mod packs (issue #42): their order, highest priority first, the ones
    // turned off and whether the loose files in Mods/ count ([Mods] Order,
    // Disabled, LooseFiles).
    mod_packs::Order mod_order;
    // [Audio]
    bool audio = true;
    int volume = 80;  // percent: everything
    // Issue #24: the game's music and its sound effects, each in percent of
    // Volume (audio/volumes.h).
    int music_volume = 100;
    int effects_volume = 100;
    // Music played from files in <user data>/Mods/Music (audio/music_mods.h),
    // relative to Volume: they are mastered louder than the game's own.
    int mod_music_volume = 25;  // percent
    // [Input]
    TouchMode touch = TouchMode::Auto;
    bool six_button = true;
    bool touch_stick = false;  // the touch directions as a joystick (issue #30)
    bool touch_sonic_cd = false;  // Sonic CD (2013) touch layout (issue #34)
    int touch_opacity = 65;           // touch controls at rest, percent (issue #33)
    int touch_pressed_opacity = 100;  // and held
    // What drives each player: "auto" (the keyboard and every gamepad the
    // other player has not claimed), "keyboard", "pad1".."padN", or "none".
    std::string device[2] = {"auto", "none"};
    // The gamepad button that opens this menu (Escape's counterpart). Only
    // buttons the emulated 6-button pad does not use are offered, so binding
    // one never costs the game a button. SDL gamepad button name.
    std::string menu_button = "leftstick";
    // Fast-forward (issue #45): the key (SDL key name), the gamepad button
    // (SDL gamepad button name, or "lefttrigger" / "righttrigger", or "none"),
    // an on-screen button (off by default), held or toggled, and how fast.
    // MAX is what fast-forward always did, so it stays the default.
    std::string ff_key = "Tab";
    std::string ff_button = "righttrigger";
    bool touch_ff = false;
    FastForwardMode ff_mode = FastForwardMode::Hold;
    FastForwardSpeed ff_speed = FastForwardSpeed::Max;
    // Save states and rewind (issue #40): keys (SDL key names) for quick
    // save, quick load, the next slot and rewind (held); gamepad buttons (as
    // ff_button). Saving and loading start with no pad button, so nobody
    // overwrites a state by brushing a button; rewind takes the left trigger,
    // fast-forward's opposite. The on-screen rewind button is off, like
    // fast-forward's.
    std::string state_save_key = "F8";
    std::string state_load_key = "F9";
    std::string state_slot_key = "F10";
    std::string rewind_key = "Backspace";
    std::string state_save_button = "none";
    std::string state_load_button = "none";
    std::string rewind_button = "lefttrigger";
    bool touch_rewind = false;
    // Mega Drive button name -> what produces it, one set per player so two
    // people can share a keyboard or two controllers behave differently.
    // keys*: SDL key names. pads*: SDL gamepad button names.
    std::map<std::string, std::string> keys;
    std::map<std::string, std::string> keys2;
    std::map<std::string, std::string> pads;
    std::map<std::string, std::string> pads2;
    // [TimeAttack] The selection the front end offers next time, so a run can
    // be repeated without setting it all up again. Values are the game's own
    // (runtime/stage_select.h).
    int ta_place = 0;
    int ta_level = 1;
    int ta_time = 0;      // 0, 2, 4 or 6
    int ta_player = 2;    // KNUCKLES
    int ta_combi = 7;     // ESPIO
    bool ta_two_players = false;
    // Best time per stage, in level-clock frames, keyed "<place>.<level>".
    std::map<std::string, int> ta_best;
    // The ghost a run races (frontend/ghost.h, ghost::Mode): 0 OFF, 1 BEST,
    // 2 LAST.
    int ta_ghost = ghost_default();
    // [LevelSelect] Issue #51 (frontend/level_select.h): the level the front
    // end's LEVEL SELECT offers next time, apart from TIME ATTACK's so that
    // practising one does not move the other. The game's own values.
    int ls_place = 0;
    int ls_level = 1;
    int ls_time = 0;
    int ls_player = 2;   // KNUCKLES
    int ls_combi = 7;    // ESPIO
    bool ls_two_players = false;
    // [Cheats] (game/cheats.h): the built-in ones switched on, by key, and
    // Action Replay codes of the player's own ("FFE009:63 + FFADFC:01").
    std::set<std::string> cheats;
    std::string cheat_codes;
    // [Extras] Rewind on or off, and the slot quick save and quick load use
    // (1..kStateSlots).
    bool rewind = rewind_default();
    int state_slot = 1;
    // [Lobby] Lobby shortcuts (issue #44, runtime/lobby.h): in story mode,
    // the partner (a character id, -1 the Combi Catcher's) and the time of
    // day (0 MORNING .. 3 NIGHT, -1 the game's own) each time the game
    // starts the lobby or an attraction. Off: the game as it was.
    bool lobby_shortcuts = false;
    int lobby_partner = -1;
    int lobby_time = -1;
    // [Tether] Ring tether options (issue #43, runtime/tether.h): the
    // tether's strength (tether::Strength) and what the partner does
    // (tether::Partner), 0 the original game. Not in TIME ATTACK runs.
    int tether_strength = 0;
    int tether_partner = 0;
    // [MusicPlayer] (issue #48, frontend/jukebox.h): what happens when a
    // track ends or has played FadeAfter seconds (0: never fades): 0 LOOP
    // (the track again, for ever), 1 ONCE (stop), 2 ALL (the next one).
    int music_repeat = 0;
    int music_fade_after = 120;
    // [Speedrun] Issue #47 (frontend/speedrun.h, docs/SPEEDRUN.md): the
    // timer and splits overlay, the input display, where each is drawn and
    // how opaque, which time the splits compare, and the keys that start and
    // reset a run (SDL key names). All off: the game as it was.
    bool sr_timer = false;
    bool sr_splits = false;
    bool sr_compare_game = false;  // false: real time, true: game time
    bool sr_input = false;
    std::string sr_timer_corner = "TopRight";
    std::string sr_input_corner = "BottomLeft";
    int sr_opacity = 85;  // percent
    std::string sr_start_key = "Home";
    std::string sr_reset_key = "End";
    // [Netplay] Issue #46 (net/netplay.h, docs/NETPLAY.md): the UDP port a
    // host waits on, the address last joined ("host:port"), and the frames
    // of input delay (0-4).
    int netplay_port = 7845;
    std::string netplay_address;
    int netplay_delay = 2;
    // [Debug]
    bool debug_overlay = false;
    bool use_recompiled = true;

    std::map<std::string, std::string> raw;  // everything as read (Section.Key)

    void set_defaults();
    bool load(const std::string& path);
    bool save(const std::string& path) const;
};

const char* window_mode_name(WindowMode m);
// A `MenuBackground` value as it is kept: Cycle, Plain or a zone's key, in
// their own spelling, case ignored on the way in. The value from before
// issue #37, Zone, is Cycle; so is anything unknown.
std::string parse_menu_background(const std::string& v);

// Best times. 0 means there is none yet; record_best returns true when the
// time is an improvement (and stores it).
std::string best_key(int place, int level);
int best_time(const Config& c, int place, int level);
bool record_best(Config& c, int place, int level, int frames);

} // namespace chaotix
