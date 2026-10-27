// SHOW FPS (issue #54): a small counter in a corner of the window with the
// frames presented and emulated per second, how long emulating a frame took
// (average and worst over the last second) and whether the game runs on the
// interpreter instead of the recompiled code.
//
// Laid out here, without SDL, in the 8x8 cells of SDL's debug font: the
// window code draws the lines at a whole-number scale into the box placed
// here, on the host's UI layer only, so the machine, its picture and
// captures of the game's frames never see it.
#pragma once
#include <string>
#include <vector>

namespace chaotix::fps_counter {

struct Stats {
    double presented_fps = 0;  // frames shown per second
    double emulated_fps = 0;   // machine frames run per second (60 at full speed)
    double emu_avg_ms = 0;     // average emulation time per frame over the last second
    double emu_worst_ms = 0;   // worst pass's emulation time per frame over the last second
    // FRAME SKIP (issue #62): on (not OFF), and the pictures it left out per
    // second; shown on a line of their own while it is on.
    bool frame_skip = false;
    double skipped_fps = 0;
    bool interpreter = false;  // the reference interpreters, not the recompiled code
};

inline constexpr int kColumns = 20;     // characters in the widest line
inline constexpr int kCell = 8;         // the debug font's glyph, in units
inline constexpr int kLineHeight = 10;  // a line, in units
inline constexpr int kPad = 3;          // around the text, in units

// The lines shown: always the same width (numbers padded), so the box does
// not jitter as they change. With FRAME SKIP on, a line with the pictures it
// left out per second; a last line marks the interpreter.
std::vector<std::string> lines(const Stats& s);

// Screen pixels per unit in a window_w x window_h window at MENU SIZE
// `menu_zoom` (0 NORMAL, 1 LARGE, 2 EXTRA LARGE): a whole number, at least 1,
// growing with the window's shorter side and the menu size, so the counter is
// as readable as the menu on a phone, and never wider than the window allows.
int scale_for(int window_w, int window_h, int menu_zoom);

struct Box {
    float x = 0, y = 0, w = 0, h = 0;
};
inline bool overlaps(const Box& a, const Box& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

// The counter's size for `line_count` lines at `scale`.
Box size(int line_count, int scale);

// Where the counter goes in a window_w x window_h window: the top right
// corner (the HUD is top left, the touch controls are at the bottom and the
// fast-forward and rewind buttons top left), moved down below anything
// listed in `avoid` (the award counter, a speedrun timer in that corner) that
// it would cover. Kept inside the window.
Box place(float window_w, float window_h, int line_count, int scale, const std::vector<Box>& avoid);

} // namespace chaotix::fps_counter
