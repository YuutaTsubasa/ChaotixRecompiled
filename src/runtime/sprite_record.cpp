#include "sprite_record.h"
#include "patches.h"
#include "sprite_replace.h"
#include "cpu/sh2/sh2_ops.h"
#include "system.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace chaotix {

namespace {

constexpr uint32_t kBankTable = 0x3614;   // SDRAM: 128 frame table pointers
constexpr uint32_t kClipRect = 0x3834;    // SDRAM: int16 left, right; int32 top, bottom
constexpr uint32_t kScaleLimits = 0x3840; // SDRAM: int16 min, max
constexpr uint32_t kWarpSine = 0x3C48;    // SDRAM: 256 int16, the warped blitter's sine table

inline uint16_t be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
inline uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
inline int32_t s8(uint8_t v) { return int8_t(v); }
inline int32_t s16(uint16_t v) { return int16_t(v); }
inline int32_t s32(uint32_t v) { return int32_t(v); }
inline uint32_t swapw(uint32_t v) { return v >> 16 | v << 16; }  // swap.w
inline uint32_t sh2_swapb(uint32_t v) { return (v & 0xFFFF0000u) | (v & 0xFF) << 8 | (v >> 8 & 0xFF); }  // swap.b

// An SH-2 address the way the blitters' loads see it (ROM and SDRAM, cached
// or cache-through): a host pointer and how many bytes follow it there.
const uint8_t* host(const Machine& m, uint32_t a, uint32_t* avail) {
    if ((a >> 29) > 1) return nullptr;
    const uint32_t p = a & 0x1FFFFFFF;
    if (p >= 0x02000000 && p < 0x02400000 && !m.rom.data.empty()) {
        const uint32_t off = (p - 0x02000000) & uint32_t(m.rom.data.size() - 1);
        *avail = uint32_t(m.rom.data.size()) - off;
        return m.rom.data.data() + off;
    }
    if (p >= 0x06000000 && p < 0x08000000) {
        const uint32_t off = p & 0x3FFFF;
        *avail = 0x40000 - off;
        return m.sdram + off;
    }
    return nullptr;
}

bool read32(const Machine& m, uint32_t a, uint32_t* v) {
    uint32_t n = 0;
    const uint8_t* p = host(m, a, &n);
    if (!p || n < 4) return false;
    *v = be32(p);
    return true;
}

// The same memory whether it is reached cached or cache-through.
inline uint32_t canonical(uint32_t a) { return a & 0xDFFFFFFF; }
inline bool in_rom(uint32_t a) { const uint32_t p = a & 0x1FFFFFFF; return p >= 0x02000000 && p < 0x02400000; }

bool plausible_graphic(const Machine& m, uint32_t a) {
    uint32_t n = 0;
    return (a & 1) == 0 && host(m, a, &n) && n >= 10;
}

// Frame buffer byte offsets (see Machine::fb_write_wide): which bytes a
// widened scene keeps in the host-side margin shadow.
bool in_margin(uint32_t o) {
    const int32_t rel = int32_t(o) - int32_t(patches::kFbLineBase);
    const int line = rel >= 0 ? rel / int32_t(patches::kFbLineStride) : -1;
    return line < 0 || line >= kActiveLines || (uint32_t(rel) & (patches::kFbLineStride - 1)) >= uint32_t(kNativeWidth);
}
bool in_picture(uint32_t o) {
    return o >= patches::kFbLineBase && o < patches::kFbLineBase + patches::kFbLineStride * uint32_t(kActiveLines);
}

// The first instruction of each hooked routine: the hook is about that
// routine, not whatever else a scene may have put at its address.
uint16_t expected_code(uint32_t pc) {
    switch (pc) {
    case patches::kSh2RecolourCopy: return 0x2F46;  // mov.l r4,@-r15
    case patches::kSh2Blit: return 0xD91A;          // mov.l @(disp,pc),r9
    case patches::kSh2BlitScaled: return 0xD02C;    // mov.l @(disp,pc),r0
    case patches::kSh2BlitWarped: return 0x9043;    // mov.w @(disp,pc),r0
    default: return 0;
    }
}

} // namespace

uint64_t SpriteRecorder::hash(const uint8_t* p, size_t n) {
    uint64_t h = 1469598103934665603ull;  // FNV-1a
    for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 1099511628211ull;
    return h;
}

uint32_t SpriteRecorder::graphic_size(const uint8_t* p, uint32_t limit) {
    limit = std::min<uint32_t>(limit, 0x10000);
    uint32_t pos = 8;
    for (;;) {
        if (pos + 2 > limit) return 0;
        const int32_t xs = s8(p[pos]), xe = s8(p[pos + 1]);
        if (xs == xe) return pos + 2;
        // A span holds xe - xs pixel bytes; the blitters copy them as words,
        // so the count is even (an odd one would misalign every span after).
        const int32_t n = xe - xs;
        if (n < 0 || (n & 1)) return 0;
        pos += 4 + uint32_t(n);
    }
}

// Whether the graphic at `at` is still exactly the recolour copy's output:
// the same layout as the source, non-zero pixels plus the offset. Code can
// change a copy before drawing it (the routine at 0x06002C0A, before the
// draws from 0x0600D310, does); such a copy is drawn as the bytes it holds.
bool SpriteRecorder::is_recoloured(const Machine& m, const Recolour& rc, uint32_t at) {
    uint32_t na = 0, nb = 0;
    const uint8_t* src = host(m, rc.src, &na);
    const uint8_t* dst = host(m, at, &nb);
    if (!src || !dst) return false;
    const uint32_t size = graphic_size(src, na);
    if (!size || size > nb || std::memcmp(src, dst, 8) != 0) return false;
    uint32_t pos = 8;
    for (;;) {
        if (src[pos] != dst[pos] || src[pos + 1] != dst[pos + 1]) return false;
        const int32_t xs = s8(src[pos]), xe = s8(src[pos + 1]);
        if (xs == xe) return true;
        if (src[pos + 2] != dst[pos + 2] || src[pos + 3] != dst[pos + 3]) return false;
        pos += 4;
        for (int32_t i = xs; i < xe; ++i, ++pos)
            if (dst[pos] != (src[pos] ? uint8_t(src[pos] + rc.offset) : 0)) return false;
    }
}

void sprite_sh2_hook(sh2::State* c, uint32_t pc) {
    const Machine* m = static_cast<const Machine*>(c->hook_user);
    if (m && m->sprites) m->sprites->hook(*m, *c, pc);
}

void SpriteRecorder::hook(const Machine& m, const sh2::State& c, uint32_t pc) {
    if (c.id != 0 || be16(m.sdram + (pc & 0x3FFFF)) != expected_code(pc)) return;
    if (pc == patches::kSh2RecolourCopy) {
        recolour_ = {c.r[1], c.r[2], uint8_t(c.r[3]), true};
        return;
    }
    SpriteDraw d;
    d.graphic = c.r[4];
    d.x = s32(c.r[1]);
    d.y = s32(c.r[2]);
    d.flags = c.r[3];
    d.kind = pc == patches::kSh2BlitScaled ? SpriteKind::Scaled : pc == patches::kSh2BlitWarped ? SpriteKind::Warped : SpriteKind::Plain;
    if (d.kind == SpriteKind::Scaled) { d.sx = s32(c.r[5]); d.sy = s32(c.r[6]); }
    d.source = d.graphic;
    if (recolour_.valid && canonical(d.graphic) == canonical(recolour_.dst) && is_recoloured(m, recolour_, d.graphic)) {
        // A character: the scratch buffer holds a copy of this source with
        // the colour offset added, made just before.
        d.source = recolour_.src;
        d.colour = recolour_.offset;
    }
    const uint8_t* clip = m.sdram + kClipRect;
    d.line = int16_t(m.line);
    d.route = m.wide_special ? 2 : m.wide_active ? 1 : 0;
    d.clip.left = int16_t(be16(clip));
    d.clip.right = int16_t(be16(clip + 2));
    d.clip.top = s32(be32(clip + 4));
    d.clip.bottom = s32(be32(clip + 8));
    d.clip.scale_min = int16_t(be16(m.sdram + kScaleLimits));
    d.clip.scale_max = int16_t(be16(m.sdram + kScaleLimits + 2));

    SpriteList& list = lists_[(m.mars.fb_display ^ 1) & 1];
    uint32_t avail = 0;
    const uint8_t* p = host(m, d.source, &avail);
    if (p) {
        // ROM graphics never change, so their size and hash are kept.
        if (in_rom(d.source)) {
            auto it = rom_info_.find(canonical(d.source));
            if (it == rom_info_.end()) {
                const uint32_t n = graphic_size(p, avail);
                it = rom_info_.emplace(canonical(d.source), std::make_pair(n, hash(p, n))).first;
            }
            d.size = it->second.first;
            d.hash = it->second.second;
        } else {
            d.size = graphic_size(p, avail);
            d.hash = hash(p, d.size);
        }
    }
    if (!d.size) ++bad_graphics;
    // The bytes as they are now: a graphic in SDRAM can be replaced before
    // the picture it is in is shown.
    d.data = uint32_t(list.pool.size());
    if (d.size) list.pool.insert(list.pool.end(), p, p + d.size);
    if (d.kind == SpriteKind::Warped) {
        // Its stretch: the parameters in the command, and the sine table.
        d.warp = uint32_t(list.pool.size());
        uint32_t n = 0;
        const uint8_t* prm = host(m, c.r[5], &n);
        for (uint32_t i = 0; i < kWarpParamBytes; ++i) list.pool.push_back(prm && i < n ? prm[i] : 0);
        list.pool.insert(list.pool.end(), m.sdram + kWarpSine, m.sdram + kWarpSine + kWarpSineBytes);
    }
    lookup_bank(m, d.source, d);
    if (d.bank < 0) ++unknown_graphics;
    list.draws.push_back(d);
    ++draws_recorded;
}

SpriteRecorder::SpriteRecorder() = default;
SpriteRecorder::~SpriteRecorder() = default;

void SpriteRecorder::flipped(const Machine& m) {
    ++flips;
    const int back = (m.mars.fb_display ^ 1) & 1;
    if (dumping) collect_dumps(m, lists_[back ^ 1]);
    // The glide looks at each picture as it goes on display, and before the
    // next one is drawn: the layer must follow that one from its start.
    moves_on_ = false;
    if (glide_on_) {
        const SpriteList& shown = lists_[back ^ 1];
        moves_on_ = glide_.picture(shown, moves_);
        moves_key_ = shown.serial << 1 | uint64_t(back ^ 1);
    }
    update_layer();
    lists_[back].clear();
    if (track_writes) std::fill(owner_[back].begin(), owner_[back].end(), uint8_t(kNone));
    shadowed_[back] = 0;
    if (layer_) layer_->restart(back);
}

void SpriteRecorder::update_layer() {
    const bool want = pack_ || (glide_on_ && glide_.watching());
    if (!want) layer_.reset();
    else if (!layer_) layer_ = std::make_unique<SpriteLayer>();
}

void SpriteRecorder::set_start_glide(bool on) {
    if (on == glide_on_) return;
    glide_on_ = on;
    glide_.reset();
    moves_on_ = false;
    shown_key_ = ~0ull;
    update_layer();
}

void SpriteRecorder::reset() {
    for (int b = 0; b < 2; ++b) {
        lists_[b].clear();
        std::fill(owner_[b].begin(), owner_[b].end(), uint8_t(kNone));
        shadowed_[b] = 0;
        if (layer_) layer_->forget(b);
    }
    recolour_ = Recolour{};
    shown_key_ = ~0ull;
    glide_.reset();
    moves_on_ = false;
}

void SpriteRecorder::set_pack(std::shared_ptr<const SpritePack> pack) {
    if (pack && pack->empty()) pack = nullptr;
    if (pack == pack_) return;
    pack_ = std::move(pack);
    shown_key_ = ~0ull;
    // A layer starts with nothing tracked: the lists drawn before it are
    // shown as they are until the next flip of each buffer.
    if (layer_) layer_->clear_cache();
    update_layer();
}

void SpriteRecorder::shown(const Machine& m, const uint8_t*& fb, const uint8_t*& margin) {
    if (!layer_) return;
    const int b = m.mars.fb_display & 1;
    const SpriteList& list = lists_[b];
    // A list does not change while its buffer is displayed: compose once.
    const uint64_t key = list.serial << 1 | uint64_t(b);
    if (key != shown_key_) {
        shown_key_ = key;
        const std::vector<SpriteMove>* moves = moves_on_ && moves_key_ == key ? &moves_ : nullptr;
        shown_composed_ = layer_->compose(m, b, list, pack_.get(), moves);
        if (shown_composed_) ++pictures_composed;
    }
    if (!shown_composed_) return;
    fb = layer_->fb();
    margin = layer_->margin();
}

// Each graphic once, by its file name: the bank and frame where it has
// them, the hash otherwise. Drawn with the palette as it is now, when the
// picture goes on display.
void SpriteRecorder::collect_dumps(const Machine& m, const SpriteList& list) {
    for (const SpriteDraw& d : list.draws) {
        if (!d.size) continue;
        const bool fresh = d.bank >= 0 ? dumped_slots_.insert(uint32_t(d.bank) << 8 | uint32_t(d.frame)).second
                                       : dumped_hashes_.insert(d.hash).second;
        if (!fresh) continue;
        SpriteDump dump;
        dump.bank = d.bank;
        dump.frame = d.frame;
        dump.hash = d.hash;
        if (!graphic_image(list.bytes(d), d.size, dump.image)) continue;
        sprite_dump_palette(d, m.mars.pal, dump.palette.data());
        pending.push_back(std::move(dump));
    }
}

const SpriteList& SpriteRecorder::displayed(const Machine& m) const { return lists_[m.mars.fb_display & 1]; }

void SpriteRecorder::fb_write(const Machine& m, uint32_t off, uint32_t v, int size, bool overwrite) {
    const int b = (m.mars.fb_display ^ 1) & 1;
    if (layer_) layer_->write(m, b, off, v, size, overwrite, uint32_t(lists_[b].draws.size()));
    if (!track_writes) return;
    uint8_t* own = owner_[b].data();
    if (m.wide_special) shadowed_[b] = 2;
    else if (m.wide_active) shadowed_[b] = 1;
    for (int i = 0; i < size; ++i) {
        const uint8_t byte = uint8_t(v >> (8 * (size - 1 - i)));
        uint32_t o = (off + uint32_t(i)) & 0x1FFFF;
        if (overwrite) {
            if (byte) own[o] = kSprite | kSpriteEver;  // zero bytes are transparent
            continue;
        }
        // The widened special stage moves polygon pixels back (patches.h).
        if (m.wide_special && in_picture(o)) o = (o - uint32_t(m.special_shift)) & 0x1FFFF;
        own[o] = uint8_t((own[o] & kSpriteEver) | kOther);
    }
}

void SpriteRecorder::fb_fill(const Machine& m, uint32_t addr, uint32_t words, uint16_t data) {
    const int b = (m.mars.fb_display ^ 1) & 1;
    if (layer_) layer_->fill(m, b, addr, words, data, uint32_t(lists_[b].draws.size()));
    if (!track_writes) return;
    uint8_t* own = owner_[b].data();
    auto other = [own](uint32_t o) { own[o & 0x1FFFF] = uint8_t((own[o & 0x1FFFF] & kSpriteEver) | kOther); };
    if (patches::is_widened_clear(m, addr, words, data)) {
        const uint32_t line = (addr & 0xFF00) * 2u;
        for (int x = -patches::kMaxWideExtra; x < kNativeWidth + patches::kMaxWideExtra; ++x) other(line + uint32_t(x));
        return;
    }
    for (uint32_t i = 0; i < words; ++i) {
        uint32_t o = (((addr & 0xFF00) | ((addr + i) & 0xFF)) * 2) & 0x1FFFF;
        if (m.wide_special && in_picture(o)) o = (o - uint32_t(m.special_shift)) & 0x1FFFF;
        other(o);
        other(o + 1);
        // A widened level: a fill word at a line's first column also clears
        // that line's margins in the shadow (Machine::fb_margin_clear_line),
        // whatever fill it is -- the clear, or a polygon's span from x = 0.
        if (m.wide_active && !m.wide_special && ((o - patches::kFbLineBase) & (patches::kFbLineStride - 1)) == 0) {
            for (int x = -patches::kMaxWideExtra; x < kNativeWidth + patches::kMaxWideExtra; ++x)
                if (x < 0 || x >= kNativeWidth) other(o + uint32_t(x));
            if ((o - patches::kFbLineBase) / patches::kFbLineStride == uint32_t(kActiveLines - 1))
                for (int k = 1; k <= patches::kMaxWideExtraBottom; ++k)
                    for (int x = -patches::kMaxWideExtra; x < kNativeWidth + patches::kMaxWideExtra; ++x)
                        other(o + uint32_t(k) * patches::kFbLineStride + uint32_t(x));
        }
    }
}

// The bank table holds 128 pointers to frame tables, each a list of graphic
// pointers. Neither has a length, so a frame table is taken to end where the
// next one starts, at the first graphic it points past, or at the first entry
// that is not a graphic's address. Where two slots hold the same graphic the
// lower bank and frame name it.
void SpriteRecorder::rebuild_index(const Machine& m) {
    for (int b = 0; b < 128; ++b) table_[b] = be32(m.sdram + kBankTable + 4 * b);
    indexed_ = true;
    index_frame_ = m.frame_count;
    index_.clear();
    std::vector<uint32_t> starts;
    for (uint32_t t : table_)
        if (plausible_graphic(m, t)) starts.push_back(canonical(t));
    std::sort(starts.begin(), starts.end());
    for (int b = 0; b < 128; ++b) {
        const uint32_t t = canonical(table_[b]);
        if (!plausible_graphic(m, t)) continue;
        auto next = std::upper_bound(starts.begin(), starts.end(), t);
        uint32_t end = next == starts.end() ? ~0u : *next;
        for (uint32_t f = 0; f < 256; ++f) {
            const uint32_t a = t + 4 * f;
            uint32_t v = 0;
            if (a >= end || !read32(m, a, &v)) break;
            // An empty slot (several character banks have them, e.g. Mighty
            // from frame 09, Heavy from 02; docs/CHARACTERS.md): the frames
            // after it still belong to the bank.
            if (v == 0xFFFFFFFFu) continue;
            v = canonical(v);
            if (!plausible_graphic(m, v)) break;
            if (v > t && in_rom(v) == in_rom(t)) end = std::min(end, v);
            index_.emplace(v, uint32_t(b) << 16 | f);
        }
    }
}

void SpriteRecorder::lookup_bank(const Machine& m, uint32_t ptr, SpriteDraw& d) {
    const uint32_t key = canonical(ptr);
    bool changed = !indexed_;
    for (int b = 0; b < 128 && !changed; ++b) changed = be32(m.sdram + kBankTable + 4 * b) != table_[b];
    if (changed) rebuild_index(m);
    for (int attempt = 0; attempt < 2; ++attempt) {
        auto it = index_.find(key);
        if (it != index_.end()) {
            const uint32_t b = it->second >> 16, f = it->second & 0xFFFF;
            uint32_t v = 0;
            if (read32(m, canonical(table_[b]) + 4 * f, &v) && canonical(v) == key) {
                d.bank = int16_t(b);
                d.frame = int16_t(f);
                return;
            }
        }
        // The frame tables can change under an unchanged bank table (a bank
        // decompressed again): look again, at most once a frame.
        if (attempt || index_frame_ == m.frame_count) return;
        rebuild_index(m);
    }
}

void SpriteCanvas::clear() {
    std::fill(px.begin(), px.end(), uint8_t(0));
    std::fill(cov.begin(), cov.end(), uint8_t(0));
}

namespace {

// Writes through the frame buffer's overwrite image, where zero bytes are
// transparent (the blitters' only target), as fb_write does.
struct Writer {
    SpriteSink sink;
    void* user;
    void byte(uint32_t addr, uint8_t v) {
        const bool overwrite = (addr & 0x20000) != 0;
        if (overwrite && !v) return;
        sink(user, addr & 0x1FFFF, v);
    }
    void word(uint32_t addr, uint32_t v) {
        addr &= ~1u;
        byte(addr, uint8_t(v >> 8));
        byte(addr + 1, uint8_t(v));
    }
};

// The graphic with the recolour copy's offset applied, read the way the
// blitters read it (out of range reads as 0).
struct Source {
    const uint8_t* p;
    uint32_t size;
    uint8_t colour;
    uint8_t raw(uint32_t i) const { return i < size ? p[i] : 0; }
    uint8_t pixel(uint32_t i) const { const uint8_t v = raw(i); return v ? uint8_t(v + colour) : 0; }
};

// The division unit (DVSR, DVDNT: 32 by 32 bits, signed), as Sh2OnChip
// models it, overflow included.
uint32_t divide(uint32_t n, uint32_t by) {
    const int32_t a = int32_t(n), b = int32_t(by);
    if (b == 0 || (a == INT32_MIN && b == -1)) return a < 0 ? 0x80000000u : 0x7FFFFFFFu;
    return uint32_t(a / b);
}

// The plain copies at an odd hotspot. The blitter still stores 16-bit words
// at even addresses, so each word holds the last pixel of one source word
// and the first of the next: it ORs the next source word, loaded with
// mov.w (sign-extended), into the previous one and shifts, or for an h-flip
// byte-swaps and shifts first. A pixel of 0x80 or more thus sets the other
// byte of a store to 0xFF; the game's graphics stay below 0x80, but this
// follows the routine instruction for instruction anyway. `line` is the
// first pixel's address less the column; [a, e) is what the clip leaves of
// the span [xs, xe), whose pixels start at `px`.
bool odd_words(const Source& g, Writer& w, bool hf, uint32_t line, int32_t xs, int32_t xe, int32_t a, int32_t e,
               uint32_t px) {
    const uint32_t r9 = line + (hf ? 1u : uint32_t(-1));  // the line, word aligned
    uint32_t src = px + uint32_t(a - xs);
    auto word = [&] { const uint32_t v = uint32_t(s16(uint16_t(g.pixel(src) << 8 | g.pixel(src + 1)))); src += 2; return v; };
    uint32_t r0 = uint32_t(a), r5 = 0;
    if (a > xs) {
        // Clipped on the left at an odd column: its first pixel alone.
        r5 = uint32_t(s8(g.pixel(src++)));
        ++r0;
        if (s32(r0) == xe) {
            if (hf) w.word(r9 - r0 - 2, r5);
            else w.word(r9 + r0, r5 << 8);
            return true;
        }
        if (!hf) r5 <<= 16;
    }
    // Clipped on the right: one source pixel more is read and not stored.
    const bool cut = e < xe;
    const uint32_t n = (uint32_t(cut ? e + 1 : xe) - r0) >> 1;
    if (n == 0) return false;  // the routine would loop 2^32 times
    if (!hf) {
        uint32_t addr = r9 + r0;
        for (uint32_t k = 0; k < n; ++k) {
            const uint32_t v = word();
            r5 = (r5 | v) >> 8;
            w.word(addr, r5);
            addr += 2;
            r5 = swapw(v);
        }
        if (!cut) w.word(addr, r5 >> 8);
    } else {
        uint32_t addr = r9 - r0;
        for (uint32_t k = 0; k < n; ++k) {
            const uint32_t v = sh2_swapb(word()) << 8;
            r5 |= v;
            addr -= 2;
            w.word(addr, r5);
            r5 = swapw(v);
        }
        if (!cut) w.word(addr - 2, r5);
    }
    return true;
}

// 0x06001380. Twelve variants, chosen by the flags through the braf table at
// 0x060013AC: h-flip, v-flip, and one of plain word copies, the other palette
// half (byte copies, 0x80 subtracted from non-zero pixels) or a stipple
// (every second pixel). Each first tests the graphic's box against the clip
// rectangle: outside, nothing is drawn; inside, the spans are copied
// unclipped; across an edge, each span is clipped. The variants keep the clip
// rectangle relative to the hotspot in r10-r13, negated for a flip, and this
// does the same, so that the edge cases come out as they do there.
bool blit(const SpriteDraw& d, const Source& g, Writer& w) {
    if (d.flags > 11) return false;  // past the end of the braf table
    const bool hf = d.flags & 1, vf = d.flags & 2;
    const int mode = int(d.flags >> 2);  // 0 plain, 1 other palette half, 2 stipple
    const uint32_t X = uint32_t(d.x), Y8 = uint32_t(d.y) << 8;
    uint32_t base = 0x24020200u + X + 2 * Y8;
    const int32_t x0 = s16(be16(g.p)), x1 = s16(be16(g.p + 2)), y0 = s16(be16(g.p + 4)), y1 = s16(be16(g.p + 6));
    int32_t r10 = s32(uint32_t(d.clip.left) - X), r11 = s32(uint32_t(d.clip.right) - X);
    int32_t r12 = s32(uint32_t(d.clip.top) - Y8), r13 = s32(uint32_t(d.clip.bottom) - Y8);
    if (mode == 2 && (X & 1)) { ++r10; ++r11; }
    if (hf) { r10 = s32(0u - uint32_t(r10)); r11 = s32(0u - uint32_t(r11)); }
    if (vf) {
        // One line up, and the lines counted upwards from the hotspot.
        r12 = s32(0u - uint32_t(r12));
        r13 = s32(0u - uint32_t(r13) - 256u);
        base -= 0x200;
    }
    // Clip window in the graphic's own x: [lo, hi).
    const int32_t lo = hf ? r11 : r10, hi = hf ? r10 : r11;
    if (x0 >= hi || !(x1 > lo)) return true;
    if (!vf ? (y0 > r13 || y1 < r12) : (y0 >= r12 || y1 < r13)) return true;
    const bool inside = x0 >= lo && !(x1 > hi) && (!vf ? (y0 >= r12 && !(y1 > r13)) : (y0 >= r13 && !(y1 >= r12)));
    uint32_t pos = 8;
    for (;;) {
        const int32_t xs = s8(g.raw(pos)), xe = s8(g.raw(pos + 1));
        if (xs == xe) return true;
        const int32_t ys = s16(uint16_t(g.raw(pos + 2) << 8 | g.raw(pos + 3)));
        const uint32_t px = pos + 4;
        pos = px + uint32_t(xe - xs);
        int32_t a = xs, e = xe;
        if (!inside) {
            if (!vf ? (ys > r13 || ys < r12) : (ys >= r12 || ys < r13)) continue;
            if (a >= hi || !(e > lo)) continue;
            a = std::max(a, lo);
            e = std::min(e, hi);
        }
        const uint32_t line = vf ? base - 2 * uint32_t(ys) : base + 2 * uint32_t(ys);
        auto at = [&](int32_t s) { return hf ? line - 1 - uint32_t(s) : line + uint32_t(s); };
        if (mode == 0 && (X & 1)) {
            if (!odd_words(g, w, hf, line, xs, xe, a, e, px)) return false;
            continue;
        }
        if (mode == 2) {
            // Every second pixel: the even ones of the span, or for an h-flip
            // the odd ones, so both land on the same screen columns.
            const uint32_t n = uint32_t(e - a) >> 1;
            for (uint32_t j = 0; j < n; ++j) {
                const int32_t s = a + int32_t(2 * j) + (hf ? 1 : 0);
                w.byte(at(s), g.pixel(px + uint32_t(s - xs)));
            }
            continue;
        }
        for (int32_t s = a; s < e; ++s) {
            uint8_t v = g.pixel(px + uint32_t(s - xs));
            if (mode == 1 && v) v = uint8_t(v - 0x80);
            w.byte(at(s), v);
        }
    }
}

// The scaled blitters' common part (0x06002166): each source pixel fills
// the rectangle its column and line tables give, two columns to a 16-bit
// store. r3 collects the store and r7 is its address; a source pixel is
// sign-extended as it is loaded (mov.b), as in the routine.
struct Tables {
    int32_t x0 = 0, y0 = 0;
    std::vector<int32_t> A, B;        // per source column: first screen column, columns
    std::vector<uint32_t> ADDR, H;    // per source line: frame buffer address, lines
};

bool draw_spans(const SpriteDraw& d, const Source& g, Writer& w, const Tables& t) {
    const int32_t x0 = t.x0, y0 = t.y0;
    const int32_t ncols = int32_t(t.A.size()), nrows = int32_t(t.H.size());
    const std::vector<int32_t>& A = t.A;
    const std::vector<int32_t>& B = t.B;
    const std::vector<uint32_t>& ADDR = t.ADDR;
    const std::vector<uint32_t>& H = t.H;
    const bool hf = d.flags & 1, pal = d.flags & 4;
    uint32_t pos = 8;
    uint32_t r3 = 0;
    for (;;) {
        const int32_t xs = s8(g.raw(pos)), xe = s8(g.raw(pos + 1));
        if (xs == xe) return true;
        const int32_t line = s8(g.raw(pos + 2));
        pos += 4;
        uint32_t r2 = uint32_t(xe - xs);
        const uint32_t px_end = pos + r2;
        if (line - y0 < 0 || line - y0 >= nrows || xs - x0 < 0 || xe - x0 > ncols) return false;
        const uint32_t h = H[size_t(line - y0)];
        if (h == 0) { pos = px_end; continue; }
        if (s32(h) < 0 || h > 1024) return false;
        uint32_t r7 = ADDR[size_t(line - y0)];
        const uint32_t r10 = (h << 9) + (hf ? 2u : uint32_t(-2));
        const int32_t a0 = A[size_t(xs - x0)];
        size_t col = size_t(xs - x0);
        r7 += uint32_t(a0);
        const bool odd = a0 & 1;
        auto store = [&] { for (uint32_t j = 0; j < h; ++j) { w.word(r7, r3); r7 += 0x200; } };
        auto load = [&](uint32_t& r1, uint32_t& r0) -> bool {
            r1 = uint32_t(B[col++]);
            r0 = uint32_t(s8(g.pixel(pos++)));
            if (s32(r1) < 0 || r1 > 1024) return false;
            return true;
        };
        auto palette = [&](uint32_t& r0) { if (pal && r0) r0 |= 0x80; };
        uint32_t r1 = 0, r0 = 0;
        if (!hf) {
            if (odd) { r3 = 0; r7 -= 1; goto o; }
            goto e;
        o:  // the low byte of the word at r7
            if (!load(r1, r0)) return false;
            if (r1 == 0) goto no;
            palette(r0);
        os:
            r3 |= r0;
            store();
            r7 -= r10;
            if (--r1 == 0) goto ne;
            r3 = sh2_swapb(r0);
            if (--r1 != 0) goto os;
        no:
            if (--r2 != 0) goto o;
            store();
            continue;
        e:  // the high byte
            if (!load(r1, r0)) return false;
            if (r1 == 0) goto ne;
            palette(r0);
        el:
            r3 = sh2_swapb(r0);
            if (--r1 == 0) goto no;
            r3 |= r0;
            store();
            r7 -= r10;
            if (--r1 != 0) goto el;
        ne:
            if (--r2 != 0) goto e;
            continue;
        }
        // h-flip: leftwards, so a word's low byte comes first.
        r3 = 0;
        if (odd) { r7 -= 1; goto ho; }
        goto he;
    ho:
        if (!load(r1, r0)) return false;
        if (r1 == 0) goto hno;
        palette(r0);
    hol:
        r3 = r0;
        if (--r1 == 0) goto hne;
        r3 |= sh2_swapb(r0);
        store();
        r7 -= r10;
        if (--r1 != 0) goto hol;
    hno:
        if (--r2 != 0) goto ho;
        continue;
    he:
        if (!load(r1, r0)) return false;
        if (r1 == 0) goto hne;
        palette(r0);
    hel:
        r3 |= sh2_swapb(r0);
        store();
        r7 -= r10;
        if (--r1 == 0) goto hno;
        r3 = r0;
        if (--r1 != 0) goto hel;
    hne:
        if (--r2 != 0) goto he;
        store();
    }
}

// 0x06001F18. The graphic's box is mapped through two tables built first,
// using the hardware divider: for each source column the screen column it
// starts at and how many columns it covers, and for each source line the
// frame buffer line it starts at and how many lines it covers, both clipped.
// Then each source pixel fills its rectangle, two columns to a 16-bit store.
// This follows the routine register by register: its rounding (positions in
// 16.16, truncated), its clipping and its way of pairing pixels into words
// are what the frame buffer shows.
bool blit_scaled(const SpriteDraw& d, const Source& g, Writer& w) {
    const uint32_t sx = uint32_t(d.sx), sy = uint32_t(d.sy);
    const int32_t smin = d.clip.scale_min, smax = d.clip.scale_max;
    if (s32(sx) < smin || s32(sx) > smax || s32(sy) < smin || s32(sy) > smax) return true;
    const uint32_t step_x = divide(0x01000000u, sx), step_y = divide(0x01000000u, sy);

    // Columns.
    const int32_t x0 = s16(be16(g.p)), x1 = s16(be16(g.p + 2));
    const int32_t ncols = x1 + 1 - x0;
    if (ncols <= 0 || ncols > 4096) return false;
    Tables t;
    t.x0 = x0;
    t.A.resize(size_t(ncols));
    t.B.resize(size_t(ncols));
    std::vector<int32_t>& A = t.A;
    std::vector<int32_t>& B = t.B;
    {
        uint32_t r7 = step_x * uint32_t(x0) + (uint32_t(d.x) << 16);
        const uint32_t macl = step_x * uint32_t(ncols);
        uint32_t r5 = 0, r0 = 0;
        int k = 0;
        auto put = [&](uint32_t a, uint32_t b) { A[size_t(k)] = s16(uint16_t(a)); B[size_t(k)] = s16(uint16_t(b)); return ++k == ncols; };
        if (!(d.flags & 1)) {
            const uint32_t r13 = uint32_t(d.clip.left) << 16, r14 = uint32_t(d.clip.right) << 16;
            if (s32(r7) >= s32(r14) || s32(macl + r7) < s32(r13)) return true;
            if (s32(r7) < s32(r13)) {
                r5 = swapw(r13);
                for (;;) {
                    r7 += step_x;
                    if (s32(r7) >= s32(r13)) goto c9a;
                    if (put(r5, 0)) return false;  // cannot happen after the test above
                }
            }
        c96:
            r5 = swapw(r7);
            r7 += step_x;
        c9a:
            if (s32(r7) < s32(r14)) {
                if (put(r5, swapw(r7) - r5)) goto rows;
                goto c96;
            }
            r0 = swapw(r14) - r5;
            while (!put(r5, r0)) r0 = 0;
        } else {
            // Flipped: positions run leftwards from just left of the hotspot,
            // and each column's table entry is its rightmost screen column.
            r7 = ~(step_x * uint32_t(x0)) + (uint32_t(d.x) << 16);
            uint32_t r13 = (uint32_t(d.clip.left) << 16) - 1, r14 = (uint32_t(d.clip.right) << 16) - 1;
            if (!(s32(r7) > s32(r13)) || s32(r7 - macl) >= s32(r14)) return true;
            if (s32(r7) > s32(r14)) {
                r5 = swapw(r14);
                for (;;) {
                    r7 -= step_x;
                    if (!(s32(r7) > s32(r14))) goto f12;
                    if (put(r5, 0)) return false;
                }
            }
        f0e:
            r5 = swapw(r7);
            r7 -= step_x;
        f12:
            if (s32(r7) > s32(r13)) {
                const uint32_t a = r5;
                r5 = r5 - swapw(r7);
                if (put(a, r5)) goto rows;
                goto f0e;
            }
            r13 = swapw(r13);
            r0 = r5 - r13;
            while (!put(r5, r0)) r0 = 0;
        }
    }
rows:
    // Lines: the header's and the spans' y are lines x 256; the routine reads
    // only their high bytes.
    const int32_t y0 = s8(g.raw(4)), y1 = s8(g.raw(6));
    const int32_t nrows = y1 + 1 - y0;
    if (nrows <= 0) return false;
    t.y0 = y0;
    t.ADDR.resize(size_t(nrows));
    t.H.resize(size_t(nrows));
    std::vector<uint32_t>& ADDR = t.ADDR;
    std::vector<uint32_t>& H = t.H;
    {
        const uint32_t top = uint32_t(d.clip.top) << 8;                   // 16.16
        uint32_t bottom = ((uint32_t(d.clip.bottom) >> 8) + 1) << 16;     // exclusive, 16.16
        uint32_t r10 = step_y * uint32_t(y0);
        const uint32_t macl = step_y * uint32_t(nrows), r2 = uint32_t(d.y) << 16;
        uint32_t r5 = 0;
        int k = 0;
        auto put = [&](uint32_t addr, uint32_t h) { ADDR[size_t(k)] = addr; H[size_t(k)] = h; return ++k == nrows; };
        auto line_addr = [](uint32_t l) { return (l << 9) + 0x24020200u; };
        if (!(d.flags & 2)) {
            r10 += r2;
            if (s32(r10) >= s32(bottom) || !(s32(macl + r10) > s32(top))) return true;
            if (s32(r10) < s32(top)) {
                r5 = swapw(top);
                for (;;) {
                    r10 += step_y;
                    if (s32(r10) > s32(top)) goto raa;
                    if (put(0, 0)) return false;
                }
            }
        ra6:
            r5 = swapw(r10);
            r10 += step_y;
        raa:
            r5 = uint32_t(s16(uint16_t(r5)));
            if (s32(r10) < s32(bottom)) {
                const uint32_t h = uint32_t(s16(uint16_t(swapw(r10)))) - r5;
                if (put(line_addr(r5), h)) goto draw;
                goto ra6;
            }
            bottom = (bottom >> 16) - r5;
            r5 = line_addr(r5);
            while (!put(r5, bottom)) bottom = 0;
        } else {
            // Flipped: lines run upwards from the hotspot; each entry is the
            // top line of the source line's rows.
            r10 = 0u - r10 + r2;
            if (!(s32(r10) > s32(top)) || s32(r10 - macl) >= s32(bottom)) return true;
            if (s32(r10) >= s32(bottom)) {
                r5 = swapw(bottom);
                for (;;) {
                    r10 -= step_y;
                    if (!(s32(r10) >= s32(bottom))) goto v26;
                    if (put(0, 0)) return false;
                }
            }
        v22:
            r5 = swapw(r10);
            r10 -= step_y;
        v26:
            r5 = uint32_t(s16(uint16_t(r5)));
            if (s32(r10) >= s32(top)) {
                const uint32_t l = uint32_t(s16(uint16_t(swapw(r10))));
                if (put(line_addr(l), r5 - l)) goto draw;
                goto v22;
            }
            uint32_t t = top >> 16;
            r5 = r5 - t;
            t = line_addr(t);
            while (!put(t, r5)) r5 = 0;
        }
    }
draw:
    return draw_spans(d, g, w, t);
}

// One axis of the warped blitter (the subroutine at 0x060025F2), from the
// four parameters p0-p3: the graphic's start and size in pixels, (from << 8)
// and (n << 8) divided by p0; and per source column (or line) a width, 0x100
// plus p3 times a sine whose phase runs from p1 to p2 across the n entries,
// scaled so that the widths add up to the size. The step of the phase is a
// 16-step DIV1 division; the widths come out of a 16.16 sum rounded at
// 0x7F00. Every result is stored as a word and read back signed.
struct WarpAxis {
    int32_t start = 0, count = 0, total = 0;
    std::vector<int32_t> widths;
};

bool warp_axis(const uint8_t* prm, int32_t from, int32_t to, const uint8_t* sine, WarpAxis& out) {
    auto p = [prm](int i) { return uint32_t(s16(be16(prm + 2 * i))); };
    const uint32_t p0 = p(0), n = uint32_t(to) - uint32_t(from);
    if (s32(n) <= 0 || n > 1024) return false;
    const uint32_t q1 = divide(uint32_t(from) << 8, p0);
    uint32_t r6 = p(1) << 8;
    // r9 = ((p2 - p1) << 8) / n, as the routine divides: dividend less one
    // when negative, DIV0S, sixteen DIV1, then the quotient's rounding.
    sh2::State alu{};
    uint32_t* R = alu.r;
    R[0] = swapw(n);
    R[9] = (p(2) << 8) - r6;
    sh2::setT(&alu, R[9] >> 31);
    {
        const uint32_t tmp1 = R[9] - R[0], tmp0 = R[9], res = tmp1 - (alu.sr & sh2::SR_T);
        sh2::setT(&alu, tmp0 < tmp1 || tmp1 < res);
        R[9] = res;
    }
    R[9] += R[0];
    {
        const uint32_t q = R[9] >> 31, mm = R[0] >> 31;
        alu.sr = (alu.sr & ~(sh2::SR_Q | sh2::SR_M | sh2::SR_T)) | (q ? sh2::SR_Q : 0) | (mm ? sh2::SR_M : 0) | ((q ^ mm) ? sh2::SR_T : 0);
    }
    for (int i = 0; i < 16; ++i) sh2::div1(&alu, 9, 0);
    R[9] = uint32_t(s16(uint16_t(R[9])));
    {
        const uint32_t t = R[9] >> 31;
        R[9] = R[9] << 1 | (alu.sr & sh2::SR_T);
        sh2::setT(&alu, t);
    }
    {
        const uint32_t tmp1 = R[9] + R[0], tmp0 = R[9], res = tmp1 + (alu.sr & sh2::SR_T);
        sh2::setT(&alu, tmp0 > tmp1 || tmp1 > res);
        R[9] = res;
    }
    R[9] -= R[0];
    const uint32_t step = R[9];
    r6 += 127;
    const uint32_t q2 = divide(sh2_swapb(n), p0);
    out.widths.assign(n, 0);
    uint32_t sum = 0;
    for (uint32_t k = 0; k < n; ++k) {
        const uint32_t phase = (r6 >> 8) & 0xFF;
        const int32_t sv = s16(be16(sine + 2 * phase));
        r6 += step;
        const uint32_t macl = uint32_t(s16(uint16_t(p(3))) * sv);
        const uint32_t v = uint32_t(s16(uint16_t(macl >> 8))) + 0x100;
        out.widths[k] = s16(uint16_t(v));
        sum += v;
    }
    const uint32_t q3 = divide(q2 << 16, sum);
    uint32_t acc = 0x7F00;
    for (uint32_t k = 0; k < n; ++k) {
        const uint32_t macl = uint32_t(s16(uint16_t(q3)) * out.widths[k]);
        const uint32_t before = acc >> 16;
        acc += macl;
        out.widths[k] = s16(uint16_t((acc >> 16) - before));
    }
    out.start = s16(uint16_t(q1));
    out.count = s16(uint16_t(n));
    out.total = s16(uint16_t(q2));
    return out.count > 0;
}

// 0x060023EC: like the scaled blitter, but the column and line tables come
// from warp_axis rather than a constant step, in whole pixels, and are
// clipped in pixels. Columns cover x0 to x1 - 1, lines y0 to y1.
bool blit_warped(const SpriteDraw& d, const Source& g, Writer& w, const uint8_t* warp) {
    if (!warp) return false;
    const uint8_t* sine = warp + kWarpParamBytes;
    const int32_t x0 = s16(be16(g.p)), x1 = s16(be16(g.p + 2));
    const int32_t y0 = s8(g.raw(4)), y1 = s8(g.raw(6));
    WarpAxis cx, cy;
    if (!warp_axis(warp, x0, x1, sine, cx) || !warp_axis(warp + 8, y0, y1 + 1, sine, cy)) return false;
    Tables t;
    t.x0 = x0;
    t.y0 = y0;
    t.A.assign(size_t(cx.count), 0);
    t.B.assign(size_t(cx.count), 0);
    t.ADDR.assign(size_t(cy.count), 0);
    t.H.assign(size_t(cy.count), 0);
    const int32_t L = d.clip.left, R = d.clip.right;
    {
        int32_t r7 = cx.start;
        size_t k = 0;
        const size_t n = size_t(cx.count);
        if (!(d.flags & 1)) {
            r7 += d.x;
            if (r7 >= R || cx.total + r7 < L) return true;
            for (; k < n; ++k) {
                int32_t r5 = r7;
                r7 += cx.widths[k];
                int32_t r0 = r7;
                if (!(r5 > L)) r5 = L;
                if (!(r0 > L)) r0 = L;
                if (r7 > R) {
                    // The column across the right edge, then nothing.
                    t.A[k] = s16(uint16_t(r5));
                    t.B[k] = s16(uint16_t(R - r5));
                    for (++k; k < n; ++k) t.A[k] = s16(uint16_t(r5));
                    break;
                }
                t.A[k] = s16(uint16_t(r5));
                t.B[k] = s16(uint16_t(r0 - r5));
            }
        } else {
            // Flipped: from just left of the hotspot leftwards; each entry is
            // its rightmost column.
            const int32_t L1 = L - 1, R1 = R - 1;
            r7 = d.x - r7 - 1;
            if (!(r7 > L1) || r7 - cx.total > R1) return true;
            for (; k < n; ++k) {
                int32_t r5 = r7;
                r7 -= cx.widths[k];
                int32_t r0 = r7;
                if (r5 > R1) r5 = R1;
                if (r0 > R1) r0 = R1;
                if (!(r7 > L1)) {
                    t.A[k] = s16(uint16_t(r5));
                    t.B[k] = s16(uint16_t(r5 - L1));
                    for (++k; k < n; ++k) t.A[k] = s16(uint16_t(r5));
                    break;
                }
                t.A[k] = s16(uint16_t(r5));
                t.B[k] = s16(uint16_t(r5 - r0));
            }
        }
    }
    {
        const int32_t top = s32(uint32_t(d.clip.top) >> 8), bottom = s32(uint32_t(d.clip.bottom) >> 8);
        auto line_addr = [](int32_t l) { return (uint32_t(l) << 9) + 0x24020200u; };
        int32_t r7 = cy.start;
        size_t k = 0;
        const size_t n = size_t(cy.count);
        if (!(d.flags & 2)) {
            r7 += d.y;
            if (r7 > bottom || cy.total + r7 < top) return true;
            for (; k < n; ++k) {
                int32_t r5 = r7;
                r7 += cy.widths[k];
                int32_t r0 = r7;
                if (!(r5 >= top)) r5 = top;
                if (!(r0 >= top)) r0 = top;
                if (r7 > bottom) {
                    // The line across the bottom edge (inclusive), then nothing.
                    t.ADDR[k] = line_addr(r5);
                    t.H[k] = uint32_t(bottom - r5 + 1);
                    for (++k; k < n; ++k) t.ADDR[k] = line_addr(r5);
                    break;
                }
                t.ADDR[k] = line_addr(r5);
                t.H[k] = uint32_t(r0 - r5);
            }
        } else {
            r7 = d.y - r7;
            if (r7 < top || r7 - cy.total > bottom) return true;
            for (; k < n; ++k) {
                int32_t r5 = r7;
                r7 -= cy.widths[k];
                int32_t r0 = r7;
                if (r5 > bottom) r5 = bottom;
                if (r0 > bottom) r0 = bottom;
                if (!(r7 >= top)) {
                    t.ADDR[k] = line_addr(top);
                    t.H[k] = uint32_t(r5 - top);
                    for (++k; k < n; ++k) t.ADDR[k] = line_addr(top);
                    break;
                }
                t.ADDR[k] = line_addr(r0);
                t.H[k] = uint32_t(r5 - r0);
            }
        }
    }
    return draw_spans(d, g, w, t);
}

} // namespace

bool rasterise_sprite(const SpriteDraw& d, const uint8_t* data, uint32_t size, SpriteCanvas& canvas, const uint8_t* warp) {
    auto put = [](void* user, uint32_t o, uint8_t v) {
        SpriteCanvas& c = *static_cast<SpriteCanvas*>(user);
        c.px[o] = v;
        c.cov[o] = 1;
    };
    return rasterise_sprite(d, data, size, put, &canvas, warp);
}

bool rasterise_sprite(const SpriteDraw& d, const uint8_t* data, uint32_t size, SpriteSink sink, void* user, const uint8_t* warp) {
    if (!data || size < 10) return false;
    Source g{data, size, d.colour};
    Writer w{sink, user};
    switch (d.kind) {
    case SpriteKind::Scaled: return blit_scaled(d, g, w);
    case SpriteKind::Warped: return blit_warped(d, g, w, warp);
    default: return blit(d, g, w);
    }
}

void rebuild_sprites(const SpriteList& list, SpriteCanvas& canvas) {
    for (const SpriteDraw& d : list.draws)
        rasterise_sprite(d, list.bytes(d), d.size, canvas, d.kind == SpriteKind::Warped ? list.pool.data() + d.warp : nullptr);
}

SpriteCheck check_sprites(const Machine& m, const SpriteRecorder& r, SpriteCanvas& canvas) {
    SpriteCheck res;
    const int b = m.mars.fb_display & 1;
    canvas.clear();
    rebuild_sprites(r.list(b), canvas);
    const uint8_t* own = r.owners(b);
    const uint8_t* fb = m.mars.fb[b];
    const uint8_t* shadow = m.fb_margin[b];
    const uint8_t mode = r.shadowed(b);
    for (uint32_t o = 0; o < 0x20000; ++o) {
        const bool rebuilt = canvas.cov[o] != 0;
        if ((own[o] & 3) == SpriteRecorder::kSprite) {
            ++res.compared;
            // Where the byte is: widened scenes keep margins in the shadow.
            const bool sh = (mode == 1 && in_margin(o)) || (mode == 2 && in_picture(o) && in_margin(o));
            const uint8_t want = sh ? shadow[o] : fb[o];
            if (rebuilt && canvas.px[o] == want) continue;
            ++(rebuilt ? res.wrong : res.missing);
            if (res.first < 0) { res.first = int(o); res.first_expected = want; res.first_rebuilt = canvas.px[o]; }
            if (res.some.size() < 12) res.some.push_back(int(o));
        } else if (rebuilt) {
            if (own[o] & SpriteRecorder::kSpriteEver) { ++res.covered; continue; }
            ++res.extra;
            if (res.first < 0) { res.first = int(o); res.first_expected = 0; res.first_rebuilt = canvas.px[o]; }
            if (res.some.size() < 12) res.some.push_back(int(o));
        }
    }
    return res;
}

uint64_t check_composed_others(const Machine& m, SpriteRecorder& r) {
    const int b = m.mars.fb_display & 1;
    const uint8_t* fb = m.mars.fb[b];
    const uint8_t* shadow = m.fb_margin[b];
    const uint8_t* cfb = fb;
    const uint8_t* cshadow = shadow;
    r.shown(m, cfb, cshadow);
    if (cfb == fb && cshadow == shadow) return 0;
    const uint8_t* own = r.owners(b);
    const uint8_t mode = r.shadowed(b);
    uint64_t changed = 0;
    for (uint32_t o = 0; o < 0x20000; ++o) {
        if ((own[o] & 3) == SpriteRecorder::kSprite) continue;
        const bool sh = (mode == 1 && in_margin(o)) || (mode == 2 && in_picture(o) && in_margin(o));
        if (sh ? cshadow[o] != shadow[o] : cfb[o] != fb[o]) ++changed;
    }
    return changed;
}

std::string describe(const SpriteDraw& d) {
    char buf[256];
    std::snprintf(buf, sizeof buf, "bank %3d frame %3d x %4d y %4d flags %X scale %03X,%03X colour %02X%s src %08X size %u hash %016llx line %d",
                  d.bank, d.frame, d.x, d.y, unsigned(d.flags), unsigned(d.sx & 0xFFFF), unsigned(d.sy & 0xFFFF), d.colour,
                  d.kind == SpriteKind::Scaled ? " scaled" : d.kind == SpriteKind::Warped ? " warped" : "", d.source, d.size,
                  (unsigned long long)d.hash, d.line);
    return buf;
}

} // namespace chaotix
