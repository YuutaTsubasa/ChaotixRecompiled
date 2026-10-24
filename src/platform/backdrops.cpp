#include "platform/backdrops.h"

#include <chrono>
#include <filesystem>
#include <utility>

#include "renderer/image_io.h"
#include "runtime/log.h"

namespace chaotix {

namespace {

// Bumped whenever the way a picture is made changes, so that older cached
// pictures are made again rather than kept.
constexpr const char* kRecipe = "v2";  // v2: the tether is left out too

} // namespace

void MenuBackdrops::configure(const Rom& rom, const std::string& cache_root) {
    shutdown();
    rom_ = std::make_unique<Rom>(rom);
    // The image actually played: a ROM patch can change the levels' art.
    const std::string& id = rom.patched.empty() ? rom.sha1 : rom.patched_sha1;
    dir_ = cache_root + std::string(kRecipe) + "-" + id.substr(0, 12) + "/";
    entries_.clear();
    for (const BackdropSpec& spec : backdrop_specs()) {
        Entry e;
        e.spec = spec;
        e.file = spec.key;
        entries_.push_back(e);
    }
    loaded_ = false;
    current_ = -1;
    demand_ = -1;
}

std::string MenuBackdrops::path_of(const Entry& e) const { return dir_ + e.file + ".png"; }

bool MenuBackdrops::load_cached(SDL_Renderer* renderer, Entry& e) {
    PngImage png;
    std::error_code ec;
    if (!std::filesystem::exists(std::filesystem::u8path(path_of(e)), ec)) return false;
    if (!read_png(path_of(e), png, nullptr) || png.width <= 0 || png.height <= 0) return false;
    BackdropImage image;
    image.width = png.width;
    image.height = png.height;
    image.pixels = std::move(png.argb);
    make_texture(renderer, e, image);
    return e.texture != nullptr;
}

void MenuBackdrops::make_texture(SDL_Renderer* renderer, Entry& e, const BackdropImage& image) {
    SDL_Texture* t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STATIC, image.width,
                                       image.height);
    if (!t) {
        LOGW("menu", "backdrop texture: %s", SDL_GetError());
        return;
    }
    SDL_UpdateTexture(t, nullptr, image.pixels.data(), image.width * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    // The picture is scaled by a fraction to cover the window and lies under
    // a translucent tint: smoothed, it stays quiet behind the text.
    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
    e.texture = t;
    e.width = image.width;
    e.height = image.height;
    e.tint = backdrop_tint(image);
    e.accent = backdrop_accent(image);
}

void MenuBackdrops::update(SDL_Renderer* renderer, bool wanted, bool menu_open) {
    if (!rom_ || !renderer) return;
    renderer_ = renderer;
    // A stage asked for since the last update (issue #53); the page asks
    // again every frame it is shown, so one it no longer shows is not made.
    int demand = demand_;
    demand_ = -1;
    if (demand >= 0 && (entries_[size_t(demand)].texture || entries_[size_t(demand)].failed)) demand = -1;
    // Held between its frames whenever nobody is looking at a menu, or the
    // setting is off and no stage is asked for: slow machines have no core to
    // spare for it in play (issue #31), and one backdrop frame is well under
    // a game frame.
    control_.pause = !((wanted || demand >= 0) && menu_open);
    // The picture wanted first: a stage asked for, else a zone chosen. One
    // being made for anything else is given up (and made again later if it
    // is still wanted).
    int first = demand;
    if (first < 0 && wanted && chosen_ >= 0 && !entries_[size_t(chosen_)].texture) first = chosen_;
    if (first >= 0 && worker_.joinable() && working_index_ != first) control_.cancel = true;
    if (worker_.joinable() && !working_.load()) worker_.join();
    std::vector<Finished> done;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        done.swap(finished_);
    }
    for (Finished& f : done) {
        Entry& e = entries_[f.index];
        if (f.cancelled) continue;  // set aside for a chosen zone, not failed
        if (!f.ok) {
            e.failed = true;
            LOGW("menu", "backdrop %s: %s", e.file.c_str(), f.why.c_str());
            continue;
        }
        LOGI("menu", "backdrop %s made in %.0f ms", e.file.c_str(), f.ms);
        // Written here rather than on the worker: the PNG writer's tables are
        // shared, and this takes a few milliseconds once per picture.
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::u8path(dir_), ec);
        if (!write_png(path_of(e), f.image.pixels.data(), f.image.width, f.image.height, f.image.width))
            LOGW("menu", "cannot write %s", path_of(e).c_str());
        make_texture(renderer, e, f.image);
        e.looked = true;
    }
    if (wanted && !loaded_) {
        loaded_ = true;
        for (Entry& e : entries_)
            if (!e.on_demand && !e.texture) load_cached(renderer, e);
    }
    if (wanted) {
        if (chosen_ >= 0) {
            current_ = chosen_;
        } else if (current_ < 0 || size_t(current_) >= entries_.size() || !entries_[size_t(current_)].texture ||
                   entries_[size_t(current_)].on_demand) {
            current_ = -1;
            for (size_t i = 0; i < entries_.size(); ++i)
                if (entries_[i].texture && !entries_[i].on_demand) { current_ = int(i); break; }
        }
    }
    if (worker_.joinable() || !menu_open) return;
    if (demand >= 0) {
        start(size_t(demand));
        return;
    }
    if (!wanted) return;
    // One picture at a time, in the list's order, so the first is ready as
    // soon as possible and a slow device is never running more than one.
    // With one zone chosen, only that one is made (issue #37).
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].on_demand) continue;
        if (chosen_ >= 0 && int(i) != chosen_) continue;
        if (entries_[i].texture || entries_[i].failed) continue;
        start(i);
        break;
    }
}

void MenuBackdrops::start(size_t i) {
    working_ = true;
    working_index_ = int(i);
    control_.cancel = false;
    const BackdropSpec spec = entries_[i].spec;
    const Rom* rom = rom_.get();
    worker_ = std::thread([this, i, spec, rom] {
        // Below the game and the audio: the picture can wait.
        SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_LOW);
        Finished f;
        f.index = i;
        const auto t0 = std::chrono::steady_clock::now();
        f.ok = render_backdrop(*rom, spec, f.image, &f.why, &control_);
        f.cancelled = !f.ok && control_.cancel.load();
        f.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            finished_.push_back(std::move(f));
        }
        working_ = false;
    });
}

bool MenuBackdrops::stage(uint16_t place, uint16_t level, uint16_t attime, Shown& out, bool* exact, bool* none) {
    if (exact) *exact = false;
    if (none) *none = false;
    if (!rom_) return false;
    ++asks_;
    const std::string key = stage_backdrop_key(place, level, attime);
    size_t index = entries_.size();
    for (size_t i = 0; i < entries_.size(); ++i)
        if (entries_[i].on_demand && entries_[i].file == key) { index = i; break; }
    if (index == entries_.size()) {
        Entry e;
        e.spec = stage_backdrop_spec(place, level, attime);
        e.file = key;
        e.on_demand = true;
        entries_.push_back(e);
    }
    Entry& e = entries_[index];
    e.used = asks_;
    if (!e.texture && !e.looked && renderer_) {
        e.looked = true;
        if (load_cached(renderer_, e)) {
            // Too many kept: the one asked for longest ago goes (its PNG
            // stays, so it comes back at once).
            int kept = 0;
            size_t oldest = entries_.size();
            for (size_t i = 0; i < entries_.size(); ++i) {
                if (!entries_[i].on_demand || !entries_[i].texture) continue;
                ++kept;
                if (i != index && (oldest == entries_.size() || entries_[i].used < entries_[oldest].used)) oldest = i;
            }
            if (kept > kStageTextures && oldest < entries_.size()) {
                SDL_DestroyTexture(entries_[oldest].texture);
                entries_[oldest].texture = nullptr;
                entries_[oldest].looked = false;
            }
        }
    }
    auto shown = [&out](const Entry& x) {
        out.texture = x.texture;
        out.width = float(x.width);
        out.height = float(x.height);
        out.tint = x.tint;
        out.accent = x.accent;
        return true;
    };
    if (e.texture) {
        if (exact) *exact = true;
        return shown(e);
    }
    if (!e.failed) demand_ = int(index);
    else if (none) *none = true;
    // Meanwhile: the same place at another level or time of day, preferring
    // the same level, then one of the menu's own pictures of it.
    const Entry* best = nullptr;
    int best_rank = 0;
    for (const Entry& x : entries_) {
        if (!x.texture || x.spec.place != place) continue;
        const int rank = x.on_demand ? (x.spec.level == level ? 3 : 2) : 1;
        if (rank > best_rank) { best = &x; best_rank = rank; }
    }
    return best ? shown(*best) : false;
}

std::string MenuBackdrops::making() const {
    if (!worker_.joinable() || !working_.load() || working_index_ < 0 || size_t(working_index_) >= entries_.size())
        return "";
    const Entry& e = entries_[size_t(working_index_)];
    return e.on_demand ? e.file : "";
}

void MenuBackdrops::choose(int index) {
    if (index < 0 || size_t(index) >= backdrop_specs().size()) index = -1;
    if (index == chosen_) return;
    chosen_ = index;
    if (chosen_ >= 0) current_ = chosen_;  // that picture, or plain until it is made
}

void MenuBackdrops::advance() {
    if (entries_.empty() || chosen_ >= 0) return;
    for (size_t step = 1; step <= entries_.size(); ++step) {
        const size_t i = (size_t(current_ < 0 ? 0 : current_) + step) % entries_.size();
        if (entries_[i].texture && !entries_[i].on_demand) { current_ = int(i); return; }
    }
}

bool MenuBackdrops::current(Shown& out) const {
    if (current_ < 0 || size_t(current_) >= entries_.size() || !entries_[size_t(current_)].texture) return false;
    const Entry& e = entries_[size_t(current_)];
    out.texture = e.texture;
    out.width = float(e.width);
    out.height = float(e.height);
    out.tint = e.tint;
    out.accent = e.accent;
    return true;
}

int MenuBackdrops::ready_count() const {
    int n = 0;
    for (const Entry& e : entries_) n += e.texture != nullptr && !e.on_demand;
    return n;
}

void MenuBackdrops::shutdown() {
    control_.cancel = true;  // also wakes a paused worker
    if (worker_.joinable()) worker_.join();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        finished_.clear();
    }
    for (Entry& e : entries_) {
        if (e.texture) SDL_DestroyTexture(e.texture);
        e.texture = nullptr;
        e.looked = false;
    }
    current_ = -1;
    loaded_ = false;
}

} // namespace chaotix
