#include "platform/sdl_setup.h"

#include <SDL3/SDL.h>

#include "frontend/setup.h"
#include "platform/ui.h"
#include "renderer/image_io.h"
#include "runtime/log.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace chaotix {

namespace {

struct Hit {
    SDL_FRect r{};
    int index = -1;          // candidate index, or one of the actions below
};
enum : int { kActionBrowse = -2, kActionInstall = -3, kActionRescan = -4 };

struct SetupState {
    std::vector<setup::Candidate> candidates;
    std::vector<Hit> hits;   // touch targets from the last frame
    int selected = 0;
    int visible = 0;         // rows the last layout had room for
    std::string status;
    std::string picked;      // filled in by the file dialog callback
    bool dialog_open = false;
    bool rescan = true;
};

void SDLCALL file_chosen(void* userdata, const char* const* files, int) {
    auto* st = static_cast<SetupState*>(userdata);
    st->dialog_open = false;
    if (files && files[0]) st->picked = files[0];
}

// A button. Returns its rectangle so the caller can lay the next one out.
SDL_FRect button(ui::Ui& g, SetupState& st, float x, float y, const std::string& label,
                 int action, bool enabled, bool primary) {
    const float pad = g.px(16);
    const float w = g.text_width(ui::Font::Body, label) + pad * 2;
    const float h = g.line_height(ui::Font::Body) + g.px(14);
    const SDL_FRect r{x, y, w, h};
    const float radius = g.px(8);
    if (!enabled) {
        g.panel(r, ui::theme::surface, ui::theme::outline, radius);
        g.text(ui::Font::Body, x + pad, y + g.px(7), label, ui::theme::text_faint);
    } else if (primary) {
        g.rect(r, ui::theme::accent, radius);
        g.text(ui::Font::Body, x + pad, y + g.px(7), label, ui::theme::background);
    } else {
        g.panel(r, ui::theme::surface_raised, ui::theme::outline_strong, radius);
        g.text(ui::Font::Body, x + pad, y + g.px(7), label, ui::theme::text);
    }
    if (enabled) st.hits.push_back({r, action});
    return r;
}

void draw(ui::Ui& g, const std::string& store_root, SetupState& st) {
    st.hits.clear();
    g.begin_frame();
    g.rect({0, 0, g.width(), g.height()}, ui::theme::background);

    // A centred column, so the page does not stretch across a wide window.
    const float column = std::min(g.width() - g.px(32), g.px(620));
    const float x = (g.width() - column) * 0.5f;
    const float right = x + column;

    // Lay the page out before drawing it, so the whole block can be centred
    // vertically instead of leaving a gap between the list and the footer.
    const float card_h = g.line_height(ui::Font::Body) + g.line_height(ui::Font::Small) + g.px(18);
    const float card_gap = g.px(8);
    const float header_h = g.line_height(ui::Font::Title) + g.line_height(ui::Font::Subtitle)
                         + g.px(14) + (g.line_height(ui::Font::Small) + g.px(2)) * 2 + g.px(20);
    const float footer_h = g.line_height(ui::Font::Body) + g.px(14)     // buttons
                         + g.line_height(ui::Font::Small) * 2 + g.px(34);
    const int rows = st.candidates.empty() ? 1 : int(st.candidates.size());
    const float room = g.height() - header_h - footer_h - g.px(48);
    st.visible = std::clamp(int(room / (card_h + card_gap)), 1, rows);
    const float list_h = float(st.visible) * (card_h + card_gap);
    float y = std::max(g.px(24), (g.height() - header_h - list_h - footer_h) * 0.5f);

    g.text(ui::Font::Title, x, y, "Knuckles' Chaotix", ui::theme::text);
    y += g.line_height(ui::Font::Title);
    g.text(ui::Font::Subtitle, x, y, "Recompiled", ui::theme::accent);
    y += g.line_height(ui::Font::Subtitle) + g.px(14);

    g.text_fit(ui::Font::Small, x, y, column,
               "This program contains no game data. Choose your own legally obtained",
               ui::theme::text_dim);
    y += g.line_height(ui::Font::Small) + g.px(2);
    g.text_fit(ui::Font::Small, x, y, column,
               "ROM: it is copied into this app's folder, so you only do this once.",
               ui::theme::text_dim);
    y += g.line_height(ui::Font::Small) + g.px(20);

    if (st.candidates.empty()) {
        const SDL_FRect box{x, y, column, card_h + g.px(10)};
        g.panel(box, ui::theme::surface, ui::theme::outline, g.px(10));
        g.text_fit(ui::Font::Body, x + g.px(14), y + g.px(10), column - g.px(28),
                   "No ROM found in the usual folders", ui::theme::warn);
        g.text_fit(ui::Font::Small, x + g.px(14), y + g.px(10) + g.line_height(ui::Font::Body),
                   column - g.px(28), "Drop a file onto this window, or use Browse below.",
                   ui::theme::text_dim);
        y += box.h + card_gap;
    } else {
        // Keep the highlighted row on screen when the list is longer than the
        // window: scroll by whole rows.
        const int first = std::clamp(st.selected - st.visible + 1, 0,
                                     std::max(0, int(st.candidates.size()) - st.visible));
        const int last = std::min(int(st.candidates.size()), first + st.visible);
        for (int i = first; i < last; ++i) {
            const setup::Candidate& c = st.candidates[size_t(i)];
            const bool sel = i == st.selected;
            const SDL_FRect card{x, y, column, card_h};
            g.panel(card, sel ? ui::theme::surface_raised : ui::theme::surface,
                    sel ? ui::theme::accent : ui::theme::outline, g.px(10), sel ? g.px(2) : 1);

            const char* status = c.verified ? "VERIFIED" : c.loadable ? "UNKNOWN" : "UNREADABLE";
            const ui::Color status_fg = c.verified ? ui::theme::good
                                      : c.loadable ? ui::theme::warn : ui::theme::bad;
            const float pill_w = g.text_width(ui::Font::Small, status) + g.px(14);
            g.pill(right - g.px(14) - pill_w, y + g.px(10), status, status_fg,
                   status_fg.alpha(38));

            const float text_w = column - g.px(36) - pill_w;
            g.text_fit(ui::Font::Body, x + g.px(14), y + g.px(8), text_w, c.name,
                       sel ? ui::theme::text : ui::theme::text_dim);
            g.text_fit(ui::Font::Small, x + g.px(14), y + g.px(8) + g.line_height(ui::Font::Body),
                       column - g.px(28), c.path, ui::theme::text_faint, true);

            st.hits.push_back({card, i});
            y += card_h + card_gap;
        }
        if (last < int(st.candidates.size()) || first > 0) {
            char more[64];
            std::snprintf(more, sizeof more, "%d of %zu", st.selected + 1, st.candidates.size());
            g.text(ui::Font::Small, x, y - g.px(2), more, ui::theme::text_faint);
        }
    }

    // Footer: the actions, then the keyboard equivalents, then where it goes.
    float fy = y + g.px(10);
    SDL_FRect b = button(g, st, x, fy, "Install", kActionInstall, !st.candidates.empty(), true);
    b = button(g, st, b.x + b.w + g.px(10), fy, "Browse", kActionBrowse, true, false);
    button(g, st, b.x + b.w + g.px(10), fy, "Rescan", kActionRescan, true, false);
    fy += b.h + g.px(12);

    if (!st.status.empty()) {
        g.text_fit(ui::Font::Small, x, fy, column, st.status, ui::theme::warn);
    } else {
        g.text_fit(ui::Font::Small, x, fy, column,
                   "Enter or (A) install    O browse    R rescan    Esc quit",
                   ui::theme::text_faint);
    }
    fy += g.line_height(ui::Font::Small) + g.px(4);
    g.text_fit(ui::Font::Small, x, fy, column, "Installs to " + setup::installed_rom_path(store_root),
               ui::theme::text_faint, true);

    g.end_frame();
}

} // namespace

std::string run_setup_screen(SDL_Window* window, SDL_Renderer* renderer,
                             const std::string& store_root,
                             const std::vector<std::string>& search_dirs) {
    SetupState st;
    ui::Ui g;
    if (!g.init(renderer)) {
        // Without a font there is nothing sensible to draw; the caller still
        // accepts a ROM on the command line or an already installed copy.
        LOGE("setup", "cannot initialise the setup screen (no font)");
        return "";
    }
    const SDL_DialogFileFilter filters[] = {{"32X ROM", "32x;bin;md;gen;rom"}, {"All files", "*"}};

    auto install = [&](const std::string& path) -> std::string {
        setup::Candidate c = setup::check_file(path);
        if (!c.loadable) {
            st.status = c.label;
            return "";
        }
        std::string installed, err;
        if (!setup::install(path, store_root, &installed, &err)) {
            st.status = "Cannot install: " + err;
            return "";
        }
        return installed;
    };
    auto browse = [&]() {
        if (st.dialog_open) return;
        st.dialog_open = true;
        SDL_ShowOpenFileDialog(file_chosen, &st, window, filters, 2, nullptr, false);
    };

    for (;;) {
        if (st.rescan) {
            st.candidates = setup::scan_candidates(search_dirs);
            st.selected = 0;
            st.rescan = false;
        }

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_EVENT_QUIT:
                return "";
            case SDL_EVENT_DROP_FILE: {
                if (!e.drop.data) break;
                std::string installed = install(e.drop.data);
                if (!installed.empty()) return installed;
                break;
            }
            case SDL_EVENT_KEY_DOWN:
                switch (e.key.key) {
                case SDLK_ESCAPE:
                    return "";
                case SDLK_UP:
                    if (st.selected > 0) --st.selected;
                    break;
                case SDLK_DOWN:
                    if (st.selected + 1 < int(st.candidates.size())) ++st.selected;
                    break;
                case SDLK_R:
                    st.rescan = true;
                    break;
                case SDLK_O:
                    browse();
                    break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER: {
                    if (st.candidates.empty()) { browse(); break; }
                    std::string installed = install(st.candidates[size_t(st.selected)].path);
                    if (!installed.empty()) return installed;
                    break;
                }
                default:
                    break;
                }
                break;
            case SDL_EVENT_FINGER_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                float px = 0, py = 0;
                if (e.type == SDL_EVENT_FINGER_DOWN) {
                    px = e.tfinger.x * g.width();
                    py = e.tfinger.y * g.height();
                } else {
                    px = e.button.x;
                    py = e.button.y;
                }
                for (const Hit& hit : st.hits) {
                    if (px < hit.r.x || px > hit.r.x + hit.r.w) continue;
                    if (py < hit.r.y || py > hit.r.y + hit.r.h) continue;
                    if (hit.index == kActionBrowse) { browse(); break; }
                    if (hit.index == kActionRescan) { st.rescan = true; break; }
                    const int target = hit.index == kActionInstall ? st.selected : hit.index;
                    if (target < 0 || target >= int(st.candidates.size())) break;
                    // First tap highlights, a tap on the highlighted row (or
                    // the Install button) goes ahead.
                    if (hit.index != kActionInstall && target != st.selected) {
                        st.selected = target;
                        break;
                    }
                    std::string installed = install(st.candidates[size_t(target)].path);
                    if (!installed.empty()) return installed;
                    break;
                }
                break;
            }
            case SDL_EVENT_GAMEPAD_ADDED:
                SDL_OpenGamepad(e.gdevice.which);
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_UP && st.selected > 0) --st.selected;
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_DOWN && st.selected + 1 < int(st.candidates.size())) ++st.selected;
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH && !st.candidates.empty()) {
                    std::string installed = install(st.candidates[size_t(st.selected)].path);
                    if (!installed.empty()) return installed;
                }
                break;
            default:
                break;
            }
        }

        if (!st.picked.empty()) {
            std::string path = st.picked;
            st.picked.clear();
            std::string installed = install(path);
            if (!installed.empty()) return installed;
        }

        draw(g, store_root, st);
        // Test hook: capture the page (before presenting, while the target
        // still holds it) and leave, so the setup screen can be checked
        // without a human at the keyboard.
        if (const char* shot = SDL_getenv("CHAOTIX_SETUP_SHOT")) {
            if (SDL_Surface* s = SDL_RenderReadPixels(renderer, nullptr)) {
                if (SDL_Surface* c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_XRGB8888)) {
                    write_png(shot, static_cast<const uint32_t*>(c->pixels), c->w, c->h, c->pitch / 4);
                    SDL_DestroySurface(c);
                }
                SDL_DestroySurface(s);
            }
            return "";
        }
        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }
}

} // namespace chaotix
