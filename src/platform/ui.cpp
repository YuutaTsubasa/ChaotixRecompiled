#include "ui.h"

#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "runtime/log.h"

namespace chaotix {

// Compiled in by cmake/embed_file.cmake (see CMakeLists.txt).
extern const unsigned char font_regular[];
extern const size_t font_regular_len;
extern const unsigned char font_semibold[];
extern const size_t font_semibold_len;

namespace ui {

namespace {

// Point sizes at scale 1, i.e. in a 720-pixel-tall window.
constexpr float kSizes[size_t(Font::Count_)] = {13.0f, 16.0f, 20.0f, 30.0f};

// Text that has not been drawn for this many frames is released. The screens
// redraw the same strings every frame, so anything still in use is refreshed
// long before it expires.
constexpr uint64_t kCacheTtlFrames = 240;

SDL_FColor to_fcolor(Color c) {
    return {float(c.r) / 255.0f, float(c.g) / 255.0f, float(c.b) / 255.0f, float(c.a) / 255.0f};
}

// How many segments each quarter turn is drawn with. Taken from the outer
// radius so that an outline's inner and outer loops have matching point
// counts and can be stitched together.
int arc_steps(float radius) {
    return std::clamp(int(radius * 0.7f) + 3, 4, 16);
}

// The outline of a rounded rectangle as a closed loop, walking clockwise on
// screen from the top edge. Angles use the usual convention (0 = +x,
// 90 = up), and y grows downwards, so sin is subtracted.
std::vector<SDL_FPoint> rounded_outline(const SDL_FRect& r, float radius, int steps) {
    radius = std::clamp(radius, 0.0f, std::min(r.w, r.h) * 0.5f);
    std::vector<SDL_FPoint> pts;
    pts.reserve(size_t(steps + 1) * 4);
    // Corner centres, and the angle each quarter turn starts at; every turn
    // sweeps 90 degrees clockwise (i.e. decreasing).
    const float cx[4] = {r.x + r.w - radius, r.x + r.w - radius, r.x + radius, r.x + radius};
    const float cy[4] = {r.y + radius, r.y + r.h - radius, r.y + r.h - radius, r.y + radius};
    const float first[4] = {SDL_PI_F / 2, 0.0f, -SDL_PI_F / 2, -SDL_PI_F};
    for (int corner = 0; corner < 4; ++corner) {
        for (int i = 0; i <= steps; ++i) {
            const float a = first[corner] - (float(i) / float(steps)) * (SDL_PI_F / 2);
            pts.push_back({cx[corner] + std::cos(a) * radius, cy[corner] - std::sin(a) * radius});
        }
    }
    return pts;
}

} // namespace

bool Ui::init(SDL_Renderer* renderer) {
    renderer_ = renderer;
    if (!TTF_Init()) {
        LOGW("ui", "TTF_Init: %s", SDL_GetError());
        return false;
    }
    engine_ = TTF_CreateRendererTextEngine(renderer);
    if (!engine_) {
        LOGW("ui", "TTF_CreateRendererTextEngine: %s", SDL_GetError());
        TTF_Quit();
        return false;
    }
    for (size_t i = 0; i < size_t(Font::Count_); ++i) {
        const bool bold = i >= size_t(Font::Subtitle);
        const unsigned char* data = bold ? font_semibold : font_regular;
        const size_t len = bold ? font_semibold_len : font_regular_len;
        // The stream must stay open for the lifetime of the font; the data is
        // static, so SDL_ttf can keep reading it.
        SDL_IOStream* io = SDL_IOFromConstMem(data, len);
        if (!io) break;
        fonts_[i] = TTF_OpenFontIO(io, true, kSizes[i]);
        if (!fonts_[i]) {
            LOGW("ui", "TTF_OpenFontIO: %s", SDL_GetError());
            break;
        }
        TTF_SetFontHinting(fonts_[i], TTF_HINTING_LIGHT);
    }
    for (size_t i = 0; i < size_t(Font::Count_); ++i) {
        if (!fonts_[i]) { shutdown(); return false; }
    }
    return true;
}

void Ui::shutdown() {
    drop_cache();
    for (auto*& f : fonts_) {
        if (f) TTF_CloseFont(f);
        f = nullptr;
    }
    if (engine_) {
        TTF_DestroyRendererTextEngine(engine_);
        engine_ = nullptr;
        TTF_Quit();
    }
    renderer_ = nullptr;
}

void Ui::drop_cache() {
    for (auto& [key, entry] : cache_) TTF_DestroyText(entry.text);
    cache_.clear();
}

void Ui::set_sizes() {
    for (size_t i = 0; i < size_t(Font::Count_); ++i) {
        if (fonts_[i]) TTF_SetFontSize(fonts_[i], kSizes[i] * scale_);
    }
    drop_cache();  // the cached textures were rendered at the old size
}

void Ui::begin_frame() {
    ++frame_;
    int ow = 0, oh = 0;
    SDL_GetRenderOutputSize(renderer_, &ow, &oh);
    width_ = float(ow);
    height_ = float(oh);
    // Scale from the height, but do not let a very wide, short window (or a
    // phone in landscape) shrink the text below legibility.
    const float want = std::clamp(std::max(float(oh), float(ow) * 0.42f) / 720.0f, 0.62f, 3.0f);
    if (std::fabs(want - scale_) > 0.01f) {
        scale_ = want;
        set_sizes();
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
}

void Ui::end_frame() {
    for (auto it = cache_.begin(); it != cache_.end();) {
        if (frame_ - it->second.used > kCacheTtlFrames) {
            TTF_DestroyText(it->second.text);
            it = cache_.erase(it);
        } else {
            ++it;
        }
    }
}

TTF_Text* Ui::acquire(Font f, const std::string& s) {
    if (!engine_ || s.empty()) return nullptr;
    CacheKey key{int(f), s};
    auto it = cache_.find(key);
    if (it == cache_.end()) {
        TTF_Text* t = TTF_CreateText(engine_, fonts_[size_t(f)], s.c_str(), s.size());
        if (!t) return nullptr;
        it = cache_.emplace(std::move(key), CacheEntry{t, frame_}).first;
    }
    it->second.used = frame_;
    return it->second.text;
}

float Ui::line_height(Font f) const {
    return fonts_[size_t(f)] ? float(TTF_GetFontHeight(fonts_[size_t(f)])) : 0.0f;
}

float Ui::text_width(Font f, const std::string& s) {
    // Measured without creating a cached TTF_Text: shorten() below asks for
    // the width of every intermediate string, and caching those would fill
    // the cache with strings that are never drawn.
    if (!fonts_[size_t(f)] || s.empty()) return 0;
    int w = 0, h = 0;
    if (!TTF_GetStringSize(fonts_[size_t(f)], s.c_str(), s.size(), &w, &h)) return 0;
    return float(w);
}

float Ui::text(Font f, float x, float y, const std::string& s, Color c) {
    TTF_Text* t = acquire(f, s);
    if (!t) return 0;
    TTF_SetTextColor(t, c.r, c.g, c.b, c.a);
    TTF_DrawRendererText(t, x, y);
    int w = 0, h = 0;
    TTF_GetTextSize(t, &w, &h);
    return float(w);
}

std::string Ui::shorten(Font f, const std::string& s, float max_w, bool is_path) {
    if (max_w <= 0 || text_width(f, s) <= max_w) return s;
    const std::string dots = "...";
    // Every index a cut may fall on: cutting inside a multi-byte character
    // would produce invalid UTF-8 (continuation bytes are 10xxxxxx). Binary
    // searching over these beats measuring once per character, and this runs
    // for every string on screen, every frame.
    std::vector<size_t> at;
    at.reserve(s.size() + 1);
    for (size_t i = 0; i <= s.size(); ++i)
        if (i == s.size() || (uint8_t(s[i]) & 0xC0) != 0x80) at.push_back(i);

    if (!is_path) {
        // The longest prefix that still fits with an ellipsis after it.
        size_t lo = 0, hi = at.size() - 1;  // index 0 always "fits": just "..."
        while (lo < hi) {
            const size_t mid = lo + (hi - lo + 1) / 2;
            if (text_width(f, s.substr(0, at[mid]) + dots) <= max_w) lo = mid; else hi = mid - 1;
        }
        return s.substr(0, at[lo]) + dots;
    }
    // Keep the end of a path: two files usually differ in the folder just
    // above them and in the file name, and dropping the head keeps both.
    size_t lo = 0, hi = at.size() - 1;      // the last index leaves only "..."
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (text_width(f, dots + s.substr(at[mid])) <= max_w) hi = mid; else lo = mid + 1;
    }
    return dots + s.substr(at[lo]);
}

float Ui::text_fit(Font f, float x, float y, float max_w, const std::string& s, Color c,
                   bool is_path) {
    return text(f, x, y, shorten(f, s, max_w, is_path), c);
}

void Ui::rect(const SDL_FRect& r, Color c, float radius) {
    if (r.w <= 0 || r.h <= 0) return;
    if (radius < 1.0f) {
        SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
        SDL_RenderFillRect(renderer_, &r);
        return;
    }
    const std::vector<SDL_FPoint> pts = rounded_outline(r, radius, arc_steps(radius));
    const SDL_FColor col = to_fcolor(c);
    std::vector<SDL_Vertex> verts;
    verts.reserve(pts.size() + 1);
    verts.push_back({{r.x + r.w * 0.5f, r.y + r.h * 0.5f}, col, {0, 0}});
    for (const SDL_FPoint& p : pts) verts.push_back({p, col, {0, 0}});
    std::vector<int> idx;
    idx.reserve(pts.size() * 3);
    for (size_t i = 1; i < verts.size(); ++i) {
        idx.push_back(0);
        idx.push_back(int(i));
        idx.push_back(int(i % pts.size() + 1));
    }
    SDL_RenderGeometry(renderer_, nullptr, verts.data(), int(verts.size()), idx.data(),
                       int(idx.size()));
}

void Ui::outline(const SDL_FRect& r, Color c, float radius, float thickness) {
    if (r.w <= 0 || r.h <= 0 || thickness <= 0) return;
    const SDL_FRect inner{r.x + thickness, r.y + thickness, r.w - thickness * 2,
                          r.h - thickness * 2};
    if (inner.w <= 0 || inner.h <= 0) { rect(r, c, radius); return; }
    const int steps = arc_steps(radius);
    const std::vector<SDL_FPoint> out = rounded_outline(r, radius, steps);
    const std::vector<SDL_FPoint> in =
        rounded_outline(inner, std::max(0.0f, radius - thickness), steps);
    const SDL_FColor col = to_fcolor(c);
    std::vector<SDL_Vertex> verts;
    verts.reserve(out.size() * 2);
    for (size_t i = 0; i < out.size(); ++i) {
        verts.push_back({out[i], col, {0, 0}});
        verts.push_back({in[i], col, {0, 0}});
    }
    std::vector<int> idx;
    idx.reserve(out.size() * 6);
    for (size_t i = 0; i < out.size(); ++i) {
        const int a = int(i * 2), b = int(i * 2 + 1);
        const int c2 = int(((i + 1) % out.size()) * 2), d = c2 + 1;
        idx.push_back(a); idx.push_back(b); idx.push_back(c2);
        idx.push_back(b); idx.push_back(d); idx.push_back(c2);
    }
    SDL_RenderGeometry(renderer_, nullptr, verts.data(), int(verts.size()), idx.data(),
                       int(idx.size()));
}

void Ui::panel(const SDL_FRect& r, Color fill, Color border, float radius, float thickness) {
    rect(r, fill, radius);
    outline(r, border, radius, thickness);
}

SDL_FRect Ui::pill(float x, float y, const std::string& label, Color fg, Color bg) {
    const float pad = px(7);
    const float w = text_width(Font::Small, label) + pad * 2;
    const float h = line_height(Font::Small) + px(3);
    const SDL_FRect r{x, y, w, h};
    rect(r, bg, h * 0.5f);
    text(Font::Small, x + pad, y + px(1.5f), label, fg);
    return r;
}

} // namespace ui
} // namespace chaotix
