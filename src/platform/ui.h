#pragma once

// A very small drawing layer for the program's own screens (first-run setup,
// achievements). It is deliberately not a widget toolkit: it draws text,
// panels and pills, and the screens do their own layout and hit testing.
//
// The font is compiled into the executable, so these screens work before any
// asset directory has been located -- which matters, because the setup screen
// is the first thing a new user sees.

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <utility>

struct TTF_Font;
struct TTF_Text;
struct TTF_TextEngine;

namespace chaotix {
struct GameFont;
}

namespace chaotix::ui {

struct Color {
    uint8_t r = 255, g = 255, b = 255, a = 255;
    constexpr Color alpha(uint8_t v) const { return {r, g, b, v}; }
};

// One dark theme. These are the only colours the screens use; anything that
// needs a new colour should get a name here rather than a literal.
namespace theme {
constexpr Color background{15, 17, 26};
constexpr Color surface{26, 29, 43};
constexpr Color surface_raised{35, 40, 58};
constexpr Color outline{51, 57, 79};
constexpr Color outline_strong{78, 88, 120};
constexpr Color accent{96, 154, 255};
constexpr Color accent_dim{38, 62, 116};
constexpr Color text{232, 236, 246};
constexpr Color text_dim{143, 152, 176};
constexpr Color text_faint{102, 110, 134};
constexpr Color good{110, 216, 152};
constexpr Color warn{240, 190, 96};
constexpr Color bad{238, 120, 120};
// The value chosen in a row of choices (TIME ATTACK, LEVEL SELECT): the
// yellow of the game's own menu letters.
constexpr Color highlight{255, 222, 66};
} // namespace theme

enum class Font {
    Small,      // captions, paths
    Body,       // list rows, buttons
    Subtitle,   // section headings
    Title,      // the one heading at the top of a page
    Pixel,      // in-game menu labels
    PixelBig,   // in-game menu headings
    Count_
};

// True for the fonts drawn from the bitmap-style face, whose sizes are
// snapped to whole pixels so the glyphs stay sharp. These are also the ones
// the game's own font stands in for (set_game_font).
constexpr bool is_pixel(Font f) { return f == Font::Pixel || f == Font::PixelBig; }

// How many screen pixels a pixel of the game's 8x16 font becomes for a pixel
// face of this size: a whole number, so its pixels stay square and sharp,
// and the glyphs about as tall as the pixel face's line.
inline int game_font_scale(float pixel_size) {
    const int k = int(pixel_size / 16.0f + 0.5f);
    return k < 1 ? 1 : k;
}

// MENU SIZE (issue #49): how much larger than NORMAL everything is laid out
// at each size. The pixel faces cannot follow a factor exactly -- they come in
// steps of their 8-pixel grid, and the game's font in whole multiples -- so
// each size also takes them at least one step past the size before it (see
// pixel_steps); otherwise LARGE and EXTRA LARGE could round to the same
// letters.
inline constexpr int kMenuSizes = 3;  // NORMAL, LARGE, EXTRA LARGE
inline constexpr float kMenuZoom[kMenuSizes] = {1.0f, 1.5f, 2.0f};

struct PixelSize {
    float face = 8;  // the pixel face's size, a multiple of 8
    int game = 1;    // screen pixels per pixel of the game's font
};
// A pixel face of design size `design` (16 or 32) at layout scale `scale`
// (Ui::scale() at NORMAL) and menu size `zoom`: at NORMAL the scaled size
// snapped to the grid, as it always was; at each size above, at least one
// grid step and one whole game-font scale more than at the size below, with
// the face kept about as tall as the game's letters.
PixelSize pixel_steps(float design, float scale, int zoom);

class Ui {
public:
    ~Ui() { shutdown(); }

    // Returns false if SDL_ttf or the font could not be initialised; the
    // caller is then expected to fall back to SDL's debug text.
    bool init(SDL_Renderer* renderer);
    void shutdown();
    bool ready() const { return engine_ != nullptr; }

    // Re-reads the output size and rescales the fonts if it changed.
    void begin_frame();
    void end_frame();

    float width() const { return width_; }
    float height() const { return height_; }
    // Everything is laid out in these units: 1 at a 720-pixel-tall window,
    // more on a larger one, so that a layout written once suits a phone, a
    // window and a 4K screen.
    float scale() const { return scale_; }
    float px(float units) const { return units * scale_; }
    // MENU SIZE (issue #49): 0 NORMAL, 1 LARGE, 2 EXTRA LARGE. Everything is
    // laid out kMenuZoom times larger, the pixel faces as pixel_steps says.
    // It takes effect at the next begin_frame; the app sets it while its menu
    // is open and puts NORMAL back for everything else.
    void set_zoom(int zoom) { zoom_ = zoom < 0 ? 0 : zoom >= kMenuSizes ? kMenuSizes - 1 : zoom; }
    int zoom() const { return zoom_; }

    float line_height(Font f) const;
    float text_width(Font f, const std::string& s);
    // Returns the advance width actually drawn.
    float text(Font f, float x, float y, const std::string& s, Color c);
    // Draws s, shortened with an ellipsis if it does not fit in max_w. A path
    // is shortened in the middle, because its ends identify it.
    float text_fit(Font f, float x, float y, float max_w, const std::string& s, Color c,
                   bool is_path = false);

    // The game's own menu font (renderer/game_font.h, issue #8). Once set,
    // and while enabled, the pixel fonts draw any string it has every
    // character of with it instead; other strings keep the pixel face. The
    // font is copied into a texture, so `font` need not outlive the call.
    // Returns false if the texture could not be made.
    bool set_game_font(const GameFont& font);
    void enable_game_font(bool on) { game_on_ = on; }
    bool game_font_enabled() const { return game_on_; }
    bool game_font_active() const { return game_on_ && game_tex_ != nullptr; }
    // Whether s, in f, is drawn with the game's font right now.
    bool uses_game_font(Font f, const std::string& s) const;
    // The game's letters normally keep their own colours, the colour asked
    // for setting only how bright they are. While tinted they are drawn in
    // that colour instead (multiplied: white digits come out in it, yellow
    // letters stay yellow) -- for a choice marked in yellow, say.
    void tint_game_font(bool on) { game_tint_ = on; }

    // Draws a texture over r, cropped to r's shape rather than stretched, so
    // a picture fills a window of any aspect.
    void image_cover(SDL_Texture* tex, float tex_w, float tex_h, const SDL_FRect& r, uint8_t alpha = 255);
    // Draws src of a texture over dst: smoothed, or with square pixels (for a
    // whole-number scale of pixel art), and mirrored if asked.
    void image(SDL_Texture* tex, const SDL_FRect& src, const SDL_FRect& dst, bool smooth, bool flip_h = false,
               uint8_t alpha = 255);
    // Limits drawing to r until clip(nullptr).
    void clip(const SDL_FRect* r);

    void rect(const SDL_FRect& r, Color c, float radius = 0);
    void outline(const SDL_FRect& r, Color c, float radius = 0, float thickness = 1);
    void panel(const SDL_FRect& r, Color fill, Color border, float radius, float thickness = 1);
    // A filled convex polygon, points in order around it.
    void poly(const SDL_FPoint* pts, int n, Color c);
    // A filled ellipse, and an elliptical band `thickness` wide inside its
    // edge (a ring seen edge-on as it turns is one of these).
    void ellipse(float cx, float cy, float rx, float ry, Color c);
    void ellipse_band(float cx, float cy, float rx, float ry, float thickness, Color c);
    // A rounded label, e.g. "VERIFIED". Returns the rectangle it occupied.
    SDL_FRect pill(float x, float y, const std::string& label, Color fg, Color bg);

private:
    struct CacheKey {
        int font;
        std::string text;
        bool operator<(const CacheKey& o) const {
            return font != o.font ? font < o.font : text < o.text;
        }
    };
    struct CacheEntry {
        TTF_Text* text = nullptr;
        uint64_t used = 0;
    };

    TTF_Text* acquire(Font f, const std::string& s);
    std::string shorten(Font f, const std::string& s, float max_w, bool is_path);
    void set_sizes();
    void drop_cache();
    // The game font's pixel size for f (game_font_scale of its pixel face).
    int game_scale(Font f) const { return game_scales_[size_t(f)]; }
    float game_text(Font f, float x, float y, const std::string& s, Color c);

    SDL_Texture* game_tex_ = nullptr;
    std::array<int16_t, 128> game_glyph_{};
    bool game_on_ = false;
    bool game_tint_ = false;
    float pixel_sizes_[size_t(Font::Count_)] = {};
    int game_scales_[size_t(Font::Count_)] = {1, 1, 1, 1, 1, 2};

    SDL_Renderer* renderer_ = nullptr;
    TTF_TextEngine* engine_ = nullptr;
    TTF_Font* fonts_[size_t(Font::Count_)] = {};
    std::map<CacheKey, CacheEntry> cache_;
    uint64_t frame_ = 0;
    float width_ = 0, height_ = 0, scale_ = 1;
    float base_scale_ = 1;  // scale_ at NORMAL
    int zoom_ = 0;          // asked for
    int sized_zoom_ = 0;    // what the fonts were last sized for
};

} // namespace chaotix::ui
