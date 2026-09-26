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
    // What drives each player: "keyboard", "pad1".."padN", or "none".
    std::string device[2] = {"keyboard", "none"};
    // pad button name -> key name, one set per player, so two people can
    // share a keyboard.
    std::map<std::string, std::string> keys;
    std::map<std::string, std::string> keys2;
    // [Debug]
    bool debug_overlay = false;
    bool use_recompiled = true;

    std::map<std::string, std::string> raw;  // everything as read (Section.Key)

    void set_defaults();
    bool load(const std::string& path);
    bool save(const std::string& path) const;
};

const char* window_mode_name(WindowMode m);

} // namespace chaotix
