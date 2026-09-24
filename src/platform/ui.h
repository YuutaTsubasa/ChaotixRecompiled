#pragma once

// A very small drawing layer for the program's own screens (first-run setup,
// achievements). It is deliberately not a widget toolkit: it draws text,
// panels and pills, and the screens do their own layout and hit testing.
//
// The font is compiled into the executable, so these screens work before any
// asset directory has been located -- which matters, because the setup screen
// is the first thing a new user sees.

#include <SDL3/SDL.h>

#include <cstdint>
#include <map>
#include <string>
#include <utility>

struct TTF_Font;
struct TTF_Text;
struct TTF_TextEngine;

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
} // namespace theme

enum class Font {
    Small,     // captions, paths
    Body,      // list rows, buttons
    Subtitle,  // section headings
    Title,     // the one heading at the top of a page
    Count_
};

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

    float line_height(Font f) const;
    float text_width(Font f, const std::string& s);
    // Returns the advance width actually drawn.
    float text(Font f, float x, float y, const std::string& s, Color c);
    // Draws s, shortened with an ellipsis if it does not fit in max_w. A path
    // is shortened in the middle, because its ends identify it.
    float text_fit(Font f, float x, float y, float max_w, const std::string& s, Color c,
                   bool is_path = false);

    void rect(const SDL_FRect& r, Color c, float radius = 0);
    void outline(const SDL_FRect& r, Color c, float radius = 0, float thickness = 1);
    void panel(const SDL_FRect& r, Color fill, Color border, float radius, float thickness = 1);
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

    SDL_Renderer* renderer_ = nullptr;
    TTF_TextEngine* engine_ = nullptr;
    TTF_Font* fonts_[size_t(Font::Count_)] = {};
    std::map<CacheKey, CacheEntry> cache_;
    uint64_t frame_ = 0;
    float width_ = 0, height_ = 0, scale_ = 1;
};

} // namespace chaotix::ui
