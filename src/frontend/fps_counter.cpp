// See fps_counter.h.
#include "frontend/fps_counter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace chaotix::fps_counter {

namespace {

// A number in exactly `width` characters: one decimal while it fits, none
// above that, and all nines past the largest that fits.
std::string number(double v, int width) {
    if (!(v >= 0)) v = 0;  // also NaN
    char b[32];
    std::snprintf(b, sizeof b, "%*.1f", width, v);
    if (int(std::string(b).size()) > width) std::snprintf(b, sizeof b, "%*.0f", width, v);
    if (int(std::string(b).size()) > width) return std::string(size_t(width), '9');
    return b;
}

} // namespace

std::vector<std::string> lines(const Stats& s) {
    std::vector<std::string> out;
    // "FPS  60.0  EMU  60.0" and "MS   4.2  MAX   9.8": 20 characters each.
    out.push_back("FPS " + number(s.presented_fps, 5) + "  EMU " + number(s.emulated_fps, 5));
    out.push_back("MS  " + number(s.emu_avg_ms, 5) + "  MAX " + number(s.emu_worst_ms, 5));
    // "SKIPPED  20.0": pictures not drawn per second; EMU counts their frames.
    if (s.frame_skip) out.push_back("SKIPPED  " + number(s.skipped_fps, 5));
    if (s.interpreter) out.push_back("INTERPRETER");
    return out;
}

Box size(int line_count, int scale) {
    const float s = float(std::max(scale, 1));
    return {0, 0, float(kColumns * kCell + 2 * kPad) * s,
            float(std::max(line_count, 1) * kLineHeight - (kLineHeight - kCell) + 2 * kPad) * s};
}

int scale_for(int window_w, int window_h, int menu_zoom) {
    static constexpr float kZoom[3] = {1.0f, 1.5f, 2.0f};  // as ui::kMenuZoom
    const float z = kZoom[std::clamp(menu_zoom, 0, 2)];
    const int side = std::max(std::min(window_w, window_h), 0);
    int s = std::max(1, int(std::lround(float(side) / 480.0f * z)));
    // The box and its margins (4 units each side) inside the window.
    while (s > 1 && size(1, s).w + float(8 * s) > float(window_w)) --s;
    return s;
}

Box place(float window_w, float window_h, int line_count, int scale, const std::vector<Box>& avoid) {
    Box b = size(line_count, scale);
    const float margin = float(4 * std::max(scale, 1));
    b.x = std::max(0.0f, window_w - margin - b.w);
    b.y = std::min(margin, std::max(0.0f, window_h - b.h));
    // Below whatever it would cover, each in turn (they may be stacked).
    for (size_t guard = 0; guard <= avoid.size(); ++guard) {
        bool moved = false;
        for (const Box& a : avoid)
            if (a.w > 0 && a.h > 0 && overlaps(b, a)) {
                b.y = a.y + a.h + margin;
                moved = true;
            }
        if (!moved) break;
    }
    // On whole multiples of the scale, so the glyphs' pixels stay square.
    const float s = float(std::max(scale, 1));
    b.x = std::floor(b.x / s) * s;
    b.y = std::ceil(b.y / s) * s;
    if (b.y + b.h > window_h) b.y = std::floor((window_h - b.h) / s) * s;
    b.x = std::max(0.0f, b.x);
    b.y = std::max(0.0f, b.y);
    return b;
}

} // namespace chaotix::fps_counter
