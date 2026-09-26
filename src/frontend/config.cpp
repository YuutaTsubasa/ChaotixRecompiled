#include "config.h"
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace chaotix {

namespace {
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
bool parse_bool(const std::string& v, bool def) {
    if (v == "1" || v == "true" || v == "True" || v == "on" || v == "yes") return true;
    if (v == "0" || v == "false" || v == "False" || v == "off" || v == "no") return false;
    return def;
}
} // namespace

const char* window_mode_name(WindowMode m) {
    switch (m) {
    case WindowMode::Windowed: return "Windowed";
    case WindowMode::Borderless: return "Borderless";
    case WindowMode::Fullscreen: return "Fullscreen";
    }
    return "Windowed";
}

void Config::set_defaults() {
    *this = Config{};
    keys = {
        {"up", "Up"}, {"down", "Down"}, {"left", "Left"}, {"right", "Right"},
        {"a", "Z"}, {"b", "X"}, {"c", "C"}, {"start", "Return"},
        {"x", "A"}, {"y", "S"}, {"z", "D"}, {"mode", "Right Shift"},
    };
    // Player 2 on the keypad: far enough from player 1 that two people can
    // share one keyboard, and nothing clashes with the set above.
    keys2 = {
        {"up", "Keypad 8"}, {"down", "Keypad 2"}, {"left", "Keypad 4"}, {"right", "Keypad 6"},
        {"a", "Keypad 1"}, {"b", "Keypad 3"}, {"c", "Keypad 0"}, {"start", "Keypad Enter"},
        {"x", "Keypad 7"}, {"y", "Keypad 9"}, {"z", "Keypad 5"}, {"mode", "Keypad +"},
    };
}

bool Config::load(const std::string& path) {
    set_defaults();
    std::ifstream f(path);
    if (!f) return false;
    std::string line, section;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line.front() == '[' && line.back() == ']') { section = line.substr(1, line.size() - 2); continue; }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        raw[section + "." + k] = v;
        if (section == "Game" && k == "RomPath") rom_path = v;
        else if (section == "Game" && k == "Installed") installed = parse_bool(v, installed);
        else if (section == "Game" && k == "RomSha1") rom_sha1 = v;
        else if (section == "Video") {
            if (k == "AspectRatio") parse_aspect(v, viewport.aspect, viewport.custom_aspect);
            else if (k == "Scaling") parse_scale(v, viewport.scale);
            else if (k == "Filter") linear_filter = (v == "Linear" || v == "linear");
            else if (k == "WindowMode") {
                if (v == "Borderless") window_mode = WindowMode::Borderless;
                else if (v == "Fullscreen") window_mode = WindowMode::Fullscreen;
                else window_mode = WindowMode::Windowed;
            } else if (k == "WindowScale") window_scale = std::max(1, std::atoi(v.c_str()));
            else if (k == "VSync") vsync = parse_bool(v, vsync);
            else if (k == "Widescreen") widescreen = parse_bool(v, widescreen);
        } else if (section == "Achievements") {
            if (k == "Enabled") achievements = parse_bool(v, achievements);
        } else if (section == "Audio") {
            if (k == "Enabled") audio = parse_bool(v, audio);
            else if (k == "Volume") volume = std::clamp(std::atoi(v.c_str()), 0, 100);
        } else if (section == "Input") {
            if (k == "TouchControls") touch = v == "On" ? TouchMode::On : v == "Off" ? TouchMode::Off : TouchMode::Auto;
            else if (k == "SixButtonPad") six_button = parse_bool(v, six_button);
            else if (k == "Device1") device[0] = v;
            else if (k == "Device2") device[1] = v;
            else if (k.rfind("Key.", 0) == 0) keys[k.substr(4)] = v;
            else if (k.rfind("Key2.", 0) == 0) keys2[k.substr(5)] = v;
        } else if (section == "Debug") {
            if (k == "Overlay") debug_overlay = parse_bool(v, debug_overlay);
            else if (k == "UseRecompiledCode") use_recompiled = parse_bool(v, use_recompiled);
        }
    }
    return true;
}

bool Config::save(const std::string& path) const {
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return false;
    std::fprintf(f, "# Knuckles' Chaotix Recompiled settings\n\n[Game]\n");
    std::fprintf(f, "# Path to your own legally obtained ROM (never distributed with this project)\n");
    std::fprintf(f, "RomPath = %s\n", rom_path.c_str());
    std::fprintf(f, "# Set by the first-run setup once the ROM has been copied into this folder.\n");
    std::fprintf(f, "Installed = %s\nRomSha1 = %s\n\n", installed ? "true" : "false", rom_sha1.c_str());
    std::fprintf(f, "[Video]\n");
    std::fprintf(f, "# Auto (match window) | 4:3 | 16:9 | 16:10 | 21:9 | W:H\n");
    if (viewport.aspect == AspectMode::Custom) std::fprintf(f, "AspectRatio = %.4f\n", viewport.custom_aspect);
    else std::fprintf(f, "AspectRatio = %s\n", aspect_name(viewport.aspect));
    std::fprintf(f, "# true: levels render extra columns (or rows, for frames taller than 4:3) to fill\n");
    std::fprintf(f, "# the aspect ratio, from 1.24:1 (320x240) to 2:1 (480x224); false: original 4:3\n");
    std::fprintf(f, "Widescreen = %s\n", widescreen ? "true" : "false");
    std::fprintf(f, "# Integer | Fit | Stretch\nScaling = %s\n", scale_name(viewport.scale));
    std::fprintf(f, "# Nearest | Linear\nFilter = %s\n", linear_filter ? "Linear" : "Nearest");
    std::fprintf(f, "# Windowed | Borderless | Fullscreen\nWindowMode = %s\n", window_mode_name(window_mode));
    std::fprintf(f, "WindowScale = %d\nVSync = %s\n\n", window_scale, vsync ? "true" : "false");
    std::fprintf(f, "[Audio]\nEnabled = %s\nVolume = %d\n\n", audio ? "true" : "false", volume);
    std::fprintf(f, "[Achievements]\n# Local achievements from assets/achievements.ini (nothing leaves this machine)\n");
    std::fprintf(f, "Enabled = %s\n\n", achievements ? "true" : "false");
    std::fprintf(f, "[Input]\n# Auto | On | Off\nTouchControls = %s\n", touch == TouchMode::On ? "On" : touch == TouchMode::Off ? "Off" : "Auto");
    std::fprintf(f, "SixButtonPad = %s\n", six_button ? "true" : "false");
    std::fprintf(f, "# Who drives each player: keyboard | pad1..padN | none\n");
    std::fprintf(f, "Device1 = %s\nDevice2 = %s\n", device[0].c_str(), device[1].c_str());
    for (const auto& [b, k] : keys) std::fprintf(f, "Key.%s = %s\n", b.c_str(), k.c_str());
    for (const auto& [b, k] : keys2) std::fprintf(f, "Key2.%s = %s\n", b.c_str(), k.c_str());
    std::fprintf(f, "\n[Debug]\nOverlay = %s\nUseRecompiledCode = %s\n", debug_overlay ? "true" : "false", use_recompiled ? "true" : "false");
    std::fclose(f);
    return true;
}

} // namespace chaotix
