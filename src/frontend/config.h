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
    // [Video]
    ViewportConfig viewport;
    bool linear_filter = false;
    WindowMode window_mode = WindowMode::Windowed;
    int window_scale = 3;
    bool vsync = true;
    // [Audio]
    bool audio = true;
    int volume = 80;  // percent
    // [Input]
    TouchMode touch = TouchMode::Auto;
    bool six_button = true;
    std::map<std::string, std::string> keys;  // pad button name -> key name
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
