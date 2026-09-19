// SDL3 platform layer: window, rendering, input, timing, storage paths.
//
// This is the only file that talks to SDL. Game/runtime code sees only
// InputState, the output framebuffer and SaveStore paths. SDL_Renderer picks a
// native backend per platform (Direct3D 11/12, Metal, Vulkan, OpenGL/ES), so
// no graphics API is referenced directly.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "frontend/config.h"
#include "frontend/debug_overlay.h"
#include "game/recomp_dispatch.h"
#include "input/input.h"
#include "input/touch_controls.h"
#include "renderer/image_io.h"
#include "renderer/viewport.h"
#include "runtime/log.h"
#include "runtime/save.h"
#include "runtime/system.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace chaotix;

namespace {

// Original NTSC field rate: 53.693175 MHz / (3420 * 262).
constexpr double kNtscFrameRate = 53693175.0 / (3420.0 * 262.0);

struct App {
    Config cfg;
    SaveStore store;
    std::unique_ptr<Machine> m;
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    SDL_AudioStream* audio = nullptr;
    std::vector<SDL_Gamepad*> pads;
    TouchControls touch;
    bool touch_enabled = false;
    std::map<SDL_FingerID, uint16_t> fingers;
    bool running = true;
    bool fast_forward = false;
    int overlay_page = 0;
    FrameTiming timing;
    std::vector<uint32_t> watches = {0xFFF000, 0xFFF010};
    uint64_t sram_dirty_since = 0;
    // --autotest: run N frames with scripted input, capture the presented
    // image (what the user would see) and exit. Used by rendering tests.
    uint64_t autotest_frames = 0;
    std::string autotest_shot;
    struct Press { uint64_t frame, duration; uint16_t buttons; };
    std::vector<Press> script;
};

void capture_output(App& app) {
    SDL_Surface* s = SDL_RenderReadPixels(app.renderer, nullptr);
    if (!s) { LOGW("app", "SDL_RenderReadPixels: %s", SDL_GetError()); return; }
    SDL_Surface* c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_XRGB8888);
    SDL_DestroySurface(s);
    if (!c) return;
    write_png(app.autotest_shot, static_cast<const uint32_t*>(c->pixels), c->w, c->h, c->pitch / 4);
    LOGI("app", "autotest: captured %dx%d output to %s", c->w, c->h, app.autotest_shot.c_str());
    SDL_DestroySurface(c);
}

std::string find_rom(const App& app, int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a.size() > 4 && a[0] != '-') return a;
    }
    if (!app.cfg.rom_path.empty() && std::filesystem::exists(app.cfg.rom_path)) return app.cfg.rom_path;
    // Look in conventional places next to the executable / working directory.
    std::vector<std::filesystem::path> dirs = {"__ROM__", "rom", "."};
    if (const char* base = SDL_GetBasePath()) {
        dirs.push_back(std::filesystem::path(base) / "__ROM__");
        dirs.push_back(std::filesystem::path(base) / "rom");
    }
    dirs.push_back(std::filesystem::path(app.store.root()) / "rom");
    // Mobile: users import the ROM through the Files app / document folder.
    if (const char* docs = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS)) {
        dirs.push_back(std::filesystem::path(docs));
        dirs.push_back(std::filesystem::path(docs) / "Chaotix");
    }
#if defined(SDL_PLATFORM_ANDROID)
    if (const char* ext = SDL_GetAndroidExternalStoragePath()) {
        dirs.push_back(std::filesystem::path(ext));
        dirs.push_back(std::filesystem::path(ext) / "rom");
    }
#endif
    for (const auto& d : dirs) {
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(d, ec)) {
            auto ext = e.path().extension().string();
            if (ext == ".32x" || ext == ".32X" || ext == ".bin") return e.path().string();
        }
    }
    return "";
}

void apply_window_mode(App& app) {
    switch (app.cfg.window_mode) {
    case WindowMode::Windowed:
        SDL_SetWindowFullscreen(app.window, false);
        SDL_SetWindowBordered(app.window, true);
        break;
    case WindowMode::Borderless:
        SDL_SetWindowFullscreenMode(app.window, nullptr);  // desktop-sized, no mode change
        SDL_SetWindowFullscreen(app.window, true);
        break;
    case WindowMode::Fullscreen: {
        SDL_DisplayID disp = SDL_GetDisplayForWindow(app.window);
        const SDL_DisplayMode* dm = SDL_GetDesktopDisplayMode(disp);
        SDL_DisplayMode closest;
        if (dm && SDL_GetClosestFullscreenDisplayMode(disp, dm->w, dm->h, dm->refresh_rate, true, &closest))
            SDL_SetWindowFullscreenMode(app.window, &closest);
        SDL_SetWindowFullscreen(app.window, true);
        break;
    }
    }
}

uint16_t keyboard_buttons(const App& app) {
    const bool* ks = SDL_GetKeyboardState(nullptr);
    uint16_t out = 0;
    for (const auto& [name, keyname] : app.cfg.keys) {
        SDL_Scancode sc = SDL_GetScancodeFromName(keyname.c_str());
        if (sc != SDL_SCANCODE_UNKNOWN && ks[sc]) out |= pad_button_from_name(name.c_str());
    }
    return out;
}

uint16_t gamepad_buttons(SDL_Gamepad* g) {
    uint16_t out = 0;
    auto b = [&](SDL_GamepadButton btn, uint16_t mask) { if (SDL_GetGamepadButton(g, btn)) out |= mask; };
    // Layout follows the Mega Drive 6-button pad: bottom row A B C, top row X Y Z.
    b(SDL_GAMEPAD_BUTTON_DPAD_UP, PAD_UP);
    b(SDL_GAMEPAD_BUTTON_DPAD_DOWN, PAD_DOWN);
    b(SDL_GAMEPAD_BUTTON_DPAD_LEFT, PAD_LEFT);
    b(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_RIGHT);
    b(SDL_GAMEPAD_BUTTON_WEST, PAD_A);
    b(SDL_GAMEPAD_BUTTON_SOUTH, PAD_B);
    b(SDL_GAMEPAD_BUTTON_EAST, PAD_C);
    b(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, PAD_X);
    b(SDL_GAMEPAD_BUTTON_NORTH, PAD_Y);
    b(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_Z);
    b(SDL_GAMEPAD_BUTTON_START, PAD_START);
    b(SDL_GAMEPAD_BUTTON_BACK, PAD_MODE);
    const int dead = 12000;
    int lx = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX), ly = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY);
    if (lx < -dead) out |= PAD_LEFT;
    if (lx > dead) out |= PAD_RIGHT;
    if (ly < -dead) out |= PAD_UP;
    if (ly > dead) out |= PAD_DOWN;
    return out;
}

void save_screenshot(App& app) {
    std::string dir = app.store.root() + "Screenshots/";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    char name[64];
    std::snprintf(name, sizeof name, "chaotix_%06llu.png", (unsigned long long)app.m->frame_count);
    write_png(dir + name, app.m->framebuffer, app.m->fb_width, app.m->fb_height, kScreenWidth);
    LOGI("app", "screenshot saved to %s%s", dir.c_str(), name);
}

void handle_key(App& app, const SDL_KeyboardEvent& k) {
    if (k.repeat) return;
    switch (k.key) {
    case SDLK_ESCAPE: app.running = false; break;
    case SDLK_F1: app.cfg.debug_overlay = !app.cfg.debug_overlay; break;
    case SDLK_F2: {
        static const AspectMode order[] = {AspectMode::Auto, AspectMode::R4_3, AspectMode::R16_9, AspectMode::R16_10, AspectMode::R21_9};
        int i = 0;
        for (int j = 0; j < 5; ++j) if (order[j] == app.cfg.viewport.aspect) i = j;
        app.cfg.viewport.aspect = order[(i + 1) % 5];
        break;
    }
    case SDLK_F3:
        app.cfg.linear_filter = !app.cfg.linear_filter;
        SDL_SetTextureScaleMode(app.texture, app.cfg.linear_filter ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
        break;
    case SDLK_F4:
        app.cfg.viewport.scale = app.cfg.viewport.scale == ScaleMode::Fit ? ScaleMode::Integer
                               : app.cfg.viewport.scale == ScaleMode::Integer ? ScaleMode::Stretch : ScaleMode::Fit;
        break;
    case SDLK_F5: app.overlay_page = (app.overlay_page + 1) % 4; break;
    case SDLK_F11:
        app.cfg.window_mode = app.cfg.window_mode == WindowMode::Windowed ? WindowMode::Borderless : WindowMode::Windowed;
        apply_window_mode(app);
        break;
    case SDLK_RETURN:
        if (k.mod & SDL_KMOD_ALT) {
            app.cfg.window_mode = app.cfg.window_mode == WindowMode::Windowed ? WindowMode::Borderless : WindowMode::Windowed;
            apply_window_mode(app);
        }
        break;
    case SDLK_F12: save_screenshot(app); break;
    default: break;
    }
}

void draw_touch_controls(App& app, uint16_t held) {
    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
    for (const auto& b : app.touch.buttons()) {
        bool on = b.is_dpad ? (held & (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT)) != 0 : (held & b.mask) != 0;
        SDL_SetRenderDrawColor(app.renderer, 255, 255, 255, on ? 110 : 55);
        // Approximate circles with a filled square plus cross for the d-pad.
        SDL_FRect r{b.cx - b.r * 0.7f, b.cy - b.r * 0.7f, b.r * 1.4f, b.r * 1.4f};
        if (b.is_dpad) {
            SDL_FRect h{b.cx - b.r, b.cy - b.r * 0.33f, b.r * 2, b.r * 0.66f};
            SDL_FRect v{b.cx - b.r * 0.33f, b.cy - b.r, b.r * 0.66f, b.r * 2};
            SDL_RenderFillRect(app.renderer, &h);
            SDL_RenderFillRect(app.renderer, &v);
        } else {
            SDL_RenderFillRect(app.renderer, &r);
            SDL_SetRenderDrawColor(app.renderer, 0, 0, 0, 200);
            SDL_RenderDebugText(app.renderer, b.cx - 4.0f * float(SDL_strlen(b.label)), b.cy - 4, b.label);
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    App app;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    char* pref = SDL_GetPrefPath("ChaotixRecompiled", "ChaotixRecompiled");
    app.store = SaveStore(pref ? pref : "./");
    SDL_free(pref);
    if (!app.cfg.load(app.store.config_file())) {
        app.cfg.set_defaults();
        app.cfg.save(app.store.config_file());
    }
    bool force_interp = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--interp") force_interp = true;
        else if (a == "--fullscreen") app.cfg.window_mode = WindowMode::Fullscreen;
        else if (a == "--borderless") app.cfg.window_mode = WindowMode::Borderless;
        else if (a == "--windowed") app.cfg.window_mode = WindowMode::Windowed;
        else if (a == "--aspect" && i + 1 < argc) parse_aspect(argv[++i], app.cfg.viewport.aspect, app.cfg.viewport.custom_aspect);
        else if (a == "--touch") app.cfg.touch = TouchMode::On;
        else if (a == "--debug") app.cfg.debug_overlay = true;
        else if (a == "--autotest" && i + 1 < argc) app.autotest_frames = std::strtoull(argv[++i], nullptr, 10);
        else if (a == "--autotest-shot" && i + 1 < argc) app.autotest_shot = argv[++i];
        else if (a == "--window-size" && i + 1 < argc) {
            int w = 0, h = 0;
            if (std::sscanf(argv[++i], "%dx%d", &w, &h) == 2) { app.cfg.raw["cli.w"] = std::to_string(w); app.cfg.raw["cli.h"] = std::to_string(h); }
        } else if (a == "--press" && i + 1 < argc) {
            // FRAME:BUTTON[+BUTTON]:DURATION
            std::string spec = argv[++i];
            App::Press p{0, 4, 0};
            size_t c1 = spec.find(':'), c2 = spec.find(':', c1 + 1);
            p.frame = std::strtoull(spec.substr(0, c1).c_str(), nullptr, 10);
            std::string names = spec.substr(c1 + 1, c2 == std::string::npos ? std::string::npos : c2 - c1 - 1);
            if (c2 != std::string::npos) p.duration = std::strtoull(spec.substr(c2 + 1).c_str(), nullptr, 10);
            size_t st = 0;
            for (;;) {
                size_t plus = names.find('+', st);
                p.buttons |= pad_button_from_name(names.substr(st, plus == std::string::npos ? std::string::npos : plus - st).c_str());
                if (plus == std::string::npos) break;
                st = plus + 1;
            }
            app.script.push_back(p);
        }
    }

    std::string rom_path = find_rom(app, argc, argv);
    app.m = std::make_unique<Machine>();
    std::string err;
    if (rom_path.empty() || !app.m->load_rom(rom_path, &err)) {
        std::string msg = "Knuckles' Chaotix ROM not found or invalid.\n\n"
                          "This project does not include any game data. Provide your own legally obtained ROM by:\n"
                          " - passing its path on the command line, or\n"
                          " - setting RomPath in " + app.store.config_file() + ", or\n"
                          " - placing it in a '__ROM__' folder next to the executable.\n\n" + err;
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Knuckles' Chaotix Recompiled", msg.c_str(), nullptr);
        SDL_Quit();
        return 1;
    }
    if (app.m->rom.version == RomVersion::Unknown)
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Knuckles' Chaotix Recompiled",
                                 "This ROM does not match the verified Knuckles' Chaotix (Japan, USA) image.\n"
                                 "The recompiled code will not be used; the reference interpreter will run it instead.", nullptr);
    app.m->input.six_button[0] = app.cfg.six_button;
    app.m->reset();
    if (!app.autotest_frames) app.store.load_sram(*app.m);  // autotests are hermetic
    RecompStatus rs = install_recompiled_code(*app.m, app.cfg.use_recompiled && !force_interp);
    LOGI("app", "execution: %s", rs.description.c_str());

    int win_h = kActiveLines * app.cfg.window_scale;
    int win_w = int(std::lround(win_h * 4.0 / 3.0));
    if (app.cfg.raw.count("cli.w")) { win_w = std::atoi(app.cfg.raw["cli.w"].c_str()); win_h = std::atoi(app.cfg.raw["cli.h"].c_str()); }
    app.window = SDL_CreateWindow("Knuckles' Chaotix Recompiled", win_w, win_h, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!app.window) { std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    app.renderer = SDL_CreateRenderer(app.window, nullptr);
    if (!app.renderer) { std::fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return 1; }
    SDL_SetRenderVSync(app.renderer, app.cfg.vsync ? 1 : 0);
    LOGI("app", "renderer: %s", SDL_GetRendererName(app.renderer));
    app.texture = SDL_CreateTexture(app.renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, kScreenWidth, kScreenHeight);
    SDL_SetTextureScaleMode(app.texture, app.cfg.linear_filter ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
    apply_window_mode(app);
    if (app.cfg.audio && !app.autotest_frames) {
        // The machine produces 16-bit stereo at the YM2612 native rate; SDL
        // resamples to the device. Emulated time drives production, so the
        // audio device never changes game speed.
        SDL_AudioSpec spec{SDL_AUDIO_S16, 2, int(kAudioRate + 0.5)};
        app.audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (app.audio) {
            SDL_SetAudioStreamGain(app.audio, float(app.cfg.volume) / 100.0f);
            SDL_ResumeAudioStreamDevice(app.audio);
            app.m->audio_enabled = true;
        } else {
            LOGW("audio", "cannot open audio device: %s", SDL_GetError());
        }
    }
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
    app.touch_enabled = app.cfg.touch != TouchMode::Off;
#else
    app.touch_enabled = app.cfg.touch == TouchMode::On;
#endif

    const uint64_t freq = SDL_GetPerformanceFrequency();
    const double frame_ticks = double(freq) / kNtscFrameRate;
    uint64_t last = SDL_GetPerformanceCounter();
    double accumulator = 0;
    uint64_t fps_t0 = last, fps_frames = 0, fps_sims = 0;

    while (app.running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_EVENT_QUIT: app.running = false; break;
            case SDL_EVENT_KEY_DOWN: handle_key(app, e.key); break;
            case SDL_EVENT_GAMEPAD_ADDED:
                if (SDL_Gamepad* g = SDL_OpenGamepad(e.gdevice.which)) {
                    app.pads.push_back(g);
                    LOGI("input", "gamepad connected: %s", SDL_GetGamepadName(g));
                }
                break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                for (size_t i = 0; i < app.pads.size(); ++i)
                    if (SDL_GetGamepadID(app.pads[i]) == e.gdevice.which) { SDL_CloseGamepad(app.pads[i]); app.pads.erase(app.pads.begin() + long(i)); break; }
                break;
            case SDL_EVENT_FINGER_DOWN: case SDL_EVENT_FINGER_MOTION: {
                app.touch_enabled = app.cfg.touch != TouchMode::Off;
                int w = 0, h = 0;
                SDL_GetRenderOutputSize(app.renderer, &w, &h);
                app.fingers[e.tfinger.fingerID] = app.touch.hit(e.tfinger.x * float(w), e.tfinger.y * float(h));
                break;
            }
            case SDL_EVENT_FINGER_UP: case SDL_EVENT_FINGER_CANCELED: app.fingers.erase(e.tfinger.fingerID); break;
            case SDL_EVENT_WINDOW_FOCUS_LOST: case SDL_EVENT_DID_ENTER_BACKGROUND:
                if (app.m->sram_dirty) app.store.store_sram(*app.m);
                break;
            default: break;
            }
        }
        const bool* ks = SDL_GetKeyboardState(nullptr);
        app.fast_forward = ks[SDL_SCANCODE_TAB];

        // Unified input: keyboard | gamepads | touch -> InputState
        uint16_t buttons = keyboard_buttons(app);
        for (SDL_Gamepad* g : app.pads) buttons |= gamepad_buttons(g);
        uint16_t touch_held = 0;
        for (const auto& [id, b] : app.fingers) touch_held |= b;
        buttons |= touch_held;
        app.m->input.pad[0] = buttons;

        // Fixed-timestep simulation at the original frame rate, independent of
        // the display refresh rate (120/144 Hz monitors do not speed up the game).
        uint64_t now = SDL_GetPerformanceCounter();
        accumulator += double(now - last);
        last = now;
        if (accumulator > frame_ticks * 8) accumulator = frame_ticks * 8;  // after a stall, don't spiral
        int steps = 0;
        const int max_steps = app.fast_forward ? 8 : 3;
        uint64_t emu_t0 = SDL_GetPerformanceCounter();
        const bool autotest = app.autotest_frames != 0;
        while ((accumulator >= frame_ticks || app.fast_forward || autotest) && steps < (autotest ? 64 : max_steps)) {
            if (autotest && app.m->frame_count >= app.autotest_frames) break;
            // Scripted input is applied per emulated frame (deterministic).
            uint16_t scripted = 0;
            for (const auto& p : app.script)
                if (app.m->frame_count >= p.frame && app.m->frame_count < p.frame + p.duration) scripted |= p.buttons;
            app.m->input.pad[0] = uint16_t(buttons | scripted);
            app.m->run_frame();
            if (!app.fast_forward) accumulator -= frame_ticks;
            ++steps;
        }
        if (app.fast_forward) accumulator = 0;
        uint64_t emu_t1 = SDL_GetPerformanceCounter();
        if (steps) app.timing.emu_ms = double(emu_t1 - emu_t0) * 1000.0 / double(freq) / steps;
        fps_sims += uint64_t(steps);

        // Audio: hand over what the machine produced; keep ~50 ms queued with a
        // gentle rate adjustment instead of dropping or stretching audio.
        if (app.audio) {
            if (!app.m->audio_out.empty()) {
                if (app.fast_forward) app.m->audio_out.clear();
                else {
                    SDL_PutAudioStreamData(app.audio, app.m->audio_out.data(), int(app.m->audio_out.size() * sizeof(int16_t)));
                    app.m->audio_out.clear();
                }
            }
            const double target = kAudioRate * 0.05 * 4;  // bytes queued for 50 ms (stereo s16)
            double queued = double(SDL_GetAudioStreamQueued(app.audio));
            double err = (queued - target) / target;
            float ratio = float(1.0 + std::clamp(err * 0.01, -0.01, 0.01));
            SDL_SetAudioStreamFrequencyRatio(app.audio, ratio);
            if (queued > target * 6) SDL_ClearAudioStream(app.audio);  // after a stall
        } else {
            app.m->audio_out.clear();
        }

        // SRAM autosave shortly after the game writes it.
        if (app.m->sram_dirty && !app.autotest_frames) {
            if (!app.sram_dirty_since) app.sram_dirty_since = now;
            else if (double(now - app.sram_dirty_since) / double(freq) > 1.0) { app.store.store_sram(*app.m); app.sram_dirty_since = 0; }
        }

        // Present
        uint64_t pr_t0 = SDL_GetPerformanceCounter();
        int ow = 0, oh = 0;
        SDL_GetRenderOutputSize(app.renderer, &ow, &oh);
        SDL_SetRenderDrawColor(app.renderer, 0, 0, 0, 255);
        SDL_RenderClear(app.renderer);
        SDL_UpdateTexture(app.texture, nullptr, app.m->framebuffer, kScreenWidth * 4);
        ViewportResult vp = compute_viewport(app.cfg.viewport, ow, oh, app.m->fb_width, app.m->fb_height);
        SDL_FRect src{0, 0, float(app.m->fb_width), float(app.m->fb_height)};
        SDL_FRect dst{float(vp.image.x), float(vp.image.y), float(vp.image.w), float(vp.image.h)};
        SDL_RenderTexture(app.renderer, app.texture, &src, &dst);
        if (app.touch_enabled) {
            app.touch.layout(ow, oh);
            draw_touch_controls(app, touch_held);
        }
        if (app.cfg.debug_overlay) {
            const SDL_DisplayMode* dm = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(app.window));
            app.timing.refresh_hz = dm ? unsigned(dm->refresh_rate + 0.5f) : 0;
            app.timing.fast_forward = app.fast_forward;
            char vline[160];
            std::snprintf(vline, sizeof vline, "video: %dx%d out, image %.0fx%.0f, aspect %s, %s, %s, %s", ow, oh, vp.image.w, vp.image.h,
                          aspect_name(app.cfg.viewport.aspect), scale_name(app.cfg.viewport.scale),
                          app.cfg.linear_filter ? "linear" : "nearest", window_mode_name(app.cfg.window_mode));
            auto lines = build_debug_overlay(*app.m, app.timing, app.overlay_page, vline, app.watches);
            float scale = std::max(1.0f, float(oh) / 540.0f);
            SDL_SetRenderScale(app.renderer, scale, scale);
            SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(app.renderer, 0, 0, 0, 170);
            SDL_FRect bg{0, 0, 8.0f * 100, 10.0f * float(lines.size()) + 4};
            SDL_RenderFillRect(app.renderer, &bg);
            SDL_SetRenderDrawColor(app.renderer, 255, 255, 160, 255);
            for (size_t i = 0; i < lines.size(); ++i) SDL_RenderDebugText(app.renderer, 4, 4 + 10.0f * float(i), lines[i].c_str());
            SDL_SetRenderScale(app.renderer, 1, 1);
        }
        if (app.autotest_frames && app.m->frame_count >= app.autotest_frames) {
            if (!app.autotest_shot.empty()) capture_output(app);
            app.running = false;
        }
        SDL_RenderPresent(app.renderer);
        app.timing.present_ms = double(SDL_GetPerformanceCounter() - pr_t0) * 1000.0 / double(freq);
        ++fps_frames;
        if (now - fps_t0 > freq) {
            double s = double(now - fps_t0) / double(freq);
            app.timing.render_fps = double(fps_frames) / s;
            app.timing.sim_fps = double(fps_sims) / s;
            fps_frames = fps_sims = 0;
            fps_t0 = now;
        }
        if (!app.cfg.vsync && steps == 0) SDL_Delay(1);
    }

    if (!app.autotest_frames) {
        if (app.m->sram_dirty) app.store.store_sram(*app.m);
        app.cfg.save(app.store.config_file());
    }
    if (app.audio) SDL_DestroyAudioStream(app.audio);
    for (SDL_Gamepad* g : app.pads) SDL_CloseGamepad(g);
    SDL_DestroyTexture(app.texture);
    SDL_DestroyRenderer(app.renderer);
    SDL_DestroyWindow(app.window);
    SDL_Quit();
    return 0;
}
