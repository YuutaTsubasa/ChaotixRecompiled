// The glide into a level's start (issue #136): as a level begins, the two
// characters circle the purple whirlwind, drawn larger or smaller for their
// depth, and on one frame the game puts both at their start point at full
// size -- 30 to 40 pixels away. The 32X original does the same. With the
// glide on, the picture shows them sliding there instead, over kPictures
// pictures, from where and how large they were last drawn.
//
// Like a sprite replacement (sprite_replace.h) this is output only: the
// characters' draws are composed again at other places and scales, and the
// machine's frame buffers, registers, positions and timing are the original
// ones, so replays, ghosts, netplay and lockstep are unaffected.
//
// The characters are the draws with a colour offset (sprite_record.h): 0x12
// the player's, 0x24 the partner's. A character's place is its first draw's
// hotspot and scale; its other draws (Espio's second graphic, a second set)
// move and scale with it. A jump is a picture in which a character drawn
// scaled by kMinScaleJump or more in the picture before is drawn unscaled, at
// least kMinJump pixels away. In play the partner is scaled too, as it comes
// back from the background, but its scale steps by 2 to 4 a frame to 0x100:
// that is no jump.
#pragma once
#include <cstdint>
#include <vector>

namespace chaotix {

struct SpriteList;

// A draw composed at another hotspot and scale (8.8, as SpriteDraw::sx).
struct SpriteMove {
    bool on = false;
    int32_t x = 0, y = 0, sx = 0x100, sy = 0x100;
};

class StartGlide {
public:
    static constexpr int kPictures = 16;  // about a quarter of a second
    static constexpr int kMinJump = 6;         // pixels
    static constexpr int kMinScaleJump = 0x10;  // 8.8

    // A picture went on display, with its draw list complete. `moves` gets
    // one entry per draw; true if any draw moves.
    bool picture(const SpriteList& list, std::vector<SpriteMove>& moves);
    // Whether the next pictures may glide: a character is drawn scaled, or
    // a glide is under way. The recorder follows the frame buffer writes
    // (which composing needs) only then.
    bool watching() const;
    void reset();

    uint64_t glides = 0;  // glides started

private:
    struct Place {
        bool seen = false;
        int32_t x = 0, y = 0, s = 0x100;
    };
    struct Glide {
        int step = 0;  // 0 none, else the picture of the glide (1..kPictures)
        Place from;
    };
    static Place place_of(const SpriteList& list, int who);
    Place last_[2];
    Glide glide_[2];
};

} // namespace chaotix
