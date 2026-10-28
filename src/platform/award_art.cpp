#include "award_art.h"

#include <algorithm>
#include <cmath>

namespace chaotix::award_art {

namespace {

// Not earned: the same picture in greys, by how bright each colour is.
ui::Color grey(ui::Color c, bool earned) {
    if (earned) return c;
    const int l = (int(c.r) * 3 + int(c.g) * 6 + int(c.b)) / 10;
    const int v = 46 + l * 70 / 255;
    return {uint8_t(v), uint8_t(v - 4), uint8_t(std::min(255, v + 22)), c.a};
}

void parallelogram(ui::Ui& g, float x, float y, float w, float h, float lean, ui::Color c) {
    const SDL_FPoint p[4] = {{x + lean, y}, {x + w, y}, {x + w - lean, y + h}, {x, y + h}};
    g.poly(p, 4, c);
}

// A star of `points` points: outer radius r, inner radius r * pinch.
void star(ui::Ui& g, float cx, float cy, float r, float pinch, int points, float turn, ui::Color c) {
    // A star is not convex, so it is drawn as triangles from the centre.
    for (int i = 0; i < points * 2; ++i) {
        const float a0 = turn + float(i) * SDL_PI_F / float(points) - SDL_PI_F / 2;
        const float a1 = turn + float(i + 1) * SDL_PI_F / float(points) - SDL_PI_F / 2;
        const float r0 = i % 2 ? r * pinch : r, r1 = i % 2 ? r : r * pinch;
        const SDL_FPoint t[3] = {{cx, cy}, {cx + std::cos(a0) * r0, cy + std::sin(a0) * r0},
                                 {cx + std::cos(a1) * r1, cy + std::sin(a1) * r1}};
        g.poly(t, 3, c);
    }
}

// A ring as the game draws its rings: gold, a dark lower rim and a light
// upper one, narrowing to an edge as it turns.
void gold_ring(ui::Ui& g, float cx, float cy, float r, float turn, ui::Color body, ui::Color dark,
               ui::Color light, ui::Color edge) {
    const float face = std::fabs(std::cos(turn * 2.0f * SDL_PI_F));
    const float rx = std::max(r * 0.16f, r * face);
    const float band = r * 0.34f;
    g.ellipse_band(cx, cy, rx + r * 0.08f, r * 1.08f, band + r * 0.16f, edge);
    g.ellipse_band(cx, cy + r * 0.05f, rx, r, band, dark);
    g.ellipse_band(cx, cy - r * 0.03f, rx * 0.97f, r * 0.95f, band * 0.72f, body);
    if (face > 0.35f) g.ellipse_band(cx - rx * 0.08f, cy - r * 0.1f, rx * 0.8f, r * 0.8f, r * 0.08f, light);
}

const ui::Color kChaos[6] = {
    {255, 214, 40}, {60, 210, 255}, {255, 72, 90}, {70, 230, 110}, {200, 110, 255}, {255, 150, 40},
};

} // namespace

void banner(ui::Ui& g, const SDL_FRect& r, float lean, ui::Color fill, ui::Color edge, float edge_w) {
    parallelogram(g, r.x, r.y, r.w, r.h, lean, edge);
    if (r.h <= edge_w * 2 || r.w <= edge_w * 4) return;
    const float k = lean / r.h;  // horizontal shift per pixel of height
    const float ih = r.h - edge_w * 2;
    parallelogram(g, r.x + edge_w * 1.6f + k * edge_w, r.y + edge_w, r.w - edge_w * 3.2f - k * edge_w * 2, ih,
                  k * ih, fill);
}

void emblem(ui::Ui& g, float cx, float cy, float r, const std::string& icon, bool earned, float turn,
            unsigned seed) {
    using namespace color;
    const float o = std::max(1.0f, r * 0.12f);  // the black edge
    // The medallion: black edge, gold (or grey) rim, a deep purple face.
    g.ellipse(cx, cy, r, r, ink);
    g.ellipse(cx, cy, r - o, r - o, earned ? gold : locked);
    g.ellipse(cx, cy + o * 0.4f, r - o * 2.2f, r - o * 2.2f, earned ? gold_dark : locked_dark);
    g.ellipse(cx, cy, r - o * 2.4f, r - o * 2.4f, earned ? night : ui::Color{24, 20, 36});
    const float a = r * 0.58f;  // the picture's size
    auto c = [&](ui::Color x) { return grey(x, earned); };

    if (icon == "ring") {
        gold_ring(g, cx, cy, a, turn, c(gold), c(gold_dark), c(cream), ink);
    } else if (icon == "chaos") {
        const ui::Color body = kChaos[seed % 6];
        const ui::Color dark{uint8_t(body.r / 2), uint8_t(body.g / 2), uint8_t(body.b / 2), 255};
        gold_ring(g, cx, cy, a, turn, c(body), c(dark), c(cream), ink);
        star(g, cx + a * 0.62f, cy - a * 0.62f, a * 0.42f, 0.28f, 4, 0, c(cream));
    } else if (icon == "goal") {
        // A goal post: a pole and its round sign, turned a little.
        const float pw = a * 0.22f;
        g.rect({cx - pw * 0.5f - o * 0.5f, cy - a * 0.1f, pw + o, a * 1.15f}, ink);
        g.rect({cx - pw * 0.5f, cy - a * 0.05f, pw, a * 1.05f}, c(cream));
        const float face = std::max(0.25f, std::fabs(std::cos(turn * 2.0f * SDL_PI_F)));
        g.ellipse(cx, cy - a * 0.38f, a * 0.72f * face + o, a * 0.72f + o, ink);
        g.ellipse(cx, cy - a * 0.38f, a * 0.72f * face, a * 0.72f, c(magenta));
        star(g, cx, cy - a * 0.38f, a * 0.42f, 0.45f, 5, 0, c(gold));
    } else if (icon == "clock") {
        g.ellipse(cx, cy, a + o * 0.6f, a + o * 0.6f, ink);
        g.ellipse(cx, cy, a, a, c(cream));
        for (int i = 0; i < 4; ++i) {
            const float t = float(i) * SDL_PI_F / 2;
            g.ellipse(cx + std::cos(t) * a * 0.74f, cy + std::sin(t) * a * 0.74f, a * 0.09f, a * 0.09f, c(purple));
        }
        const float hw = a * 0.09f;
        const SDL_FPoint hour[4] = {{cx - hw, cy}, {cx - hw, cy - a * 0.5f}, {cx + hw, cy - a * 0.5f}, {cx + hw, cy}};
        g.poly(hour, 4, ink);
        const SDL_FPoint minute[4] = {{cx, cy - hw}, {cx + a * 0.7f, cy - hw}, {cx + a * 0.7f, cy + hw}, {cx, cy + hw}};
        g.poly(minute, 4, c(magenta));
        g.ellipse(cx, cy, a * 0.14f, a * 0.14f, ink);
    } else if (icon == "place") {
        // A pennant on a pole, as over an attraction's gate.
        const float px0 = cx - a * 0.55f;
        g.rect({px0 - o * 0.5f, cy - a - o * 0.5f, a * 0.18f + o, a * 2.0f + o}, ink);
        g.rect({px0, cy - a, a * 0.18f, a * 2.0f}, c(cream));
        const SDL_FPoint edge[3] = {{px0 + a * 0.12f, cy - a - o}, {cx + a * 0.95f + o, cy - a * 0.38f},
                                    {px0 + a * 0.12f, cy + a * 0.24f + o}};
        g.poly(edge, 3, ink);
        const SDL_FPoint flag[3] = {{px0 + a * 0.18f, cy - a * 0.92f}, {cx + a * 0.8f, cy - a * 0.38f},
                                    {px0 + a * 0.18f, cy + a * 0.16f}};
        g.poly(flag, 3, c(magenta));
        const SDL_FPoint stripe[3] = {{px0 + a * 0.18f, cy - a * 0.92f}, {cx + a * 0.8f, cy - a * 0.38f},
                                      {px0 + a * 0.18f, cy - a * 0.5f}};
        g.poly(stripe, 3, c(gold));
    } else if (icon == "boss") {
        star(g, cx, cy, a * 1.12f + o, 0.5f, 8, SDL_PI_F / 8, ink);
        star(g, cx, cy, a * 1.12f, 0.5f, 8, SDL_PI_F / 8, c(ui::Color{255, 120, 30}));
        star(g, cx, cy, a * 0.7f, 0.5f, 8, 0, c(gold));
        g.ellipse(cx, cy, a * 0.22f, a * 0.22f, c(cream));
    } else if (icon == "team") {
        // Two rings and the tether of light between them: the game's combi.
        const float rr = a * 0.58f;
        gold_ring(g, cx - a * 0.5f, cy + a * 0.14f, rr, 0, c(gold), c(gold_dark), c(cream), ink);
        gold_ring(g, cx + a * 0.5f, cy - a * 0.14f, rr, 0, c(magenta), c(ui::Color{120, 20, 80}), c(cream), ink);
        for (int i = -1; i <= 1; ++i)  // the tether's sparks between them
            star(g, cx + float(i) * a * 0.2f, cy - float(i) * a * 0.06f, a * 0.16f, 0.35f, 4, 0, c(cream));
    } else {
        star(g, cx, cy, a * 1.05f + o, 0.45f, 5, 0, ink);
        star(g, cx, cy, a * 1.05f, 0.45f, 5, 0, c(gold));
    }
}

void ring(ui::Ui& g, float cx, float cy, float r, float turn) {
    using namespace color;
    gold_ring(g, cx, cy, r, turn, gold, gold_dark, cream, ink);
}

SDL_FRect heading(ui::Ui& g, float x, float y, ui::Font f, const std::string& text, float max_w) {
    using namespace color;
    const float edge = std::max(2.0f, std::round(g.px(3)));
    const float head_h = g.line_height(f) + g.px(18);
    const float lean = head_h * 0.45f;
    const float pad = lean + g.px(16);
    const float w = std::min(g.text_width(f, text) + lean * 2 + g.px(40), std::max(head_h * 2, max_w - g.px(10)));
    const SDL_FRect box{x, y, w, head_h};
    banner(g, {box.x + g.px(10), box.y + head_h - g.px(6), box.w, g.px(14)}, lean * 0.3f, magenta, ink, edge);
    banner(g, box, lean, purple, ink, edge);
    g.text_fit(f, box.x + pad, y + (head_h - g.line_height(f)) * 0.5f, std::max(0.0f, w - pad - lean), text,
               ui::theme::text);
    return box;
}

void arrowhead(ui::Ui& g, float cx, float cy, float s, bool up, ui::Color fill, float edge) {
    const float d = up ? -1.0f : 1.0f;
    const SDL_FPoint outer[3] = {{cx - s - edge, cy - d * (s * 0.6f + edge * 0.5f)},
                                 {cx + s + edge, cy - d * (s * 0.6f + edge * 0.5f)},
                                 {cx, cy + d * (s * 0.6f + edge)}};
    const SDL_FPoint inner[3] = {{cx - s, cy - d * s * 0.6f}, {cx + s, cy - d * s * 0.6f}, {cx, cy + d * s * 0.6f}};
    g.poly(outer, 3, color::ink);
    g.poly(inner, 3, fill);
}

} // namespace chaotix::award_art
