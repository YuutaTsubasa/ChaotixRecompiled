// 32X sprite replacement (issue #35, docs/SPRITES_32X.md): modders' versions
// of the graphics the SH-2's blitters draw -- characters, the HUD, objects,
// the title -- drawn in the original's place.
//
// A replacement holds the graphic's own palette indices, before the colour
// offset of a character (0x12 the player, 0x24 the partner) and before flag
// bit 2 (the other palette half), so those, the game's fades and the per
// entry priority bit apply to it exactly as to the original. It is turned
// into a graphic in the game's own format and drawn by the host-side rebuild
// of the blitters (sprite_record.h), so flips, scaling, the stipple and the
// clipping are those of the original call.
//
// The picture is composed again only for what is shown: the machine's frame
// buffers, registers and timing never see a replacement, so lockstep holds.
// While a pack is set, SpriteLayer follows every frame buffer write of the
// back buffer: for each byte (of the frame buffer and of the widescreen
// margin shadow), whether a blitter or something else (polygons, fills)
// wrote it last, the value under the blitters' writes, and how many draws
// the list had at the last other write. Composing a displayed list then
// replays its draws, the replacements in place of the originals, over what
// lay under the blitters, in list order, each draw only where nothing else
// wrote after it began; bytes no replaced draw touches stay the emulated
// ones.
#pragma once
#include "runtime/sprite_record.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace chaotix {

class Machine;

// The replacements by name: (bank, frame) for graphics in the bank table,
// the hash of the graphic's bytes for the others. A graphic with a bank and
// frame is looked up by them first, then by its hash.
class SpritePack {
public:
    void add(int bank, int frame, SpriteImage image) { by_slot_[uint32_t(bank) << 8 | uint32_t(frame)] = std::move(image); }
    void add(uint64_t hash, SpriteImage image) { by_hash_[hash] = std::move(image); }
    const SpriteImage* find(const SpriteDraw& d) const;
    size_t size() const { return by_slot_.size() + by_hash_.size(); }
    bool empty() const { return size() == 0; }

private:
    std::unordered_map<uint32_t, SpriteImage> by_slot_;
    std::unordered_map<uint64_t, SpriteImage> by_hash_;
};

// The graphic as an image of its box (SpriteImage): every span's pixels, as
// stored. False for a malformed graphic, or one with pixels outside its box
// (the game's have none).
bool graphic_image(const uint8_t* g, uint32_t size, SpriteImage& out);

// The graphic drawn in place of `g` for the replacement `image`, in the
// game's format. An image the size of g's box keeps g's spans with their
// pixels replaced, and gets spans of its own for the pixels it adds; so a
// replacement equal to the original draws exactly the original. An image of
// another size gets a box of its own size with the same top-left corner.
// False (and `why`) if it cannot be drawn: the spans' bytes hold columns and
// lines from -128 to 127 around the hotspot.
bool replacement_graphic(const uint8_t* g, uint32_t size, const SpriteImage& image, std::vector<uint8_t>& out,
                         std::string* why);

// The colours a draw shows its pixel values in: palette[i] (ARGB) is the
// 32X colour of value i after the colour offset and flag bit 2, as the
// draw's blitter applies them; values that end up 0 are transparent (0).
void sprite_dump_palette(const SpriteDraw& d, const uint16_t* cram, uint32_t* palette256);

// What a recorder keeps to compose its displayed pictures (see above).
class SpriteLayer {
public:
    SpriteLayer();

    // The back buffer `b` starts again (a flip): nothing written yet.
    void restart(int b);
    // Nothing known about buffer `b` (until it starts again).
    void forget(int b) { track_[b & 1].complete = false; }
    // The machine's frame buffer writes and auto fills into the back buffer
    // `b`, before they are made; `draws` is how many draws its list has.
    void write(const Machine& m, int b, uint32_t off, uint32_t v, int size, bool overwrite, uint32_t draws);
    void fill(const Machine& m, int b, uint32_t addr, uint32_t words, uint16_t data, uint32_t draws);

    // Composes buffer `b` (on display) from its list with the pack's
    // replacements, and with the draws `moves` puts elsewhere (one entry per
    // draw; start_glide.h): false if nothing in it is replaced or moved, or
    // the buffer was not tracked from its start; then the emulated buffers
    // are shown. Either may be null.
    bool compose(const Machine& m, int b, const SpriteList& list, const SpritePack* pack,
                 const std::vector<SpriteMove>* moves = nullptr);
    const uint8_t* fb() const { return out_.data(); }
    const uint8_t* margin() const { return out_.data() + kPlane; }

    // The replacement graphics made so far belong to a pack; a new pack
    // starts again.
    void clear_cache() { graphics_.clear(); }

    uint64_t draws_replaced = 0;   // draws composed with a replacement
    uint64_t draws_rejected = 0;   // replacements that could not be drawn
    uint64_t draws_moved = 0;      // draws composed elsewhere (start_glide.h)

    static constexpr uint32_t kPlane = 0x20000;  // the frame buffer, then the margin shadow

private:
    struct Track {
        // Per byte of the frame buffer and of the shadow (kPlane each):
        // 0 not written, 1 a blitter wrote last, 2 something else did.
        std::vector<uint8_t> owner = std::vector<uint8_t>(2 * kPlane);
        std::vector<uint8_t> under = std::vector<uint8_t>(2 * kPlane);   // before the blitters' writes
        std::vector<uint16_t> seq = std::vector<uint16_t>(2 * kPlane);   // draws at the last other write
        bool complete = false;  // tracked since the buffer became the back buffer
    };
    struct Replay;
    const std::vector<uint8_t>* graphic_for(const SpriteDraw& d, const uint8_t* g, const SpriteImage& image);

    Track track_[2];
    std::vector<uint8_t> out_ = std::vector<uint8_t>(2 * kPlane);
    std::vector<uint8_t> replay_ = std::vector<uint8_t>(2 * kPlane);
    std::vector<uint32_t> drawn_ = std::vector<uint32_t>(2 * kPlane);   // pass a replay wrote the byte in
    std::vector<uint32_t> marked_ = std::vector<uint32_t>(2 * kPlane);  // pass the byte was marked to compose in
    std::vector<uint32_t> marks_;                                         // the marked bytes
    uint32_t pass_ = 0;
    // Replacement graphics by (image, original graphic): empty if it cannot
    // be drawn.
    struct Key {
        const SpriteImage* image;
        uint64_t hash;
        uint32_t size;
        bool operator==(const Key& o) const { return image == o.image && hash == o.hash && size == o.size; }
    };
    struct KeyHash {
        size_t operator()(const Key& k) const { return size_t(k.hash ^ (uint64_t(reinterpret_cast<uintptr_t>(k.image)) * 0x9E3779B97F4A7C15ull) ^ k.size); }
    };
    std::unordered_map<Key, std::vector<uint8_t>, KeyHash> graphics_;
};

} // namespace chaotix
