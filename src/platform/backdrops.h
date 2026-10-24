#pragma once

// The menu's zone pictures (issue #8, frontend/backdrop.h), as textures.
//
// Each picture is made once per ROM by a worker thread running a machine of
// its own, then kept as a PNG in the user data folder (Cache/Backdrops), so
// later starts load it at once. The playing machine is never touched: the
// worker only reads a copy of the ROM. Until a picture exists the menu keeps
// its plain look, so nothing waits for the worker.

#include <SDL3/SDL.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "frontend/backdrop.h"
#include "runtime/rom.h"

namespace chaotix {

class MenuBackdrops {
public:
    MenuBackdrops() = default;
    MenuBackdrops(const MenuBackdrops&) = delete;
    MenuBackdrops& operator=(const MenuBackdrops&) = delete;
    ~MenuBackdrops() { shutdown(); }

    // The ROM the pictures are made from (copied) and the folder they are
    // cached in; a subfolder per ROM image, so a patched ROM gets its own.
    void configure(const Rom& rom, const std::string& cache_root);

    // Main thread, once a frame. While `wanted`, loads the cached pictures,
    // starts the worker on the first one missing, and turns what it finished
    // into a texture (and a PNG in the cache). Nothing happens while not
    // wanted, so the setting turned off costs nothing. The worker only runs
    // while `menu_open`: while the game is played it is paused between two
    // of its frames (not cancelled), so it never takes CPU from the game, and
    // it carries on when a menu opens again.
    void update(SDL_Renderer* renderer, bool wanted, bool menu_open);
    // Whether the worker is currently held (for tests and logs).
    bool paused() const { return control_.pause.load(); }
    // Whether a picture is being made (running or paused), and how many
    // frames of its machine have been emulated so far.
    bool busy() const { return worker_.joinable(); }
    uint64_t frames_emulated() const { return control_.frames.load(); }
    // The next picture, when the menu opens. Nothing while one zone is chosen.
    void advance();
    // MENU BACKGROUND (issue #37): -1 cycles through every zone; an index into
    // backdrop_specs() shows only that zone, and only its picture is made (the
    // menu is plain until it is ready). Takes effect at once, so the menu
    // shows the choice while it is being made.
    void choose(int index);
    int chosen() const { return chosen_; }

    struct Shown {
        SDL_Texture* texture = nullptr;
        float width = 0, height = 0;
        uint32_t tint = 0, accent = 0;  // ARGB (backdrop_tint, backdrop_accent)
    };
    // False while no picture is ready.
    bool current(Shown& out) const;
    // The menu's own pictures that are ready (not the stages' below).
    int ready_count() const;

    // TIME ATTACK and LEVEL SELECT (issue #53): the picture of a stage at a
    // time of day (stage_backdrop_spec), asked for once a frame while the
    // page shows it. True with it in `out` once it is ready -- from the cache,
    // or made. Until then the next update makes it, ahead of the menu's own
    // pictures and whatever the MENU BACKGROUND setting, giving up one being
    // made for a stage asked for before; meanwhile `out` holds another
    // picture of the same place if there is one ready (*exact false), and the
    // result is false when there is none. Pictures are made only while a
    // menu is open, like the rest. A stage that cannot be made (*none) is not
    // tried again this run.
    bool stage(uint16_t place, uint16_t level, uint16_t attime, Shown& out, bool* exact, bool* none = nullptr);
    // The stage picture being made (for tests and logs), empty when none.
    std::string making() const;

    // Stops the worker (it gives up at its next frame) and frees the textures.
    void shutdown();

private:
    struct Entry {
        BackdropSpec spec;
        SDL_Texture* texture = nullptr;
        int width = 0, height = 0;
        uint32_t tint = 0, accent = 0;
        bool failed = false;   // the worker could not make it; not tried again this run
        std::string file;      // the cache's file name stem
        // A stage's picture (stage()), made only when asked for.
        bool on_demand = false;
        bool looked = false;   // the cache has been looked in for it
        uint64_t used = 0;     // when stage() last asked for it
    };
    // Stage pictures kept as textures; the ones asked for longest ago are let
    // go beyond this (their PNGs stay in the cache).
    static constexpr int kStageTextures = 24;
    struct Finished {
        size_t index = 0;
        bool ok = false;
        bool cancelled = false;  // given up for a chosen zone (or at shutdown)
        std::string why;
        double ms = 0;
        BackdropImage image;
    };
    // Starts the worker on entry i (it must be free).
    void start(size_t i);
    // Loads the entry's PNG from the cache, if it is there.
    bool load_cached(SDL_Renderer* renderer, Entry& e);
    void make_texture(SDL_Renderer* renderer, Entry& e, const BackdropImage& image);
    std::string path_of(const Entry& e) const;

    std::vector<Entry> entries_;
    std::unique_ptr<Rom> rom_;
    std::string dir_;
    bool loaded_ = false;
    int current_ = -1;
    int chosen_ = -1;  // choose()
    SDL_Renderer* renderer_ = nullptr;  // the last update's
    int demand_ = -1;       // the stage asked for since the last update
    uint64_t asks_ = 0;     // stage() calls, for `used`

    std::thread worker_;
    int working_index_ = -1;  // the entry the worker is making (main thread)
    BackdropControl control_;
    std::atomic<bool> working_{false};
    std::mutex mutex_;
    std::vector<Finished> finished_;  // guarded by mutex_
};

} // namespace chaotix
