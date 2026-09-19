#include "viewport.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace chaotix {

double aspect_value(AspectMode m, double custom, double window_aspect) {
    switch (m) {
    case AspectMode::R4_3: return 4.0 / 3.0;
    case AspectMode::R16_9: return 16.0 / 9.0;
    case AspectMode::R16_10: return 16.0 / 10.0;
    case AspectMode::R21_9: return 64.0 / 27.0;
    case AspectMode::Custom: return custom > 0.1 ? custom : 4.0 / 3.0;
    case AspectMode::Auto:
    default:
        // Auto follows the window/display shape (so ultrawide and phones work
        // without configuration).
        return window_aspect > 0.1 ? window_aspect : 4.0 / 3.0;
    }
}

ViewportResult compute_viewport(const ViewportConfig& cfg, int out_w, int out_h, int sim_w, int sim_h) {
    ViewportResult r;
    if (out_w <= 0 || out_h <= 0 || sim_w <= 0 || sim_h <= 0) return r;
    const double win_aspect = double(out_w) / double(out_h);
    const double frame_aspect = aspect_value(cfg.aspect, cfg.custom_aspect, win_aspect);

    // Fit the frame into the window.
    Rect f;
    if (cfg.scale == ScaleMode::Stretch) {
        f = {0, 0, double(out_w), double(out_h)};
    } else if (win_aspect > frame_aspect) {
        f.h = out_h;
        f.w = f.h * frame_aspect;
    } else {
        f.w = out_w;
        f.h = f.w / frame_aspect;
    }

    // Place the image inside the frame (pillarbox or letterbox; never stretch
    // the game horizontally unless Stretch is requested).
    Rect img;
    const double ia = cfg.image_aspect;
    if (cfg.scale == ScaleMode::Stretch) {
        img = f;
    } else {
        if (f.w / f.h > ia) { img.h = f.h; img.w = img.h * ia; }
        else { img.w = f.w; img.h = img.w / ia; }
        if (cfg.scale == ScaleMode::Integer) {
            // Integer multiple of the simulation height; width follows the
            // display aspect (pixels are not square on the original hardware).
            int k = int(std::floor(img.h / sim_h));
            if (k >= 1) {
                img.h = double(k * sim_h);
                img.w = std::round(img.h * ia);
            }
        }
        if (cfg.scale != ScaleMode::Stretch) {
            // Keep the frame at least as large as the image.
            if (f.w < img.w) f.w = img.w;
            if (f.h < img.h) f.h = img.h;
        }
    }
    f.x = std::floor((out_w - f.w) / 2);
    f.y = std::floor((out_h - f.h) / 2);
    img.x = std::floor(f.x + (f.w - img.w) / 2);
    img.y = std::floor(f.y + (f.h - img.h) / 2);
    r.frame = f;
    r.image = img;
    return r;
}

const char* aspect_name(AspectMode m) {
    switch (m) {
    case AspectMode::Auto: return "Auto";
    case AspectMode::R4_3: return "4:3";
    case AspectMode::R16_9: return "16:9";
    case AspectMode::R16_10: return "16:10";
    case AspectMode::R21_9: return "21:9";
    case AspectMode::Custom: return "Custom";
    }
    return "Auto";
}

bool parse_aspect(const std::string& s, AspectMode& mode, double& custom) {
    if (s == "Auto" || s == "auto") { mode = AspectMode::Auto; return true; }
    if (s == "4:3") { mode = AspectMode::R4_3; return true; }
    if (s == "16:9") { mode = AspectMode::R16_9; return true; }
    if (s == "16:10") { mode = AspectMode::R16_10; return true; }
    if (s == "21:9") { mode = AspectMode::R21_9; return true; }
    // Arbitrary "W:H" or a decimal ratio.
    size_t colon = s.find(':');
    if (colon != std::string::npos) {
        double w = std::atof(s.substr(0, colon).c_str()), h = std::atof(s.substr(colon + 1).c_str());
        if (w > 0 && h > 0) { mode = AspectMode::Custom; custom = w / h; return true; }
        return false;
    }
    double v = std::atof(s.c_str());
    if (v > 0.1) { mode = AspectMode::Custom; custom = v; return true; }
    return false;
}

const char* scale_name(ScaleMode m) {
    switch (m) {
    case ScaleMode::Integer: return "Integer";
    case ScaleMode::Fit: return "Fit";
    case ScaleMode::Stretch: return "Stretch";
    }
    return "Fit";
}

bool parse_scale(const std::string& s, ScaleMode& m) {
    if (s == "Integer" || s == "integer") { m = ScaleMode::Integer; return true; }
    if (s == "Fit" || s == "fit") { m = ScaleMode::Fit; return true; }
    if (s == "Stretch" || s == "stretch") { m = ScaleMode::Stretch; return true; }
    return false;
}

} // namespace chaotix
