#include "sprite_replace.h"

#include "log.h"
#include "patches.h"
#include "system.h"

#include <algorithm>
#include <climits>
#include <cstring>

namespace chaotix {

namespace {

inline int s16(const uint8_t* p) { return int16_t(uint16_t(p[0] << 8 | p[1])); }
inline int s8(uint8_t v) { return int8_t(v); }
// A y word (lines x 256) as a line: its high byte, as the scaled blitters
// read it.
inline int line_of(int y) { return s8(uint8_t(uint16_t(y) >> 8)); }

void put16(std::vector<uint8_t>& v, int x) {
    v.push_back(uint8_t(uint16_t(x) >> 8));
    v.push_back(uint8_t(x));
}

inline uint32_t rgb555(uint16_t c) {
    uint32_t r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
    r = (r << 3) | (r >> 2); g = (g << 3) | (g >> 2); b = (b << 3) | (b >> 2);
    return (r << 16) | (g << 8) | b;
}

// Frame buffer offsets as Machine::fb_in_margin and fb_in_picture see them.
inline bool in_margin(uint32_t o) {
    const int32_t rel = int32_t(o) - int32_t(patches::kFbLineBase);
    const int line = rel >= 0 ? rel / int32_t(patches::kFbLineStride) : -1;
    return line < 0 || line >= kActiveLines || (uint32_t(rel) & (patches::kFbLineStride - 1)) >= uint32_t(kNativeWidth);
}
inline bool in_picture(uint32_t o) {
    return o >= patches::kFbLineBase && o < patches::kFbLineBase + patches::kFbLineStride * uint32_t(kActiveLines);
}

// Where a blitter's byte lands (Machine::fb_write for the overwrite image):
// the frame buffer or the margin shadow, as an index into the two planes.
inline uint32_t place(uint8_t route, uint32_t o) {
    const bool shadow = route == 1 ? in_margin(o) : route == 2 ? in_picture(o) && in_margin(o) : false;
    return (shadow ? SpriteLayer::kPlane : 0) + o;
}

constexpr int kNoColumn = INT_MIN;

// One span of `out`: columns [s, e) of a line, the pixels from `px(x)`.
template <class F>
void span(std::vector<uint8_t>& out, int s, int e, int line, F px) {
    out.push_back(uint8_t(s));
    out.push_back(uint8_t(e));
    put16(out, line * 256);
    for (int x = s; x < e; ++x) out.push_back(px(x));
}

// A span from the first to the last pixel `want` holds on a row, made the
// way the game's are: starting at an even column and of even length (the
// blitters copy words), inside [x0, x1) where it can be. The columns added
// hold 0, which the blitters do not draw.
bool aligned(int first, int last, int x0, int x1, int* s, int* e) {
    *s = first & ~1;
    if (*s < x0) *s = x0;
    *e = last + 1;
    if ((*e - *s) & 1) {
        if (*e < x1) ++*e;
        else if (*s > x0) --*s;
        else ++*e;
    }
    return *s < *e;
}

} // namespace

const SpriteImage* SpritePack::find(const SpriteDraw& d) const {
    if (d.bank >= 0) {
        auto it = by_slot_.find(uint32_t(d.bank) << 8 | uint32_t(d.frame));
        if (it != by_slot_.end()) return &it->second;
    }
    auto it = by_hash_.find(d.hash);
    return it == by_hash_.end() ? nullptr : &it->second;
}

bool graphic_image(const uint8_t* g, uint32_t size, SpriteImage& out) {
    if (!g || size < 10) return false;
    const int x0 = s16(g), x1 = s16(g + 2), y0 = line_of(s16(g + 4)), y1 = line_of(s16(g + 6));
    const int w = x1 - x0, h = y1 - y0 + 1;
    if (w <= 0 || h <= 0) return false;
    out.width = w;
    out.height = h;
    out.px.assign(size_t(w) * size_t(h), 0);
    uint32_t pos = 8;
    for (;;) {
        if (pos + 2 > size) return false;
        const int xs = s8(g[pos]), xe = s8(g[pos + 1]);
        if (xs == xe) return true;
        const int n = xe - xs;
        if (n < 0 || pos + 4 + uint32_t(n) > size) return false;
        const int row = line_of(s16(g + pos + 2)) - y0;
        if (row < 0 || row >= h || xs < x0 || xe > x1) return false;
        // Zero is transparent: a later span's 0 leaves an earlier pixel.
        for (int i = 0; i < n; ++i)
            if (const uint8_t v = g[pos + 4 + uint32_t(i)]) out.px[size_t(row * w + xs - x0 + i)] = v;
        pos += 4 + uint32_t(n);
    }
}

bool replacement_graphic(const uint8_t* g, uint32_t size, const SpriteImage& img, std::vector<uint8_t>& out,
                         std::string* why) {
    auto fail = [why](const char* m) { if (why) *why = m; return false; };
    out.clear();
    if (!g || size < 10) return fail("the original graphic is unreadable");
    if (img.width <= 0 || img.height <= 0 || img.px.size() != size_t(img.width) * size_t(img.height))
        return fail("the image is empty");
    const int x0 = s16(g), x1 = s16(g + 2), y0 = line_of(s16(g + 4)), y1 = line_of(s16(g + 6));
    const int w = img.width, h = img.height;
    auto at = [&](int x, int line) { return img.px[size_t((line - y0) * w + (x - x0))]; };

    if (w == x1 - x0 && h == y1 - y0 + 1) {
        // The original's own box: its spans, in its order, with the image's
        // pixels; so its stipple columns and its word pairs are the same.
        out.assign(g, g + 8);
        std::vector<uint8_t> covered(img.px.size());
        uint32_t pos = 8;
        for (;;) {
            if (pos + 2 > size) return fail("the original graphic is malformed");
            const int xs = s8(g[pos]), xe = s8(g[pos + 1]);
            if (xs == xe) break;
            const int n = xe - xs;
            if (n < 0 || pos + 4 + uint32_t(n) > size) return fail("the original graphic is malformed");
            const int line = line_of(s16(g + pos + 2));
            out.insert(out.end(), g + pos, g + pos + 4);
            for (int i = 0; i < n; ++i) {
                const int x = xs + i;
                // A pixel outside the box (the game's graphics have none)
                // stays as it is: the image cannot say what it should be.
                if (x < x0 || x >= x1 || line < y0 || line > y1) {
                    out.push_back(g[pos + 4 + uint32_t(i)]);
                    continue;
                }
                covered[size_t((line - y0) * w + (x - x0))] = 1;
                out.push_back(at(x, line));
            }
            pos += 4 + uint32_t(n);
        }
        // Pixels the original's spans do not reach: one more span a line.
        for (int line = y0; line <= y1; ++line) {
            int first = kNoColumn, last = kNoColumn;  // columns can be negative
            for (int x = x0; x < x1; ++x) {
                const size_t i = size_t((line - y0) * w + (x - x0));
                if (!covered[i] && img.px[i]) { if (first == kNoColumn) first = x; last = x; }
            }
            int s = 0, e = 0;
            if (first == kNoColumn || !aligned(first, last, x0, x1, &s, &e)) continue;
            span(out, s, e, line, [&](int x) {
                if (x < x0 || x >= x1) return uint8_t(0);
                const size_t i = size_t((line - y0) * w + (x - x0));
                return covered[i] ? uint8_t(0) : img.px[i];
            });
        }
    } else {
        // Another size: a box of its own at the original's top-left corner.
        // Columns and lines are bytes in the spans, so the box must stay
        // within -128..127 of the hotspot.
        const int nx1 = x0 + ((w + 1) & ~1), ny1 = y0 + h - 1;
        if (x0 < -128 || nx1 > 127 || y0 < -128 || ny1 > 127)
            return fail("too large: from its top-left corner it reaches past 127 pixels from the hotspot");
        put16(out, x0);
        put16(out, nx1);
        put16(out, y0 * 256);
        put16(out, ny1 * 256);
        for (int line = y0; line <= ny1; ++line) {
            int first = kNoColumn, last = kNoColumn;  // columns can be negative
            for (int x = x0; x < x0 + w; ++x)
                if (at(x, line)) { if (first == kNoColumn) first = x; last = x; }
            int s = 0, e = 0;
            if (first == kNoColumn || !aligned(first, last, x0, nx1, &s, &e)) continue;
            span(out, s, e, line, [&](int x) { return x >= x0 && x < x0 + w ? at(x, line) : uint8_t(0); });
        }
    }
    // The end: a span with xs == xe.
    out.push_back(0);
    out.push_back(0);
    put16(out, 0);
    return true;
}

void sprite_dump_palette(const SpriteDraw& d, const uint16_t* cram, uint32_t* palette) {
    palette[0] = 0;
    for (int i = 1; i < 256; ++i) {
        uint8_t v = uint8_t(i + d.colour);
        // Flag bit 2: the plain blitter subtracts 0x80 from non-zero pixels,
        // the scaled ones OR it in.
        const bool half = d.kind == SpriteKind::Plain ? (d.flags >> 2) == 1 : (d.flags & 4) != 0;
        if (v && half) v = d.kind == SpriteKind::Plain ? uint8_t(v - 0x80) : uint8_t(v | 0x80);
        palette[i] = v ? 0xFF000000u | rgb555(cram[v]) : 0;
    }
}

struct SpriteLayer::Replay {
    SpriteLayer* layer;
    const Track* track;
    uint32_t draw;
    uint8_t route;
    bool mark;    // the bytes are composed again (a replaced draw's)
    bool write;   // the bytes are drawn (not the original of a replaced draw)
};

SpriteLayer::SpriteLayer() = default;

void SpriteLayer::restart(int b) {
    Track& t = track_[b & 1];
    std::fill(t.owner.begin(), t.owner.end(), uint8_t(0));
    t.complete = true;
}

void SpriteLayer::write(const Machine& m, int b, uint32_t off, uint32_t v, int size, bool overwrite, uint32_t draws) {
    Track& t = track_[b & 1];
    const uint8_t* plane[2] = {m.mars.fb[b & 1], m.fb_margin[b & 1]};
    const uint16_t seq = uint16_t(std::min<uint32_t>(draws, 0xFFFF));
    for (int i = 0; i < size; ++i) {
        const uint8_t byte = uint8_t(v >> (8 * (size - 1 - i)));
        if (overwrite && !byte) continue;  // the overwrite image's transparent bytes
        uint32_t o = (off + uint32_t(i)) & 0x1FFFF;
        // Where the machine puts it (Machine::fb_write and its helpers).
        bool shadow = false;
        if (m.wide_special) {
            if (in_picture(o)) {
                if (!overwrite) o = (o - uint32_t(m.special_shift)) & 0x1FFFF;
                shadow = in_margin(o);
            }
        } else if (overwrite && m.wide_active) {
            shadow = in_margin(o);
        }
        const uint32_t k = (shadow ? kPlane : 0) + o;
        if (!overwrite) {
            t.owner[k] = 2;
            t.seq[k] = seq;
        } else if (t.owner[k] != 1) {
            // The first blitter byte since the last other write: what it
            // covers is what a replay draws over.
            t.under[k] = plane[shadow][o];
            if (t.owner[k] == 0) t.seq[k] = 0;
            t.owner[k] = 1;
        }
    }
}

void SpriteLayer::fill(const Machine& m, int b, uint32_t addr, uint32_t words, uint16_t data, uint32_t draws) {
    Track& t = track_[b & 1];
    const uint16_t seq = uint16_t(std::min<uint32_t>(draws, 0xFFFF));
    auto other = [&](bool shadow, uint32_t o) {
        const uint32_t k = (shadow ? kPlane : 0) + (o & 0x1FFFF);
        t.owner[k] = 2;
        t.seq[k] = seq;
    };
    constexpr int E = patches::kMaxWideExtra;
    // As Machine::vdp32x_write does the fill.
    if (m.wide_special) {
        if (patches::is_widened_clear(m, addr, words, data)) {
            const uint32_t line = (addr & 0xFF00) * 2u;
            for (int x = -E; x < kNativeWidth + E; ++x) {
                const uint32_t o = (line + uint32_t(x)) & 0x1FFFF;
                other(in_margin(o), o);
            }
            return;
        }
        for (uint32_t i = 0; i < words; ++i) {
            const uint32_t raw = (((addr & 0xFF00) | ((addr + i) & 0xFF)) * 2) & 0x1FFFF;
            const bool picture = in_picture(raw);
            const uint32_t o = picture ? ((raw - uint32_t(m.special_shift)) & 0x1FFFF) : raw;
            const bool shadow = picture && in_margin(o);
            other(shadow, o);
            other(shadow, o + 1);
        }
        return;
    }
    // Runs of bytes at a time: the per-frame clear fills every line.
    auto others = [&](bool shadow, uint32_t o, uint32_t n) {
        while (n) {
            const uint32_t k = o & 0x1FFFF, run = std::min(n, kPlane - k);
            const size_t at = (shadow ? kPlane : 0) + k;
            std::memset(t.owner.data() + at, 2, run);
            std::fill_n(t.seq.data() + at, run, seq);
            o += run;
            n -= run;
        }
    };
    // The words wrap within their 256-word block.
    for (uint32_t i = 0; i < words;) {
        const uint32_t w = (addr + i) & 0xFF, run = std::min(words - i, 256 - w);
        others(false, ((addr & 0xFF00) | w) * 2, run * 2);
        // Machine::fb_margin_clear_line: a fill word at a line's first
        // column clears that line's margins in the shadow.
        for (uint32_t j = 0; m.wide_active && j < run; ++j) {
            const uint32_t o = ((addr & 0xFF00) | (w + j)) * 2;
            if (((o - patches::kFbLineBase) & (patches::kFbLineStride - 1)) != 0) continue;
            others(true, o - uint32_t(E), uint32_t(E));
            others(true, o + uint32_t(kNativeWidth), uint32_t(E));
            if ((o - patches::kFbLineBase) / patches::kFbLineStride == uint32_t(kActiveLines - 1))
                for (int k = 1; k <= patches::kMaxWideExtraBottom; ++k)
                    others(true, o + uint32_t(k) * patches::kFbLineStride - uint32_t(E), uint32_t(kNativeWidth + 2 * E));
        }
        i += run;
    }
}

const std::vector<uint8_t>* SpriteLayer::graphic_for(const SpriteDraw& d, const uint8_t* g, const SpriteImage& image) {
    const Key key{&image, d.hash, d.size};
    auto it = graphics_.find(key);
    if (it == graphics_.end()) {
        std::vector<uint8_t> out;
        std::string why;
        if (!replacement_graphic(g, d.size, image, out, &why)) {
            // Said once: the original is drawn instead.
            if (d.bank >= 0) LOGW("sprites", "32x_%02x_%02x.png: %s; the original is drawn", unsigned(d.bank), unsigned(d.frame), why.c_str());
            else LOGW("sprites", "32x_%016llx.png: %s; the original is drawn", (unsigned long long)d.hash, why.c_str());
            out.clear();
        }
        it = graphics_.emplace(key, std::move(out)).first;
    }
    return it->second.empty() ? nullptr : &it->second;
}

bool SpriteLayer::compose(const Machine& m, int b, const SpriteList& list, const SpritePack* pack,
                          const std::vector<SpriteMove>* moves) {
    b &= 1;
    const Track& t = track_[b];
    if (!t.complete) return false;
    std::vector<const std::vector<uint8_t>*> rep(list.draws.size(), nullptr);
    size_t replaced = 0, moved = 0;
    for (size_t j = 0; j < list.draws.size(); ++j) {
        const SpriteDraw& d = list.draws[j];
        if (!d.size) continue;
        if (moves && j < moves->size() && (*moves)[j].on) ++moved;
        const SpriteImage* img = pack ? pack->find(d) : nullptr;
        if (!img) continue;
        rep[j] = graphic_for(d, list.bytes(d), *img);
        if (rep[j]) ++replaced;
        else ++draws_rejected;
    }
    if (!replaced && !moved) return false;
    draws_replaced += replaced;
    draws_moved += moved;

    if (++pass_ == 0) {
        std::fill(drawn_.begin(), drawn_.end(), 0u);
        std::fill(marked_.begin(), marked_.end(), 0u);
        pass_ = 1;
    }
    marks_.clear();
    std::memcpy(out_.data(), m.mars.fb[b], kPlane);
    std::memcpy(out_.data() + kPlane, m.fb_margin[b], kPlane);

    auto sink = [](void* user, uint32_t o, uint8_t v) {
        Replay& r = *static_cast<Replay*>(user);
        SpriteLayer& l = *r.layer;
        const uint32_t k = place(r.route, o);
        if (r.mark && l.marked_[k] != l.pass_) {
            l.marked_[k] = l.pass_;
            l.marks_.push_back(k);
        }
        // Drawn only where nothing but blitters wrote since this draw began.
        if (r.write && (r.track->owner[k] == 0 || r.draw >= r.track->seq[k])) {
            l.replay_[k] = v;
            l.drawn_[k] = l.pass_;
        }
    };
    for (size_t j = 0; j < list.draws.size(); ++j) {
        const SpriteDraw& d = list.draws[j];
        const uint8_t* warp = d.kind == SpriteKind::Warped ? list.pool.data() + d.warp : nullptr;
        Replay r{this, &t, uint32_t(std::min<size_t>(j, 0xFFFF)), d.route, false, true};
        const SpriteMove* mv = moves && j < moves->size() && (*moves)[j].on ? &(*moves)[j] : nullptr;
        if (rep[j] || mv) {
            // The original's bytes and those drawn in its place (the
            // replacement, or the draw moved: start_glide.h) are composed
            // again.
            SpriteDraw at = d;
            if (mv) {
                at.x = mv->x;
                at.y = mv->y;
                if (d.kind != SpriteKind::Warped && (mv->sx != 0x100 || mv->sy != 0x100 || d.kind == SpriteKind::Scaled)) {
                    at.kind = SpriteKind::Scaled;
                    at.sx = mv->sx;
                    at.sy = mv->sy;
                    // The game's limits were for its own scales.
                    at.clip.scale_min = int16_t(std::min<int32_t>({at.clip.scale_min, mv->sx, mv->sy}));
                    at.clip.scale_max = int16_t(std::max<int32_t>({at.clip.scale_max, mv->sx, mv->sy}));
                }
            }
            r.mark = true;
            r.write = false;
            rasterise_sprite(d, list.bytes(d), d.size, sink, &r, warp);
            r.write = true;
            if (rep[j]) rasterise_sprite(at, rep[j]->data(), uint32_t(rep[j]->size()), sink, &r, warp);
            else rasterise_sprite(at, list.bytes(d), d.size, sink, &r, warp);
        } else {
            rasterise_sprite(d, list.bytes(d), d.size, sink, &r, warp);
        }
    }
    for (const uint32_t k : marks_) {
        const uint8_t base = t.owner[k] == 1 ? t.under[k] : out_[k];
        out_[k] = drawn_[k] == pass_ ? replay_[k] : base;
    }
    return true;
}

} // namespace chaotix
