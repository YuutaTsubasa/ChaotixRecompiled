// The game's giant ring, read from the player's ROM, for the menu's cursor
// (issue #134): the rings beside the chosen entry, which were drawn from
// shapes (platform/award_art.h), become the game's own pixels.
//
// The giant ring a level hides is 32X sprite bank 0x23 frames 0x29-0x2F:
// the ring's top half from front on (0x29) to edge on (0x2F), which the game
// draws twice, the second time flipped upside down under the first. The bank
// is stored packed in the ROM and unpacked into SDRAM when a scene starts
// (the SH-2 routine at 0x0600075A; docs/MENU_ART.md says how it works), so
// it is unpacked here the same way. Its colours are the palette line at ROM
// 0x001922 (32X colour words, the level's sprite colours 0-15). Nothing of it
// is in this repository: the pictures are made from the ROM when the program
// starts.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace chaotix {

// ROM addresses (file offsets) and where the ring is in its bank.
constexpr uint32_t kPortalRingPalette = 0x001922;
constexpr int kPortalRingBank = 0x23;
constexpr int kPortalRingFirstFrame = 0x29;

// Unpacks one packed graphic of a sprite bank, as the game does into SDRAM:
// a word that is the unpacked size, the byte 'B', six signed bytes (the
// base and bit width of the columns, of the lines, and of the pixels), then
// the fields most significant bit first: the box's right edge and bottom
// line, and spans (first column, end column; if they differ, the line and
// a pixel per column) until a span whose columns are equal. Non-zero
// pixels get `colour` added, as the game's colour offset. `out` gets the
// graphic in the blitters' format (docs/SPRITES_32X.md). False (and `why`)
// for data that is not a packed graphic or runs past `size`.
bool unpack_sprite_graphic(const uint8_t* data, size_t size, uint8_t colour, std::vector<uint8_t>& out,
                           std::string* why);

struct PortalRing {
    // Each frame is the whole ring, both halves, its hotspot at the centre
    // of a cell of this size; the frames side by side, frame f at
    // x = f * kCellW. ARGB, 0 where transparent.
    static constexpr int kFrames = 7;  // front on to edge on
    static constexpr int kCellW = 52, kCellH = 62;
    int width = 0, height = 0;
    std::vector<uint32_t> pixels;

    bool empty() const { return pixels.empty(); }
};

// The frame for a ring turned by `turn` (0-1, a whole turn about its upright
// axis, 0 front on), and whether it is drawn mirrored: front on to edge on,
// then mirrored back to front on, twice a turn, as the game spins it.
int portal_ring_frame(float turn, bool* mirrored);

// Reads the ring from a ROM image. False (and `why`) when the data there is
// not the ring: a different or damaged ROM, which then keeps the drawn ring.
bool build_portal_ring(const std::vector<uint8_t>& rom, PortalRing& out, std::string* why);

} // namespace chaotix
