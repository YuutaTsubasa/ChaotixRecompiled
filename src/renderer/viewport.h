// Render viewport computation (pure logic, no platform dependencies).
//
// Terminology:
//  * simulation viewport: what the game renders (e.g. 320x224). Gameplay
//    timing and logic never depend on the output size.
//  * frame: the rectangle, in output pixels, that represents the configured
//    display aspect ratio (4:3, 16:9, ...). It is fitted into the window.
//  * image: where the game image is drawn inside the frame. Until a scene is
//    rendered in true widescreen, a 4:3 image is centred (pillarboxed) inside
//    a wider frame instead of being stretched.
#pragma once
#include <string>

namespace chaotix {

enum class AspectMode { Auto, R4_3, R16_9, R16_10, R21_9, Custom };
enum class ScaleMode { Integer, Fit, Stretch };

struct ViewportConfig {
    AspectMode aspect = AspectMode::Auto;
    double custom_aspect = 4.0 / 3.0;      // used when aspect == Custom
    ScaleMode scale = ScaleMode::Fit;
    // Display aspect of the simulated image (the original 320x224 image is
    // shown at 4:3 on a CRT). Widescreen scenes report a wider value.
    double image_aspect = 4.0 / 3.0;
};

struct Rect {
    double x = 0, y = 0, w = 0, h = 0;
};

struct ViewportResult {
    Rect frame;  // area representing the configured display aspect
    Rect image;  // where the game image is drawn
};

double aspect_value(AspectMode m, double custom, double window_aspect);
// Widescreen margin (columns per side of the native 320 px image) that fills
// a frame of the given display aspect, capped at max_extra.
int widescreen_extra(double frame_aspect, int max_extra);
// Display aspect of a rendered image `width` pixels wide whose native part
// (shown at 4:3 on the original hardware) is `native_width` pixels.
inline double image_aspect_for_width(int width, int native_width) {
    return native_width > 0 ? (4.0 / 3.0) * double(width) / double(native_width) : 4.0 / 3.0;
}
ViewportResult compute_viewport(const ViewportConfig& cfg, int out_w, int out_h, int sim_w, int sim_h);

const char* aspect_name(AspectMode m);
bool parse_aspect(const std::string& s, AspectMode& mode, double& custom);
const char* scale_name(ScaleMode m);
bool parse_scale(const std::string& s, ScaleMode& m);

} // namespace chaotix
