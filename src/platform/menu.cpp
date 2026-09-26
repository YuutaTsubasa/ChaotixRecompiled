#include "platform/menu.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "frontend/config.h"
#include "game/achievements.h"

namespace chaotix {

namespace {

// Menus of this era shout, so the labels are upper case and the selected one
// carries a marker rather than a highlight bar.
const char* kAspectNames[] = {"AUTO", "4:3", "16:9", "16:10", "21:9", "CUSTOM"};
const char* kScaleNames[] = {"FIT", "INTEGER", "STRETCH"};
const char* kWindowNames[] = {"WINDOW", "BORDERLESS", "FULLSCREEN"};
const char* kTouchNames[] = {"AUTO", "ON", "OFF"};

std::string on_off(bool v) { return v ? "ON" : "OFF"; }

// Steps a value through a list, wrapping at both ends.
template <typename T>
void cycle(T& value, int count, int step) {
    int i = int(value) + step;
    while (i < 0) i += count;
    value = T(i % count);
}

} // namespace

void Menu::set_page(Page p) {
    page_ = p;
    selected_ = 0;
    scroll_ = 0;
    anim_ = 0;
    if (p == Page::Main) build_main();
    else if (p == Page::Options) build_options();
    else items_.clear();
}

void Menu::toggle() {
    if (open()) close(); else set_page(Page::Main);
}

void Menu::show_achievements() { set_page(Page::Achievements); }

bool Menu::show_page(const std::string& name) {
    if (name == "main") set_page(Page::Main);
    else if (name == "options") set_page(Page::Options);
    else if (name == "awards") set_page(Page::Achievements);
    else return false;
    return true;
}

std::string Menu::page_name() const {
    switch (page_) {
    case Page::Main: return "main";
    case Page::Options: return "options";
    case Page::Achievements: return "awards";
    default: return "";
    }
}

std::string Menu::selected_label() const {
    if (selected_ < 0 || selected_ >= int(items_.size())) return "";
    return items_[size_t(selected_)].label;
}

void Menu::build_main() {
    items_.clear();
    items_.push_back({"RESUME", {}, [this](int) { close(); }});
    items_.push_back({"OPTIONS", {}, [this](int) { set_page(Page::Options); }});
    items_.push_back({"AWARDS", {}, [this](int) { set_page(Page::Achievements); }});
    items_.push_back({"QUIT", {}, [this](int) { if (hooks_.quit) hooks_.quit(); }});
}

void Menu::build_options() {
    items_.clear();
    Config* c = cfg_;
    if (!c) return;
    auto video = [this] { if (hooks_.apply_video) hooks_.apply_video(); };

    items_.push_back({"WIDESCREEN",
                      [c] { return on_off(c->widescreen); },
                      [c](int s) { if (s) c->widescreen = !c->widescreen; }});
    items_.push_back({"ASPECT RATIO",
                      [c] { return std::string(kAspectNames[int(c->viewport.aspect)]); },
                      [c](int s) { if (s) cycle(c->viewport.aspect, 5, s); }});
    items_.push_back({"SCALING",
                      [c] { return std::string(kScaleNames[int(c->viewport.scale)]); },
                      [c](int s) { if (s) cycle(c->viewport.scale, 3, s); }});
    items_.push_back({"SMOOTHING",
                      [c] { return on_off(c->linear_filter); },
                      [c, video](int s) { if (s) { c->linear_filter = !c->linear_filter; video(); } }});
    items_.push_back({"SCREEN",
                      [c] { return std::string(kWindowNames[int(c->window_mode)]); },
                      [c, video](int s) { if (s) { cycle(c->window_mode, 3, s); video(); } }});
    items_.push_back({"V-SYNC",
                      [c] { return on_off(c->vsync); },
                      [c, video](int s) { if (s) { c->vsync = !c->vsync; video(); } }});
    items_.push_back({"SOUND",
                      [c] { return on_off(c->audio); },
                      [c](int s) { if (s) c->audio = !c->audio; }});
    items_.push_back({"VOLUME",
                      [c] { return std::to_string(c->volume); },
                      [c](int s) { c->volume = std::clamp(c->volume + s * 5, 0, 100); }});
    items_.push_back({"TOUCH PAD",
                      [c] { return std::string(kTouchNames[int(c->touch)]); },
                      [c](int s) { if (s) cycle(c->touch, 3, s); }});
    items_.push_back({"6-BUTTON PAD",
                      [c] { return on_off(c->six_button); },
                      [c](int s) { if (s) c->six_button = !c->six_button; }});
    items_.push_back({"AWARDS",
                      [c] { return on_off(c->achievements); },
                      [c](int s) { if (s) c->achievements = !c->achievements; }});
    items_.push_back({"BACK", {}, [this](int) { set_page(Page::Main); selected_ = 1; }});
}

void Menu::move(int delta) {
    if (items_.empty()) return;
    const int n = int(items_.size());
    selected_ = (selected_ + delta % n + n) % n;
    anim_ = 0;
}

void Menu::activate(int step) {
    if (selected_ < 0 || selected_ >= int(items_.size())) return;
    const Item& it = items_[size_t(selected_)];
    // An action ignores left/right; a value ignores "activate".
    if (!it.value && step != 0) return;
    if (it.act) it.act(step);
}

bool Menu::on_key(SDL_Keycode key) {
    if (!open()) return false;
    switch (key) {
    case SDLK_ESCAPE:
        if (page_ == Page::Main) close(); else set_page(Page::Main);
        return true;
    case SDLK_LEFT:
        if (page_ == Page::Main) move(-1); else activate(-1);
        return true;
    case SDLK_RIGHT:
        if (page_ == Page::Main) move(+1); else activate(+1);
        return true;
    case SDLK_UP:
        if (page_ == Page::Achievements) --scroll_; else if (page_ == Page::Options) move(-1);
        return true;
    case SDLK_DOWN:
        if (page_ == Page::Achievements) ++scroll_; else if (page_ == Page::Options) move(+1);
        return true;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
    case SDLK_SPACE:
        if (page_ == Page::Achievements) set_page(Page::Main); else activate(0);
        return true;
    default:
        return true;  // the menu swallows everything else while it is open
    }
}

bool Menu::on_pad(Uint8 button) {
    if (!open()) return false;
    switch (button) {
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
        if (page_ == Page::Main) move(-1); else activate(-1);
        return true;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
        if (page_ == Page::Main) move(+1); else activate(+1);
        return true;
    case SDL_GAMEPAD_BUTTON_DPAD_UP:
        if (page_ == Page::Achievements) --scroll_; else if (page_ == Page::Options) move(-1);
        return true;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
        if (page_ == Page::Achievements) ++scroll_; else if (page_ == Page::Options) move(+1);
        return true;
    case SDL_GAMEPAD_BUTTON_SOUTH:
        if (page_ == Page::Achievements) set_page(Page::Main); else activate(0);
        return true;
    case SDL_GAMEPAD_BUTTON_EAST:
        if (page_ == Page::Main) close(); else set_page(Page::Main);
        return true;
    default:
        return true;
    }
}

bool Menu::on_touch_down(float x, float y) {
    if (!open()) return false;
    drag_y_ = y;
    drag_total_ = 0;
    for (const Hit& h : hits_) {
        if (x < h.r.x || x > h.r.x + h.r.w || y < h.r.y || y > h.r.y + h.r.h) continue;
        // A tap on an entry selects it; a second tap acts on it, which keeps
        // one finger enough to drive the whole menu.
        if (h.index == selected_) activate(0); else { selected_ = h.index; anim_ = 0; }
        drag_total_ = 1e9f;  // this touch is spoken for
        return true;
    }
    return true;
}

bool Menu::on_touch_up() {
    if (!open()) return false;
    if (page_ == Page::Achievements && drag_total_ < 1e8f) set_page(Page::Main);
    return true;
}

void Menu::draw(ui::Ui& g, achievements::Tracker& ach) {
    if (!open()) return;
    hits_.clear();
    anim_ += 0.08f;

    // Dim the game rather than cover it: the characters on the title screen
    // are the backdrop.
    g.rect({0, 0, g.width(), g.height()}, ui::theme::background.alpha(150));

    if (page_ == Page::Main) draw_main(g);
    else if (page_ == Page::Options) draw_options(g);
    else draw_achievements(g, ach);
}

void Menu::draw_main(ui::Ui& g) {
    const float item_h = g.line_height(ui::Font::Pixel);
    const float hint_h = g.line_height(ui::Font::Small);
    // A band across the lower part, tall enough to hold the row and the hint,
    // so the art above it stays visible.
    const float band_h = g.px(26) + item_h + g.px(14) + hint_h + g.px(16);
    const float band_y = g.height() - band_h - g.px(24);
    g.rect({0, band_y, g.width(), band_h}, ui::theme::background.alpha(215));
    g.rect({0, band_y, g.width(), g.px(3)}, ui::theme::accent);
    g.rect({0, band_y + band_h - g.px(3), g.width(), g.px(3)}, ui::theme::accent);

    // Measure first so the row can be centred as a whole.
    const float gap = g.px(34);
    float total = 0;
    for (size_t i = 0; i < items_.size(); ++i) {
        total += g.text_width(ui::Font::Pixel, items_[i].label);
        if (i + 1 < items_.size()) total += gap;
    }
    float x = (g.width() - total) * 0.5f;
    const float y = band_y + g.px(26);

    for (size_t i = 0; i < items_.size(); ++i) {
        const bool sel = int(i) == selected_;
        const float w = g.text_width(ui::Font::Pixel, items_[i].label);
        if (sel) {
            // The selected entry gets a marker that bobs, the way these menus
            // have always done it.
            const float bob = std::sin(anim_ * 3.0f) * g.px(2);
            g.text(ui::Font::Pixel, x - g.px(24) + bob, y, ">", ui::theme::accent);
            g.text(ui::Font::Pixel, x, y, items_[i].label, ui::theme::text);
            g.rect({x, y + item_h + g.px(4), w, g.px(3)}, ui::theme::accent);
        } else {
            g.text(ui::Font::Pixel, x, y, items_[i].label, ui::theme::text_faint);
        }
        hits_.push_back({{x - g.px(24), y - g.px(10), w + g.px(34), item_h + g.px(20)}, int(i)});
        x += w + gap;
    }

    const char* hint = "Left/Right choose    Enter select    Esc close";
    g.text(ui::Font::Small, (g.width() - g.text_width(ui::Font::Small, hint)) * 0.5f,
           band_y + band_h - g.px(16) - hint_h, hint, ui::theme::text_faint);
}

void Menu::draw_options(ui::Ui& g) {
    const float pad = g.px(34);   // room for the selection marker inside the panel
    const float column = std::min(g.width() - pad * 2 - g.px(24), g.px(560));
    const float x = (g.width() - column) * 0.5f;
    const float row_h = g.line_height(ui::Font::Pixel) + g.px(16);
    const float head_h = g.line_height(ui::Font::PixelBig) + g.px(22);
    const float hint_h = g.line_height(ui::Font::Small);
    const float body = head_h + row_h * float(items_.size()) + g.px(12) + hint_h;
    const float total = body + g.px(30);
    float y = std::max(g.px(14), (g.height() - total) * 0.5f) + g.px(16);
    const float panel_top = y - g.px(16);

    g.panel({x - pad, panel_top, column + pad * 2, total},
            ui::theme::background.alpha(240), ui::theme::accent, g.px(4), g.px(3));

    g.text(ui::Font::PixelBig, x, y, "OPTIONS", ui::theme::accent);
    y += head_h;

    for (size_t i = 0; i < items_.size(); ++i) {
        const bool sel = int(i) == selected_;
        const Item& it = items_[i];
        if (sel) {
            const float bob = std::sin(anim_ * 3.0f) * g.px(2);
            g.text(ui::Font::Pixel, x - g.px(24) + bob, y, ">", ui::theme::accent);
        }
        g.text(ui::Font::Pixel, x, y, it.label, sel ? ui::theme::text : ui::theme::text_dim);
        if (it.value) {
            const std::string v = it.value();
            const float vw = g.text_width(ui::Font::Pixel, v);
            const float vx = x + column - vw;
            g.text(ui::Font::Pixel, vx, y, v, sel ? ui::theme::accent : ui::theme::text_dim);
            // Arrows only on the row you can actually change.
            if (sel) {
                g.text(ui::Font::Pixel, vx - g.px(22), y, "<", ui::theme::accent);
                g.text(ui::Font::Pixel, x + column + g.px(8), y, ">", ui::theme::accent);
            }
        }
        hits_.push_back({{x - g.px(24), y - g.px(4), column + g.px(48), row_h}, int(i)});
        y += row_h;
    }

    g.text(ui::Font::Small, x, y + g.px(8),
           "Up/Down choose    Left/Right change    Esc back", ui::theme::text_faint);
}

void Menu::draw_achievements(ui::Ui& g, achievements::Tracker& ach) {
    const float column = std::min(g.width() - g.px(32), g.px(680));
    const float x = (g.width() - column) * 0.5f;
    float y = std::max(g.px(18), g.height() * 0.05f);

    g.rect({0, 0, g.width(), g.height()}, ui::theme::background.alpha(200));
    g.text(ui::Font::PixelBig, x, y, "AWARDS", ui::theme::accent);
    char head[96];
    std::snprintf(head, sizeof head, "%d OF %zu    %d / %d PTS", ach.unlocked_count(),
                  ach.list().size(), ach.points_earned(), ach.points_total());
    const float hw = g.text_width(ui::Font::Pixel, head);
    g.text(ui::Font::Pixel, x + column - hw, y + g.px(10), head, ui::theme::text_dim);
    y += g.line_height(ui::Font::PixelBig) + g.px(12);

    const float bar_h = g.px(6);
    const int total = ach.points_total();
    const float done = total > 0 ? float(ach.points_earned()) / float(total) : 0.0f;
    g.rect({x, y, column, bar_h}, ui::theme::surface_raised);
    if (done > 0) g.rect({x, y, column * done, bar_h}, ui::theme::accent);
    y += bar_h + g.px(14);

    const float row_h = g.line_height(ui::Font::Pixel) + g.line_height(ui::Font::Small) + g.px(16);
    const float gap = g.px(6);
    const float bottom = g.height() - g.line_height(ui::Font::Small) - g.px(20);
    const auto& all = ach.list();
    rows_ = std::max(1, int((bottom - y) / (row_h + gap)));
    row_h_ = row_h + gap;
    scroll_ = std::clamp(scroll_, 0, std::max(0, int(all.size()) - rows_));
    const int last = std::min(int(all.size()), scroll_ + rows_);
    for (int i = scroll_; i < last; ++i) {
        const auto& a = all[size_t(i)];
        const SDL_FRect row{x, y, column, row_h};
        g.panel(row, a.unlocked ? ui::theme::surface_raised : ui::theme::surface,
                a.unlocked ? ui::theme::accent : ui::theme::outline, g.px(4), a.unlocked ? 2.0f : 1.0f);
        char pts[16];
        std::snprintf(pts, sizeof pts, "%d", a.points);
        const float pw = g.text_width(ui::Font::Pixel, pts);
        g.text(ui::Font::Pixel, x + column - pw - g.px(12), y + g.px(8), pts,
               a.unlocked ? ui::theme::accent : ui::theme::text_faint);
        const float tw = column - pw - g.px(32);
        g.text_fit(ui::Font::Pixel, x + g.px(12), y + g.px(6), tw, a.title,
                   a.unlocked ? ui::theme::text : ui::theme::text_dim);
        g.text_fit(ui::Font::Small, x + g.px(12), y + g.px(6) + g.line_height(ui::Font::Pixel),
                   tw, a.description, a.unlocked ? ui::theme::text_dim : ui::theme::text_faint);
        y += row_h + gap;
    }
    g.text(ui::Font::Small, x, g.height() - g.line_height(ui::Font::Small) - g.px(10),
           "Up/Down scroll    Enter or Esc back", ui::theme::text_faint);
}

} // namespace chaotix
