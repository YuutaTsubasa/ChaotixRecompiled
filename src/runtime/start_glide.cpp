#include "start_glide.h"

#include "sprite_record.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace chaotix {

namespace {

constexpr uint8_t kColour[2] = {0x12, 0x24};  // the player's, the partner's

bool is_character(const SpriteDraw& d, int who) { return d.colour == kColour[who] && d.bank >= 0 && d.bank < 16; }

int32_t mix(int32_t a, int32_t b, double e) { return int32_t(std::lround(a + (b - a) * e)); }

} // namespace

StartGlide::Place StartGlide::place_of(const SpriteList& list, int who) {
    Place p;
    for (const SpriteDraw& d : list.draws) {
        if (!is_character(d, who)) continue;
        p.seen = true;
        p.x = d.x;
        p.y = d.y;
        p.s = d.kind == SpriteKind::Scaled ? d.sx : 0x100;
        break;
    }
    return p;
}

bool StartGlide::picture(const SpriteList& list, std::vector<SpriteMove>& moves) {
    moves.assign(list.draws.size(), SpriteMove{});
    bool any = false;
    for (int who = 0; who < 2; ++who) {
        const Place now = place_of(list, who);
        const Place before = last_[who];
        last_[who] = now;
        Glide& g = glide_[who];
        if (!now.seen) {
            g.step = 0;
            continue;
        }
        if (before.seen && std::abs(before.s - 0x100) >= kMinScaleJump && now.s == 0x100 &&
            (std::abs(now.x - before.x) >= kMinJump || std::abs(now.y - before.y) >= kMinJump)) {
            g.step = 1;
            g.from = before;
            // Drawn larger than life (S below 0x100), a character is nearer
            // than the whirlwind and hidden in it: it starts at its own size,
            // not as a giant popping out.
            g.from.s = std::max<int32_t>(before.s, 0x100);
            ++glides;
        } else if (g.step) {
            ++g.step;
        }
        if (!g.step) continue;
        if (g.step >= kPictures) {
            g.step = 0;
            continue;
        }
        // Smoothstep: no jolt at either end.
        const double t = double(g.step) / kPictures, e = t * t * (3 - 2 * t);
        const int32_t x = mix(g.from.x, now.x, e), y = mix(g.from.y, now.y, e), s = mix(g.from.s, now.s, e);
        // A scale is a step (8.8): larger draws smaller. Offsets from the
        // first draw shrink as the scale grows; the scales grow with it.
        const double k = double(s) / now.s;
        for (size_t j = 0; j < list.draws.size(); ++j) {
            const SpriteDraw& d = list.draws[j];
            if (!is_character(d, who)) continue;
            const int32_t sx = d.kind == SpriteKind::Scaled ? d.sx : 0x100;
            const int32_t sy = d.kind == SpriteKind::Scaled ? d.sy : 0x100;
            SpriteMove& mv = moves[j];
            mv.on = true;
            mv.x = x + int32_t(std::lround((d.x - now.x) / k));
            mv.y = y + int32_t(std::lround((d.y - now.y) / k));
            mv.sx = int32_t(std::lround(sx * k));
            mv.sy = int32_t(std::lround(sy * k));
            any = true;
        }
    }
    return any;
}

bool StartGlide::watching() const {
    for (int who = 0; who < 2; ++who)
        if ((last_[who].seen && last_[who].s != 0x100) || glide_[who].step) return true;
    return false;
}

void StartGlide::reset() {
    for (int who = 0; who < 2; ++who) {
        last_[who] = Place{};
        glide_[who] = Glide{};
    }
}

} // namespace chaotix
