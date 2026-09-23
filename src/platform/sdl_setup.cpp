#include "platform/sdl_setup.h"

#include <SDL3/SDL.h>

#include "frontend/setup.h"
#include "renderer/image_io.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace chaotix {

namespace {

struct Hit {
    float x = 0, y = 0, w = 0, h = 0;
    int index = -1;          // candidate index, or one of the actions below
};
enum : int { kActionBrowse = -2, kActionInstall = -3 };

struct SetupState {
    std::vector<setup::Candidate> candidates;
    std::vector<Hit> hits;   // touch targets from the last frame
    float scale = 1;         // render scale the hits were computed in
    int selected = 0;
    std::string status;
    std::string picked;      // filled in by the file dialog callback
    bool dialog_open = false;
    bool rescan = true;
};

// The debug font is 8 px wide; keep a line inside the window, with the tail
// of a long path rather than its head (the file name matters most).
std::string fit(const std::string& s, float x, float width, bool keep_tail = false) {
    const size_t room = size_t(std::max(4.0f, (width - x) / 8.0f));
    if (s.size() <= room) return s;
    if (keep_tail) return "..." + s.substr(s.size() - (room - 3));
    return s.substr(0, room - 3) + "...";
}

void text(SDL_Renderer* r, float x, float y, const std::string& s, Uint8 cr, Uint8 cg, Uint8 cb) {
    SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
    SDL_RenderDebugText(r, x, y, s.c_str());
}

void SDLCALL file_chosen(void* userdata, const char* const* files, int) {
    auto* st = static_cast<SetupState*>(userdata);
    st->dialog_open = false;
    if (files && files[0]) st->picked = files[0];
}

void draw(SDL_Renderer* renderer, const std::string& store_root, SetupState& st) {
    st.hits.clear();
    int ow = 0, oh = 0;
    SDL_GetRenderOutputSize(renderer, &ow, &oh);
    SDL_SetRenderDrawColor(renderer, 12, 14, 32, 255);
    SDL_RenderClear(renderer);
    // The debug font is 8x8; scale it so the page fills a reasonable part of
    // the window on both a small window and a phone screen.
    const float scale = std::max(1.0f, std::min(float(ow) / 520.0f, float(oh) / 300.0f));
    st.scale = scale;
    SDL_SetRenderScale(renderer, scale, scale);
    const float h = float(oh) / scale, w = float(ow) / scale;

    float y = 16;
    text(renderer, 16, y, "Knuckles' Chaotix Recompiled", 255, 232, 120);
    y += 12;
    text(renderer, 16, y, "Setup", 255, 232, 120);
    y += 20;
    text(renderer, 16, y, "This program contains no game data.", 205, 205, 215);
    y += 11;
    text(renderer, 16, y, "Choose your own legally obtained Knuckles' Chaotix", 205, 205, 215);
    y += 11;
    text(renderer, 16, y, "ROM. It is copied into this app's own folder, so you", 205, 205, 215);
    y += 11;
    text(renderer, 16, y, "only have to do this once.", 205, 205, 215);
    y += 20;

    if (st.candidates.empty()) {
        text(renderer, 16, y, "No ROM found in the usual folders.", 230, 160, 160);
        y += 11;
        text(renderer, 16, y, "Drop a file onto this window, or press O to browse.", 205, 205, 215);
    } else {
        text(renderer, 16, y, "Found:", 150, 200, 255);
        y += 13;
        for (size_t i = 0; i < st.candidates.size() && i < 8; ++i) {
            const setup::Candidate& c = st.candidates[i];
            const bool sel = int(i) == st.selected;
            const std::string line = fit(std::string(sel ? "> " : "  ") + c.label, 20, w - 8);
            if (c.verified) text(renderer, 20, y, line, sel ? 180 : 140, 255, sel ? 180 : 140);
            else if (c.loadable) text(renderer, 20, y, line, 255, sel ? 220 : 180, 120);
            else text(renderer, 20, y, line, 190, 130, 130);
            st.hits.push_back({12, y - 2, w - 24, 12, int(i)});
            y += 11;
        }
    }

    // Where the highlighted file actually is.
    if (!st.candidates.empty()) {
        y += 6;
        text(renderer, 20, y, fit(st.candidates[size_t(st.selected)].path, 20, w - 8, true), 140, 140, 160);
    }

    const float footer = h - 46;
    // Tappable buttons, with the keyboard shortcuts written next to them.
    auto button = [&](float x, float bw, const char* label, int action, bool enabled) {
        SDL_FRect r{x, footer - 16, bw, 14};
        SDL_SetRenderDrawColor(renderer, enabled ? 40 : 26, enabled ? 60 : 30, enabled ? 110 : 40, 255);
        SDL_RenderFillRect(renderer, &r);
        SDL_SetRenderDrawColor(renderer, enabled ? 150 : 80, enabled ? 215 : 90, 255, 255);
        SDL_RenderRect(renderer, &r);
        text(renderer, x + 6, footer - 12, label, enabled ? 235 : 130, enabled ? 235 : 130, 255);
        if (enabled) st.hits.push_back({x, footer - 16, bw, 14, action});
    };
    button(16, 86, "INSTALL", kActionInstall, !st.candidates.empty());
    button(110, 86, "BROWSE", kActionBrowse, true);
    text(renderer, 16, footer + 2, "Enter / (A): install    O: browse    R: rescan", 150, 215, 255);
    text(renderer, 16, footer + 13, "Esc: quit     or drag a ROM file onto this window", 150, 215, 255);
    if (!st.status.empty()) text(renderer, 16, footer + 26, fit(st.status, 16, w - 8), 255, 200, 140);

    text(renderer, 16, h - 12, fit("Installs to " + setup::installed_rom_path(store_root), 16, w - 8, true), 120, 120, 145);

    SDL_SetRenderScale(renderer, 1, 1);
}

} // namespace

std::string run_setup_screen(SDL_Window* window, SDL_Renderer* renderer,
                             const std::string& store_root,
                             const std::vector<std::string>& search_dirs) {
    SetupState st;
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
                int ow = 0, oh = 0;
                SDL_GetRenderOutputSize(renderer, &ow, &oh);
                float px = 0, py = 0;
                if (e.type == SDL_EVENT_FINGER_DOWN) {
                    px = e.tfinger.x * float(ow) / st.scale;
                    py = e.tfinger.y * float(oh) / st.scale;
                } else {
                    px = e.button.x / st.scale;
                    py = e.button.y / st.scale;
                }
                for (const Hit& hit : st.hits) {
                    if (px < hit.x || px > hit.x + hit.w || py < hit.y || py > hit.y + hit.h) continue;
                    if (hit.index == kActionBrowse) { browse(); break; }
                    const int target = hit.index == kActionInstall ? st.selected : hit.index;
                    if (target < 0 || target >= int(st.candidates.size())) break;
                    // First tap highlights, a tap on the highlighted row (or
                    // the INSTALL button) goes ahead.
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

        draw(renderer, store_root, st);
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
