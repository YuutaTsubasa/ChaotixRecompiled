// User settings (Config/chaotix.ini). Plain "key = value" lines grouped in
// [Sections]; unknown keys are preserved on save.
#pragma once
#include "renderer/viewport.h"
#include <map>
#include <string>

namespace chaotix {

enum class WindowMode { Windowed, Borderless, Fullscreen };
enum class TouchMode { Auto, On, Off };

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
    bool widescreen = true;  // true widescreen in levels (margins follow AspectRatio)
    bool achievements = true;  // local achievement tracking (assets/achievements.ini)
    // [Audio]
    bool audio = true;
    int volume = 80;  // percent
    // [Input]
    TouchMode touch = TouchMode::Auto;
    bool six_button = true;
    // What drives each player: "auto" (the keyboard and every gamepad the
    // other player has not claimed), "keyboard", "pad1".."padN", or "none".
    std::string device[2] = {"auto", "none"};
    // The gamepad button that opens this menu (Escape's counterpart). Only
    // buttons the emulated 6-button pad does not use are offered, so binding
    // one never costs the game a button. SDL gamepad button name.
    std::string menu_button = "leftstick";
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
    // [Debug]
    bool debug_overlay = false;
    bool use_recompiled = true;

    std::map<std::string, std::string> raw;  // everything as read (Section.Key)

    void set_defaults();
    bool load(const std::string& path);
    bool save(const std::string& path) const;
};

const char* window_mode_name(WindowMode m);

// Best times. 0 means there is none yet; record_best returns true when the
// time is an improvement (and stores it).
std::string best_key(int place, int level);
int best_time(const Config& c, int place, int level);
bool record_best(Config& c, int place, int level, int frames);

} // namespace chaotix
