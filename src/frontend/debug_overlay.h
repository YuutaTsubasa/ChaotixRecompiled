// Developer overlay: builds text lines describing the machine and timing.
// The platform layer only draws the strings.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace chaotix {

class Machine;

struct FrameTiming {
    double render_fps = 0;        // presented frames per second
    double sim_fps = 0;           // emulated frames per second
    double emu_ms = 0;            // time spent running the machine per frame
    double present_ms = 0;        // time spent rendering/presenting
    unsigned refresh_hz = 0;      // display refresh rate (0 = unknown)
    bool fast_forward = false;
};

// page 0: summary; page 1: 68K registers; page 2: SH-2 registers; page 3: memory watch
std::vector<std::string> build_debug_overlay(const Machine& m, const FrameTiming& t, int page, const std::string& video_line,
                                             const std::vector<uint32_t>& watches);

} // namespace chaotix
