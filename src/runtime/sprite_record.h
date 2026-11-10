// 32X sprite recording (issue #35, docs/SPRITES_32X.md): what the master
// SH-2's blitters draw into the frame buffer, call by call, as a draw list
// per frame buffer.
//
// Record-only SH-2 hooks (patches.h, kSh2Hooks) note each blitter call's
// registers -- graphic, hotspot, flags, scale -- and, for the characters drawn
// from the scratch buffer, the source and colour offset of the recolour copy
// before it. Each graphic is named by its slot in the game's bank table
// (BANK, FRAME) where it has one, and by a hash of its bytes. Like the tile
// replacer this is output only: nothing the emulated machine sees changes,
// so lockstep holds with it installed.
//
// Draws go to the back buffer, and a list belongs to a buffer, not a frame
// (the bonus stage flips every second frame). When the display flips, the
// buffer that is now shown has its complete list, and the new back buffer's
// list starts again; so the picture on screen always has its draw list.
//
// rebuild_sprites() draws a list again host-side from the graphics' own data,
// exactly as the blitters do; with nothing replaced, the result equals the
// emulated frame buffer wherever the blitters wrote last (check_sprites()).
// That rebuild is the path a replacement graphic takes (sprite_replace.h):
// with a pack set, the recorder also tracks what lies under each blitter
// write, and composes the displayed picture again with the replacements.
#pragma once
#include "runtime/start_glide.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sh2 { struct State; }

namespace chaotix {

class Machine;
class SpriteLayer;
class SpritePack;

// The clip rectangle at SDRAM 0x06003834 (int16 left/right in pixels, int32
// top/bottom in lines x 256, bottom inclusive) and the scaled blitter's scale
// limits at 0x06003840, as they were when a call was made.
struct SpriteClip {
    int16_t left = 0, right = 320;
    int32_t top = 0, bottom = 223 << 8;
    int16_t scale_min = 0, scale_max = 0;
};

// Which blitter: 0x06001380 (unscaled), 0x06001F18 (scaled) or 0x060023EC
// (scaled with a sine-shaped stretch, from the command at 0x06000C48).
enum class SpriteKind : uint8_t { Plain, Scaled, Warped };

struct SpriteDraw {
    uint32_t graphic = 0;  // r4 as passed: the scratch buffer for characters
    uint32_t source = 0;   // where the pixels come from (the recolour copy's source)
    int32_t x = 0, y = 0;  // r1, r2: the hotspot, in pixels
    uint32_t flags = 0;    // r3: 1 h-flip, 2 v-flip, 4 other palette half, 8 stipple
    int32_t sx = 0x100, sy = 0x100;  // r5, r6 (8.8), scaled blitter only
    SpriteKind kind = SpriteKind::Plain;
    // Warped: the 16 bytes of parameters r5 points to, then the sine table
    // at 0x06003C48 (kWarpSineBytes), in SpriteList::pool.
    uint32_t warp = 0;
    uint8_t colour = 0;    // the recolour copy's offset, added to non-zero pixels
    int16_t bank = -1, frame = -1;  // in the bank table at 0x06003614; -1 if not there
    int16_t line = 0;               // the scanline the call was made on
    // Where the call's bytes went (Machine::fb_write): 0 the frame buffer,
    // 1 margins to the host-side shadow (a widened level), 2 the widened
    // special stage (the shadow for picture lines only).
    uint8_t route = 0;
    uint64_t hash = 0;     // FNV-1a of the source graphic's bytes
    SpriteClip clip;
    uint32_t data = 0, size = 0;  // the source graphic's bytes in SpriteList::pool
};

constexpr uint32_t kWarpParamBytes = 16, kWarpSineBytes = 512;

struct SpriteList {
    std::vector<SpriteDraw> draws;
    std::vector<uint8_t> pool;  // graphic bytes, copied when drawn
    uint64_t serial = 0;        // changes whenever the list starts again
    void clear() { draws.clear(); pool.clear(); ++serial; }
    const uint8_t* bytes(const SpriteDraw& d) const { return pool.data() + d.data; }
};

// A graphic as an image: its box, x1 - x0 pixels by (y1 - y0) / 256 + 1
// lines (the bottom line is inclusive), its top-left corner at (x0, y0 / 256)
// from the hotspot, holding the graphic's own palette indices -- before the
// colour offset and the palette-half flag. 0 is transparent.
struct SpriteImage {
    int width = 0, height = 0;
    std::vector<uint8_t> px;  // row-major
};

// A graphic drawn, for dumping: its name (bank and frame, or the hash), its
// pixels, and the 32X colours it was shown in (ARGB, palette[i] for pixel
// value i, the colour offset and flag bit 2 applied; transparent entries 0).
struct SpriteDump {
    int16_t bank = -1, frame = -1;
    uint64_t hash = 0;
    SpriteImage image;
    std::array<uint32_t, 256> palette{};
};

class SpriteRecorder {
public:
    SpriteRecorder();
    ~SpriteRecorder();
    SpriteRecorder(const SpriteRecorder&) = delete;
    SpriteRecorder& operator=(const SpriteRecorder&) = delete;

    // Called by the machine: before a hooked SH-2 instruction, and when the
    // displayed frame buffer changes.
    void hook(const Machine& m, const sh2::State& c, uint32_t pc);
    void flipped(const Machine& m);
    // Forgets every list, as if just installed: for a machine whose state
    // was replaced (a snapshot restored), whose buffers the lists no longer
    // describe.
    void reset();

    // Replacement graphics (sprite_replace.h). With a pack set, the writes
    // into the frame buffer are tracked, and shown() gives the 32X layer to
    // display: the emulated frame buffer and margin shadow, or -- when the
    // displayed list draws a replaced graphic -- the same picture composed
    // again with the replacements. Output only, like everything here.
    void set_pack(std::shared_ptr<const SpritePack> pack);
    const SpritePack* pack() const { return pack_.get(); }
    void shown(const Machine& m, const uint8_t*& fb, const uint8_t*& margin);
    uint64_t pictures_composed = 0;  // displayed lists drawn with replacements or moves

    // The glide into a level's start (issue #136, start_glide.h): the
    // characters' draws composed elsewhere for a few pictures, the same way.
    // The writes are followed only while a glide may come.
    void set_start_glide(bool on);
    bool start_glide() const { return glide_on_; }
    const StartGlide& glide() const { return glide_; }

    // Dumping: each graphic drawn, once (by its name), into `pending` for
    // the frontend to write.
    bool dumping = false;
    std::vector<SpriteDump> pending;
    // Whether the machine must report frame buffer writes (fb_write/fb_fill).
    bool tracks_writes() const { return track_writes || layer_ != nullptr; }

    // The list of the buffer on display, and of each buffer.
    const SpriteList& displayed(const Machine& m) const;
    const SpriteList& list(int buffer) const { return lists_[buffer & 1]; }

    // Who wrote each frame buffer byte last since the buffer became the back
    // buffer, when track_writes is set (the self-test; machine hooks feed it).
    enum Owner : uint8_t { kNone = 0, kSprite = 1, kOther = 2, kSpriteEver = 0x80 };
    bool track_writes = false;
    void fb_write(const Machine& m, uint32_t off, uint32_t v, int size, bool overwrite);
    void fb_fill(const Machine& m, uint32_t word_addr, uint32_t words, uint16_t data);
    const uint8_t* owners(int buffer) const { return owner_[buffer & 1].data(); }
    // Where the buffer's margin bytes went: 0 the frame buffer, 1 the
    // host-side shadow (Machine::fb_margin, a widened level), 2 the shadow
    // for picture lines only (the widened special stage).
    uint8_t shadowed(int buffer) const { return shadowed_[buffer & 1]; }

    uint64_t draws_recorded = 0, unknown_graphics = 0, bad_graphics = 0, flips = 0;

    static uint64_t hash(const uint8_t* p, size_t n);
    // Bytes of the graphic at `addr` (header and spans, up to the end span),
    // or 0 if it does not end within `limit` bytes.
    static uint32_t graphic_size(const uint8_t* p, uint32_t limit);

private:
    struct Recolour { uint32_t src = 0, dst = 0; uint8_t offset = 0; bool valid = false; };
    void lookup_bank(const Machine& m, uint32_t ptr, SpriteDraw& d);
    void rebuild_index(const Machine& m);
    static bool is_recoloured(const Machine& m, const Recolour& rc, uint32_t at);
    void collect_dumps(const Machine& m, const SpriteList& list);

    SpriteList lists_[2];
    std::shared_ptr<const SpritePack> pack_;
    void update_layer();

    std::unique_ptr<SpriteLayer> layer_;
    bool glide_on_ = false;
    StartGlide glide_;
    std::vector<SpriteMove> moves_;  // for the displayed list, one per draw
    uint64_t moves_key_ = ~0ull;
    bool moves_on_ = false;
    uint64_t shown_key_ = ~0ull;
    bool shown_composed_ = false;
    std::unordered_set<uint32_t> dumped_slots_;  // bank << 8 | frame
    std::unordered_set<uint64_t> dumped_hashes_;
    std::vector<uint8_t> owner_[2] = {std::vector<uint8_t>(0x20000), std::vector<uint8_t>(0x20000)};
    uint8_t shadowed_[2] = {};
    Recolour recolour_;
    // Reverse index of the bank table: graphic address -> bank << 16 | frame.
    uint32_t table_[128] = {};
    bool indexed_ = false;
    uint64_t index_frame_ = ~0ull;
    std::unordered_map<uint32_t, uint32_t> index_;
    std::unordered_map<uint32_t, std::pair<uint32_t, uint64_t>> rom_info_;  // ROM graphic -> size, hash
};

// Installed on the master SH-2 each frame while Machine::sprites is set.
void sprite_sh2_hook(sh2::State* c, uint32_t pc);

// A host-side image of the frame buffer (0x20000 bytes, indexed like it) and
// which of its bytes a rebuild wrote.
struct SpriteCanvas {
    std::vector<uint8_t> px = std::vector<uint8_t>(0x20000), cov = std::vector<uint8_t>(0x20000);
    void clear();
};

// Draws one call as its blitter does, into `canvas`. `data` is the source
// graphic (`size` bytes); the colour offset of a character draw is applied
// first. `warp` is a warped call's parameters and sine table. Returns false
// for a call the blitters cannot make (flags 12-15) or a malformed graphic.
bool rasterise_sprite(const SpriteDraw& d, const uint8_t* data, uint32_t size, SpriteCanvas& canvas,
                      const uint8_t* warp = nullptr);
// The same, handing each byte the blitter would store (a frame buffer
// offset and a non-zero value: zero bytes of the overwrite image are
// transparent) to `sink` instead.
using SpriteSink = void (*)(void* user, uint32_t offset, uint8_t value);
bool rasterise_sprite(const SpriteDraw& d, const uint8_t* data, uint32_t size, SpriteSink sink, void* user,
                      const uint8_t* warp = nullptr);
void rebuild_sprites(const SpriteList& list, SpriteCanvas& canvas);

// The self-test: the rebuild of the displayed buffer's list against the
// emulated frame buffer. Bytes whose last writer was a blitter must be
// rebuilt with the same value; bytes the rebuild writes must have been
// written by a blitter (later polygons and fills over them are left out).
struct SpriteCheck {
    uint64_t compared = 0;   // bytes the blitters wrote last
    uint64_t wrong = 0;      // rebuilt with another value
    uint64_t missing = 0;    // not rebuilt
    uint64_t extra = 0;      // rebuilt where no blitter wrote
    uint64_t covered = 0;    // bytes drawn over later by polygons or fills
    int first = -1;          // first mismatching frame buffer offset
    uint8_t first_expected = 0, first_rebuilt = 0;
    std::vector<int> some;   // the first few mismatching offsets
    uint64_t mismatches() const { return wrong + missing + extra; }
};
SpriteCheck check_sprites(const Machine& m, const SpriteRecorder& r, SpriteCanvas& canvas);

// With a pack set and track_writes on: how many bytes of the displayed
// picture that polygons or fills wrote last (or nothing wrote) the composed
// picture shows otherwise. A replacement with the original's footprint must
// leave them all; a larger one may legitimately cover a polygon drawn
// before it.
uint64_t check_composed_others(const Machine& m, SpriteRecorder& r);

// One line per draw, for --sprites-log.
std::string describe(const SpriteDraw& d);

} // namespace chaotix
