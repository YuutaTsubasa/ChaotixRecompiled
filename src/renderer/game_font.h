// The game's own menu font (issue #8), read from the player's ROM.
//
// The TRAINING and DATA LOAD screens write their zone names with an 8x16
// font of Mega Drive sprite tiles: white digits, yellow letters with a dark
// brown shade, a black drop shadow, and a few signs. It is stored
// Nemesis-compressed (the usual Sega format) at ROM 0x2C4B20, 94 tiles in
// XOR mode, and the screens colour it with the palette line at ROM 0x03E8CA
// (docs/MENU_ART.md says how this was found). Nothing of it is in this
// repository: the atlas is built from the ROM when the program starts.
//
// The menu draws its labels with it when the font has every character of a
// label; anything else (a "%", a "/") keeps the bundled pixel face for that
// label, so text never comes out with holes in it.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace chaotix {

// Decodes Nemesis-compressed tiles starting at data[0]: a header word (bit 15
// the XOR mode, the rest the tile count), the code table, then the bit
// stream. `out` gets the tiles' 32-byte patterns. False (and `why`) for a
// stream that runs past `size`, holds a code its table does not have, or
// claims more than `max_tiles` tiles. `consumed` is the stream's length.
bool nemesis_decode(const uint8_t* data, size_t size, std::vector<uint8_t>& out, size_t* consumed,
                    std::string* why, size_t max_tiles = 2048);

struct GameFont {
    static constexpr int kGlyphW = 8, kGlyphH = 16;  // every glyph is this cell, and advances by its width
    // Each glyph also comes half as tall, for the menu's small lines (the
    // notes beside its entries, the hints along its foot): the same letters,
    // each pair of rows folded into one.
    static constexpr int kSmallGlyphH = 8;
    // The glyphs side by side, glyph g at x = g * kGlyphW: the full ones in
    // the top kGlyphH rows, the small ones in the kSmallGlyphH rows below.
    // ARGB, 0 where transparent.
    int width = 0, height = 0;
    std::vector<uint32_t> pixels;
    int glyph_count = 0;
    // The glyph for each ASCII character, -1 for none. Lower case maps to
    // upper case: the font has capitals only.
    std::array<int16_t, 128> glyph{};

    bool empty() const { return glyph_count == 0; }
    int glyph_for(unsigned char c) const { return c < 128 ? glyph[c] : -1; }
    // True when every character of s has a glyph (a space always does).
    bool supports(const std::string& s) const;
    // The width of s in glyph cells' pixels (one cell a character).
    int width_of(const std::string& s) const { return int(s.size()) * kGlyphW; }
};

// ROM addresses (file offsets) of the font and its colours.
constexpr uint32_t kGameFontTiles = 0x2C4B20;
constexpr uint32_t kGameFontPalette = 0x03E8CA;
constexpr int kGameFontTileCount = 94;  // 47 glyphs of two tiles, top then bottom

// Builds the atlas from the 94 decoded tiles and the palette line (16 Mega
// Drive colour words, entry 0 transparent): the 47 glyphs of the ROM, in its
// order (0-9, A-Z, ? ! - , . and four more signs), plus ':' '<' '>' '/' made
// from the ROM's own pixels (the full stop's dot twice; triangles and a
// stroke in the letters' yellow and shadow colours), which the menu's values,
// arrows and hints need.
bool game_font_from_tiles(const std::vector<uint8_t>& tiles, const std::array<uint16_t, 16>& palette, GameFont& out,
                          std::string* why);

// Reads the font from a ROM image (game_font_from_tiles with the ROM's tiles
// and palette). False (and `why`) when the data there is not the font: a
// different or damaged ROM, which then simply keeps the pixel face.
bool build_game_font(const std::vector<uint8_t>& rom, GameFont& out, std::string* why);

} // namespace chaotix
