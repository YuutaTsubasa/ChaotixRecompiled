#include "config.h"
#include "frontend/backdrop.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <sstream>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

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

std::string parse_menu_background(const std::string& v) {
    std::string low;
    for (char ch : v) low += char(std::tolower(static_cast<unsigned char>(ch)));
    if (low == "plain") return "Plain";
    const int zone = backdrop_find_zone(v);
    if (zone >= 0) return backdrop_specs()[size_t(zone)].zone;
    return "Cycle";  // Cycle, the earlier Zone, or anything unknown
}

int Config::menu_backdrop_zone() const { return backdrop_find_zone(menu_background); }

bool rewind_default() {
#if defined(__ANDROID__) || (defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    return false;
#else
    return true;
#endif
}

void Config::set_defaults() {
    *this = Config{};
    keys = {
        {"up", "Up"}, {"down", "Down"}, {"left", "Left"}, {"right", "Right"},
        {"a", "Z"}, {"b", "X"}, {"c", "C"}, {"start", "Return"},
        {"x", "A"}, {"y", "S"}, {"z", "D"}, {"mode", "Right Shift"},
    };
    // The Mega Drive's six-button layout on a modern pad: bottom row A B C
    // across the face buttons, top row X Y Z on shoulder-north-shoulder.
    // Both players start here; assign them different controllers to split.
    pads = {
        {"up", "dpup"}, {"down", "dpdown"}, {"left", "dpleft"}, {"right", "dpright"},
        {"a", "x"}, {"b", "a"}, {"c", "b"}, {"start", "start"},
        {"x", "leftshoulder"}, {"y", "y"}, {"z", "rightshoulder"}, {"mode", "back"},
    };
    pads2 = pads;
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
            else if (k == "WidescreenEverywhere") wide_everywhere = parse_bool(v, wide_everywhere);
            else if (k == "WidescreenHud") wide_hud = parse_bool(v, wide_hud);
            else if (k == "MenuFont") menu_game_font = !(v == "Pixel" || v == "pixel");
            else if (k == "MenuBackground") menu_background = parse_menu_background(v);
        } else if (section == "Cheats") {
            if (k == "Enabled") {
                cheats.clear();
                std::string key;
                for (char ch : v + ",") {
                    if (ch == ',' || ch == ' ') { if (!key.empty()) cheats.insert(key); key.clear(); }
                    else key += char(std::tolower(static_cast<unsigned char>(ch)));
                }
            } else if (k == "Codes") cheat_codes = v;
        } else if (section == "Achievements") {
            if (k == "Enabled") achievements = parse_bool(v, achievements);
        } else if (section == "Mods") {
            // DumpTiles is its name from before the 32X sprites were dumped too.
            if (k == "DumpGraphics" || k == "DumpTiles") dump_graphics = parse_bool(v, dump_graphics);
        } else if (section == "Audio") {
            if (k == "Enabled") audio = parse_bool(v, audio);
            else if (k == "Volume") volume = std::clamp(std::atoi(v.c_str()), 0, 100);
            else if (k == "ModMusicVolume") mod_music_volume = std::clamp(std::atoi(v.c_str()), 0, 100);
            else if (k == "MusicVolume") music_volume = std::clamp(std::atoi(v.c_str()), 0, 100);
            else if (k == "EffectsVolume") effects_volume = std::clamp(std::atoi(v.c_str()), 0, 100);
        } else if (section == "Input") {
            if (k == "TouchControls") touch = v == "On" ? TouchMode::On : v == "Off" ? TouchMode::Off : TouchMode::Auto;
            else if (k == "SixButtonPad") six_button = parse_bool(v, six_button);
            else if (k == "TouchStick") touch_stick = parse_bool(v, touch_stick);
            else if (k == "TouchLayout") touch_sonic_cd = v == "SonicCD";
            else if (k == "TouchOpacity") touch_opacity = std::clamp(std::atoi(v.c_str()), 0, 100);
            else if (k == "TouchPressedOpacity") touch_pressed_opacity = std::clamp(std::atoi(v.c_str()), 0, 100);
            else if (k == "Device1") device[0] = v;
            else if (k == "Device2") device[1] = v;
            else if (k == "MenuButton") menu_button = v;
            else if (k == "FastForwardKey") ff_key = v;
            else if (k == "FastForwardButton") ff_button = v;
            else if (k == "TouchFastForward") touch_ff = parse_bool(v, touch_ff);
            else if (k == "SaveStateKey") state_save_key = v;
            else if (k == "LoadStateKey") state_load_key = v;
            else if (k == "StateSlotKey") state_slot_key = v;
            else if (k == "RewindKey") rewind_key = v;
            else if (k == "SaveStateButton") state_save_button = v;
            else if (k == "LoadStateButton") state_load_button = v;
            else if (k == "RewindButton") rewind_button = v;
            else if (k == "TouchRewind") touch_rewind = parse_bool(v, touch_rewind);
            else if (k == "FastForwardMode") ff_mode = (v == "Toggle" || v == "toggle") ? FastForwardMode::Toggle : FastForwardMode::Hold;
            else if (k == "FastForwardSpeed")
                ff_speed = (v == "2x" || v == "2X" || v == "2") ? FastForwardSpeed::X2
                         : (v == "4x" || v == "4X" || v == "4") ? FastForwardSpeed::X4 : FastForwardSpeed::Max;
            else if (k.rfind("Key.", 0) == 0) keys[k.substr(4)] = v;
            else if (k.rfind("Key2.", 0) == 0) keys2[k.substr(5)] = v;
            else if (k.rfind("Pad2.", 0) == 0) pads2[k.substr(5)] = v;
            else if (k.rfind("Pad.", 0) == 0) pads[k.substr(4)] = v;
        } else if (section == "Extras") {
            if (k == "Rewind") rewind = parse_bool(v, rewind);
            else if (k == "StateSlot") state_slot = std::clamp(std::atoi(v.c_str()), 1, kStateSlots);
        } else if (section == "Lobby") {
            if (k == "Shortcuts") lobby_shortcuts = parse_bool(v, lobby_shortcuts);
            else if (k == "Partner") lobby_partner = std::clamp(std::atoi(v.c_str()), -1, 7);
            else if (k == "Time") lobby_time = std::clamp(std::atoi(v.c_str()), -1, 3);
        } else if (section == "TimeAttack") {
            if (k == "Place") ta_place = std::atoi(v.c_str());
            else if (k == "Level") ta_level = std::atoi(v.c_str());
            else if (k == "Time") ta_time = std::atoi(v.c_str());
            else if (k == "Player") ta_player = std::atoi(v.c_str());
            else if (k == "Combi") ta_combi = std::atoi(v.c_str());
            else if (k == "TwoPlayers") ta_two_players = parse_bool(v, ta_two_players);
            else if (k.rfind("Best.", 0) == 0) ta_best[k.substr(5)] = std::atoi(v.c_str());
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
    std::fprintf(f, "# true: also widen the lobby and the INTRODUCTION Eggman scene, which glitch\n");
    std::fprintf(f, "WidescreenEverywhere = %s\n", wide_everywhere ? "true" : "false");
    std::fprintf(f, "# true: in widescreen levels, the HUD keeps to the left edge of the picture\n");
    std::fprintf(f, "WidescreenHud = %s\n", wide_hud ? "true" : "false");
    std::fprintf(f, "# The menu's font: Game (read from your ROM) | Pixel (the bundled pixel face)\n");
    std::fprintf(f, "MenuFont = %s\n", menu_game_font ? "Game" : "Pixel");
    std::fprintf(f, "# Behind the menu, pictures of the game's zones made from your ROM: Cycle (each in\n");
    std::fprintf(f, "# turn) | WorldEntrance | IsolatedIsland | BotanicBase | MarinaMadness | SpeedSlider |\n");
    std::fprintf(f, "# AmazingArena | TechnoTower (that one only) | Plain (no picture)\n");
    std::fprintf(f, "MenuBackground = %s\n", parse_menu_background(menu_background).c_str());
    std::fprintf(f, "# Integer | Fit | Stretch\nScaling = %s\n", scale_name(viewport.scale));
    std::fprintf(f, "# Nearest | Linear\nFilter = %s\n", linear_filter ? "Linear" : "Nearest");
    std::fprintf(f, "# Windowed | Borderless | Fullscreen\nWindowMode = %s\n", window_mode_name(window_mode));
    std::fprintf(f, "WindowScale = %d\nVSync = %s\n\n", window_scale, vsync ? "true" : "false");
    std::fprintf(f, "[Audio]\nEnabled = %s\nVolume = %d\n", audio ? "true" : "false", volume);
    std::fprintf(f, "# Music from files in Mods/Music (e.g. 05.wav), in percent of Volume\n");
    std::fprintf(f, "ModMusicVolume = %d\n", mod_music_volume);
    std::fprintf(f, "# The game's music and its sound effects, in percent of Volume\n");
    std::fprintf(f, "MusicVolume = %d\nEffectsVolume = %d\n\n", music_volume, effects_volume);
    std::fprintf(f, "[Cheats]\n# Built-in: rings, speed (also in OPTIONS > CHEATS). Codes: Action Replay codes,\n");
    std::fprintf(f, "# e.g. FFE009:63 + FFADFC:01. Awards and time attack records are not earned with any on.\n");
    std::string enabled;
    for (const auto& k : cheats) enabled += (enabled.empty() ? "" : ", ") + k;
    std::fprintf(f, "Enabled = %s\nCodes = %s\n\n", enabled.c_str(), cheat_codes.c_str());
    std::fprintf(f, "[Achievements]\n# Local achievements from assets/achievements.ini (nothing leaves this machine)\n");
    std::fprintf(f, "Enabled = %s\n\n", achievements ? "true" : "false");
    std::fprintf(f, "[Mods]\n# Write each graphic the game draws (Mega Drive tiles, 32X sprites) to Mods/Textures/Dump, for replacements\n");
    std::fprintf(f, "DumpGraphics = %s\n\n", dump_graphics ? "true" : "false");
    std::fprintf(f, "[Input]\n# Auto | On | Off\nTouchControls = %s\n", touch == TouchMode::On ? "On" : touch == TouchMode::Off ? "Off" : "Auto");
    std::fprintf(f, "SixButtonPad = %s\n", six_button ? "true" : "false");
    std::fprintf(f, "# The touch directions as a joystick instead of a d-pad\nTouchStick = %s\n", touch_stick ? "true" : "false");
    std::fprintf(f, "# Classic (every button) | SonicCD (one big jump button, as in Sonic CD's mobile version)\nTouchLayout = %s\n",
                 touch_sonic_cd ? "SonicCD" : "Classic");
    std::fprintf(f, "# How opaque the touch controls are, percent: at rest, and held\nTouchOpacity = %d\nTouchPressedOpacity = %d\n",
                 touch_opacity, touch_pressed_opacity);
    std::fprintf(f, "# Who drives each player: keyboard | pad1..padN | none\n");
    std::fprintf(f, "Device1 = %s\nDevice2 = %s\n", device[0].c_str(), device[1].c_str());
    std::fprintf(f, "# Gamepad button that opens this menu (the emulated pad uses every other one):\n");
    std::fprintf(f, "# leftstick | rightstick | guide | touchpad | misc1\n");
    std::fprintf(f, "MenuButton = %s\n", menu_button.c_str());
    std::fprintf(f, "# Fast-forward: a key, a gamepad button (as MenuButton, or lefttrigger | righttrigger | none),\n");
    std::fprintf(f, "# an on-screen button, Hold | Toggle, and 2x | 4x | Max. Never during a time attack run.\n");
    std::fprintf(f, "FastForwardKey = %s\nFastForwardButton = %s\nTouchFastForward = %s\n", ff_key.c_str(),
                 ff_button.c_str(), touch_ff ? "true" : "false");
    std::fprintf(f, "FastForwardMode = %s\nFastForwardSpeed = %s\n", fast_forward_mode_name(ff_mode),
                 fast_forward_speed_name(ff_speed));
    std::fprintf(f, "# Save states and rewind: keys for quick save, quick load, the next slot and rewind\n");
    std::fprintf(f, "# (held), gamepad buttons (as FastForwardButton), an on-screen rewind button. None of\n");
    std::fprintf(f, "# them work during a time attack run or with the menu open.\n");
    std::fprintf(f, "SaveStateKey = %s\nLoadStateKey = %s\nStateSlotKey = %s\nRewindKey = %s\n", state_save_key.c_str(),
                 state_load_key.c_str(), state_slot_key.c_str(), rewind_key.c_str());
    std::fprintf(f, "SaveStateButton = %s\nLoadStateButton = %s\nRewindButton = %s\nTouchRewind = %s\n",
                 state_save_button.c_str(), state_load_button.c_str(), rewind_button.c_str(),
                 touch_rewind ? "true" : "false");
    for (const auto& [b, k] : keys) std::fprintf(f, "Key.%s = %s\n", b.c_str(), k.c_str());
    for (const auto& [b, k] : keys2) std::fprintf(f, "Key2.%s = %s\n", b.c_str(), k.c_str());
    for (const auto& [b, k] : pads) std::fprintf(f, "Pad.%s = %s\n", b.c_str(), k.c_str());
    for (const auto& [b, k] : pads2) std::fprintf(f, "Pad2.%s = %s\n", b.c_str(), k.c_str());
    std::fprintf(f, "\n[Extras]\n# Rewind: keep the last minute of play to step back through (the rewind key)\n");
    std::fprintf(f, "Rewind = %s\n# The save state slot quick save and quick load use, 1-%d (States/ in this folder)\n",
                 rewind ? "true" : "false", kStateSlots);
    std::fprintf(f, "StateSlot = %d\n", state_slot);
    std::fprintf(f, "\n[Lobby]\n# Story mode: choose the partner and the time of day instead of the Combi Catcher and the clock.\n");
    std::fprintf(f, "# Partner: -1 the catcher's, else 0 Mighty 2 Knuckles 3 Charmy 4 Vector 5 Bomb 6 Heavy 7 Espio.\n");
    std::fprintf(f, "# Time: -1 the game's own, else 0 morning 1 day 2 sunset 3 night.\n");
    std::fprintf(f, "Shortcuts = %s\nPartner = %d\nTime = %d\n", lobby_shortcuts ? "true" : "false", lobby_partner,
                 lobby_time);
    std::fprintf(f, "\n[TimeAttack]\n# The last run set up in the front end. Values are the game's own.\n");
    std::fprintf(f, "Place = %d\nLevel = %d\nTime = %d\nPlayer = %d\nCombi = %d\nTwoPlayers = %s\n",
                 ta_place, ta_level, ta_time, ta_player, ta_combi, ta_two_players ? "true" : "false");
    std::fprintf(f, "# Best times, in level-clock frames at 60 Hz (Best.<place>.<level>)\n");
    for (const auto& [k, v] : ta_best) std::fprintf(f, "Best.%s = %d\n", k.c_str(), v);
    std::fprintf(f, "\n[Debug]\nOverlay = %s\nUseRecompiledCode = %s\n", debug_overlay ? "true" : "false", use_recompiled ? "true" : "false");
    std::fclose(f);
    return true;
}

std::string best_key(int place, int level) {
    return std::to_string(place) + "." + std::to_string(level);
}

int best_time(const Config& c, int place, int level) {
    auto it = c.ta_best.find(best_key(place, level));
    return it == c.ta_best.end() ? 0 : it->second;
}

bool record_best(Config& c, int place, int level, int frames) {
    if (frames <= 0) return false;
    const int had = best_time(c, place, level);
    if (had && had <= frames) return false;
    c.ta_best[best_key(place, level)] = frames;
    return true;
}

} // namespace chaotix
