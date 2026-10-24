#include "frontend/backdrop.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>

#include "input/input.h"
#include "runtime/patches.h"
#include "runtime/sprite_record.h"
#include "runtime/sprite_replace.h"
#include "runtime/stage_select.h"
#include "runtime/system.h"

namespace chaotix {

namespace {

// The 32X banks left out of a backdrop: the eight characters (both sets), the
// HUD's labels and digits (docs/SPRITES_32X.md), and the seven dots of the
// tether between the two characters (bank 29, scaled, found with
// --sprites-log in every zone).
constexpr int kCharacterBanks = 16;
constexpr int kHudBank = 0x13;
constexpr int kTetherBank = 29;

std::shared_ptr<const SpritePack> hide_characters_and_hud() {
    auto pack = std::make_shared<SpritePack>();
    // A one-pixel image whose pixel is transparent: drawn in place of the
    // graphic, it draws nothing.
    SpriteImage nothing;
    nothing.width = nothing.height = 1;
    nothing.px.assign(1, 0);
    for (int frame = 0; frame < 256; ++frame) {
        for (int bank = 0; bank < kCharacterBanks; ++bank) pack->add(bank, frame, nothing);
        pack->add(kHudBank, frame, nothing);
        pack->add(kTetherBank, frame, nothing);
    }
    return pack;
}

} // namespace

const std::vector<BackdropSpec>& backdrop_specs() {
    // Times of day chosen for variety across the cycle, Marina Madness at
    // sunset because that is the picture issue #8 asked for. The levels are
    // ones whose starting view is a good picture: Botanic Base 1 starts by
    // its left edge, which shows tiles from beyond it. WORLD ENTRANCE is the
    // lobby (place 7, level 0: the hall with EXIT and PLAY), which the game
    // keeps 4:3; ISOLATED ISLAND is the outdoor level the game opens with
    // (INTRODUCTION, place 6, level 0), whose start is a clean wide view.
    static const std::vector<BackdropSpec> specs = {
        {4, 2, 4, "marina_madness_sunset", true, "MarinaMadness", "MARINA MADNESS"},
        {0, 3, 0, "botanic_base_morning", true, "BotanicBase", "BOTANIC BASE"},
        {1, 1, 6, "speed_slider_night", true, "SpeedSlider", "SPEED SLIDER"},
        {3, 1, 4, "techno_tower_sunset", true, "TechnoTower", "TECHNO TOWER"},
        {2, 2, 2, "amazing_arena_day", true, "AmazingArena", "AMAZING ARENA"},
        {7, 0, 4, "world_entrance_sunset", false, "WorldEntrance", "WORLD ENTRANCE"},
        {6, 0, 2, "isolated_island_day", true, "IsolatedIsland", "ISOLATED ISLAND"},
    };
    return specs;
}

const std::vector<int>& backdrop_menu_order() {
    static const std::vector<int> order = [] {
        static const char* const zones[] = {"WorldEntrance", "IsolatedIsland", "BotanicBase", "MarinaMadness",
                                            "SpeedSlider", "AmazingArena", "TechnoTower"};
        std::vector<int> o;
        for (const char* z : zones)
            if (const int i = backdrop_find_zone(z); i >= 0) o.push_back(i);
        return o;
    }();
    return order;
}

int backdrop_find_zone(const std::string& zone) {
    const auto& specs = backdrop_specs();
    for (size_t i = 0; i < specs.size(); ++i) {
        const std::string z = specs[i].zone;
        if (z.size() == zone.size() &&
            std::equal(z.begin(), z.end(), zone.begin(), [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
            }))
            return int(i);
    }
    return -1;
}

BackdropSpec stage_backdrop_spec(uint16_t place, uint16_t level, uint16_t attime) {
    BackdropSpec s;
    s.place = place;
    s.level = level;
    s.attime = attime;
    s.wide = false;
    s.walk = kStageBackdropWalk;
    s.walk_from = kStageBackdropWalkFrom;
    s.settle = kStageBackdropSettle;
    return s;
}

std::string stage_backdrop_key(uint16_t place, uint16_t level, uint16_t attime) {
    char b[48];
    std::snprintf(b, sizeof b, "stage_p%02u_l%u_t%u", unsigned(place), unsigned(level), unsigned(attime));
    return b;
}

bool render_backdrop(const Rom& rom, const BackdropSpec& spec, BackdropImage& out, std::string* why,
                     BackdropControl* control) {
    auto fail = [why](const char* m) { if (why) *why = m; return false; };
    stage_select::Request request;
    request.place = spec.place;
    request.level = spec.level;
    request.attime = spec.attime;
    if (!stage_select::valid(request)) return fail("not a stage the game has");

    // A machine of its own, on the reference interpreters: the recompiled
    // code serves one machine at a time, and that one is being played.
    auto m = std::make_unique<Machine>();
    m->load_rom(Rom(rom));
    m->reset();
    m->wide_enabled = spec.wide;
    m->wide_extra = spec.wide ? std::min(kBackdropWideExtra, patches::kMaxWideExtra) : 0;
    auto recorder = std::make_shared<SpriteRecorder>();
    recorder->set_pack(hide_characters_and_hud());
    m->sprites = recorder;

    // The request is taken at the game's mode dispatcher; the title screen
    // goes back there once Start has been tapped (as TIME ATTACK does it).
    m->stage_request = request;
    m->stage_pending = true;
    int kick = 0;
    uint64_t taken = 0;
    const uint64_t kGiveUp = 1020 + uint64_t(spec.settle);
    while (m->frame_count < kGiveUp) {
        if (control) {
            // Paused between frames, so the game gets the CPU back within one
            // backdrop frame (well under a millisecond on a PC).
            while (control->pause.load() && !control->cancel.load())
                std::this_thread::sleep_for(std::chrono::milliseconds(15));
            if (control->cancel.load()) return fail("cancelled");
        }
        uint16_t pad = 0;
        if (m->stage_pending) {
            pad = kick % 24 < 6 ? uint16_t(PAD_START) : uint16_t(0);
            ++kick;
        } else if (!taken) {
            taken = m->frame_count;
        } else if (spec.walk > 0 && m->frame_count >= taken + uint64_t(spec.walk_from) &&
                   m->frame_count < taken + uint64_t(spec.walk_from) + uint64_t(spec.walk)) {
            pad = uint16_t(PAD_RIGHT);
        }
        m->input.pad[0] = pad;
        m->input.pad[1] = 0;
        m->run_frame();
        if (control) control->frames.fetch_add(1);
        if (taken && m->frame_count >= taken + uint64_t(spec.settle)) break;
    }
    if (!taken || m->frame_count < taken + uint64_t(spec.settle)) return fail("the stage did not start");
    out.width = m->fb_width;
    out.height = m->fb_height;
    out.pixels.resize(size_t(out.width) * size_t(out.height));
    for (int y = 0; y < out.height; ++y)
        std::copy_n(m->framebuffer + size_t(y) * kScreenWidth, out.width, out.pixels.data() + size_t(y) * size_t(out.width));
    // A stage whose picture is not in the frame buffer (the bonus stage
    // comes out black) has nothing to show.
    size_t dark = 0;
    for (uint32_t p : out.pixels) dark += (p & 0xF0F0F0u) == 0;
    if (dark * 100 >= out.pixels.size() * 99) return fail("the picture is blank");
    out.hidden_draws = out.sprite_draws = 0;
    for (const SpriteDraw& d : recorder->displayed(*m).draws) {
        ++out.sprite_draws;
        out.hidden_draws += (d.bank >= 0 && d.bank < kCharacterBanks) || d.bank == kHudBank || d.bank == kTetherBank;
    }
    return true;
}

uint32_t backdrop_tint(const BackdropImage& image) {
    if (image.pixels.empty()) return 0xFF0F111A;
    uint64_t r = 0, g = 0, b = 0;
    for (uint32_t p : image.pixels) {
        r += (p >> 16) & 0xFF;
        g += (p >> 8) & 0xFF;
        b += p & 0xFF;
    }
    const uint64_t n = image.pixels.size();
    // A third of the average: dark enough under white and yellow text, and
    // still the zone's own hue.
    const uint32_t R = uint32_t(r / n / 3), G = uint32_t(g / n / 3), B = uint32_t(b / n / 3);
    return 0xFF000000u | R << 16 | G << 8 | B;
}

uint32_t backdrop_accent(const BackdropImage& image) {
    // Vivid pixels by hue, in twelve bins; the fullest bin's average colour,
    // scaled up until its brightest channel is full.
    uint64_t sum[12][3] = {}, count[12] = {};
    for (uint32_t p : image.pixels) {
        const int r = int(p >> 16 & 0xFF), g = int(p >> 8 & 0xFF), b = int(p & 0xFF);
        const int hi = std::max({r, g, b}), lo = std::min({r, g, b});
        if (hi < 96 || hi - lo < hi / 2) continue;  // dark or greyish
        float h;
        const float d = float(hi - lo);
        if (hi == r) h = float(g - b) / d;
        else if (hi == g) h = 2.0f + float(b - r) / d;
        else h = 4.0f + float(r - g) / d;
        const int bin = (int((h < 0 ? h + 6.0f : h) * 2.0f) + 12) % 12;
        sum[bin][0] += uint64_t(r);
        sum[bin][1] += uint64_t(g);
        sum[bin][2] += uint64_t(b);
        ++count[bin];
    }
    int best = -1;
    for (int i = 0; i < 12; ++i)
        if (count[i] && (best < 0 || count[i] > count[best])) best = i;
    if (best < 0 || count[best] * 50 < image.pixels.size()) return 0xFF609AFF;
    double c[3];
    for (int k = 0; k < 3; ++k) c[k] = double(sum[best][k]) / double(count[best]);
    const double top = std::max({c[0], c[1], c[2], 1.0});
    uint32_t out = 0xFF000000u;
    for (int k = 0; k < 3; ++k) out |= uint32_t(std::min(255.0, c[k] * 255.0 / top)) << (16 - 8 * k);
    return out;
}

} // namespace chaotix
