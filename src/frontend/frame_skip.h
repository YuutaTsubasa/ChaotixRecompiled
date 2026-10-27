// Frame skip (issue #62): which emulated frames have their picture drawn.
// Pure, so the policy is testable without SDL.
//
// Skipping a frame only leaves out its picture (Machine::skip_picture): the
// game runs every frame exactly as it would have, so play, recordings,
// netplay and lockstep are the same. What is saved is the drawing: the Mega
// Drive and 32X lines, their merge and the widescreen margins.
//
// The main loop runs one frame per elapsed frame period and shows the last
// picture of each pass (fast_forward.h, plan_pass). A device that cannot
// keep up runs several frames in a pass, and the pictures of all but the
// last are drawn and never shown. AUTO leaves out exactly those, so it never
// changes what is on the screen; the time saved lets such a pass catch up,
// up to plan_pass's three frames, so at most two pictures in a row are
// skipped. With headroom every pass runs one frame and every frame is drawn.
// 1 and 2 also draw at most one frame in every two or three, steadily,
// whether or not the device keeps up.
#pragma once
#include <string>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace chaotix {

enum class FrameSkip { Off, Auto, One, Two };
inline constexpr int kFrameSkipModes = 4;

// AUTO on phones and tablets, where a slow device falls behind (issue #54);
// OFF elsewhere, where the emulation takes a third of a frame or less and
// AUTO would skip nothing anyway.
inline FrameSkip frame_skip_default() {
#if defined(__ANDROID__) || (defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    return FrameSkip::Auto;
#else
    return FrameSkip::Off;
#endif
}

// [Video] FrameSkip: Off | Auto | 1 | 2; anything unknown is the default.
inline const char* frame_skip_name(FrameSkip s) {
    switch (s) {
    case FrameSkip::Auto: return "Auto";
    case FrameSkip::One: return "1";
    case FrameSkip::Two: return "2";
    default: return "Off";
    }
}
inline FrameSkip parse_frame_skip(const std::string& v) {
    std::string low;
    for (char ch : v) low += char(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch);
    if (low == "off" || low == "0" || low == "false") return FrameSkip::Off;
    if (low == "auto" || low == "true") return FrameSkip::Auto;
    if (low == "1") return FrameSkip::One;
    if (low == "2") return FrameSkip::Two;
    return frame_skip_default();
}

// The frames left out after each one drawn, at most: 1 and 2 by name;
// AUTO by plan_pass's catch-up (three frames a pass).
inline int frame_skip_fixed(FrameSkip s) { return s == FrameSkip::One ? 1 : s == FrameSkip::Two ? 2 : 0; }

struct FrameSkipper {
    int skipped = 0;  // pictures left out since the last one drawn

    // Whether frame `index` (from 0) of a pass of `count` frames is drawn.
    // Only the last frame of a pass can be shown; with 1 or 2 it is drawn
    // only once that many before it were left out.
    bool draw(FrameSkip mode, int index, int count) {
        bool d = true;
        if (mode != FrameSkip::Off) d = index >= count - 1 && skipped >= frame_skip_fixed(mode);
        skipped = d ? 0 : skipped + 1;
        return d;
    }
};

} // namespace chaotix
